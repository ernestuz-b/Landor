// Behavioural tests for the first real checked Map layer access:
//
//     Map::value<LayerT>(position)
//
// The exercised path is:
//
//     checked world coordinate
//         -> Cache residency
//         -> missing canonical layer Chunk
//         -> authored Placements, highest PlacementId first
//         -> terminal fallback for unresolved in-Map cells
//         -> Cache::fill<LayerT>()
//
// Real StorageFilesystem-backed .layer files in a temporary directory drive
// the authored-source behaviour, following the filesystem-test style.

#include <gtest/gtest.h>

#include "platform/storage/storage_filesystem.hpp"
#include "world/area.hpp"
#include "world/coord.hpp"
#include "world/layer_source.hpp"
#include "world/map.hpp"
#include "world/map_result.hpp"
#include "world/orientation.hpp"
#include "world/patch.hpp"
#include "world/placement.hpp"
#include "world/runtime_layer_source.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>
#include <vector>


namespace
{

using landor::geo::Area32;
using landor::geo::AuthoredLayerSourceError;
using landor::geo::Coord32;
using landor::geo::LayerBinding;
using landor::geo::LayerId;
using landor::geo::LayerSourceError;
using landor::geo::MapError;
using landor::geo::MapErrorCode;
using landor::geo::Orientation;
using landor::geo::PlacementId;
using landor::geo::Rotation;
using landor::geo::RuntimeLayerBinding;
using landor::storage::SourceId;
using landor::storage::Storage;

using Patch = landor::geo::Patch<>;


/// Byte-sized test layer.
struct Terrain
{
    static constexpr LayerId id = 1;
    using value_type = std::uint8_t;
};

static_assert(landor::geo::Layer<Terrain>);


/**
 * Terminal fallback provider whose returned value is a deterministic
 * function of the coordinate, while it records every query as test
 * instrumentation.
 */
struct RecordingFallback
{
    static constexpr std::size_t k_capacity = 64;

    template<typename LayerT>
    [[nodiscard]]
    typename LayerT::value_type
    value(Coord32 position) const
    {
        m_positions[m_count % k_capacity] = position;
        ++m_count;
        return static_cast<typename LayerT::value_type>(
            static_cast<std::int32_t>(position.x())
            + static_cast<std::int32_t>(position.y()));
    }

    [[nodiscard]] std::size_t call_count() const noexcept
    {
        return m_count;
    }

    [[nodiscard]] std::size_t calls_at(Coord32 position) const noexcept
    {
        std::size_t count = 0;
        for (std::size_t i = 0; i < m_count && i < k_capacity; ++i)
        {
            if (m_positions[i] == position)
            {
                ++count;
            }
        }
        return count;
    }

    [[nodiscard]] bool called_at(Coord32 position) const noexcept
    {
        return calls_at(position) > 0;
    }


private:
    mutable std::size_t m_count = 0;
    mutable std::array<Coord32, k_capacity> m_positions {};
};

static_assert(
    landor::geo::LayerFallbackProvider<RecordingFallback, Coord32, Terrain>);


/// Standard small test Map: 4 placement slots, 4 cache slots, 4x4 chunks.
using TestMap = landor::geo::Map<4, 4, 4, RecordingFallback, Coord32, Terrain>;

/// One-spatial-slot cache: a second chunk cannot be accepted.
using TinyCacheMap =
    landor::geo::Map<2, 1, 4, RecordingFallback, Coord32, Terrain>;

/// One-cell chunks: the smallest inspectable residency unit.
using CellMap = landor::geo::Map<2, 2, 1, RecordingFallback, Coord32, Terrain>;


/// Convert string_view row tables into owning strings so the file builder
/// can append LF terminators.
[[nodiscard]] std::vector<std::string> rows_from_views(
    std::span<const std::string_view> rows)
{
    std::vector<std::string> strings;
    strings.reserve(rows.size());
    for (const auto row : rows)
    {
        strings.emplace_back(row);
    }
    return strings;
}


/// One version 1.0 dense .layer file: metadata plus rows of exactly
/// `width` cell bytes each, LF-terminated, with the given natural position.
[[nodiscard]] std::string make_layer_file(
    std::uint32_t width,
    std::uint32_t height,
    std::int32_t natural_x,
    std::int32_t natural_y,
    std::span<const std::string> rows)
{
    std::string file = "V:1.0\n";
    file += "D:" + std::to_string(width) + " " + std::to_string(height) + "\n";
    file += "P:" + std::to_string(natural_x) + " " + std::to_string(natural_y) + "\n";
    file += "\n";
    for (const auto& row : rows)
    {
        file += row;
        file += '\n';
    }
    return file;
}


class MapValueTest : public ::testing::Test
{
protected:
    static constexpr std::size_t source_count = 9;

    void SetUp() override
    {
        std::error_code ec;
        m_root_path = std::filesystem::temp_directory_path(ec) / "landor_map_value_test";
        ASSERT_FALSE(ec) << ec.message();

        std::error_code create_error;
        const bool created =
            std::filesystem::create_directories(m_root_path, create_error);
        ASSERT_TRUE(created || !create_error) << create_error.message();

        // The Storage object stores the root non-owning, so a persistent
        // spelling of the path must outlive every Storage instance.
        m_root = m_root_path.native();
    }

    void TearDown() override
    {
        std::error_code ec;
        std::filesystem::remove_all(m_root_path, ec);
        ASSERT_FALSE(ec) << ec.message();
    }

    void write_source(std::size_t index, const std::string& contents)
    {
        std::ofstream stream(
            m_root_path / m_sources[index],
            std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(static_cast<bool>(stream));
        stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        ASSERT_TRUE(static_cast<bool>(stream));
    }

    [[nodiscard]] Storage make_storage() const
    {
        return Storage {m_root, m_sources};
    }

    [[nodiscard]] landor::geo::PatchId patch_id(std::size_t index) const noexcept
    {
        // The catalogue assigns ids 1..10 in declaration order.
        return static_cast<landor::geo::PatchId>(index + 1);
    }


protected:
    std::filesystem::path m_root_path;
    std::string m_root;

    // Dense source table; the string literals outlive the fixture.
    std::array<std::string_view, source_count> m_sources {
        "p_a.layer",
        "p_b1.layer",
        "p_b2.layer",
        "p_space.layer",
        "p_rot.layer",
        "p_bad_data.layer",
        "p_bad_dims.layer",
        "p_cell.layer",
        "p_broken.layer"
    };

    static constexpr Area32 k_area_4x4 {Coord32(0, 0), Coord32(3, 3)};
    static constexpr Area32 k_area_1x1 {Coord32(0, 0), Coord32(0, 0)};

    // Binding tables: each span outlives the Patch that borrows it.
    std::array<LayerBinding, 1> a_bindings {{Terrain::id, SourceId {0}}};
    std::array<LayerBinding, 1> b1_bindings {{Terrain::id, SourceId {1}}};
    std::array<LayerBinding, 1> b2_bindings {{Terrain::id, SourceId {2}}};
    std::array<LayerBinding, 1> space_bindings {{Terrain::id, SourceId {3}}};
    std::array<LayerBinding, 1> rot_bindings {{Terrain::id, SourceId {4}}};
    std::array<LayerBinding, 1> bad_data_bindings {{Terrain::id, SourceId {5}}};
    std::array<LayerBinding, 1> bad_dims_bindings {{Terrain::id, SourceId {6}}};
    std::array<LayerBinding, 1> cell_bindings {{Terrain::id, SourceId {7}}};
    std::array<LayerBinding, 1> broken_bindings {{Terrain::id, SourceId {8}}};

    // Catalogue, one Patch per scenario:
    //   1 a         4x4, cell (2,1) authored
    //   2 b1        4x4, cell (2,1) = 'B'
    //   3 b2        4x4, cell (2,1) = space
    //   4 space     4x4, cell (2,1) = space
    //   5 rot       4x4 asymmetric
    //   6 bad_data  4x4 declared, corrupt row 0
    //   7 bad_dims  declared 3x4 against a 4x4 local area
    //   8 cell      1x1 = 'Z'
    //   9 broken    1x1 binding over an unreadable file
    //   10 plain    4x4, no layer bindings
    std::array<Patch, 10> m_patches {
        Patch {patch_id(0), "a", Coord32(0, 0), k_area_4x4, std::span(a_bindings)},
        Patch {patch_id(1), "b1", Coord32(0, 0), k_area_4x4, std::span(b1_bindings)},
        Patch {patch_id(2), "b2", Coord32(0, 0), k_area_4x4, std::span(b2_bindings)},
        Patch {patch_id(3), "space", Coord32(0, 0), k_area_4x4, std::span(space_bindings)},
        Patch {patch_id(4), "rot", Coord32(0, 0), k_area_4x4, std::span(rot_bindings)},
        Patch {patch_id(5), "bad_data", Coord32(0, 0), k_area_4x4, std::span(bad_data_bindings)},
        Patch {patch_id(6), "bad_dims", Coord32(0, 0), k_area_4x4, std::span(bad_dims_bindings)},
        Patch {patch_id(7), "cell", Coord32(0, 0), k_area_1x1, std::span(cell_bindings)},
        Patch {patch_id(8), "broken", Coord32(0, 0), k_area_1x1, std::span(broken_bindings)},
        Patch {patch_id(9), "plain", Coord32(0, 0), k_area_4x4, std::span<const LayerBinding> {}}
    };

    // Shared 4x4 row patterns; cell (2,1) is the probe cell.
    static constexpr std::array<std::string_view, 4> rows_a {
        "qwer", "tyAu", "opis", "dfgh"
    };
    static constexpr std::array<std::string_view, 4> rows_b1 {
        "qwer", "tyBu", "opis", "dfgh"
    };
    static constexpr std::array<std::string_view, 4> rows_b2 {
        "qwer", "ty u", "opis", "dfgh"
    };
    static constexpr std::array<std::string_view, 4> rows_space {
        "qwer", "ty u", "opis", "dfgS"
    };
    static constexpr std::array<std::string_view, 4> rows_rot {
        "abcd", "efgh", "ijkl", "mnop"
    };
};


TEST_F(MapValueTest, FallbackOnlyValueComesFromTheTerminalFallback)
{
    Storage storage {m_root, m_sources};
    RecordingFallback fallback {};
    TestMap map {
        1,
        Area32(Coord32(0, 0), Coord32(15, 15)),
        std::span<const Patch> {},
        storage,
        storage,
        std::span<const RuntimeLayerBinding> {},
        fallback
    };

    const auto result = map.value<Terrain>(Coord32(2, 1));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, std::uint8_t(3));

    // The whole canonical 4x4 chunk containing the position (x 0..3, y 0..3)
    // was resolved from the terminal fallback.
    EXPECT_EQ(fallback.call_count(), 16u);
    EXPECT_TRUE(fallback.called_at(Coord32(0, 0)));
    EXPECT_TRUE(fallback.called_at(Coord32(2, 1)));
    EXPECT_TRUE(fallback.called_at(Coord32(3, 3)));
}


TEST_F(MapValueTest, SecondQueryInTheSameChunkUsesResidentState)
{
    Storage storage {m_root, m_sources};
    RecordingFallback fallback {};
    TestMap map {
        1,
        Area32(Coord32(0, 0), Coord32(15, 15)),
        std::span<const Patch> {},
        storage,
        storage,
        std::span<const RuntimeLayerBinding> {},
        fallback
    };

    const auto first = map.value<Terrain>(Coord32(0, 0));
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(*first, std::uint8_t(0));
    const std::size_t after_first = fallback.call_count();
    EXPECT_EQ(after_first, 16u);

    const auto second = map.value<Terrain>(Coord32(1, 1));
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(*second, std::uint8_t(2));

    // Resident hit: no further fallback or source work.
    EXPECT_EQ(fallback.call_count(), after_first);
}


TEST_F(MapValueTest, OutOfBoundsIsDetectedBeforeAnyResolutionWork)
{
    Storage storage {m_root, m_sources};
    RecordingFallback fallback {};
    TestMap map {
        1,
        Area32(Coord32(0, 0), Coord32(15, 15)),
        std::span<const Patch> {},
        storage,
        storage,
        std::span<const RuntimeLayerBinding> {},
        fallback
    };

    const auto below = map.value<Terrain>(Coord32(-1, 0));
    const auto beyond = map.value<Terrain>(Coord32(16, 0));

    ASSERT_FALSE(below.has_value());
    ASSERT_TRUE(std::holds_alternative<MapErrorCode>(below.error()));
    EXPECT_EQ(std::get<MapErrorCode>(below.error()), MapErrorCode::OutOfBounds);

    ASSERT_FALSE(beyond.has_value());
    ASSERT_TRUE(std::holds_alternative<MapErrorCode>(beyond.error()));
    EXPECT_EQ(std::get<MapErrorCode>(beyond.error()), MapErrorCode::OutOfBounds);

    EXPECT_EQ(fallback.call_count(), 0u);
}


TEST_F(MapValueTest, BoundaryChunkFallsBackOnlyForLogicalMapCells)
{
    Storage storage {m_root, m_sources};
    RecordingFallback fallback {};
    TestMap map {
        1,
        Area32(Coord32(0, 0), Coord32(5, 5)),
        std::span<const Patch> {},
        storage,
        storage,
        std::span<const RuntimeLayerBinding> {},
        fallback
    };

    const auto result = map.value<Terrain>(Coord32(5, 5));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, static_cast<std::uint8_t>(10));

    // The canonical chunk covers x 4..7, y 4..7; only the four cells inside
    // the Map may reach the fallback.
    EXPECT_EQ(fallback.call_count(), 4u);
    EXPECT_TRUE(fallback.called_at(Coord32(4, 4)));
    EXPECT_TRUE(fallback.called_at(Coord32(5, 4)));
    EXPECT_TRUE(fallback.called_at(Coord32(4, 5)));
    EXPECT_TRUE(fallback.called_at(Coord32(5, 5)));
    EXPECT_FALSE(fallback.called_at(Coord32(6, 4)));
    EXPECT_FALSE(fallback.called_at(Coord32(6, 5)));
    EXPECT_FALSE(fallback.called_at(Coord32(4, 6)));
    EXPECT_FALSE(fallback.called_at(Coord32(5, 6)));
    EXPECT_FALSE(fallback.called_at(Coord32(6, 6)));
    EXPECT_FALSE(fallback.called_at(Coord32(7, 7)));
}


TEST_F(MapValueTest, SecondSpatialChunkFailsWhenTheCacheHasNoFreeSlot)
{
    Storage storage {m_root, m_sources};
    RecordingFallback fallback {};
    TinyCacheMap map {
        1,
        Area32(Coord32(0, 0), Coord32(15, 15)),
        std::span<const Patch> {},
        storage,
        storage,
        std::span<const RuntimeLayerBinding> {},
        fallback
    };

    const auto first = map.value<Terrain>(Coord32(0, 0));
    ASSERT_TRUE(first.has_value());

    const auto other = map.value<Terrain>(Coord32(4, 0));
    ASSERT_FALSE(other.has_value());
    ASSERT_TRUE(std::holds_alternative<MapErrorCode>(other.error()));
    EXPECT_EQ(std::get<MapErrorCode>(other.error()), MapErrorCode::CacheFull);

    // No eviction: the first chunk is still resident and answerable.
    const auto again = map.value<Terrain>(Coord32(1, 0));
    ASSERT_TRUE(again.has_value());
    EXPECT_EQ(*again, std::uint8_t(1));
}


TEST_F(MapValueTest, AuthoredByteOverridesTheFallback)
{
    write_source(0, make_layer_file(4, 4, 0, 0, rows_from_views(rows_a)));

    Storage storage {m_root, m_sources};
    RecordingFallback fallback {};
    TestMap map {
        1,
        Area32(Coord32(0, 0), Coord32(15, 15)),
        std::span<const Patch> {m_patches},
        storage,
        storage,
        std::span<const RuntimeLayerBinding> {},
        fallback
    };

    const auto placed = map.place(patch_id(0));
    ASSERT_TRUE(placed.has_value());

    const auto result = map.value<Terrain>(Coord32(2, 1));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, static_cast<std::uint8_t>('A'));

    const auto corner = map.value<Terrain>(Coord32(0, 0));
    ASSERT_TRUE(corner.has_value());
    EXPECT_EQ(*corner, static_cast<std::uint8_t>('q'));

    // Every cell of the 4x4 chunk carries an authored contribution, so the
    // fallback saw none of them.
    for (std::int32_t y = 0; y < 4; ++y)
    {
        for (std::int32_t x = 0; x < 4; ++x)
        {
            EXPECT_FALSE(fallback.called_at(Coord32(x, y)))
                << "fallback queried at (" << x << ", " << y << ")";
        }
    }
}


TEST_F(MapValueTest, PatchWithoutALayerBindingLetsTheFallbackWin)
{
    Storage storage {m_root, m_sources};
    RecordingFallback fallback {};
    TestMap map {
        1,
        Area32(Coord32(0, 0), Coord32(15, 15)),
        std::span<const Patch> {m_patches},
        storage,
        storage,
        std::span<const RuntimeLayerBinding> {},
        fallback
    };

    const auto placed = map.place(patch_id(9), Coord32(0, 0));
    ASSERT_TRUE(placed.has_value());

    const auto result = map.value<Terrain>(Coord32(2, 1));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, std::uint8_t(3));
    EXPECT_TRUE(fallback.called_at(Coord32(2, 1)));
}


TEST_F(MapValueTest, SpaceCellContributesNothingSoTheFallbackWins)
{
    write_source(3, make_layer_file(4, 4, 0, 0, rows_from_views(rows_space)));

    Storage storage {m_root, m_sources};
    RecordingFallback fallback {};
    TestMap map {
        1,
        Area32(Coord32(0, 0), Coord32(15, 15)),
        std::span<const Patch> {m_patches},
        storage,
        storage,
        std::span<const RuntimeLayerBinding> {},
        fallback
    };

    const auto placed = map.place(patch_id(3), Coord32(0, 0));
    ASSERT_TRUE(placed.has_value());

    const auto space = map.value<Terrain>(Coord32(2, 1));
    ASSERT_TRUE(space.has_value());
    EXPECT_EQ(*space, std::uint8_t(3));

    const auto solid = map.value<Terrain>(Coord32(3, 3));
    ASSERT_TRUE(solid.has_value());
    EXPECT_EQ(*solid, static_cast<std::uint8_t>('S'));
}


TEST_F(MapValueTest, RotatedPlacementMapsSourceCellsToRotatedWorldCells)
{
    write_source(4, make_layer_file(4, 4, 0, 0, rows_from_views(rows_rot)));

    Storage storage {m_root, m_sources};
    RecordingFallback fallback {};
    TestMap map {
        1,
        Area32(Coord32(0, 0), Coord32(15, 15)),
        std::span<const Patch> {m_patches},
        storage,
        storage,
        std::span<const RuntimeLayerBinding> {},
        fallback
    };

    const auto placed = map.place(
        patch_id(4),
        Coord32(8, 8),
        Orientation {Rotation::r90});
    ASSERT_TRUE(placed.has_value());

    // r90 maps local (x, y) to local (y, -x) about the patch-local origin,
    // then translates by the placement position.
    const auto d = map.value<Terrain>(Coord32(8, 5));   // local (3, 0)
    const auto i = map.value<Terrain>(Coord32(10, 8));  // local (0, 2)

    ASSERT_TRUE(d.has_value());
    EXPECT_EQ(*d, static_cast<std::uint8_t>('d'));
    ASSERT_TRUE(i.has_value());
    EXPECT_EQ(*i, static_cast<std::uint8_t>('i'));
}


TEST_F(MapValueTest, LaterPlacementOverlaysTheEarlierOne)
{
    write_source(0, make_layer_file(4, 4, 0, 0, rows_from_views(rows_a)));
    write_source(1, make_layer_file(4, 4, 0, 0, rows_from_views(rows_b1)));

    Storage storage {m_root, m_sources};
    RecordingFallback fallback {};
    TestMap map {
        1,
        Area32(Coord32(0, 0), Coord32(15, 15)),
        std::span<const Patch> {m_patches},
        storage,
        storage,
        std::span<const RuntimeLayerBinding> {},
        fallback
    };

    const auto a = map.place(patch_id(0), Coord32(0, 0));
    const auto b = map.place(patch_id(1), Coord32(0, 0));
    ASSERT_TRUE(a.has_value());
    ASSERT_TRUE(b.has_value());

    // Both cover the cell; the later placement carries the higher
    // PlacementId and wins.
    const auto result = map.value<Terrain>(Coord32(2, 1));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, static_cast<std::uint8_t>('B'));
}


TEST_F(MapValueTest, HigherPlacementWithASpaceCellShowsTheLowerThrough)
{
    write_source(0, make_layer_file(4, 4, 0, 0, rows_from_views(rows_a)));
    write_source(2, make_layer_file(4, 4, 0, 0, rows_from_views(rows_b2)));

    Storage storage {m_root, m_sources};
    RecordingFallback fallback {};
    TestMap map {
        1,
        Area32(Coord32(0, 0), Coord32(15, 15)),
        std::span<const Patch> {m_patches},
        storage,
        storage,
        std::span<const RuntimeLayerBinding> {},
        fallback
    };

    const auto a = map.place(patch_id(0), Coord32(0, 0));
    const auto b2 = map.place(patch_id(2), Coord32(0, 0));
    ASSERT_TRUE(a.has_value());
    ASSERT_TRUE(b2.has_value());

    // The higher placement's authored cell is space, so the earlier
    // placement's contribution shows through.
    const auto result = map.value<Terrain>(Coord32(2, 1));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, static_cast<std::uint8_t>('A'));
}


TEST_F(MapValueTest, MalformedDataFromTheReaderReachesTheCallerUnchanged)
{
    std::string corrupt_row = "AB";
    corrupt_row += '\n';
    corrupt_row += 'C';
    const std::array<std::string, 4> rows {
        corrupt_row, "DEFG", "HIJK", "LMNO"
    };
    write_source(5, make_layer_file(4, 4, 0, 0, rows));

    Storage storage {m_root, m_sources};
    RecordingFallback fallback {};
    TestMap map {
        1,
        Area32(Coord32(0, 0), Coord32(15, 15)),
        std::span<const Patch> {m_patches},
        storage,
        storage,
        std::span<const RuntimeLayerBinding> {},
        fallback
    };

    const auto placed = map.place(patch_id(5), Coord32(0, 0));
    ASSERT_TRUE(placed.has_value());

    const auto result = map.value<Terrain>(Coord32(2, 0));
    ASSERT_FALSE(result.has_value());
    ASSERT_TRUE(std::holds_alternative<LayerSourceError>(result.error()));
    EXPECT_EQ(
        std::get<LayerSourceError>(result.error()),
        LayerSourceError::MalformedData);
}


TEST_F(MapValueTest, PatchSourceGeometryMismatchReachesTheCallerUnchanged)
{
    // A self-consistent 3x4 file: it opens and parses, but its declared
    // width disagrees with the 4x4 Patch local area.
    const std::array<std::string, 4> rows {"abc", "def", "ghi", "jkl"};
    write_source(6, make_layer_file(3, 4, 0, 0, rows));

    Storage storage {m_root, m_sources};
    RecordingFallback fallback {};
    TestMap map {
        1,
        Area32(Coord32(0, 0), Coord32(15, 15)),
        std::span<const Patch> {m_patches},
        storage,
        storage,
        std::span<const RuntimeLayerBinding> {},
        fallback
    };

    const auto placed = map.place(patch_id(6), Coord32(0, 0));
    ASSERT_TRUE(placed.has_value());

    // The file opens and parses; the 3x4 declared geometry disagrees with
    // the 4x4 Patch local area.
    const auto result = map.value<Terrain>(Coord32(0, 0));
    ASSERT_FALSE(result.has_value());
    ASSERT_TRUE(
        std::holds_alternative<AuthoredLayerSourceError>(result.error()));
    EXPECT_EQ(
        std::get<AuthoredLayerSourceError>(result.error()),
        AuthoredLayerSourceError::DimensionMismatch);
}


TEST_F(MapValueTest, CompletelyHiddenLowerSourceIsNeverOpened)
{
    const std::array<std::string, 1> cell_rows {"Z"};
    write_source(7, make_layer_file(1, 1, 0, 0, cell_rows));
    write_source(8, std::string("not a layer source at all"));

    Storage storage {m_root, m_sources};
    RecordingFallback fallback {};
    CellMap map {
        1,
        Area32(Coord32(0, 0), Coord32(0, 0)),
        std::span<const Patch> {m_patches},
        storage,
        storage,
        std::span<const RuntimeLayerBinding> {},
        fallback
    };

    // The lower (broken) placement is created first; the valid placement
    // receives the higher PlacementId and resolves the only cell.
    const auto lower = map.place(patch_id(8), Coord32(0, 0));
    const auto higher = map.place(patch_id(7), Coord32(0, 0));
    ASSERT_TRUE(lower.has_value());
    ASSERT_TRUE(higher.has_value());

    const auto result = map.value<Terrain>(Coord32(0, 0));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, static_cast<std::uint8_t>('Z'));
}


TEST_F(MapValueTest, MovingTheWinningPlacementInvalidatesTheResidentChunk)
{
    write_source(0, make_layer_file(4, 4, 0, 0, rows_from_views(rows_a)));

    Storage storage {m_root, m_sources};
    RecordingFallback fallback {};
    TestMap map {
        1,
        Area32(Coord32(0, 0), Coord32(15, 15)),
        std::span<const Patch> {m_patches},
        storage,
        storage,
        std::span<const RuntimeLayerBinding> {},
        fallback
    };

    const auto placed = map.place(patch_id(0), Coord32(0, 0));
    ASSERT_TRUE(placed.has_value());

    const auto authored = map.value<Terrain>(Coord32(2, 1));
    ASSERT_TRUE(authored.has_value());
    EXPECT_EQ(*authored, static_cast<std::uint8_t>('A'));

    // Move the winning placement away from the chunk.
    ASSERT_TRUE(map.set_position(*placed, Coord32(8, 0)));

    const auto stale = map.value<Terrain>(Coord32(2, 1));
    ASSERT_TRUE(stale.has_value());
    EXPECT_EQ(*stale, std::uint8_t(3));   // fallback, not the stale 'A'

    const auto moved = map.value<Terrain>(Coord32(10, 1));
    ASSERT_TRUE(moved.has_value());
    EXPECT_EQ(*moved, static_cast<std::uint8_t>('A'));
}

} // namespace

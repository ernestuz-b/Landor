// Behavioural tests for the first real mutable world-state path:
//
//     Map::set<LayerT>(position, value)
//         -> checked coordinate
//         -> ensure the layer Chunk is resident
//         -> mutate the resident Cache value
//         -> mark that resident layer plane dirty
//
// The pinned contract:
//
// - The bounds check happens before any Cache, source or fallback work.
// - A changed value makes the whole resident layer plane dirty; the dirty
//   resident value is authoritative for subsequent reads of that Chunk
//   without rereading runtime, authored or fallback state.
// - A same-value set succeeds and does not turn a clean plane dirty.
// - A layer needs no runtime binding to acquire live state.
// - No Storage write happens: the backing runtime and authored files remain
//   byte-for-byte unchanged after a set().
// - Resolution failures before mutation propagate the exact existing source
//   error and create no dirty state.
//
// Real StorageFilesystem-backed .layer files in a temporary directory drive
// the source behaviour, following the filesystem-test style.

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
#include <iterator>
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
using landor::geo::PlacementId;
using landor::geo::Rotation;
using landor::geo::RuntimeLayerBinding;
using landor::storage::SourceId;
using landor::storage::Storage;

using Patch = landor::geo::Patch<>;


/// Byte-sized test layers.
struct Terrain
{
    static constexpr LayerId id = 1;
    using value_type = std::uint8_t;
};

struct Fire
{
    static constexpr LayerId id = 3;
    using value_type = std::uint8_t;
};

static_assert(landor::geo::Layer<Terrain>);
static_assert(landor::geo::Layer<Fire>);

inline constexpr Terrain terrain {};
inline constexpr Fire fire {};


/**
 * Terminal fallback provider whose returned value is a deterministic
 * function of the coordinate, while it records every query as test
 * instrumentation.
 */
struct RecordingFallback
{
    static constexpr std::size_t k_capacity = 128;

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

    [[nodiscard]] bool called_at(Coord32 position) const noexcept
    {
        for (std::size_t i = 0; i < m_count && i < k_capacity; ++i)
        {
            if (m_positions[i] == position)
            {
                return true;
            }
        }

        return false;
    }


private:
    mutable std::size_t m_count = 0;
    mutable std::array<Coord32, k_capacity> m_positions {};
};

static_assert(
    landor::geo::LayerFallbackProvider<RecordingFallback, Coord32, Terrain>);
static_assert(
    landor::geo::LayerFallbackProvider<RecordingFallback, Coord32, Terrain, Fire>);


/// Standard small test Map: 4 placement slots, 4 cache slots, 4x4 chunks.
using TestMap = landor::geo::Map<4, 4, 4, RecordingFallback, Coord32, Terrain>;

/// Two-layer Map for at() packing checks.
using MultiLayerMap =
    landor::geo::Map<4, 4, 4, RecordingFallback, Coord32, Terrain, Fire>;

/// One-spatial-slot cache: a second chunk cannot be accepted.
using TinyCacheMap =
    landor::geo::Map<2, 1, 4, RecordingFallback, Coord32, Terrain>;


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


/// The 16x16 runtime overlay source for the 16x16 test Map area: all space
/// except local (2, 1) = 'R'.
[[nodiscard]] std::string make_runtime_layer_file()
{
    std::vector<std::string> rows;
    for (int y = 0; y < 16; ++y)
    {
        std::string row(16, ' ');
        if (y == 1)
        {
            row[2] = 'R';
        }
        rows.push_back(row);
    }

    return make_layer_file(16, 16, 0, 0, rows);
}


[[nodiscard]] bool file_matches(const std::filesystem::path& path, const std::string& expected)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
    {
        return false;
    }

    std::string actual(
        (std::istreambuf_iterator<char>(stream)),
        std::istreambuf_iterator<char>());
    return actual == expected;
}


class MapMutationTest : public ::testing::Test
{
protected:
    static constexpr std::size_t source_count = 2;

    void SetUp() override
    {
        std::error_code ec;
        m_root_path = std::filesystem::temp_directory_path(ec) / "landor_map_mutation_test";
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


protected:
    std::filesystem::path m_root_path;
    std::string m_root;

    // Dense source table; the string literals outlive the fixture.
    std::array<std::string_view, source_count> m_sources {
        "a.layer",
        "rt.layer"
    };

    static constexpr Area32 k_area_16x16 {Coord32(0, 0), Coord32(15, 15)};
    static constexpr Area32 k_area_4x4 {Coord32(0, 0), Coord32(3, 3)};
    static constexpr Coord32 probe = Coord32(2, 1);

    std::array<LayerBinding, 1> a_bindings {{Terrain::id, SourceId {0}}};

    // Catalogue, one Patch per scenario:
    //   1 a      4x4, authored cell (2,1) = 'A' when a.layer exists
    //   2 plain  4x4, no layer bindings
    std::array<Patch, 2> m_patches {
        Patch {1, "a", Coord32(0, 0), k_area_4x4, std::span(a_bindings)},
        Patch {2, "plain", Coord32(0, 0), k_area_4x4, std::span<const LayerBinding> {}}
    };

    // Shared 4x4 authored row pattern; cell (2,1) is the probe cell.
    static constexpr std::array<std::string_view, 4> rows_a {
        "qwer", "tyAu", "opis", "dfgh"
    };

    [[nodiscard]] std::string authored_layer_contents() const
    {
        std::vector<std::string> rows;
        for (const auto row : rows_a)
        {
            rows.emplace_back(row);
        }

        return make_layer_file(4, 4, 0, 0, rows);
    }
};


} // namespace


// ---------------------------------------------------------------------------
// Fallback, runtime and authored backing
// ---------------------------------------------------------------------------

TEST_F(MapMutationTest, FallbackBackedMutationWorksWithoutARuntimeBinding)
{
    Storage storage {m_root, m_sources};
    RecordingFallback fallback {};
    TestMap map {
        1,
        k_area_16x16,
        std::span<const Patch> {},
        storage,
        storage,
        std::span<const RuntimeLayerBinding> {},
        fallback
    };

    const auto initial = map.value<Terrain>(probe);
    ASSERT_TRUE(initial.has_value());
    EXPECT_EQ(*initial, std::uint8_t(3));

    // No runtime binding and no authored contribution: a plain live mutation.
    ASSERT_TRUE(map.set<Terrain>(probe, 99).has_value());

    const auto after = map.value<Terrain>(probe);
    ASSERT_TRUE(after.has_value());
    EXPECT_EQ(*after, 99);
}


TEST_F(MapMutationTest, RuntimeBackedMutationOverridesTheRuntimeSource)
{
    const std::string runtime_contents = make_runtime_layer_file();
    write_source(1, runtime_contents);

    const std::array<RuntimeLayerBinding, 1> runtime_bindings {
        RuntimeLayerBinding {Terrain::id, SourceId {1}}
    };

    Storage storage {m_root, m_sources};
    RecordingFallback fallback {};
    TestMap map {
        1,
        k_area_16x16,
        std::span<const Patch> {},
        storage,
        storage,
        std::span<const RuntimeLayerBinding>(runtime_bindings),
        fallback
    };

    const auto resolved = map.value<Terrain>(probe);
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(*resolved, static_cast<std::uint8_t>('R'));
    EXPECT_FALSE(fallback.called_at(probe));

    ASSERT_TRUE(map.set<Terrain>(probe, 'M').has_value());

    const auto live = map.value<Terrain>(probe);
    ASSERT_TRUE(live.has_value());
    EXPECT_EQ(*live, static_cast<std::uint8_t>('M'));

    // No write-back exists yet: the runtime file is byte-for-byte unchanged.
    EXPECT_TRUE(file_matches(m_root_path / m_sources[1], runtime_contents));
}


TEST_F(MapMutationTest, AuthoredBackedMutationOverridesTheAuthoredSource)
{
    const std::string authored_contents = authored_layer_contents();
    write_source(0, authored_contents);

    Storage storage {m_root, m_sources};
    RecordingFallback fallback {};
    TestMap map {
        1,
        k_area_16x16,
        std::span<const Patch> {m_patches},
        storage,
        storage,
        std::span<const RuntimeLayerBinding> {},
        fallback
    };

    const auto placed = map.place(1);
    ASSERT_TRUE(placed.has_value());

    const auto resolved = map.value<Terrain>(probe);
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(*resolved, static_cast<std::uint8_t>('A'));
    EXPECT_FALSE(fallback.called_at(probe));

    ASSERT_TRUE(map.set<Terrain>(probe, 'M').has_value());

    const auto live = map.value<Terrain>(probe);
    ASSERT_TRUE(live.has_value());
    EXPECT_EQ(*live, static_cast<std::uint8_t>('M'));

    // The authored source is immutable input and remains untouched.
    EXPECT_TRUE(file_matches(m_root_path / m_sources[0], authored_contents));
}


// ---------------------------------------------------------------------------
// Live-state behaviour
// ---------------------------------------------------------------------------

TEST_F(MapMutationTest, AtPacksTheMutatedLiveValue)
{
    Storage storage {m_root, m_sources};
    RecordingFallback fallback {};
    MultiLayerMap map {
        1,
        k_area_16x16,
        std::span<const Patch> {},
        storage,
        storage,
        std::span<const RuntimeLayerBinding> {},
        fallback
    };

    ASSERT_TRUE(map.set<Terrain>(probe, 99).has_value());

    const auto tile = map.at(probe);
    ASSERT_TRUE(tile.has_value());
    EXPECT_EQ((*tile)[terrain], 99);
    EXPECT_EQ((*tile)[fire], std::uint8_t(3));
}


TEST_F(MapMutationTest, MutationStaysLocalToTheCell)
{
    Storage storage {m_root, m_sources};
    RecordingFallback fallback {};
    TestMap map {
        1,
        k_area_16x16,
        std::span<const Patch> {},
        storage,
        storage,
        std::span<const RuntimeLayerBinding> {},
        fallback
    };

    ASSERT_TRUE(map.value<Terrain>(probe).has_value());

    ASSERT_TRUE(map.set<Terrain>(probe, 99).has_value());

    const auto mutated = map.value<Terrain>(probe);
    ASSERT_TRUE(mutated.has_value());
    EXPECT_EQ(*mutated, 99);

    // Neighbours in the same resident plane keep their resolved values.
    const auto other = map.value<Terrain>(Coord32(3, 3));
    ASSERT_TRUE(other.has_value());
    EXPECT_EQ(*other, std::uint8_t(6));

    const auto corner = map.value<Terrain>(Coord32(0, 0));
    ASSERT_TRUE(corner.has_value());
    EXPECT_EQ(*corner, std::uint8_t(0));
}


TEST_F(MapMutationTest, SameValueMutationCreatesNoDirtyState)
{
    Storage storage {m_root, m_sources};
    RecordingFallback fallback {};
    TestMap map {
        1,
        k_area_16x16,
        std::span<const Patch> {m_patches},
        storage,
        storage,
        std::span<const RuntimeLayerBinding> {},
        fallback
    };

    const auto resolved = map.value<Terrain>(probe);
    ASSERT_TRUE(resolved.has_value());

    // Setting exactly the current live value must not create dirty state.
    ASSERT_TRUE(map.set<Terrain>(probe, *resolved).has_value());

    // An operation that requires clean invalidation is still allowed.
    ASSERT_TRUE(map.place(2).has_value());
}


TEST_F(MapMutationTest, OutOfBoundsSetFailsBeforeAnyResolutionWork)
{
    Storage storage {m_root, m_sources};
    RecordingFallback fallback {};
    TestMap map {
        1,
        k_area_16x16,
        std::span<const Patch> {},
        storage,
        storage,
        std::span<const RuntimeLayerBinding> {},
        fallback
    };

    const auto below = map.set<Terrain>(Coord32(-1, 0), 99);
    const auto beyond = map.set<Terrain>(Coord32(16, 0), 99);

    ASSERT_FALSE(below.has_value());
    ASSERT_TRUE(std::holds_alternative<MapErrorCode>(below.error()));
    EXPECT_EQ(std::get<MapErrorCode>(below.error()), MapErrorCode::OutOfBounds);

    ASSERT_FALSE(beyond.has_value());
    ASSERT_TRUE(std::holds_alternative<MapErrorCode>(beyond.error()));
    EXPECT_EQ(std::get<MapErrorCode>(beyond.error()), MapErrorCode::OutOfBounds);

    EXPECT_EQ(fallback.call_count(), 0u);
}


TEST_F(MapMutationTest, SourceFailurePropagatesWithoutCreatingDirtyState)
{
    // A self-consistent file whose row 0 data is corrupt: it opens and
    // parses, then fails the first cell read.
    std::string corrupt_row = "AB";
    corrupt_row += '\n';
    corrupt_row += 'C';
    const std::array<std::string, 4> rows {
        corrupt_row, "DEFG", "HIJK", "LMNO"
    };
    write_source(0, make_layer_file(4, 4, 0, 0, rows));

    Storage storage {m_root, m_sources};
    RecordingFallback fallback {};
    TestMap map {
        1,
        k_area_16x16,
        std::span<const Patch> {m_patches},
        storage,
        storage,
        std::span<const RuntimeLayerBinding> {},
        fallback
    };

    const auto placed = map.place(1);
    ASSERT_TRUE(placed.has_value());

    const auto blocked = map.set<Terrain>(Coord32(2, 0), 99);

    ASSERT_FALSE(blocked.has_value());
    ASSERT_TRUE(std::holds_alternative<LayerSourceError>(blocked.error()));
    EXPECT_EQ(
        std::get<LayerSourceError>(blocked.error()),
        LayerSourceError::MalformedData);
    EXPECT_EQ(fallback.call_count(), 0u);

    // No dirty state was created: an operation that requires clean
    // invalidation is still allowed.
    ASSERT_TRUE(map.place(2).has_value());
}


TEST_F(MapMutationTest, CacheFullWhenMutationNeedsANewSpatialSlot)
{
    Storage storage {m_root, m_sources};
    RecordingFallback fallback {};
    TinyCacheMap map {
        1,
        k_area_16x16,
        std::span<const Patch> {},
        storage,
        storage,
        std::span<const RuntimeLayerBinding> {},
        fallback
    };

    const auto first = map.value<Terrain>(Coord32(0, 0));
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(*first, std::uint8_t(0));

    // The mutation would need a second spatial slot; the Cache has none.
    const auto blocked = map.set<Terrain>(Coord32(4, 0), 99);
    ASSERT_FALSE(blocked.has_value());
    ASSERT_TRUE(std::holds_alternative<MapErrorCode>(blocked.error()));
    EXPECT_EQ(std::get<MapErrorCode>(blocked.error()), MapErrorCode::CacheFull);

    // No eviction: the existing resident state is unchanged and answerable.
    const auto again = map.value<Terrain>(Coord32(1, 0));
    ASSERT_TRUE(again.has_value());
    EXPECT_EQ(*again, std::uint8_t(1));
}

// Behavioural tests for the checked multi-layer Map tile access:
//
//     Map::at(position)   /   Map::at(x, y)
//
// The exercised path is:
//
//     checked world coordinate (OutOfBounds before any other work)
//         -> every supported layer resident, in the declared template order
//         -> fail-fast, exact first MapError
//         -> Cache::tile() packs the owned Tile value
//
// Real StorageFilesystem-backed .layer files in a temporary directory drive
// the authored-source behaviour, following the filesystem-test style.

#include <gtest/gtest.h>

#include "platform/storage/storage_filesystem.hpp"
#include "world/area.hpp"
#include "world/coord.hpp"
#include "world/layer.hpp"
#include "world/map.hpp"
#include "world/map_result.hpp"
#include "world/patch.hpp"
#include "world/tile.hpp"

#include <array>
#include <cassert>
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
using landor::geo::PlacementId;
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
    static constexpr LayerId id = 2;
    using value_type = std::uint8_t;
};

struct Water
{
    static constexpr LayerId id = 3;
    using value_type = std::uint8_t;
};

static_assert(landor::geo::Layer<Terrain>);
static_assert(landor::geo::Layer<Fire>);
static_assert(landor::geo::Layer<Water>);

/// Empty tag objects for symbolic `tile[layer]` access.
inline constexpr Terrain terrain {};
inline constexpr Fire fire {};
inline constexpr Water water {};


/**
 * Terminal fallback provider whose returned value is a deterministic
 * function of both the layer and the coordinate, while it records every
 * query per layer as test instrumentation.
 *
 * value = LayerT::id * 10 + x + y (byte-sized layers), so every layer
 * answers with a distinct deterministic baseline.
 */
struct RecordingFallback
{
    static constexpr std::size_t k_max_layer_id = 4;

    template<typename LayerT>
    [[nodiscard]]
    typename LayerT::value_type
    value(Coord32 position) const
    {
        assert(LayerT::id < k_max_layer_id);
        ++m_calls[LayerT::id];
        ++m_count;
        const std::int32_t value =
            static_cast<std::int32_t>(LayerT::id) * 10 + position.x() + position.y();
        return static_cast<typename LayerT::value_type>(value);
    }

    [[nodiscard]] std::size_t calls_for(LayerId id) const noexcept
    {
        return m_calls[id];
    }

    [[nodiscard]] std::size_t total_calls() const noexcept
    {
        return m_count;
    }


private:
    mutable std::array<std::size_t, k_max_layer_id> m_calls {};
    mutable std::size_t m_count = 0;
};

static_assert(
    landor::geo::LayerFallbackProvider<RecordingFallback, Coord32, Terrain, Fire>);
static_assert(
    landor::geo::LayerFallbackProvider<RecordingFallback, Coord32, Terrain, Fire, Water>);
static_assert(
    landor::geo::LayerFallbackProvider<RecordingFallback, Coord32, Terrain, Water, Fire>);


/// Standard small two-layer test Map: 4 placement slots, 4 cache slots,
/// 4x4 chunks.
using TestMap = landor::geo::Map<4, 4, 4, RecordingFallback, Coord32, Terrain, Fire>;

/// One-spatial-slot cache: a second chunk cannot be accepted.
using TinyCacheMap =
    landor::geo::Map<2, 1, 4, RecordingFallback, Coord32, Terrain, Fire>;

/// Three-layer Map whose declared order matches the layer ids.
using ThreeLayerMap =
    landor::geo::Map<4, 4, 4, RecordingFallback, Coord32, Terrain, Fire, Water>;

/// Three-layer Map whose declared order disagrees with the layer ids
/// (Water 3 is declared before Fire 2): multi-layer residency must follow
/// the declared template order, not a LayerId sort.
using ReorderedThreeLayerMap =
    landor::geo::Map<4, 4, 4, RecordingFallback, Coord32, Terrain, Water, Fire>;


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


class MapAtTest : public ::testing::Test
{
protected:
    static constexpr std::size_t source_count = 9;

    void SetUp() override
    {
        std::error_code ec;
        m_root_path = std::filesystem::temp_directory_path(ec) / "landor_map_at_test";
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

    [[nodiscard]] landor::geo::PatchId patch_id(std::size_t index) const noexcept
    {
        // The catalogue assigns ids 1..8 in declaration order.
        return static_cast<landor::geo::PatchId>(index + 1);
    }


protected:
    std::filesystem::path m_root_path;
    std::string m_root;

    // Dense source table; the string literals outlive the fixture.
    std::array<std::string_view, source_count> m_sources {
        "terr_a.layer",      // 0: Terrain 'A' at (2,1)
        "terr_b.layer",      // 1: Terrain 'B' at (2,1)
        "terr_t.layer",      // 2: Terrain 'T' at (2,1)
        "fire_g.layer",      // 3: Fire 'G' at (2,1)
        "fire_f.layer",      // 4: Fire 'F' at (2,1)
        "fire_space.layer",  // 5: Fire space at (2,1)
        "fire_bad.layer",    // 6: corrupt Fire (row 0)
        "terr_m.layer",      // 7: Terrain 'M' at (2,1)
        "fire_m.layer"       // 8: Fire 'M' at (2,1)
    };

    static constexpr Area32 k_area_4x4 {Coord32(0, 0), Coord32(3, 3)};

    // Binding tables: each span outlives the Patch that borrows it.
    std::array<LayerBinding, 1> a_bindings {{Terrain::id, SourceId {0}}};
    std::array<LayerBinding, 1> b_bindings {{Terrain::id, SourceId {1}}};
    std::array<LayerBinding, 1> t_bindings {{Terrain::id, SourceId {2}}};
    std::array<LayerBinding, 2> bg_bindings {
        LayerBinding {Terrain::id, SourceId {1}},
        LayerBinding {Fire::id, SourceId {3}}
    };
    std::array<LayerBinding, 2> as_bindings {
        LayerBinding {Terrain::id, SourceId {0}},
        LayerBinding {Fire::id, SourceId {5}}
    };
    std::array<LayerBinding, 1> bad_bindings {LayerBinding {Fire::id, SourceId {6}}};
    std::array<LayerBinding, 2> mm_bindings {
        LayerBinding {Terrain::id, SourceId {7}},
        LayerBinding {Fire::id, SourceId {8}}
    };

    // Catalogue, one Patch per scenario:
    //   1 terr_a   4x4 Terrain 'A'
    //   2 terr_b   4x4 Terrain 'B'
    //   3 terr_t   4x4 Terrain 'T'
    //   4 bg       4x4 Terrain 'B' + Fire 'G'
    //   5 as       4x4 Terrain 'A' + Fire space
    //   6 fire_bad 4x4 corrupt Fire
    //   7 mm       4x4 Terrain 'M' + Fire 'M'
    //   8 plain    4x4, no layer bindings
    std::array<Patch, 8> m_patches {
        Patch {patch_id(0), "terr_a", Coord32(0, 0), k_area_4x4, std::span(a_bindings)},
        Patch {patch_id(1), "terr_b", Coord32(0, 0), k_area_4x4, std::span(b_bindings)},
        Patch {patch_id(2), "terr_t", Coord32(0, 0), k_area_4x4, std::span(t_bindings)},
        Patch {patch_id(3), "bg", Coord32(0, 0), k_area_4x4, std::span(bg_bindings)},
        Patch {patch_id(4), "as", Coord32(0, 0), k_area_4x4, std::span(as_bindings)},
        Patch {patch_id(5), "fire_bad", Coord32(0, 0), k_area_4x4, std::span(bad_bindings)},
        Patch {patch_id(6), "mm", Coord32(0, 0), k_area_4x4, std::span(mm_bindings)},
        Patch {patch_id(7), "plain", Coord32(0, 0), k_area_4x4, std::span<const LayerBinding> {}}
    };

    // Shared 4x4 row patterns; cell (2,1) is the probe cell.
    static constexpr std::array<std::string_view, 4> rows_a {
        "qwer", "tyAu", "opis", "dfgh"
    };
    static constexpr std::array<std::string_view, 4> rows_b {
        "qwer", "tyBu", "opis", "dfgh"
    };
    static constexpr std::array<std::string_view, 4> rows_t {
        "qwer", "tyTu", "opis", "dfgh"
    };
    static constexpr std::array<std::string_view, 4> rows_m {
        "qwer", "tyMu", "opis", "dfgh"
    };
    static constexpr std::array<std::string_view, 4> rows_g {
        "asdf", "kjGl", "zxcv", "qwer"
    };
    static constexpr std::array<std::string_view, 4> rows_f {
        "asdf", "kjFl", "zxcv", "qwer"
    };
    static constexpr std::array<std::string_view, 4> rows_fire_space {
        "asdf", "kj l", "zxcv", "qwer"
    };
    static constexpr std::array<std::string_view, 4> rows_f_m {
        "asdf", "kjMl", "zxcv", "qwer"
    };
};


TEST_F(MapAtTest, FallbackOnlyTileAssemblesEveryLayer)
{
    Storage storage {m_root, m_sources};
    RecordingFallback fallback {};
    TestMap map {
        1,
        Area32(Coord32(0, 0), Coord32(15, 15)),
        std::span<const Patch> {},
        storage,
        fallback
    };

    // Scalar convenience overload: it forwards to at(coord_type).
    const auto result = map.at(2, 1);

    ASSERT_TRUE(result.has_value());
    const auto& tile = *result;

    // at() assembled a real Tile, not a single layer: the coordinate is the
    // identity and both layers carry their distinct fallback baselines.
    EXPECT_EQ(tile.position(), Coord32(2, 1));
    EXPECT_EQ(tile[terrain], std::uint8_t(13));   // 1 * 10 + 2 + 1
    EXPECT_EQ(tile[fire], std::uint8_t(23));      // 2 * 10 + 2 + 1

    // The whole canonical 4x4 chunk was resolved per layer.
    EXPECT_EQ(fallback.calls_for(Terrain::id), 16u);
    EXPECT_EQ(fallback.calls_for(Fire::id), 16u);
}


TEST_F(MapAtTest, OutOfBoundsIsDetectedBeforeAnyResolutionWork)
{
    // A corrupt authored source is placed over the in-Map chunk: if the
    // bounds check did not come first, the source would be opened and the
    // failure would be a source error, not OutOfBounds.
    const std::string corrupt_row = "AB\nC";
    const std::array<std::string, 4> rows {
        corrupt_row, "DEFG", "HIJK", "LMNO"
    };
    write_source(6, make_layer_file(4, 4, 0, 0, rows));

    Storage storage {m_root, m_sources};
    RecordingFallback fallback {};
    TestMap map {
        1,
        Area32(Coord32(0, 0), Coord32(15, 15)),
        std::span<const Patch> {m_patches},
        storage,
        fallback
    };

    const auto placed = map.place(patch_id(5), Coord32(0, 0));
    ASSERT_TRUE(placed.has_value());

    const auto below = map.at(Coord32(-1, 0));
    const auto beyond = map.at(16, 0);

    ASSERT_FALSE(below.has_value());
    ASSERT_TRUE(std::holds_alternative<MapErrorCode>(below.error()));
    EXPECT_EQ(std::get<MapErrorCode>(below.error()), MapErrorCode::OutOfBounds);

    ASSERT_FALSE(beyond.has_value());
    ASSERT_TRUE(std::holds_alternative<MapErrorCode>(beyond.error()));
    EXPECT_EQ(std::get<MapErrorCode>(beyond.error()), MapErrorCode::OutOfBounds);

    // No fallback and no source I/O: the corrupt source was never opened and
    // the terminal fallback was never queried.
    EXPECT_EQ(fallback.total_calls(), 0u);
}


TEST_F(MapAtTest, ExistingLayerResidencyIsReused)
{
    Storage storage {m_root, m_sources};
    RecordingFallback fallback {};
    TestMap map {
        1,
        Area32(Coord32(0, 0), Coord32(15, 15)),
        std::span<const Patch> {},
        storage,
        fallback
    };

    // Terrain becomes resident through the single-layer API first.
    const auto value = map.value<Terrain>(Coord32(2, 1));
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(*value, std::uint8_t(13));
    EXPECT_EQ(fallback.calls_for(Terrain::id), 16u);
    EXPECT_EQ(fallback.calls_for(Fire::id), 0u);

    // at() must resolve only the missing layer, never re-resolve Terrain.
    const auto result = map.at(Coord32(2, 1));
    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(fallback.calls_for(Terrain::id), 16u);   // not regenerated
    EXPECT_EQ(fallback.calls_for(Fire::id), 16u);     // only Fire resolved
    EXPECT_EQ((*result)[terrain], std::uint8_t(13));
    EXPECT_EQ((*result)[fire], std::uint8_t(23));
}


TEST_F(MapAtTest, SecondAtCallPacksFromResidentState)
{
    Storage storage {m_root, m_sources};
    RecordingFallback fallback {};
    TestMap map {
        1,
        Area32(Coord32(0, 0), Coord32(15, 15)),
        std::span<const Patch> {},
        storage,
        fallback
    };

    const auto first = map.at(Coord32(2, 1));
    ASSERT_TRUE(first.has_value());
    const std::size_t after_first = fallback.total_calls();

    // Fully resident: the second call performs no further resolution work
    // and simply packs another Tile value from the Cache.
    const auto second = map.at(Coord32(2, 1));
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(fallback.total_calls(), after_first);
    EXPECT_EQ(*second, *first);
}


TEST_F(MapAtTest, AuthoredLayerDoesNotHideTheOther)
{
    // The patch supplies Terrain only; Fire is absent from the authored
    // content and must come from the terminal fallback.
    write_source(0, make_layer_file(4, 4, 0, 0, rows_from_views(rows_a)));

    Storage storage {m_root, m_sources};
    RecordingFallback fallback {};
    TestMap map {
        1,
        Area32(Coord32(0, 0), Coord32(15, 15)),
        std::span<const Patch> {m_patches},
        storage,
        fallback
    };

    const auto placed = map.place(patch_id(0), Coord32(0, 0));
    ASSERT_TRUE(placed.has_value());

    const auto result = map.at(Coord32(2, 1));
    ASSERT_TRUE(result.has_value());

    // The complete Tile carries the authored Terrain byte and the fallback
    // Fire baseline at the same coordinate.
    EXPECT_EQ((*result)[terrain], static_cast<std::uint8_t>('A'));
    EXPECT_EQ((*result)[fire], std::uint8_t(23));

    // The chunk is fully authored for Terrain, so only Fire reached the
    // fallback.
    EXPECT_EQ(fallback.calls_for(Terrain::id), 0u);
    EXPECT_EQ(fallback.calls_for(Fire::id), 16u);
}


TEST_F(MapAtTest, PerLayerPrecedenceIsResolvedIndependently)
{
    // Older placement supplies both layers; the newer placement supplies
    // Terrain only. At the overlap, Terrain comes from the newer placement
    // and Fire from the older one: precedence is per layer, and there is no
    // Tile-level precedence concept.
    write_source(1, make_layer_file(4, 4, 0, 0, rows_from_views(rows_b)));
    write_source(2, make_layer_file(4, 4, 0, 0, rows_from_views(rows_t)));
    write_source(3, make_layer_file(4, 4, 0, 0, rows_from_views(rows_g)));

    Storage storage {m_root, m_sources};
    RecordingFallback fallback {};
    TestMap map {
        1,
        Area32(Coord32(0, 0), Coord32(15, 15)),
        std::span<const Patch> {m_patches},
        storage,
        fallback
    };

    const auto older = map.place(patch_id(3), Coord32(0, 0));
    const auto newer = map.place(patch_id(2), Coord32(0, 0));
    ASSERT_TRUE(older.has_value());
    ASSERT_TRUE(newer.has_value());

    const auto result = map.at(Coord32(2, 1));
    ASSERT_TRUE(result.has_value());

    EXPECT_EQ((*result)[terrain], static_cast<std::uint8_t>('T'));   // newer
    EXPECT_EQ((*result)[fire], static_cast<std::uint8_t>('G'));      // older
}


TEST_F(MapAtTest, SpaceCellFallsThroughPerLayer)
{
    // The higher-priority placement carries a real Terrain byte but an
    // ASCII-space Fire cell. The two layers resolve independently: Terrain
    // comes from the higher placement, Fire falls through to the lower one.
    write_source(0, make_layer_file(4, 4, 0, 0, rows_from_views(rows_a)));
    write_source(1, make_layer_file(4, 4, 0, 0, rows_from_views(rows_b)));
    write_source(3, make_layer_file(4, 4, 0, 0, rows_from_views(rows_g)));
    write_source(5, make_layer_file(4, 4, 0, 0, rows_from_views(rows_fire_space)));

    Storage storage {m_root, m_sources};
    RecordingFallback fallback {};
    TestMap map {
        1,
        Area32(Coord32(0, 0), Coord32(15, 15)),
        std::span<const Patch> {m_patches},
        storage,
        fallback
    };

    const auto lower = map.place(patch_id(3), Coord32(0, 0));
    const auto higher = map.place(patch_id(4), Coord32(0, 0));
    ASSERT_TRUE(lower.has_value());
    ASSERT_TRUE(higher.has_value());

    const auto result = map.at(Coord32(2, 1));
    ASSERT_TRUE(result.has_value());

    EXPECT_EQ((*result)[terrain], static_cast<std::uint8_t>('A'));   // higher
    EXPECT_EQ((*result)[fire], static_cast<std::uint8_t>('G'));      // lower
}


TEST_F(MapAtTest, FailureStopsAtTheFailingLayerInDeclaredOrder)
{
    // Declared order: Terrain, Fire, Water. Terrain resolves successfully
    // from the fallback; Fire fails against a real corrupt source; Water is
    // declared after Fire and must never be attempted.
    const std::string corrupt_row = "AB\nC";
    const std::array<std::string, 4> rows {
        corrupt_row, "DEFG", "HIJK", "LMNO"
    };
    write_source(6, make_layer_file(4, 4, 0, 0, rows));

    Storage storage {m_root, m_sources};
    RecordingFallback fallback {};
    ThreeLayerMap map {
        1,
        Area32(Coord32(0, 0), Coord32(15, 15)),
        std::span<const Patch> {m_patches},
        storage,
        fallback
    };

    const auto placed = map.place(patch_id(5), Coord32(0, 0));
    ASSERT_TRUE(placed.has_value());

    // The probe cell sits on the corrupt row.
    const auto result = map.at(Coord32(2, 0));
    ASSERT_FALSE(result.has_value());
    ASSERT_TRUE(std::holds_alternative<LayerSourceError>(result.error()));
    EXPECT_EQ(
        std::get<LayerSourceError>(result.error()),
        LayerSourceError::MalformedData);

    // Terrain (declared first) resolved its whole chunk before the failure;
    // Water (declared after Fire) was never attempted.
    EXPECT_EQ(fallback.calls_for(Terrain::id), 16u);
    EXPECT_EQ(fallback.calls_for(Water::id), 0u);

    // No rollback: the resident Terrain stays answerable, and Water can
    // still be resolved on demand as a plain single-layer access.
    const auto terrain_value = map.value<Terrain>(Coord32(2, 0));
    ASSERT_TRUE(terrain_value.has_value());
    EXPECT_EQ(*terrain_value, std::uint8_t(12));
    EXPECT_EQ(fallback.calls_for(Terrain::id), 16u);

    const auto water_value = map.value<Water>(Coord32(2, 0));
    ASSERT_TRUE(water_value.has_value());
    EXPECT_EQ(*water_value, std::uint8_t(32));
    EXPECT_EQ(fallback.calls_for(Water::id), 16u);
}


TEST_F(MapAtTest, ResidencyFollowsTheDeclaredOrderNotTheLayerIds)
{
    // Declared order: Terrain (id 1), Water (id 3), Fire (id 2). Fire fails
    // against the corrupt source. Water, declared before Fire, must be
    // attempted and resolved in full even though its LayerId is higher:
    // multi-layer residency walks the template pack, not a LayerId sort.
    const std::string corrupt_row = "AB\nC";
    const std::array<std::string, 4> rows {
        corrupt_row, "DEFG", "HIJK", "LMNO"
    };
    write_source(6, make_layer_file(4, 4, 0, 0, rows));

    Storage storage {m_root, m_sources};
    RecordingFallback fallback {};
    ReorderedThreeLayerMap map {
        1,
        Area32(Coord32(0, 0), Coord32(15, 15)),
        std::span<const Patch> {m_patches},
        storage,
        fallback
    };

    const auto placed = map.place(patch_id(5), Coord32(0, 0));
    ASSERT_TRUE(placed.has_value());

    const auto result = map.at(Coord32(2, 0));
    ASSERT_FALSE(result.has_value());
    ASSERT_TRUE(std::holds_alternative<LayerSourceError>(result.error()));
    EXPECT_EQ(
        std::get<LayerSourceError>(result.error()),
        LayerSourceError::MalformedData);

    EXPECT_EQ(fallback.calls_for(Terrain::id), 16u);
    EXPECT_EQ(fallback.calls_for(Water::id), 16u);    // declared before Fire
    // Fire never reached the fallback: its failure came from the source.
    EXPECT_EQ(fallback.total_calls(), 32u);
}


TEST_F(MapAtTest, CacheFullWhenTheSecondSpatialChunkNeedsANewSlot)
{
    Storage storage {m_root, m_sources};
    RecordingFallback fallback {};
    TinyCacheMap map {
        1,
        Area32(Coord32(0, 0), Coord32(15, 15)),
        std::span<const Patch> {},
        storage,
        fallback
    };

    // The first chunk holds both layer planes in the single spatial slot.
    const auto first = map.at(Coord32(0, 0));
    ASSERT_TRUE(first.has_value());

    // A second spatial chunk needs a new slot; the one-slot cache has none.
    const auto other = map.at(Coord32(4, 0));
    ASSERT_FALSE(other.has_value());
    ASSERT_TRUE(std::holds_alternative<MapErrorCode>(other.error()));
    EXPECT_EQ(std::get<MapErrorCode>(other.error()), MapErrorCode::CacheFull);

    // No eviction: the first chunk is still resident and answerable.
    const auto again = map.at(Coord32(1, 0));
    ASSERT_TRUE(again.has_value());
    EXPECT_EQ((*again)[terrain], std::uint8_t(11));
    EXPECT_EQ((*again)[fire], std::uint8_t(21));
}


TEST_F(MapAtTest, MovingTheWinningPlacementInvalidatesTheMultiLayerTile)
{
    write_source(7, make_layer_file(4, 4, 0, 0, rows_from_views(rows_m)));
    write_source(8, make_layer_file(4, 4, 0, 0, rows_from_views(rows_f_m)));

    Storage storage {m_root, m_sources};
    RecordingFallback fallback {};
    TestMap map {
        1,
        Area32(Coord32(0, 0), Coord32(15, 15)),
        std::span<const Patch> {m_patches},
        storage,
        fallback
    };

    const auto placed = map.place(patch_id(6), Coord32(0, 0));
    ASSERT_TRUE(placed.has_value());

    const auto authored = map.at(Coord32(2, 1));
    ASSERT_TRUE(authored.has_value());
    EXPECT_EQ((*authored)[terrain], static_cast<std::uint8_t>('M'));
    EXPECT_EQ((*authored)[fire], static_cast<std::uint8_t>('M'));

    // Move the winning placement away: the whole resident cache is
    // invalidated and the next at() must reflect the current composition.
    ASSERT_TRUE(map.set_position(*placed, Coord32(8, 0)));

    const auto stale = map.at(Coord32(2, 1));
    ASSERT_TRUE(stale.has_value());
    EXPECT_EQ((*stale)[terrain], std::uint8_t(13));   // fallback, not 'M'
    EXPECT_EQ((*stale)[fire], std::uint8_t(23));

    const auto moved = map.at(Coord32(10, 1));
    ASSERT_TRUE(moved.has_value());
    EXPECT_EQ((*moved)[terrain], static_cast<std::uint8_t>('M'));
    EXPECT_EQ((*moved)[fire], static_cast<std::uint8_t>('M'));
}


TEST_F(MapAtTest, ReturnedTileIsAnOwnedValue)
{
    Storage storage {m_root, m_sources};
    RecordingFallback fallback {};
    TestMap map {
        1,
        Area32(Coord32(0, 0), Coord32(15, 15)),
        std::span<const Patch> {},
        storage,
        fallback
    };

    auto first = map.at(Coord32(2, 1));
    ASSERT_TRUE(first.has_value());

    // Modify the property in the returned Tile.
    (*first)[terrain] = 99;

    // The next at() still returns the cached world value, not the modified
    // local copy: the Tile is an owned value, not a proxy.
    const auto second = map.at(Coord32(2, 1));
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ((*second)[terrain], std::uint8_t(13));
    EXPECT_EQ((*second)[fire], std::uint8_t(23));
    EXPECT_EQ((*second).position(), Coord32(2, 1));

    // The local copy keeps its modified value independently.
    EXPECT_EQ((*first)[terrain], 99);
}

} // namespace

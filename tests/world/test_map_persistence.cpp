// Behavioural tests for the explicit Map write-back (D-38):
//
//     Map::flush<LayerT>()
//         -> collect the dirty LayerT Chunks (fixed-capacity, no allocation)
//         -> re-resolve each backing plane directly, never through the
//            dirty resident Cache
//         -> compare the live plane with the fresh backing answer over the
//            logical in-Map cells only
//         -> mark the plane clean without any write when live state has net
//            returned to its backing answer
//         -> otherwise persist only the cells that differ, through the bound
//            runtime source and write_cells()
//         -> mark the plane clean only when every write succeeded
//
// Real StorageFilesystem-backed .layer files in a temporary directory drive
// the source behaviour, following the filesystem-test style.

#include <gtest/gtest.h>

#include "platform/storage/storage_filesystem.hpp"
#include "world/area.hpp"
#include "world/coord.hpp"
#include "world/layer_source.hpp"
#include "world/map.hpp"
#include "world/map_persistence.hpp"
#include "world/map_result.hpp"
#include "world/patch.hpp"
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
using landor::geo::Coord32;
using landor::geo::LayerBinding;
using landor::geo::LayerId;
using landor::geo::LayerSourceError;
using landor::geo::MapPlacementError;
using landor::geo::MapPlacementMutationError;
using landor::geo::MapPersistenceErrorCode;
using landor::geo::PatchId;
using landor::geo::PlacementId;
using landor::geo::RuntimeLayerBinding;
using landor::geo::RuntimeLayerSourceError;
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


/// Constant terminal fallback provider. 7 is distinct from every authored
/// and runtime byte used below.
struct ConstantFallback7
{
    template<typename LayerT>
    [[nodiscard]]
    typename LayerT::value_type
    value(Coord32) const
    {
        return 7;
    }
};

static_assert(
    landor::geo::LayerFallbackProvider<ConstantFallback7, Coord32, Terrain>);
static_assert(
    landor::geo::LayerFallbackProvider<ConstantFallback7, Coord32, Terrain, Fire>);


/// Test Maps: two placement slots, four cache slots, 4x4 chunks.
using TerrainMap = landor::geo::Map<2, 4, 4, ConstantFallback7, Coord32, Terrain>;
using MultiLayerMap = landor::geo::Map<
    2, 4, 4, ConstantFallback7, Coord32, Terrain, Fire>;


/// One version 1.0 dense .layer file: metadata plus exactly `height` rows of
/// exactly `width` cell bytes each, LF-terminated, at the given natural
/// position.
template<typename Rows>
[[nodiscard]] std::string make_layer_file(
    std::uint32_t width,
    std::uint32_t height,
    std::int32_t natural_x,
    std::int32_t natural_y,
    const Rows& rows)
{
    std::string file = "V:1.0\n";
    file += "D:" + std::to_string(width) + " " + std::to_string(height) + "\n";
    file += "P:" + std::to_string(natural_x) + " " + std::to_string(natural_y) + "\n";
    file += "\n";
    for (const auto& row : rows)
    {
        file += std::string_view(row);
        file += '\n';
    }
    return file;
}


/// 16x16 runtime overlay rows: space (0x20) in every cell except the single
/// marker cell, which is where the overlay contributes a non-space value.
[[nodiscard]] std::array<std::string, 16>
runtime_rows_16x16(char marker_char, std::uint32_t marker_col, std::uint32_t marker_row)
{
    std::array<std::string, 16> rows {};
    for (std::string& row : rows)
    {
        row.assign(16, ' ');
    }
    rows[marker_row][marker_col] = marker_char;
    return rows;
}


class MapPersistenceTest : public ::testing::Test
{
protected:
    static constexpr std::size_t source_count = 3;
    static constexpr PatchId k_patch_id = 1;
    static constexpr Coord32 k_probe {2, 1};

    void SetUp() override
    {
        std::error_code ec;
        m_base_path =
            std::filesystem::temp_directory_path(ec) / "landor_map_persistence_test";
        ASSERT_FALSE(ec) << ec.message();

        m_authored_root_path = m_base_path / "authored";
        m_runtime_root_path = m_base_path / "runtime";

        std::error_code create_error;
        const bool created_authored =
            std::filesystem::create_directories(m_authored_root_path, create_error);
        ASSERT_TRUE(created_authored || !create_error) << create_error.message();
        const bool created_runtime =
            std::filesystem::create_directories(m_runtime_root_path, create_error);
        ASSERT_TRUE(created_runtime || !create_error) << create_error.message();

        // The Storage objects store the roots non-owning, so persistent
        // spellings of the paths must outlive every Storage instance.
        m_authored_root = m_authored_root_path.native();
        m_runtime_root = m_runtime_root_path.native();
    }

    void TearDown() override
    {
        std::error_code ec;
        std::filesystem::remove_all(m_base_path, ec);
        ASSERT_FALSE(ec) << ec.message();
    }

    /// Write one source file under a root by table index. Both roots share
    /// the same SourceId table, so an index names a logical source in either
    /// role.
    void write_source(
        const std::filesystem::path& root,
        std::size_t index,
        const std::string& contents)
    {
        std::ofstream stream(
            root / m_sources[index],
            std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(static_cast<bool>(stream));
        stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        ASSERT_TRUE(static_cast<bool>(stream));
    }

    /// Write the authored 4x4 Terrain source (table index 0).
    template<typename Rows>
    void write_authored(const Rows& rows)
    {
        write_source(m_authored_root_path, 0, make_layer_file(4, 4, 0, 0, rows));
    }

    /// Write the 16x16 runtime Terrain overlay (table index 2), matching the
    /// 16x16 Map area anchored at (0, 0).
    template<typename Rows>
    void write_runtime(const Rows& rows)
    {
        write_source(m_runtime_root_path, 2, make_layer_file(16, 16, 0, 0, rows));
    }

    /// Read one source file back by table index for byte-exact comparison.
    [[nodiscard]] std::string
    read_file(const std::filesystem::path& root, std::size_t index) const
    {
        std::ifstream stream(root / m_sources[index], std::ios::binary);
        if (!stream)
        {
            return {};
        }

        return std::string(
            (std::istreambuf_iterator<char>(stream)),
            std::istreambuf_iterator<char>());
    }


protected:
    std::filesystem::path m_base_path;
    std::filesystem::path m_authored_root_path;
    std::filesystem::path m_runtime_root_path;

    // Persistent root spellings for the Storage objects.
    std::string m_authored_root;
    std::string m_runtime_root;

    // Dense source table shared by both roots: index 0 is the authored
    // Terrain source, index 1 the authored Fire source, index 2 the
    // runtime Terrain overlay. The string literals outlive the fixture.
    std::array<std::string_view, source_count> m_sources {
        "pb_terrain_authored.layer",
        "pb_fire_authored.layer",
        "pb_terrain_runtime.layer"
    };

    static constexpr Area32 k_area_4x4 {Coord32(0, 0), Coord32(3, 3)};
    static constexpr Area32 k_map_area {Coord32(0, 0), Coord32(15, 15)};

    // Binding table: declared before the catalogue so the span it lends is
    // already constructed when m_patches initialises.
    std::array<LayerBinding, 2> patch_bindings {
        { {Terrain::id, SourceId {0}}, {Fire::id, SourceId {1}} }
    };

    // Catalogue: one 4x4 Patch contributing both layers.
    std::array<Patch, 1> m_patches {
        Patch {k_patch_id, "persistence", Coord32(0, 0), k_area_4x4, std::span(patch_bindings)}
    };

    // The one logical runtime source for the Terrain layer.
    static constexpr RuntimeLayerBinding k_runtime_binding {Terrain::id, SourceId {2}};

    std::span<const RuntimeLayerBinding> runtime_span() const
    {
        return std::span<const RuntimeLayerBinding>(&k_runtime_binding, 1);
    }

    std::span<const RuntimeLayerBinding> empty_runtime_span() const
    {
        return std::span<const RuntimeLayerBinding> {};
    }

    // The shared 4x4 authored rows; local (2, 1) is the probe cell and is
    // 'A'.
    static constexpr std::array<std::string_view, 4> authored_rows {
        "qwer", "tyAu", "opis", "dfgh"
    };
};


// --- No dirty work -----------------------------------------------------------------

// A flush with no dirty plane needs no binding and performs no Storage I/O.
TEST_F(MapPersistenceTest, FlushWithNoDirtyWorkSucceedsWithoutIO)
{
    // No runtime file, no binding, nothing authored: everything resolves
    // from the fallback.
    Storage authored_storage {m_authored_root, m_sources};
    Storage runtime_storage {m_runtime_root, m_sources};
    ConstantFallback7 fallback {};
    TerrainMap map {
        1,
        k_map_area,
        std::span<const Patch> {m_patches},
        authored_storage,
        runtime_storage,
        empty_runtime_span(),
        fallback
    };

    const auto before = map.value<Terrain>(k_probe);
    ASSERT_TRUE(before.has_value());
    EXPECT_EQ(*before, 7);

    EXPECT_TRUE(map.flush<Terrain>());
    EXPECT_TRUE(map.flush_all());

    // The plane stayed clean, so a placement creation (which requires the
    // dirty-safe whole-cache invalidation) is allowed...
    ASSERT_TRUE(map.place(k_patch_id, Coord32(12, 12)));
    // ...and no runtime file was ever created.
    EXPECT_FALSE(
        std::filesystem::exists(m_runtime_root_path / m_sources[2]));

    // The same holds with a binding present: clean means no I/O, so the
    // runtime file stays byte-for-byte unchanged.
    write_runtime(runtime_rows_16x16('R', 2, 1));
    const std::string before_file = read_file(m_runtime_root_path, 2);

    Storage bound_authored {m_authored_root, m_sources};
    Storage bound_runtime {m_runtime_root, m_sources};
    ConstantFallback7 bound_fallback {};
    TerrainMap bound_map {
        1,
        k_map_area,
        std::span<const Patch> {m_patches},
        bound_authored,
        bound_runtime,
        runtime_span(),
        bound_fallback
    };

    const auto bound_value = bound_map.value<Terrain>(k_probe);
    ASSERT_TRUE(bound_value.has_value());
    EXPECT_EQ(*bound_value, static_cast<std::uint8_t>('R'));

    EXPECT_TRUE(bound_map.flush<Terrain>());
    EXPECT_EQ(read_file(m_runtime_root_path, 2), before_file);
    ASSERT_TRUE(bound_map.place(k_patch_id, Coord32(12, 12)));
}


// --- Backed mutations ---------------------------------------------------------------

// A fallback-backed mutation persists exactly one runtime override: the
// mutated cell, and nothing else changes in the overlay.
TEST_F(MapPersistenceTest, FallbackBackedMutationPersistsOnlyTheMutatedCell)
{
    write_runtime(runtime_rows_16x16(' ', 0, 0));

    Storage authored_storage {m_authored_root, m_sources};
    Storage runtime_storage {m_runtime_root, m_sources};
    ConstantFallback7 fallback {};
    TerrainMap map {
        1,
        k_map_area,
        std::span<const Patch> {m_patches},
        authored_storage,
        runtime_storage,
        runtime_span(),
        fallback
    };

    const auto before = map.value<Terrain>(k_probe);
    ASSERT_TRUE(before.has_value());
    EXPECT_EQ(*before, 7);

    ASSERT_TRUE(map.set<Terrain>(k_probe, static_cast<std::uint8_t>('Z')));
    EXPECT_TRUE(map.flush<Terrain>());

    // Exactly the mutated cell became a runtime override; every other cell
    // (including the neighbour) stayed space.
    EXPECT_EQ(
        read_file(m_runtime_root_path, 2),
        make_layer_file(16, 16, 0, 0, runtime_rows_16x16('Z', 2, 1)));
    // No authored file was ever created or written.
    EXPECT_FALSE(std::filesystem::exists(m_authored_root_path / m_sources[0]));

    // The plane is clean now: a placement creation is allowed, the override
    // survives a fresh re-resolution and the neighbour still resolves from
    // the fallback.
    ASSERT_TRUE(map.place(k_patch_id, Coord32(12, 12)));
    const auto after = map.value<Terrain>(k_probe);
    ASSERT_TRUE(after.has_value());
    EXPECT_EQ(*after, static_cast<std::uint8_t>('Z'));
    const auto neighbour = map.value<Terrain>(Coord32(1, 1));
    ASSERT_TRUE(neighbour.has_value());
    EXPECT_EQ(*neighbour, 7);
}


// The critical persistence proof: within one Cache chunk, one mutated
// authored cell becomes a runtime override while every neighbouring
// untouched authored cell stays space in the runtime source, and the
// authored file itself never changes.
TEST_F(MapPersistenceTest, AuthoredBackedMutationDoesNotFreezeUntouchedCells)
{
    write_authored(authored_rows);
    write_runtime(runtime_rows_16x16(' ', 0, 0));
    const std::string authored_before = read_file(m_authored_root_path, 0);

    Storage authored_storage {m_authored_root, m_sources};
    Storage runtime_storage {m_runtime_root, m_sources};
    ConstantFallback7 fallback {};
    TerrainMap map {
        1,
        k_map_area,
        std::span<const Patch> {m_patches},
        authored_storage,
        runtime_storage,
        runtime_span(),
        fallback
    };

    const auto placed = map.place(k_patch_id, Coord32(0, 0));
    ASSERT_TRUE(placed.has_value());

    const auto probe_before = map.value<Terrain>(k_probe);
    ASSERT_TRUE(probe_before.has_value());
    EXPECT_EQ(*probe_before, static_cast<std::uint8_t>('A'));
    const auto neighbour_before = map.value<Terrain>(Coord32(1, 1));
    ASSERT_TRUE(neighbour_before.has_value());
    EXPECT_EQ(*neighbour_before, static_cast<std::uint8_t>('y'));

    ASSERT_TRUE(map.set<Terrain>(k_probe, static_cast<std::uint8_t>('Z')));
    EXPECT_TRUE(map.flush<Terrain>());

    // The mutated authored cell became exactly one runtime override...
    EXPECT_EQ(
        read_file(m_runtime_root_path, 2),
        make_layer_file(16, 16, 0, 0, runtime_rows_16x16('Z', 2, 1)));
    // ...while the authored file is byte-for-byte unchanged.
    EXPECT_EQ(read_file(m_authored_root_path, 0), authored_before);

    // The plane is clean: a second placement is allowed, the override
    // survives a fresh re-resolution and the neighbour still resolves from
    // the authored source.
    ASSERT_TRUE(map.place(k_patch_id, Coord32(12, 12)));
    const auto probe_after = map.value<Terrain>(k_probe);
    ASSERT_TRUE(probe_after.has_value());
    EXPECT_EQ(*probe_after, static_cast<std::uint8_t>('Z'));
    const auto neighbour_after = map.value<Terrain>(Coord32(1, 1));
    ASSERT_TRUE(neighbour_after.has_value());
    EXPECT_EQ(*neighbour_after, static_cast<std::uint8_t>('y'));
}


// A mutation over an existing runtime override persists the new value at
// the same overlay cell.
TEST_F(MapPersistenceTest, RuntimeBackedMutationPersists)
{
    write_runtime(runtime_rows_16x16('R', 2, 1));

    Storage authored_storage {m_authored_root, m_sources};
    Storage runtime_storage {m_runtime_root, m_sources};
    ConstantFallback7 fallback {};
    TerrainMap map {
        1,
        k_map_area,
        std::span<const Patch> {m_patches},
        authored_storage,
        runtime_storage,
        runtime_span(),
        fallback
    };

    const auto before = map.value<Terrain>(k_probe);
    ASSERT_TRUE(before.has_value());
    EXPECT_EQ(*before, static_cast<std::uint8_t>('R'));

    ASSERT_TRUE(map.set<Terrain>(k_probe, static_cast<std::uint8_t>('Z')));
    EXPECT_TRUE(map.flush<Terrain>());

    EXPECT_EQ(
        read_file(m_runtime_root_path, 2),
        make_layer_file(16, 16, 0, 0, runtime_rows_16x16('Z', 2, 1)));

    ASSERT_TRUE(map.place(k_patch_id, Coord32(12, 12)));
    const auto after = map.value<Terrain>(k_probe);
    ASSERT_TRUE(after.has_value());
    EXPECT_EQ(*after, static_cast<std::uint8_t>('Z'));
}


// One flush<LayerT>() call persists every dirty Chunk of the layer.
TEST_F(MapPersistenceTest, FlushPersistsEveryDirtyChunkOfOneLayer)
{
    write_runtime(runtime_rows_16x16(' ', 0, 0));

    Storage authored_storage {m_authored_root, m_sources};
    Storage runtime_storage {m_runtime_root, m_sources};
    ConstantFallback7 fallback {};
    TerrainMap map {
        1,
        k_map_area,
        std::span<const Patch> {m_patches},
        authored_storage,
        runtime_storage,
        runtime_span(),
        fallback
    };

    const auto first = map.value<Terrain>(Coord32(2, 1));   // chunk (0, 0)
    const auto second = map.value<Terrain>(Coord32(6, 5));  // chunk (4, 4)
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(*first, 7);
    EXPECT_EQ(*second, 7);

    ASSERT_TRUE(map.set<Terrain>(Coord32(2, 1), static_cast<std::uint8_t>('A')));
    ASSERT_TRUE(map.set<Terrain>(Coord32(6, 5), static_cast<std::uint8_t>('B')));
    EXPECT_TRUE(map.flush<Terrain>());

    auto expected_rows = runtime_rows_16x16('A', 2, 1);
    expected_rows[5][6] = 'B';
    EXPECT_EQ(
        read_file(m_runtime_root_path, 2),
        make_layer_file(16, 16, 0, 0, expected_rows));

    ASSERT_TRUE(map.place(k_patch_id, Coord32(12, 12)));
}


// --- Restoration without writes ------------------------------------------------------

// When a changed cell is restored to its backing answer before the flush,
// the plane becomes clean with no binding, no write and no Storage I/O.
TEST_F(MapPersistenceTest, ChangedThenRestoredClearsWithoutAnyWrite)
{
    Storage authored_storage {m_authored_root, m_sources};
    Storage runtime_storage {m_runtime_root, m_sources};
    ConstantFallback7 fallback {};
    TerrainMap map {
        1,
        k_map_area,
        std::span<const Patch> {m_patches},
        authored_storage,
        runtime_storage,
        empty_runtime_span(),
        fallback
    };

    const auto before = map.value<Terrain>(k_probe);
    ASSERT_TRUE(before.has_value());

    ASSERT_TRUE(map.set<Terrain>(k_probe, static_cast<std::uint8_t>('B')));
    ASSERT_TRUE(map.set<Terrain>(k_probe, *before));

    // Live state net returned to its backing answer: the flush needs no
    // binding and writes nothing.
    EXPECT_TRUE(map.flush<Terrain>());
    ASSERT_TRUE(map.place(k_patch_id, Coord32(12, 12)));

    // No runtime source was ever created.
    EXPECT_FALSE(
        std::filesystem::exists(m_runtime_root_path / m_sources[2]));
}


// --- Persistence-specific failures ----------------------------------------------------

// A real difference with no runtime binding fails with
// MissingRuntimeBinding and leaves the plane dirty and authoritative.
TEST_F(MapPersistenceTest, MissingRuntimeBindingWithRealChange)
{
    Storage authored_storage {m_authored_root, m_sources};
    Storage runtime_storage {m_runtime_root, m_sources};
    ConstantFallback7 fallback {};
    TerrainMap map {
        1,
        k_map_area,
        std::span<const Patch> {m_patches},
        authored_storage,
        runtime_storage,
        empty_runtime_span(),
        fallback
    };

    ASSERT_TRUE(map.set<Terrain>(k_probe, static_cast<std::uint8_t>('B')));

    const auto flushed = map.flush<Terrain>();
    ASSERT_FALSE(flushed);
    const auto* code = std::get_if<MapPersistenceErrorCode>(&flushed.error());
    ASSERT_NE(code, nullptr);
    EXPECT_EQ(*code, MapPersistenceErrorCode::MissingRuntimeBinding);

    // The plane remains dirty and authoritative; nothing was written.
    const auto live = map.value<Terrain>(k_probe);
    ASSERT_TRUE(live.has_value());
    EXPECT_EQ(*live, static_cast<std::uint8_t>('B'));

    // The plane remains dirty: a placement creation is refused.
    const auto dirty_probe = map.place(k_patch_id, Coord32(12, 12));
    ASSERT_FALSE(dirty_probe);
    EXPECT_EQ(dirty_probe.error(), MapPlacementError::DirtyState);
    EXPECT_FALSE(
        std::filesystem::exists(m_runtime_root_path / m_sources[2]));
}


// A differing live value that cannot be stored as a v1 runtime contribution
// byte (ASCII space or LF) fails with UnencodableValue and writes nothing.
TEST_F(MapPersistenceTest, UnencodableValueLeavesThePlaneDirty)
{
    write_runtime(runtime_rows_16x16(' ', 0, 0));
    const std::string before = read_file(m_runtime_root_path, 2);

    Storage authored_storage {m_authored_root, m_sources};
    Storage runtime_storage {m_runtime_root, m_sources};
    ConstantFallback7 fallback {};
    TerrainMap map {
        1,
        k_map_area,
        std::span<const Patch> {m_patches},
        authored_storage,
        runtime_storage,
        runtime_span(),
        fallback
    };

    // 0x20 differs from the fallback (7) but would mean "no contribution".
    ASSERT_TRUE(map.set<Terrain>(k_probe, 0x20));
    auto flushed = map.flush<Terrain>();
    ASSERT_FALSE(flushed);
    const auto* code = std::get_if<MapPersistenceErrorCode>(&flushed.error());
    ASSERT_NE(code, nullptr);
    EXPECT_EQ(*code, MapPersistenceErrorCode::UnencodableValue);

    // 0x0A is structural and equally unencodable.
    ASSERT_TRUE(map.set<Terrain>(k_probe, 0x0A));
    flushed = map.flush<Terrain>();
    ASSERT_FALSE(flushed);
    code = std::get_if<MapPersistenceErrorCode>(&flushed.error());
    ASSERT_NE(code, nullptr);
    EXPECT_EQ(*code, MapPersistenceErrorCode::UnencodableValue);

    // No write happened; the plane is still dirty and live.
    EXPECT_EQ(read_file(m_runtime_root_path, 2), before);
    const auto live = map.value<Terrain>(k_probe);
    ASSERT_TRUE(live.has_value());
    EXPECT_EQ(*live, 0x0A);

    // The plane remains dirty: a placement creation is refused.
    const auto dirty_probe = map.place(k_patch_id, Coord32(12, 12));
    ASSERT_FALSE(dirty_probe);
    EXPECT_EQ(dirty_probe.error(), MapPlacementError::DirtyState);
}


// --- Lower-level error propagation -----------------------------------------------------

// A bound-but-absent runtime source fails the flush with the exact
// LayerSourceError from opening.
TEST_F(MapPersistenceTest, BoundButAbsentRuntimeSourceFailsFlush)
{
    write_runtime(runtime_rows_16x16(' ', 0, 0));

    Storage authored_storage {m_authored_root, m_sources};
    Storage runtime_storage {m_runtime_root, m_sources};
    ConstantFallback7 fallback {};
    TerrainMap map {
        1,
        k_map_area,
        std::span<const Patch> {m_patches},
        authored_storage,
        runtime_storage,
        runtime_span(),
        fallback
    };

    const auto before = map.value<Terrain>(k_probe);
    ASSERT_TRUE(before.has_value());
    ASSERT_TRUE(map.set<Terrain>(k_probe, static_cast<std::uint8_t>('B')));

    // Remove the overlay after the plane is resident and dirty.
    std::error_code ec;
    std::filesystem::remove(m_runtime_root_path / m_sources[2], ec);
    ASSERT_FALSE(ec) << ec.message();

    const auto flushed = map.flush<Terrain>();
    ASSERT_FALSE(flushed);
    const auto* src = std::get_if<LayerSourceError>(&flushed.error());
    ASSERT_NE(src, nullptr);
    EXPECT_EQ(*src, LayerSourceError::StorageFailed);

    // The plane remains dirty: a placement creation is refused.
    const auto dirty_probe = map.place(k_patch_id, Coord32(12, 12));
    ASSERT_FALSE(dirty_probe);
    EXPECT_EQ(dirty_probe.error(), MapPlacementError::DirtyState);
}


// A runtime source whose dimensions disagree with the Map area fails the
// flush with the exact RuntimeLayerSourceError.
TEST_F(MapPersistenceTest, RuntimeDimensionMismatchPropagatesExactly)
{
    write_runtime(runtime_rows_16x16(' ', 0, 0));

    Storage authored_storage {m_authored_root, m_sources};
    Storage runtime_storage {m_runtime_root, m_sources};
    ConstantFallback7 fallback {};
    TerrainMap map {
        1,
        k_map_area,
        std::span<const Patch> {m_patches},
        authored_storage,
        runtime_storage,
        runtime_span(),
        fallback
    };

    const auto before = map.value<Terrain>(k_probe);
    ASSERT_TRUE(before.has_value());
    ASSERT_TRUE(map.set<Terrain>(k_probe, static_cast<std::uint8_t>('B')));

    // Replace the overlay with a 15x16 source after the plane is resident.
    std::vector<std::string> rows(16, std::string(15, ' '));
    write_source(m_runtime_root_path, 2, make_layer_file(15, 16, 0, 0, rows));

    const auto flushed = map.flush<Terrain>();
    ASSERT_FALSE(flushed);
    const auto* dim = std::get_if<RuntimeLayerSourceError>(&flushed.error());
    ASSERT_NE(dim, nullptr);
    EXPECT_EQ(*dim, RuntimeLayerSourceError::DimensionMismatch);

    // The plane remains dirty: a placement creation is refused.
    const auto dirty_probe = map.place(k_patch_id, Coord32(12, 12));
    ASSERT_FALSE(dirty_probe);
    EXPECT_EQ(dirty_probe.error(), MapPlacementError::DirtyState);
}


// A runtime source whose position disagrees with the Map area fails the
// flush with the exact RuntimeLayerSourceError.
TEST_F(MapPersistenceTest, RuntimePositionMismatchPropagatesExactly)
{
    write_runtime(runtime_rows_16x16(' ', 0, 0));

    Storage authored_storage {m_authored_root, m_sources};
    Storage runtime_storage {m_runtime_root, m_sources};
    ConstantFallback7 fallback {};
    TerrainMap map {
        1,
        k_map_area,
        std::span<const Patch> {m_patches},
        authored_storage,
        runtime_storage,
        runtime_span(),
        fallback
    };

    const auto before = map.value<Terrain>(k_probe);
    ASSERT_TRUE(before.has_value());
    ASSERT_TRUE(map.set<Terrain>(k_probe, static_cast<std::uint8_t>('B')));

    // Replace the overlay with a same-sized source at P:1 0.
    write_source(
        m_runtime_root_path,
        2,
        make_layer_file(16, 16, 1, 0, runtime_rows_16x16(' ', 0, 0)));

    const auto flushed = map.flush<Terrain>();
    ASSERT_FALSE(flushed);
    const auto* pos = std::get_if<RuntimeLayerSourceError>(&flushed.error());
    ASSERT_NE(pos, nullptr);
    EXPECT_EQ(*pos, RuntimeLayerSourceError::PositionMismatch);

    // The plane remains dirty: a placement creation is refused.
    const auto dirty_probe = map.place(k_patch_id, Coord32(12, 12));
    ASSERT_FALSE(dirty_probe);
    EXPECT_EQ(dirty_probe.error(), MapPlacementError::DirtyState);
}


// A corrupted runtime row (structural LF inside a data cell) fails the
// flush with the exact LayerSourceError.
TEST_F(MapPersistenceTest, MalformedRuntimeRowFailsFlushExactly)
{
    write_runtime(runtime_rows_16x16(' ', 0, 0));

    Storage authored_storage {m_authored_root, m_sources};
    Storage runtime_storage {m_runtime_root, m_sources};
    ConstantFallback7 fallback {};
    TerrainMap map {
        1,
        k_map_area,
        std::span<const Patch> {m_patches},
        authored_storage,
        runtime_storage,
        runtime_span(),
        fallback
    };

    const auto before = map.value<Terrain>(k_probe);
    ASSERT_TRUE(before.has_value());
    ASSERT_TRUE(map.set<Terrain>(k_probe, static_cast<std::uint8_t>('B')));

    // Corrupt the touched runtime row: an LF inside a data cell.
    auto rows = runtime_rows_16x16(' ', 0, 0);
    rows[1][2] = '\n';
    write_source(m_runtime_root_path, 2, make_layer_file(16, 16, 0, 0, rows));

    const auto flushed = map.flush<Terrain>();
    ASSERT_FALSE(flushed);
    const auto* row = std::get_if<LayerSourceError>(&flushed.error());
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(*row, LayerSourceError::MalformedData);

    // The plane remains dirty: a placement creation is refused.
    const auto dirty_probe = map.place(k_patch_id, Coord32(12, 12));
    ASSERT_FALSE(dirty_probe);
    EXPECT_EQ(dirty_probe.error(), MapPlacementError::DirtyState);
}


// --- Multi-layer flush -------------------------------------------------------------------

// The multi-layer flush walks the declared order and fails fast: the first
// (Terrain) layer persists completely, then the unbound Fire layer fails
// with the exact error.
TEST_F(MapPersistenceTest, MultiLayerFlushFailsFastInDeclaredOrder)
{
    write_runtime(runtime_rows_16x16(' ', 0, 0));
    const std::string expected_terrain =
        make_layer_file(16, 16, 0, 0, runtime_rows_16x16('T', 2, 1));

    Storage authored_storage {m_authored_root, m_sources};
    Storage runtime_storage {m_runtime_root, m_sources};
    ConstantFallback7 fallback {};
    MultiLayerMap map {
        1,
        k_map_area,
        std::span<const Patch> {m_patches},
        authored_storage,
        runtime_storage,
        runtime_span(),
        fallback
    };

    ASSERT_TRUE(map.set<Terrain>(k_probe, static_cast<std::uint8_t>('T')));
    ASSERT_TRUE(map.set<Fire>(k_probe, static_cast<std::uint8_t>('F')));

    const auto flushed = map.flush_all();
    ASSERT_FALSE(flushed);
    const auto* code = std::get_if<MapPersistenceErrorCode>(&flushed.error());
    ASSERT_NE(code, nullptr);
    EXPECT_EQ(*code, MapPersistenceErrorCode::MissingRuntimeBinding);

    // Partial progress: the Terrain override was persisted...
    EXPECT_EQ(read_file(m_runtime_root_path, 2), expected_terrain);
    // ...and the Terrain plane is clean, so flushing it again is a no-op
    // that needs no binding lookup.
    EXPECT_TRUE(map.flush<Terrain>());
    EXPECT_EQ(read_file(m_runtime_root_path, 2), expected_terrain);

    // The Fire plane remains dirty and authoritative: a placement creation
    // is refused while any plane is dirty.
    const auto dirty_probe = map.place(k_patch_id, Coord32(12, 12));
    ASSERT_FALSE(dirty_probe);
    EXPECT_EQ(dirty_probe.error(), MapPlacementError::DirtyState);
    const auto fire = map.value<Fire>(k_probe);
    ASSERT_TRUE(fire.has_value());
    EXPECT_EQ(*fire, static_cast<std::uint8_t>('F'));
}


// --- Placement resumption ---------------------------------------------------------------

// A placement change is refused over dirty state, succeeds after a
// successful flush, and the persisted override survives the move while the
// authored content moves with the Placement.
TEST_F(MapPersistenceTest, PlacementChangeResumesAfterSuccessfulFlush)
{
    write_authored(authored_rows);
    write_runtime(runtime_rows_16x16(' ', 0, 0));
    const std::string authored_before = read_file(m_authored_root_path, 0);

    Storage authored_storage {m_authored_root, m_sources};
    Storage runtime_storage {m_runtime_root, m_sources};
    ConstantFallback7 fallback {};
    TerrainMap map {
        1,
        k_map_area,
        std::span<const Patch> {m_patches},
        authored_storage,
        runtime_storage,
        runtime_span(),
        fallback
    };

    const auto placed = map.place(k_patch_id, Coord32(0, 0));
    ASSERT_TRUE(placed.has_value());
    const PlacementId id = *placed;

    const auto before = map.value<Terrain>(k_probe);
    ASSERT_TRUE(before.has_value());
    EXPECT_EQ(*before, static_cast<std::uint8_t>('A'));

    ASSERT_TRUE(map.set<Terrain>(k_probe, static_cast<std::uint8_t>('Z')));

    // The move is refused while the plane is dirty.
    const auto refused = map.set_position(id, Coord32(8, 8));
    ASSERT_FALSE(refused);
    EXPECT_EQ(
        refused.error(),
        MapPlacementMutationError::DirtyState);

    // A successful flush unblocks the placement change.
    EXPECT_TRUE(map.flush<Terrain>());
    const auto moved = map.set_position(id, Coord32(8, 8));
    ASSERT_TRUE(moved.has_value());

    // The override survived the move (runtime state is world-oriented)...
    const auto probe_after = map.value<Terrain>(k_probe);
    ASSERT_TRUE(probe_after.has_value());
    EXPECT_EQ(*probe_after, static_cast<std::uint8_t>('Z'));
    // ...and the authored content moved with the Placement.
    const auto moved_cell = map.value<Terrain>(Coord32(10, 9));
    ASSERT_TRUE(moved_cell.has_value());
    EXPECT_EQ(*moved_cell, static_cast<std::uint8_t>('A'));
    const auto cleared_cell = map.value<Terrain>(Coord32(1, 1));
    ASSERT_TRUE(cleared_cell.has_value());
    EXPECT_EQ(*cleared_cell, 7);

    // The authored file never changed.
    EXPECT_EQ(read_file(m_authored_root_path, 0), authored_before);
}

} // namespace

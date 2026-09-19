// Behavioural tests for the runtime layer overlay in checked Map reads.
//
// These pin the implemented D-36 resolution order:
//
//     resident Cache
//         -> runtime/materialized overlay
//         -> authored Placements, highest PlacementId first
//         -> terminal fallback
//
// Precedence is per-cell, not per-source or per-Chunk: a non-space runtime
// cell resolves that cell only, and the still-unresolved cells of the same
// Chunk continue down to authored Placements and the terminal fallback.
//
// The bound runtime source is opened and geometry-validated exactly once per
// layer-Chunk resolution, before the per-cell read loop; the loop then issues
// one-cell read_cells() calls through the shared reader. The current
// StorageFilesystem exposes no open counter, so the once-per-resolution open
// is pinned structurally (the single open/validate above the per-cell loop in
// resolve_chunk<LayerT>()) rather than by a behavioural test; adding an
// instrumentation seam to Storage is deliberately avoided.

#include <gtest/gtest.h>

#include "platform/storage/storage_filesystem.hpp"
#include "world/area.hpp"
#include "world/coord.hpp"
#include "world/map.hpp"
#include "world/patch.hpp"
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


namespace
{

using landor::geo::Area32;
using landor::geo::Coord32;
using landor::geo::LayerBinding;
using landor::geo::LayerId;
using landor::geo::PatchId;
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
/// One-cell Map and one-cell Chunk: the whole resolution is one plane cell.
using CellMap = landor::geo::Map<2, 4, 1, ConstantFallback7, Coord32, Terrain>;


/// One version 1.0 dense .layer file: metadata plus exactly `height` rows of
/// exactly `width` cell bytes each, LF-terminated, at the given natural
/// position.
[[nodiscard]] std::string make_layer_file(
    std::uint32_t width,
    std::uint32_t height,
    std::int32_t natural_x,
    std::int32_t natural_y,
    const std::span<const std::string>& rows)
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


class MapRuntimeOverlayTest : public ::testing::Test
{
protected:
    static constexpr std::size_t source_count = 3;
    static constexpr PatchId k_patch_id = 1;
    static constexpr Coord32 k_probe {2, 1};

    void SetUp() override
    {
        std::error_code ec;
        m_base_path =
            std::filesystem::temp_directory_path(ec) / "landor_map_runtime_overlay_test";
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
    void write_authored(const std::span<const std::string>& rows)
    {
        write_source(m_authored_root_path, 0, make_layer_file(4, 4, 0, 0, rows));
    }

    /// Write the 16x16 runtime Terrain overlay (table index 2), matching the
    /// 16x16 Map area anchored at (0, 0).
    void write_runtime(const std::span<const std::string>& rows)
    {
        write_source(m_runtime_root_path, 2, make_layer_file(16, 16, 0, 0, rows));
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
        "ov_terrain_authored.layer",
        "ov_fire_authored.layer",
        "ov_terrain_runtime.layer"
    };

    static constexpr Area32 k_area_4x4 {Coord32(0, 0), Coord32(3, 3)};
    static constexpr Area32 k_map_area {Coord32(0, 0), Coord32(15, 15)};

    // Binding table: declared before the catalogue so the span it lends is
    // already constructed when m_patches initialises.
    std::array<LayerBinding, 2> overlay_bindings {
        { {Terrain::id, SourceId {0}}, {Fire::id, SourceId {1}} }
    };

    // Catalogue: one 4x4 Patch contributing both layers.
    std::array<Patch, 1> m_patches {
        Patch {k_patch_id, "overlay", Coord32(0, 0), k_area_4x4, std::span(overlay_bindings)}
    };

    // The one logical runtime source for the Terrain layer.
    static constexpr RuntimeLayerBinding k_runtime_binding {Terrain::id, SourceId {2}};

    std::span<const RuntimeLayerBinding> runtime_span() const
    {
        return std::span<const RuntimeLayerBinding>(&k_runtime_binding, 1);
    }
};


// --- Precedence ----------------------------------------------------------------

// D-36: a non-space runtime cell is authoritative over the authored
// Placement and the fallback at the same coordinate.
TEST_F(MapRuntimeOverlayTest, RuntimeNonSpaceOverridesAuthored)
{
    const std::array<std::string, 4> authored_rows {"qwer", "tyAu", "opis", "dfgh"};
    write_authored(authored_rows);
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

    const auto placed = map.place(k_patch_id, Coord32(0, 0));
    ASSERT_TRUE(placed.has_value());

    const auto result = map.value<Terrain>(k_probe);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, static_cast<std::uint8_t>('R'));
}


// D-36: with no authored Placement contributing, a non-space runtime cell
// wins over the terminal fallback.
TEST_F(MapRuntimeOverlayTest, RuntimeNonSpaceOverridesFallback)
{
    write_runtime(runtime_rows_16x16('R', 2, 1));

    Storage authored_storage {m_authored_root, m_sources};
    Storage runtime_storage {m_runtime_root, m_sources};
    ConstantFallback7 fallback {};
    TerrainMap map {
        1,
        k_map_area,
        std::span<const Patch> {},
        authored_storage,
        runtime_storage,
        runtime_span(),
        fallback
    };

    const auto result = map.value<Terrain>(k_probe);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, static_cast<std::uint8_t>('R'));
}


// Space (0x20) means "no runtime contribution": resolution continues to the
// authored Placement below the probe cell.
TEST_F(MapRuntimeOverlayTest, RuntimeSpaceFallsThroughToAuthored)
{
    const std::array<std::string, 4> authored_rows {"qwer", "tyAu", "opis", "dfgh"};
    write_authored(authored_rows);
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

    const auto placed = map.place(k_patch_id, Coord32(0, 0));
    ASSERT_TRUE(placed.has_value());

    const auto result = map.value<Terrain>(k_probe);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, static_cast<std::uint8_t>('A'));
}


// Space runtime cell with no authored contribution: the terminal fallback
// answers.
TEST_F(MapRuntimeOverlayTest, RuntimeSpaceFallsThroughToFallback)
{
    write_runtime(runtime_rows_16x16(' ', 0, 0));

    Storage authored_storage {m_authored_root, m_sources};
    Storage runtime_storage {m_runtime_root, m_sources};
    ConstantFallback7 fallback {};
    TerrainMap map {
        1,
        k_map_area,
        std::span<const Patch> {},
        authored_storage,
        runtime_storage,
        runtime_span(),
        fallback
    };

    const auto result = map.value<Terrain>(k_probe);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 7);
}


// Precedence is per-cell: one canonical Chunk resolves cell A from the
// runtime overlay, cell B from the authored Placement, and cell C from the
// fallback, all inside the same resident plane.
TEST_F(MapRuntimeOverlayTest, PerCellPrecedenceCoexistsInOneChunk)
{
    // Authored row 0: 'q' at (0, 0), 'A' at (1, 0), space at (2, 0).
    const std::array<std::string, 4> authored_rows {"qA  ", "    ", "    ", "    "};
    write_authored(authored_rows);
    // Runtime row 0: 'R' at (0, 0), space elsewhere.
    write_runtime(runtime_rows_16x16('R', 0, 0));

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

    // Cell A: runtime 'R' over authored 'q'.
    const auto cell_a = map.value<Terrain>(Coord32(0, 0));
    ASSERT_TRUE(cell_a.has_value());
    EXPECT_EQ(*cell_a, static_cast<std::uint8_t>('R'));

    // Cell B: runtime space falls through to authored 'A'.
    const auto cell_b = map.value<Terrain>(Coord32(1, 0));
    ASSERT_TRUE(cell_b.has_value());
    EXPECT_EQ(*cell_b, static_cast<std::uint8_t>('A'));

    // Cell C: runtime space and authored space fall through to the fallback.
    const auto cell_c = map.value<Terrain>(Coord32(2, 0));
    ASSERT_TRUE(cell_c.has_value());
    EXPECT_EQ(*cell_c, 7);
}


// --- Lazy lower sources ---------------------------------------------------------

// When the runtime overlay resolves every logical cell of the requested
// Chunk, no lower authored source is opened and the fallback is never
// queried: the deliberately broken authored source cannot fail the access.
TEST_F(MapRuntimeOverlayTest, FullyRuntimeResolvedChunkHidesBrokenAuthoredSource)
{
    // Broken authored source: unsupported version. It would fail if opened.
    write_source(m_authored_root_path, 0, "V:2.0\nD:1 1\nP:0 0\n\nR\n");
    // Valid one-cell runtime overlay resolving the only cell.
    write_source(m_runtime_root_path, 2, "V:1.0\nD:1 1\nP:0 0\n\nR\n");

    const auto cell_binding = LayerBinding {Terrain::id, SourceId {0}};
    const Patch cell_patch {
        k_patch_id,
        "cell",
        Coord32(0, 0),
        Area32 {Coord32(0, 0), Coord32(0, 0)},
        std::span(&cell_binding, 1)};
    const std::array<Patch, 1> patches {cell_patch};

    Storage authored_storage {m_authored_root, m_sources};
    Storage runtime_storage {m_runtime_root, m_sources};
    ConstantFallback7 fallback {};
    CellMap map {
        1,
        Area32 {Coord32(0, 0), Coord32(0, 0)},
        std::span<const Patch> {patches},
        authored_storage,
        runtime_storage,
        runtime_span(),
        fallback
    };

    const auto placed = map.place(k_patch_id, Coord32(0, 0));
    ASSERT_TRUE(placed.has_value());

    const auto result = map.value<Terrain>(Coord32(0, 0));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, static_cast<std::uint8_t>('R'));
}


// --- Runtime error domains -------------------------------------------------------

// A structurally malformed touched runtime row surfaces the exact reader
// error, regardless of source role. The file is otherwise size-consistent;
// only the probe cell of row 1 holds an LF, which is structural in version
// 1.0 and never a cell value.
TEST_F(MapRuntimeOverlayTest, RuntimeMalformedRowFailsWithMalformedData)
{
    auto rows = runtime_rows_16x16(' ', 0, 0);
    rows[1][2] = '\n';
    write_runtime(rows);

    Storage authored_storage {m_authored_root, m_sources};
    Storage runtime_storage {m_runtime_root, m_sources};
    ConstantFallback7 fallback {};
    TerrainMap map {
        1,
        k_map_area,
        std::span<const Patch> {},
        authored_storage,
        runtime_storage,
        runtime_span(),
        fallback
    };

    const auto result = map.value<Terrain>(k_probe);
    ASSERT_FALSE(result.has_value());
    const auto* error = std::get_if<landor::geo::LayerSourceError>(&result.error());
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(*error, landor::geo::LayerSourceError::MalformedData);
}


// A correctly parseable runtime source with the wrong dimension D fails
// runtime geometry validation, not authored or Map-local validation.
TEST_F(MapRuntimeOverlayTest, RuntimeDimensionMismatchFailsExactly)
{
    std::array<std::string, 16> rows {};
    for (std::string& row : rows)
    {
        row.assign(15, ' ');
    }
    write_source(m_runtime_root_path, 2, make_layer_file(15, 16, 0, 0, rows));

    Storage authored_storage {m_authored_root, m_sources};
    Storage runtime_storage {m_runtime_root, m_sources};
    ConstantFallback7 fallback {};
    TerrainMap map {
        1,
        k_map_area,
        std::span<const Patch> {},
        authored_storage,
        runtime_storage,
        runtime_span(),
        fallback
    };

    const auto result = map.value<Terrain>(k_probe);
    ASSERT_FALSE(result.has_value());
    const auto* error = std::get_if<RuntimeLayerSourceError>(&result.error());
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(*error, RuntimeLayerSourceError::DimensionMismatch);
}


// A correctly parseable runtime source anchored at the wrong position P
// fails runtime geometry validation.
TEST_F(MapRuntimeOverlayTest, RuntimePositionMismatchFailsExactly)
{
    write_source(
        m_runtime_root_path,
        2,
        make_layer_file(16, 16, 1, 0, runtime_rows_16x16('R', 2, 1)));

    Storage authored_storage {m_authored_root, m_sources};
    Storage runtime_storage {m_runtime_root, m_sources};
    ConstantFallback7 fallback {};
    TerrainMap map {
        1,
        k_map_area,
        std::span<const Patch> {},
        authored_storage,
        runtime_storage,
        runtime_span(),
        fallback
    };

    const auto result = map.value<Terrain>(k_probe);
    ASSERT_FALSE(result.has_value());
    const auto* error = std::get_if<RuntimeLayerSourceError>(&result.error());
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(*error, RuntimeLayerSourceError::PositionMismatch);
}


// A present binding whose runtime source is physically absent is an error,
// not an absent overlay: the exact reader storage failure propagates and no
// fall-through to authored state or the fallback happens.
TEST_F(MapRuntimeOverlayTest, BoundRuntimeSourceAbsenceFails)
{
    // No runtime file is written for the bound SourceId.
    Storage authored_storage {m_authored_root, m_sources};
    Storage runtime_storage {m_runtime_root, m_sources};
    ConstantFallback7 fallback {};
    TerrainMap map {
        1,
        k_map_area,
        std::span<const Patch> {},
        authored_storage,
        runtime_storage,
        runtime_span(),
        fallback
    };

    const auto result = map.value<Terrain>(k_probe);
    ASSERT_FALSE(result.has_value());
    const auto* error = std::get_if<landor::geo::LayerSourceError>(&result.error());
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(*error, landor::geo::LayerSourceError::StorageFailed);
}


// Without a runtime binding the runtime storage role is not inspected at
// all: authored and fallback behaviour remains exactly as before the
// binding catalogue existed.
TEST_F(MapRuntimeOverlayTest, NoBindingKeepsAuthoredAndFallbackBehaviour)
{
    const std::array<std::string, 4> authored_rows {"qwer", "tyAu", "opis", "dfgh"};
    write_authored(authored_rows);

    Storage authored_storage {m_authored_root, m_sources};
    Storage runtime_storage {m_runtime_root, m_sources};
    ConstantFallback7 fallback {};
    TerrainMap map {
        1,
        k_map_area,
        std::span<const Patch> {m_patches},
        authored_storage,
        runtime_storage,
        std::span<const RuntimeLayerBinding> {},
        fallback
    };

    const auto placed = map.place(k_patch_id, Coord32(0, 0));
    ASSERT_TRUE(placed.has_value());

    const auto authored = map.value<Terrain>(k_probe);
    ASSERT_TRUE(authored.has_value());
    EXPECT_EQ(*authored, static_cast<std::uint8_t>('A'));

    // No Placement at all: the terminal fallback answers.
    const TerrainMap baseline_map {
        1,
        k_map_area,
        std::span<const Patch> {},
        authored_storage,
        runtime_storage,
        std::span<const RuntimeLayerBinding> {},
        fallback
    };
    const auto baseline = baseline_map.value<Terrain>(k_probe);
    ASSERT_TRUE(baseline.has_value());
    EXPECT_EQ(*baseline, 7);
}


// --- Residency -------------------------------------------------------------------

// Once a Chunk is resolved it is current live state: an external change to
// the runtime backing file does not re-resolve the resident Chunk, and Map
// never polls Storage.
TEST_F(MapRuntimeOverlayTest, ResidentChunkSurvivesExternalRuntimeFileChange)
{
    write_runtime(runtime_rows_16x16('R', 2, 1));

    Storage authored_storage {m_authored_root, m_sources};
    Storage runtime_storage {m_runtime_root, m_sources};
    ConstantFallback7 fallback {};
    TerrainMap map {
        1,
        k_map_area,
        std::span<const Patch> {},
        authored_storage,
        runtime_storage,
        runtime_span(),
        fallback
    };

    const auto first = map.value<Terrain>(k_probe);
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(*first, static_cast<std::uint8_t>('R'));

    // Change the runtime backing file behind the resident Chunk.
    write_runtime(runtime_rows_16x16('X', 2, 1));

    const auto second = map.value<Terrain>(k_probe);
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(*second, static_cast<std::uint8_t>('R'));
}


// --- Multi-layer at() -------------------------------------------------------------

// at() assembles every layer through the existing per-layer seam. Terrain is
// answered by the runtime overlay; Fire by the authored Placement of the
// same Patch. No runtime-specific code belongs in at() itself.
TEST_F(MapRuntimeOverlayTest, MultiLayerAtAssemblesRuntimeOverrideAndAuthored)
{
    const std::array<std::string, 4> terrain_rows {"qwer", "tyAu", "opis", "dfgh"};
    write_source(m_authored_root_path, 0, make_layer_file(4, 4, 0, 0, terrain_rows));
    const std::array<std::string, 4> fire_rows {"    ", "  F ", "    ", "    "};
    write_source(m_authored_root_path, 1, make_layer_file(4, 4, 0, 0, fire_rows));
    write_runtime(runtime_rows_16x16('R', 2, 1));

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

    const auto placed = map.place(k_patch_id, Coord32(0, 0));
    ASSERT_TRUE(placed.has_value());

    const Terrain terrain {};
    const Fire fire {};
    const auto result = map.at(k_probe);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ((*result).position(), k_probe);
    EXPECT_EQ((*result)[terrain], static_cast<std::uint8_t>('R'));
    EXPECT_EQ((*result)[fire], static_cast<std::uint8_t>('F'));
}

} // namespace

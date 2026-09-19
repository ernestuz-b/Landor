// Behavioural and compile-time tests for the Map runtime layer binding
// catalogue and the runtime overlay it names.
//
// The exercised facts:
//
//   - Map constructs with an empty runtime binding span;
//   - Map constructs with one runtime binding;
//   - the old constructor without the runtime-binding span is no longer
//     the contract;
//   - the same Storage object can still be used for the authored and the
//     runtime role at the same time as the binding catalogue;
//   - a bound runtime source is consulted by checked reads (D-36): a
//     non-space runtime cell answers over authored state, and an existing
//     malformed runtime source fails the access with its exact error;
//   - a present binding whose source does not exist yet is a reserved,
//     unmaterialized identity (D-39): the runtime pass is skipped and the
//     authored state or fallback answers.
//
// The private Map::runtime_binding() seam is deliberately not exposed; the
// catalogue invariants are unit-tested through the free
// valid_runtime_layer_bindings() helper in test_runtime_layer_source.cpp
// and pinned here through construction behaviour.

#include <gtest/gtest.h>

#include "platform/storage/storage_filesystem.hpp"
#include "world/area.hpp"
#include "world/coord.hpp"
#include "world/map.hpp"
#include "world/patch.hpp"
#include "world/runtime_layer_source.hpp"
#include "storage/types.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <variant>


namespace
{

using landor::geo::Area32;
using landor::geo::Coord32;
using landor::geo::LayerBinding;
using landor::geo::LayerId;
using landor::geo::PatchId;
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


/// Constant terminal fallback provider.
struct ConstantFallback
{
    template<typename LayerT>
    [[nodiscard]]
    typename LayerT::value_type
    value(Coord32) const
    {
        return 0;
    }
};

static_assert(
    landor::geo::LayerFallbackProvider<ConstantFallback, Coord32, Terrain>);


/// Small test Map: two placement slots, four cache slots, 4x4 chunks.
using TestMap = landor::geo::Map<2, 4, 4, ConstantFallback, Coord32, Terrain>;


/// One version 1.0 dense .layer file: metadata plus rows of exactly
/// `width` cell bytes each, LF-terminated, at the given natural position.
[[nodiscard]] std::string make_layer_file(
    std::uint32_t width,
    std::uint32_t height,
    std::int32_t natural_x,
    std::int32_t natural_y,
    std::span<const std::string_view> rows)
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


/// One 16x16 version 1.0 dense runtime overlay file: every cell holds
/// `fill`. It matches the 16x16 Map area anchored at (0, 0), so it passes
/// runtime geometry validation as-is.
[[nodiscard]] std::string make_runtime_file(char fill)
{
    std::string file = "V:1.0\nD:16 16\nP:0 0\n\n";
    for (std::uint32_t y = 0; y < 16; ++y)
    {
        file.append(16, fill);
        file += '\n';
    }
    return file;
}


/// One 16x16 version 1.0 dense runtime overlay file: every cell is space
/// (0x20, "no runtime contribution") except the single marker cell.
[[nodiscard]] std::string make_runtime_file_with_marker(
    char marker_char,
    std::uint32_t marker_col,
    std::uint32_t marker_row)
{
    std::string file = "V:1.0\nD:16 16\nP:0 0\n\n";
    for (std::uint32_t y = 0; y < 16; ++y)
    {
        for (std::uint32_t x = 0; x < 16; ++x)
        {
            file += (x == marker_col && y == marker_row) ? marker_char : ' ';
        }
        file += '\n';
    }
    return file;
}


class MapRuntimeCatalogueTest : public ::testing::Test
{
protected:
    static constexpr std::size_t source_count = 1;
    static constexpr PatchId k_patch_id = 1;

    void SetUp() override
    {
        std::error_code ec;
        m_base_path =
            std::filesystem::temp_directory_path(ec) / "landor_map_runtime_catalogue_test";
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

    /// Write one source file under a root. Both roots share the same
    /// SourceId table, so the same index names "the same" logical source in
    /// each role.
    void write_source(const std::filesystem::path& root, const std::string& contents)
    {
        std::ofstream stream(
            root / m_sources[0],
            std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(static_cast<bool>(stream));
        stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        ASSERT_TRUE(static_cast<bool>(stream));
    }


protected:
    std::filesystem::path m_base_path;
    std::filesystem::path m_authored_root_path;
    std::filesystem::path m_runtime_root_path;

    // Persistent root spellings for the Storage objects.
    std::string m_authored_root;
    std::string m_runtime_root;

    // Dense source table shared by both roots; the string literal outlives
    // the fixture.
    std::array<std::string_view, source_count> m_sources {
        "p_rt.layer"
    };

    static constexpr Area32 k_area_4x4 {Coord32(0, 0), Coord32(3, 3)};

    // Binding table: declared before the catalogue so the span it lends is
    // already constructed when m_patches initialises.
    std::array<LayerBinding, 1> catalogue_bindings {{Terrain::id, SourceId {0}}};

    // Catalogue: one 4x4 Patch bound to the single shared SourceId.
    std::array<Patch, 1> m_patches {
        Patch {k_patch_id, "rt", Coord32(0, 0), k_area_4x4, std::span(catalogue_bindings)}
    };
    static constexpr Area32 k_map_area {Coord32(0, 0), Coord32(15, 15)};

    // Shared 4x4 authored row pattern; cell (2, 1) is the probe cell.
    static constexpr std::array<std::string_view, 4> rows_a {
        "qwer", "tyAu", "opis", "dfgh"
    };
};


// --- Constructor contract -----------------------------------------------------

TEST(MapRuntimeCatalogue, ConstructorRequiresTheRuntimeBindingSpan)
{
    // The current constructor takes the runtime binding span between the
    // runtime storage role and the fallback provider.
    static_assert(
        std::is_constructible_v<
            TestMap,
            landor::geo::MapId,
            Area32,
            std::span<const Patch>,
            const Storage&,
            Storage&,
            std::span<const RuntimeLayerBinding>,
            const ConstantFallback&>);

    // The old two-storage constructor without the span is no longer
    // constructible.
    static_assert(
        !std::is_constructible_v<
            TestMap,
            landor::geo::MapId,
            Area32,
            std::span<const Patch>,
            const Storage&,
            Storage&,
            const ConstantFallback&>);

    SUCCEED();
}


// --- Empty catalogue ------------------------------------------------------------

TEST_F(MapRuntimeCatalogueTest, EmptyRuntimeBindingSpanConstructsAndReadsAuthored)
{
    write_source(m_authored_root_path, make_layer_file(4, 4, 0, 0, rows_a));

    Storage authored_storage {m_authored_root, m_sources};
    Storage runtime_storage {m_runtime_root, m_sources};
    ConstantFallback fallback {};
    TestMap map {
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

    // Zero bindings changes nothing about current reads.
    const auto result = map.value<Terrain>(Coord32(2, 1));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, static_cast<std::uint8_t>('A'));
}


// --- One binding, live runtime reads ------------------------------------------

// D-36: a non-space runtime cell is authoritative over all authored
// Placements and the fallback. The runtime root holds 'R' at the probe cell
// behind the bound SourceId, so the runtime byte now answers.
TEST_F(MapRuntimeCatalogueTest, BoundRuntimeCellOverridesAuthored)
{
    write_source(m_authored_root_path, make_layer_file(4, 4, 0, 0, rows_a));
    write_source(m_runtime_root_path, make_runtime_file_with_marker('R', 2, 1));

    const std::array<RuntimeLayerBinding, 1> runtime_layers {
        {Terrain::id, SourceId {0}}
    };

    Storage authored_storage {m_authored_root, m_sources};
    Storage runtime_storage {m_runtime_root, m_sources};
    ConstantFallback fallback {};
    TestMap map {
        1,
        k_map_area,
        std::span<const Patch> {m_patches},
        authored_storage,
        runtime_storage,
        std::span<const RuntimeLayerBinding> {runtime_layers},
        fallback
    };

    const auto placed = map.place(k_patch_id, Coord32(0, 0));
    ASSERT_TRUE(placed.has_value());

    const auto result = map.value<Terrain>(Coord32(2, 1));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, static_cast<std::uint8_t>('R'));
}


// D-36/D-39: a present binding whose runtime source does not exist yet is
// a reserved, unmaterialized identity, not a checked-access failure. The
// runtime root holds no file for the bound SourceId; the runtime pass is
// skipped and the authored placement answers.
TEST_F(MapRuntimeCatalogueTest, BoundButAbsentRuntimeSourceIsNotConsulted)
{
    // Only the authored root contains a source file.
    write_source(m_authored_root_path, make_layer_file(4, 4, 0, 0, rows_a));

    const std::array<RuntimeLayerBinding, 1> runtime_layers {
        {Terrain::id, SourceId {0}}
    };

    Storage authored_storage {m_authored_root, m_sources};
    Storage runtime_storage {m_runtime_root, m_sources};
    ConstantFallback fallback {};
    TestMap map {
        1,
        k_map_area,
        std::span<const Patch> {m_patches},
        authored_storage,
        runtime_storage,
        std::span<const RuntimeLayerBinding> {runtime_layers},
        fallback
    };

    const auto placed = map.place(k_patch_id, Coord32(0, 0));
    ASSERT_TRUE(placed.has_value());

    // The reserved-but-absent runtime source is skipped: the authored 'A'
    // answers.
    const auto result = map.value<Terrain>(Coord32(2, 1));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, static_cast<std::uint8_t>('A'));

    // No read created the runtime source.
    bool present = true;
    ASSERT_TRUE(runtime_storage.exists(SourceId {0}, present));
    EXPECT_FALSE(present);
}


// D-36/D-39: with nothing authored under the probe cell either, a
// reserved-but-absent runtime source falls through to the fallback.
TEST_F(MapRuntimeCatalogueTest, BoundButAbsentRuntimeSourceFallsThroughToFallback)
{
    // No authored patch is placed, and the runtime root holds no file.
    const std::array<RuntimeLayerBinding, 1> runtime_layers {
        {Terrain::id, SourceId {0}}
    };

    Storage authored_storage {m_authored_root, m_sources};
    Storage runtime_storage {m_runtime_root, m_sources};
    ConstantFallback fallback {};
    TestMap map {
        1,
        k_map_area,
        std::span<const Patch> {m_patches},
        authored_storage,
        runtime_storage,
        std::span<const RuntimeLayerBinding> {runtime_layers},
        fallback
    };

    const auto result = map.value<Terrain>(Coord32(2, 1));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 0u);
}


// D-36: an existing runtime source that cannot be opened is a real
// checked-access failure with its exact error, not an absent overlay. The
// runtime root holds a file for the bound SourceId whose V record does not
// follow the version 1.0 grammar, so open fails with MalformedMetadata and
// no fall-through to authored state happens.
TEST_F(MapRuntimeCatalogueTest, BoundRuntimeSourceThatCannotBeOpenedIsStillAnError)
{
    // Only the authored root contains a source file; the runtime root holds
    // a malformed file for the bound SourceId.
    write_source(m_authored_root_path, make_layer_file(4, 4, 0, 0, rows_a));
    write_source(m_runtime_root_path, "V:1.x\nD:16 16\nP:0 0\n\n" + make_runtime_file('R'));

    const std::array<RuntimeLayerBinding, 1> runtime_layers {
        {Terrain::id, SourceId {0}}
    };

    Storage authored_storage {m_authored_root, m_sources};
    Storage runtime_storage {m_runtime_root, m_sources};
    ConstantFallback fallback {};
    TestMap map {
        1,
        k_map_area,
        std::span<const Patch> {m_patches},
        authored_storage,
        runtime_storage,
        std::span<const RuntimeLayerBinding> {runtime_layers},
        fallback
    };

    const auto placed = map.place(k_patch_id, Coord32(0, 0));
    ASSERT_TRUE(placed.has_value());

    const auto result = map.value<Terrain>(Coord32(2, 1));
    ASSERT_FALSE(result.has_value());
    const auto* error = std::get_if<landor::geo::LayerSourceError>(&result.error());
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(*error, landor::geo::LayerSourceError::MalformedMetadata);
}


// One physical Storage object fills both semantic roles while the binding
// catalogue is non-empty. The shared source file carries the 16x16 overlay
// spelling: the runtime role opens and validates it, resolves every cell of
// the requested Chunk, and the 4x4 authored patch is never opened (its D
// would fail authored geometry validation if it were).
TEST_F(MapRuntimeCatalogueTest, SameStorageObjectFillsBothRolesWithABinding)
{
    write_source(m_authored_root_path, make_runtime_file('A'));

    const std::array<RuntimeLayerBinding, 1> runtime_layers {
        {Terrain::id, SourceId {0}}
    };

    Storage storage {m_authored_root, m_sources};
    ConstantFallback fallback {};
    TestMap map {
        1,
        k_map_area,
        std::span<const Patch> {m_patches},
        storage,
        storage,
        std::span<const RuntimeLayerBinding> {runtime_layers},
        fallback
    };

    const auto placed = map.place(k_patch_id, Coord32(0, 0));
    ASSERT_TRUE(placed.has_value());

    const auto result = map.value<Terrain>(Coord32(2, 1));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, static_cast<std::uint8_t>('A'));
}

} // namespace

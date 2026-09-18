// Behavioural and compile-time tests for the Map runtime layer binding
// catalogue.
//
// The exercised facts:
//
//   - Map constructs with an empty runtime binding span;
//   - Map constructs with one runtime binding;
//   - the old constructor without the runtime-binding span is no longer
//     the contract;
//   - the same Storage object can still be used for the authored and the
//     runtime role at the same time as the binding catalogue;
//   - runtime bindings cause zero runtime I/O today: a bound runtime source
//     holding different bytes, or a runtime root with no such source at
//     all, cannot change a current read.
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

    // Shared 4x4 row patterns; cell (2, 1) is the probe cell and differs
    // between the authored and the runtime spelling.
    static constexpr std::array<std::string_view, 4> rows_a {
        "qwer", "tyAu", "opis", "dfgh"
    };
    static constexpr std::array<std::string_view, 4> rows_r {
        "qwer", "tyRu", "opis", "dfgh"
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


// --- One binding, zero runtime I/O ---------------------------------------------

TEST_F(MapRuntimeCatalogueTest, OneRuntimeBindingStillReadsAuthoredOnly)
{
    // Both roots carry the same SourceId table. The runtime root holds a
    // different byte ('R') at the probe cell behind the bound SourceId, so
    // any runtime read would have changed the answer.
    write_source(m_authored_root_path, make_layer_file(4, 4, 0, 0, rows_a));
    write_source(m_runtime_root_path, make_layer_file(4, 4, 0, 0, rows_r));

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

    // The bound runtime source is not consulted, so the authored byte
    // answers.
    const auto result = map.value<Terrain>(Coord32(2, 1));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, static_cast<std::uint8_t>('A'));
}


TEST_F(MapRuntimeCatalogueTest, BoundSourceMayBeAbsentInRuntimeStorage)
{
    // Only the authored root contains the source. The binding names a
    // SourceId that does not exist in the runtime root; no read of the
    // runtime role happens, so its absence is not an error.
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

    const auto result = map.value<Terrain>(Coord32(2, 1));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, static_cast<std::uint8_t>('A'));
}


TEST_F(MapRuntimeCatalogueTest, SameStorageObjectFillsBothRolesWithABinding)
{
    // One physical Storage object fills both semantic roles while the
    // binding catalogue is non-empty.
    write_source(m_authored_root_path, make_layer_file(4, 4, 0, 0, rows_a));

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

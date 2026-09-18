// Behavioural tests for the two explicit Map storage roles:
//
//     const storage::Storage&   authored storage (immutable read source)
//     storage::Storage&         runtime storage (writable persistence)
//
// The exercised facts:
//
//   - current authored resolution uses only the authored role: with the
//     same SourceId present in two different roots, Map answers from the
//     authored root, never from the runtime root;
//   - the runtime role may be absent or irrelevant to current reads:
//     authored access still succeeds when the runtime root holds no such
//     source at all, and the absence is not an error;
//   - the same Storage object may fill both roles; physical separation is
//     optional.
//
// No runtime behaviour exists yet: in this slice Map never opens, reads or
// writes the runtime storage. The constructor contract itself is pinned by
// the header tripwire (tests/test_world_headers.cpp).

#include <gtest/gtest.h>

#include "platform/storage/storage_filesystem.hpp"
#include "world/area.hpp"
#include "world/coord.hpp"
#include "world/map.hpp"
#include "world/patch.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <system_error>


namespace
{

using landor::geo::Area32;
using landor::geo::Coord32;
using landor::geo::LayerBinding;
using landor::geo::LayerId;
using landor::geo::PatchId;
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


class MapStorageRolesTest : public ::testing::Test
{
protected:
    static constexpr std::size_t source_count = 1;
    static constexpr PatchId k_patch_id = 1;

    void SetUp() override
    {
        std::error_code ec;
        m_base_path =
            std::filesystem::temp_directory_path(ec) / "landor_map_storage_roles_test";
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
        "p_role.layer"
    };

    static constexpr Area32 k_area_4x4 {Coord32(0, 0), Coord32(3, 3)};

    // Binding table: declared before the catalogue so the span it lends is
    // already constructed when m_patches initialises.
    std::array<LayerBinding, 1> role_bindings {{Terrain::id, SourceId {0}}};

    // Catalogue: one 4x4 Patch bound to the single shared SourceId.
    std::array<Patch, 1> m_patches {
        Patch {k_patch_id, "role", Coord32(0, 0), k_area_4x4, std::span(role_bindings)}
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


TEST_F(MapStorageRolesTest, AuthoredResolutionUsesOnlyTheAuthoredStorage)
{
    // Both roots carry the same SourceId table, and the same logical
    // source holds different bytes in each role: 'A' in the authored root,
    // 'R' in the runtime root.
    write_source(m_authored_root_path, make_layer_file(4, 4, 0, 0, rows_a));
    write_source(m_runtime_root_path, make_layer_file(4, 4, 0, 0, rows_r));

    Storage authored_storage {m_authored_root, m_sources};
    Storage runtime_storage {m_runtime_root, m_sources};
    ConstantFallback fallback {};
    TestMap map {
        1,
        k_map_area,
        std::span<const Patch> {m_patches},
        authored_storage,
        runtime_storage,
        fallback
    };

    const auto placed = map.place(k_patch_id, Coord32(0, 0));
    ASSERT_TRUE(placed.has_value());

    // Current authored resolution must answer from the authored role only.
    const auto result = map.value<Terrain>(Coord32(2, 1));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, static_cast<std::uint8_t>('A'));

    // The rest of the plane also comes from the authored source.
    const auto corner = map.value<Terrain>(Coord32(0, 0));
    ASSERT_TRUE(corner.has_value());
    EXPECT_EQ(*corner, static_cast<std::uint8_t>('q'));
}


TEST_F(MapStorageRolesTest, RuntimeSourceMayBeAbsentWithoutAffectingAuthoredAccess)
{
    // Only the authored root contains the source; the runtime root holds
    // nothing at all for this SourceId.
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
        fallback
    };

    const auto placed = map.place(k_patch_id, Coord32(0, 0));
    ASSERT_TRUE(placed.has_value());

    // The absent runtime source is not consulted, so it is not an error.
    const auto result = map.value<Terrain>(Coord32(2, 1));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, static_cast<std::uint8_t>('A'));
}


TEST_F(MapStorageRolesTest, SameStorageObjectSatisfiesBothRoles)
{
    write_source(m_authored_root_path, make_layer_file(4, 4, 0, 0, rows_a));

    // One physical Storage object fills both roles.
    Storage storage {m_authored_root, m_sources};
    ConstantFallback fallback {};
    TestMap map {
        1,
        k_map_area,
        std::span<const Patch> {m_patches},
        storage,
        storage,
        fallback
    };

    const auto placed = map.place(k_patch_id, Coord32(0, 0));
    ASSERT_TRUE(placed.has_value());

    const auto authored = map.value<Terrain>(Coord32(2, 1));
    ASSERT_TRUE(authored.has_value());
    EXPECT_EQ(*authored, static_cast<std::uint8_t>('A'));

    // With no authored coverage the terminal fallback still answers.
    TestMap fallback_map {
        1,
        k_map_area,
        std::span<const Patch> {},
        storage,
        storage,
        fallback
    };

    const auto baseline = fallback_map.value<Terrain>(Coord32(2, 1));
    ASSERT_TRUE(baseline.has_value());
    EXPECT_EQ(*baseline, 0);
}

} // namespace

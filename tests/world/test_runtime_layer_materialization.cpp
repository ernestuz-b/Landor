// Behavioural tests for materialize_runtime_layer_source() (D-39): the
// lazy creation of one Map layer's runtime overlay source as a blank dense
// v1 source covering the complete Map area.
//
// The exercised facts:
//
//   - the created source is a blank dense v1 source: exact P / D metadata
//     derived from the Map area, every cell the no-contribution space byte;
//   - the geometry is derived from the complete Map area, including Map
//     areas that do not start at the origin;
//   - an existing source is never replaced;
//   - an unrepresentable Map area fails with InvalidMapGeometry before any
//     Storage is touched.
//
// Real StorageFilesystem-backed .layer files in a temporary directory drive
// the source behaviour, following the filesystem-test style.

#include <gtest/gtest.h>

#include "platform/storage/storage_filesystem.hpp"
#include "world/area.hpp"
#include "world/coord.hpp"
#include "world/layer_source.hpp"
#include "world/runtime_layer_source.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>


namespace
{

using landor::geo::Area32;
using landor::geo::Coord32;
using landor::geo::LayerSourceError;
using landor::geo::RuntimeLayerSourceError;
using landor::geo::materialize_runtime_layer_source;
using landor::geo::open_layer_source;
using landor::geo::read_cells;
using landor::geo::validate_runtime_layer_source;
using landor::storage::Offset;
using landor::storage::Size;
using landor::storage::SourceId;
using landor::storage::Storage;


class RuntimeLayerMaterializationTest : public ::testing::Test
{
protected:
    static constexpr std::size_t source_count = 1;

    void SetUp() override
    {
        std::error_code ec;
        m_base_path =
            std::filesystem::temp_directory_path(ec) /
            "landor_runtime_layer_materialization_test";
        ASSERT_FALSE(ec) << ec.message();

        std::error_code create_error;
        const bool created =
            std::filesystem::create_directories(m_base_path, create_error);
        ASSERT_TRUE(created || !create_error) << create_error.message();

        // The Storage objects store the root non-owning, so a persistent
        // spelling of the path must outlive every Storage instance.
        m_root = m_base_path.native();
    }

    void TearDown() override
    {
        std::error_code ec;
        std::filesystem::remove_all(m_base_path, ec);
        ASSERT_FALSE(ec) << ec.message();
    }

    [[nodiscard]] Storage make_storage() const
    {
        return Storage {m_root, std::span<const std::string_view> {m_sources}};
    }

    void write_file(const std::string& contents)
    {
        std::ofstream stream(
            m_base_path / m_sources[0],
            std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(static_cast<bool>(stream));
        stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        ASSERT_TRUE(static_cast<bool>(stream));
    }

    /// Read the source file back for byte-exact comparison.
    [[nodiscard]] std::string read_file() const
    {
        std::ifstream stream(m_base_path / m_sources[0], std::ios::binary);
        if (!stream)
        {
            return {};
        }

        return std::string(
            (std::istreambuf_iterator<char>(stream)),
            std::istreambuf_iterator<char>());
    }

    /// One version 1.0 dense blank source: exact metadata at the given
    /// natural position plus rows of exactly `width` space bytes each,
    /// LF-terminated.
    [[nodiscard]] std::string
    make_blank_file(std::uint32_t width, std::uint32_t height,
                    std::int32_t natural_x, std::int32_t natural_y) const
    {
        std::string file = "V:1.0\n";
        file += "D:" + std::to_string(width) + " " + std::to_string(height) + "\n";
        file += "P:" + std::to_string(natural_x) + " " + std::to_string(natural_y) + "\n";
        file += "\n";
        for (std::uint32_t y = 0; y < height; ++y)
        {
            file.append(width, ' ');
            file += '\n';
        }
        return file;
    }


protected:
    std::filesystem::path m_base_path;
    std::string m_root;

    // The string literal outlives the fixture.
    std::array<std::string_view, source_count> m_sources {"mat.layer"};

    static constexpr Area32 k_area_16x16 {Coord32(0, 0), Coord32(15, 15)};
};


// The materialized source is the blank dense v1 source for the complete Map
// area: P is the area minimum, D is the area extent, and every cell is the
// no-contribution space byte.
TEST_F(RuntimeLayerMaterializationTest, MaterializesABlankDenseV1SourceForTheFullMapArea)
{
    Storage storage = make_storage();

    const auto result =
        materialize_runtime_layer_source(storage, SourceId {0}, k_area_16x16);

    ASSERT_TRUE(result.has_value());

    const auto& layout = *result;
    EXPECT_EQ(layout.version_major, 1);
    EXPECT_EQ(layout.version_minor, 0);
    EXPECT_EQ(layout.width, 16u);
    EXPECT_EQ(layout.height, 16u);
    EXPECT_EQ(layout.natural_position, Coord32(0, 0));

    // "V:1.0\nD:16 16\nP:0 0\n\n" is 21 bytes; the body is 16 rows of
    // 17 bytes each.
    EXPECT_EQ(layout.data_offset, Offset {21});
    EXPECT_EQ(layout.row_stride, Offset {17});
    EXPECT_EQ(layout.source_size, Size {293});

    // The exact file bytes.
    EXPECT_EQ(read_file(), make_blank_file(16, 16, 0, 0));

    // The created source satisfies the runtime geometry contract and
    // reopens through the ordinary reader.
    const auto validated = validate_runtime_layer_source(k_area_16x16, layout);
    ASSERT_TRUE(validated.has_value());

    const auto reopened = open_layer_source(storage, SourceId {0});
    ASSERT_TRUE(reopened.has_value());
    EXPECT_EQ(reopened->width, layout.width);
    EXPECT_EQ(reopened->height, layout.height);
    EXPECT_EQ(reopened->natural_position, layout.natural_position);

    // A row reads back as all no-contribution cells.
    std::array<std::byte, 16> row {};
    const auto read = read_cells(
        layout, storage, SourceId {0}, /*row=*/3, /*x=*/0,
        std::span<std::byte>(row));
    ASSERT_TRUE(read.has_value());
    for (const auto cell : row)
    {
        EXPECT_EQ(cell, std::byte {0x20});
    }
}


// The geometry is derived from the complete Map area, not from any Cache
// grid: a Map area off the origin anchors the source at its own minimum.
TEST_F(RuntimeLayerMaterializationTest, DerivesGeometryFromAnOffOriginMapArea)
{
    Storage storage = make_storage();
    const Area32 area {Coord32(-5, -7), Coord32(10, 8)};

    const auto result = materialize_runtime_layer_source(storage, SourceId {0}, area);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->width, 16u);
    EXPECT_EQ(result->height, 16u);
    EXPECT_EQ(result->natural_position, Coord32(-5, -7));
    EXPECT_EQ(read_file(), make_blank_file(16, 16, -5, -7));
}


// An existing source is never replaced: the create() already_exists mapping
// surfaces as LayerSourceError::StorageFailed and the file bytes survive.
TEST_F(RuntimeLayerMaterializationTest, DoesNotReplaceAnExistingSource)
{
    // The existing source carries one unrelated override cell.
    std::string existing = make_blank_file(16, 16, 0, 0);
    // The data starts after the 21-byte metadata; cell (2, 1).
    existing[21 + 1 * 17 + 2] = 'R';
    write_file(existing);

    Storage storage = make_storage();

    const auto result =
        materialize_runtime_layer_source(storage, SourceId {0}, k_area_16x16);

    ASSERT_FALSE(result.has_value());
    const auto* src = std::get_if<LayerSourceError>(&result.error());
    ASSERT_NE(src, nullptr);
    EXPECT_EQ(*src, LayerSourceError::StorageFailed);

    // The existing source is untouched, override cell included.
    EXPECT_EQ(read_file(), existing);
}


// An empty area fails as a geometry error before any Storage is touched.
TEST_F(RuntimeLayerMaterializationTest, RejectsAnEmptyAreaWithoutTouchingStorage)
{
    Storage storage = make_storage();
    const Area32 empty {}; // default-constructed: no cells

    const auto result = materialize_runtime_layer_source(storage, SourceId {0}, empty);

    ASSERT_FALSE(result.has_value());
    const auto* error = std::get_if<RuntimeLayerSourceError>(&result.error());
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(*error, RuntimeLayerSourceError::InvalidMapGeometry);

    // No source was created.
    bool present = true;
    ASSERT_TRUE(storage.exists(SourceId {0}, present));
    EXPECT_FALSE(present);
}


// A full-range Coord32 extent is 2^32 cells, above the dense v1 per-axis
// uint32 maximum: a geometry failure, not a format overflow, and no Storage
// is touched.
TEST_F(RuntimeLayerMaterializationTest, RejectsAnExtentAboveTheDenseV1Maximum)
{
    Storage storage = make_storage();
    const Area32 too_wide {
        Coord32(std::numeric_limits<std::int32_t>::min(), 0),
        Coord32(std::numeric_limits<std::int32_t>::max(), 0)};

    const auto result = materialize_runtime_layer_source(storage, SourceId {0}, too_wide);

    ASSERT_FALSE(result.has_value());
    const auto* error = std::get_if<RuntimeLayerSourceError>(&result.error());
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(*error, RuntimeLayerSourceError::InvalidMapGeometry);

    bool present = true;
    ASSERT_TRUE(storage.exists(SourceId {0}, present));
    EXPECT_FALSE(present);
}


// A Map minimum that does not fit the source's Coord32 position fails as a
// geometry error. Only a wider coordinate type can express such a minimum.
TEST_F(RuntimeLayerMaterializationTest, RejectsAMinimumOutsideTheInt32PositionRange)
{
    Storage storage = make_storage();

    using WideCoord = landor::geo::Coord<std::int64_t>;
    using WideArea = landor::geo::Area<WideCoord>;
    const WideArea area {
        WideCoord(-3000000000, 0),
        WideCoord(-3000000000 + 15, 0)};

    const auto result = materialize_runtime_layer_source(storage, SourceId {0}, area);

    ASSERT_FALSE(result.has_value());
    const auto* error = std::get_if<RuntimeLayerSourceError>(&result.error());
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(*error, RuntimeLayerSourceError::InvalidMapGeometry);

    bool present = true;
    ASSERT_TRUE(storage.exists(SourceId {0}, present));
    EXPECT_FALSE(present);
}

} // namespace

// Behavioural tests for the streaming version 1.0 layer source reader.
//
// The unit under test is the format/storage seam: a recording in-memory
// StorageBackend supplies bytes through bounded reads, and the tests prove
// that opening and cell access touch only the requested source ranges.

#include <gtest/gtest.h>

#include "world/layer_source.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>


namespace
{

using landor::geo::LayerSourceError;
using landor::geo::LayerSourceLayout;
using landor::geo::has_contribution;
using landor::geo::open_layer_source;
using landor::geo::read_cells;
using landor::geo::write_cells;

using landor::storage::Error;
using landor::storage::Offset;
using landor::storage::Result;
using landor::storage::Size;
using landor::storage::SourceId;


constexpr SourceId k_source = 0;
constexpr SourceId k_unknown_source = 1;


/**
 * In-memory StorageBackend that records every read and write request.
 *
 * Also supports injecting storage-level failures so the reader's and
 * writer's error mapping can be tested without touching a real filesystem.
 */
class RecordingStorage
{
public:
    struct ReadRequest
    {
        Offset offset;
        std::size_t size;
    };

    struct WriteRequest
    {
        Offset offset;
        std::size_t size;
    };

    void set_bytes(std::string bytes)
    {
        m_bytes = std::move(bytes);
    }

    void fail_size_with(Error error)
    {
        m_size_error = error;
    }

    void fail_reads_with(Error error)
    {
        m_read_error = error;
    }

    void fail_writes_with(Error error)
    {
        m_write_error = error;
    }

    [[nodiscard]] Result read(
        SourceId source,
        Offset offset,
        std::span<std::byte> destination) const
    {
        if (source != k_source)
        {
            return Result{Error::invalid_source};
        }

        if (m_read_error != Error::none)
        {
            return Result{m_read_error};
        }

        const std::size_t total = m_bytes.size();
        if (offset > total || destination.size() > total - offset)
        {
            return Result{Error::out_of_range};
        }

        if (!destination.empty())
        {
            std::memcpy(
                destination.data(),
                m_bytes.data() + offset,
                destination.size());
        }

        m_requests.push_back(ReadRequest{offset, destination.size()});
        return Result{};
    }

    [[nodiscard]] Result write(
        SourceId source,
        Offset offset,
        std::span<const std::byte> data)
    {
        if (source != k_source)
        {
            return Result{Error::invalid_source};
        }

        if (m_write_error != Error::none)
        {
            return Result{m_write_error};
        }

        const std::size_t total = m_bytes.size();
        if (offset > total || data.size() > total - offset)
        {
            return Result{Error::out_of_range};
        }

        if (!data.empty())
        {
            std::memcpy(m_bytes.data() + offset, data.data(), data.size());
        }

        m_writes.push_back(WriteRequest{offset, data.size()});
        return Result{};
    }

    [[nodiscard]] Result size(SourceId source, Size& result) const
    {
        if (source != k_source)
        {
            return Result{Error::invalid_source};
        }

        if (m_size_error != Error::none)
        {
            return Result{m_size_error};
        }

        result = static_cast<Size>(m_bytes.size());
        return Result{};
    }

    [[nodiscard]] const std::vector<ReadRequest>& requests() const
    {
        return m_requests;
    }

    void clear_requests()
    {
        m_requests.clear();
    }

    [[nodiscard]] const std::vector<WriteRequest>& writes() const
    {
        return m_writes;
    }

    void clear_writes()
    {
        m_writes.clear();
    }

    [[nodiscard]] const std::string& bytes() const
    {
        return m_bytes;
    }

    [[nodiscard]] std::size_t total_requested_bytes() const
    {
        std::size_t total = 0;
        for (const auto& request : m_requests)
        {
            total += request.size;
        }
        return total;
    }


private:
    std::string m_bytes;
    mutable std::vector<ReadRequest> m_requests;
    std::vector<WriteRequest> m_writes;
    Error m_size_error = Error::none;
    Error m_read_error = Error::none;
    Error m_write_error = Error::none;
};


/** Deterministic cell pattern used by the test grids. */
[[nodiscard]] char cell_byte(std::uint32_t x, std::uint32_t y)
{
    if (x == 1u || y == 2u)
    {
        return ' ';
    }

    if (x == 3u && y == 1u)
    {
        return '7';
    }

    return static_cast<char>('A' + static_cast<int>((x * 7u + y * 13u) % 20u));
}


[[nodiscard]] std::string make_grid(std::uint32_t width, std::uint32_t height)
{
    std::string grid;
    for (std::uint32_t y = 0; y < height; ++y)
    {
        for (std::uint32_t x = 0; x < width; ++x)
        {
            grid += cell_byte(x, y);
        }
        grid += '\n';
    }
    return grid;
}


/// Header lines joined by LF, terminated by the metadata-terminating blank line.
[[nodiscard]] std::string make_header(std::initializer_list<const char*> lines)
{
    std::string header;
    for (const auto* line : lines)
    {
        header += line;
        header += '\n';
    }
    header += '\n';
    return header;
}


constexpr std::uint32_t k_width = 4;
constexpr std::uint32_t k_height = 3;


[[nodiscard]] std::string make_default_source()
{
    return make_header({"V:1.0", "D:4 3", "P:0 0"}) + make_grid(k_width, k_height);
}


/// Open the default 4x3 source and return its layout.
[[nodiscard]] bool open_default(
    RecordingStorage& storage,
    LayerSourceLayout& layout)
{
    storage.set_bytes(make_default_source());

    const auto result = open_layer_source(storage, k_source);
    if (!result)
    {
        return false;
    }

    layout = *result;
    return true;
}

} // namespace


TEST(LayerSource, OpensMinimalValidSource)
{
    RecordingStorage storage;
    const std::string header = make_header({"V:1.0", "D:4 3", "P:0 0"});
    storage.set_bytes(header + make_grid(k_width, k_height));

    const auto result = open_layer_source(storage, k_source);

    ASSERT_TRUE(result);

    const auto& layout = *result;
    EXPECT_EQ(layout.version_major, 1);
    EXPECT_EQ(layout.version_minor, 0);
    EXPECT_EQ(layout.width, k_width);
    EXPECT_EQ(layout.height, k_height);
    EXPECT_EQ(layout.natural_position, landor::geo::Coord32(0, 0));

    // data_offset points immediately past the 0x0A 0x0A terminator.
    EXPECT_EQ(layout.data_offset, static_cast<Offset>(header.size()));

    // row_stride is width + 1 (cells plus row LF).
    EXPECT_EQ(layout.row_stride, static_cast<Offset>(k_width + 1u));

    // expected total size from dimensions equals the source size.
    EXPECT_EQ(
        layout.expected_size,
        static_cast<Size>(
            header.size()
            + static_cast<std::uint64_t>(k_height) * (k_width + 1u)));
    EXPECT_EQ(layout.expected_size, layout.source_size);
}


TEST(LayerSource, OpensMetadataLargerThanOneParserReadBuffer)
{
    // Core records plus unknown padding lines so the metadata block
    // comfortably exceeds one bounded parser read.
    std::string header;
    const char* core_lines[] = {"V:1.0", "D:8 500", "P:-4 9"};
    for (const auto* line : core_lines)
    {
        header += line;
        header += '\n';
    }
    for (int i = 0; i < 200; ++i)
    {
        header += "X:0123456789abcdefghijklmnopqrstuvwxyz";
        header += '\n';
    }
    header += '\n';

    RecordingStorage storage;
    storage.set_bytes(header + make_grid(8, 500));

    const auto result = open_layer_source(storage, k_source);

    ASSERT_TRUE(result);

    const auto& layout = *result;
    EXPECT_EQ(layout.width, 8u);
    EXPECT_EQ(layout.height, 500u);
    EXPECT_EQ(layout.natural_position, landor::geo::Coord32(-4, 9));
    EXPECT_EQ(layout.data_offset, static_cast<Offset>(header.size()));
    EXPECT_EQ(layout.row_stride, static_cast<Offset>(9u));
    EXPECT_EQ(layout.expected_size, layout.source_size);

    // The metadata spanned several bounded reads, and no read targets the
    // data grid: every read starts inside the metadata region.
    EXPECT_GE(storage.requests().size(), 2u);
    for (const auto& request : storage.requests())
    {
        EXPECT_LT(request.offset, layout.data_offset);
    }

    // The final read may extend past the terminator by at most one parser
    // block, never out into the data grid as a whole.
    EXPECT_GE(storage.total_requested_bytes(), layout.data_offset);
    EXPECT_LE(
        storage.total_requested_bytes() - layout.data_offset,
        landor::geo::layer_source_metadata_read_size);
    EXPECT_LT(storage.total_requested_bytes(), layout.source_size);
}


TEST(LayerSource, AcceptsCoreRecordsInAnyOrder)
{
    const char* orders[][3] = {
        {"V:1.0", "D:4 3", "P:0 0"},
        {"D:4 3", "V:1.0", "P:0 0"},
        {"P:0 0", "D:4 3", "V:1.0"},
        {"D:4 3", "P:0 0", "V:1.0"},
    };

    for (const auto& order : orders)
    {
        RecordingStorage storage;
        storage.set_bytes(
            make_header({order[0], order[1], order[2]}) + make_grid(k_width, k_height));

        const auto result = open_layer_source(storage, k_source);

        ASSERT_TRUE(result);
        EXPECT_EQ((*result).width, k_width);
        EXPECT_EQ((*result).height, k_height);
        EXPECT_EQ((*result).natural_position, landor::geo::Coord32(0, 0));
    }
}


TEST(LayerSource, ParsesDecimalDimensionsAndPosition)
{
    RecordingStorage storage;
    storage.set_bytes(
        make_header({"V:1.0", "D:120 60", "P:-10 25"}) + make_grid(120, 60));

    const auto result = open_layer_source(storage, k_source);

    ASSERT_TRUE(result);
    EXPECT_EQ((*result).width, 120u);
    EXPECT_EQ((*result).height, 60u);
    EXPECT_EQ((*result).natural_position, landor::geo::Coord32(-10, 25));
}


TEST(LayerSource, ParsesHexadecimalDimensionsAndPosition)
{
    RecordingStorage storage;
    storage.set_bytes(
        make_header({"V:1.0", "D:0x20 0x10", "P:0x78 0x50"}) + make_grid(32, 16));

    const auto result = open_layer_source(storage, k_source);

    ASSERT_TRUE(result);
    EXPECT_EQ((*result).width, 32u);
    EXPECT_EQ((*result).height, 16u);
    EXPECT_EQ((*result).natural_position, landor::geo::Coord32(120, 80));
}


TEST(LayerSource, ParsesNegativeNaturalPosition)
{
    RecordingStorage storage;
    storage.set_bytes(
        make_header({"V:1.0", "D:4 3", "P:-10 -0x10"}) + make_grid(k_width, k_height));

    const auto result = open_layer_source(storage, k_source);

    ASSERT_TRUE(result);
    EXPECT_EQ((*result).natural_position, landor::geo::Coord32(-10, -16));
}


TEST(LayerSource, SkipsUnknownMetadataLines)
{
    RecordingStorage storage;
    storage.set_bytes(
        make_header(
            {
                "V:1.0",
                "Author:ernest",
                "D:4 3",
                "Note:whatever goes here",
                "P:0 0",
            })
        + make_grid(k_width, k_height));

    const auto result = open_layer_source(storage, k_source);

    ASSERT_TRUE(result);
    EXPECT_EQ((*result).width, k_width);
    EXPECT_EQ((*result).height, k_height);
    EXPECT_EQ((*result).natural_position, landor::geo::Coord32(0, 0));
}


TEST(LayerSource, OpensUnknownRecordLargerThanManyReadBlocks)
{
    // A single unknown metadata record far larger than one bounded Storage
    // read. It must be streamed and skipped without being buffered, so its
    // length is not limited by the read-block size.
    const std::string header =
        "V:1.0\nD:4 3\nP:0 0\n"
        + std::string("Z:")
        + std::string(4096, 'a')
        + "\n\n";

    RecordingStorage storage;
    storage.set_bytes(header + make_grid(k_width, k_height));

    const auto result = open_layer_source(storage, k_source);

    ASSERT_TRUE(result);
    const auto& layout = *result;
    EXPECT_EQ(layout.width, k_width);
    EXPECT_EQ(layout.height, k_height);
    EXPECT_EQ(layout.data_offset, static_cast<Offset>(header.size()));

    // The long record was skipped without being retained: every read still
    // starts inside the metadata region, and the final bounded read extends
    // past the terminator by at most one parser block.
    for (const auto& request : storage.requests())
    {
        EXPECT_LT(request.offset, layout.data_offset);
    }
    EXPECT_GE(storage.total_requested_bytes(), layout.data_offset);
    EXPECT_LE(
        storage.total_requested_bytes() - layout.data_offset,
        landor::geo::layer_source_metadata_read_size);
}


TEST(LayerSource, OpensUnknownRecordWhoseKeySpansReadBlocks)
{
    // An unknown record whose first byte is the last byte of the first
    // bounded read block (offset 511) and whose second byte is the first
    // byte of the next block: the line-classification state must survive a
    // block boundary.
    //
    // "V:1.0\nD:4 3\nP:0 0\n" occupies offsets 0..17. One padding line
    // fills offsets 18..510 so the special line starts at offset 511.
    std::string header;
    header += "V:1.0\n";
    header += "D:4 3\n";
    header += "P:0 0\n";
    header += "A:";
    header += std::string(511 - 18 - 3, 'b');  // "A:" + payload + '\n'
    header += '\n';

    // 'V' at offset 511, 'X' at offset 512: a two-character key that is
    // NOT the known 'V' record, split across two read blocks.
    header += "VX:";
    header += std::string(300, 'c');
    header += "\n\n";

    ASSERT_EQ(header.find("VX:"), 511u);

    RecordingStorage storage;
    storage.set_bytes(header + make_grid(k_width, k_height));

    const auto result = open_layer_source(storage, k_source);

    ASSERT_TRUE(result);
    EXPECT_EQ((*result).width, k_width);
    EXPECT_EQ((*result).height, k_height);
    EXPECT_EQ((*result).data_offset, static_cast<Offset>(header.size()));
}


TEST(LayerSource, ReportsFirstCellOffset)
{
    RecordingStorage storage;
    LayerSourceLayout layout {};

    ASSERT_TRUE(open_default(storage, layout));

    const auto offset = layout.cell_offset(0, 0);
    ASSERT_TRUE(offset);
    EXPECT_EQ(*offset, layout.data_offset);
}


TEST(LayerSource, ReportsLastCellOffset)
{
    RecordingStorage storage;
    LayerSourceLayout layout {};

    ASSERT_TRUE(open_default(storage, layout));

    const auto offset = layout.cell_offset(k_width - 1u, k_height - 1u);
    ASSERT_TRUE(offset);
    EXPECT_EQ(
        *offset,
        static_cast<Offset>(
            layout.data_offset
            + static_cast<std::uint64_t>(k_height - 1u) * layout.row_stride
            + (k_width - 1u)));
}


TEST(LayerSource, CellOffsetIsAbsentOutOfRange)
{
    RecordingStorage storage;
    LayerSourceLayout layout {};

    ASSERT_TRUE(open_default(storage, layout));

    EXPECT_FALSE(layout.cell_offset(k_width, 0));
    EXPECT_FALSE(layout.cell_offset(0, k_height));
    EXPECT_FALSE(layout.cell_offset(k_width, k_height));
}


TEST(LayerSource, ReadsBoundedRowFragment)
{
    RecordingStorage storage;
    LayerSourceLayout layout {};

    ASSERT_TRUE(open_default(storage, layout));
    storage.clear_requests();

    std::array<std::byte, 2> destination {};
    const auto result = read_cells(layout, storage, k_source, /*row=*/1, /*x=*/2, destination);

    ASSERT_TRUE(result);
    ASSERT_EQ(storage.requests().size(), 2u);

    const auto& request = storage.requests()[0];
    EXPECT_EQ(
        request.offset,
        static_cast<Offset>(
            layout.data_offset + layout.row_stride + 2u));
    EXPECT_EQ(request.size, 2u);

    const auto& terminator_request = storage.requests()[1];
    EXPECT_EQ(
        terminator_request.offset,
        static_cast<Offset>(
            layout.data_offset + layout.row_stride + k_width));
    EXPECT_EQ(terminator_request.size, 1u);

    EXPECT_EQ(static_cast<unsigned char>(destination[0]),
              static_cast<unsigned char>(cell_byte(2, 1)));
    EXPECT_EQ(static_cast<unsigned char>(destination[1]),
              static_cast<unsigned char>(cell_byte(3, 1)));
}


TEST(LayerSource, ReadsPreserveSpaceCellsUnchanged)
{
    RecordingStorage storage;
    LayerSourceLayout layout {};

    ASSERT_TRUE(open_default(storage, layout));

    std::array<std::byte, 2> destination {};
    const auto result = read_cells(layout, storage, k_source, /*row=*/0, /*x=*/0, destination);

    ASSERT_TRUE(result);

    // cell (1, 0) is an authored no-contribution cell: literal 0x20.
    EXPECT_EQ(destination[1], std::byte{0x20});
    EXPECT_FALSE(has_contribution(destination[1]));
    EXPECT_TRUE(has_contribution(destination[0]));
    EXPECT_EQ(static_cast<unsigned char>(destination[0]),
              static_cast<unsigned char>(cell_byte(0, 0)));
}


TEST(LayerSource, ReadsPreserveLiteralNonSpaceBytes)
{
    RecordingStorage storage;
    LayerSourceLayout layout {};

    ASSERT_TRUE(open_default(storage, layout));

    // Row 1 contains letter cells and the literal digit '7' at (3, 1).
    std::array<std::byte, 4> destination {};
    const auto result = read_cells(layout, storage, k_source, /*row=*/1, /*x=*/0, destination);

    ASSERT_TRUE(result);

    EXPECT_EQ(static_cast<unsigned char>(destination[3]), '7');
    for (std::size_t i = 0; i < destination.size(); ++i)
    {
        EXPECT_EQ(static_cast<unsigned char>(destination[i]),
                  static_cast<unsigned char>(cell_byte(static_cast<std::uint32_t>(i), 1)));
    }
}


TEST(LayerSource, ReadEndingAtRowBoundarySucceeds)
{
    RecordingStorage storage;
    LayerSourceLayout layout {};

    ASSERT_TRUE(open_default(storage, layout));
    storage.clear_requests();

    // The last two cells of the row: the read ends exactly at the row LF
    // without crossing it.
    std::array<std::byte, 2> destination {};
    const auto result = read_cells(layout, storage, k_source, /*row=*/0, /*x=*/2, destination);

    ASSERT_TRUE(result);
    ASSERT_EQ(storage.requests().size(), 2u);

    const auto& request = storage.requests()[0];
    EXPECT_EQ(
        static_cast<std::uint64_t>(request.offset) + request.size,
        static_cast<std::uint64_t>(layout.data_offset) + k_width);

    const auto& terminator_request = storage.requests()[1];
    EXPECT_EQ(
        terminator_request.offset,
        static_cast<Offset>(layout.data_offset + k_width));
    EXPECT_EQ(terminator_request.size, 1u);

    EXPECT_EQ(static_cast<unsigned char>(destination[0]),
              static_cast<unsigned char>(cell_byte(2, 0)));
    EXPECT_EQ(static_cast<unsigned char>(destination[1]),
              static_cast<unsigned char>(cell_byte(3, 0)));
}


TEST(LayerSource, RejectsNewlineInTouchedCellRange)
{
    RecordingStorage storage;
    std::string source = make_default_source();
    storage.set_bytes(source);

    const auto opened = open_layer_source(storage, k_source);
    ASSERT_TRUE(opened);
    const LayerSourceLayout layout = *opened;

    // Keep the source size unchanged but put structural LF in an actual
    // cell position. Opening intentionally does not scan the data grid.
    const auto corrupt_offset = static_cast<std::size_t>(
        layout.data_offset + layout.row_stride + 2u);
    source[corrupt_offset] = '\n';
    storage.set_bytes(source);
    storage.clear_requests();

    std::array<std::byte, 2> destination {};
    const auto result =
        read_cells(layout, storage, k_source, /*row=*/1, /*x=*/2, destination);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), LayerSourceError::MalformedData);

    // The malformed byte is found in the requested fragment, so there is no
    // reason to issue the terminator read afterwards.
    ASSERT_EQ(storage.requests().size(), 1u);
}


TEST(LayerSource, RejectsNonNewlineTouchedRowTerminator)
{
    RecordingStorage storage;
    std::string source = make_default_source();
    storage.set_bytes(source);

    const auto opened = open_layer_source(storage, k_source);
    ASSERT_TRUE(opened);
    const LayerSourceLayout layout = *opened;

    // Preserve total size while corrupting row 1's structural terminator.
    const auto terminator_offset = static_cast<std::size_t>(
        layout.data_offset + layout.row_stride + k_width);
    source[terminator_offset] = 'X';
    storage.set_bytes(source);
    storage.clear_requests();

    std::array<std::byte, 1> destination {};
    const auto result =
        read_cells(layout, storage, k_source, /*row=*/1, /*x=*/0, destination);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), LayerSourceError::MalformedData);
    ASSERT_EQ(storage.requests().size(), 2u);

    const auto& terminator_request = storage.requests()[1];
    EXPECT_EQ(
        terminator_request.offset,
        static_cast<Offset>(
            layout.data_offset + layout.row_stride + k_width));
    EXPECT_EQ(terminator_request.size, 1u);
}


TEST(LayerSource, RejectsReadAcrossRowBoundary)
{
    RecordingStorage storage;
    LayerSourceLayout layout {};

    ASSERT_TRUE(open_default(storage, layout));
    storage.clear_requests();

    std::array<std::byte, 2> destination {};
    const auto result = read_cells(layout, storage, k_source, /*row=*/0, /*x=*/3, destination);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), LayerSourceError::OutOfRange);
    EXPECT_TRUE(storage.requests().empty());

    // A row past the last one is rejected as well.
    const auto past_bottom =
        read_cells(layout, storage, k_source, /*row=*/k_height, /*x=*/0, destination);
    ASSERT_FALSE(past_bottom);
    EXPECT_EQ(past_bottom.error(), LayerSourceError::OutOfRange);
    EXPECT_TRUE(storage.requests().empty());
}


TEST(LayerSource, RejectsOutOfRangeCellReads)
{
    RecordingStorage storage;
    LayerSourceLayout layout {};

    ASSERT_TRUE(open_default(storage, layout));

    std::array<std::byte, 1> destination {};

    const auto column_past_edge =
        read_cells(layout, storage, k_source, /*row=*/0, /*x=*/k_width, destination);
    ASSERT_FALSE(column_past_edge);
    EXPECT_EQ(column_past_edge.error(), LayerSourceError::OutOfRange);
}


TEST(LayerSource, ReadCellRangeBoundariesAreExact)
{
    RecordingStorage storage;
    LayerSourceLayout layout {};

    ASSERT_TRUE(open_default(storage, layout));

    // The last single cell of the row is the maximal valid fragment...
    std::array<std::byte, 1> one {};
    EXPECT_TRUE(
        read_cells(layout, storage, k_source, /*row=*/0, /*x=*/k_width - 1u, one));

    // ...but extending one cell past it would cross the row LF.
    std::array<std::byte, 2> two {};
    const auto cross_row =
        read_cells(layout, storage, k_source, /*row=*/0, /*x=*/k_width - 1u, two);
    ASSERT_FALSE(cross_row);
    EXPECT_EQ(cross_row.error(), LayerSourceError::OutOfRange);

    // A full row from column 0 fits exactly.
    std::array<std::byte, 4> full {};
    EXPECT_TRUE(read_cells(layout, storage, k_source, /*row=*/0, /*x=*/0, full));

    // Starting one cell later overflows the row by exactly one cell.
    EXPECT_FALSE(read_cells(layout, storage, k_source, /*row=*/0, /*x=*/1u, full));

    // x == width is out of range for a non-empty fragment...
    EXPECT_FALSE(read_cells(layout, storage, k_source, /*row=*/0, /*x=*/k_width, one));

    // ...but an empty fragment issues no read at all.
    EXPECT_TRUE(read_cells(
        layout, storage, k_source, /*row=*/0, /*x=*/k_width, std::span<std::byte>{}));
}


TEST(LayerSource, ReadCellRangeRejectsHugeColumnWithoutWraparound)
{
    RecordingStorage storage;
    LayerSourceLayout layout {};

    ASSERT_TRUE(open_default(storage, layout));
    storage.clear_requests();

    // A column this large could only pass a bound check whose arithmetic
    // wrapped in a 32-bit size_t; it must be rejected before the fragment
    // size is ever combined with it.
    constexpr std::uint32_t huge_x = std::numeric_limits<std::uint32_t>::max();

    std::array<std::byte, 1> one {};
    const auto result =
        read_cells(layout, storage, k_source, /*row=*/0, /*x=*/huge_x, one);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), LayerSourceError::OutOfRange);

    const auto empty_result = read_cells(
        layout, storage, k_source, /*row=*/0, /*x=*/huge_x, std::span<std::byte>{});
    ASSERT_FALSE(empty_result);
    EXPECT_EQ(empty_result.error(), LayerSourceError::OutOfRange);

    EXPECT_TRUE(storage.requests().empty());
}


TEST(LayerSource, RejectsSourceShorterThanDeclared)
{
    RecordingStorage storage;
    std::string source = make_default_source();
    source.erase(source.size() - 3u);
    storage.set_bytes(source);

    const auto result = open_layer_source(storage, k_source);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), LayerSourceError::SizeMismatch);
}


TEST(LayerSource, RejectsSourceLongerThanDeclared)
{
    RecordingStorage storage;
    std::string source = make_default_source();
    source += "trailing";
    storage.set_bytes(source);

    const auto result = open_layer_source(storage, k_source);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), LayerSourceError::SizeMismatch);
}


TEST(LayerSource, RejectsMissingMetadataTerminator)
{
    RecordingStorage storage;

    // No final blank line: the data grid follows the last record directly.
    std::string source;
    const char* lines[] = {"V:1.0", "D:4 3", "P:0 0"};
    for (const auto* line : lines)
    {
        source += line;
        source += '\n';
    }
    source += make_grid(k_width, k_height);

    storage.set_bytes(source);

    const auto result = open_layer_source(storage, k_source);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), LayerSourceError::MissingTerminator);
}


TEST(LayerSource, RejectsMissingRequiredRecord)
{
    const std::string grid = make_grid(k_width, k_height);

    const std::string cases[] = {
        make_header({"V:1.0", "D:4 3"}) + grid,          // missing P
        make_header({"D:4 3", "P:0 0"}) + grid,          // missing V
        make_header({"V:1.0", "P:0 0"}) + grid,          // missing D
    };

    for (const auto& source : cases)
    {
        RecordingStorage storage;
        storage.set_bytes(source);

        const auto result = open_layer_source(storage, k_source);

        ASSERT_FALSE(result);
        EXPECT_EQ(result.error(), LayerSourceError::MissingRecord);
    }
}


TEST(LayerSource, RejectsDuplicateRequiredRecord)
{
    const std::string grid = make_grid(k_width, k_height);

    const std::string cases[] = {
        make_header({"V:1.0", "D:4 3", "P:0 0", "V:1.0"}) + grid,
        make_header({"V:1.0", "D:4 3", "D:4 3", "P:0 0"}) + grid,
        make_header({"V:1.0", "D:4 3", "P:0 0", "P:0 0"}) + grid,
    };

    for (const auto& source : cases)
    {
        RecordingStorage storage;
        storage.set_bytes(source);

        const auto result = open_layer_source(storage, k_source);

        ASSERT_FALSE(result);
        EXPECT_EQ(result.error(), LayerSourceError::DuplicateRecord);
    }
}


TEST(LayerSource, RejectsMalformedNumbers)
{
    const std::string grid = make_grid(k_width, k_height);

    const std::string cases[] = {
        make_header({"V:1.0", "D:4 x", "P:0 0"}) + grid,    // non-numeric
        make_header({"V:1.0", "D:4", "P:0 0"}) + grid,      // one number
        make_header({"V:1.0", "D:4 3 2", "P:0 0"}) + grid,  // three numbers
        make_header({"V:1.0", "D:0x 3", "P:0 0"}) + grid,   // hex without digits
        make_header({"V:1.0", "D:0X4 3", "P:0 0"}) + grid,  // wrong hex prefix
        make_header({"V:1.0", "D:4 3", "P:+1 2"}) + grid,   // '+' not allowed
        make_header({"V:1.0", "D:4 3", "P:1 2 "}) + grid,   // trailing space
        make_header({"V:1.0", "D:4 3", "P:1"}) + grid,      // one number
    };

    for (const auto& source : cases)
    {
        RecordingStorage storage;
        storage.set_bytes(source);

        const auto result = open_layer_source(storage, k_source);

        ASSERT_FALSE(result);
        EXPECT_EQ(result.error(), LayerSourceError::MalformedMetadata);
    }
}


TEST(LayerSource, RejectsMetadataLineWithoutColon)
{
    // A line without a colon is malformed even when it is not a known
    // key: streaming skip applies to records, not to arbitrary lines.
    const std::string source =
        make_header({"V:1.0", "GARBAGE", "D:4 3", "P:0 0"})
        + make_grid(k_width, k_height);

    RecordingStorage storage;
    storage.set_bytes(source);

    const auto result = open_layer_source(storage, k_source);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), LayerSourceError::MalformedMetadata);
}


TEST(LayerSource, RejectsKnownRecordWithOversizedPayload)
{
    // A known-record payload longer than any well-formed payload (41
    // bytes) cannot be valid: the excess is streamed away and the record
    // is rejected.
    const std::string source =
        "V:1.0\n"
        + std::string("D:")
        + std::string(100, '9')
        + " 3\nP:0 0\n\n"
        + make_grid(k_width, k_height);

    RecordingStorage storage;
    storage.set_bytes(source);

    const auto result = open_layer_source(storage, k_source);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), LayerSourceError::MalformedMetadata);
}


TEST(LayerSource, RejectsUnsupportedVersion)
{
    const std::string grid = make_grid(k_width, k_height);

    const std::string cases[] = {
        make_header({"V:2.0", "D:4 3", "P:0 0"}) + grid,
        make_header({"V:1.1", "D:4 3", "P:0 0"}) + grid,
    };

    for (const auto& source : cases)
    {
        RecordingStorage storage;
        storage.set_bytes(source);

        const auto result = open_layer_source(storage, k_source);

        ASSERT_FALSE(result);
        EXPECT_EQ(result.error(), LayerSourceError::UnsupportedVersion);
    }
}


TEST(LayerSource, RejectsZeroDimension)
{
    const std::string grid = make_grid(k_width, k_height);

    const std::string cases[] = {
        make_header({"V:1.0", "D:0 3", "P:0 0"}) + grid,
        make_header({"V:1.0", "D:4 0", "P:0 0"}) + grid,
    };

    for (const auto& source : cases)
    {
        RecordingStorage storage;
        storage.set_bytes(source);

        const auto result = open_layer_source(storage, k_source);

        ASSERT_FALSE(result);
        EXPECT_EQ(result.error(), LayerSourceError::ZeroDimension);
    }
}


TEST(LayerSource, RejectsArithmeticOverflow)
{
    const std::string grid = make_grid(k_width, k_height);

    // The declared grids cannot fit in storage::Size / storage::Offset.
    const std::string cases[] = {
        make_header({"V:1.0", "D:65536 65536", "P:0 0"}) + grid,
        make_header({"V:1.0", "D:4294967296 2", "P:0 0"}) + grid,
    };

    for (const auto& source : cases)
    {
        RecordingStorage storage;
        storage.set_bytes(source);

        const auto result = open_layer_source(storage, k_source);

        ASSERT_FALSE(result);
        EXPECT_EQ(result.error(), LayerSourceError::Overflow);
    }

    // Natural positions outside int32 are not representable either.
    const std::string position_cases[] = {
        make_header({"V:1.0", "D:4 3", "P:2147483648 0"}) + grid,
        make_header({"V:1.0", "D:4 3", "P:-2147483649 0"}) + grid,
    };

    for (const auto& source : position_cases)
    {
        RecordingStorage storage;
        storage.set_bytes(source);

        const auto result = open_layer_source(storage, k_source);

        ASSERT_FALSE(result);
        EXPECT_EQ(result.error(), LayerSourceError::Overflow);
    }
}


TEST(LayerSource, RejectsCrlfMetadata)
{
    // CRLF is not part of the format: a CRLF-terminated blank line is
    // 0x0D 0x0A 0x0D 0x0A and never contains the 0x0A 0x0A terminator.
    const std::string source =
        "V:1.0\r\nD:4 3\r\nP:0 0\r\n\r\n" + make_grid(k_width, k_height);

    RecordingStorage storage;
    storage.set_bytes(source);

    const auto result = open_layer_source(storage, k_source);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), LayerSourceError::MissingTerminator);
}


TEST(LayerSource, MapsStorageSizeFailure)
{
    RecordingStorage storage;
    storage.set_bytes(make_default_source());
    storage.fail_size_with(Error::read_failed);

    const auto result = open_layer_source(storage, k_source);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), LayerSourceError::StorageFailed);
}


TEST(LayerSource, MapsStorageReadFailureDuringOpen)
{
    RecordingStorage storage;
    storage.set_bytes(make_default_source());
    storage.fail_reads_with(Error::read_failed);

    const auto result = open_layer_source(storage, k_source);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), LayerSourceError::StorageFailed);
}


TEST(LayerSource, MapsStorageReadFailureDuringCellRead)
{
    RecordingStorage storage;
    LayerSourceLayout layout {};

    ASSERT_TRUE(open_default(storage, layout));
    storage.fail_reads_with(Error::read_failed);

    std::array<std::byte, 2> destination {};
    const auto result = read_cells(layout, storage, k_source, /*row=*/0, /*x=*/0, destination);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), LayerSourceError::StorageFailed);
}


TEST(LayerSource, MapsUnknownSourceAsStorageFailure)
{
    RecordingStorage storage;
    storage.set_bytes(make_default_source());

    const auto result = open_layer_source(storage, k_unknown_source);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), LayerSourceError::StorageFailed);
}


TEST(LayerSource, HasContributionClassifiesGenericCellBytes)
{
    EXPECT_FALSE(has_contribution(std::byte{0x20}));
    EXPECT_FALSE(has_contribution(std::byte{0x0A}));
    EXPECT_TRUE(has_contribution(std::byte{'#'}));
    EXPECT_TRUE(has_contribution(std::byte{'0'}));
}


TEST(LayerSource, OpenDoesNotReadTheDataSection)
{
    // Small metadata header, substantially larger data grid.
    const std::uint32_t width = 8;
    const std::uint32_t height = 500;
    const std::string header = make_header({"V:1.0", "D:8 500", "P:0 0"});
    const std::string source = header + make_grid(width, height);

    RecordingStorage storage;
    storage.set_bytes(source);

    const auto result = open_layer_source(storage, k_source);

    ASSERT_TRUE(result);
    const auto& layout = *result;

    // No read may target the data grid: every read starts inside the
    // metadata region...
    for (const auto& request : storage.requests())
    {
        EXPECT_LT(request.offset, layout.data_offset);
    }

    // ...and the final bounded read extends past the terminator by at most
    // one parser block, far short of the data grid as a whole.
    EXPECT_GE(storage.total_requested_bytes(), layout.data_offset);
    EXPECT_LE(
        storage.total_requested_bytes() - layout.data_offset,
        landor::geo::layer_source_metadata_read_size);
    EXPECT_LT(storage.total_requested_bytes(), layout.source_size);

    // A small row fragment requests that fragment plus one byte for the
    // touched row's structural LF terminator.
    storage.clear_requests();

    std::array<std::byte, 4> destination {};
    const auto read = read_cells(layout, storage, k_source, /*row=*/250, /*x=*/4, destination);

    ASSERT_TRUE(read);
    ASSERT_EQ(storage.requests().size(), 2u);

    const auto& request = storage.requests()[0];
    EXPECT_EQ(
        request.offset,
        static_cast<Offset>(
            layout.data_offset
            + static_cast<std::uint64_t>(250) * layout.row_stride
            + 4u));
    EXPECT_EQ(request.size, 4u);

    const auto& terminator_request = storage.requests()[1];
    EXPECT_EQ(
        terminator_request.offset,
        static_cast<Offset>(
            layout.data_offset
            + static_cast<std::uint64_t>(250) * layout.row_stride
            + layout.width));
    EXPECT_EQ(terminator_request.size, 1u);

    for (std::size_t i = 0; i < destination.size(); ++i)
    {
        EXPECT_EQ(
            static_cast<unsigned char>(destination[i]),
            static_cast<unsigned char>(
                cell_byte(static_cast<std::uint32_t>(4 + i), 250)));
    }
}


TEST(LayerSource, WritesExactlyTheRequestedCellRange)
{
    RecordingStorage storage;
    LayerSourceLayout layout {};

    ASSERT_TRUE(open_default(storage, layout));
    const std::string before = storage.bytes();
    storage.clear_requests();
    storage.clear_writes();

    const std::byte data[] = {std::byte{'X'}, std::byte{'Y'}};
    const auto result = write_cells(
        layout, storage, k_source, /*row=*/1, /*x=*/1,
        std::span<const std::byte>(data));

    ASSERT_TRUE(result);
    ASSERT_EQ(storage.writes().size(), 1u);

    // Exactly the requested fragment: column 1 of row 1, two cells.
    const auto& write = storage.writes()[0];
    EXPECT_EQ(
        write.offset,
        static_cast<Offset>(
            layout.data_offset
            + static_cast<std::uint64_t>(1u) * layout.row_stride + 1u));
    EXPECT_EQ(write.size, 2u);

    const std::string& after = storage.bytes();
    EXPECT_EQ(after.size(), before.size());
    EXPECT_EQ(static_cast<unsigned char>(after[write.offset]), 'X');
    EXPECT_EQ(static_cast<unsigned char>(after[write.offset + 1u]), 'Y');

    // No byte outside the written range changed, including the touched
    // row terminator: the write never moves or overwrites it.
    for (std::size_t i = 0; i < before.size(); ++i)
    {
        if (i >= write.offset && i < write.offset + write.size)
        {
            continue;
        }
        EXPECT_EQ(after[i], before[i]) << "byte " << i << " changed";
    }

    // The touched row still reads back normally through the reader.
    std::array<std::byte, 4> destination {};
    EXPECT_TRUE(read_cells(
        layout, storage, k_source, /*row=*/1, /*x=*/0, destination));
}


TEST(LayerSource, EmptyWritePerformsNoStorageIO)
{
    RecordingStorage storage;
    LayerSourceLayout layout {};

    ASSERT_TRUE(open_default(storage, layout));
    storage.clear_requests();
    storage.clear_writes();

    // An empty fragment succeeds, including at x == width.
    const std::byte* no_bytes = nullptr;
    EXPECT_TRUE(write_cells(
        layout, storage, k_source, /*row=*/0, /*x=*/0,
        std::span<const std::byte>(no_bytes, 0)));
    EXPECT_TRUE(write_cells(
        layout, storage, k_source, /*row=*/0, /*x=*/layout.width,
        std::span<const std::byte>(no_bytes, 0)));

    EXPECT_TRUE(storage.requests().empty());
    EXPECT_TRUE(storage.writes().empty());
}


TEST(LayerSource, WriteRejectsOutOfRangeRanges)
{
    RecordingStorage storage;
    LayerSourceLayout layout {};

    ASSERT_TRUE(open_default(storage, layout));
    storage.clear_requests();
    storage.clear_writes();

    const std::byte one[] = {std::byte{'X'}};
    const std::byte two[] = {std::byte{'X'}, std::byte{'X'}};

    auto result = write_cells(
        layout, storage, k_source,
        /*row=*/layout.height, /*x=*/0, std::span<const std::byte>(one));
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), LayerSourceError::OutOfRange);

    result = write_cells(
        layout, storage, k_source, /*row=*/0, /*x=*/layout.width,
        std::span<const std::byte>(one));
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), LayerSourceError::OutOfRange);

    // The fragment would cross the touched row's terminator.
    result = write_cells(
        layout, storage, k_source, /*row=*/0, /*x=*/layout.width - 1u,
        std::span<const std::byte>(two));
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), LayerSourceError::OutOfRange);

    // A column far beyond the width must not wrap or clamp.
    result = write_cells(
        layout, storage, k_source, /*row=*/0, /*x=*/4000000000u,
        std::span<const std::byte>(one));
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), LayerSourceError::OutOfRange);

    // No I/O happened on any rejected range.
    EXPECT_TRUE(storage.requests().empty());
    EXPECT_TRUE(storage.writes().empty());
}


TEST(LayerSource, WriteRejectsNewlineInCellData)
{
    RecordingStorage storage;
    LayerSourceLayout layout {};

    ASSERT_TRUE(open_default(storage, layout));
    const std::string before = storage.bytes();
    storage.clear_requests();
    storage.clear_writes();

    // LF is structural in v1 and can never be a cell value.
    const std::byte data[] = {std::byte{'X'}, std::byte{0x0A}, std::byte{'Y'}};
    const auto result = write_cells(
        layout, storage, k_source, /*row=*/0, /*x=*/0,
        std::span<const std::byte>(data));

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), LayerSourceError::MalformedData);

    // Rejected before any Storage I/O, so nothing changed.
    EXPECT_TRUE(storage.requests().empty());
    EXPECT_TRUE(storage.writes().empty());
    EXPECT_EQ(storage.bytes(), before);
}


TEST(LayerSource, WriteRejectsNonNewlineTouchedRowTerminator)
{
    // Corrupt row 1's structural terminator byte before opening.
    std::string source = make_default_source();
    const std::size_t terminator =
        make_header({"V:1.0", "D:4 3", "P:0 0"}).size()
        + static_cast<std::size_t>(k_width + 1u) + k_width;
    source[terminator] = 'X';

    RecordingStorage storage;
    storage.set_bytes(source);

    const auto opened = open_layer_source(storage, k_source);
    ASSERT_TRUE(opened);
    const auto& layout = *opened;
    storage.clear_requests();
    storage.clear_writes();

    const std::byte data[] = {std::byte{'X'}};
    const auto result = write_cells(
        layout, storage, k_source, /*row=*/1, /*x=*/0,
        std::span<const std::byte>(data));

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), LayerSourceError::MalformedData);

    // The terminator read happened, but no write did.
    EXPECT_EQ(storage.requests().size(), 1u);
    EXPECT_TRUE(storage.writes().empty());
}


TEST(LayerSource, WriteAllowsAsciiSpaceCells)
{
    RecordingStorage storage;
    LayerSourceLayout layout {};

    ASSERT_TRUE(open_default(storage, layout));
    storage.clear_writes();

    // ASCII space is a valid no-contribution cell byte, not an error.
    const std::byte data[] = {std::byte{0x20}};
    const auto result = write_cells(
        layout, storage, k_source, /*row=*/0, /*x=*/0,
        std::span<const std::byte>(data));

    ASSERT_TRUE(result);
    ASSERT_EQ(storage.writes().size(), 1u);
    EXPECT_EQ(
        static_cast<unsigned char>(
            storage.bytes()[storage.writes()[0].offset]),
        0x20);
    EXPECT_FALSE(has_contribution(std::byte{0x20}));
}


TEST(LayerSource, WriteMapsCellWriteStorageFailure)
{
    RecordingStorage storage;
    LayerSourceLayout layout {};

    ASSERT_TRUE(open_default(storage, layout));
    storage.fail_writes_with(Error::write_failed);

    const std::byte data[] = {std::byte{'X'}};
    const auto result = write_cells(
        layout, storage, k_source, /*row=*/0, /*x=*/0,
        std::span<const std::byte>(data));

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), LayerSourceError::StorageFailed);
}


TEST(LayerSource, WriteMapsTerminatorReadStorageFailure)
{
    RecordingStorage storage;
    LayerSourceLayout layout {};

    ASSERT_TRUE(open_default(storage, layout));
    storage.fail_reads_with(Error::read_failed);

    const std::byte data[] = {std::byte{'X'}};
    const auto result = write_cells(
        layout, storage, k_source, /*row=*/0, /*x=*/0,
        std::span<const std::byte>(data));

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), LayerSourceError::StorageFailed);
    EXPECT_TRUE(storage.writes().empty());
}

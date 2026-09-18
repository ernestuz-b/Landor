#pragma once

#include "coord.hpp"
#include "../storage/storage_contract.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <span>


namespace landor::geo
{

/**
 * Errors produced by the streaming layer source reader.
 *
 * The values keep the failures distinct so callers can react to them
 * separately: storage boundary problems, format problems, and access
 * problems are different classes of outcome.
 */
enum class LayerSourceError : std::uint8_t
{
    /// The backing storage reported a failure for size() or read().
    StorageFailed,

    /// A metadata line or integer does not follow the version 1.0 grammar
    /// (CRLF line endings, stray separators, unparseable numbers, ...).
    MalformedMetadata,

    /// No 0x0A 0x0A metadata terminator exists in the source.
    MissingTerminator,

    /// One of the required V / D / P records does not appear in metadata.
    MissingRecord,

    /// A required V / D / P record appears more than once.
    DuplicateRecord,

    /// The source declares a version other than 1.0.
    UnsupportedVersion,

    /// A declared width or height is zero.
    ZeroDimension,

    /// A parsed value or the derived stride/data-size arithmetic exceeds
    /// the supported widths (storage::Offset / storage::Size).
    Overflow,

    /// The expected total size derived from metadata differs from the
    /// Storage-reported source size.
    SizeMismatch,

    /// Dense row structure is malformed in data that was actually touched:
    /// LF appeared in a requested cell position, or the touched row's
    /// terminator byte is not LF.
    MalformedData,

    /// A requested cell or cell range lies outside the declared grid.
    OutOfRange
};


/**
 * Parsed layout of one version 1.0 dense layer source.
 *
 * This is the only source information retained after the metadata block has
 * been read. It is a small plain value: it owns no source data, holds no
 * pointer or handle into the backing source, and needs no cleanup.
 *
 * The dense grid itself stays in storage. Cell addressing uses only the
 * fields below:
 *
 *     offset = data_offset + y * row_stride + x
 *
 * Natural position does not participate in that calculation; it is the
 * authored world position of the represented extent.
 */
struct LayerSourceLayout
{
    /// Format version accepted by the reader (always 1.0 on success).
    std::uint8_t version_major = 0;
    std::uint8_t version_minor = 0;

    /// Declared grid dimensions in cells.
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    /// Authored natural position of the represented layer extent.
    Coord32 natural_position {};

    /// First byte of the dense data section, immediately after the
    /// metadata-terminating 0x0A 0x0A.
    storage::Offset data_offset = 0;

    /// Bytes per row in the backing source: width + 1 (cells plus LF).
    storage::Offset row_stride = 0;

    /// data_offset + height * row_stride. Equals source_size on success.
    storage::Size expected_size = 0;

    /// Storage-reported size of the backing source.
    storage::Size source_size = 0;


    /**
     * Physical offset of one source-local cell.
     *
     * Returns nullopt when (x, y) lies outside the declared grid.
     * Out-of-range coordinates are neither clamped nor wrapped.
     */
    [[nodiscard]] constexpr std::optional<storage::Offset>
    cell_offset(std::uint32_t x, std::uint32_t y) const noexcept
    {
        if (x >= width || y >= height)
        {
            return std::nullopt;
        }

        const std::uint64_t offset =
            static_cast<std::uint64_t>(data_offset)
            + static_cast<std::uint64_t>(y) * static_cast<std::uint64_t>(row_stride)
            + x;

        return static_cast<storage::Offset>(offset);
    }
};


/// Maximum number of bytes one metadata read requests from storage.
///
/// The metadata block and individual metadata lines may span as many bounded
/// reads as needed. Because the terminator cannot be located before it is read,
/// the final metadata read may extend slightly past the terminator, but never
/// by more than this many bytes.
inline constexpr std::size_t layer_source_metadata_read_size = 512;


/**
 * Open and parse one version 1.0 dense layer source through Storage.
 *
 * Reads the metadata block with bounded reads until the first
 * 0x0A 0x0A terminator and validates what can be validated cheaply:
 *
 *   - the source exists and size() succeeds;
 *   - the metadata terminator exists;
 *   - the required V / D / P records exist, are unique and are valid;
 *   - the version is 1.0;
 *   - dimensions are non-zero;
 *   - stride / data-size / expected-size arithmetic does not overflow;
 *   - the expected total size equals the Storage-reported source size.
 *
 * The dense data section is NOT read during open. A source may be much
 * larger than RAM; retaining the small parsed layout is all that is needed
 * for direct addressing afterwards. No read issued during open starts at or
 * beyond the terminator; at most one bounded block of data bytes is read
 * past it.
 *
 * Unknown metadata records are skipped without being interpreted; metadata
 * and individual metadata lines may span many bounded reads. Line-level
 * format errors encountered before the terminator are reported when the
 * terminator is found; a source without any terminator is diagnosed as
 * MissingTerminator, which subsumes them. Whole-file structural scanning is
 * deliberately not part of normal opening.
 */
template<storage::StorageBackend Backend>
[[nodiscard]] std::expected<LayerSourceLayout, LayerSourceError>
open_layer_source(const Backend& storage, storage::SourceId source);


/**
 * Read consecutive cells from one source row into destination.
 *
 * destination.size() cells are read starting at column x of the given row.
 * A non-empty request performs one bounded Storage read for that fragment,
 * then one one-byte read of the touched row's structural LF terminator.
 * The fragment never crosses the terminator: x + destination.size() must not
 * exceed the declared width. Row or column outside the grid fails with
 * OutOfRange rather than clamping or wrapping.
 *
 * A LF byte in a requested cell position, or a non-LF byte at the touched
 * row terminator, fails with MalformedData. Empty fragments perform no
 * Storage reads. Other returned bytes are literal: ASCII space (0x20)
 * remains 0x20 and carries no layer interpretation.
 */
template<storage::StorageBackend Backend>
[[nodiscard]] std::expected<void, LayerSourceError>
read_cells(
    const LayerSourceLayout& layout,
    const Backend& storage,
    storage::SourceId source,
    std::uint32_t row,
    std::uint32_t x,
    std::span<std::byte> destination);


namespace detail
{

inline constexpr std::byte k_newline = std::byte{0x0A};
inline constexpr std::byte k_space = std::byte{0x20};
inline constexpr std::byte k_colon = std::byte{':'};
inline constexpr std::byte k_dot = std::byte{'.'};

inline constexpr std::int64_t k_int32_min = std::numeric_limits<std::int32_t>::min();
inline constexpr std::int64_t k_int32_max = std::numeric_limits<std::int32_t>::max();

inline constexpr std::size_t k_npos = static_cast<std::size_t>(-1);


/// Index of value in text, or k_npos when absent.
[[nodiscard]] constexpr std::size_t find_byte(
    std::span<const std::byte> text,
    std::byte value) noexcept
{
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == value)
        {
            return i;
        }
    }

    return k_npos;
}


/**
 * One integer token in a metadata line.
 *
 * Grammar: optional '-', then decimal digits, or '0x' followed by at least
 * one hexadecimal digit (0-9, a-f, A-F). No '+' sign, no empty value, and
 * no overflow past 64 bits.
 */
[[nodiscard]] constexpr bool parse_int_token(
    std::span<const std::byte> text,
    bool negative_allowed,
    std::int64_t& out) noexcept
{
    if (text.empty())
    {
        return false;
    }

    std::size_t index = 0;
    bool negative = false;

    if (text[index] == std::byte{'-'})
    {
        if (!negative_allowed)
        {
            return false;
        }

        negative = true;
        index = 1;
    }

    std::uint8_t base = 10;

    if (index + 1 < text.size()
        && text[index] == std::byte{'0'}
        && text[index + 1] == std::byte{'x'})
    {
        base = 16;
        index += 2;
    }

    std::uint64_t magnitude = 0;
    bool has_digits = false;

    for (; index < text.size(); ++index)
    {
        const int code = static_cast<int>(text[index]);

        const bool valid_digit =
            (code >= '0' && code <= '9')
            || (base == 16 && code >= 'a' && code <= 'f')
            || (base == 16 && code >= 'A' && code <= 'F');

        if (!valid_digit)
        {
            return false;
        }

        const std::uint64_t digit =
            (code >= 'a') ? static_cast<std::uint64_t>(code - 'a' + 10)
            : (code >= 'A') ? static_cast<std::uint64_t>(code - 'A' + 10)
            : static_cast<std::uint64_t>(code - '0');

        if (magnitude > (std::numeric_limits<std::uint64_t>::max() - digit) / base)
        {
            return false;
        }

        magnitude = magnitude * base + digit;
        has_digits = true;
    }

    if (!has_digits)
    {
        return false;
    }

    if (negative)
    {
        // 2^63 is the largest magnitude representable in int64.
        if (magnitude > (std::numeric_limits<std::uint64_t>::max() >> 1) + 1)
        {
            return false;
        }

        // Unsigned wraparound is defined, and C++20 unsigned-to-signed
        // conversion is well-defined, so this cannot overflow.
        out = static_cast<std::int64_t>(0 - magnitude);
    }
    else
    {
        out = static_cast<std::int64_t>(magnitude);
    }

    return true;
}


/// Parse exactly two integers separated by one ASCII space.
[[nodiscard]] constexpr bool parse_two_ints(
    std::span<const std::byte> payload,
    bool negative_allowed,
    std::int64_t& first,
    std::int64_t& second) noexcept
{
    const auto separator = find_byte(payload, k_space);
    if (separator == k_npos)
    {
        return false;
    }

    return parse_int_token(payload.first(separator), negative_allowed, first)
        && parse_int_token(payload.subspan(separator + 1), negative_allowed, second);
}


/// Parse a version payload of exactly the form "<major>.<minor>".
[[nodiscard]] constexpr bool parse_version(
    std::span<const std::byte> payload,
    std::uint64_t& major,
    std::uint64_t& minor) noexcept
{
    const auto separator = find_byte(payload, k_dot);
    if (separator == k_npos)
    {
        return false;
    }

    std::int64_t major_raw = 0;
    std::int64_t minor_raw = 0;

    if (!parse_int_token(payload.first(separator), false, major_raw))
    {
        return false;
    }

    if (!parse_int_token(payload.subspan(separator + 1), false, minor_raw))
    {
        return false;
    }

    major = static_cast<std::uint64_t>(major_raw);
    minor = static_cast<std::uint64_t>(minor_raw);
    return true;
}


/// The core metadata records accumulated while scanning one metadata block.
struct CoreRecords
{
    bool version_seen = false;
    std::uint8_t version_major = 0;
    std::uint8_t version_minor = 0;

    bool dimensions_seen = false;
    std::uint64_t width = 0;
    std::uint64_t height = 0;

    bool position_seen = false;
    std::int64_t position_x = 0;
    std::int64_t position_y = 0;
};


/// The longest payload a well-formed V / D / P record can have.
///
/// A version payload is two unsigned 64-bit integers (decimal or '0x'
/// hexadecimal) separated by '.', and a D / P payload is two such integers
/// (optionally signed for P) separated by a space. Each integer is at most
/// 20 characters, so a valid payload never exceeds 41 bytes. A known
/// record's payload can therefore be buffered in a fixed array of this
/// size; anything longer is necessarily malformed.
inline constexpr std::size_t k_max_known_payload = 64;


/**
 * Streaming classification of one metadata line.
 *
 * Only the first two bytes of a line decide whether it is a known record
 * (a single 'V', 'D' or 'P' followed by ':') or an unknown one, so the
 * state machine never needs to retain more than those two bytes plus a
 * bounded known payload. Unknown payloads are streamed away without
 * buffering, which lets a metadata record span any number of bounded
 * Storage reads.
 */
enum class LineState : std::uint8_t
{
    /// No line byte consumed yet.
    AtLineStart,

    /// First byte was 'V', 'D' or 'P'; the second byte decides.
    FirstKeyCandidate,

    /// Unknown record: consume until LF, remembering whether a colon was
    /// seen (a record without a colon is malformed).
    SkippingUnknown,

    /// Known 'V' record: buffering the bounded payload.
    ParsingVersion,

    /// Known 'D' record: buffering the bounded payload.
    ParsingDims,

    /// Known 'P' record: buffering the bounded payload.
    ParsingPos,
};


/**
 * Validate the accumulated core records and derive the direct-addressing
 * layout, once the terminator offset is known.
 */
[[nodiscard]] constexpr std::expected<LayerSourceLayout, LayerSourceError>
finalize(
    const CoreRecords& core,
    std::uint64_t data_offset,
    std::uint64_t total_size) noexcept
{
    if (!core.version_seen || !core.dimensions_seen || !core.position_seen)
    {
        return std::unexpected(LayerSourceError::MissingRecord);
    }

    const std::uint64_t width = core.width;
    const std::uint64_t height = core.height;

    if (width > std::numeric_limits<std::uint32_t>::max()
        || height > std::numeric_limits<std::uint32_t>::max())
    {
        return std::unexpected(LayerSourceError::Overflow);
    }

    const std::uint64_t row_stride = width + 1;

    if (height > std::numeric_limits<std::uint64_t>::max() / row_stride)
    {
        return std::unexpected(LayerSourceError::Overflow);
    }

    const std::uint64_t data_size = height * row_stride;

    if (data_size > std::numeric_limits<std::uint64_t>::max() - data_offset)
    {
        return std::unexpected(LayerSourceError::Overflow);
    }

    const std::uint64_t expected_size = data_offset + data_size;

    if (expected_size > std::numeric_limits<std::uint32_t>::max())
    {
        return std::unexpected(LayerSourceError::Overflow);
    }

    if (expected_size != total_size)
    {
        return std::unexpected(LayerSourceError::SizeMismatch);
    }

    LayerSourceLayout layout {};
    layout.version_major = core.version_major;
    layout.version_minor = core.version_minor;
    layout.width = static_cast<std::uint32_t>(width);
    layout.height = static_cast<std::uint32_t>(height);
    layout.natural_position = Coord32(
        static_cast<std::int32_t>(core.position_x),
        static_cast<std::int32_t>(core.position_y));
    layout.data_offset = static_cast<storage::Offset>(data_offset);
    layout.row_stride = static_cast<storage::Offset>(row_stride);
    layout.expected_size = static_cast<storage::Size>(expected_size);
    layout.source_size = static_cast<storage::Size>(total_size);

    return layout;
}

} // namespace detail


/**
 * Generic source-level meaning of one cell byte, without layer semantics.
 *
 *   0x20 ' '   no contribution from this source at this coordinate
 *   0x0A '\n'  structural row terminator (never a cell value)
 *   other      literal layer byte
 *
 * This reader does not decode any byte into a gameplay meaning.
 */
[[nodiscard]] constexpr bool has_contribution(std::byte cell) noexcept
{
    return cell != detail::k_space && cell != detail::k_newline;
}


template<storage::StorageBackend Backend>
[[nodiscard]] std::expected<LayerSourceLayout, LayerSourceError>
open_layer_source(const Backend& storage, storage::SourceId source)
{
    storage::Size total_size = 0;
    const auto size_result = storage.size(source, total_size);
    if (!size_result)
    {
        return std::unexpected(LayerSourceError::StorageFailed);
    }

    std::array<std::byte, layer_source_metadata_read_size> read_buffer {};

    detail::CoreRecords core {};
    bool previous_was_newline = false;

    // Line-level errors are reported only once the metadata block is known
    // to end: without a terminator, MissingTerminator is the more
    // fundamental diagnosis and subsumes them.
    bool has_line_error = false;
    LayerSourceError line_error = LayerSourceError::MalformedMetadata;

    auto record_error = [&has_line_error, &line_error](LayerSourceError error)
    {
        if (!has_line_error)
        {
            has_line_error = true;
            line_error = error;
        }
    };

    // Per-line streaming state.
    detail::LineState line_state = detail::LineState::AtLineStart;
    std::byte key_candidate = std::byte{0};    // valid in FirstKeyCandidate
    bool unknown_colon_seen = false;           // valid in SkippingUnknown
    std::array<std::byte, detail::k_max_known_payload> payload {};
    std::size_t payload_length = 0;            // valid in the Parsing* states

    // Finalize the line just ended by a non-terminating LF.
    auto finish_line = [&]()
    {
        switch (line_state)
        {
            case detail::LineState::AtLineStart:
            case detail::LineState::FirstKeyCandidate:
                // Empty line, or a bare 'V'/'D'/'P' with no colon.
                record_error(LayerSourceError::MalformedMetadata);
                break;

            case detail::LineState::SkippingUnknown:
                if (!unknown_colon_seen)
                {
                    record_error(LayerSourceError::MalformedMetadata);
                }
                break;

            case detail::LineState::ParsingVersion:
            {
                const auto view =
                    std::span<const std::byte>(payload.data(), payload_length);
                std::uint64_t major = 0;
                std::uint64_t minor = 0;
                if (!detail::parse_version(view, major, minor))
                {
                    record_error(LayerSourceError::MalformedMetadata);
                }
                else if (major != 1 || minor != 0)
                {
                    record_error(LayerSourceError::UnsupportedVersion);
                }
                else
                {
                    core.version_major = static_cast<std::uint8_t>(major);
                    core.version_minor = static_cast<std::uint8_t>(minor);
                    core.version_seen = true;
                }
                break;
            }

            case detail::LineState::ParsingDims:
            {
                const auto view =
                    std::span<const std::byte>(payload.data(), payload_length);
                std::int64_t width = 0;
                std::int64_t height = 0;
                if (!detail::parse_two_ints(view, false, width, height))
                {
                    record_error(LayerSourceError::MalformedMetadata);
                }
                else if (width == 0 || height == 0)
                {
                    record_error(LayerSourceError::ZeroDimension);
                }
                else
                {
                    core.width = static_cast<std::uint64_t>(width);
                    core.height = static_cast<std::uint64_t>(height);
                    core.dimensions_seen = true;
                }
                break;
            }

            case detail::LineState::ParsingPos:
            {
                const auto view =
                    std::span<const std::byte>(payload.data(), payload_length);
                std::int64_t x = 0;
                std::int64_t y = 0;
                if (!detail::parse_two_ints(view, true, x, y))
                {
                    record_error(LayerSourceError::MalformedMetadata);
                }
                else if (x < detail::k_int32_min || x > detail::k_int32_max
                    || y < detail::k_int32_min || y > detail::k_int32_max)
                {
                    record_error(LayerSourceError::Overflow);
                }
                else
                {
                    core.position_x = x;
                    core.position_y = y;
                    core.position_seen = true;
                }
                break;
            }
        }

        line_state = detail::LineState::AtLineStart;
        key_candidate = std::byte{0};
        unknown_colon_seen = false;
        payload_length = 0;
    };

    const std::uint64_t end = static_cast<std::uint64_t>(total_size);

    for (std::uint64_t offset = 0; offset < end;)
    {
        const std::uint64_t remaining = end - offset;
        const std::size_t wanted = static_cast<std::size_t>(
            std::min<std::uint64_t>(layer_source_metadata_read_size, remaining));

        std::span<std::byte> chunk(read_buffer.data(), wanted);
        const auto read_result =
            storage.read(source, static_cast<storage::Offset>(offset), chunk);
        if (!read_result)
        {
            return std::unexpected(LayerSourceError::StorageFailed);
        }

        for (std::size_t i = 0; i < wanted; ++i)
        {
            const std::byte cell = read_buffer[i];

            if (cell == detail::k_newline)
            {
                if (previous_was_newline)
                {
                    // First blank line: metadata ends here.
                    const std::uint64_t data_offset = offset + i + 1;
                    if (has_line_error)
                    {
                        return std::unexpected(line_error);
                    }
                    return detail::finalize(core, data_offset, end);
                }

                previous_was_newline = true;
                finish_line();
            }
            else
            {
                previous_was_newline = false;

                switch (line_state)
                {
                    case detail::LineState::AtLineStart:
                        if (cell == std::byte{'V'} || cell == std::byte{'D'}
                            || cell == std::byte{'P'})
                        {
                            key_candidate = cell;
                            line_state = detail::LineState::FirstKeyCandidate;
                        }
                        else
                        {
                            unknown_colon_seen = (cell == detail::k_colon);
                            line_state = detail::LineState::SkippingUnknown;
                        }
                        break;

                    case detail::LineState::FirstKeyCandidate:
                        if (cell != detail::k_colon)
                        {
                            // Multi-character key: unknown record.
                            unknown_colon_seen = false;
                            line_state = detail::LineState::SkippingUnknown;
                        }
                        else if (key_candidate == std::byte{'V'}
                            && core.version_seen)
                        {
                            record_error(LayerSourceError::DuplicateRecord);
                            unknown_colon_seen = true;
                            line_state = detail::LineState::SkippingUnknown;
                        }
                        else if (key_candidate == std::byte{'D'}
                            && core.dimensions_seen)
                        {
                            record_error(LayerSourceError::DuplicateRecord);
                            unknown_colon_seen = true;
                            line_state = detail::LineState::SkippingUnknown;
                        }
                        else if (key_candidate == std::byte{'P'}
                            && core.position_seen)
                        {
                            record_error(LayerSourceError::DuplicateRecord);
                            unknown_colon_seen = true;
                            line_state = detail::LineState::SkippingUnknown;
                        }
                        else
                        {
                            payload_length = 0;
                            if (key_candidate == std::byte{'V'})
                            {
                                line_state = detail::LineState::ParsingVersion;
                            }
                            else if (key_candidate == std::byte{'D'})
                            {
                                line_state = detail::LineState::ParsingDims;
                            }
                            else
                            {
                                line_state = detail::LineState::ParsingPos;
                            }
                        }
                        break;

                    case detail::LineState::SkippingUnknown:
                        unknown_colon_seen =
                            unknown_colon_seen || (cell == detail::k_colon);
                        break;

                    case detail::LineState::ParsingVersion:
                    case detail::LineState::ParsingDims:
                    case detail::LineState::ParsingPos:
                        if (payload_length == payload.size())
                        {
                            // Longer than any well-formed known payload:
                            // necessarily malformed. Stream the rest away.
                            record_error(LayerSourceError::MalformedMetadata);
                            unknown_colon_seen = true;
                            line_state = detail::LineState::SkippingUnknown;
                        }
                        else
                        {
                            payload[payload_length++] = cell;
                        }
                        break;
                }
            }
        }

        offset += wanted;
    }

    return std::unexpected(LayerSourceError::MissingTerminator);
}


template<storage::StorageBackend Backend>
[[nodiscard]] std::expected<void, LayerSourceError>
read_cells(
    const LayerSourceLayout& layout,
    const Backend& storage,
    storage::SourceId source,
    std::uint32_t row,
    std::uint32_t x,
    std::span<std::byte> destination)
{
    // Subtraction-style bounds: x + destination.size() would be computed in
    // size_t, which wraps on 32-bit targets. width - x cannot underflow
    // because x <= width is checked first, and the comparison widens to
    // size_t without truncation.
    if (row >= layout.height || x > layout.width)
    {
        return std::unexpected(LayerSourceError::OutOfRange);
    }

    if (destination.size() > static_cast<std::size_t>(layout.width - x))
    {
        return std::unexpected(LayerSourceError::OutOfRange);
    }

    if (destination.empty())
    {
        return {};
    }

    const auto cell = layout.cell_offset(x, row);
    if (!cell)
    {
        return std::unexpected(LayerSourceError::OutOfRange);
    }

    const auto result = storage.read(source, *cell, destination);
    if (!result)
    {
        return std::unexpected(LayerSourceError::StorageFailed);
    }

    // LF is structural in v1 and can never be a cell value. Only inspect
    // the requested fragment: normal gameplay does not scan untouched data.
    for (const std::byte value : destination)
    {
        if (value == detail::k_newline)
        {
            return std::unexpected(LayerSourceError::MalformedData);
        }
    }

    // Validate the structural byte for every row we actually touch without
    // reading the rest of that row. The parsed layout normally guarantees
    // this offset fits storage::Offset; keep the arithmetic defensive for
    // callers that construct a LayerSourceLayout directly.
    const std::uint64_t row_offset =
        static_cast<std::uint64_t>(row)
        * static_cast<std::uint64_t>(layout.row_stride);
    const std::uint64_t max_offset =
        static_cast<std::uint64_t>(std::numeric_limits<storage::Offset>::max());

    if (row_offset > max_offset
        || static_cast<std::uint64_t>(layout.data_offset) > max_offset - row_offset)
    {
        return std::unexpected(LayerSourceError::Overflow);
    }

    const std::uint64_t row_start =
        static_cast<std::uint64_t>(layout.data_offset) + row_offset;

    if (static_cast<std::uint64_t>(layout.width) > max_offset - row_start)
    {
        return std::unexpected(LayerSourceError::Overflow);
    }

    const auto terminator_offset = static_cast<storage::Offset>(
        row_start + static_cast<std::uint64_t>(layout.width));

    std::byte terminator {};
    const auto terminator_result = storage.read(
        source,
        terminator_offset,
        std::span<std::byte>(&terminator, 1));

    if (!terminator_result)
    {
        return std::unexpected(LayerSourceError::StorageFailed);
    }

    if (terminator != detail::k_newline)
    {
        return std::unexpected(LayerSourceError::MalformedData);
    }

    return {};
}


} // namespace landor::geo

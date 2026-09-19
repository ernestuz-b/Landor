#pragma once

#include <cstddef>
#include <cstdint>


namespace landor::storage
{

/**
 * Identity of one independently stored source of spatial data.
 *
 * A SourceId says which stored object is being addressed; it says nothing
 * about how that object is represented physically.
 *
 * Depending on the selected platform Storage implementation, a source may be
 * backed by a host file, an SD-card file, ROM data, flash or something else.
 *
 * Geo code uses SourceId as an opaque identity. It must never derive paths,
 * filenames, sector numbers or other physical storage information from it.
 */
using SourceId = std::uint16_t;


/**
 * Byte offset inside one storage source.
 *
 * The runtime map representation is layer-oriented, so a Patch may bind its
 * Terrain layer and Elevation layer to different SourceIds. Map/cache code
 * then addresses portions of those sources using Offset.
 */
using Offset = std::uint32_t;


/**
 * Size in bytes.
 *
 * Kept explicit rather than using the platform's native file-size type so the
 * stored-data contract has identical width on every target.
 */
using Size = std::uint32_t;


/**
 * Result of a low-level storage operation.
 *
 * Storage errors are part of the platform boundary. Higher layers decide what
 * an error means to the game; the platform Storage implementation only reports
 * what happened.
 */
enum class Error : std::uint8_t
{
    none,

    invalid_source,
    out_of_range,
    read_only,
    no_space,

    /// create() found that a source already exists at its configured path;
    /// the existing source was left untouched.
    already_exists,

    read_failed,
    write_failed
};


/**
 * Result returned by Storage operations.
 *
 * Storage reads and writes are expected to transfer the complete requested
 * range or fail. Partial success is deliberately not exposed to Map code:
 * platform implementations must either complete the operation or report an
 * error.
 */
struct Result
{
    Error error = Error::none;

    [[nodiscard]] constexpr explicit operator bool() const noexcept
    {
        return error == Error::none;
    }
};


} // namespace landor::storage
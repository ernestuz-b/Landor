#include "platform/storage/storage_filesystem.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <limits>


namespace landor::storage
{

namespace
{

/**
 * Resolve one configured source file to its physical path and logical size.
 *
 * On success, fills physical_path and logical_size.
 *
 * Fails with Error::read_failed when the backing file is missing or cannot
 * be described, or Error::out_of_range when the file does not fit in the
 * logical Size type.
 */
[[nodiscard]] Result source_file_size(
    std::string_view root,
    std::string_view relative,
    std::filesystem::path& physical_path,
    Size& logical_size) noexcept
{
    const auto physical = std::filesystem::path(root) / relative;

    std::error_code ec;
    const auto file_size = std::filesystem::file_size(physical, ec);
    if (ec)
    {
        return Result{Error::read_failed};
    }

    if (file_size > std::numeric_limits<Size>::max())
    {
        return Result{Error::out_of_range};
    }

    physical_path = physical;
    logical_size = static_cast<Size>(file_size);
    return Result{Error::none};
}

} // namespace


Result StorageFilesystem::write(SourceId source, Offset offset,
                                std::span<const std::byte> data) noexcept
{
    const auto* relative = relative_path(source);
    if (relative == nullptr)
    {
        return Result{Error::invalid_source};
    }

    std::filesystem::path physical_path;
    Size logical_size{};

    const Result resolved = source_file_size(m_root, *relative, physical_path, logical_size);
    if (!resolved)
    {
        return resolved;
    }

    // The same subtraction-style checking as read(): the whole range must
    // fit inside the existing logical source. write() replaces bytes; it
    // never grows, truncates or creates a source.
    if (offset > logical_size || data.size() > static_cast<std::size_t>(logical_size - offset))
    {
        return Result{Error::out_of_range};
    }

    // An empty write is a successful no-op once the range has been accepted.
    if (data.empty())
    {
        return Result{Error::none};
    }

    // Opened without truncation so an existing source keeps its exact size,
    // and with in|out so a file that is missing at this point is reported
    // instead of created.
    //
    // Standard std::fstream does not provide reliable access to the
    // underlying failure reason. Every failure of open, seek, write, flush
    // or close below is therefore reported as write_failed, and this backend
    // cannot currently report read_only or no_space. A more specific error
    // requires direct, reliable evidence of the cause; it must never be
    // inferred from other observations.
    std::fstream stream(physical_path, std::ios::in | std::ios::out | std::ios::binary);
    if (!stream)
    {
        return Result{Error::write_failed};
    }

    stream.seekp(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!stream)
    {
        return Result{Error::write_failed};
    }

    stream.write(reinterpret_cast<const char*>(data.data()),
                 static_cast<std::streamsize>(data.size()));

    // close() flushes the stream, which is what makes the bytes visible to a
    // subsequent read() through a fresh stream. A failure during flush means
    // the write did not complete.
    stream.close();
    if (stream.fail())
    {
        return Result{Error::write_failed};
    }

    return Result{Error::none};
}

Result StorageFilesystem::exists(
    SourceId source,
    bool& result) const noexcept
{
    const auto* relative = relative_path(source);
    if (relative == nullptr)
    {
        return Result{Error::invalid_source};
    }

    const auto physical = std::filesystem::path(m_root) / *relative;

    std::error_code ec;
    const auto present = std::filesystem::exists(physical, ec);
    if (ec)
    {
        // The filesystem state could not be determined reliably; do not
        // guess absence.
        return Result{Error::read_failed};
    }

    if (!present)
    {
        // Absence is a success outcome, not an error.
        result = false;
        return Result{Error::none};
    }

    // A path that exists but is not a regular file (for example a
    // directory) cannot represent a normal source; fail rather than
    // masquerade as absence.
    const auto regular = std::filesystem::is_regular_file(physical, ec);
    if (ec || !regular)
    {
        return Result{Error::read_failed};
    }

    result = true;
    return Result{Error::none};
}


Result StorageFilesystem::create(
    SourceId source,
    Size size,
    std::byte initial_value) noexcept
{
    const auto* relative = relative_path(source);
    if (relative == nullptr)
    {
        return Result{Error::invalid_source};
    }

    const auto physical = std::filesystem::path(m_root) / *relative;

    // Never replace something already at the configured path. The existence
    // check is the guard against overwriting an existing source; it is
    // followed immediately by the open below.
    std::error_code ec;
    const auto present = std::filesystem::exists(physical, ec);
    if (ec)
    {
        return Result{Error::read_failed};
    }

    if (present)
    {
        const auto regular = std::filesystem::is_regular_file(physical, ec);
        if (ec)
        {
            return Result{Error::read_failed};
        }

        if (regular)
        {
            return Result{Error::already_exists};
        }

        // A directory or other non-file entry: creation cannot complete.
        return Result{Error::write_failed};
    }

    // Parent directories of a nested relative path are created when they do
    // not exist yet; create_directories succeeds when they already exist.
    if (physical.has_parent_path())
    {
        std::error_code parent_ec;
        std::filesystem::create_directories(physical.parent_path(), parent_ec);
        if (parent_ec)
        {
            return Result{Error::write_failed};
        }
    }

    // If creation fails after the file has been started, remove the partial
    // file as a best effort so a failed create() does not leave a
    // half-formed source that a later exists() would report.
    const auto cleanup_partial = [&physical]() noexcept
    {
        std::error_code remove_ec;
        std::filesystem::remove(physical, remove_ec);
    };

    // Truncation is safe here because existence was checked just above: it
    // guarantees the stream starts from an empty file, so a failed fill can
    // never mix stale bytes with the newly created source. Standard
    // std::fstream does not expose the underlying failure reason, so every
    // open or write failure is reported as write_failed, the same way
    // write() does.
    std::ofstream stream(
        physical,
        std::ios::out | std::ios::binary | std::ios::trunc);
    if (!stream)
    {
        cleanup_partial();
        return Result{Error::write_failed};
    }

    // Fill the source with the requested initial byte in fixed-size blocks
    // without a whole-file image in RAM.
    constexpr std::size_t k_fill_block_bytes = 4096;
    std::array<std::byte, k_fill_block_bytes> block{};
    std::fill(block.begin(), block.end(), initial_value);

    for (Size remaining = size; remaining != 0;)
    {
        const auto chunk =
            remaining < k_fill_block_bytes ? remaining : k_fill_block_bytes;
        stream.write(
            reinterpret_cast<const char*>(block.data()),
            static_cast<std::streamsize>(chunk));
        if (stream.fail())
        {
            cleanup_partial();
            return Result{Error::write_failed};
        }

        remaining -= chunk;
    }

    // close() flushes the stream; a failure there means the fill did not
    // complete.
    stream.close();
    if (stream.fail())
    {
        cleanup_partial();
        return Result{Error::write_failed};
    }

    // Success must mean subsequent size()/read()/write() can address the
    // source normally: verify the promised size exactly.
    std::error_code verify_ec;
    const auto final_size = std::filesystem::file_size(physical, verify_ec);
    if (verify_ec || final_size != static_cast<std::uintmax_t>(size))
    {
        cleanup_partial();
        return Result{Error::write_failed};
    }

    return Result{Error::none};
}


Result StorageFilesystem::size(
    SourceId source,
    Size& result) const noexcept
{
    const auto* relative = relative_path(source);
    if (relative == nullptr)
    {
        return Result{Error::invalid_source};
    }

    std::filesystem::path physical_path;
    Size logical_size{};

    const Result resolved = source_file_size(m_root, *relative, physical_path, logical_size);
    if (!resolved)
    {
        return resolved;
    }

    result = logical_size;
    return Result{Error::none};
}


Result StorageFilesystem::read(
    SourceId source,
    Offset offset,
    std::span<std::byte> destination) const noexcept
{
    const auto* relative = relative_path(source);
    if (relative == nullptr)
    {
        return Result{Error::invalid_source};
    }

    std::filesystem::path physical_path;
    Size logical_size{};

    const Result resolved = source_file_size(m_root, *relative, physical_path, logical_size);
    if (!resolved)
    {
        return resolved;
    }

    // Subtraction-style checking so that an offset near the maximum Offset
    // cannot overflow and present a range beyond the source as valid.
    // An empty destination is valid up to and including the end of the source.
    if (offset > logical_size ||
        destination.size() > static_cast<std::size_t>(logical_size - offset))
    {
        return Result{Error::out_of_range};
    }

    std::ifstream stream(physical_path, std::ios::in | std::ios::binary);
    if (!stream)
    {
        return Result{Error::read_failed};
    }

    stream.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!stream)
    {
        return Result{Error::read_failed};
    }

    stream.read(
        reinterpret_cast<char*>(destination.data()),
        static_cast<std::streamsize>(destination.size()));

    // A short read is a failed read; partial success is never exposed.
    if (static_cast<std::size_t>(stream.gcount()) != destination.size())
    {
        return Result{Error::read_failed};
    }

    return Result{Error::none};
}

} // namespace landor::storage

#include "platform/storage/storage_filesystem.hpp"

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

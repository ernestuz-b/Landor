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

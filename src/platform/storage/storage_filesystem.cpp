#include "platform/storage/storage_filesystem.hpp"

#include <filesystem>
#include <limits>


namespace landor::storage
{

Result StorageFilesystem::size(
    SourceId source,
    Size& result) const noexcept
{
    const auto* relative = relative_path(source);
    if (relative == nullptr)
        return Result{Error::invalid_source};

    std::error_code ec;
    const std::filesystem::path physical_path = std::filesystem::path(m_root) / *relative;

    const auto file_size = std::filesystem::file_size(physical_path, ec);
    if (ec)
        return Result{Error::read_failed};

    if (file_size > std::numeric_limits<Size>::max())
        return Result{Error::out_of_range};

    result = static_cast<Size>(file_size);
    return Result{Error::none};
}

} // namespace landor::storage

#include <gtest/gtest.h>

#include "platform/storage/storage_filesystem.hpp"

#include <array>
#include <cstdint>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <unistd.h>


namespace
{

using landor::storage::Error;
using landor::storage::Result;
using landor::storage::Size;
using landor::storage::SourceId;
using landor::storage::Storage;


class StorageFilesystemTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        std::error_code ec;
        m_root_path = std::filesystem::temp_directory_path(ec) / "landor_storage_filesystem_test";
        ASSERT_FALSE(ec) << ec.message();

        const bool created = std::filesystem::create_directories(m_root_path, ec);
        ASSERT_TRUE(created || !ec) << ec.message();

        // The storage object stores the root non-owning, so a persistent
        // spelling of the path must outlive every Storage instance.
        m_root = m_root_path.native();
    }


    void TearDown() override
    {
        std::error_code ec;
        std::filesystem::remove_all(m_root_path, ec);
        ASSERT_FALSE(ec) << ec.message();
    }


    Storage make_storage() const
    {
        // The Storage object also stores the source table non-owning, so the
        // table itself must outlive every Storage instance.
        return Storage {m_root, m_sources};
    }


    void write_file(const std::string_view name, const std::size_t byte_count)
    {
        const std::string data(byte_count, 'a');

        std::ofstream stream(m_root_path / name, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(static_cast<bool>(stream));
        stream.write(data.data(), static_cast<std::streamsize>(data.size()));
        ASSERT_TRUE(static_cast<bool>(stream));
    }


    std::filesystem::path m_root_path;
    std::string           m_root;
    // SourceId 0: existing file, SourceId 1: configured but missing file,
    // SourceId 2: existing empty file, SourceId 3: oversized file.
    std::array<std::string_view, 4>
        m_sources {"present.layer", "missing.layer", "empty.layer", "huge.layer"};
};


TEST_F(StorageFilesystemTest, ReportsTheExactSizeOfExistingFiles)
{
    write_file("present.layer", 777);
    write_file("empty.layer", 0);

    const Storage storage = make_storage();

    Size present_size{};
    const Result present_result = storage.size(SourceId {0}, present_size);
    EXPECT_TRUE(present_result);
    EXPECT_EQ(present_result.error, Error::none);
    EXPECT_EQ(present_size, Size {777});

    Size empty_size{};
    const Result empty_result = storage.size(SourceId {2}, empty_size);
    EXPECT_TRUE(empty_result);
    EXPECT_EQ(empty_result.error, Error::none);
    EXPECT_EQ(empty_size, Size {0});
}


TEST_F(StorageFilesystemTest, RejectsASourceIdOutsideTheSourceTable)
{
    const Storage storage = make_storage();

    for (const SourceId source : {SourceId {4}, SourceId {0xFFFF}})
    {
        Size out{};
        const Result result = storage.size(source, out);
        EXPECT_FALSE(result);
        EXPECT_EQ(result.error, Error::invalid_source);
    }
}


TEST_F(StorageFilesystemTest, ReportsReadFailedWhenTheConfiguredFileIsMissing)
{
    const Storage storage = make_storage();

    Size out{};
    const Result result = storage.size(SourceId {1}, out);

    EXPECT_FALSE(result);
    EXPECT_EQ(result.error, Error::read_failed);
}


TEST_F(StorageFilesystemTest, ReportsOutOfRangeForAFileLargerThanStorageSize)
{
    const int fd = ::open((m_root_path / "huge.layer").c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    ASSERT_GE(fd, 0);

    // Sparse file: apparent size 2^32 bytes, no actual blocks allocated.
    const std::int64_t byte_count = static_cast<std::int64_t>(std::numeric_limits<Size>::max()) + 1;
    ASSERT_EQ(0, ::ftruncate(fd, byte_count));
    ASSERT_EQ(0, ::close(fd));

    const Storage storage = make_storage();

    Size out{};
    const Result result = storage.size(SourceId {3}, out);

    EXPECT_FALSE(result);
    EXPECT_EQ(result.error, Error::out_of_range);
}

} // namespace

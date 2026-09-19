#include <gtest/gtest.h>

#include "platform/storage/storage_filesystem.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <limits>
#include <span>
#include <string>
#include <unistd.h>
#include <vector>


namespace
{

using landor::storage::Error;
using landor::storage::Offset;
using landor::storage::Result;
using landor::storage::Size;
using landor::storage::SourceId;
using landor::storage::Storage;


// Distinct deterministic byte pattern; 37 is coprime with 251, so the first
// 251 bytes are all different and a one-byte shift is always visible.
std::vector<std::byte> make_pattern(const std::size_t count)
{
    std::vector<std::byte> pattern(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        pattern[i] = static_cast<std::byte>((i * 37 + 11) % 251);
    }

    return pattern;
}


// A copy of one contiguous range of a pattern.
std::vector<std::byte> sub_range(
    const std::vector<std::byte>& pattern, const std::size_t offset, const std::size_t count)
{
    const auto first = pattern.begin() + static_cast<std::ptrdiff_t>(offset);
    const auto last = first + static_cast<std::ptrdiff_t>(count);
    return {first, last};
}


// A second deterministic byte pattern with a different phase from
// make_pattern() (138 != 11 modulo 251), so replacement data always differs
// from the contents it replaces at every offset.
std::vector<std::byte> make_replacement(const std::size_t count)
{
    std::vector<std::byte> replacement(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        replacement[i] = static_cast<std::byte>((i * 37 + 138) % 251);
    }

    return replacement;
}

// The full file contents after replacing [offset, offset + data.size()).
std::vector<std::byte> file_after_replacement(const std::vector<std::byte>& contents,
                                              const std::size_t offset,
                                              const std::span<const std::byte>& data)
{
    std::vector<std::byte> result = contents;
    for (std::size_t i = 0; i < data.size(); ++i)
    {
        result[offset + i] = data[i];
    }

    return result;
}

// Reads one complete source back through the storage interface.
std::vector<std::byte> read_full_source(const Storage& storage, const SourceId source)
{
    Size size{};
    const Result size_result = storage.size(source, size);
    if (!size_result)
    {
        return {};
    }

    std::vector<std::byte> contents(size);
    const Result read_result = storage.read(source, Offset{0}, std::span<std::byte>{contents});
    if (!read_result)
    {
        return {};
    }

    return contents;
}


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


    void write_bytes(const std::string_view name, const std::span<const std::byte> bytes)
    {
        std::ofstream stream(m_root_path / name, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(static_cast<bool>(stream));
        stream.write(
            reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
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


TEST_F(StorageFilesystemTest, ReadsTheCompleteFile)
{
    const std::vector<std::byte> pattern = make_pattern(64);
    write_bytes("present.layer", std::span<const std::byte> {pattern});

    const Storage storage = make_storage();

    std::vector<std::byte> destination(pattern.size());
    const Result result =
        storage.read(SourceId {0}, Offset {0}, std::span<std::byte> {destination});

    EXPECT_TRUE(result);
    EXPECT_EQ(result.error, Error::none);
    EXPECT_EQ(destination, pattern);
}


TEST_F(StorageFilesystemTest, ReadsAByteRangeFromTheMiddleOfTheFile)
{
    const std::vector<std::byte> pattern = make_pattern(128);
    write_bytes("present.layer", std::span<const std::byte> {pattern});

    const Storage storage = make_storage();

    constexpr std::size_t offset = 40;
    constexpr std::size_t count = 33;

    std::vector<std::byte> destination(count);
    const Result result =
        storage.read(SourceId {0}, static_cast<Offset>(offset), std::span<std::byte> {destination});

    EXPECT_TRUE(result);
    EXPECT_EQ(result.error, Error::none);
    EXPECT_EQ(destination, sub_range(pattern, offset, count));
}


TEST_F(StorageFilesystemTest, ReadsTheFinalByteRangeEndingExactlyAtEOF)
{
    const std::vector<std::byte> pattern = make_pattern(100);
    write_bytes("present.layer", std::span<const std::byte> {pattern});

    const Storage storage = make_storage();

    // Final byte only, ending exactly at EOF.
    {
        std::vector<std::byte> destination(1);
        const Result result =
            storage.read(SourceId {0}, Offset {99}, std::span<std::byte> {destination});

        EXPECT_TRUE(result);
        EXPECT_EQ(result.error, Error::none);
        EXPECT_EQ(destination, sub_range(pattern, 99, 1));
    }

    // Final four bytes, ending exactly at EOF.
    {
        std::vector<std::byte> destination(4);
        const Result result =
            storage.read(SourceId {0}, Offset {96}, std::span<std::byte> {destination});

        EXPECT_TRUE(result);
        EXPECT_EQ(result.error, Error::none);
        EXPECT_EQ(destination, sub_range(pattern, 96, 4));
    }
}


TEST_F(StorageFilesystemTest, RejectsARangeExtendingOneByteBeyondEOF)
{
    write_file("present.layer", 64);

    const Storage storage = make_storage();

    // Offset 63 plus two bytes ends one byte past the end of the source.
    std::vector<std::byte> destination(2, std::byte {0xEE});
    const Result result =
        storage.read(SourceId {0}, Offset {63}, std::span<std::byte> {destination});

    EXPECT_FALSE(result);
    EXPECT_EQ(result.error, Error::out_of_range);

    // No partial success is exposed: the destination is untouched.
    for (const auto byte : destination)
    {
        EXPECT_EQ(byte, std::byte {0xEE});
    }
}


TEST_F(StorageFilesystemTest, RejectsAnOffsetBeyondEOF)
{
    write_file("present.layer", 64);

    const Storage storage = make_storage();

    std::vector<std::byte> destination(8);
    const Result result =
        storage.read(SourceId {0}, Offset {65}, std::span<std::byte> {destination});

    EXPECT_FALSE(result);
    EXPECT_EQ(result.error, Error::out_of_range);
}


TEST_F(StorageFilesystemTest, AcceptsAnEmptyDestinationAtOrBeforeEOF)
{
    write_file("present.layer", 64);
    write_file("empty.layer", 0);

    const Storage storage = make_storage();

    const std::span<std::byte> empty {};

    // Exactly at EOF.
    const Result at_eof = storage.read(SourceId {0}, Offset {64}, empty);
    EXPECT_TRUE(at_eof);
    EXPECT_EQ(at_eof.error, Error::none);

    // Before EOF.
    const Result before_eof = storage.read(SourceId {0}, Offset {10}, empty);
    EXPECT_TRUE(before_eof);
    EXPECT_EQ(before_eof.error, Error::none);

    // Empty source, offset at its EOF.
    const Result empty_source = storage.read(SourceId {2}, Offset {0}, empty);
    EXPECT_TRUE(empty_source);
    EXPECT_EQ(empty_source.error, Error::none);
}


TEST_F(StorageFilesystemTest, RejectsAnEmptyDestinationBeyondEOF)
{
    write_file("present.layer", 64);

    const Storage storage = make_storage();

    const std::span<std::byte> empty {};
    const Result result = storage.read(SourceId {0}, Offset {65}, empty);

    EXPECT_FALSE(result);
    EXPECT_EQ(result.error, Error::out_of_range);
}


TEST_F(StorageFilesystemTest, RejectsAReadForASourceIdOutsideTheSourceTable)
{
    const Storage storage = make_storage();

    std::vector<std::byte> destination(8);
    for (const SourceId source : {SourceId {4}, SourceId {0xFFFF}})
    {
        const Result result =
            storage.read(source, Offset {0}, std::span<std::byte> {destination});
        EXPECT_FALSE(result);
        EXPECT_EQ(result.error, Error::invalid_source);
    }
}


TEST_F(StorageFilesystemTest, ReportsReadFailedOnReadWhenTheConfiguredFileIsMissing)
{
    const Storage storage = make_storage();

    std::vector<std::byte> destination(8);
    const Result result =
        storage.read(SourceId {1}, Offset {0}, std::span<std::byte> {destination});

    EXPECT_FALSE(result);
    EXPECT_EQ(result.error, Error::read_failed);
}


TEST_F(StorageFilesystemTest, ReportsOutOfRangeOnReadForAFileLargerThanStorageSize)
{
    const int fd = ::open((m_root_path / "huge.layer").c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    ASSERT_GE(fd, 0);

    // Sparse file: apparent size 2^32 bytes, no actual blocks allocated.
    const std::int64_t byte_count = static_cast<std::int64_t>(std::numeric_limits<Size>::max()) + 1;
    ASSERT_EQ(0, ::ftruncate(fd, byte_count));
    ASSERT_EQ(0, ::close(fd));

    const Storage storage = make_storage();

    std::vector<std::byte> destination(16);
    const Result result =
        storage.read(SourceId {3}, Offset {0}, std::span<std::byte> {destination});

    EXPECT_FALSE(result);
    EXPECT_EQ(result.error, Error::out_of_range);
}


TEST_F(StorageFilesystemTest, ReplacesTheCompleteContentsOfAFile)
{
    const std::vector<std::byte> pattern = make_pattern(16);
    write_bytes("present.layer", std::span<const std::byte>{pattern});

    const std::vector<std::byte> replacement = make_replacement(16);
    Storage storage = make_storage();

    const Result result =
        storage.write(SourceId{0}, Offset{0}, std::span<const std::byte>{replacement});

    EXPECT_TRUE(result);
    EXPECT_EQ(result.error, Error::none);

    // The replacement is observable through StorageFilesystem::read().
    std::vector<std::byte> destination(replacement.size());
    const Result read_result =
        storage.read(SourceId{0}, Offset{0}, std::span<std::byte>{destination});
    EXPECT_TRUE(read_result);
    EXPECT_EQ(read_result.error, Error::none);
    EXPECT_EQ(destination, replacement);

    // The source retains its previous size.
    Size size{};
    const Result size_result = storage.size(SourceId{0}, size);
    EXPECT_TRUE(size_result);
    EXPECT_EQ(size, Size{16});
}

TEST_F(StorageFilesystemTest, ReplacesBytesInTheMiddleLeavingTheSurroundingBytesUnchanged)
{
    const std::vector<std::byte> pattern = make_pattern(64);
    write_bytes("present.layer", std::span<const std::byte>{pattern});

    constexpr std::size_t offset = 20;
    constexpr std::size_t count = 12;

    const std::vector<std::byte> replacement = make_replacement(count);
    Storage storage = make_storage();

    const Result result = storage.write(SourceId{0}, static_cast<Offset>(offset),
                                        std::span<const std::byte>{replacement});

    EXPECT_TRUE(result);
    EXPECT_EQ(result.error, Error::none);

    // A read of the whole file sees the replacement exactly in the written
    // range and the original bytes everywhere else.
    EXPECT_EQ(read_full_source(storage, SourceId{0}),
              file_after_replacement(pattern, offset, replacement));

    // The source retains its previous size.
    Size size{};
    const Result size_result = storage.size(SourceId{0}, size);
    EXPECT_TRUE(size_result);
    EXPECT_EQ(size, Size{64});
}

TEST_F(StorageFilesystemTest, ReplacesTheFirstBytesOfAFile)
{
    const std::vector<std::byte> pattern = make_pattern(32);
    write_bytes("present.layer", std::span<const std::byte>{pattern});

    const std::vector<std::byte> replacement = make_replacement(5);
    Storage storage = make_storage();

    const Result result =
        storage.write(SourceId{0}, Offset{0}, std::span<const std::byte>{replacement});

    EXPECT_TRUE(result);
    EXPECT_EQ(result.error, Error::none);

    EXPECT_EQ(read_full_source(storage, SourceId{0}),
              file_after_replacement(pattern, 0, replacement));

    Size size{};
    const Result size_result = storage.size(SourceId{0}, size);
    EXPECT_TRUE(size_result);
    EXPECT_EQ(size, Size{32});
}

TEST_F(StorageFilesystemTest, ReplacesTheFinalBytesEndingExactlyAtEof)
{
    const std::vector<std::byte> pattern = make_pattern(64);
    write_bytes("present.layer", std::span<const std::byte>{pattern});

    const std::vector<std::byte> replacement = make_replacement(4);
    Storage storage = make_storage();

    // Final four bytes only, ending exactly at EOF.
    const Result result =
        storage.write(SourceId{0}, Offset{60}, std::span<const std::byte>{replacement});

    EXPECT_TRUE(result);
    EXPECT_EQ(result.error, Error::none);

    EXPECT_EQ(read_full_source(storage, SourceId{0}),
              file_after_replacement(pattern, 60, replacement));

    Size size{};
    const Result size_result = storage.size(SourceId{0}, size);
    EXPECT_TRUE(size_result);
    EXPECT_EQ(size, Size{64});
}

TEST_F(StorageFilesystemTest, RejectsAWriteExtendingOneByteBeyondEof)
{
    const std::vector<std::byte> pattern = make_pattern(64);
    write_bytes("present.layer", std::span<const std::byte>{pattern});

    Storage storage = make_storage();

    // Offset 63 plus two bytes ends one byte past the end of the source.
    const std::vector<std::byte> data = make_replacement(2);
    const Result result = storage.write(SourceId{0}, Offset{63}, std::span<const std::byte>{data});

    EXPECT_FALSE(result);
    EXPECT_EQ(result.error, Error::out_of_range);

    // The rejected write leaves the source unchanged.
    EXPECT_EQ(read_full_source(storage, SourceId{0}), pattern);
}

TEST_F(StorageFilesystemTest, RejectsAWriteAtAnOffsetBeyondEof)
{
    const std::vector<std::byte> pattern = make_pattern(64);
    write_bytes("present.layer", std::span<const std::byte>{pattern});

    Storage storage = make_storage();

    const std::vector<std::byte> data = make_replacement(8);
    const Result result = storage.write(SourceId{0}, Offset{65}, std::span<const std::byte>{data});

    EXPECT_FALSE(result);
    EXPECT_EQ(result.error, Error::out_of_range);

    EXPECT_EQ(read_full_source(storage, SourceId{0}), pattern);
}

TEST_F(StorageFilesystemTest, RejectsANonEmptyWriteToAnEmptySource)
{
    write_file("empty.layer", 0);

    Storage storage = make_storage();

    const std::vector<std::byte> data = make_replacement(1);
    const Result result = storage.write(SourceId{2}, Offset{0}, std::span<const std::byte>{data});

    EXPECT_FALSE(result);
    EXPECT_EQ(result.error, Error::out_of_range);

    // The empty source stays empty and is not grown by the rejected write.
    Size size{};
    const Result size_result = storage.size(SourceId{2}, size);
    EXPECT_TRUE(size_result);
    EXPECT_EQ(size, Size{0});
}

TEST_F(StorageFilesystemTest, AcceptsAnEmptyWriteAtOrBeforeEof)
{
    write_file("present.layer", 64);
    write_file("empty.layer", 0);

    Storage storage = make_storage();

    const std::span<const std::byte> empty{};

    // Before EOF.
    const Result before_eof = storage.write(SourceId{0}, Offset{10}, empty);
    EXPECT_TRUE(before_eof);
    EXPECT_EQ(before_eof.error, Error::none);

    // Exactly at EOF.
    const Result at_eof = storage.write(SourceId{0}, Offset{64}, empty);
    EXPECT_TRUE(at_eof);
    EXPECT_EQ(at_eof.error, Error::none);

    // Empty source, offset at its EOF.
    const Result empty_source = storage.write(SourceId{2}, Offset{0}, empty);
    EXPECT_TRUE(empty_source);
    EXPECT_EQ(empty_source.error, Error::none);

    // The no-op writes leave the source unchanged.
    const std::vector<std::byte> original(64, std::byte{'a'});
    EXPECT_EQ(read_full_source(storage, SourceId{0}), original);
    Size size{};
    const Result size_result = storage.size(SourceId{0}, size);
    EXPECT_TRUE(size_result);
    EXPECT_EQ(size, Size{64});
}

TEST_F(StorageFilesystemTest, RejectsAnEmptyWriteBeyondEof)
{
    const std::vector<std::byte> pattern = make_pattern(64);
    write_bytes("present.layer", std::span<const std::byte>{pattern});

    Storage storage = make_storage();

    const std::span<const std::byte> empty{};
    const Result result = storage.write(SourceId{0}, Offset{65}, empty);

    EXPECT_FALSE(result);
    EXPECT_EQ(result.error, Error::out_of_range);

    EXPECT_EQ(read_full_source(storage, SourceId{0}), pattern);
}

TEST_F(StorageFilesystemTest, RejectsAWriteForASourceIdOutsideTheSourceTable)
{
    Storage storage = make_storage();

    const std::vector<std::byte> data = make_replacement(8);
    for (const SourceId source : {SourceId{4}, SourceId{0xFFFF}})
    {
        const Result result = storage.write(source, Offset{0}, std::span<const std::byte>{data});
        EXPECT_FALSE(result);
        EXPECT_EQ(result.error, Error::invalid_source);
    }
}

TEST_F(StorageFilesystemTest, DoesNotCreateAMissingConfiguredFileOnWrite)
{
    Storage storage = make_storage();

    const std::vector<std::byte> data = make_replacement(8);
    const Result result = storage.write(SourceId{1}, Offset{0}, std::span<const std::byte>{data});

    // The shared path/size resolution reports a missing file as read_failed;
    // write() keeps that mapping and must not create the file.
    EXPECT_FALSE(result);
    EXPECT_EQ(result.error, Error::read_failed);
    EXPECT_FALSE(std::filesystem::exists(m_root_path / "missing.layer"));
}


TEST_F(StorageFilesystemTest, ExistsReportsPresenceForExistingFiles)
{
    write_file("present.layer", 64);
    write_file("empty.layer", 0);

    const Storage storage = make_storage();

    bool present{};
    const Result present_result = storage.exists(SourceId {0}, present);
    EXPECT_TRUE(present_result);
    EXPECT_EQ(present_result.error, Error::none);
    EXPECT_TRUE(present);

    // An existing empty file is still a present source.
    bool empty{};
    const Result empty_result = storage.exists(SourceId {2}, empty);
    EXPECT_TRUE(empty_result);
    EXPECT_TRUE(empty);
}


TEST_F(StorageFilesystemTest, ExistsReportsAbsenceWithoutFailing)
{
    const Storage storage = make_storage();

    bool absent{};
    const Result result = storage.exists(SourceId {1}, absent);

    // Absence is a success outcome, not an error.
    EXPECT_TRUE(result);
    EXPECT_EQ(result.error, Error::none);
    EXPECT_FALSE(absent);
}


TEST_F(StorageFilesystemTest, ExistsRejectsASourceIdOutsideTheSourceTable)
{
    const Storage storage = make_storage();

    bool out{};
    for (const SourceId source : {SourceId {4}, SourceId {0xFFFF}})
    {
        const Result result = storage.exists(source, out);
        EXPECT_FALSE(result);
        EXPECT_EQ(result.error, Error::invalid_source);
    }
}


TEST_F(StorageFilesystemTest, ExistsFailsWhenTheConfiguredPathIsADirectory)
{
    std::error_code ec;
    ASSERT_TRUE(std::filesystem::create_directories(m_root_path / "missing.layer", ec))
        << ec.message();

    const Storage storage = make_storage();

    // A directory at the configured path cannot represent a normal source:
    // the backend fails instead of masquerading it as absence.
    bool out{};
    const Result result = storage.exists(SourceId {1}, out);

    EXPECT_FALSE(result);
    EXPECT_EQ(result.error, Error::read_failed);
}


TEST_F(StorageFilesystemTest, CreatesAMissingSourceWithTheExactSizeAndFillValue)
{
    Storage storage = make_storage();

    constexpr Size fill_size {1000};
    const Result result =
        storage.create(SourceId {1}, fill_size, std::byte {0x20});

    EXPECT_TRUE(result);
    EXPECT_EQ(result.error, Error::none);

    // The new source is addressable through the existing operations.
    bool present{};
    const Result present_result = storage.exists(SourceId {1}, present);
    EXPECT_TRUE(present_result);
    EXPECT_TRUE(present);

    Size size{};
    const Result size_result = storage.size(SourceId {1}, size);
    EXPECT_TRUE(size_result);
    EXPECT_EQ(size, fill_size);

    std::vector<std::byte> contents(fill_size);
    const Result read_result =
        storage.read(SourceId {1}, Offset {0}, std::span<std::byte> {contents});
    EXPECT_TRUE(read_result);
    for (const auto byte : contents)
    {
        EXPECT_EQ(byte, std::byte {0x20});
    }

    // The new source also accepts writes through the ordinary path.
    const std::vector<std::byte> data = make_replacement(4);
    const Result write_result =
        storage.write(SourceId {1}, Offset {10}, std::span<const std::byte> {data});
    EXPECT_TRUE(write_result);
    EXPECT_EQ(read_full_source(storage, SourceId {1})[10 + 2], data[2]);
}


TEST_F(StorageFilesystemTest, CreateReportsAlreadyExistsAndLeavesTheSourceUntouched)
{
    const std::vector<std::byte> pattern = make_pattern(32);
    write_bytes("present.layer", std::span<const std::byte> {pattern});

    Storage storage = make_storage();

    const Result result =
        storage.create(SourceId {0}, Size {8}, std::byte {0x20});

    EXPECT_FALSE(result);
    EXPECT_EQ(result.error, Error::already_exists);

    // The existing source was not replaced, truncated or rewritten.
    EXPECT_EQ(read_full_source(storage, SourceId {0}), pattern);
}


TEST_F(StorageFilesystemTest, CreateRejectsASourceIdOutsideTheSourceTable)
{
    Storage storage = make_storage();

    for (const SourceId source : {SourceId {4}, SourceId {0xFFFF}})
    {
        const Result result = storage.create(source, Size {8}, std::byte {0x20});
        EXPECT_FALSE(result);
        EXPECT_EQ(result.error, Error::invalid_source);
    }
}


TEST_F(StorageFilesystemTest, CreatesAnEmptySourceForZeroSize)
{
    Storage storage = make_storage();

    const Result result = storage.create(SourceId {1}, Size {0}, std::byte {0x20});

    EXPECT_TRUE(result);
    EXPECT_EQ(result.error, Error::none);

    Size size{};
    const Result size_result = storage.size(SourceId {1}, size);
    EXPECT_TRUE(size_result);
    EXPECT_EQ(size, Size {0});
}


TEST_F(StorageFilesystemTest, CreateCreatesParentDirectoriesForNestedPaths)
{
    // A separate table with one nested relative path: the parent directory
    // does not exist yet and must be created by create().
    constexpr std::string_view nested_sources [] = {"nested/dir/new.layer"};
    Storage storage {m_root, std::span<const std::string_view> {nested_sources}};

    const Result result = storage.create(SourceId {0}, Size {16}, std::byte {0x20});

    EXPECT_TRUE(result);
    EXPECT_EQ(result.error, Error::none);
    EXPECT_EQ(read_full_source(storage, SourceId {0}).size(), std::size_t {16});
}


TEST_F(StorageFilesystemTest, CreateFailsWhenTheParentPathIsAFile)
{
    write_file("present.layer", 64);

    // The nested relative path's parent is an existing regular file, so the
    // parent directory cannot be created. The failure must not create a
    // partial source and must not touch the parent file.
    constexpr std::string_view nested_sources [] = {"present.layer/sub.layer"};
    Storage storage {m_root, std::span<const std::string_view> {nested_sources}};

    const Result result = storage.create(SourceId {0}, Size {16}, std::byte {0x20});

    EXPECT_FALSE(result);
    EXPECT_EQ(result.error, Error::write_failed);
    EXPECT_FALSE(std::filesystem::exists(m_root_path / "present.layer" / "sub.layer"));

    Size size{};
    const Result size_result = storage.size(SourceId {0}, size);
    EXPECT_FALSE(size_result);

    // The parent file is untouched.
    EXPECT_EQ(read_full_source(make_storage(), SourceId {0}).size(), std::size_t {64});
}


TEST_F(StorageFilesystemTest, CreateFailsWhenTheConfiguredPathIsADirectory)
{
    std::error_code ec;
    ASSERT_TRUE(std::filesystem::create_directories(m_root_path / "missing.layer", ec))
        << ec.message();

    Storage storage = make_storage();

    const Result result = storage.create(SourceId {1}, Size {16}, std::byte {0x20});

    // A directory at the configured path is not a source that create() may
    // replace, and creation cannot complete over it.
    EXPECT_FALSE(result);
    EXPECT_EQ(result.error, Error::write_failed);
    EXPECT_TRUE(std::filesystem::is_directory(m_root_path / "missing.layer"));
}

} // namespace

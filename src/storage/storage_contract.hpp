#pragma once
#pragma once

#include "types.hpp"

#include <concepts>
#include <cstddef>
#include <span>


namespace landor::storage
{

/**
 * Compile-time contract for a Landor storage backend.
 *
 * Storage addresses logical objects using SourceId and byte ranges.
 *
 * The physical representation is deliberately invisible above this boundary.
 * A SourceId may ultimately refer to a host file, an SD-card file, an object
 * in raw SPI NAND, ROM, flash or some other platform-specific representation.
 *
 * Code using Storage must therefore know nothing about paths, file handles,
 * sectors, pages, erase blocks, mounts or other physical-storage details.
 *
 *
 * read()
 * ------
 *
 * Reads exactly destination.size() bytes starting at offset.
 *
 * Success means the complete destination span was filled. Partial reads are
 * not exposed through this interface.
 *
 *
 * write()
 * -------
 *
 * Replaces the specified logical byte range.
 *
 * Success guarantees that subsequent reads of that range observe the supplied
 * data. It does NOT imply that the physical storage was modified in place.
 *
 * A filesystem implementation may perform an ordinary file write while a raw
 * NAND implementation may need to copy erase blocks, allocate new pages,
 * update mappings or perform wear management.
 *
 * A source which cannot be written reports Error::read_only.
 *
 *
 * size()
 * ------
 *
 * Returns the logical size of a source in bytes.
 *
 * Physical allocation size, filesystem cluster size, NAND erase-block size,
 * spare areas and similar implementation details remain hidden.
 *
 *
 * Lifetime and initialization
 * ---------------------------
 *
 * Opening files, mounting devices, initializing SPI peripherals and shutting
 * hardware down are responsibilities of the selected concrete implementation.
 *
 * Construction of that implementation supplies whatever platform resources
 * and configuration it requires.
 *
 * There is deliberately no virtual base class. The build system selects one
 * concrete implementation and that implementation exposes:
 *
 *     using Storage = ConcreteStorageType;
 */
template<typename T>
concept StorageBackend =
    requires(
        T& storage,
        const T& const_storage,
        SourceId source,
        Offset offset,
        Size& source_size,
        std::span<std::byte> destination,
        std::span<const std::byte> data)
{
    {
        const_storage.read(source, offset, destination)
    } -> std::same_as<Result>;

    {
        storage.write(source, offset, data)
    } -> std::same_as<Result>;

    {
        const_storage.size(source, source_size)
    } -> std::same_as<Result>;
};


} // namespace landor::storage
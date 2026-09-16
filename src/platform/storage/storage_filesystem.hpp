#pragma once

#include "storage/storage_contract.hpp"

#include <span>
#include <string_view>


namespace landor::storage
{

/**
 * Filesystem-backed Landor storage.
 *
 * SourceId values index a build-generated table of relative paths.
 *
 * For example:
 *
 *     constexpr std::string_view world_sources[] =
 *     {
 *         "GoodMagePalace/GoodMagePalace.terrain.layer",
 *         "GoodMagePalace/GoodMagePalace.elevation.layer",
 *         "Home01/Home01.terrain.layer"
 *     };
 *
 *     Storage storage {
 *         "/home/ernesto/Landor/world",
 *         world_sources
 *     };
 *
 * SourceId 1 therefore refers to:
 *
 *     /home/ernesto/Landor/world/
 *         GoodMagePalace/GoodMagePalace.elevation.layer
 *
 * Paths stored in the source table are always relative. The root selects where
 * this instance of the world is physically located.
 *
 * This permits exactly the same source catalogue to be used from, for example:
 *
 *     ./world/
 *     /usr/share/landor/world/
 *     /landor/world/                 -- SD-card filesystem
 *     /tmp/landor-test/world/
 *
 * The path mapping is an implementation detail of StorageFilesystem.
 * SourceId remains an opaque logical identity to Patch, Map and the rest of
 * the world code.
 *
 * The strings referred to by root and sources must outlive this object.
 *
 * SourceIds are expected to be dense and are used directly as indexes into
 * the source table. There is therefore no map, hash table or source lookup
 * structure at runtime.
 */
class StorageFilesystem
{
public:
    constexpr StorageFilesystem(
        std::string_view root,
        std::span<const std::string_view> sources) noexcept
        : m_root(root),
          m_sources(sources)
    {
    }


    /**
     * Read one complete logical byte range.
     *
     * Returns Error::invalid_source when source is outside the source table,
     * Error::out_of_range when the requested byte range lies outside the
     * logical source, or an appropriate implementation error when the
     * filesystem operation itself fails.
     */
    [[nodiscard]] Result read(
        SourceId source,
        Offset offset,
        std::span<std::byte> destination) const noexcept;


    /**
     * Replace one logical byte range.
     *
     * Successful completion guarantees that later reads observe the supplied
     * bytes. The interface makes no promise about buffering or the physical
     * mechanics used by the filesystem.
     */
    [[nodiscard]] Result write(
        SourceId source,
        Offset offset,
        std::span<const std::byte> data) noexcept;


    /**
     * Return the logical size of one source.
     */
    [[nodiscard]] Result size(
        SourceId source,
        Size& result) const noexcept;


private:
    /**
     * Return the configured relative path for a source.
     *
     * nullptr means that source is not present in the source table.
     *
     * Joining this path with m_root is deliberately kept inside the concrete
     * filesystem implementation.
     */
    [[nodiscard]] constexpr const std::string_view*
    relative_path(SourceId source) const noexcept
    {
        const auto index = static_cast<std::size_t>(source);

        if (index >= m_sources.size())
            return nullptr;

        return &m_sources[index];
    }


    std::string_view                  m_root;
    std::span<const std::string_view> m_sources;
};


static_assert(StorageBackend<StorageFilesystem>);

/*
 * The rest of Landor uses storage::Storage.
 *
 * The build system selects the storage.hpp containing this alias; no runtime
 * dispatch and no preprocessor backend selector are required.
 */
using Storage = StorageFilesystem;


} // namespace landor::storage
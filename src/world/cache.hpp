#pragma once

#include "area.hpp"
#include "chunk.hpp"
#include "layer.hpp"
#include "tile.hpp"

#include <concepts>
#include <cstddef>
#include <span>


namespace landor::geo
{

/**
 * Bounded resident layer cache used internally by Map.
 *
 * Cache remains layer-oriented internally. It does not reorganise the resident
 * world into an array of Tiles. Instead it retains resident values by layer
 * and packs one value from every layer only when producing a Tile.
 *
 * The Cache has a canonical two-dimensional fetch/residency granularity:
 * CacheChunkSide x CacheChunkSide. If a caller needs a smaller Region or one
 * Tile, the corresponding layer request is promoted to the containing
 * canonical Chunk. Missing layers are then filled from the Map's normal
 * source-resolution / Storage machinery.
 *
 * Chunk and Cache are implementation details below Map. Game/simulation code
 * should use Map::at() and Region rather than inspecting Cache residency.
 *
 * This header intentionally fixes the interface and invariants but leaves the
 * replacement/write-back policy for the implementation slice. In particular,
 * filling a cache whose bounded capacity is exhausted must not silently throw
 * away dirty world state.
 */
template<
    std::size_t Capacity,
    std::size_t CacheChunkSide,
    typename CoordT,
    Layer... Layers>
class Cache
{
public:
    using coord_type = CoordT;
    using area_type  = Area<CoordT>;
    using chunk_type = Chunk<CoordT>;
    using tile_type  = Tile<CoordT, Layers...>;

    static constexpr std::size_t capacity    = Capacity;
    static constexpr std::size_t chunk_side  = CacheChunkSide;
    static constexpr std::size_t layer_count = sizeof...(Layers);

    static_assert(Capacity > 0, "Cache capacity must be non-zero");
    static_assert(CacheChunkSide > 0, "Cache chunk side must be non-zero");

    constexpr Cache() noexcept = default;

    /**
     * Return the canonical cache Chunk containing position for LayerT.
     *
     * This operation is pure geometry; it performs no lookup or I/O.
     */
    template<Layer LayerT>
        requires (std::same_as<LayerT, Layers> || ...)
    [[nodiscard]] static constexpr chunk_type
    chunk_for(coord_type position) noexcept
    {
        return chunk_type::template containing<LayerT>(
            position,
            static_cast<typename chunk_type::side_type>(CacheChunkSide));
    }

    /**
     * True when LayerT is resident at position.
     */
    template<Layer LayerT>
        requires (std::same_as<LayerT, Layers> || ...)
    [[nodiscard]] bool contains(coord_type position) const noexcept;

    /**
     * True when every layer required to construct a complete Tile is resident
     * at position.
     */
    [[nodiscard]] bool contains(coord_type position) const noexcept;

    /**
     * Read one resident layer value.
     *
     * Precondition: contains<LayerT>(position).
     *
     * This is internal Map/Cache access; public world access remains checked
     * through Map::at().
     */
    template<Layer LayerT>
        requires (std::same_as<LayerT, Layers> || ...)
    [[nodiscard]] typename LayerT::value_type
    value(coord_type position) const noexcept;

    /**
     * Pack one Tile value from the resident layers.
     *
     * Precondition: contains(position).
     *
     * The returned Tile owns its coordinate and property values and remains
     * valid if the cache entry is later replaced.
     */
    [[nodiscard]] tile_type tile(coord_type position) const noexcept;

    /**
     * Install one canonical layer Chunk into the cache.
     *
     * `values` are in row-major order and contain exactly
     * CacheChunkSide * CacheChunkSide elements.
     *
     * The Chunk must:
     *   - identify LayerT;
     *   - be aligned;
     *   - have side == CacheChunkSide.
     *
     * Returns false when the bounded cache cannot accept the Chunk without a
     * replacement/write-back decision. This function must not silently discard
     * dirty state merely to make room.
     *
     * The implementation may fill the layer plane directly; it does not need
     * to create intermediate Tiles.
     */
    template<Layer LayerT>
        requires (std::same_as<LayerT, Layers> || ...)
    [[nodiscard]] bool fill(
        const chunk_type& chunk,
        std::span<const typename LayerT::value_type> values) noexcept;

    /**
     * Invalidate cached values intersecting an Area.
     *
     * This is a residency operation, not permission to discard dirty state.
     * The implementation must honour the eventual write-back policy.
     */
    void invalidate(const area_type& area) noexcept;

    /**
     * Invalidate all resident data, subject to the same dirty-state rule.
     */
    void invalidate_all() noexcept;

    /**
     * Compile-time capability query for the layer set carried by this cache.
     */
    template<Layer LayerT>
    [[nodiscard]] static consteval bool supports() noexcept
    {
        return (std::same_as<LayerT, Layers> || ...);
    }

private:
    /*
     * Deliberately left for the implementation slice.
     *
     * Required representation invariant:
     *
     *   resident data stays layer-oriented
     *
     * A practical implementation can use fixed-capacity cache slots whose
     * spatial key is the canonical chunk origin, with a separate plane and
     * residency/dirty metadata for every Layer. tile() gathers one value from
     * each plane into the small Tile value.
     *
     * Do not replace this with a resident Tile array merely because Tile is the
     * public presentation type.
     */
};


} // namespace landor::geo

#pragma once

#include "area.hpp"
#include "chunk.hpp"
#include "layer.hpp"
#include "tile.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <tuple>


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
 * Capacity counts resident spatial slots, not individual layer chunks. One
 * slot may therefore contain several independently resident layer planes for
 * the same canonical area.
 *
 * This first implementation deliberately has no replacement or write-back
 * policy. When every spatial slot is occupied, filling a new area fails rather
 * than evicting existing state.
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
    [[nodiscard]] bool contains(coord_type position) const noexcept
    {
        constexpr auto index = detail::layer_index<LayerT, Layers...>();
        const auto origin = canonical_origin(position);
        const auto* slot = find_slot(origin);

        return slot != nullptr && slot->resident[index];
    }

    /**
     * True when every layer required to construct a complete Tile is resident
     * at position.
     */
    [[nodiscard]] bool contains(coord_type position) const noexcept
    {
        const auto origin = canonical_origin(position);
        const auto* slot = find_slot(origin);

        if (slot == nullptr)
        {
            return false;
        }

        return (slot->resident[detail::layer_index<Layers, Layers...>()] && ...);
    }

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
    value(coord_type position) const noexcept
    {
        constexpr auto index = detail::layer_index<LayerT, Layers...>();
        const auto origin = canonical_origin(position);
        const auto* slot = find_slot(origin);

        assert(slot != nullptr);
        assert(slot->resident[index]);

        const auto local = local_index(position, origin);
        return std::get<index>(slot->planes)[local];
    }

    /**
     * Pack one Tile value from the resident layers.
     *
     * Precondition: contains(position).
     *
     * The returned Tile owns its coordinate and property values and remains
     * valid if the cache entry is later replaced.
     */
    [[nodiscard]] tile_type tile(coord_type position) const noexcept
    {
        assert(contains(position));

        return tile_type {
            position,
            std::array<typename tile_type::property_type, layer_count> {
                value<Layers>(position)...
            }
        };
    }

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
     * The implementation fills the layer plane directly; it does not create
     * intermediate Tiles.
     */
    template<Layer LayerT>
        requires (std::same_as<LayerT, Layers> || ...)
    [[nodiscard]] bool fill(
        const chunk_type& chunk,
        std::span<const typename LayerT::value_type> values) noexcept
    {
        if (chunk.layer() != LayerT::id
            || chunk.side() != CacheChunkSide
            || !chunk.aligned()
            || values.size() != cells_per_plane)
        {
            return false;
        }

        auto* slot = find_slot(chunk.origin());
        if (slot == nullptr)
        {
            slot = find_free_slot();
            if (slot == nullptr)
            {
                return false;
            }

            slot->occupied = true;
            slot->origin = chunk.origin();
            slot->resident.fill(false);
        }

        constexpr auto index = detail::layer_index<LayerT, Layers...>();
        auto& plane = std::get<index>(slot->planes);
        std::copy(values.begin(), values.end(), plane.begin());
        slot->resident[index] = true;

        return true;
    }

    /**
     * Invalidate cached values intersecting an Area.
     *
     * The initial cache core has no mutable/dirty layer state yet, so dropping
     * residency cannot discard altered world state. A later write-back slice
     * must strengthen this operation before dirty values are introduced.
     */
    void invalidate(const area_type& area) noexcept
    {
        if (area.is_empty())
        {
            return;
        }

        for (auto& slot : m_slots)
        {
            if (!slot.occupied)
            {
                continue;
            }

            const chunk_type chunk {
                static_cast<LayerId>(0),
                slot.origin,
                static_cast<typename chunk_type::side_type>(CacheChunkSide)
            };

            if (chunk.area().intersects(area))
            {
                slot = Slot {};
            }
        }
    }

    /**
     * Invalidate all resident data.
     *
     * The same initial-core restriction as invalidate(area) applies: no dirty
     * state exists yet.
     */
    void invalidate_all() noexcept
    {
        for (auto& slot : m_slots)
        {
            slot = Slot {};
        }
    }

    /**
     * Compile-time capability query for the layer set carried by this cache.
     */
    template<Layer LayerT>
    [[nodiscard]] static consteval bool supports() noexcept
    {
        return (std::same_as<LayerT, Layers> || ...);
    }

private:
    static constexpr std::size_t cells_per_plane = CacheChunkSide * CacheChunkSide;

    template<Layer LayerT>
    using plane_type = std::array<typename LayerT::value_type, cells_per_plane>;

    /** One canonical spatial area with independent layer residency. */
    struct Slot
    {
        bool occupied = false;
        coord_type origin {};
        std::array<bool, layer_count> resident {};
        std::tuple<plane_type<Layers>...> planes {};
    };

    [[nodiscard]] static constexpr coord_type
    canonical_origin(coord_type position) noexcept
    {
        return chunk_type::containing(
            static_cast<LayerId>(0),
            position,
            static_cast<typename chunk_type::side_type>(CacheChunkSide)).origin();
    }

    [[nodiscard]] static constexpr std::size_t
    local_index(coord_type position, coord_type origin) noexcept
    {
        const auto local_x = static_cast<std::size_t>(
            static_cast<std::int64_t>(position.x())
            - static_cast<std::int64_t>(origin.x()));
        const auto local_y = static_cast<std::size_t>(
            static_cast<std::int64_t>(position.y())
            - static_cast<std::int64_t>(origin.y()));

        assert(local_x < CacheChunkSide);
        assert(local_y < CacheChunkSide);

        return local_y * CacheChunkSide + local_x;
    }

    [[nodiscard]] Slot* find_slot(coord_type origin) noexcept
    {
        for (auto& slot : m_slots)
        {
            if (slot.occupied && slot.origin == origin)
            {
                return &slot;
            }
        }

        return nullptr;
    }

    [[nodiscard]] const Slot* find_slot(coord_type origin) const noexcept
    {
        for (const auto& slot : m_slots)
        {
            if (slot.occupied && slot.origin == origin)
            {
                return &slot;
            }
        }

        return nullptr;
    }

    [[nodiscard]] Slot* find_free_slot() noexcept
    {
        for (auto& slot : m_slots)
        {
            if (!slot.occupied)
            {
                return &slot;
            }
        }

        return nullptr;
    }

    std::array<Slot, Capacity> m_slots {};
};


} // namespace landor::geo

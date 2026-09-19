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
 * This first implementation deliberately has no replacement policy. When
 * every spatial slot is occupied, filling a new area fails rather than
 * evicting existing state.
 *
 * Dirty tracking is per resident layer plane per spatial slot (one dirty
 * bit, see DESIGN_DECISIONS.md D-37). A normal fill installs clean backing
 * state and never overwrites a dirty plane; set() marks a changed plane
 * dirty; and invalidation that would discard a dirty plane is refused
 * atomically. The only dirty-clearing path is the explicit write-back hook
 * mark_clean<LayerT>() (D-38), which Map's flush<LayerT>() calls after a
 * plane has been fully persisted: no fill, invalidation or eviction path
 * clears dirt, so a dirty plane survives every other operation.
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
     * Mutate one resident layer value.
     *
     * Precondition: contains<LayerT>(position).
     *
     * A value that differs from the current live value replaces it and marks
     * the whole resident layer plane dirty. A value equal to the current live
     * value changes nothing and does not turn a clean plane dirty; a plane
     * that is already dirty stays dirty. Once dirty, the plane stays dirty
     * until the explicit write-back clears it through mark_clean<LayerT>()
     * (D-38). No mutation sequence that happens to restore the backing
     * contents is detected.
     */
    template<Layer LayerT>
        requires (std::same_as<LayerT, Layers> || ...)
    void set(
        coord_type position,
        typename LayerT::value_type value) noexcept
    {
        constexpr auto index = detail::layer_index<LayerT, Layers...>();
        const auto origin = canonical_origin(position);
        auto* slot = find_slot(origin);

        assert(slot != nullptr);
        assert(slot->resident[index]);

        auto& plane = std::get<index>(slot->planes);
        const auto local = local_index(position, origin);

        if (plane[local] == value)
        {
            return;
        }

        plane[local] = value;
        slot->dirty[index] = true;
    }

    /**
     * True when the LayerT plane resident at position is dirty.
     *
     * A missing plane is never dirty, a resident clean plane is not dirty,
     * and only a resident plane altered since it was filled reports dirty.
     */
    template<Layer LayerT>
        requires (std::same_as<LayerT, Layers> || ...)
    [[nodiscard]] bool dirty(coord_type position) const noexcept
    {
        constexpr auto index = detail::layer_index<LayerT, Layers...>();
        const auto origin = canonical_origin(position);
        const auto* slot = find_slot(origin);

        return slot != nullptr && slot->resident[index] && slot->dirty[index];
    }

    /**
     * True when any resident layer plane in any occupied slot is dirty.
     */
    [[nodiscard]] bool has_dirty() const noexcept
    {
        for (const auto& slot : m_slots)
        {
            if (slot_has_dirty_plane(slot))
            {
                return true;
            }
        }

        return false;
    }

    /**
     * Enumerate the canonical Chunks of every dirty resident LayerT plane.
     *
     * The enumeration follows the deterministic slot order of the bounded
     * slot array: an occupied slot whose LayerT plane is both resident and
     * dirty contributes exactly one canonical Chunk
     * ({ LayerT::id, slot origin, CacheChunkSide }); clean planes, missing
     * planes and planes of other layers contribute nothing.
     *
     * The caller supplies the fixed-size destination, so the enumeration
     * performs no heap allocation. output must be able to hold capacity
     * entries; the bound is asserted, not returned.
     */
    template<Layer LayerT>
        requires (std::same_as<LayerT, Layers> || ...)
    [[nodiscard]] std::size_t
    dirty_chunks(std::span<chunk_type> output) const noexcept
    {
        assert(output.size() >= capacity);

        constexpr auto index = detail::layer_index<LayerT, Layers...>();

        std::size_t count = 0;
        for (const auto& slot : m_slots)
        {
            // A non-resident plane is never dirty; the flags are checked
            // together, exactly like slot_has_dirty_plane().
            if (slot.occupied && slot.resident[index] && slot.dirty[index])
            {
                output[count++] = chunk_type {
                    LayerT::id,
                    slot.origin,
                    static_cast<typename chunk_type::side_type>(CacheChunkSide)
                };
            }
        }

        return count;
    }


    /**
     * Clear the dirty bit of one resident LayerT plane.
     *
     * Preconditions: a spatial slot matching chunk.origin() exists, the
     * LayerT plane is resident there, and it is dirty there.
     *
     * This is the write-back hook (D-38): Map's explicit flush() clears a
     * plane through it only after the plane's complete persistence has
     * succeeded. The operation touches nothing else: no values, no
     * residency and no other layer's dirty state are altered. There is
     * deliberately no general "clear every dirty plane" escape hatch.
     */
    template<Layer LayerT>
        requires (std::same_as<LayerT, Layers> || ...)
    void mark_clean(const chunk_type& chunk) noexcept
    {
        constexpr auto index = detail::layer_index<LayerT, Layers...>();
        auto* slot = find_slot(chunk.origin());

        assert(slot != nullptr);
        assert(slot->resident[index]);
        assert(slot->dirty[index]);

        slot->dirty[index] = false;
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
     * A normal source-resolution fill installs clean backing state. It
     * returns false when the bounded cache cannot accept the Chunk without a
     * replacement/write-back decision, or when the target layer plane is
     * already dirty: a fill must never overwrite or otherwise discard dirty
     * state. Filling another missing layer in the same spatial slot remains
     * allowed when that plane is clean.
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
            slot->dirty.fill(false);
        }

        constexpr auto index = detail::layer_index<LayerT, Layers...>();

        // Never overwrite a dirty plane: dirty state must not be silently
        // discarded to make room or to refresh backing state.
        if (slot->dirty[index])
        {
            return false;
        }

        auto& plane = std::get<index>(slot->planes);
        std::copy(values.begin(), values.end(), plane.begin());
        slot->resident[index] = true;

        return true;
    }

    /**
     * Invalidate cached values intersecting an Area.
     *
     * The operation is atomic. It first determines every occupied slot that
     * would be discarded; when any of them carries a dirty resident layer
     * plane the whole operation is refused and nothing is changed. Only when
     * every intersecting slot is clean are all intersecting slots discarded.
     *
     * A dirty slot outside the requested area does not block the operation:
     * only dirty state that the operation would actually discard can be
     * refused.
     *
     * Refusing is the safe behaviour: a plane may mix one mutated cell with
     * many backing-resolved cells, and keeping the whole plane after a
     * Placement change would preserve stale composition. The caller can
     * clear the way by flushing dirty planes through Map's explicit
     * write-back (D-38) before retrying.
     *
     * Returns true when the requested slots were discarded (or none were
     * intersected), false when the operation was refused unchanged.
     */
    [[nodiscard]] bool invalidate(const area_type& area) noexcept
    {
        if (area.is_empty())
        {
            return true;
        }

        // First pass: refuse the whole operation when it would discard a
        // dirty resident layer plane. Nothing is changed before this pass.
        for (const auto& slot : m_slots)
        {
            if (!slot.occupied || !slot_intersects(slot, area))
            {
                continue;
            }

            if (slot_has_dirty_plane(slot))
            {
                return false;
            }
        }

        // Second pass: every intersecting slot is clean; discard them all.
        for (auto& slot : m_slots)
        {
            if (slot.occupied && slot_intersects(slot, area))
            {
                slot = Slot {};
            }
        }

        return true;
    }

    /**
     * Invalidate all resident data.
     *
     * Atomic like invalidate(area): when any occupied slot carries a dirty
     * resident layer plane the whole operation is refused and nothing is
     * changed; otherwise every slot is cleared.
     *
     * Returns true when every slot was cleared, false when the operation was
     * refused unchanged.
     */
    [[nodiscard]] bool invalidate_all() noexcept
    {
        if (has_dirty())
        {
            return false;
        }

        for (auto& slot : m_slots)
        {
            slot = Slot {};
        }

        return true;
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

    /**
     * One canonical spatial area with independent layer residency and dirty
     * state.
     *
     * A non-resident plane is never dirty; the two flags are maintained
     * together. Dirty granularity is one bit per resident layer plane
     * (D-37): mutating one cell marks the whole plane dirty.
     */
    struct Slot
    {
        bool occupied = false;
        coord_type origin {};
        std::array<bool, layer_count> resident {};
        std::array<bool, layer_count> dirty {};
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

    /** The slot's canonical chunk, used for intersection tests. */
    [[nodiscard]] static constexpr chunk_type
    slot_chunk(const Slot& slot) noexcept
    {
        return chunk_type {
            static_cast<LayerId>(0),
            slot.origin,
            static_cast<typename chunk_type::side_type>(CacheChunkSide)
        };
    }

    [[nodiscard]] static bool
    slot_intersects(const Slot& slot, const area_type& area) noexcept
    {
        return slot_chunk(slot).area().intersects(area);
    }

    /**
     * True when the slot carries at least one dirty resident layer plane.
     *
     * A non-resident plane is never dirty, so the flag pair is checked
     * together. The layer count is small, so a plain scan suffices.
     */
    [[nodiscard]] static bool slot_has_dirty_plane(const Slot& slot) noexcept
    {
        for (std::size_t i = 0; i < layer_count; ++i)
        {
            if (slot.resident[i] && slot.dirty[i])
            {
                return true;
            }
        }

        return false;
    }

    std::array<Slot, Capacity> m_slots {};
};


} // namespace landor::geo

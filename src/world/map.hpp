#pragma once

#include "area.hpp"
#include "layer.hpp"
#include "orientation.hpp"
#include "patch.hpp"
#include "placement.hpp"
#include "tile.hpp"

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <type_traits>


namespace landor::geo
{

class Storage;
class Generator;

using MapId = std::uint16_t;


/**
 * The logical spatial surface of one part of the world.
 *
 * A Map is not an authored map file and is not a rectangular array of stored
 * Tiles.
 *
 * Authored files describe Patches: relatively small, interesting pieces of
 * the world such as a house, palace, swamp, well or field. Patches are placed
 * in the Map, possibly several times and possibly rotated or reflected.
 * Coordinates not covered by authored content may be supplied by relatively
 * boring procedural generation.
 *
 * The Map assembles those sources into one coherent world.
 *
 * For example, a player's home might be composed from reusable authored
 * pieces:
 *
 *     House03
 *     Well01
 *     Barn02
 *     Field04
 *     AncientOak
 *
 * A deterministic PatchSet composer can arrange those pieces around the
 * player's home coordinate. Map does not need to know why they were chosen;
 * once placed, they are simply Placements contributing spatial data.
 *
 *
 * Layers
 * ------
 *
 * Map data remains layer-oriented all the way down.
 *
 * Terrain, elevation, fire, water, moisture, wind and other spatial
 * properties are independent layers. An authored Patch may supply any subset
 * of them.
 *
 * A Patch supplying no Fire layer does not mean that fire cannot exist there.
 * It only means that Patch provides no authored fire data. The Map can obtain
 * the normal fallback value, and later simulation or magic may create live
 * fire state at that coordinate.
 *
 * Consequently, layer capability belongs to the game build while authored
 * layer presence belongs to each Patch.
 *
 *
 * Tiles
 * -----
 *
 * A Tile is synthetic.
 *
 * When a caller asks:
 *
 *     auto tile = map.at(position);
 *
 * Map resolves each layer independently and assembles a Tile value containing
 * the complete state visible at that coordinate.
 *
 * The Tile owns those values. It contains no references into layer storage,
 * cached blocks, Patch data or the managed heap.
 *
 * There is therefore no authoritative array of complete Tiles in a Map.
 * A small Tile cache may memoise recently assembled answers, but that cache is
 * disposable and has no semantic meaning.
 *
 *
 * Storage and alteration
 * ----------------------
 *
 * Map uses Storage to obtain non-resident authored or materialised layer data.
 * Storage abstracts where those bytes physically live.
 *
 * Altered layer state belongs to the working world. Changes may eventually be
 * written back to the storage working copy, but the timing of cache eviction
 * and write-back must never change the answer returned by Map.
 *
 * Procedurally supplied regions may be materialised lazily when first changed.
 * The exact block and write-back machinery lives below this interface.
 *
 *
 * Placements
 * ----------
 *
 * Map owns its live Placements.
 *
 * Authored Patch descriptors are immutable and may be shared by many
 * Placements. Moving, rotating or reflecting a Placement changes only where
 * that occurrence contributes to the Map.
 *
 * Placement mutation goes through Map so cached spatial answers can be
 * invalidated correctly.
 *
 * Story systems do not belong here. They refer to PlacementIds through
 * world-facing Place objects; Map only deals with geometry and spatial state.
 *
 *
 * Overlap
 * -------
 *
 * Placements are ordered. When several Placements cover the same coordinate,
 * resolution is performed independently for each layer. A Placement which
 * does not provide a particular layer does not hide a lower contribution to
 * that layer.
 *
 * The precise precedence rule is deliberately kept in Map rather than Patch:
 * Patch describes authored content; Map decides how placed content composes.
 *
 *
 * Fixed storage
 * -------------
 *
 * MaxPlacements and TileCacheSize are compile-time bounds. Neither requires
 * the managed heap. The managed heap remains available for genuinely dynamic
 * game objects whose lifetime requires it.
 */
template<
    std::size_t MaxPlacements,
    std::size_t TileCacheSize,
    typename CoordT = Coord32,
    Layer... Layers>
class Map
{
public:
    using coord_type     = CoordT;
    using area_type      = Area<CoordT>;
    using patch_type     = Patch<CoordT>;
    using placement_type = Placement<CoordT>;
    using tile_type      = Tile<Layers...>;


    /**
     * Construct a live Map over an authored Patch catalogue.
     *
     * Patch descriptors and Storage outlive the Map. Map owns neither.
     *
     * Generator supplies values where no authored or materialised value
     * exists. It may return a constant default for some layers and procedural
     * values for others.
     */
    constexpr Map(
        MapId id,
        area_type area,
        std::span<const patch_type> patches,
        Storage& storage,
        Generator& generator) noexcept
        : m_id(id),
          m_area(area),
          m_patches(patches),
          m_storage(storage),
          m_generator(generator)
    {
    }


    Map(const Map&) = delete;
    Map& operator=(const Map&) = delete;

    Map(Map&&) = delete;
    Map& operator=(Map&&) = delete;


    /// Stable identity of this Map.
    [[nodiscard]] constexpr MapId id() const noexcept
    {
        return m_id;
    }


    /// Logical coordinate extent covered by this Map.
    [[nodiscard]] constexpr const area_type& area() const noexcept
    {
        return m_area;
    }


    /// Returns whether a coordinate belongs to this Map.
    [[nodiscard]] constexpr bool contains(coord_type position) const noexcept
    {
        return m_area.contains(position);
    }


    /**
     * Returns whether this build understands LayerT.
     *
     * This is a property of the Map type, not of any particular Patch.
     */
    template<Layer LayerT>
    [[nodiscard]] static consteval bool supports() noexcept
    {
        return (std::same_as<LayerT, Layers> || ...);
    }


    /**
     * Resolve one layer at one coordinate.
     *
     * Resolution may obtain the value from working state, an authored Patch,
     * procedural generation or a layer default.
     *
     * A missing authored layer is not an error.
     */
    template<Layer LayerT>
        requires (std::same_as<LayerT, Layers> || ...)
    [[nodiscard]] typename LayerT::value_type
    value(coord_type position) const;


    /**
     * Resolve the complete synthetic Tile at one coordinate.
     *
     * Every layer understood by this Map is resolved before the Tile is
     * returned. The result owns its values and remains valid regardless of
     * later cache movement, storage activity or managed-heap compaction.
     */
    [[nodiscard]] tile_type at(coord_type position) const;


    /**
     * Find an immutable authored Patch descriptor.
     *
     * Returns nullptr when the id is not present in this Map's catalogue.
     */
    [[nodiscard]] constexpr const patch_type*
    patch(PatchId id) const noexcept
    {
        for (const auto& patch : m_patches)
        {
            if (patch.id() == id)
                return &patch;
        }

        return nullptr;
    }


    /**
     * Place a Patch at its natural authored position.
     *
     * Map allocates and returns a stable PlacementId for the occurrence.
     */
    [[nodiscard]] PlacementId place(PatchId patch);


    /**
     * Place a Patch at an explicit position and orientation.
     *
     * This is the primitive used by PatchSet composition. Reusing an authored
     * Patch does not duplicate its layer data.
     */
    [[nodiscard]] PlacementId place(
        PatchId patch,
        coord_type position,
        Orientation orientation = {});


    /**
     * Return a live Placement by identity.
     *
     * Only const access is exposed. Spatial mutation must go through Map so
     * cache invalidation cannot be bypassed accidentally.
     */
    [[nodiscard]] const placement_type*
    placement(PlacementId id) const noexcept;


    /// Number of currently live Placements.
    [[nodiscard]] constexpr std::size_t placement_count() const noexcept
    {
        return m_placement_count;
    }


    /**
     * Move a Placement.
     *
     * The authored Patch remains unchanged. Map invalidates spatial answers
     * affected by both the old and new coverage.
     */
    bool set_position(
        PlacementId id,
        coord_type position);


    /**
     * Rotate a Placement.
     *
     * Rotation affects its contribution to every layer supplied by its Patch.
     */
    bool set_rotation(
        PlacementId id,
        Rotation rotation);


    /**
     * Reflect a Placement.
     *
     * Reflection is applied before rotation, as defined by Orientation.
     */
    bool set_reflection(
        PlacementId id,
        Reflection reflection);


    /// Replace a Placement's complete orientation.
    bool set_orientation(
        PlacementId id,
        Orientation orientation);


private:
    /**
     * One optional memoised synthetic Tile.
     *
     * This cache is strictly derived state. Entries can be discarded at any
     * time without changing the world.
     */
    struct CachedTile
    {
        coord_type position;
        tile_type  tile;
    };


    /**
     * Resolve LayerT without consulting the synthetic Tile cache.
     *
     * This is where authored Placements, working state and Generator fallback
     * eventually meet.
     */
    template<Layer LayerT>
        requires (std::same_as<LayerT, Layers> || ...)
    [[nodiscard]] typename LayerT::value_type
    resolve(coord_type position) const;


    /// Find a Placement for internal mutation.
    [[nodiscard]] placement_type*
    mutable_placement(PlacementId id) noexcept;


    /// Invalidate a cached answer at one coordinate.
    void invalidate(coord_type position) const noexcept;


    /// Invalidate cached answers intersecting an area.
    void invalidate(const area_type& area) const noexcept;


    /// Discard every synthetic cached Tile.
    void invalidate_all() const noexcept;


    MapId m_id;
    area_type m_area;

    /*
     * Patch descriptors are authored content and are shared. Map keeps only a
     * view of the catalogue.
     */
    std::span<const patch_type> m_patches;

    /*
     * Storage owns the persistent backing data. Generator supplies the
     * otherwise empty parts of the logical Map.
     */
    Storage&   m_storage;
    Generator& m_generator;

    /*
     * Placements are live Map state. std::optional gives us fixed-capacity
     * slots without requiring Placement to have a meaningless default state.
     */
    std::array<std::optional<placement_type>, MaxPlacements> m_placements {};
    std::size_t m_placement_count = 0;
    PlacementId m_next_placement_id = 1;

    /*
     * A very small cache of fully synthesized answers is permitted because it
     * is only an optimisation. Authoritative state remains layer-oriented.
     */
    mutable std::array<std::optional<CachedTile>, TileCacheSize> m_tile_cache {};
    mutable std::size_t m_next_cache_slot = 0;
};


} // namespace landor::geo
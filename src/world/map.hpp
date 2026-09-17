#pragma once

#include "area.hpp"
#include "cache.hpp"
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
 * The Cache preserves this layer-oriented representation. It does not turn
 * the resident world into an array of Tiles.
 *
 *
 * Tiles
 * -----
 *
 * A Tile is a small value presentation of one Map coordinate.
 *
 * When a caller asks:
 *
 *     Tile tile = map.at(position);
 *
 * Map ensures that the required layer data is resident, then packs the value
 * of each layer at that coordinate into a Tile. The Tile also carries its
 * coordinate as identity.
 *
 * The Tile owns those values. It contains no references into layer storage,
 * cached Chunks, Patch data or the managed heap. Copying a Tile is therefore
 * ordinary value copying and remains valid regardless of later cache
 * replacement or storage activity.
 *
 * Symbolic access such as:
 *
 *     tile[Fire]
 *
 * selects a value already packed into that Tile. It does not perform a Map,
 * Cache or Storage lookup.
 *
 * There is no authoritative array of complete Tiles in a Map. The
 * authoritative/resident representation remains layer-oriented; Tile is the
 * cheap per-position presentation given to callers.
 *
 *
 * Cache and Chunks
 * ----------------
 *
 * Cache is private Map machinery. Game and simulation code does not inspect
 * it directly.
 *
 * The Cache has a canonical two-dimensional Chunk size. If a requested Tile
 * or Region falls inside a non-resident cache area, Map obtains the required
 * layer Chunks from authored, working, procedural or default sources and
 * installs those layer values in the Cache.
 *
 * A Chunk belongs to one layer. Filling the same spatial cache area for five
 * layers can therefore involve five layer Chunks.
 *
 * Chunk geometry is an I/O/cache concern; it does not constrain the shape of
 * higher-level algorithmic Regions.
 *
 *
 * Checked access
 * --------------
 *
 * Public Map access is checked. at() and value() must validate that the
 * coordinate belongs to this Map before performing cache or storage work.
 *
 * There is deliberately no unchecked operator[] alternative. A Map access can
 * involve cache lookup, Chunk selection and storage I/O, so avoiding a couple
 * of coordinate comparisons would provide no useful optimisation.
 *
 *
 * Storage and alteration
 * ----------------------
 *
 * Map uses Storage to obtain non-resident authored or materialised layer data.
 * Storage abstracts where those bytes physically live.
 *
 * Altered layer state belongs to the working world. Changes may eventually be
 * written back to the storage working copy, but cache eviction and write-back
 * must never change the logical answer returned by Map.
 *
 * Procedurally supplied regions may be materialised lazily when first changed.
 * The exact replacement and write-back policy lives below this interface.
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
 * Placement mutation goes through Map so affected cached layer data can be
 * invalidated/refreshed correctly.
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
 * MaxPlacements, CacheCapacity and CacheChunkSide are compile-time bounds or
 * configuration. Neither Placement storage nor Cache requires the managed
 * heap merely to enforce those bounds. The managed heap remains available for
 * genuinely dynamic game objects whose lifetime requires it.
 */
template<
    std::size_t MaxPlacements,
    std::size_t CacheCapacity,
    std::size_t CacheChunkSide,
    typename CoordT = Coord32,
    Layer... Layers>
class Map
{
public:
    using coord_type     = CoordT;
    using scalar_type    = typename CoordT::scalar_type;
    using area_type      = Area<CoordT>;
    using patch_type     = Patch<CoordT>;
    using placement_type = Placement<CoordT>;
    using tile_type      = Tile<CoordT, Layers...>;
    using cache_type     = Cache<
        CacheCapacity,
        CacheChunkSide,
        CoordT,
        Layers...>;


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
     * Return one layer value at one coordinate.
     *
     * This is checked Map access just like at(): position must be validated
     * before cache/storage work begins.
     *
     * Resolution may obtain the value from resident working state, authored
     * Patch data, procedural generation or a layer default. A missing authored
     * layer is not an error.
     */
    template<Layer LayerT>
        requires (std::same_as<LayerT, Layers> || ...)
    [[nodiscard]] typename LayerT::value_type
    value(coord_type position) const;


    /**
     * Return a complete Tile value at one coordinate.
     *
     * Access is checked. Map first validates that position belongs to its
     * logical Area, ensures every required layer is resident in Cache, then
     * asks Cache to pack those values together with the coordinate into a
     * Tile.
     *
     * The returned Tile owns its values. Later cache replacement, storage
     * activity or managed-heap compaction cannot invalidate it.
     */
    [[nodiscard]] tile_type at(coord_type position) const;


    /** Convenience checked access from scalar coordinates. */
    [[nodiscard]] tile_type at(scalar_type x, scalar_type y) const
    {
        return at(coord_type {x, y});
    }


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
     *
     * Returns std::nullopt when every placement slot is occupied. Placement
     * capacity is a compile-time bound, so exhaustion is a normal outcome and
     * is reported instead of being hidden behind a reserved PlacementId.
     */
    [[nodiscard]] std::optional<PlacementId>
    place(PatchId patch);


    /**
     * Place a Patch at an explicit position and orientation.
     *
     * This is the primitive used by PatchSet composition. Reusing an authored
     * Patch does not duplicate its layer data.
     *
     * Returns std::nullopt when every placement slot is occupied.
     */
    [[nodiscard]] std::optional<PlacementId>
    place(
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
     * The authored Patch remains unchanged. Map invalidates affected cached
     * layer data for both the old and new coverage.
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
     * Resolve LayerT from authored Placements, working state and Generator
     * fallback when Cache needs to fill a missing Chunk.
     *
     * This source-resolution operation is separate from Tile presentation.
     */
    template<Layer LayerT>
        requires (std::same_as<LayerT, Layers> || ...)
    [[nodiscard]] typename LayerT::value_type
    resolve(coord_type position) const;


    /**
     * Ensure that every layer required for a complete Tile is resident at
     * position.
     *
     * The implementation promotes the request to CacheChunkSide-aligned
     * Chunks and fills missing layer Chunks through resolve()/Storage.
     */
    void ensure_resident(coord_type position) const;


    /** Ensure one layer is resident at position for value<LayerT>(). */
    template<Layer LayerT>
        requires (std::same_as<LayerT, Layers> || ...)
    void ensure_resident(coord_type position) const;


    /// Find a Placement for internal mutation.
    [[nodiscard]] placement_type*
    mutable_placement(PlacementId id) noexcept;


    /// Invalidate cached layer data covering one coordinate.
    void invalidate(coord_type position) const noexcept;


    /// Invalidate cached layer data intersecting an area.
    void invalidate(const area_type& area) const noexcept;


    /**
     * Invalidate all resident data.
     *
     * Cache's dirty-state/write-back contract still applies; invalidation must
     * never silently discard altered world state.
     */
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
     * Resident world data remains layer-oriented inside Cache. Cache is mutable
     * because logically-const Map reads may populate residency as an I/O
     * optimisation; that does not alter the logical world state.
     */
    mutable cache_type m_cache {};
};


} // namespace landor::geo

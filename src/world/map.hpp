#pragma once

#include "area.hpp"
#include "authored_layer_source.hpp"
#include "cache.hpp"
#include "layer.hpp"
#include "layer_fallback.hpp"
#include "layer_source.hpp"
#include "map_result.hpp"
#include "orientation.hpp"
#include "patch.hpp"
#include "placement.hpp"
#include "platform/storage/storage_filesystem.hpp"
#include "tile.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <span>


namespace landor::geo
{

using MapId = std::uint16_t;


/**
 * Failure outcomes of Map placement management.
 *
 * Placement creation is a Map-management operation, not checked world
 * access, so its failures do not share the MapError domain of
 * map_result.hpp. There are exactly two meaningful failures:
 *
 *   - UnknownPatch: the PatchId is not present in this Map's catalogue;
 *   - CapacityFull: every fixed placement slot is already occupied.
 */
enum class MapPlacementError : std::uint8_t
{
    UnknownPatch,
    CapacityFull
};


using MapPlacementResult =
    std::expected<PlacementId, MapPlacementError>;


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
 * the value from its terminal fallback provider (procedural baseline
 * generation or a fixed layer default, see layer_fallback.hpp), and later
 * simulation or magic may create live fire state at that coordinate.
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
 *     auto tile = map.at(position);
 *
 * and the access succeeds, Map ensures that the required layer data is
 * resident, then packs the value of each layer at that coordinate into a
 * Tile. The Tile also carries its coordinate as identity.
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
 * The checked outcome is the expected-based result MapResult (see
 * map_result.hpp): success carries the layer value or the complete Tile;
 * failure carries the exact MapError from the seam that failed. Map-local
 * failures are MapErrorCode::OutOfBounds and MapErrorCode::CacheFull; source
 * failures keep their exact lower-level domains, LayerSourceError and
 * AuthoredLayerSourceError.
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
 * Placement creation and mutation go through Map so cached layer data can
 * be invalidated correctly and Patch descriptors stay immutable.
 *
 * place() resolves the PatchId against the catalogue before allocating a
 * slot. A natural place() puts the occurrence at Patch::natural_position()
 * with the default Orientation; the explicit overload stores the supplied
 * position and orientation. Successful placements receive new stable
 * PlacementIds: they start at one, increase monotonically and are never
 * reused (no remove-placement operation exists). Public lookup is const-only;
 * mutable access stays private so spatial mutation cannot bypass Map.
 *
 * Current invalidation policy: transformed Patch coverage is not calculated
 * yet, so a successful placement creation or an actual placement transform
 * change invalidates the entire resident Cache (invalidate_all()). This is
 * deliberately conservative and safe with the current Cache, which has no
 * dirty state yet; once transformed coverage exists, Map can narrow
 * invalidation to the affected areas.
 *
 * Story systems do not belong here. They refer to PlacementIds through
 * world-facing Place objects; Map only deals with geometry and spatial state.
 *
 *
 * Overlap
 * -------
 *
 * When several Placements cover the same coordinate, resolution is performed
 * independently for each layer. A Placement which does not provide a
 * particular layer does not hide a lower contribution to that layer.
 *
 * The placement array preserves insertion order, but that order is not a
 * precedence rule. Authored precedence is deliberately kept in Map and is
 * pinned to stable Placement identity: a higher PlacementId has higher
 * precedence, so a later successful placement overlays an earlier one (see
 * DESIGN_DECISIONS.md, D-34). Patch describes authored content; Map decides
 * how placed content composes.
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
    typename FallbackT,
    typename CoordT = Coord32,
    Layer... Layers>
    requires LayerFallbackProvider<FallbackT, CoordT, Layers...>
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
    using fallback_type  = FallbackT;


    static_assert(
        MaxPlacements <= std::numeric_limits<PlacementId>::max(),
        "MaxPlacements cannot require more non-zero PlacementIds than "
        "PlacementId can represent");


    /**
     * Construct a live Map over an authored Patch catalogue.
     *
     * Patch descriptors, Storage and the terminal fallback provider outlive
     * the Map. Map borrows all three and owns none of them.
     *
     * The fallback provider is the final source of per-layer resolution. Map
     * queries it per layer and per in-Map world coordinate and receives
     * exactly LayerT::value_type; see layer_fallback.hpp for the contract.
     * value<LayerT>() reaches it as the last step of chunk-plane resolution;
     * no runtime/materialized override sits above the authored Placements
     * yet.
     */
    constexpr Map(
        MapId id,
        area_type area,
        std::span<const patch_type> patches,
        storage::Storage& storage,
        const fallback_type& fallback) noexcept
        : m_id(id),
          m_area(area),
          m_patches(patches),
          m_storage(storage),
          m_fallback(fallback)
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
     *
     * Failure carries the exact MapError from the seam that failed:
     * MapErrorCode::OutOfBounds for a coordinate outside this Map, the exact
     * LayerSourceError / AuthoredLayerSourceError from source resolution, or
     * MapErrorCode::CacheFull when the bounded Cache cannot accept the
     * required chunk.
     */
    template<Layer LayerT>
        requires (std::same_as<LayerT, Layers> || ...)
    [[nodiscard]] MapResult<typename LayerT::value_type>
    value(coord_type position) const
    {
        // Checked access: the bounds decision happens before any cache,
        // source or fallback work.
        if (!contains(position))
        {
            return std::unexpected(MapErrorCode::OutOfBounds);
        }

        const auto resident = ensure_resident<LayerT>(position);
        if (!resident)
        {
            return std::unexpected(resident.error());
        }

        // A resident hit never touches authored sources or the fallback.
        return m_cache.template value<LayerT>(position);
    }


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
     *
     * Failure carries the exact MapError from the seam that failed:
     * MapErrorCode::OutOfBounds for a coordinate outside this Map,
     * MapErrorCode::CacheFull when the bounded Cache cannot accept a required
     * chunk, or the exact lower-level source error from resolution.
     */
    [[nodiscard]] MapResult<tile_type> at(coord_type position) const;


    /** Convenience checked access from scalar coordinates. */
    [[nodiscard]] MapResult<tile_type> at(scalar_type x, scalar_type y) const
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
     * The occurrence is created at `patch.natural_position()` with the
     * default `Orientation{}`, delegating to the explicit overload.
     *
     * Returns MapPlacementError::UnknownPatch when the PatchId is absent
     * from this Map's catalogue, and MapPlacementError::CapacityFull when
     * every placement slot is occupied. UnknownPatch is checked before
     * capacity, so an invalid PatchId is never hidden by a full Map.
     */
    [[nodiscard]] MapPlacementResult
    place(PatchId patch_id) noexcept
    {
        const auto* descriptor = patch(patch_id);

        if (descriptor == nullptr)
            return std::unexpected(MapPlacementError::UnknownPatch);

        return place(patch_id, descriptor->natural_position());
    }


    /**
     * Place a Patch at an explicit position and orientation.
     *
     * This is the primitive used by PatchSet composition. Reusing an authored
     * Patch does not duplicate its layer data.
     *
     * On success a new live Placement occupies one fixed slot and the result
     * carries its new stable PlacementId. Map then invalidates its currently
     * resident data under the current conservative whole-cache policy (see
     * the class documentation).
     *
     * Returns MapPlacementError::UnknownPatch when the PatchId is absent
     * from this Map's catalogue, checked before capacity so it is never
     * hidden by a full Map, or MapPlacementError::CapacityFull when every
     * placement slot is already occupied.
     */
    [[nodiscard]] MapPlacementResult
    place(
        PatchId patch_id,
        coord_type position,
        Orientation orientation = {}) noexcept
    {
        if (patch(patch_id) == nullptr)
            return std::unexpected(MapPlacementError::UnknownPatch);

        std::optional<placement_type>* slot = nullptr;
        for (auto& entry : m_placements)
        {
            if (!entry.has_value())
            {
                slot = &entry;
                break;
            }
        }

        if (slot == nullptr)
            return std::unexpected(MapPlacementError::CapacityFull);

        const PlacementId id = m_next_placement_id;
        ++m_next_placement_id;
        slot->emplace(id, patch_id, position, orientation);
        ++m_placement_count;

        // Current conservative policy: transformed coverage is not computed,
        // so a new placement may affect any previously resident area.
        invalidate_all();

        return id;
    }


    /**
     * Return a live Placement by identity.
     *
     * Only const access is exposed. Spatial mutation must go through Map so
     * cache invalidation cannot be bypassed accidentally.
     */
    [[nodiscard]] const placement_type*
    placement(PlacementId id) const noexcept
    {
        for (const auto& entry : m_placements)
        {
            if (entry.has_value() && entry->id() == id)
                return &(*entry);
        }

        return nullptr;
    }


    /// Number of currently live Placements.
    [[nodiscard]] constexpr std::size_t placement_count() const noexcept
    {
        return m_placement_count;
    }


    /**
     * Move a Placement to a new position.
     *
     * The authored Patch remains unchanged.
     *
     * Returns false for an unknown PlacementId. When the requested position
     * already equals the current position the call returns true without
     * invalidating; an actual move mutates the Placement and invalidates
     * resident Map data.
     */
    bool set_position(
        PlacementId id,
        coord_type position) noexcept
    {
        placement_type* target = mutable_placement(id);
        if (target == nullptr)
            return false;

        if (target->position() == position)
            return true;

        target->set_position(position);
        invalidate_all();
        return true;
    }


    /**
     * Rotate a Placement.
     *
     * Rotation affects its contribution to every layer supplied by its
     * Patch.
     *
     * Returns false for an unknown PlacementId. A change to the current
     * rotation mutates the Placement and invalidates resident Map data;
     * requesting the rotation it already has returns true without
     * invalidating.
     */
    bool set_rotation(
        PlacementId id,
        Rotation rotation) noexcept
    {
        placement_type* target = mutable_placement(id);
        if (target == nullptr)
            return false;

        if (target->rotation() == rotation)
            return true;

        target->set_rotation(rotation);
        invalidate_all();
        return true;
    }


    /**
     * Reflect a Placement.
     *
     * Reflection is applied before rotation, as defined by Orientation.
     *
     * Returns false for an unknown PlacementId. A change to the current
     * reflection mutates the Placement and invalidates resident Map data;
     * requesting the reflection it already has returns true without
     * invalidating.
     */
    bool set_reflection(
        PlacementId id,
        Reflection reflection) noexcept
    {
        placement_type* target = mutable_placement(id);
        if (target == nullptr)
            return false;

        if (target->reflection() == reflection)
            return true;

        target->set_reflection(reflection);
        invalidate_all();
        return true;
    }


    /**
     * Replace a Placement's complete orientation.
     *
     * Returns false for an unknown PlacementId. A change to the current
     * orientation mutates the Placement and invalidates resident Map data;
     * requesting the orientation it already has returns true without
     * invalidating.
     */
    bool set_orientation(
        PlacementId id,
        Orientation orientation) noexcept
    {
        placement_type* target = mutable_placement(id);
        if (target == nullptr)
            return false;

        // Orientation equality is its two component fields, compared directly.
        const Orientation current = target->orientation();
        if (current.rotation == orientation.rotation
            && current.reflection == orientation.reflection)
        {
            return true;
        }

        target->set_orientation(orientation);
        invalidate_all();
        return true;
    }


private:
    /**
     * Resolve one complete canonical layer plane for one canonical Chunk.
     *
     * This is the Map's source-resolution seam, and it operates at chunk
     * granularity rather than point granularity: a cache miss requires a
     * complete canonical layer plane, and a point resolver would reopen the
     * same .layer metadata over and over.
     *
     * On success the plane receives exactly CacheChunkSide * CacheChunkSide
     * values of LayerT in row-major plane order (x fastest). The operation
     * performs no heap allocation.
     *
     * Every in-Map cell resolves through this order:
     *
     *     authored Placements, highest PlacementId first
     *         -> terminal fallback provider
     *
     * Authored precedence follows stable Placement identity rather than
     * array slot order: a higher PlacementId has higher precedence, so a
     * later successful placement overlays an earlier one (D-34). A Placement
     * contributes to a cell only when its Patch binds LayerT, the world
     * coordinate inverse-transforms into the Patch's local area
     * (world_to_local nullopt means "does not contribute there", not an
     * error), and the authored cell carries a contribution (not ASCII space
     * 0x20). Otherwise the cell stays unresolved and resolution continues
     * downward.
     *
     * Each Placement considered for the Chunk is resolved against its Patch,
     * checked for a LayerT binding, and inspected for coverage of the
     * still-unresolved in-Map cells before its source is opened; the source
     * is then opened once, validated once against the Patch, and read cell by
     * cell through the streaming reader. A Placement whose relevant cells are
     * all already resolved is never opened, so a completely hidden lower
     * source cannot fail the access.
     *
     * A canonical Chunk can extend outside Map::area(). Those plane cells are
     * cache padding, not logical world positions: the fallback provider is
     * never queried for them, no authored source is consulted, and their
     * values remain value-initialized. Public checked access can never expose
     * them.
     *
     * World coordinates are derived from chunk.origin() plus the local x/y in
     * a wide signed intermediate, so cells near the coordinate limits are
     * padding rather than wrapped coordinates.
     *
     * Failure carries the exact lower-level error that ended resolution: the
     * exact LayerSourceError from opening or reading one .layer source, or
     * the exact AuthoredLayerSourceError from a Patch/source geometry
     * disagreement. The terminal fallback provider itself never fails.
     */
    template<Layer LayerT>
        requires (std::same_as<LayerT, Layers> || ...)
    [[nodiscard]] MapResult<void>
    resolve_chunk(
        const typename cache_type::chunk_type& chunk,
        std::span<typename LayerT::value_type> values) const
    {
        constexpr std::size_t side = CacheChunkSide;
        constexpr std::size_t plane_cells = side * side;

        // Classify each plane cell as a logical in-Map position or as cache
        // padding. Wide signed intermediates: a cell whose world coordinate
        // no longer fits the scalar type is padding, never a wrapped
        // coordinate.
        std::array<bool, plane_cells> in_map {};
        std::array<bool, plane_cells> resolved {};
        std::size_t unresolved = 0;

        for (std::size_t y = 0; y < side; ++y)
        {
            for (std::size_t x = 0; x < side; ++x)
            {
                const auto world = chunk_cell_world(chunk, x, y);
                if (world && m_area.contains(*world))
                {
                    in_map[y * side + x] = true;
                    ++unresolved;
                }
            }
        }

        // Collect the live Placements into a fixed pointer array and sort the
        // occupied prefix by descending PlacementId. The rule follows stable
        // identity, not array slot order, and needs no dynamic container.
        std::array<const placement_type*, MaxPlacements> ordered {};
        std::size_t ordered_count = 0;
        for (const auto& entry : m_placements)
        {
            if (entry.has_value())
            {
                ordered[ordered_count++] = &(*entry);
            }
        }

        std::sort(
            ordered.begin(),
            ordered.begin() + ordered_count,
            [](const placement_type* a, const placement_type* b)
            { return a->id() > b->id(); });

        std::array<bool, plane_cells> candidate {};
        std::array<std::uint32_t, plane_cells> local_x {};
        std::array<std::uint32_t, plane_cells> local_y {};
        std::byte cell {};

        for (std::size_t i = 0; i < ordered_count && unresolved > 0; ++i)
        {
            const auto* placement = ordered[i];

            // Placements are created through place(), which resolves the
            // PatchId against the immutable borrowed catalogue, so every
            // live Placement always resolves there.
            const auto* descriptor = patch(placement->patch());
            assert(descriptor != nullptr);

            const auto* binding = descriptor->template binding<LayerT>();
            if (binding == nullptr)
            {
                continue;
            }

            // Mark the still-unresolved in-Map cells this Placement can
            // contribute to: the world coordinate must inverse-transform
            // into the Patch's local area.
            candidate.fill(false);
            bool has_candidate = false;

            for (std::size_t y = 0; y < side; ++y)
            {
                for (std::size_t x = 0; x < side; ++x)
                {
                    const std::size_t index = y * side + x;
                    if (!in_map[index] || resolved[index])
                    {
                        continue;
                    }

                    const auto world = chunk_cell_world(chunk, x, y);
                    if (!world)
                    {
                        continue;
                    }

                    const auto local = placement->world_to_local(*world);
                    if (!local || !descriptor->local_area().contains(*local))
                    {
                        continue;
                    }

                    // Dense v1 sources anchor the local area at (0, 0), so a
                    // contained local coordinate is non-negative and converts
                    // explicitly into the reader's unsigned cell addressing.
                    local_x[index] = static_cast<std::uint32_t>(local->x());
                    local_y[index] = static_cast<std::uint32_t>(local->y());
                    candidate[index] = true;
                    has_candidate = true;
                }
            }

            if (!has_candidate)
            {
                // Completely hidden for this Chunk: the source is never
                // opened, so a broken lower source cannot fail the access.
                continue;
            }

            // The source is opened and geometry-validated once per Placement,
            // not once per cell.
            const auto opened = open_layer_source(m_storage, binding->source);
            if (!opened)
            {
                return std::unexpected(opened.error());
            }

            const auto validated = validate_authored_layer_source(
                *descriptor, *opened);
            if (!validated)
            {
                return std::unexpected(validated.error());
            }

            for (std::size_t y = 0; y < side; ++y)
            {
                for (std::size_t x = 0; x < side; ++x)
                {
                    const std::size_t index = y * side + x;
                    if (!candidate[index] || resolved[index])
                    {
                        continue;
                    }

                    // One-cell reads through the reader keep the touched-row
                    // structural validation; orientation-specific bulk reads
                    // are a later slice.
                    const auto read = read_cells(
                        *opened,
                        m_storage,
                        binding->source,
                        local_y[index],
                        local_x[index],
                        std::span<std::byte>(&cell, 1));
                    if (!read)
                    {
                        return std::unexpected(read.error());
                    }

                    if (has_contribution(cell))
                    {
                        values[index] = static_cast<
                            typename LayerT::value_type>(
                            std::to_integer<std::uint8_t>(cell));
                        resolved[index] = true;
                        --unresolved;
                    }
                    // A 0x20 authored cell contributes nothing; lower
                    // Placements may still resolve this cell.
                }
            }
        }

        // Terminal fallback: the final, always-answerable source for every
        // still-unresolved in-Map cell. Cache padding outside the Map is
        // never queried and stays value-initialized.
        for (std::size_t y = 0; y < side; ++y)
        {
            for (std::size_t x = 0; x < side; ++x)
            {
                const std::size_t index = y * side + x;
                if (!in_map[index] || resolved[index])
                {
                    continue;
                }

                const auto world = chunk_cell_world(chunk, x, y);
                assert(world.has_value());

                values[index] = m_fallback.template value<LayerT>(*world);
                resolved[index] = true;
                --unresolved;
            }
        }

        assert(unresolved == 0);
        return {};
    }


    /**
     * Ensure that every layer required for a complete Tile is resident at
     * position.
     *
     * The implementation promotes the request to CacheChunkSide-aligned
     * Chunks and fills missing layer Chunks through resolve_chunk()/Storage.
     *
     * Fails with MapErrorCode::CacheFull when a required chunk would need a
     * spatial slot the bounded Cache cannot provide; source resolution
     * failures surface as their exact lower-level errors.
     *
     * Pending: this form serves the not-yet-implemented Map::at().
     */
    [[nodiscard]] MapResult<void> ensure_resident(coord_type position) const;


    /**
     * Ensure one layer is resident at position for value<LayerT>().
     *
     * A resident hit returns immediately without touching authored sources
     * or the fallback. Otherwise the canonical Chunk containing position is
     * resolved into a fixed temporary plane and installed through
     * Cache::fill<LayerT>().
     *
     * Fails with MapErrorCode::CacheFull when the bounded Cache cannot accept
     * the Chunk's spatial slot, or with the exact lower-level source error
     * from resolve_chunk().
     */
    template<Layer LayerT>
        requires (std::same_as<LayerT, Layers> || ...)
    [[nodiscard]] MapResult<void>
    ensure_resident(coord_type position) const
    {
        if (m_cache.template contains<LayerT>(position))
        {
            return {};
        }

        const auto chunk = m_cache.template chunk_for<LayerT>(position);

        std::array<
            typename LayerT::value_type,
            CacheChunkSide * CacheChunkSide>
            values {};

        const auto resolved = resolve_chunk<LayerT>(chunk, values);
        if (!resolved)
        {
            return std::unexpected(resolved.error());
        }

        if (!m_cache.template fill<LayerT>(chunk, values))
        {
            return std::unexpected(MapErrorCode::CacheFull);
        }

        return {};
    }


    /**
     * True when a wide coordinate component still fits in the Map's scalar
     * type.
     */
    [[nodiscard]] static constexpr bool
    fits_coord_component(std::int64_t value) noexcept
    {
        return value >= static_cast<std::int64_t>(
                           std::numeric_limits<scalar_type>::min())
            && value <= static_cast<std::int64_t>(
                           std::numeric_limits<scalar_type>::max());
    }


    /**
     * One Chunk-local plane position as a world coordinate.
     *
     * The origin offset is taken in a wide signed intermediate, so a plane
     * cell near the coordinate limits is padding (nullopt) rather than a
     * wrapped coordinate.
     */
    [[nodiscard]] static constexpr std::optional<coord_type>
    chunk_cell_world(
        const typename cache_type::chunk_type& chunk,
        std::size_t x,
        std::size_t y) noexcept
    {
        const std::int64_t wide_x =
            static_cast<std::int64_t>(chunk.origin().x())
            + static_cast<std::int64_t>(x);
        const std::int64_t wide_y =
            static_cast<std::int64_t>(chunk.origin().y())
            + static_cast<std::int64_t>(y);

        if (!fits_coord_component(wide_x) || !fits_coord_component(wide_y))
        {
            return std::nullopt;
        }

        return coord_type {
            static_cast<scalar_type>(wide_x),
            static_cast<scalar_type>(wide_y)
        };
    }


    /**
     * Find a Placement for internal mutation.
     *
     * Returns nullptr for an unknown PlacementId. Mutable Placement access
     * is never exposed publicly.
     */
    [[nodiscard]] placement_type*
    mutable_placement(PlacementId id) noexcept
    {
        for (auto& entry : m_placements)
        {
            if (entry.has_value() && entry->id() == id)
                return &(*entry);
        }

        return nullptr;
    }


    /// Invalidate cached layer data covering one coordinate.
    void invalidate(coord_type position) const noexcept
    {
        m_cache.invalidate(area_type {position, position});
    }


    /// Invalidate cached layer data intersecting an area.
    void invalidate(const area_type& area) const noexcept
    {
        m_cache.invalidate(area);
    }


    /**
     * Invalidate all resident data.
     *
     * The current Cache has no dirty state, so dropping every resident slot
     * cannot discard altered world state. The placement lifecycle uses this
     * conservatively today; once transformed coverage and a write-back
     * policy exist, placement changes must invalidate only the affected
     * areas.
     */
    void invalidate_all() const noexcept
    {
        m_cache.invalidate_all();
    }


    MapId m_id;
    area_type m_area;

    /*
     * Patch descriptors are authored content and are shared. Map keeps only a
     * view of the catalogue.
     */
    std::span<const patch_type> m_patches;

    /* Storage owns the persistent backing data. */
    storage::Storage& m_storage;

    /*
     * Terminal fallback provider: the final source of per-layer resolution,
     * covering procedural baseline generation or a fixed layer default behind
     * one small operation. Borrowed; the provider outlives the Map.
     */
    const fallback_type& m_fallback;

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

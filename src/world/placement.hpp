#pragma once

#include "coord.hpp"
#include "orientation.hpp"
#include "patch.hpp"

#include <cstdint>
#include <limits>
#include <optional>
#include <utility>


namespace landor::geo
{

using PlacementId = std::uint16_t;


/**
 * One live occurrence of an authored Patch.
 *
 * Placement gives reusable authored content a position and orientation in a
 * Map. It contains no authored layer data itself; that remains in the Patch.
 *
 * A useful example is a player's home.
 *
 * The game may have several reusable authored house patches, barn patches,
 * wells, fields and unique features. A home generator chooses among them and
 * places the selected pieces around the player's home coordinates. The same
 * authored house patch may therefore appear in many homes, each at a different
 * position and perhaps rotated or reflected differently.
 *
 * For example:
 *
 *     House03
 *
 * may be authored once, but appear as:
 *
 *     home A: position (120, 80), rotation 90 degrees
 *     home B: position (600, 310), reflected on X
 *
 * Both placements refer to the same PatchId and therefore to the same authored
 * layer data. Only their PlacementIds and transforms differ.
 *
 * This distinction also matters when a placement changes while the game is
 * running. Moving or rotating an occurrence changes where its Patch contributes
 * to the Map without rewriting or duplicating the authored Patch.
 *
 * Several Placement objects may refer to the same Patch.
 *
 * Placement is deliberately unaware of story identity. A Place may refer to
 * one or more PlacementIds, but geography does not need to know what the
 * story calls a location. A collection of placements such as house, barn,
 * well, fields and a unique feature may collectively form one story-facing
 * Place: the player's home.
 *
 * Changing a Placement changes where its Patch contributes to the Map. The
 * owning Map is responsible for invalidating any cached spatial results
 * affected by such a change.
 *
 * Patch-local `(0, 0)` is the transform anchor. `position()` is the Map
 * coordinate at which that local origin is placed. Patch-local coordinates
 * are transformed in this order:
 *
 *     reflection -> rotation -> translation
 *
 * Conceptually:
 *
 *     world = position + rotate(reflect(local))
 *
 * There is no post-transform renormalisation to keep the oriented Patch on the
 * positive side of `position`; rotated/reflected local coordinates may be
 * negative relative to the anchor.
 *
 * That contract is implemented pointwise by `local_to_world()` and
 * `world_to_local()`. Both perform reflection, rotation, and
 * translation/subtraction in a wide signed intermediate, so narrow coordinate
 * types never overflow; a result that no longer fits in the coordinate type is
 * reported as `std::nullopt` rather than wrapped, clamped or saturated. Patch
 * bounds checking is deliberately not part of the transform; a caller that
 * needs it uses `patch.local_area().contains(local)` separately.
 *
 * Placement owns only its transform and identities. It does not own the Patch
 * and contains no pointers or references into map storage.
 */
template<typename CoordT = Coord32>
class Placement
{
public:
    using coord_type = CoordT;


    constexpr Placement(
        PlacementId id,
        PatchId patch,
        coord_type position,
        Orientation orientation = {}) noexcept
        : m_id(id),
          m_patch(patch),
          m_position(position),
          m_orientation(orientation)
    {
    }


    /// Stable identity of this placed occurrence.
    [[nodiscard]] constexpr PlacementId id() const noexcept
    {
        return m_id;
    }


    /// Authored Patch used by this occurrence.
    [[nodiscard]] constexpr PatchId patch() const noexcept
    {
        return m_patch;
    }


    /// Map coordinate of this occurrence's Patch-local origin `(0, 0)`.
    [[nodiscard]] constexpr coord_type position() const noexcept
    {
        return m_position;
    }


    /// Current orientation relative to the authored Patch.
    [[nodiscard]] constexpr Orientation orientation() const noexcept
    {
        return m_orientation;
    }


    /// Current clockwise rotation relative to the authored Patch.
    [[nodiscard]] constexpr Rotation rotation() const noexcept
    {
        return m_orientation.rotation;
    }


    /// Current reflection relative to the authored Patch.
    [[nodiscard]] constexpr Reflection reflection() const noexcept
    {
        return m_orientation.reflection;
    }


    /**
     * Transform one Patch-local coordinate into a Map/world coordinate.
     *
     * Implements the pinned contract exactly:
     *
     *     world = position + rotate(reflect(local))
     *
     * Reflection and rotation operate about Patch-local `(0, 0)`, so the
     * local origin always maps exactly to `position()` and nothing is
     * renormalised afterwards; the result may lie below `position()` on
     * either axis. No `Patch` object is needed: this is pure geometry and
     * does not check that `local` lies inside `patch.local_area()`.
     *
     * Returns nullopt when the mathematical result does not fit in
     * `coord_type::scalar_type`; the result is never wrapped, clamped or
     * saturated.
     */
    [[nodiscard]] constexpr std::optional<coord_type>
    local_to_world(coord_type local) const noexcept
    {
        wide_component x = local.x();
        wide_component y = local.y();

        auto [rx, ry] = reflect_components(m_orientation.reflection, x, y);
        auto [px, py] = rotate_components_clockwise(m_orientation.rotation, rx, ry);

        const wide_component world_x = px + static_cast<wide_component>(m_position.x());
        const wide_component world_y = py + static_cast<wide_component>(m_position.y());

        if (!fits_scalar(world_x) || !fits_scalar(world_y))
            return std::nullopt;

        return coord_type(
            static_cast<scalar_type>(world_x),
            static_cast<scalar_type>(world_y));
    }


    /**
     * Inverse of `local_to_world()`.
     *
     *     local = reflect(inverse_rotate(world - position))
     *
     * The inverse rotation (r90 undone by r270, r270 undone by r90) is undone
     * before the reflection, which is self-inverse. Returns nullopt when the
     * mathematical local coordinate does not fit in `coord_type`.
     */
    [[nodiscard]] constexpr std::optional<coord_type>
    world_to_local(coord_type world) const noexcept
    {
        const wide_component x =
            static_cast<wide_component>(world.x()) - static_cast<wide_component>(m_position.x());
        const wide_component y =
            static_cast<wide_component>(world.y()) - static_cast<wide_component>(m_position.y());

        auto [ux, uy] = undo_rotation_components(m_orientation.rotation, x, y);
        auto [lx, ly] = reflect_components(m_orientation.reflection, ux, uy);

        if (!fits_scalar(lx) || !fits_scalar(ly))
            return std::nullopt;

        return coord_type(
            static_cast<scalar_type>(lx),
            static_cast<scalar_type>(ly));
    }


    /**
     * Move this occurrence without modifying its authored Patch.
     *
     * The owning Map must invalidate any cached spatial results affected by
     * both the old and new positions.
     */
    constexpr void set_position(coord_type position) noexcept
    {
        m_position = position;
    }


    /**
     * Change the rotation of this occurrence.
     *
     * Rotation changes only how the authored Patch is mapped into the Map.
     * The Patch and its stored layer data remain unchanged.
     *
     * The owning Map must invalidate any cached spatial results affected by
     * the old and new orientations.
     */
    constexpr void set_rotation(Rotation rotation) noexcept
    {
        m_orientation.rotation = rotation;
    }


    /**
     * Change the reflection of this occurrence.
     *
     * Reflection is applied before rotation.
     *
     * The owning Map must invalidate any cached spatial results affected by
     * the old and new orientations.
     */
    constexpr void set_reflection(Reflection reflection) noexcept
    {
        m_orientation.reflection = reflection;
    }


    /**
     * Replace the complete orientation of this occurrence.
     *
     * The owning Map must invalidate any cached spatial results affected by
     * the old and new orientations.
     */
    constexpr void set_orientation(Orientation orientation) noexcept
    {
        m_orientation = orientation;
    }


private:
    /// Scalar type of the coordinate this placement is expressed in.
    using scalar_type = typename coord_type::scalar_type;

    /// Wide signed intermediate for transform arithmetic. The supported
    /// coordinate widths (int8_t/int16_t/int32_t) cannot overflow it, and
    /// negating an extreme value such as the minimum scalar stays
    /// representable in it.
    using wide_component = std::int64_t;


    /**
     * Reflect a wide (x, y) pair about the local origin, using the axes
     * documented in `orientation.hpp`.
     *
     * Reflection is self-inverse, so the same helper serves both transform
     * directions.
     */
    [[nodiscard]] static constexpr std::pair<wide_component, wide_component>
    reflect_components(Reflection reflection, wide_component x, wide_component y) noexcept
    {
        switch (reflection)
        {
        case Reflection::none: return {x,  y};
        case Reflection::x:    return {x, -y};
        case Reflection::y:    return {-x, y};
        case Reflection::xy:   return {-x, -y};
        }
        return {x, y};  // unreachable — all enumerators handled above
    }


    /**
     * Rotate a wide (x, y) pair clockwise about the local origin, matching
     * the `Coord::rotate_90cw()` convention.
     */
    [[nodiscard]] static constexpr std::pair<wide_component, wide_component>
    rotate_components_clockwise(Rotation rotation, wide_component x, wide_component y) noexcept
    {
        switch (rotation)
        {
        case Rotation::none: return {x,  y};
        case Rotation::r90:  return {y, -x};
        case Rotation::r180: return {-x, -y};
        case Rotation::r270: return {-y, x};
        }
        return {x, y};  // unreachable — all enumerators handled above
    }


    /**
     * Inverse of `rotate_components_clockwise()`:
     * r90 is undone by r270 and r270 is undone by r90; r180 is self-inverse.
     */
    [[nodiscard]] static constexpr std::pair<wide_component, wide_component>
    undo_rotation_components(Rotation rotation, wide_component x, wide_component y) noexcept
    {
        switch (rotation)
        {
        case Rotation::none: return {x,  y};
        case Rotation::r90:  return {-y, x};
        case Rotation::r180: return {-x, -y};
        case Rotation::r270: return {y, -x};
        }
        return {x, y};  // unreachable — all enumerators handled above
    }


    /**
     * True when a wide component still fits in the coordinate scalar type.
     */
    [[nodiscard]] static constexpr bool fits_scalar(wide_component value) noexcept
    {
        return value >= static_cast<wide_component>(std::numeric_limits<scalar_type>::min())
            && value <= static_cast<wide_component>(std::numeric_limits<scalar_type>::max());
    }


    PlacementId m_id;
    PatchId      m_patch;
    coord_type   m_position;
    Orientation  m_orientation;
};


} // namespace landor::geo
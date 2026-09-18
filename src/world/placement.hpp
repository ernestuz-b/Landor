#pragma once

#include "coord.hpp"
#include "orientation.hpp"
#include "patch.hpp"

#include <cstdint>


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
    PlacementId m_id;
    PatchId      m_patch;
    coord_type   m_position;
    Orientation  m_orientation;
};


} // namespace landor::geo
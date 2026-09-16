#pragma once

#include "placement.hpp"

#include <cstdint>
#include <span>
#include <string_view>


namespace landor::world
{

using PlaceId = std::uint16_t;


/**
 * A place known by the people and stories of the world.
 *
 * Place is a narrative identity, not authored geometry.
 *
 * Actors, missions, schedules, ownership and dialogue refer to PlaceId rather
 * than to PatchId or PlacementId. They care about "GoodMagePalace", not which
 * reusable patch was used to build it or how that patch is oriented.
 *
 * A Place may consist of one or more Placements. For example:
 *
 *     GoodMagePalace
 *         palace exterior
 *         garden
 *         courtyard
 *
 * Geometry may move or change without changing the identity of the Place.
 *
 * Place owns no map data and performs no spatial queries. Map is responsible
 * for resolving its PlacementIds into geometry.
 */
class Place
{
public:
    constexpr Place(
        PlaceId id,
        std::string_view name,
        std::span<const geo::PlacementId> placements) noexcept
        : m_id(id),
          m_name(name),
          m_placements(placements)
    {
    }


    /// Stable story identity of this place.
    [[nodiscard]] constexpr PlaceId id() const noexcept
    {
        return m_id;
    }


    /**
     * Human-readable name of this place.
     *
     * The name is not its identity. PlaceId remains stable if displayed text
     * is changed, translated or otherwise presented differently.
     */
    [[nodiscard]] constexpr std::string_view name() const noexcept
    {
        return m_name;
    }


    /**
     * Physical occurrences which currently make up this place.
     *
     * A Place may contain more than one Placement, but usually contains only
     * a small number. The order carries no story meaning.
     */
    [[nodiscard]] constexpr std::span<const geo::PlacementId>
    placements() const noexcept
    {
        return m_placements;
    }


    [[nodiscard]] constexpr bool
    contains(geo::PlacementId placement) const noexcept
    {
        for (const auto id : m_placements)
        {
            if (id == placement)
                return true;
        }

        return false;
    }


private:
    PlaceId                           m_id;
    std::string_view                  m_name;
    std::span<const geo::PlacementId> m_placements;
};


} // namespace landor::world

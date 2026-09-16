#pragma once

#include "patch.hpp"

#include <cstdint>
#include <span>
#include <string_view>


namespace landor::geo
{

using PatchSetId  = std::uint16_t;
using PatchRoleId = std::uint16_t;


/**
 * One role in a PatchSet composition recipe.
 *
 * A role describes something the composed area should contain and provides
 * the authored Patches from which that thing may be chosen.
 *
 * For a player's home, roles might represent:
 *
 *     house
 *     well
 *     barn
 *     field
 *     unique feature
 *
 * The role itself does not choose a Patch, position it or orient it. That is
 * the composer's job. PatchSet only describes the available authored pieces.
 *
 * min_count and max_count allow the same mechanism to describe required,
 * optional and repeated elements:
 *
 *     1..1    exactly one
 *     0..1    optional
 *     2..4    several
 *
 * Candidate order carries no selection priority. Selection must be explicit
 * and deterministic; array order must not accidentally become game policy.
 */
struct PatchSetRole
{
    PatchRoleId             role;
    std::span<const PatchId> candidates;

    std::uint8_t min_count = 1;
    std::uint8_t max_count = 1;
};


/**
 * An immutable recipe for composing a group of reusable authored Patches.
 *
 * PatchSet exists because many useful areas of the world are neither one
 * authored Patch nor wholly procedural.
 *
 * A player's home is the motivating example. The game can author several
 * houses, wells, barns, fields and unusual features independently, then define
 * a Home PatchSet containing roles for those pieces.
 *
 * When a home is needed, a composer combines:
 *
 *     PatchSet
 *     + world seed
 *     + home coordinates
 *
 * to choose Patches and produce Placements. The coordinates may also influence
 * their positions, rotations and reflections, so homes assembled from the same
 * recipe need not look alike.
 *
 * For example, the same Home PatchSet might produce:
 *
 *     home A
 *         House03, rotated 90 degrees
 *         Well01
 *         Barn02, reflected
 *         Field04
 *         AncientOak
 *
 *     home B
 *         House01
 *         Well03, reflected
 *         Barn01, rotated 180 degrees
 *         Field02
 *         SmallPond
 *
 * The PatchSet owns none of those Placements. It is only the recipe from which
 * they are derived.
 *
 * Given the same world seed, coordinates and PatchSet, composition must produce
 * the same result. This allows every multiplayer participant to reconstruct
 * the same untouched world without exchanging its generated baseline.
 *
 * Only changes away from that baseline are world state: a burnt barn, moved
 * object, cultivated field, new building and similar alterations.
 *
 * PatchSet contains no procedural-generation algorithm itself. The composition
 * policy is kept separate so the same authored recipe can be composed
 * differently by different games or ports without changing the Patch data.
 */
class PatchSet
{
public:
    constexpr PatchSet(
        PatchSetId id,
        std::string_view name,
        std::span<const PatchSetRole> roles) noexcept
        : m_id(id),
          m_name(name),
          m_roles(roles)
    {
    }


    /// Stable identity of this authored composition recipe.
    [[nodiscard]] constexpr PatchSetId id() const noexcept
    {
        return m_id;
    }


    /// Human-readable authored name of the recipe.
    [[nodiscard]] constexpr std::string_view name() const noexcept
    {
        return m_name;
    }


    /// Roles from which the composer builds an instance of this PatchSet.
    [[nodiscard]] constexpr std::span<const PatchSetRole> roles() const noexcept
    {
        return m_roles;
    }


    /**
     * Find a role in this recipe.
     *
     * The number of roles is expected to remain small, so linear lookup is
     * intentional.
     */
    [[nodiscard]] constexpr const PatchSetRole*
    role(PatchRoleId id) const noexcept
    {
        for (const auto& role : m_roles)
        {
            if (role.role == id)
                return &role;
        }

        return nullptr;
    }


private:
    PatchSetId                   m_id;
    std::string_view             m_name;
    std::span<const PatchSetRole> m_roles;
};


} // namespace landor::geo
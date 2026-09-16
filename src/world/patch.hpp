#pragma once

#include "area.hpp"
#include "layer.hpp"
#include "../storage/types.hpp"

#include <cstdint>
#include <span>
#include <string_view>


namespace landor::geo
{

using PatchId       = std::uint16_t;


/**
 * Connects one layer provided by an authored Patch with its backing source.
 *
 * The source is only an identity. Patch does not know whether the data comes
 * from a host file, an SD card, ROM or some other storage implementation.
 */
struct LayerBinding
{
    LayerId           layer;
    storage::SourceId source;
};


/**
 * An immutable authored region of a Map.
 *
 * A Patch describes reusable authored content. It has an identity, a name,
 * a natural position in the world and a rectangular local area.
 *
 * A patch may provide data for any subset of the layers understood by the
 * game. All provided layers share the same geometry and natural placement,
 * but remain independently stored.
 *
 * For example, an underwater patch may provide Terrain, Elevation and Water
 * without providing Fire. This does not mean that fire cannot exist there;
 * missing layer data is resolved by the Map through the normal fallback or
 * default machinery.
 *
 * Patch does not own layer data. LayerBinding records merely identify the
 * backing sources from which the Map can obtain authored values.
 *
 * Patch also has no live placement state. Moving, rotating or reflecting an
 * occurrence of a patch is the responsibility of the placed instance.
 */
template<typename CoordT = Coord32>
class Patch
{
public:
    using coord_type = CoordT;
    using area_type  = Area<CoordT>;


    constexpr Patch(
        PatchId id,
        std::string_view name,
        coord_type natural_position,
        area_type local_area,
        std::span<const LayerBinding> layers) noexcept
        : m_id(id),
          m_name(name),
          m_natural_position(natural_position),
          m_local_area(local_area),
          m_layers(layers)
    {
    }


    /// Stable identity of the authored patch.
    [[nodiscard]] constexpr PatchId id() const noexcept
    {
        return m_id;
    }


    /**
     * Authored name of this reusable patch.
     *
     * This is not the story name of a placed occurrence. The same authored
     * patch may be reused for several differently named places.
     */
    [[nodiscard]] constexpr std::string_view name() const noexcept
    {
        return m_name;
    }


    /// Position where the author intended this patch to be placed.
    [[nodiscard]] constexpr coord_type natural_position() const noexcept
    {
        return m_natural_position;
    }


    /// Area covered by the authored data in patch-local coordinates.
    [[nodiscard]] constexpr const area_type& local_area() const noexcept
    {
        return m_local_area;
    }


    /// Layer sources explicitly provided by this patch.
    [[nodiscard]] constexpr std::span<const LayerBinding> layers() const noexcept
    {
        return m_layers;
    }


    /**
     * Find the authored source supplied for a layer.
     *
     * Returns nullptr when this Patch contains no authored data for the layer.
     * Absence does not mean that the property cannot exist there; Map may resolve
     * it through fallback, procedural generation or live state.
     *
     * The number of layers in a patch is expected to be small, so the lookup
     * is intentionally linear. Do not add indexing machinery without a
     * measured reason.
     */
    [[nodiscard]] constexpr const LayerBinding*
    binding(LayerId layer) const noexcept
    {
        for (const auto& entry : m_layers)
        {
            if (entry.layer == layer)
                return &entry;
        }

        return nullptr;
    }


    template<Layer LayerT>
    [[nodiscard]] constexpr const LayerBinding*
    binding() const noexcept
    {
        return binding(LayerT::id);
    }


    /**
     * Returns whether this patch provides authored data for a layer.
     *
     * Absence says nothing about whether the layer exists in the game or may
     * acquire live state later.
     */
    [[nodiscard]] constexpr bool provides(LayerId layer) const noexcept
    {
        return binding(layer) != nullptr;
    }


    template<Layer LayerT>
    [[nodiscard]] constexpr bool provides() const noexcept
    {
        return provides(LayerT::id);
    }


private:
    PatchId                       m_id;
    std::string_view              m_name;
    coord_type                    m_natural_position;
    area_type                     m_local_area;
    std::span<const LayerBinding> m_layers;
};


} // namespace landor::geo
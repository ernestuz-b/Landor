#pragma once

#include "layer.hpp"

#include <tuple>
#include <utility>


namespace landor::geo
{

/**
 * Associates a value with the Layer whose value it represents.
 *
 * The layer identity is part of the type. This matters because unrelated
 * layers may use the same value_type.
 *
 * LayerValue is mainly useful when constructing a Tile:
 *
 *     Tile<Terrain, Elevation, Fire> {
 *         LayerValue<Terrain>   { terrain },
 *         LayerValue<Elevation> { elevation },
 *         LayerValue<Fire>      { fire }
 *     };
 */
template<Layer LayerT>
struct LayerValue
{
    using layer_type = LayerT;
    using value_type = typename LayerT::value_type;

    value_type value;
};


/**
 * The synthetic value of one Map coordinate.
 *
 * Tile is not part of the stored representation of a Map. Map data remains
 * layer-oriented all the way down to authored patches, procedural sources,
 * live simulation state, caches and mass storage.
 *
 * A Tile is assembled only when somebody asks the Map what exists at one
 * coordinate:
 *
 *     auto tile = map.at(position);
 *
 * Map resolves each layer independently and copies the resulting values into
 * the Tile. The Tile therefore owns its values and contains no references,
 * pointers or resolved addresses into map storage or the managed heap.
 *
 * This is particularly important because the sources of two values in the
 * same Tile may be completely different. At one coordinate, for example:
 *
 *     Terrain    may come from an authored palace Patch;
 *     Elevation  may come from the same Patch;
 *     Moisture   may come from procedural generation;
 *     Fire       may come from materialised simulation state;
 *     Wind       may be derived from the world seed and current step.
 *
 * None of those differences are visible to the consumer of the Tile.
 *
 * The Layers template arguments are the layer kinds understood by this build,
 * not the layers supplied by a particular Patch. A Patch which provides no
 * Fire data does not remove Fire from the Tile. Map still resolves Fire from
 * its fallback/default/live source.
 *
 * For example, an underwater Patch may provide no authored Fire layer:
 *
 *     patch.provides<Fire>() == false
 *
 * while:
 *
 *     map.at(position).get<Fire>()
 *
 * remains perfectly meaningful. Its initial value may be "no fire"; an arcane
 * spell may subsequently create fire there, and the fire simulation may then
 * extinguish it because the position is underwater.
 *
 * Construction requires one LayerValue for every layer. This is deliberate:
 * Map must not accidentally return a partially assembled Tile merely because
 * one of its sources was absent.
 *
 * Tile is a value. Modifying a Tile does not modify the Map. Authoritative
 * world changes must go through the Map's mutation interface.
 */
template<Layer... Layers>
class Tile
{
public:
    constexpr explicit Tile(LayerValue<Layers>... values)
        : m_values(std::move(values)...)
    {
    }


    /**
     * Return the value of one layer in this Tile.
     *
     * LayerT must be one of the layer types with which this Tile was declared.
     */
    template<Layer LayerT>
    [[nodiscard]] constexpr typename LayerT::value_type& get() noexcept
    {
        return std::get<LayerValue<LayerT>>(m_values).value;
    }


    template<Layer LayerT>
    [[nodiscard]] constexpr const typename LayerT::value_type&
    get() const noexcept
    {
        return std::get<LayerValue<LayerT>>(m_values).value;
    }


private:
    std::tuple<LayerValue<Layers>...> m_values;
};


} // namespace landor::geo
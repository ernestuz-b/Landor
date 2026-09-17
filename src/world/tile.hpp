#pragma once

#include "coord.hpp"
#include "layer.hpp"

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>


namespace landor::geo
{

namespace detail
{

template<typename Needle, typename... Haystack>
consteval std::size_t layer_index() noexcept
{
    constexpr std::array<bool, sizeof...(Haystack)> matches {
        std::same_as<Needle, Haystack>...
    };

    for (std::size_t i = 0; i < matches.size(); ++i)
    {
        if (matches[i])
            return i;
    }

    return matches.size();
}

} // namespace detail


/**
 * Compact value representing one Map coordinate.
 *
 * Tile is deliberately a value type. It owns a copy of the values visible at
 * its coordinate and contains no pointer, reference or handle into Map, Cache,
 * Storage or the managed heap.
 *
 * The layer set is a build-time property. The current representation assumes
 * one byte per layer and packs the values into a dense array. That storage is
 * private on purpose: if a future layer needs a wider value, Tile's internal
 * representation may change without changing the public tile[layer] API.
 *
 * Layer semantics remain intact. tile[Fire] means "the Fire value already
 * packed into this Tile"; it does not perform a Map, Cache or Storage lookup.
 *
 * A concrete layer intended for operator[] use is expected to be an empty
 * compile-time tag object, for example:
 *
 *     struct FireLayer {
 *         static constexpr LayerId id = ...;
 *         using value_type = std::uint8_t;
 *     };
 *
 *     inline constexpr FireLayer Fire {};
 *
 *     Tile tile = map.at(position);
 *     if (tile[Fire] == 0) { ... }
 */
template<typename CoordT, Layer... Layers>
class Tile
{
public:
    using coord_type    = CoordT;
    using property_type = std::uint8_t;

    static constexpr std::size_t property_count = sizeof...(Layers);

    // Current compact representation. This assertion is intentionally local to
    // Tile: widening a layer should require changing Tile's private packing,
    // not the Map/Tile public API.
    static_assert(
        (std::same_as<typename Layers::value_type, property_type> && ...),
        "Current Tile packing requires byte-sized layer values");

    constexpr Tile() noexcept = default;

    constexpr Tile(
        coord_type position,
        std::array<property_type, property_count> properties) noexcept
        : m_position(position),
          m_properties(properties)
    {
    }

    [[nodiscard]] constexpr coord_type position() const noexcept
    {
        return m_position;
    }

    /**
     * Symbolic property access.
     *
     * The tag type must be one of the Layers carried by this Tile.
     */
    template<Layer LayerT>
        requires (std::same_as<LayerT, Layers> || ...)
    [[nodiscard]] constexpr typename LayerT::value_type&
    operator[](LayerT) noexcept
    {
        constexpr auto index = detail::layer_index<LayerT, Layers...>();
        static_assert(index < property_count);

        return m_properties[index];
    }

    template<Layer LayerT>
        requires (std::same_as<LayerT, Layers> || ...)
    [[nodiscard]] constexpr const typename LayerT::value_type&
    operator[](LayerT) const noexcept
    {
        constexpr auto index = detail::layer_index<LayerT, Layers...>();
        static_assert(index < property_count);

        return m_properties[index];
    }

    /**
     * Typed access retained for generic code that has the layer as a type
     * rather than as a tag object.
     */
    template<Layer LayerT>
        requires (std::same_as<LayerT, Layers> || ...)
    [[nodiscard]] constexpr typename LayerT::value_type& get() noexcept
    {
        constexpr auto index = detail::layer_index<LayerT, Layers...>();
        static_assert(index < property_count);

        return m_properties[index];
    }

    template<Layer LayerT>
        requires (std::same_as<LayerT, Layers> || ...)
    [[nodiscard]] constexpr const typename LayerT::value_type&
    get() const noexcept
    {
        constexpr auto index = detail::layer_index<LayerT, Layers...>();
        static_assert(index < property_count);

        return m_properties[index];
    }

    [[nodiscard]] constexpr bool operator==(const Tile&) const noexcept = default;

private:
    coord_type m_position {};
    std::array<property_type, property_count> m_properties {};
};


} // namespace landor::geo

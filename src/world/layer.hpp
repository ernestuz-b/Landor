#pragma once

#include <concepts>
#include <cstdint>


namespace landor::geo
{

/**
 * Stable identifier of a layer kind understood by the game.
 *
 * Layer ids belong to the game format. They identify concepts such as
 * terrain, elevation, fire or water; they do not identify stored instances
 * of those layers.
 */
using LayerId = std::uint8_t;


/**
 * A spatial property understood by the game.
 *
 * A Layer is a type, not a container and not stored data. It defines the
 * meaning and value type of one property of the world.
 *
 * A patch may provide authored data for any subset of the layers understood
 * by the game. Absence from a patch does not mean that the property cannot
 * exist there.
 *
 * For example, an underwater patch need not provide a Fire layer. A query may
 * initially obtain the game's default fire state there, while a spell can
 * later create fire state which the fire simulation may immediately extinguish.
 *
 * Concrete layer types provide:
 *
 *     static constexpr LayerId id;
 *     using value_type = ...;
 *
 * Layer types contain no storage, caching, persistence or simulation policy.
 * Those belong to the objects that provide and modify layer values.
 */
template<typename T>
concept Layer =
    requires
{
    { T::id } -> std::convertible_to<LayerId>;
    typename T::value_type;
};


} // namespace landor::geo

#pragma once

#include <cstdint>


namespace landor::geo
{

/**
 * Rotation applied to an authored patch when it is placed in a Map.
 *
 * Rotations are clockwise and operate in patch-local coordinates.
 */
enum class Rotation : std::uint8_t
{
    none,
    r90,
    r180,
    r270
};


/**
 * Reflection applied to an authored patch when it is placed in a Map.
 *
 * Reflection is applied before rotation.
 *
 * `x` reflects across the local X axis.
 * `y` reflects across the local Y axis.
 * `xy` reflects across both axes.
 */
enum class Reflection : std::uint8_t
{
    none,
    x,
    y,
    xy
};


/**
 * Orientation of a placed patch relative to its authored orientation.
 *
 * Translation is deliberately not part of Orientation. Position belongs to
 * the placement itself; this type only describes how the authored coordinates
 * are turned around their local origin.
 *
 * The transformation order is always:
 *
 *     reflection -> rotation -> translation
 *
 * Keeping the order fixed matters because reflection and rotation do not, in
 * general, commute.
 */
struct Orientation
{
    Rotation   rotation   = Rotation::none;
    Reflection reflection = Reflection::none;
};


} // namespace landor::geo

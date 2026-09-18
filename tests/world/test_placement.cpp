// Behavioural tests for the pinned Placement/world <-> Patch-local coordinate
// transform implemented by Placement::local_to_world() / world_to_local().
//
// The pinned contract (see placement.hpp and dev-docs/DESIGN_DECISIONS.md):
//
//     world = position + rotate(reflect(local))
//     local = reflect(inverse_rotate(world - position))
//
// with no post-transform renormalisation around position.

#include <gtest/gtest.h>

#include "world/placement.hpp"

#include <cstdint>
#include <limits>
#include <optional>


namespace
{

using landor::geo::Coord8;
using landor::geo::Coord16;
using landor::geo::Coord32;
using landor::geo::Orientation;
using landor::geo::Reflection;
using landor::geo::Rotation;

template<typename CoordT>
landor::geo::Placement<CoordT> make_placement(
    CoordT position,
    Rotation rotation = Rotation::none,
    Reflection reflection = Reflection::none)
{
    return landor::geo::Placement<CoordT>(
        1, 2, position, Orientation{rotation, reflection});
}


inline constexpr Rotation kAllRotations[] = {
    Rotation::none, Rotation::r90, Rotation::r180, Rotation::r270
};

inline constexpr Reflection kAllReflections[] = {
    Reflection::none, Reflection::x, Reflection::y, Reflection::xy
};


// The transform must be usable in constant expressions, including the
// representability failure path.
static_assert([] {
    const landor::geo::Placement<Coord32> placement(
        1, 2, Coord32(100, 200), Orientation{});
    const auto world = placement.local_to_world(Coord32(3, 4));
    return world.has_value() && *world == Coord32(103, 204);
}());

static_assert([] {
    const landor::geo::Placement<Coord32> placement(
        1, 2, Coord32(0, 0), Orientation{Rotation::r90, Reflection::x});
    const auto world = placement.local_to_world(Coord32(2, 3));
    return world.has_value() && *world == Coord32(-3, -2);
}());

static_assert([] {
    const landor::geo::Placement<Coord8> placement(
        1, 2, Coord8(127, 0), Orientation{});
    return !placement.local_to_world(Coord8(1, 0)).has_value();
}());


} // namespace


// ---------------------------------------------------------------------------
// Coordinate-width coverage: the transform must hold for every supported
// width, not only the host-friendly one.
// ---------------------------------------------------------------------------

using CoordinateWidths = ::testing::Types<Coord8, Coord16, Coord32>;


template<typename CoordT>
struct PlacementTransform : ::testing::Test
{
    using coord_type  = CoordT;
    using scalar_type = typename CoordT::scalar_type;
};


TYPED_TEST_SUITE(PlacementTransform, CoordinateWidths);


TYPED_TEST(PlacementTransform, IdentityForwardAndInverse)
{
    using coord_t = TestFixture::coord_type;

    const auto placement = make_placement<coord_t>(coord_t(5, 7));

    EXPECT_EQ(placement.local_to_world(coord_t(0, 0)), coord_t(5, 7));
    EXPECT_EQ(placement.local_to_world(coord_t(3, 4)), coord_t(8, 11));

    EXPECT_EQ(placement.world_to_local(coord_t(5, 7)), coord_t(0, 0));
    EXPECT_EQ(placement.world_to_local(coord_t(8, 11)), coord_t(3, 4));
}


TYPED_TEST(PlacementTransform, AllRotationsMatchPinnedOffsets)
{
    using coord_t = TestFixture::coord_type;

    struct { Rotation rotation; coord_t expected; } const cases[] = {
        {Rotation::none, { 2,  3}},
        {Rotation::r90,  { 3, -2}},
        {Rotation::r180, {-2, -3}},
        {Rotation::r270, {-3,  2}},
    };

    for (const auto& test_case : cases)
    {
        const auto placement = make_placement<coord_t>(
            coord_t(0, 0), test_case.rotation);

        EXPECT_EQ(placement.local_to_world(coord_t(2, 3)), test_case.expected)
            << static_cast<int>(test_case.rotation);
        EXPECT_EQ(placement.world_to_local(test_case.expected), coord_t(2, 3))
            << static_cast<int>(test_case.rotation);
    }
}


TYPED_TEST(PlacementTransform, AllReflectionsMatchPinnedOffsets)
{
    using coord_t = TestFixture::coord_type;

    struct { Reflection reflection; coord_t expected; } const cases[] = {
        {Reflection::none, { 2,  3}},
        {Reflection::x,    { 2, -3}},
        {Reflection::y,    {-2,  3}},
        {Reflection::xy,   {-2, -3}},
    };

    for (const auto& test_case : cases)
    {
        const auto placement = make_placement<coord_t>(
            coord_t(0, 0), Rotation::none, test_case.reflection);

        EXPECT_EQ(placement.local_to_world(coord_t(2, 3)), test_case.expected)
            << static_cast<int>(test_case.reflection);
        EXPECT_EQ(placement.world_to_local(test_case.expected), coord_t(2, 3))
            << static_cast<int>(test_case.reflection);
    }
}


TYPED_TEST(PlacementTransform, ReflectionIsAppliedBeforeRotation)
{
    using coord_t = TestFixture::coord_type;

    // local (2,3) with Reflection::x then r90:
    //     reflect: (2, -3)   rotate r90: (-3, -2)
    // Rotate-then-reflect would give (3, 2), so this pins the order.
    const auto at_origin = make_placement<coord_t>(
        coord_t(0, 0), Rotation::r90, Reflection::x);
    EXPECT_EQ(at_origin.local_to_world(coord_t(2, 3)), coord_t(-3, -2));
    EXPECT_EQ(at_origin.world_to_local(coord_t(-3, -2)), coord_t(2, 3));

    // Same orientation, translated anchor: (-3, -2) + (5, -7).
    const auto translated = make_placement<coord_t>(
        coord_t(5, -7), Rotation::r90, Reflection::x);
    EXPECT_EQ(translated.local_to_world(coord_t(2, 3)), coord_t(2, -9));
    EXPECT_EQ(translated.world_to_local(coord_t(2, -9)), coord_t(2, 3));

    // Every reflection combined with r90 on the non-symmetric point (2, 3).
    struct { Reflection reflection; coord_t expected; } const cases[] = {
        {Reflection::none, { 3, -2}},
        {Reflection::x,    {-3, -2}},
        {Reflection::y,    { 3,  2}},
        {Reflection::xy,   {-3,  2}},
    };

    for (const auto& test_case : cases)
    {
        const auto placement = make_placement<coord_t>(
            coord_t(0, 0), Rotation::r90, test_case.reflection);

        EXPECT_EQ(placement.local_to_world(coord_t(2, 3)), test_case.expected)
            << static_cast<int>(test_case.reflection);
    }
}


TYPED_TEST(PlacementTransform, RotationDoesNotRenormaliseAroundPosition)
{
    using coord_t = TestFixture::coord_type;

    // r90 sends local (1, 0) to relative (0, -1): the Patch now occupies a
    // coordinate *below* its anchor. The result must stay (10, 9); there is no
    // re-shift that keeps the oriented rectangle at or above position (10, 10).
    const auto placement = make_placement<coord_t>(
        coord_t(10, 10), Rotation::r90);

    EXPECT_EQ(placement.local_to_world(coord_t(1, 0)), coord_t(10, 9));
    EXPECT_EQ(placement.world_to_local(coord_t(10, 9)), coord_t(1, 0));
}


TYPED_TEST(PlacementTransform, LocalOriginAlwaysMapsToPosition)
{
    using coord_t = TestFixture::coord_type;

    const coord_t positions[] = {{0, 0}, {120, -40}, {-127, 127}};

    for (const auto position : positions)
    {
        for (const auto rotation : kAllRotations)
        {
            for (const auto reflection : kAllReflections)
            {
                const auto placement = make_placement<coord_t>(
                    position, rotation, reflection);

                EXPECT_EQ(placement.local_to_world(coord_t(0, 0)), position)
                    << static_cast<int>(rotation) << " " << static_cast<int>(reflection);
                EXPECT_EQ(placement.world_to_local(position), coord_t(0, 0))
                    << static_cast<int>(rotation) << " " << static_cast<int>(reflection);
            }
        }
    }
}


TYPED_TEST(PlacementTransform, RoundTripForAllSixteenOrientations)
{
    using coord_t = TestFixture::coord_type;

    // Anchor and points are small enough that every intermediate value stays
    // representable in the narrowest supported width (int8_t), so both
    // directions must always succeed here.
    const coord_t position(37, -11);

    const coord_t local_points[] = {
        {0, 0}, {2, 3}, {-2, 3}, {5, -4}, {-9, -9}, {11, 6}
    };
    const coord_t world_points[] = {
        {37, -11}, {0, 0}, {12, -88}, {-90, 100}, {100, -60}
    };

    for (const auto rotation : kAllRotations)
    {
        for (const auto reflection : kAllReflections)
        {
            const auto placement = make_placement<coord_t>(
                position, rotation, reflection);

            for (const auto local : local_points)
            {
                const auto world = placement.local_to_world(local);
                ASSERT_TRUE(world.has_value())
                    << static_cast<int>(rotation) << " " << static_cast<int>(reflection);
                const auto back = placement.world_to_local(*world);
                ASSERT_TRUE(back.has_value())
                    << static_cast<int>(rotation) << " " << static_cast<int>(reflection);
                EXPECT_EQ(back.value(), local)
                    << static_cast<int>(rotation) << " " << static_cast<int>(reflection);
            }

            for (const auto world : world_points)
            {
                const auto local = placement.world_to_local(world);
                ASSERT_TRUE(local.has_value())
                    << static_cast<int>(rotation) << " " << static_cast<int>(reflection);
                const auto back = placement.local_to_world(*local);
                ASSERT_TRUE(back.has_value())
                    << static_cast<int>(rotation) << " " << static_cast<int>(reflection);
                EXPECT_EQ(back.value(), world)
                    << static_cast<int>(rotation) << " " << static_cast<int>(reflection);
            }
        }
    }
}


TYPED_TEST(PlacementTransform, ForwardOverflowReturnsNulloptInsteadOfWrapping)
{
    using coord_t = TestFixture::coord_type;
    using scalar_t = TestFixture::scalar_type;

    const auto max_position = static_cast<scalar_t>(
        std::numeric_limits<scalar_t>::max());
    const auto placement = make_placement<coord_t>(coord_t(max_position, 0));

    // max + 1 must be reported as absent, not wrapped to the minimum.
    EXPECT_FALSE(placement.local_to_world(coord_t(1, 0)).has_value());

    // One step inside the range must still succeed.
    EXPECT_EQ(
        placement.local_to_world(coord_t(-1, 0)),
        coord_t(static_cast<scalar_t>(max_position - 1), 0));
}


TYPED_TEST(PlacementTransform, InverseSubtractionOverflowReturnsNullopt)
{
    using coord_t = TestFixture::coord_type;
    using scalar_t = TestFixture::scalar_type;

    const auto min_position = static_cast<scalar_t>(
        std::numeric_limits<scalar_t>::min());
    const auto placement = make_placement<coord_t>(coord_t(min_position, 0));

    // max - min is 2*max + 1 away from the anchor: outside the type.
    EXPECT_FALSE(placement.world_to_local(
        coord_t(static_cast<scalar_t>(std::numeric_limits<scalar_t>::max()), 0))
        .has_value());

    // Adjacent representable relatives still succeed.
    EXPECT_EQ(
        placement.world_to_local(coord_t(min_position, 0)),
        coord_t(0, 0));
    EXPECT_EQ(
        placement.world_to_local(
            coord_t(static_cast<scalar_t>(min_position + 1), 0)),
        coord_t(1, 0));
}


TYPED_TEST(PlacementTransform, ReflectingTheMinimumScalarUsesWideNegation)
{
    using coord_t = TestFixture::coord_type;
    using scalar_t = TestFixture::scalar_type;

    const auto min_value = static_cast<scalar_t>(
        std::numeric_limits<scalar_t>::min());
    const auto placement = make_placement<coord_t>(
        coord_t(0, min_value), Rotation::none, Reflection::x);

    // local (0, min) reflected across the x axis is mathematically (0, -min),
    // which scalar_t cannot hold. Adding the anchor (0, min) brings the
    // result back to (0, 0). Negating `min` in scalar_t would be signed
    // overflow; the wide intermediate must handle it instead.
    EXPECT_EQ(placement.local_to_world(coord_t(0, min_value)), coord_t(0, 0));
}


TYPED_TEST(PlacementTransform, RotationKeepsRepresentableExtremesRepresentable)
{
    using coord_t = TestFixture::coord_type;
    using scalar_t = TestFixture::scalar_type;

    const auto max_value = static_cast<scalar_t>(
        std::numeric_limits<scalar_t>::max());
    const auto min_value = static_cast<scalar_t>(
        std::numeric_limits<scalar_t>::min());

    // r180 of local (1, 0) is relative (-1, 0): max + (-1) is representable.
    EXPECT_EQ(
        make_placement<coord_t>(coord_t(max_value, max_value), Rotation::r180)
            .local_to_world(coord_t(1, 0)),
        coord_t(static_cast<scalar_t>(max_value - 1), max_value));

    // r180 of local (0, -1) is relative (0, 1): min + 1 is representable.
    EXPECT_EQ(
        make_placement<coord_t>(coord_t(min_value, min_value), Rotation::r180)
            .local_to_world(coord_t(0, -1)),
        coord_t(min_value, static_cast<scalar_t>(min_value + 1)));
}


// ---------------------------------------------------------------------------
// Pinned non-typed cases (exact values from the task contract).
// ---------------------------------------------------------------------------

TEST(PlacementTransform, IdentityMatchesPinnedExample)
{
    const landor::geo::Placement<Coord32> placement(
        1, 2, Coord32(100, 200), Orientation{});

    EXPECT_EQ(placement.local_to_world(Coord32(0, 0)), Coord32(100, 200));
    EXPECT_EQ(placement.local_to_world(Coord32(3, 4)), Coord32(103, 204));

    EXPECT_EQ(placement.world_to_local(Coord32(100, 200)), Coord32(0, 0));
    EXPECT_EQ(placement.world_to_local(Coord32(103, 204)), Coord32(3, 4));
}


TEST(PlacementTransform, Coord8PinnedRepresentabilityCases)
{
    // Forward: 127 + 1 would wrap to -128 in int8_t; the transform must not.
    const landor::geo::Placement<Coord8> forward(
        1, 2, Coord8(127, 0), Orientation{});
    EXPECT_FALSE(forward.local_to_world(Coord8(1, 0)).has_value());

    // Inverse: 127 - (-128) = 255 is outside int8_t even though both inputs
    // are perfectly representable.
    const landor::geo::Placement<Coord8> inverse(
        1, 2, Coord8(-128, 0), Orientation{});
    EXPECT_FALSE(inverse.world_to_local(Coord8(127, 0)).has_value());
}

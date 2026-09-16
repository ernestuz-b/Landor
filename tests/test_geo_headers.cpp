// GoogleTest smoke test for the header-only landor::geo contracts.
//
// Exists to prove the toolchain works end to end: fetched GoogleTest links, the
// src/ include path resolves, and the headers compile as a translation unit of
// their own. Cases are behavioural, mirroring tests/test_coord.cpp and
// tests/test_area.cpp, which are still hand-rolled mains.
//
// Note on style: brace-init lists carry commas, which the preprocessor treats
// as macro-argument separators inside EXPECT_*/ASSERT_* calls. Values are bound
// to named locals first, which reads better than sprinkling protective parens.

#include <gtest/gtest.h>

#include "world/area.hpp"
#include "world/coord.hpp"

TEST(Coord32, AddsAndSubtractsComponentwise)
{
    const landor::geo::Coord32 origin{5, -3};
    const landor::geo::Coord32 offset{2, 4};

    const landor::geo::Coord32 sum = origin + offset;
    const landor::geo::Coord32 difference = origin - offset;
    const landor::geo::Coord32 negated = -origin;

    EXPECT_EQ(sum, (landor::geo::Coord32{7, 1}));
    EXPECT_EQ(difference, (landor::geo::Coord32{3, -7}));
    EXPECT_EQ(negated, (landor::geo::Coord32{-5, 3}));
}

TEST(Coord32, MeasuresDistanceBetweenPoints)
{
    const landor::geo::Coord32 a{0, 0};
    const landor::geo::Coord32 b{3, 4};

    EXPECT_EQ(landor::geo::Coord32::manhattan(a, b), 7);
    EXPECT_EQ(landor::geo::Coord32::chebyshev(a, b), 4);
    EXPECT_EQ(a.dist_sq(b), 25);
}

TEST(Coord32, StepsToNeighbourInCardinalDirections)
{
    const landor::geo::Coord32 here{6, -3};

    EXPECT_EQ(here.neighbour(landor::geo::Dir::East), (landor::geo::Coord32{7, -3}));
    EXPECT_EQ(here.neighbour(landor::geo::Dir::North), (landor::geo::Coord32{6, -4}));
    EXPECT_EQ(here.neighbour(landor::geo::Dir::West), (landor::geo::Coord32{5, -3}));
    EXPECT_EQ(here.neighbour(landor::geo::Dir::South), (landor::geo::Coord32{6, -2}));
}

TEST(Coord32, IsUsableInConstantExpressions)
{
    // The header contracts are constexpr throughout; if that regresses this
    // translation unit stops compiling rather than failing at runtime.
    constexpr landor::geo::Coord32 a{1, 2};
    constexpr landor::geo::Coord32 b{3, 4};
    static_assert(a + b == landor::geo::Coord32{4, 6});
    SUCCEED();
}

/** Regression guard for the neighbour8() octant table.
 *
 * Octant 5 (north-west) shipped as `dx = -1, dy = -0`, i.e. it returned due
 * west, and octants 0..7 were therefore not eight distinct neighbours. The
 * loop form below fails on any single-axis repeat, so a copy-paste slip in one
 * case cannot pass silently again.
 */
TEST(Coord32, Neighbour8CoversEveryOctantExactlyOnce)
{
    const landor::geo::Coord32 here{6, -3};
    // Matches the Dir ordering documented on neighbour8(): y grows southward.
    const landor::geo::Coord32 expected[8] = {
        {7, -3},   // 0 east
        {7, -2},   // 1 south-east
        {6, -2},   // 2 south
        {5, -2},   // 3 south-west
        {5, -3},   // 4 west
        {5, -4},   // 5 north-west
        {6, -4},   // 6 north
        {7, -4},   // 7 north-east
    };

    for (uint8_t octant = 0; octant < 8; ++octant) {
        const landor::geo::Coord32 step = here.neighbour8(octant);
        EXPECT_EQ(step, expected[octant]) << "octant " << unsigned(octant);
        EXPECT_EQ(landor::geo::Coord32::chebyshev(here, step), 1)
            << "octant " << unsigned(octant);
    }

    // The even octants are the cardinal directions and must agree with
    // neighbour(Dir): Dir{East,South,West,North} == octants {0,2,4,6}.
    for (const auto dir : {landor::geo::Dir::East, landor::geo::Dir::South,
                           landor::geo::Dir::West, landor::geo::Dir::North}) {
        const auto ordinal = static_cast<uint8_t>(dir) * 2;
        EXPECT_EQ(here.neighbour8(ordinal), here.neighbour(dir))
            << "octant " << unsigned(ordinal) << " disagrees with its Dir";
    }
}

/// Out-of-range octants must be inert rather than wrap around.
TEST(Coord32, Neighbour8LeavesCoordUnchangedOutOfRange)
{
    const landor::geo::Coord32 here{6, -3};

    EXPECT_EQ(here.neighbour8(8), here);
    EXPECT_EQ(here.neighbour8(255), here);
}

TEST(Area32, SortsIncomingCorners)
{
    const landor::geo::Coord32 lo{0, 0};
    const landor::geo::Coord32 hi{4, 3};

    const landor::geo::Area32 forward{lo, hi};
    const landor::geo::Area32 reversed{hi, lo};

    EXPECT_EQ(reversed.min(), forward.min());
    EXPECT_EQ(reversed.max(), forward.max());
    EXPECT_EQ(forward.width(), 5);   // corners are inclusive
    EXPECT_EQ(forward.height(), 4);
}

TEST(Area32, ContainsItsBorderCellsOnly)
{
    const landor::geo::Area32 area{landor::geo::Coord32{0, 0}, landor::geo::Coord32{4, 3}};

    EXPECT_TRUE(area.contains(landor::geo::Coord32{0, 0}));
    EXPECT_TRUE(area.contains(landor::geo::Coord32{4, 3}));
    EXPECT_TRUE(area.contains(landor::geo::Coord32{2, 1}));
    EXPECT_FALSE(area.contains(landor::geo::Coord32{-1, 0}));
    EXPECT_FALSE(area.contains(landor::geo::Coord32{4, 4}));
}

TEST(Area32, DefaultsToEmptyAndReportsNoExtent)
{
    const landor::geo::Area32 empty;

    EXPECT_TRUE(empty.is_empty());
    EXPECT_EQ(empty.width(), 0);
    EXPECT_EQ(empty.height(), 0);
}

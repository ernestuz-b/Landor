// GoogleTest smoke test for the header-only Geo contracts.
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
    const Geo::Coord32 origin{5, -3};
    const Geo::Coord32 offset{2, 4};

    const Geo::Coord32 sum = origin + offset;
    const Geo::Coord32 difference = origin - offset;
    const Geo::Coord32 negated = -origin;

    EXPECT_EQ(sum, (Geo::Coord32{7, 1}));
    EXPECT_EQ(difference, (Geo::Coord32{3, -7}));
    EXPECT_EQ(negated, (Geo::Coord32{-5, 3}));
}

TEST(Coord32, MeasuresDistanceBetweenPoints)
{
    const Geo::Coord32 a{0, 0};
    const Geo::Coord32 b{3, 4};

    EXPECT_EQ(Geo::Coord32::manhattan(a, b), 7);
    EXPECT_EQ(Geo::Coord32::chebyshev(a, b), 4);
    EXPECT_EQ(a.dist_sq(b), 25);
}

TEST(Coord32, StepsToNeighbourInCardinalDirections)
{
    const Geo::Coord32 here{6, -3};

    EXPECT_EQ(here.neighbour(Geo::Dir::East), (Geo::Coord32{7, -3}));
    EXPECT_EQ(here.neighbour(Geo::Dir::North), (Geo::Coord32{6, -4}));
    EXPECT_EQ(here.neighbour(Geo::Dir::West), (Geo::Coord32{5, -3}));
    EXPECT_EQ(here.neighbour(Geo::Dir::South), (Geo::Coord32{6, -2}));
}

TEST(Coord32, IsUsableInConstantExpressions)
{
    // The header contracts are constexpr throughout; if that regresses this
    // translation unit stops compiling rather than failing at runtime.
    static_assert(Geo::Coord32{1, 2} + Geo::Coord32{3, 4} == Geo::Coord32{4, 6});
    SUCCEED();
}

TEST(Area32, SortsIncomingCorners)
{
    const Geo::Coord32 lo{0, 0};
    const Geo::Coord32 hi{4, 3};

    const Geo::Area32 forward{lo, hi};
    const Geo::Area32 reversed{hi, lo};

    EXPECT_EQ(reversed.min(), forward.min());
    EXPECT_EQ(reversed.max(), forward.max());
    EXPECT_EQ(forward.width(), 5);   // corners are inclusive
    EXPECT_EQ(forward.height(), 4);
}

TEST(Area32, ContainsItsBorderCellsOnly)
{
    const Geo::Area32 area{Geo::Coord32{0, 0}, Geo::Coord32{4, 3}};

    EXPECT_TRUE(area.contains(Geo::Coord32{0, 0}));
    EXPECT_TRUE(area.contains(Geo::Coord32{4, 3}));
    EXPECT_TRUE(area.contains(Geo::Coord32{2, 1}));
    EXPECT_FALSE(area.contains(Geo::Coord32{-1, 0}));
    EXPECT_FALSE(area.contains(Geo::Coord32{4, 4}));
}

TEST(Area32, DefaultsToEmptyAndReportsNoExtent)
{
    const Geo::Area32 empty;

    EXPECT_TRUE(empty.is_empty());
    EXPECT_EQ(empty.width(), 0);
    EXPECT_EQ(empty.height(), 0);
}

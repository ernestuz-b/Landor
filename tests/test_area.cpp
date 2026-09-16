// Unit tests for landor::geo::Area (area.hpp).
//
// Coverage migrated on 2026-09-16 from the hand-rolled `int main()` that used to
// live in this file, plus the cases that were in tests/test_geo_headers.cpp.
// Corner cases that the old main only checked as "does not crash" are now
// asserted exactly: split halves, clamp results and the stream format.
//
// Style note: brace-init lists carry commas, which the preprocessor treats as
// macro-argument separators inside EXPECT_*/ASSERT_* calls. Values are bound to
// named locals first — that reads better than sprinkling protective parens.

#include <gtest/gtest.h>

#include "world/area.hpp"

#include <cstdint>
#include <optional>
#include <sstream>
#include <string>

using landor::geo::Area8;
using landor::geo::Area16;
using landor::geo::Area32;
using landor::geo::Coord16;
using landor::geo::Coord32;
using landor::geo::Coord8;

/// Corners are inclusive, so a 1x1 area has width 1, not 0.
TEST(Area32, MeasuresInclusiveCorners)
{
    const Coord32 lo{0, 0};
    const Coord32 hi{4, 3};
    const Area32 area{lo, hi};

    EXPECT_EQ(area.min(), lo);
    EXPECT_EQ(area.max(), hi);
    EXPECT_EQ(area.width(), 5);
    EXPECT_EQ(area.height(), 4);
    EXPECT_FALSE(area.is_empty());

    const Area32 single_cell{lo, lo};

    EXPECT_EQ(single_cell.width(), 1);
    EXPECT_EQ(single_cell.height(), 1);
    EXPECT_FALSE(single_cell.is_empty());
}

TEST(Area32, DefaultsToEmptyAndReportsNoExtent)
{
    const Area32 empty;

    EXPECT_TRUE(empty.is_empty());
    EXPECT_EQ(empty.width(), 0);
    EXPECT_EQ(empty.height(), 0);
}

/** The default state is the only way to get an inverted area.
 *
 * Both public constructors sort their corners, so `min > max` cannot be
 * authored; it exists because the default is deliberately "no cells"
 * (min = (1,1), max = (0,0)). Every query therefore has to keep its
 * is_empty() guard honest.
 */
TEST(Area32, ConstructorsCannotProduceAnInvertedArea)
{
    const Area32 forwards{Coord32{0, 0}, Coord32{4, 3}};
    const Area32 reversed{Coord32{4, 3}, Coord32{0, 0}};
    const Area32 crossed{Coord32{4, 0}, Coord32{0, 3}};

    EXPECT_EQ(reversed, forwards);
    EXPECT_EQ(crossed, forwards);
    EXPECT_TRUE(forwards == reversed);
    EXPECT_FALSE(forwards != reversed);
    EXPECT_FALSE(reversed.is_empty());

    // Sorting is per axis, not per corner: min stays top-left.
    const Area32 sorted = reversed;
    EXPECT_LE(sorted.min().x(), sorted.max().x());
    EXPECT_LE(sorted.min().y(), sorted.max().y());
}

TEST(Area32, ContainsItsBorderCellsOnly)
{
    const Area32 area{Coord32{0, 0}, Coord32{4, 3}};

    EXPECT_TRUE(area.contains(Coord32{0, 0}));
    EXPECT_TRUE(area.contains(Coord32{4, 3}));
    EXPECT_TRUE(area.contains(Coord32{2, 1}));
    EXPECT_FALSE(area.contains(Coord32{-1, 0}));
    EXPECT_FALSE(area.contains(Coord32{4, 4}));

    // An empty area contains nothing, not even the coordinate of its own min.
    const Area32 empty;

    EXPECT_FALSE(empty.contains(Coord32{0, 0}));
    EXPECT_FALSE(empty.contains(Coord32{1, 1}));
}

/// An empty Area is a subset of everything, including another empty one.
TEST(Area32, ContainsWholeAreas)
{
    const Area32 outer{Coord32{-1, -1}, Coord32{5, 4}};
    const Area32 inner{Coord32{1, 1}, Coord32{3, 2}};
    const Area32 straddling{Coord32{3, 3}, Coord32{9, 9}};
    const Area32 empty;

    EXPECT_TRUE(outer.contains(inner));
    EXPECT_TRUE(inner.contains(inner));   // itself counts as contained
    EXPECT_FALSE(inner.contains(outer));
    EXPECT_FALSE(outer.contains(straddling));

    EXPECT_TRUE(outer.contains(empty));
    EXPECT_TRUE(empty.contains(empty));
}

TEST(Area32, IntersectsReportsSharedCells)
{
    const Area32 a{Coord32{0, 0}, Coord32{4, 3}};
    const Area32 overlapping{Coord32{2, 1}, Coord32{6, 5}};
    const Area32 distant{Coord32{10, 10}, Coord32{20, 20}};
    const Area32 adjacent{Coord32{5, 0}, Coord32{9, 3}};
    const Area32 empty;

    EXPECT_TRUE(a.intersects(overlapping));
    EXPECT_TRUE(overlapping.intersects(a));      // symmetric
    EXPECT_FALSE(a.intersects(distant));
    EXPECT_FALSE(a.intersects(adjacent));        // touching edges share no cell
    EXPECT_FALSE(a.intersects(empty));
    EXPECT_FALSE(empty.intersects(a));
}

TEST(Area32, IntersectionOfOverlappingAreas)
{
    const Area32 a{Coord32{0, 0}, Coord32{4, 3}};
    const Area32 b{Coord32{2, 1}, Coord32{6, 5}};

    const std::optional<Area32> inter = a.intersection(b);

    ASSERT_TRUE(inter.has_value());
    EXPECT_EQ(inter->min(), (Coord32{2, 1}));
    EXPECT_EQ(inter->max(), (Coord32{4, 3}));
    EXPECT_EQ(inter->width(), 3);
    EXPECT_EQ(inter->height(), 3);
}

TEST(Area32, IntersectionOfDisjointAreasHasNoValue)
{
    const Area32 a{Coord32{0, 0}, Coord32{4, 4}};
    const Area32 distant{Coord32{10, 10}, Coord32{20, 20}};
    const Area32 adjacent{Coord32{5, 0}, Coord32{9, 4}};
    const Area32 empty;

    EXPECT_FALSE(a.intersection(distant).has_value());
    EXPECT_FALSE(a.intersection(adjacent).has_value());
    EXPECT_FALSE(a.intersection(empty).has_value());
}

/// Corner-touching areas overlap in exactly one cell — the boundary case.
TEST(Area32, IntersectionOfCornerTouchingAreasIsOneCell)
{
    const Area32 a{Coord32{0, 0}, Coord32{4, 4}};
    const Area32 b{Coord32{4, 4}, Coord32{8, 8}};

    const std::optional<Area32> corner = a.intersection(b);

    ASSERT_TRUE(corner.has_value());
    EXPECT_EQ(corner->min(), (Coord32{4, 4}));
    EXPECT_EQ(corner->max(), (Coord32{4, 4}));
    EXPECT_EQ(corner->width(), 1);
    EXPECT_EQ(corner->height(), 1);
}

TEST(Area32, UnionIsTheBoundingBox)
{
    const Area32 a{Coord32{0, 0}, Coord32{4, 3}};
    const Area32 d{Coord32{3, 4}, Coord32{7, 8}};

    const Area32 united = a.union_area(d);

    EXPECT_EQ(united.min(), (Coord32{0, 0}));
    EXPECT_EQ(united.max(), (Coord32{7, 8}));

    // Adjacent areas: the bounding box covers the gap between them too.
    const Area32 adjacent{Coord32{5, 0}, Coord32{9, 4}};
    const Area32 wide_box = a.union_area(adjacent);

    EXPECT_EQ(wide_box, (Area32::from_coords(0, 0, 9, 4)));
}

/** Empty is the identity element of union, from either side.
 *
 * The far-from-origin case matters: an empty Area is stored as min = (1,1),
 * max = (0,0), so bounding-box arithmetic against an area that happens to
 * contain those corners produces the right answer even when the is_empty()
 * guard is missing. {6, 6}-{9, 9} does not contain them, so dropping a guard
 * shows up as a wrong box instead of passing by luck.
 */
TEST(Area32, UnionWithEmptyReturnsTheOtherSide)
{
    const Area32 near_origin{Coord32{0, 0}, Coord32{4, 3}};
    const Area32 far_origin{Coord32{6, 6}, Coord32{9, 9}};
    const Area32 empty;

    EXPECT_EQ(near_origin.union_area(empty), near_origin);
    EXPECT_EQ(empty.union_area(near_origin), near_origin);
    EXPECT_EQ(far_origin.union_area(empty), far_origin);
    EXPECT_EQ(empty.union_area(far_origin), far_origin);
    EXPECT_TRUE(empty.union_area(empty).is_empty());
}

TEST(Area32, ClampTrimsToTheOuterArea)
{
    const Area32 outer{Coord32{0, 0}, Coord32{4, 3}};
    const Area32 overlapping{Coord32{-2, -2}, Coord32{6, 6}};
    const Area32 inside{Coord32{1, 1}, Coord32{2, 2}};

    const Area32 trimmed = overlapping.clamp(outer);

    EXPECT_EQ(trimmed.min(), (Coord32{0, 0}));
    EXPECT_EQ(trimmed.max(), (Coord32{4, 3}));
    // An area already inside is unchanged by clamping.
    EXPECT_EQ(inside.clamp(outer), inside);
}

TEST(Area32, ClampOutsideOrAgainstEmptyYieldsEmpty)
{
    const Area32 outer{Coord32{0, 0}, Coord32{4, 3}};
    const Area32 distant{Coord32{10, 10}, Coord32{20, 20}};
    const Area32 empty;

    EXPECT_TRUE(distant.clamp(outer).is_empty());
    EXPECT_TRUE(outer.clamp(empty).is_empty());
    EXPECT_TRUE(empty.clamp(outer).is_empty());

    // Away from the origin, where the (1,1)/(0,0) empty sentinel cannot make a
    // missing guard accidentally produce the right box.
    const Area32 far_outer{Coord32{6, 6}, Coord32{9, 9}};
    const Area32 far_distant{Coord32{-9, -9}, Coord32{-6, -6}};

    EXPECT_TRUE(far_distant.clamp(far_outer).is_empty());
}

/** split() cuts the longer axis, leaving no gap and no overlap.
 *
 * Even extents halve evenly; odd extents leave the extra cell in the first
 * half, which is what makes `first.max() + 1 == second.min()` hold always.
 */
TEST(Area32, SplitCutsTheLongerAxisWithoutGaps)
{
    const Area32 wide{Coord32{0, 0}, Coord32{9, 3}};   // 10 x 4 → vertical cut
    const auto [wide_first, wide_second] = wide.split();

    EXPECT_FALSE(wide_first.is_empty());
    EXPECT_FALSE(wide_second.is_empty());
    EXPECT_EQ(wide_first.width(), 5);
    EXPECT_EQ(wide_second.width(), 5);
    EXPECT_EQ(wide_first.max().x() + 1, wide_second.min().x());
    EXPECT_EQ(wide_first.union_area(wide_second), wide);
    EXPECT_FALSE(wide_first.intersection(wide_second).has_value());

    const Area32 tall{Coord32{0, 0}, Coord32{3, 9}};   // 4 x 10 → horizontal cut
    const auto [tall_first, tall_second] = tall.split();

    EXPECT_EQ(tall_first.height(), 5);
    EXPECT_EQ(tall_second.height(), 5);
    EXPECT_EQ(tall_first.max().y() + 1, tall_second.min().y());
    EXPECT_EQ(tall_first.union_area(tall_second), tall);
    EXPECT_FALSE(tall_first.intersection(tall_second).has_value());
}

/// Odd extents split unevenly; a square is cut vertically (the x >= h branch).
TEST(Area32, SplitHandlesOddExtentsAndTies)
{
    const Area32 odd{Coord32{0, 0}, Coord32{4, 1}};   // 5 wide, 2 tall
    const auto [odd_first, odd_second] = odd.split();

    EXPECT_EQ(odd_first.width(), 3);
    EXPECT_EQ(odd_second.width(), 2);
    EXPECT_EQ(odd_first.union_area(odd_second), odd);

    const Area32 square{Coord32{0, 0}, Coord32{1, 1}};
    const auto [square_first, square_second] = square.split();

    // Tie goes to the x axis: two single-column halves.
    EXPECT_EQ(square_first, (Area32::from_coords(0, 0, 0, 1)));
    EXPECT_EQ(square_second, (Area32::from_coords(1, 0, 1, 1)));
}

/// Negative coordinates must split exactly like positive ones (§split casts).
TEST(Area32, SplitWorksWithNegativeCoordinates)
{
    const Area32 negative{Coord32{-4, -3}, Coord32{-1, 0}};   // 4 x 4 → vertical
    const auto [first, second] = negative.split();

    EXPECT_EQ(first, (Area32::from_coords(-4, -3, -3, 0)));
    EXPECT_EQ(second, (Area32::from_coords(-2, -3, -1, 0)));
    EXPECT_EQ(first.max().x() + 1, second.min().x());
    EXPECT_EQ(first.union_area(second), negative);
}

TEST(Area32, SplitOfEmptyYieldsTwoEmptyAreas)
{
    const Area32 empty;
    const auto [first, second] = empty.split();

    EXPECT_TRUE(first.is_empty());
    EXPECT_TRUE(second.is_empty());
}

TEST(Area32, FromCoordsBuildsAndNormalises)
{
    const Area32 built = Area32::from_coords(1, 2, 5, 6);

    EXPECT_EQ(built.min(), (Coord32{1, 2}));
    EXPECT_EQ(built.max(), (Coord32{5, 6}));
    EXPECT_EQ(built.width(), 5);
    EXPECT_EQ(built.height(), 5);

    const Area32 reversed = Area32::from_coords(5, 6, 1, 2);

    EXPECT_EQ(reversed, built);
}

TEST(Area32, AssignReNormalisesInPlace)
{
    Area32 area;

    EXPECT_TRUE(area.is_empty());

    area.assign(Coord32{10, 20}, Coord32{5, 15});

    EXPECT_EQ(area.min(), (Coord32{5, 15}));
    EXPECT_EQ(area.max(), (Coord32{10, 20}));
    EXPECT_FALSE(area.is_empty());
}

/// Narrow widths behave identically — the geometry must not depend on scalar size.
TEST(AreaWidths, BehaveIdenticallyAcrossScalarWidths)
{
    const Area8 narrow{Coord8{-50, -30}, Coord8{50, 30}};

    EXPECT_EQ(narrow.width(), 101);
    EXPECT_EQ(narrow.height(), 61);
    EXPECT_TRUE(narrow.contains(Coord8{0, 0}));

    const Area8 narrow_reversed{Coord8{50, 30}, Coord8{-50, -30}};

    EXPECT_EQ(narrow_reversed, narrow);

    const Coord16 far_lo{-1000, -2000};
    const Coord16 far_hi{1000, 2000};
    const Area16 medium{far_lo, far_hi};

    EXPECT_EQ(medium.width(), 2001);
    EXPECT_EQ(medium.height(), 4001);
    EXPECT_TRUE(medium.contains(Coord16{0, 0}));
    EXPECT_FALSE(medium.contains(Coord16{1001, 0}));
}

TEST(Area32, StreamsBothCorners)
{
    const Area32 area{Coord32{0, 0}, Coord32{4, 3}};

    std::ostringstream out;
    out << area;

    EXPECT_EQ(out.str(), "[(0,0)-(4,3)]");
}

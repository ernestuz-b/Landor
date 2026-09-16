// Unit tests for landor::geo::Coord (coord.hpp) and its Dir enum.
//
// Coverage migrated on 2026-09-16 from the hand-rolled `int main()` that used to
// live in this file, plus the cases that were in tests/test_geo_headers.cpp.
// Nothing was dropped on purpose; the numbered `return N;` failure codes of the
// old main are gone because GoogleTest names the failing expression itself.
//
// Style note: brace-init lists carry commas, which the preprocessor treats as
// macro-argument separators inside EXPECT_*/ASSERT_* calls. Values are bound to
// named locals first — that reads better than sprinkling protective parens.

#include <gtest/gtest.h>

#include "world/coord.hpp"

#include <array>
#include <cstdint>
#include <sstream>
#include <string>
#include <type_traits>

using landor::geo::Coord;
using landor::geo::Coord16;
using landor::geo::Coord32;
using landor::geo::Coord8;
using landor::geo::Dir;

// Compile-time constants of the header, checked without needing a running test.
static_assert(landor::geo::coord_dim_count == 2);
static_assert(landor::geo::coord_origin.x() == 0);
static_assert(landor::geo::coord_origin.y() == 0);
static_assert(std::is_same_v<Coord32::scalar_type, std::int32_t>);

TEST(Coord32, DefaultsToOriginAndReportsZero)
{
    const Coord32 default_constructed;

    EXPECT_TRUE(default_constructed.is_zero());
    EXPECT_EQ(default_constructed, landor::geo::coord_origin);

    const Coord32 single_axis{7};

    EXPECT_EQ(single_axis.x(), 7);
    EXPECT_EQ(single_axis.y(), 0);
}

TEST(Coord32, AddsAndSubtractsComponentwise)
{
    const Coord32 origin{5, -3};
    const Coord32 offset{2, 4};

    const Coord32 sum = origin + offset;
    const Coord32 difference = origin - offset;
    const Coord32 negated = -origin;

    EXPECT_EQ(sum, (Coord32{7, 1}));
    EXPECT_EQ(difference, (Coord32{3, -7}));
    EXPECT_EQ(negated, (Coord32{-5, 3}));
}

TEST(Coord8, MultipliesByScalarOnEitherSide)
{
    const Coord8 scaled{5, 10};

    const Coord8 left = 2 * scaled;
    const Coord8 right = scaled * 2;

    EXPECT_EQ(left, (Coord8{10, 20}));
    EXPECT_EQ(right, left);
}

TEST(Coord8, DividesTruncatingAndRemaindersComponentwise)
{
    const Coord8 quotient{12, 20};

    const Coord8 halved = quotient / 4;
    // Remainder takes a Coord on the right-hand side; there is no operator%(T).
    const Coord8 modulus{5, 20};
    const Coord8 remainder = quotient % modulus;

    EXPECT_EQ(halved, (Coord8{3, 5}));
    EXPECT_EQ(remainder, (Coord8{2, 0}));
}

TEST(Coord8, CompoundOperatorsMatchTheBinaryOnes)
{
    Coord8 value{1, 2};

    value += Coord8{3, 4};
    EXPECT_EQ(value, (Coord8{4, 6}));

    value -= Coord8{1, 1};
    EXPECT_EQ(value, (Coord8{3, 5}));

    value *= 2;
    EXPECT_EQ(value, (Coord8{6, 10}));

    value /= 3;
    EXPECT_EQ(value, (Coord8{2, 3}));
}

TEST(Coord32, MeasuresDistanceBetweenPoints)
{
    const Coord32 a{0, 0};
    const Coord32 b{3, 4};

    EXPECT_EQ(Coord32::manhattan(a, b), 7);
    EXPECT_EQ(Coord32::chebyshev(a, b), 4);
    EXPECT_EQ(a.dist_sq(b), 25);
}

/** dist_sq promotes to int64_t before multiplying.
 *
 * The narrow widths are the interesting ones: int8 coordinates squared must
 * not wrap inside the multiply, which is why the header casts explicitly.
 */
TEST(Coordinates, SquaredDistancePromotesAcrossWidths)
{
    const Coord8 origin8;
    const Coord16 origin16;
    const Coord32 origin32;
    const Coord8 near8{3, 4};
    const Coord16 near16{3, 4};
    const Coord32 near32{3, 4};
    const Coord8 offset{5, -3};

    EXPECT_EQ(origin8.dist_sq(near8), 25);
    EXPECT_EQ(origin16.dist_sq(near16), 25);
    EXPECT_EQ(origin32.dist_sq(near32), 25);
    EXPECT_EQ(offset.dist_sq(origin8), 34);

    // 127^2 + 127^2 would overflow int16_t; the return type carries it.
    const Coord8 corner_max{127, 127};
    const Coord8 corner_min{-128, -128};
    const auto far = corner_max.dist_sq(corner_min);
    static_assert(std::is_same_v<decltype(far), const std::int64_t>);
    EXPECT_EQ(far, 130050);   // 255^2 + 255^2
}

/** The narrow-width multiply must happen in 64 bits.
 *
 * Extreme corners of an int16 coordinate are 65535 apart, and 65535^2 does not
 * fit in a 32-bit int, so without the explicit promotion in dist_sq() the
 * multiply overflows (UB) long before the result leaves int64_t headroom.
 */
TEST(Coord16, ExtremeSquaredDistanceStaysInSixtyFourBits)
{
    const Coord16 lo{-32768, -32768};
    const Coord16 hi{32767, 32767};

    const auto squared = lo.dist_sq(hi);

    static_assert(std::is_same_v<decltype(squared), const std::int64_t>);
    EXPECT_EQ(squared, std::int64_t{2} * 65535 * 65535);
}

TEST(Coord8, DotProductTreatsCoordsAsVectors)
{
    const Coord8 a{1, 2};
    const Coord8 b{3, 4};

    EXPECT_EQ(a.dot(b), 11);
}

TEST(Coord32, StepsToNeighbourInCardinalDirections)
{
    const Coord32 here{6, -3};

    EXPECT_EQ(here.neighbour(Dir::East), (Coord32{7, -3}));
    EXPECT_EQ(here.neighbour(Dir::North), (Coord32{6, -4}));
    EXPECT_EQ(here.neighbour(Dir::West), (Coord32{5, -3}));
    EXPECT_EQ(here.neighbour(Dir::South), (Coord32{6, -2}));
}

/** Regression guard for the neighbour8() octant table.
 *
 * Octant 5 (north-west) shipped as `dx = -1, dy = -0`, i.e. it returned due
 * west, so octants 0..7 were not eight distinct neighbours. The loop form below
 * fails on any single-axis repeat, so a copy-paste slip in one case cannot pass
 * silently again.
 */
TEST(Coord32, Neighbour8CoversEveryOctantExactlyOnce)
{
    const Coord32 here{6, -3};
    // Matches the Dir ordering documented on neighbour8(): y grows southward.
    const Coord32 expected[8] = {
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
        const Coord32 step = here.neighbour8(octant);
        EXPECT_EQ(step, expected[octant]) << "octant " << unsigned(octant);
        EXPECT_EQ(Coord32::chebyshev(here, step), 1) << "octant " << unsigned(octant);
    }

    // The even octants are the cardinal directions and must agree with
    // neighbour(Dir): Dir{East,South,West,North} == octants {0,2,4,6}.
    for (const auto dir : {Dir::East, Dir::South, Dir::West, Dir::North}) {
        const auto ordinal = static_cast<uint8_t>(dir) * 2;
        EXPECT_EQ(here.neighbour8(ordinal), here.neighbour(dir))
            << "octant " << unsigned(ordinal) << " disagrees with its Dir";
    }
}

/// Out-of-range octants must be inert rather than wrap around.
TEST(Coord32, Neighbour8LeavesCoordUnchangedOutOfRange)
{
    const Coord32 here{6, -3};

    EXPECT_EQ(here.neighbour8(8), here);
    EXPECT_EQ(here.neighbour8(255), here);
}

TEST(Coord16, ClampsEachAxisIndependently)
{
    const Coord16 value{100, 200};

    const Coord16 clamped = value.clamp(-128, 127, -128, 127);

    EXPECT_EQ(clamped.x(), 100);   // already inside, untouched
    EXPECT_EQ(clamped.y(), 127);   // only y exceeded its bound
}

TEST(Coord16, ClampsToASquareAroundTheOrigin)
{
    const Coord16 value{40, -90};

    const Coord16 clamped = value.clamp_radius(64);

    EXPECT_EQ(clamped, (Coord16{40, -64}));
}

/// Toroidal wrapping: results stay in range and negative inputs do not stick.
TEST(Coord8, WrapModIsAlwaysNonNegative)
{
    const Coord8 negative{-5, -3};
    const Coord8 modulus{127, 127};

    const Coord8 wrapped = negative.wrap_mod(modulus);

    EXPECT_EQ(wrapped, (Coord8{122, 124}));
    EXPECT_FALSE(wrapped.has_negative());
    // Already in range: unchanged. Wrapping twice is a no-op.
    const Coord8 inside{5, 3};

    EXPECT_EQ(inside.wrap_mod(modulus), inside);
    EXPECT_EQ(wrapped.wrap_mod(modulus), wrapped);
}

TEST(Coord32, RotatesAroundTheOrigin)
{
    const Coord32 value{-1000, 2000};

    EXPECT_EQ(value.rotate_90ccw(), (Coord32{-2000, -1000}));
    EXPECT_EQ(value.rotate_180(), (Coord32{1000, -2000}));
    EXPECT_EQ(value.rotate_90cw(), (Coord32{2000, 1000}));
}

/** Rotation and reflection are involutions where the header claims they are.
 *
 * A quarter turn applied four times must be the identity — cheap to state and
 * it would catch a rotation implemented as a reflection by mistake.
 */
TEST(Coord32, RotationsAreInvertibleAndQuarterTurnsClose)
{
    const Coord32 value{3, -8};

    Coord32 spun = value;
    for (int turn = 0; turn < 4; ++turn) {
        spun = spun.rotate_90ccw();
    }
    EXPECT_EQ(spun, value);

    EXPECT_EQ(value.rotate_90ccw().rotate_90cw(), value);
    EXPECT_EQ(value.rotate_180().rotate_180(), value);
}

TEST(Coord8, InPlaceRotationMatchesTheConstOne)
{
    const Coord8 original{1, 2};

    Coord8 rotated = original;
    rotated.rotate_90ccw_inplace();

    EXPECT_EQ(rotated, original.rotate_90ccw());
    EXPECT_EQ(rotated, (Coord8{-2, 1}));
}

TEST(Coord8, SwapAxesIsEquivalentToDiagonalReflection)
{
    Coord8 swapped{7, -4};
    swapped.swap_axes();

    const Coord8 original{7, -4};

    EXPECT_EQ(swapped, original.reflect_diagonal());
    EXPECT_EQ(swapped, (Coord8{-4, 7}));

    // Swapping twice restores the original, which reflection also does.
    swapped.swap_axes();
    EXPECT_EQ(swapped, original);
}

TEST(Coord16, ReflectsAcrossEachAxis)
{
    const Coord16 value{3, 4};

    EXPECT_EQ(value.reflect_x(), (Coord16{3, -4}));   // negate y
    EXPECT_EQ(value.reflect_y(), (Coord16{-3, 4}));   // negate x
    EXPECT_EQ(value.reflect_diagonal(), (Coord16{4, 3}));

    // Each reflection undoes itself.
    EXPECT_EQ(value.reflect_x().reflect_x(), value);
    EXPECT_EQ(value.reflect_y().reflect_y(), value);
    EXPECT_EQ(value.reflect_diagonal().reflect_diagonal(), value);
}

/// Sign predicates: `non_negative` is "no axis negative", not "positive".
TEST(Coord8, SignPredicates)
{
    struct Case
    {
        Coord8      coord;
        bool        zero;
        bool        all_negative;
        bool        has_negative;
    };

    const Case cases[] = {
        {Coord8{1, 1}, false, false, false},
        {Coord8{-1, -1}, false, true, true},
        {Coord8{1, -1}, false, false, true},
        {Coord8{-1, 1}, false, false, true},
        {Coord8{0, 0}, true, false, false},
        {Coord8{0, -1}, false, false, true},
    };

    for (const auto& [coord, zero, all_negative, has_negative] : cases) {
        EXPECT_EQ(coord.is_zero(), zero) << coord;
        EXPECT_EQ(coord.all_negative(), all_negative) << coord;
        EXPECT_EQ(coord.has_negative(), has_negative) << coord;
        // non_negative() is exactly the negation of has_negative().
        EXPECT_EQ(coord.non_negative(), !has_negative) << coord;
    }
}

TEST(Coord32, AccessesComponentsByIndexAndArray)
{
    const std::array<std::int32_t, 2> values{4, -5};
    const Coord32 from_array{values};

    EXPECT_EQ(from_array.x(), 4);
    EXPECT_EQ(from_array.y(), -5);
    EXPECT_EQ(from_array.at(0), 4);
    EXPECT_EQ(from_array.at(1), -5);
    EXPECT_EQ(from_array.to_array(), values);

    // Out-of-range indices clamp to y rather than reading past the object.
    EXPECT_EQ(from_array.at(9), -5);

    Coord32 mutable_coord{0, 0};
    mutable_coord.at(1) = 6;
    EXPECT_EQ(mutable_coord, (Coord32{0, 6}));
}

/** Ordering is lexicographic with x first: DESIGN_STATE.md §9 invariant 12.
 *
 * Iteration and tie-break orders must be explicit and stable, so which axis
 * dominates is part of the contract, not an accident of the compiler.
 */
TEST(Coord8, OrdersLexicographicallyXBeforeY)
{
    const Coord8 origin;
    const Coord8 east{1, 0};
    const Coord8 north{0, 1};
    const Coord8 further_north{0, 2};
    const Coord8 low_left{-1, 100};
    const Coord8 high_right{0, -100};
    const Coord8 pair_a{2, 3};
    const Coord8 pair_b{3, 2};

    EXPECT_TRUE(origin < east);
    EXPECT_TRUE(north < further_north);
    EXPECT_TRUE(east > origin);

    // x wins even when y disagrees in the other direction.
    EXPECT_TRUE(low_left < high_right);
    EXPECT_FALSE(high_right < low_left);

    EXPECT_EQ(pair_a, (Coord8{2, 3}));
    EXPECT_NE(pair_a, pair_b);
}

/// Arithmetic wraps on overflow, like fixed-width integer math — never UB.
TEST(Coord8, ArithmeticWrapsInsteadOfOverflowing)
{
    const Coord8 max_value{127, 0};

    const Coord8 wrapped = max_value + Coord8{1, 0};

    EXPECT_EQ(wrapped.x(), -128);
    EXPECT_EQ(wrapped.y(), 0);
}

TEST(Coord32, StreamsAsParenthesisedPair)
{
    std::ostringstream out;
    out << Coord32{-5, 3};

    EXPECT_EQ(out.str(), "(-5,3)");
}

TEST(Coord32, IsUsableInConstantExpressions)
{
    // The header contracts are constexpr throughout; if that regresses this
    // translation unit stops compiling rather than failing at runtime.
    constexpr Coord32 a{1, 2};
    constexpr Coord32 b{3, 4};
    static_assert(a + b == Coord32{4, 6});
    static_assert(a.neighbour(Dir::North) == Coord32{1, 1});
    static_assert(a.neighbour8(5) == Coord32{0, 1});
    static_assert((a * 2).x() == 2);

    // manhattan()/chebyshev() are deliberately NOT asserted here: they call
    // std::abs on the promoted difference, and glibc's abs(int) is not declared
    // constexpr. GCC constant-folds it anyway, clang rejects the call in a
    // constant expression. Runtime behaviour is covered by
    // MeasuresDistanceBetweenPoints; the portability question is recorded in
    // STATUS.md §9 rather than hidden.
    EXPECT_EQ(Coord32::manhattan(a, b), 4);
    EXPECT_EQ(Coord32::chebyshev(a, b), 2);
    SUCCEED();
}

/// The scalar width is exposed as a nested type, which is what Map threads on.
TEST(Coordinates, ExposeTheirScalarType)
{
    static_assert(std::is_same_v<Coord8::scalar_type, std::int8_t>);
    static_assert(std::is_same_v<Coord16::scalar_type, std::int16_t>);
    static_assert(std::is_same_v<Coord<int64_t>::scalar_type, std::int64_t>);
    SUCCEED();
}

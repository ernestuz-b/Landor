// Behavioural tests for the Patch <-> dense v1 authored layer source
// geometry contract.
//
// The unit under test is the pure validation seam: plainly constructed
// Patch and LayerSourceLayout values. No Storage, no parsing.

#include <gtest/gtest.h>

#include "world/authored_layer_source.hpp"

#include <cstdint>
#include <limits>
#include <span>


namespace
{

using landor::geo::AuthoredLayerSourceError;
using landor::geo::Coord8;
using landor::geo::Coord16;
using landor::geo::Coord32;
using landor::geo::LayerBinding;
using landor::geo::LayerSourceLayout;
using landor::geo::Patch;
using landor::geo::validate_authored_layer_source;


constexpr std::uint16_t k_patch_id = 1;


/// One binding table shared by every test Patch. The helper never looks
/// at it, so any valid values do.
const LayerBinding k_bindings[] = {
    {landor::geo::LayerId{1}, landor::storage::SourceId{0}},
};


/**
 * Construct a Patch with the given local rectangle and natural position.
 *
 * Corners and position are scalar values of the Patch's own coordinate
 * width, so the same helper drives the narrow-type tests.
 */
template<typename CoordT>
Patch<CoordT> make_patch(
    typename CoordT::scalar_type min_x,
    typename CoordT::scalar_type min_y,
    typename CoordT::scalar_type max_x,
    typename CoordT::scalar_type max_y,
    typename CoordT::scalar_type pos_x,
    typename CoordT::scalar_type pos_y)
{
    return Patch<CoordT>(
        k_patch_id,
        "TestPatch",
        CoordT{pos_x, pos_y},
        landor::geo::Area<CoordT>{CoordT{min_x, min_y}, CoordT{max_x, max_y}},
        std::span<const LayerBinding>{k_bindings});
}


/**
 * Construct a parsed layout with the given D dimensions and P position.
 *
 * The other layout fields (offsets, strides, sizes) are irrelevant to the
 * geometry contract and stay at their defaults.
 */
LayerSourceLayout make_layout(
    std::uint32_t width,
    std::uint32_t height,
    std::int32_t pos_x,
    std::int32_t pos_y)
{
    LayerSourceLayout layout {};
    layout.width = width;
    layout.height = height;
    layout.natural_position = Coord32{pos_x, pos_y};
    return layout;
}


} // namespace


// --- Valid contract ---------------------------------------------------------

TEST(AuthoredLayerSource, ValidGeometryPasses)
{
    // Patch local (0,0)..(3,2), natural (100,200); source D 4 x 3, P (100,200).
    const auto patch = make_patch<Coord32>(0, 0, 3, 2, 100, 200);
    const auto layout = make_layout(4, 3, 100, 200);

    EXPECT_TRUE(validate_authored_layer_source(patch, layout).has_value());
}


// --- Patch local origin -------------------------------------------------------

TEST(AuthoredLayerSource, NonZeroOriginIsRejected)
{
    // Dimensions and position match in every case, so only the local
    // origin violates the contract. None is translated or normalized away.
    const auto layout = make_layout(4, 3, 100, 200);

    const auto shifted_x = make_patch<Coord32>(1, 0, 4, 2, 100, 200);
    EXPECT_EQ(
        validate_authored_layer_source(shifted_x, layout).error(),
        AuthoredLayerSourceError::InvalidPatchGeometry);

    const auto negative_x = make_patch<Coord32>(-1, 0, 2, 2, 100, 200);
    EXPECT_EQ(
        validate_authored_layer_source(negative_x, layout).error(),
        AuthoredLayerSourceError::InvalidPatchGeometry);

    const auto shifted_y = make_patch<Coord32>(0, 1, 3, 3, 100, 200);
    EXPECT_EQ(
        validate_authored_layer_source(shifted_y, layout).error(),
        AuthoredLayerSourceError::InvalidPatchGeometry);
}


TEST(AuthoredLayerSource, EmptyLocalAreaIsRejected)
{
    const auto layout = make_layout(1, 1, 100, 200);

    const Patch<Coord32> patch(
        k_patch_id,
        "EmptyPatch",
        Coord32{100, 200},
        landor::geo::Area32 {},   // default-constructed: empty
        std::span<const LayerBinding>{k_bindings});

    EXPECT_EQ(
        validate_authored_layer_source(patch, layout).error(),
        AuthoredLayerSourceError::InvalidPatchGeometry);
}


// --- Dimensions ---------------------------------------------------------------

TEST(AuthoredLayerSource, DimensionMismatchIsReportedIndependentlyPerAxis)
{
    // The Patch local extent is 4 x 3 (from (0,0) to (3,2)).
    const auto patch = make_patch<Coord32>(0, 0, 3, 2, 100, 200);

    EXPECT_EQ(   // width too small
        validate_authored_layer_source(patch, make_layout(3, 3, 100, 200)).error(),
        AuthoredLayerSourceError::DimensionMismatch);
    EXPECT_EQ(   // width too large
        validate_authored_layer_source(patch, make_layout(5, 3, 100, 200)).error(),
        AuthoredLayerSourceError::DimensionMismatch);
    EXPECT_EQ(   // height too small
        validate_authored_layer_source(patch, make_layout(4, 2, 100, 200)).error(),
        AuthoredLayerSourceError::DimensionMismatch);
    EXPECT_EQ(   // height too large
        validate_authored_layer_source(patch, make_layout(4, 4, 100, 200)).error(),
        AuthoredLayerSourceError::DimensionMismatch);
}


// --- Natural position ---------------------------------------------------------

TEST(AuthoredLayerSource, PositionMismatchIsReportedIndependentlyPerAxis)
{
    const auto patch = make_patch<Coord32>(0, 0, 3, 2, 100, 200);

    EXPECT_EQ(   // x only
        validate_authored_layer_source(patch, make_layout(4, 3, 101, 200)).error(),
        AuthoredLayerSourceError::PositionMismatch);
    EXPECT_EQ(   // y only
        validate_authored_layer_source(patch, make_layout(4, 3, 100, 201)).error(),
        AuthoredLayerSourceError::PositionMismatch);
}


// --- Narrow coordinate types ----------------------------------------------------

TEST(AuthoredLayerSource, NarrowCoordinateTypesValidateWithTheSameContract)
{
    // Position (100, 50) fits in every supported coordinate width.
    const auto layout = make_layout(4, 3, 100, 50);

    EXPECT_TRUE(validate_authored_layer_source(
        make_patch<Coord8>(0, 0, 3, 2, 100, 50), layout).has_value());
    EXPECT_TRUE(validate_authored_layer_source(
        make_patch<Coord16>(0, 0, 3, 2, 100, 50), layout).has_value());
    EXPECT_TRUE(validate_authored_layer_source(
        make_patch<Coord32>(0, 0, 3, 2, 100, 50), layout).has_value());
}


TEST(AuthoredLayerSource, SourcePositionIsComparedWithoutNarrowing)
{
    // The pinned example: 128 does not fit in int8_t, so the check must
    // report a mismatch, not wrap 128 to -128.
    EXPECT_EQ(
        validate_authored_layer_source(
            make_patch<Coord8>(0, 0, 3, 2, 127, 0),
            make_layout(4, 3, 128, 0)).error(),
        AuthoredLayerSourceError::PositionMismatch);

    // The dangerous twin: -128 and 128 are the same byte, so a comparison
    // that narrows the source P into Coord8 would see them as equal. The
    // widened comparison must not.
    EXPECT_EQ(
        validate_authored_layer_source(
            make_patch<Coord8>(0, 0, 3, 2, -128, 0),
            make_layout(4, 3, 128, 0)).error(),
        AuthoredLayerSourceError::PositionMismatch);

    // An in-range negative position still validates on both sides.
    EXPECT_TRUE(validate_authored_layer_source(
        make_patch<Coord8>(0, 0, 3, 2, -5, 0),
        make_layout(4, 3, -5, 0)).has_value());
}


// --- Boundary dimensions ---------------------------------------------------------

TEST(AuthoredLayerSource, FullExtentOfNarrowCoordinateTypesIsSupported)
{
    // A Coord8 area covering the full 128 x 128 extent is expressible by
    // its corners, even though Area<Coord8>::width()/height() themselves
    // wrap for such an area (127 - 0 + 1 does not fit in int8_t and
    // reports -128). That is a separate Area accessor limitation; the
    // validator computes the extent from the corners in a wide
    // intermediate, so the contract holds.
    EXPECT_TRUE(validate_authored_layer_source(
        make_patch<Coord8>(0, 0, 127, 127, 0, 0),
        make_layout(128, 128, 0, 0)).has_value());

    // The same shape at Coord16 width: 32768 x 32768.
    EXPECT_TRUE(validate_authored_layer_source(
        make_patch<Coord16>(0, 0, 32767, 32767, 0, 0),
        make_layout(32768, 32768, 0, 0)).has_value());

    // At the default width the extent may exceed the scalar maximum
    // itself: a Coord32 area ending at int32 max on x has width 2^31,
    // which does not fit in int32_t but does in the uint32_t D value.
    const auto patch = make_patch<Coord32>(
        0, 0, std::numeric_limits<std::int32_t>::max(), 0, 0, 0);
    EXPECT_TRUE(validate_authored_layer_source(
        patch, make_layout(2147483648u, 1, 0, 0)).has_value());

    // Exact equality still applies at the boundary: one fewer cell in D
    // does not validate.
    EXPECT_EQ(
        validate_authored_layer_source(
            make_patch<Coord8>(0, 0, 127, 127, 0, 0),
            make_layout(127, 128, 0, 0)).error(),
        AuthoredLayerSourceError::DimensionMismatch);
}

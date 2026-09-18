// Behavioural tests for the Map area <-> dense v1 runtime layer source
// geometry contract, and for the Map runtime binding catalogue
// precondition.
//
// The unit under test is the pure validation seam: plainly constructed
// Area, LayerSourceLayout and RuntimeLayerBinding values. No Storage, no
// parsing, no Patch: the runtime contract is world-oriented and deliberately
// carries no Patch or Placement semantics.

#include <gtest/gtest.h>

#include "world/area.hpp"
#include "world/runtime_layer_source.hpp"
#include "storage/types.hpp"

#include <array>
#include <cstdint>
#include <span>


namespace
{

using landor::geo::Area;
using landor::geo::Coord8;
using landor::geo::Coord16;
using landor::geo::Coord32;
using landor::geo::LayerSourceLayout;
using landor::geo::RuntimeLayerBinding;
using landor::geo::RuntimeLayerSourceError;
using landor::geo::valid_runtime_layer_bindings;
using landor::geo::validate_runtime_layer_source;
using landor::storage::SourceId;


/// Byte-sized test layers for the binding-catalogue precondition.
struct Terrain
{
    static constexpr landor::geo::LayerId id = 1;
    using value_type = std::uint8_t;
};

struct Fire
{
    static constexpr landor::geo::LayerId id = 2;
    using value_type = std::uint8_t;
};

struct Water
{
    static constexpr landor::geo::LayerId id = 3;
    using value_type = std::uint8_t;
};

static_assert(landor::geo::Layer<Terrain>);
static_assert(landor::geo::Layer<Fire>);
static_assert(landor::geo::Layer<Water>);


/**
 * Construct a Map area from scalar corners of the area's own coordinate
 * width, so the same helper drives the narrow-type tests.
 */
template<typename CoordT>
Area<CoordT> make_area(
    typename CoordT::scalar_type min_x,
    typename CoordT::scalar_type min_y,
    typename CoordT::scalar_type max_x,
    typename CoordT::scalar_type max_y)
{
    return Area<CoordT>{
        CoordT{min_x, min_y},
        CoordT{max_x, max_y}
    };
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


// --- Valid geometry ---------------------------------------------------------

TEST(RuntimeLayerSource, ValidGeometryPasses)
{
    // Map area min (-10, 20), max (-7, 22): extent 4 x 3, origin (-10, 20).
    // The negative, non-zero origin is deliberate: the runtime source is
    // anchored at the Map minimum, not at (0, 0).
    const auto area = make_area<Coord32>(-10, 20, -7, 22);
    const auto layout = make_layout(4, 3, -10, 20);

    EXPECT_TRUE(validate_runtime_layer_source(area, layout).has_value());
}


// --- Empty Map --------------------------------------------------------------

TEST(RuntimeLayerSource, EmptyMapAreaIsRejected)
{
    // A dense runtime source cannot represent an empty Map.
    const auto layout = make_layout(4, 3, 0, 0);

    EXPECT_EQ(
        validate_runtime_layer_source(Area<Coord32> {}, layout).error(),
        RuntimeLayerSourceError::InvalidMapGeometry);
}


// --- Dimensions ---------------------------------------------------------------

TEST(RuntimeLayerSource, DimensionMismatchIsReportedIndependentlyPerAxis)
{
    // Map extent is 4 x 3 (from (-10, 20) to (-7, 22)).
    const auto area = make_area<Coord32>(-10, 20, -7, 22);

    EXPECT_EQ(   // width too small
        validate_runtime_layer_source(area, make_layout(3, 3, -10, 20)).error(),
        RuntimeLayerSourceError::DimensionMismatch);
    EXPECT_EQ(   // width too large
        validate_runtime_layer_source(area, make_layout(5, 3, -10, 20)).error(),
        RuntimeLayerSourceError::DimensionMismatch);
    EXPECT_EQ(   // height too small
        validate_runtime_layer_source(area, make_layout(4, 2, -10, 20)).error(),
        RuntimeLayerSourceError::DimensionMismatch);
    EXPECT_EQ(   // height too large
        validate_runtime_layer_source(area, make_layout(4, 4, -10, 20)).error(),
        RuntimeLayerSourceError::DimensionMismatch);
}


// --- Position -----------------------------------------------------------------

TEST(RuntimeLayerSource, PositionMismatchIsReportedIndependentlyPerAxis)
{
    const auto area = make_area<Coord32>(-10, 20, -7, 22);

    EXPECT_EQ(   // x only
        validate_runtime_layer_source(area, make_layout(4, 3, -9, 20)).error(),
        RuntimeLayerSourceError::PositionMismatch);
    EXPECT_EQ(   // y only
        validate_runtime_layer_source(area, make_layout(4, 3, -10, 21)).error(),
        RuntimeLayerSourceError::PositionMismatch);
}


// --- Narrow coordinate types ----------------------------------------------------

TEST(RuntimeLayerSource, FullExtentOfNarrowCoordinateTypesIsSupported)
{
    // A Coord8 area covering the full 128 x 128 extent is expressible by
    // its corners, even though Area<Coord8>::width()/height() themselves
    // wrap for such an area (127 - 0 + 1 does not fit in int8_t and
    // reports -128). That Area accessor limitation is separate from this
    // contract; the validator computes the extent from the corners in a
    // wide intermediate, so the contract holds without touching the
    // accessors.
    EXPECT_TRUE(validate_runtime_layer_source(
        make_area<Coord8>(0, 0, 127, 127),
        make_layout(128, 128, 0, 0)).has_value());

    // The same boundary at Coord16 width, degenerate in one axis:
    // extent 32768 x 1.
    EXPECT_TRUE(validate_runtime_layer_source(
        make_area<Coord16>(0, 0, 32767, 0),
        make_layout(32768, 1, 0, 0)).has_value());

    // Exact equality still applies at the boundary: one fewer cell in D
    // does not validate.
    EXPECT_EQ(
        validate_runtime_layer_source(
            make_area<Coord8>(0, 0, 127, 127),
            make_layout(127, 128, 0, 0)).error(),
        RuntimeLayerSourceError::DimensionMismatch);
}


TEST(RuntimeLayerSource, SourcePositionIsComparedWithoutNarrowing)
{
    // 128 does not fit in int8_t, so the check must report a mismatch, not
    // wrap 128 to -128 before comparing it against the Map origin.
    const auto area = make_area<Coord8>(0, 0, 127, 127);
    EXPECT_EQ(
        validate_runtime_layer_source(area, make_layout(128, 128, 128, 0)).error(),
        RuntimeLayerSourceError::PositionMismatch);

    // The dangerous twin: -128 and 128 are the same byte, so a comparison
    // that narrows the source P into Coord8 would see them as equal. The
    // widened comparison must not.
    const auto negative_origin = make_area<Coord8>(-128, 0, 127, 127);
    EXPECT_EQ(
        validate_runtime_layer_source(
            negative_origin, make_layout(256, 128, 128, 0)).error(),
        RuntimeLayerSourceError::PositionMismatch);

    // An in-range negative origin still validates on both sides.
    EXPECT_TRUE(validate_runtime_layer_source(
        make_area<Coord8>(-5, -5, -2, -3),
        make_layout(4, 3, -5, -5)).has_value());
}


// --- Binding catalogue precondition ------------------------------------------

TEST(RuntimeLayerSource, BindingCatalogueRequiresSupportedUniqueLayers)
{
    // An empty span is always valid: the Map simply has no runtime overlay
    // source today.
    EXPECT_TRUE(valid_runtime_layer_bindings<Terrain>({}));

    // Each supported layer appears at most once.
    const RuntimeLayerBinding ok[2] {
        {Terrain::id, SourceId{0}},
        {Fire::id, SourceId{1}}
    };
    EXPECT_TRUE((valid_runtime_layer_bindings<Terrain, Fire>(std::span{ok})));

    // Duplicate LayerId: invalid configuration, not a precedence case.
    const RuntimeLayerBinding duplicate[2] {
        {Terrain::id, SourceId{0}},
        {Terrain::id, SourceId{1}}
    };
    EXPECT_FALSE((valid_runtime_layer_bindings<Terrain, Fire>(std::span{duplicate})));

    // A layer the Map does not support cannot be bound.
    const RuntimeLayerBinding unsupported[1] {
        {Water::id, SourceId{0}}
    };
    EXPECT_FALSE((valid_runtime_layer_bindings<Terrain, Fire>(std::span{unsupported})));

}

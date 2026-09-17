#include <gtest/gtest.h>

#include "world/chunk.hpp"

#include <cstdint>


namespace
{

struct GroundLayer
{
    static constexpr landor::geo::LayerId id = 1;
    using value_type = std::uint8_t;
};

struct FireLayer
{
    static constexpr landor::geo::LayerId id = 2;
    using value_type = std::uint8_t;
};

using Chunk32 = landor::geo::Chunk<landor::geo::Coord32>;

} // namespace


TEST(Chunk, PreservesLayerOriginAndSide)
{
    const Chunk32 chunk {GroundLayer::id, landor::geo::Coord32 {32, -64}, 32};

    EXPECT_EQ(chunk.layer(), GroundLayer::id);
    EXPECT_EQ(chunk.origin(), landor::geo::Coord32(32, -64));
    EXPECT_EQ(chunk.side(), 32U);
    EXPECT_FALSE(chunk.is_empty());
}


TEST(Chunk, AreaAndContainsUseInclusiveAlignedSquare)
{
    const Chunk32 chunk {GroundLayer::id, landor::geo::Coord32 {32, 64}, 32};

    EXPECT_EQ(
        chunk.area(),
        landor::geo::Area32(landor::geo::Coord32(32, 64), landor::geo::Coord32(63, 95)));
    EXPECT_TRUE(chunk.contains(landor::geo::Coord32(32, 64)));
    EXPECT_TRUE(chunk.contains(landor::geo::Coord32(63, 95)));
    EXPECT_FALSE(chunk.contains(landor::geo::Coord32(64, 95)));
    EXPECT_FALSE(chunk.contains(landor::geo::Coord32(63, 96)));
}


TEST(Chunk, AlignedRecognisesCanonicalOrigins)
{
    EXPECT_TRUE(Chunk32(GroundLayer::id, landor::geo::Coord32(0, 0), 32).aligned());
    EXPECT_TRUE(Chunk32(GroundLayer::id, landor::geo::Coord32(-32, 64), 32).aligned());
    EXPECT_FALSE(Chunk32(GroundLayer::id, landor::geo::Coord32(1, 0), 32).aligned());
    EXPECT_FALSE(Chunk32(GroundLayer::id, landor::geo::Coord32(-31, 64), 32).aligned());
}


TEST(Chunk, ContainingAlignsPositiveAndBoundaryCoordinates)
{
    EXPECT_EQ(
        Chunk32::containing<GroundLayer>(landor::geo::Coord32(0, 0), 32).origin(),
        landor::geo::Coord32(0, 0));
    EXPECT_EQ(
        Chunk32::containing<GroundLayer>(landor::geo::Coord32(31, 31), 32).origin(),
        landor::geo::Coord32(0, 0));
    EXPECT_EQ(
        Chunk32::containing<GroundLayer>(landor::geo::Coord32(32, 32), 32).origin(),
        landor::geo::Coord32(32, 32));
}


TEST(Chunk, ContainingUsesFloorAlignmentForNegativeCoordinates)
{
    EXPECT_EQ(
        Chunk32::containing<GroundLayer>(landor::geo::Coord32(-1, -1), 32).origin(),
        landor::geo::Coord32(-32, -32));
    EXPECT_EQ(
        Chunk32::containing<GroundLayer>(landor::geo::Coord32(-32, -32), 32).origin(),
        landor::geo::Coord32(-32, -32));
    EXPECT_EQ(
        Chunk32::containing<GroundLayer>(landor::geo::Coord32(-33, -33), 32).origin(),
        landor::geo::Coord32(-64, -64));
}


TEST(Chunk, LayerIdentityDoesNotChangeSpatialGeometry)
{
    const auto ground = Chunk32::containing<GroundLayer>(landor::geo::Coord32(17, 40), 32);
    const auto fire = Chunk32::containing<FireLayer>(landor::geo::Coord32(17, 40), 32);

    EXPECT_NE(ground.layer(), fire.layer());
    EXPECT_EQ(ground.origin(), fire.origin());
    EXPECT_EQ(ground.side(), fire.side());
    EXPECT_EQ(ground.area(), fire.area());
}


TEST(Chunk, ZeroSideIsEmptyUnalignedAndContainsNothing)
{
    const Chunk32 chunk {GroundLayer::id, landor::geo::Coord32 {5, -7}, 0};

    EXPECT_TRUE(chunk.is_empty());
    EXPECT_FALSE(chunk.aligned());
    EXPECT_TRUE(chunk.area().is_empty());
    EXPECT_FALSE(chunk.contains(landor::geo::Coord32(5, -7)));

    const auto containing =
        Chunk32::containing<GroundLayer>(landor::geo::Coord32(5, -7), 0);
    EXPECT_EQ(containing.origin(), landor::geo::Coord32(5, -7));
    EXPECT_TRUE(containing.is_empty());
}

#include <gtest/gtest.h>

#include "world/tile.hpp"

#include <array>
#include <cstdint>
#include <type_traits>


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

struct UnknownLayer
{
    static constexpr landor::geo::LayerId id = 99;
    using value_type = std::uint8_t;
};

inline constexpr GroundLayer Ground {};
inline constexpr FireLayer Fire {};
inline constexpr UnknownLayer Unknown {};

using TestTile = landor::geo::Tile<landor::geo::Coord32, GroundLayer, FireLayer>;

template<typename TileT>
concept HasUnknownLayerAccess = requires(TileT tile)
{
    tile[Unknown];
};

static_assert(std::is_trivially_copyable_v<TestTile>);
static_assert(!HasUnknownLayerAccess<TestTile>);

} // namespace


TEST(Tile, ConstructionPreservesPositionAndValues)
{
    const TestTile tile {
        landor::geo::Coord32 {23, 14},
        std::array<std::uint8_t, 2> {7, 11}
    };

    EXPECT_EQ(tile.position(), landor::geo::Coord32(23, 14));
    EXPECT_EQ(tile[Ground], 7);
    EXPECT_EQ(tile[Fire], 11);
}


TEST(Tile, GetAndSymbolicAccessAgree)
{
    TestTile tile {
        landor::geo::Coord32 {3, 5},
        std::array<std::uint8_t, 2> {9, 12}
    };

    EXPECT_EQ(tile.get<GroundLayer>(), tile[Ground]);
    EXPECT_EQ(tile.get<FireLayer>(), tile[Fire]);

    tile.get<GroundLayer>() = 21;
    EXPECT_EQ(tile[Ground], 21);
}


TEST(Tile, CopyOwnsIndependentPropertyValues)
{
    const TestTile original {
        landor::geo::Coord32 {-4, 8},
        std::array<std::uint8_t, 2> {5, 6}
    };

    auto copy = original;
    copy[Fire] = 42;

    EXPECT_EQ(copy.position(), original.position());
    EXPECT_EQ(copy[Ground], original[Ground]);
    EXPECT_EQ(copy[Fire], 42);
    EXPECT_EQ(original[Fire], 6);
}

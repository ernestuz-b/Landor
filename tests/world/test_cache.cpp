#include <gtest/gtest.h>

#include "world/cache.hpp"

#include <array>
#include <cstdint>
#include <span>


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

inline constexpr GroundLayer Ground {};
inline constexpr FireLayer Fire {};

using TestCache = landor::geo::Cache<2, 4, landor::geo::Coord32, GroundLayer, FireLayer>;
using Plane = std::array<std::uint8_t, 16>;

[[nodiscard]] constexpr Plane sequence(std::uint8_t base)
{
    Plane values {};

    for (std::size_t i = 0; i < values.size(); ++i)
    {
        values[i] = static_cast<std::uint8_t>(base + static_cast<std::uint8_t>(i));
    }

    return values;
}

} // namespace


TEST(Cache, StartsEmpty)
{
    const TestCache cache;

    EXPECT_FALSE(cache.contains<GroundLayer>(landor::geo::Coord32(0, 0)));
    EXPECT_FALSE(cache.contains<FireLayer>(landor::geo::Coord32(0, 0)));
    EXPECT_FALSE(cache.contains(landor::geo::Coord32(0, 0)));
}


TEST(Cache, FillOneLayerCreatesOneSpatialSlot)
{
    TestCache cache;
    const auto chunk = TestCache::chunk_for<GroundLayer>(landor::geo::Coord32(1, 2));
    const auto values = sequence(10);

    ASSERT_TRUE(cache.fill<GroundLayer>(chunk, values));

    EXPECT_TRUE(cache.contains<GroundLayer>(landor::geo::Coord32(0, 0)));
    EXPECT_TRUE(cache.contains<GroundLayer>(landor::geo::Coord32(3, 3)));
    EXPECT_FALSE(cache.contains<FireLayer>(landor::geo::Coord32(1, 2)));
    EXPECT_FALSE(cache.contains(landor::geo::Coord32(1, 2)));
}


TEST(Cache, ValueUsesDeterministicRowMajorIndexing)
{
    TestCache cache;
    const auto chunk = TestCache::chunk_for<GroundLayer>(landor::geo::Coord32(2, 1));
    const auto values = sequence(20);

    ASSERT_TRUE(cache.fill<GroundLayer>(chunk, values));

    EXPECT_EQ(cache.value<GroundLayer>(landor::geo::Coord32(0, 0)), 20);
    EXPECT_EQ(cache.value<GroundLayer>(landor::geo::Coord32(3, 0)), 23);
    EXPECT_EQ(cache.value<GroundLayer>(landor::geo::Coord32(0, 1)), 24);
    EXPECT_EQ(cache.value<GroundLayer>(landor::geo::Coord32(2, 2)), 30);
    EXPECT_EQ(cache.value<GroundLayer>(landor::geo::Coord32(3, 3)), 35);
}


TEST(Cache, FillingRemainingLayerReusesSameSpatialSlot)
{
    TestCache cache;
    const auto ground_chunk = TestCache::chunk_for<GroundLayer>(landor::geo::Coord32(1, 1));
    const auto fire_chunk = TestCache::chunk_for<FireLayer>(landor::geo::Coord32(1, 1));
    const auto ground_values = sequence(1);
    const auto fire_values = sequence(101);

    ASSERT_TRUE(cache.fill<GroundLayer>(ground_chunk, ground_values));
    ASSERT_TRUE(cache.fill<FireLayer>(fire_chunk, fire_values));

    EXPECT_TRUE(cache.contains(landor::geo::Coord32(2, 3)));
    EXPECT_EQ(cache.value<GroundLayer>(landor::geo::Coord32(2, 3)), 15);
    EXPECT_EQ(cache.value<FireLayer>(landor::geo::Coord32(2, 3)), 115);
}


TEST(Cache, TilePacksResidentLayersAndPositionByValue)
{
    TestCache cache;
    const auto ground_chunk = TestCache::chunk_for<GroundLayer>(landor::geo::Coord32(0, 0));
    const auto fire_chunk = TestCache::chunk_for<FireLayer>(landor::geo::Coord32(0, 0));
    const auto ground_values = sequence(5);
    const auto fire_values = sequence(50);

    ASSERT_TRUE(cache.fill<GroundLayer>(ground_chunk, ground_values));
    ASSERT_TRUE(cache.fill<FireLayer>(fire_chunk, fire_values));

    auto tile = cache.tile(landor::geo::Coord32(2, 1));

    EXPECT_EQ(tile.position(), landor::geo::Coord32(2, 1));
    EXPECT_EQ(tile[Ground], 11);
    EXPECT_EQ(tile[Fire], 56);

    tile[Fire] = 200;
    EXPECT_EQ(cache.value<FireLayer>(landor::geo::Coord32(2, 1)), 56);
}


TEST(Cache, SupportsMultipleSpatialSlots)
{
    TestCache cache;
    const auto first = TestCache::chunk_for<GroundLayer>(landor::geo::Coord32(0, 0));
    const auto second = TestCache::chunk_for<GroundLayer>(landor::geo::Coord32(4, 0));
    const auto first_values = sequence(1);
    const auto second_values = sequence(40);

    ASSERT_TRUE(cache.fill<GroundLayer>(first, first_values));
    ASSERT_TRUE(cache.fill<GroundLayer>(second, second_values));

    EXPECT_EQ(cache.value<GroundLayer>(landor::geo::Coord32(3, 3)), 16);
    EXPECT_EQ(cache.value<GroundLayer>(landor::geo::Coord32(4, 0)), 40);
}


TEST(Cache, CapacityExhaustionFailsWithoutEviction)
{
    TestCache cache;
    const auto first = TestCache::chunk_for<GroundLayer>(landor::geo::Coord32(0, 0));
    const auto second = TestCache::chunk_for<GroundLayer>(landor::geo::Coord32(4, 0));
    const auto third = TestCache::chunk_for<GroundLayer>(landor::geo::Coord32(8, 0));
    const auto fire_first = TestCache::chunk_for<FireLayer>(landor::geo::Coord32(0, 0));
    const auto values = sequence(1);

    ASSERT_TRUE(cache.fill<GroundLayer>(first, values));
    ASSERT_TRUE(cache.fill<GroundLayer>(second, values));

    EXPECT_FALSE(cache.fill<GroundLayer>(third, values));
    EXPECT_FALSE(cache.contains<GroundLayer>(landor::geo::Coord32(8, 0)));

    EXPECT_TRUE(cache.fill<FireLayer>(fire_first, values));
    EXPECT_TRUE(cache.contains<FireLayer>(landor::geo::Coord32(0, 0)));
}


TEST(Cache, FillRejectsWrongLayerGeometryAndValueCount)
{
    TestCache cache;
    const auto canonical = TestCache::chunk_for<GroundLayer>(landor::geo::Coord32(0, 0));
    const landor::geo::Chunk<landor::geo::Coord32> wrong_layer {
        FireLayer::id,
        canonical.origin(),
        canonical.side()
    };
    const landor::geo::Chunk<landor::geo::Coord32> wrong_side {
        GroundLayer::id,
        canonical.origin(),
        8
    };
    const landor::geo::Chunk<landor::geo::Coord32> unaligned {
        GroundLayer::id,
        landor::geo::Coord32(1, 0),
        4
    };
    const auto values = sequence(1);
    const std::span<const std::uint8_t> short_values(values.data(), values.size() - 1);

    EXPECT_FALSE(cache.fill<GroundLayer>(wrong_layer, values));
    EXPECT_FALSE(cache.fill<GroundLayer>(wrong_side, values));
    EXPECT_FALSE(cache.fill<GroundLayer>(unaligned, values));
    EXPECT_FALSE(cache.fill<GroundLayer>(canonical, short_values));
    EXPECT_FALSE(cache.contains<GroundLayer>(landor::geo::Coord32(0, 0)));
}


TEST(Cache, InvalidateDropsOnlyIntersectingSpatialSlots)
{
    TestCache cache;
    const auto first = TestCache::chunk_for<GroundLayer>(landor::geo::Coord32(0, 0));
    const auto second = TestCache::chunk_for<GroundLayer>(landor::geo::Coord32(4, 0));
    const auto values = sequence(1);

    ASSERT_TRUE(cache.fill<GroundLayer>(first, values));
    ASSERT_TRUE(cache.fill<GroundLayer>(second, values));

    cache.invalidate(landor::geo::Area32(
        landor::geo::Coord32(2, 1),
        landor::geo::Coord32(2, 1)));

    EXPECT_FALSE(cache.contains<GroundLayer>(landor::geo::Coord32(0, 0)));
    EXPECT_TRUE(cache.contains<GroundLayer>(landor::geo::Coord32(4, 0)));
}


TEST(Cache, InvalidateAllDropsEverySpatialSlot)
{
    TestCache cache;
    const auto first = TestCache::chunk_for<GroundLayer>(landor::geo::Coord32(0, 0));
    const auto second = TestCache::chunk_for<GroundLayer>(landor::geo::Coord32(4, 0));
    const auto values = sequence(1);

    ASSERT_TRUE(cache.fill<GroundLayer>(first, values));
    ASSERT_TRUE(cache.fill<GroundLayer>(second, values));

    cache.invalidate_all();

    EXPECT_FALSE(cache.contains<GroundLayer>(landor::geo::Coord32(0, 0)));
    EXPECT_FALSE(cache.contains<GroundLayer>(landor::geo::Coord32(4, 0)));
}


TEST(Cache, NegativeCoordinatesUseCanonicalFloorAlignedSlot)
{
    TestCache cache;
    const auto chunk = TestCache::chunk_for<GroundLayer>(landor::geo::Coord32(-1, -1));
    const auto values = sequence(10);

    ASSERT_EQ(chunk.origin(), landor::geo::Coord32(-4, -4));
    ASSERT_TRUE(cache.fill<GroundLayer>(chunk, values));

    EXPECT_TRUE(cache.contains<GroundLayer>(landor::geo::Coord32(-1, -1)));
    EXPECT_EQ(cache.value<GroundLayer>(landor::geo::Coord32(-4, -4)), 10);
    EXPECT_EQ(cache.value<GroundLayer>(landor::geo::Coord32(-1, -1)), 25);
}

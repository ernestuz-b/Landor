// Compile tripwire for the whole header set.
//
// Every design header under src/ lands in one translation unit here. The
// headers are interdependent contracts, so this file fails when a header
// changes namespace, drops an include or changes a public type seam without
// its users being updated.

#include <gtest/gtest.h>

#include "platform/storage/storage_filesystem.hpp"
#include "storage/storage_contract.hpp"
#include "storage/types.hpp"

#include "world/area.hpp"
#include "world/authored_layer_source.hpp"
#include "world/cache.hpp"
#include "world/chunk.hpp"
#include "world/coord.hpp"
#include "world/layer.hpp"
#include "world/layer_fallback.hpp"
#include "world/layer_source.hpp"
#include "world/map.hpp"
#include "world/map_result.hpp"
#include "world/orientation.hpp"
#include "world/patch.hpp"
#include "world/patchset.hpp"
#include "world/place.hpp"
#include "world/placement.hpp"
#include "world/tile.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <type_traits>
#include <utility>

namespace
{

/// Minimal layer satisfying landor::geo::Layer, used to instantiate Tile/Map.
struct Fire
{
    static constexpr landor::geo::LayerId id = 3;
    using value_type = std::uint8_t;
};

static_assert(landor::geo::Layer<Fire>);

/// Constant terminal fallback provider for the test Map. It supplies the
/// exact value type of the test layer at the Map's coordinate type, which is
/// all the LayerFallbackFor contract asks of a provider.
struct TestFallback
{
    template<typename LayerT>
    [[nodiscard]]
    typename LayerT::value_type
    value(landor::geo::Coord32) const
    {
        return {};
    }
};

static_assert(
    landor::geo::LayerFallbackProvider<TestFallback, landor::geo::Coord32, Fire>);

using TestMap = landor::geo::Map<8, 16, 32, TestFallback, landor::geo::Coord32, Fire>;

} // namespace


TEST(WorldHeaders, DefaultTemplateArgumentsResolveInsideGeo)
{
    using Patch32 = landor::geo::Patch<>;
    using Placement32 = landor::geo::Placement<>;

    static_assert(std::is_same_v<Patch32::coord_type, landor::geo::Coord32>);
    static_assert(std::is_same_v<Patch32::area_type, landor::geo::Area32>);
    static_assert(std::is_same_v<Placement32::coord_type, landor::geo::Coord32>);
    SUCCEED();
}


TEST(WorldHeaders, MapAliasesAgreeWithGeometryTypes)
{
    static_assert(std::is_same_v<TestMap::coord_type, landor::geo::Coord32>);
    static_assert(std::is_same_v<TestMap::area_type, landor::geo::Area32>);
    static_assert(std::is_same_v<TestMap::patch_type, landor::geo::Patch<>>);
    static_assert(std::is_same_v<TestMap::placement_type, landor::geo::Placement<>>);
    static_assert(std::is_same_v<TestMap::fallback_type, TestFallback>);
    static_assert(
        std::is_same_v<TestMap::tile_type, landor::geo::Tile<landor::geo::Coord32, Fire>>);
    SUCCEED();
}


TEST(WorldHeaders, MapConstructorBorrowsStorageAndFallback)
{
    // Pins the Map construction seam: the constructor takes the
    // build-selected landor::storage::Storage reference and a const-borrowed
    // terminal fallback provider. The old constructor without the fallback
    // is no longer the contract.
    static_assert(
        std::is_constructible_v<
            TestMap,
            landor::geo::MapId,
            TestMap::area_type,
            std::span<const TestMap::patch_type>,
            landor::storage::Storage&,
            const TestMap::fallback_type&>);
    static_assert(
        !std::is_constructible_v<
            TestMap,
            landor::geo::MapId,
            TestMap::area_type,
            std::span<const TestMap::patch_type>,
            landor::storage::Storage&>);
    SUCCEED();
}


TEST(WorldHeaders, PlaceResolvesGeoIdentifiers)
{
    using PlaceIds = std::span<const landor::geo::PlacementId>;

    static_assert(std::is_same_v<decltype(std::declval<const landor::world::Place&>().placements()),
                                 PlaceIds>);
    SUCCEED();
}


TEST(WorldHeaders, BuildSelectedStorageSatisfiesTheContract)
{
    static_assert(landor::storage::StorageBackend<landor::storage::Storage>);
    static_assert(std::is_same_v<landor::storage::Storage, landor::storage::StorageFilesystem>);
    SUCCEED();
}


TEST(WorldHeaders, TileCarriesLayerValuesAndPosition)
{
    const landor::geo::Tile<landor::geo::Coord32, Fire> tile {
        landor::geo::Coord32 {7, 9},
        std::array<std::uint8_t, 1> {11}
    };

    EXPECT_EQ(tile.position(), landor::geo::Coord32(7, 9));
    EXPECT_EQ(tile.get<Fire>(), 11);
}

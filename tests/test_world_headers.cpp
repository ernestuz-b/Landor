// Compile tripwire for the whole header set.
//
// Every design header under src/ lands in one translation unit here. The
// headers are interdependent contracts — Patch needs Area and Coord32, Map
// needs Patch, Placement, Tile and Layer — so this file fails the moment a
// header changes namespace, drops an #include or renames a type. Per-header
// tests cannot see that class of breakage because each one only pulls in what
// it already knows about; this TU is the architectural guard the build relies
// on before any implementation exists.
//
// The assertions are type identities, not behaviour: if a member declaration
// stops parsing, instantiating the class type reports it. Behaviour lives in
// test_geo_headers.cpp and friends.

#include <gtest/gtest.h>

#include "platform/storage/storage_filesystem.hpp"
#include "storage/storage_contract.hpp"
#include "storage/types.hpp"

#include "world/area.hpp"
#include "world/cache.hpp"
#include "world/chunk.hpp"
#include "world/coord.hpp"
#include "world/layer.hpp"
#include "world/map.hpp"
#include "world/orientation.hpp"
#include "world/patch.hpp"
#include "world/patchset.hpp"
#include "world/place.hpp"
#include "world/placement.hpp"
#include "world/tile.hpp"

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

using TestMap = landor::geo::Map<8, 16, landor::geo::Coord32, Fire>;

} // namespace

/** The default template arguments of the geo contracts must resolve.
 *
 * `Patch<>` and `Placement<>` are spelled with no argument on purpose: their
 * defaults are written as bare `Coord32` inside `landor::geo`, which only name
 * a type once coord.hpp and area.hpp live in that namespace. This is exactly
 * the breakage the migration left behind on 2026-09-16.
 */
TEST(WorldHeaders, DefaultTemplateArgumentsResolveInsideGeo)
{
    using Patch32 = landor::geo::Patch<>;
    using Placement32 = landor::geo::Placement<>;

    static_assert(std::is_same_v<Patch32::coord_type, landor::geo::Coord32>);
    static_assert(std::is_same_v<Patch32::area_type, landor::geo::Area32>);
    static_assert(std::is_same_v<Placement32::coord_type, landor::geo::Coord32>);
    SUCCEED();
}

/// Map re-exports the geometry types; all four aliases must agree on one CoordT.
TEST(WorldHeaders, MapAliasesAgreeWithGeometryTypes)
{
    static_assert(std::is_same_v<TestMap::coord_type, landor::geo::Coord32>);
    static_assert(std::is_same_v<TestMap::area_type, landor::geo::Area32>);
    static_assert(std::is_same_v<TestMap::patch_type, landor::geo::Patch<>>);
    static_assert(std::is_same_v<TestMap::placement_type, landor::geo::Placement<>>);
    static_assert(std::is_same_v<TestMap::tile_type, landor::geo::Tile<Fire>>);
    SUCCEED();
}

/// The world-facing layer refers to geo ids through landor::geo, not a flat Geo.
TEST(WorldHeaders, PlaceResolvesGeoIdentifiers)
{
    using PlaceIds = std::span<const landor::geo::PlacementId>;

    static_assert(std::is_same_v<decltype(std::declval<const landor::world::Place&>().placements()),
                                 PlaceIds>);
    SUCCEED();
}

// Note: a negative assertion of the form "`Geo::Coord32` must not resolve" is
// not expressible — an undeclared name inside a requires-expression is a hard
// error, not substitution failure. The positive aliases above are the guard:
// they only compile while every contract lives in landor::geo.

/** The storage seam Map will consume must exist and satisfy its contract.
 *
 * `storage::Storage` is a build-chosen alias (`StorageFilesystem` today). Map
 * forward-declares its own `geo::Storage` (see STATUS.md §6), so until Map is
 * repointed this assertion is the only thing checking the two sides meet.
 */
TEST(WorldHeaders, BuildSelectedStorageSatisfiesTheContract)
{
    static_assert(landor::storage::StorageBackend<landor::storage::Storage>);
    static_assert(std::is_same_v<landor::storage::Storage, landor::storage::StorageFilesystem>);
    SUCCEED();
}

/// A Tile round-trips a layer value, proving tile.hpp and layer.hpp compose.
TEST(WorldHeaders, TileCarriesLayerValues)
{
    const landor::geo::Tile<Fire> tile{landor::geo::LayerValue<Fire>{7}};

    EXPECT_EQ(tile.get<Fire>(), 7);
}

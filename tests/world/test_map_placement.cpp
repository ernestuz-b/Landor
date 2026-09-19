// Behavioural tests for the Map placement lifecycle implemented in
// src/world/map.hpp:
//
//     place()        PatchId validation, slot allocation, id assignment
//     placement()    const lookup by PlacementId
//     set_position() / set_rotation() / set_reflection() / set_orientation()
//
// The pinned contract:
//
// - place() resolves the PatchId through Map::patch(); an unknown PatchId
//   yields MapPlacementError::UnknownPatch, checked before capacity and
//   before dirty state, so a full Map or a dirty Cache never hides an
//   invalid PatchId.
// - A natural place() creates the occurrence at Patch::natural_position()
//   with the default Orientation.
// - Successful placements receive monotonically increasing PlacementIds
//   starting at one; ids are never reused. The PlacementId is assigned only
//   after the whole-cache invalidation has succeeded, so a refused
//   invalidation (MapPlacementError::DirtyState) creates no Placement and
//   consumes no id.
// - Transform mutation returns MapPlacementMutationResult:
//   UnknownPlacement for an unknown id, DirtyState when the whole-cache
//   invalidation would discard dirty resident state. Invalidation happens
//   before the Placement is modified; a no-op transform request needs no
//   invalidation and succeeds even while dirty state exists.
//
// Invalidation is internal Map policy. These tests observe Placements only
// through the public Map API and never touch the Cache, and they never
// depend on array slot addresses.

#include <gtest/gtest.h>

#include "platform/storage/storage_filesystem.hpp"
#include "world/coord.hpp"
#include "world/map.hpp"
#include "world/orientation.hpp"
#include "world/patch.hpp"
#include "world/placement.hpp"
#include "world/runtime_layer_source.hpp"

#include <array>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>


namespace
{

using landor::geo::Area32;
using landor::geo::Coord32;
using landor::geo::LayerBinding;
using landor::geo::Orientation;
using Patch = landor::geo::Patch<>;
using landor::geo::PatchId;
using landor::geo::PlacementId;
using landor::geo::Reflection;
using landor::geo::Rotation;
using landor::geo::RuntimeLayerBinding;

/// Byte-sized test layer, matching the header-tripwire test layer.
struct Fire
{
    static constexpr landor::geo::LayerId id = 3;
    using value_type = std::uint8_t;
};

static_assert(landor::geo::Layer<Fire>);

/// Trivial terminal fallback provider satisfying the LayerFallbackProvider
/// concept. The dirty-state tests resolve the Fire layer through it, so the
/// resolved value is zero and any non-zero set() marks the plane dirty.
struct TestFallback
{
    template<typename LayerT>
    [[nodiscard]]
    typename LayerT::value_type
    value(Coord32) const
    {
        return {};
    }
};

static_assert(landor::geo::LayerFallbackProvider<TestFallback, Coord32, Fire>);

/// Small test Map: three placement slots, four cache spatial slots,
/// 8x8 canonical chunks. StorageFilesystem is constructed over an empty
/// source table and no Storage I/O happens in these tests.
using TestMap = landor::geo::Map<3, 4, 8, TestFallback, Coord32, Fire>;


// The creation result API is pinned: expected-based, and separate from the
// checked-world-access MapError domain.
static_assert(std::is_same_v<
    landor::geo::MapPlacementResult,
    std::expected<PlacementId, landor::geo::MapPlacementError>>);

static_assert(std::is_same_v<
    decltype(std::declval<TestMap&>().place(std::declval<PatchId>())),
    landor::geo::MapPlacementResult>);

static_assert(std::is_same_v<
    decltype(std::declval<TestMap&>().place(
        std::declval<PatchId>(),
        std::declval<Coord32>(),
        std::declval<Orientation>())),
    landor::geo::MapPlacementResult>);

static_assert(std::is_same_v<
    decltype(std::declval<const TestMap&>().placement(std::declval<PlacementId>())),
    const landor::geo::Placement<Coord32>*>);

// The transform mutation result API is pinned: expected-based, and separate
// from the creation result and the checked-world-access MapError domain.
static_assert(std::is_same_v<
    landor::geo::MapPlacementMutationResult,
    std::expected<void, landor::geo::MapPlacementMutationError>>);

static_assert(std::is_same_v<
    decltype(std::declval<TestMap&>().set_position(std::declval<PlacementId>(),
                                                    std::declval<Coord32>())),
    landor::geo::MapPlacementMutationResult>);

static_assert(std::is_same_v<
    decltype(std::declval<TestMap&>().set_rotation(std::declval<PlacementId>(),
                                                   std::declval<Rotation>())),
    landor::geo::MapPlacementMutationResult>);

static_assert(std::is_same_v<
    decltype(std::declval<TestMap&>().set_reflection(std::declval<PlacementId>(),
                                                     std::declval<Reflection>())),
    landor::geo::MapPlacementMutationResult>);

static_assert(std::is_same_v<
    decltype(std::declval<TestMap&>().set_orientation(std::declval<PlacementId>(),
                                                      std::declval<Orientation>())),
    landor::geo::MapPlacementMutationResult>);

// Map::set<LayerT>() is the first mutable world-state path and returns the
// checked-access result domain.
static_assert(std::is_same_v<
    decltype(std::declval<TestMap&>().set<Fire>(std::declval<Coord32>(),
                                                std::declval<std::uint8_t>())),
    landor::geo::MapResult<void>>);


[[nodiscard]] Patch make_patch(PatchId id, Coord32 natural_position)
{
    const Area32 local_area(Coord32(0, 0), Coord32(3, 2));
    return Patch(
        id,
        "test-patch",
        natural_position,
        local_area,
        std::span<const LayerBinding> {});
}


struct MapPlacementTest : ::testing::Test
{
    static constexpr PatchId house_id = 7;
    static constexpr PatchId well_id  = 9;
    static constexpr Coord32 house_natural = Coord32(10, 20);
    static constexpr Coord32 well_natural  = Coord32(40, 50);

    // Declared before the Map so the catalogue span stays valid.
    std::array<Patch, 2> patches = {
        make_patch(house_id, house_natural),
        make_patch(well_id, well_natural)
    };

    // Empty source table: these tests perform no Storage I/O.
    landor::storage::Storage storage {
        "unused-root",
        std::span<const std::string_view> {}
    };

    TestFallback fallback {};

    TestMap map {
        1,
        Area32(Coord32(0, 0), Coord32(127, 127)),
        patches,
        storage,
        storage,
        std::span<const RuntimeLayerBinding> {},
        fallback
    };
};


} // namespace


// ---------------------------------------------------------------------------
// Creation
// ---------------------------------------------------------------------------

TEST_F(MapPlacementTest, NaturalPlacementUsesPatchNaturalPosition)
{
    const auto result = map.place(house_id);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(map.placement_count(), 1u);

    const auto* placement = map.placement(*result);
    ASSERT_NE(placement, nullptr);
    EXPECT_EQ(placement->id(), *result);
    EXPECT_EQ(placement->patch(), house_id);
    EXPECT_EQ(placement->position(), house_natural);
    EXPECT_EQ(placement->orientation().rotation, Rotation::none);
    EXPECT_EQ(placement->orientation().reflection, Reflection::none);
}


TEST_F(MapPlacementTest, ExplicitPlacementStoresSuppliedState)
{
    const Coord32 position(33, 44);
    const Orientation orientation {Rotation::r90, Reflection::x};

    const auto result = map.place(well_id, position, orientation);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(map.placement_count(), 1u);

    const auto* placement = map.placement(*result);
    ASSERT_NE(placement, nullptr);
    EXPECT_EQ(placement->id(), *result);
    EXPECT_EQ(placement->patch(), well_id);
    EXPECT_EQ(placement->position(), position);
    EXPECT_EQ(placement->rotation(), Rotation::r90);
    EXPECT_EQ(placement->reflection(), Reflection::x);
}


TEST_F(MapPlacementTest, SamePatchCanBePlacedMultipleTimesIndependently)
{
    const auto first = map.place(house_id);
    const auto second = map.place(
        house_id, Coord32(60, 70), Orientation {Rotation::r180});

    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_NE(*first, *second);
    EXPECT_EQ(map.placement_count(), 2u);

    // Moving the second occurrence must not alter the first.
    ASSERT_TRUE(map.set_position(*second, Coord32(61, 71)).has_value());

    const auto* a = map.placement(*first);
    const auto* b = map.placement(*second);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(a->position(), house_natural);
    EXPECT_EQ(b->position(), Coord32(61, 71));
    EXPECT_EQ(b->rotation(), Rotation::r180);
}


TEST_F(MapPlacementTest, SuccessfulIdsStartAtOneAndIncrease)
{
    const auto a = map.place(house_id);
    const auto b = map.place(well_id);
    const auto c = map.place(house_id);

    ASSERT_TRUE(a.has_value());
    ASSERT_TRUE(b.has_value());
    ASSERT_TRUE(c.has_value());

    EXPECT_EQ(*a, PlacementId(1));
    EXPECT_EQ(*b, PlacementId(2));
    EXPECT_EQ(*c, PlacementId(3));

    const auto check = [this](const landor::geo::MapPlacementResult& result) {
        const auto* placement = map.placement(*result);
        EXPECT_NE(placement, nullptr);

        if (placement != nullptr)
        {
            EXPECT_EQ(placement->id(), *result);
        }
    };

    check(a);
    check(b);
    check(c);
}


TEST_F(MapPlacementTest, UnknownPatchIsReportedByBothOverloads)
{
    const auto natural = map.place(PatchId(42));
    const auto explicit_ = map.place(PatchId(42), Coord32(0, 0));

    ASSERT_FALSE(natural.has_value());
    EXPECT_EQ(natural.error(), landor::geo::MapPlacementError::UnknownPatch);

    ASSERT_FALSE(explicit_.has_value());
    EXPECT_EQ(explicit_.error(), landor::geo::MapPlacementError::UnknownPatch);

    EXPECT_EQ(map.placement_count(), 0u);
}


TEST_F(MapPlacementTest, UnknownPatchIsNotHiddenByFullCapacity)
{
    // Fill all three slots first.
    ASSERT_TRUE(map.place(house_id).has_value());
    ASSERT_TRUE(map.place(well_id).has_value());
    ASSERT_TRUE(map.place(house_id).has_value());

    const auto unknown_when_full = map.place(PatchId(42));

    ASSERT_FALSE(unknown_when_full.has_value());
    EXPECT_EQ(
        unknown_when_full.error(),
        landor::geo::MapPlacementError::UnknownPatch);
}


TEST_F(MapPlacementTest, CapacityFullWhenAllSlotsAreOccupied)
{
    const auto a = map.place(house_id);
    const auto b = map.place(well_id);
    const auto c = map.place(house_id);

    ASSERT_TRUE(a.has_value());
    ASSERT_TRUE(b.has_value());
    ASSERT_TRUE(c.has_value());

    const auto overflow = map.place(well_id, Coord32(0, 0));

    ASSERT_FALSE(overflow.has_value());
    EXPECT_EQ(
        overflow.error(),
        landor::geo::MapPlacementError::CapacityFull);
    EXPECT_EQ(map.placement_count(), 3u);

    // Existing placements are untouched.
    const auto* pa = map.placement(*a);
    const auto* pb = map.placement(*b);
    const auto* pc = map.placement(*c);
    ASSERT_NE(pa, nullptr);
    ASSERT_NE(pb, nullptr);
    ASSERT_NE(pc, nullptr);
    EXPECT_EQ(pa->position(), house_natural);
    EXPECT_EQ(pb->position(), well_natural);
    EXPECT_EQ(pc->position(), house_natural);
    EXPECT_EQ(pa->orientation().rotation, Rotation::none);
}


// ---------------------------------------------------------------------------
// Lookup
// ---------------------------------------------------------------------------

TEST_F(MapPlacementTest, LookupReturnsNullptrForUnknownIds)
{
    EXPECT_EQ(map.placement(PlacementId(0)), nullptr);
    EXPECT_EQ(map.placement(PlacementId(1)), nullptr);
    EXPECT_EQ(map.placement(PlacementId(255)), nullptr);

    const auto placed = map.place(house_id);
    ASSERT_TRUE(placed.has_value());

    EXPECT_NE(map.placement(*placed), nullptr);
    EXPECT_EQ(map.placement(PlacementId(2)), nullptr);
    EXPECT_EQ(map.placement(PlacementId(0)), nullptr);
}


// ---------------------------------------------------------------------------
// Mutation
// ---------------------------------------------------------------------------

TEST_F(MapPlacementTest, SetPositionMovesPlacement)
{
    const auto placed = map.place(house_id);
    ASSERT_TRUE(placed.has_value());

    const Coord32 target(15, 25);
    ASSERT_TRUE(map.set_position(*placed, target).has_value());

    const auto* placement = map.placement(*placed);
    ASSERT_NE(placement, nullptr);
    EXPECT_EQ(placement->position(), target);
    EXPECT_EQ(placement->rotation(), Rotation::none);
    EXPECT_EQ(placement->reflection(), Reflection::none);
    EXPECT_EQ(map.placement_count(), 1u);
}


TEST_F(MapPlacementTest, SetRotationChangesOnlyRotation)
{
    const auto placed = map.place(house_id);
    ASSERT_TRUE(placed.has_value());

    ASSERT_TRUE(map.set_rotation(*placed, Rotation::r180).has_value());

    const auto* placement = map.placement(*placed);
    ASSERT_NE(placement, nullptr);
    EXPECT_EQ(placement->rotation(), Rotation::r180);
    EXPECT_EQ(placement->reflection(), Reflection::none);
    EXPECT_EQ(placement->position(), house_natural);
    EXPECT_EQ(map.placement_count(), 1u);
}


TEST_F(MapPlacementTest, SetReflectionChangesOnlyReflection)
{
    const auto placed = map.place(house_id);
    ASSERT_TRUE(placed.has_value());

    ASSERT_TRUE(map.set_reflection(*placed, Reflection::y).has_value());

    const auto* placement = map.placement(*placed);
    ASSERT_NE(placement, nullptr);
    EXPECT_EQ(placement->reflection(), Reflection::y);
    EXPECT_EQ(placement->rotation(), Rotation::none);
    EXPECT_EQ(placement->position(), house_natural);
    EXPECT_EQ(map.placement_count(), 1u);
}


TEST_F(MapPlacementTest, SetOrientationReplacesBothComponents)
{
    const auto placed = map.place(
        house_id, Coord32(33, 44), Orientation {Rotation::r90});
    ASSERT_TRUE(placed.has_value());

    ASSERT_TRUE(map.set_orientation(
        *placed, Orientation {Rotation::r270, Reflection::xy}).has_value());

    const auto* placement = map.placement(*placed);
    ASSERT_NE(placement, nullptr);
    EXPECT_EQ(placement->rotation(), Rotation::r270);
    EXPECT_EQ(placement->reflection(), Reflection::xy);
    EXPECT_EQ(placement->position(), Coord32(33, 44));
    EXPECT_EQ(map.placement_count(), 1u);
}


TEST_F(MapPlacementTest, MutationsRejectUnknownPlacementIds)
{
    const auto placed = map.place(house_id);
    ASSERT_TRUE(placed.has_value());

    const auto* before = map.placement(*placed);
    ASSERT_NE(before, nullptr);

    const Coord32 position_before = before->position();
    const Orientation orientation_before = before->orientation();

    const auto position = map.set_position(PlacementId(0), Coord32(0, 0));
    const auto position_unknown = map.set_position(PlacementId(99), Coord32(0, 0));
    const auto rotation = map.set_rotation(PlacementId(99), Rotation::r90);
    const auto reflection = map.set_reflection(PlacementId(99), Reflection::x);
    const auto orientation = map.set_orientation(PlacementId(99), Orientation {});

    for (const auto& result : {position, position_unknown, rotation, reflection, orientation})
    {
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(
            result.error(),
            landor::geo::MapPlacementMutationError::UnknownPlacement);
    }

    const auto* after = map.placement(*placed);
    ASSERT_NE(after, nullptr);
    EXPECT_EQ(after->position(), position_before);
    EXPECT_EQ(after->orientation().rotation, orientation_before.rotation);
    EXPECT_EQ(after->orientation().reflection, orientation_before.reflection);
    EXPECT_EQ(map.placement_count(), 1u);
}


TEST_F(MapPlacementTest, SettingCurrentStateSucceedsWithNoChange)
{
    const auto placed = map.place(house_id);
    ASSERT_TRUE(placed.has_value());

    // The placement was created at the natural position with the default
    // orientation; re-requesting exactly that state must succeed each time
    // without invalidating.
    ASSERT_TRUE(map.set_position(*placed, house_natural).has_value());
    ASSERT_TRUE(map.set_rotation(*placed, Rotation::none).has_value());
    ASSERT_TRUE(map.set_reflection(*placed, Reflection::none).has_value());
    ASSERT_TRUE(map.set_orientation(*placed, Orientation {}).has_value());

    const auto* placement = map.placement(*placed);
    ASSERT_NE(placement, nullptr);
    EXPECT_EQ(placement->position(), house_natural);
    EXPECT_EQ(placement->rotation(), Rotation::none);
    EXPECT_EQ(placement->reflection(), Reflection::none);
    EXPECT_EQ(map.placement_count(), 1u);
}


// ---------------------------------------------------------------------------
// Dirty-safe creation and mutation
// ---------------------------------------------------------------------------

TEST_F(MapPlacementTest, CreationBlockedByDirtyState)
{
    // Make the Fire layer resident (fallback value zero), then dirty.
    ASSERT_TRUE(map.set<Fire>(Coord32(0, 0), std::uint8_t(9)).has_value());

    const auto blocked = map.place(house_id);

    ASSERT_FALSE(blocked.has_value());
    EXPECT_EQ(blocked.error(), landor::geo::MapPlacementError::DirtyState);
    EXPECT_EQ(map.placement_count(), 0u);

    // The failed creation must not have consumed a PlacementId: no
    // placement exists, and the first placement on this Map could only ever
    // receive id 1.
    EXPECT_EQ(map.placement(PlacementId(1)), nullptr);

    // The dirty live value survives the refused creation.
    const auto live = map.value<Fire>(Coord32(0, 0));
    ASSERT_TRUE(live.has_value());
    EXPECT_EQ(*live, std::uint8_t(9));
}


TEST_F(MapPlacementTest, UnknownPatchWinsOverDirtyState)
{
    ASSERT_TRUE(map.set<Fire>(Coord32(0, 0), std::uint8_t(9)).has_value());

    const auto blocked = map.place(PatchId(42));

    ASSERT_FALSE(blocked.has_value());
    EXPECT_EQ(blocked.error(), landor::geo::MapPlacementError::UnknownPatch);
    EXPECT_EQ(map.placement_count(), 0u);
}


TEST_F(MapPlacementTest, CapacityFullIsReportedWithoutInvalidation)
{
    ASSERT_TRUE(map.place(house_id).has_value());
    ASSERT_TRUE(map.place(well_id).has_value());
    ASSERT_TRUE(map.place(house_id).has_value());

    // Dirty state that a reached invalidation would refuse.
    ASSERT_TRUE(map.set<Fire>(Coord32(0, 0), std::uint8_t(9)).has_value());

    const auto overflow = map.place(well_id);

    // Capacity is checked before invalidation, so the result is
    // CapacityFull, not DirtyState.
    ASSERT_FALSE(overflow.has_value());
    EXPECT_EQ(overflow.error(), landor::geo::MapPlacementError::CapacityFull);
    EXPECT_EQ(map.placement_count(), 3u);

    // The dirty live value is untouched: no invalidation was attempted.
    const auto live = map.value<Fire>(Coord32(0, 0));
    ASSERT_TRUE(live.has_value());
    EXPECT_EQ(*live, std::uint8_t(9));
}


TEST_F(MapPlacementTest, TransformChangeBlockedByDirtyState)
{
    const auto placed = map.place(house_id);
    ASSERT_TRUE(placed.has_value());

    ASSERT_TRUE(map.set<Fire>(Coord32(0, 0), std::uint8_t(9)).has_value());

    const auto blocked = map.set_position(*placed, Coord32(50, 60));

    ASSERT_FALSE(blocked.has_value());
    EXPECT_EQ(blocked.error(), landor::geo::MapPlacementMutationError::DirtyState);

    // The Placement is unchanged and the dirty live value survives.
    const auto* placement = map.placement(*placed);
    ASSERT_NE(placement, nullptr);
    EXPECT_EQ(placement->position(), house_natural);

    const auto live = map.value<Fire>(Coord32(0, 0));
    ASSERT_TRUE(live.has_value());
    EXPECT_EQ(*live, std::uint8_t(9));
}


TEST_F(MapPlacementTest, RotationChangeBlockedByDirtyState)
{
    const auto placed = map.place(house_id);
    ASSERT_TRUE(placed.has_value());

    ASSERT_TRUE(map.set<Fire>(Coord32(0, 0), std::uint8_t(9)).has_value());

    const auto blocked = map.set_rotation(*placed, Rotation::r90);

    ASSERT_FALSE(blocked.has_value());
    EXPECT_EQ(blocked.error(), landor::geo::MapPlacementMutationError::DirtyState);

    const auto* placement = map.placement(*placed);
    ASSERT_NE(placement, nullptr);
    EXPECT_EQ(placement->rotation(), Rotation::none);
}


TEST_F(MapPlacementTest, ReflectionChangeBlockedByDirtyState)
{
    const auto placed = map.place(house_id);
    ASSERT_TRUE(placed.has_value());

    ASSERT_TRUE(map.set<Fire>(Coord32(0, 0), std::uint8_t(9)).has_value());

    const auto blocked = map.set_reflection(*placed, Reflection::x);

    ASSERT_FALSE(blocked.has_value());
    EXPECT_EQ(blocked.error(), landor::geo::MapPlacementMutationError::DirtyState);

    const auto* placement = map.placement(*placed);
    ASSERT_NE(placement, nullptr);
    EXPECT_EQ(placement->reflection(), Reflection::none);
}


TEST_F(MapPlacementTest, OrientationChangeBlockedByDirtyState)
{
    const auto placed = map.place(
        house_id, Coord32(33, 44), Orientation {Rotation::r90});
    ASSERT_TRUE(placed.has_value());

    ASSERT_TRUE(map.set<Fire>(Coord32(0, 0), std::uint8_t(9)).has_value());

    const auto blocked = map.set_orientation(
        *placed, Orientation {Rotation::r270, Reflection::xy});

    ASSERT_FALSE(blocked.has_value());
    EXPECT_EQ(blocked.error(), landor::geo::MapPlacementMutationError::DirtyState);

    const auto* placement = map.placement(*placed);
    ASSERT_NE(placement, nullptr);
    EXPECT_EQ(placement->rotation(), Rotation::r90);
    EXPECT_EQ(placement->reflection(), Reflection::none);
}


TEST_F(MapPlacementTest, NoOpTransformSucceedsWhileDirty)
{
    const auto placed = map.place(house_id);
    ASSERT_TRUE(placed.has_value());

    ASSERT_TRUE(map.set<Fire>(Coord32(0, 0), std::uint8_t(9)).has_value());

    // The requested state is the current state: no world composition change
    // and no invalidation, so the dirty state cannot block the no-op.
    ASSERT_TRUE(map.set_position(*placed, house_natural).has_value());
    ASSERT_TRUE(map.set_rotation(*placed, Rotation::none).has_value());
    ASSERT_TRUE(map.set_reflection(*placed, Reflection::none).has_value());
    ASSERT_TRUE(map.set_orientation(*placed, Orientation {}).has_value());

    const auto live = map.value<Fire>(Coord32(0, 0));
    ASSERT_TRUE(live.has_value());
    EXPECT_EQ(*live, std::uint8_t(9));
}

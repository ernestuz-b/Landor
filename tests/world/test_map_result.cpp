// Compile-time contract tests for the checked Map access result defined by
// src/world/map_result.hpp.
//
// The contract is a compile-time one:
//
//     Map::value<LayerT>() / Map::at()
//         -> MapResult<T> = std::expected<T, MapError>
//
// where MapError preserves the lower-level source error domains instead of
// flattening them into one Map-local enum. The GoogleTest bodies below exist
// to carry the static assertions; the runtime checks only verify that each
// MapError alternative stores and retrieves cleanly.

#include <gtest/gtest.h>

#include "world/coord.hpp"
#include "world/layer.hpp"
#include "world/map.hpp"
#include "world/map_result.hpp"
#include "world/tile.hpp"

#include <cstdint>
#include <expected>
#include <functional>
#include <type_traits>
#include <utility>
#include <variant>


namespace
{

using landor::geo::Coord32;

/// Byte-sized test layer, matching the header-tripwire test layer.
struct Fire
{
    static constexpr landor::geo::LayerId id = 3;
    using value_type = std::uint8_t;
};

static_assert(landor::geo::Layer<Fire>);

/// Constant terminal fallback provider for the test Map, in the exact shape
/// the LayerFallbackProvider contract requires.
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

/// The same test Map type the header tripwire uses.
using TestMap = landor::geo::Map<8, 16, 32, TestFallback, Coord32, Fire>;

} // namespace


TEST(MapResult, ErrorVariantPreservesExactlyThePinnedDomains)
{
    using landor::geo::AuthoredLayerSourceError;
    using landor::geo::LayerSourceError;
    using landor::geo::MapError;
    using landor::geo::MapErrorCode;

    // MapError carries exactly three alternatives, in this order.
    static_assert(std::variant_size_v<MapError> == 3);
    static_assert(std::is_same_v<std::variant_alternative_t<0, MapError>, MapErrorCode>);
    static_assert(std::is_same_v<std::variant_alternative_t<1, MapError>, LayerSourceError>);
    static_assert(std::is_same_v<std::variant_alternative_t<2, MapError>, AuthoredLayerSourceError>);

    // The lower domains keep their exact vocabulary and no flattened
    // duplicate enum stands in for them; storage::Error never appears
    // directly in MapError.
    SUCCEED();
}


TEST(MapResult, EachErrorAlternativeStoresAndRetrieves)
{
    {
        landor::geo::MapError error = landor::geo::MapErrorCode::OutOfBounds;
        EXPECT_TRUE(std::holds_alternative<landor::geo::MapErrorCode>(error));
        EXPECT_EQ(std::get<landor::geo::MapErrorCode>(error),
                  landor::geo::MapErrorCode::OutOfBounds);
    }

    {
        landor::geo::MapError error = landor::geo::MapErrorCode::CacheFull;
        EXPECT_TRUE(std::holds_alternative<landor::geo::MapErrorCode>(error));
        EXPECT_EQ(std::get<landor::geo::MapErrorCode>(error),
                  landor::geo::MapErrorCode::CacheFull);
    }

    {
        landor::geo::MapError error = landor::geo::LayerSourceError::StorageFailed;
        EXPECT_TRUE(std::holds_alternative<landor::geo::LayerSourceError>(error));
        EXPECT_EQ(std::get<landor::geo::LayerSourceError>(error),
                  landor::geo::LayerSourceError::StorageFailed);
    }

    {
        landor::geo::MapError error = landor::geo::AuthoredLayerSourceError::DimensionMismatch;
        EXPECT_TRUE(std::holds_alternative<landor::geo::AuthoredLayerSourceError>(error));
        EXPECT_EQ(std::get<landor::geo::AuthoredLayerSourceError>(error),
                  landor::geo::AuthoredLayerSourceError::DimensionMismatch);
    }
}


TEST(MapResult, ResultAliasIsExpectedBased)
{
    static_assert(std::is_same_v<
        landor::geo::MapResult<std::uint8_t>,
        std::expected<std::uint8_t, landor::geo::MapError>>);

    static_assert(std::is_same_v<
        landor::geo::MapResult<void>,
        std::expected<void, landor::geo::MapError>>);

    static_assert(std::is_same_v<
        landor::geo::MapResult<landor::geo::Tile<Coord32, Fire>>,
        std::expected<landor::geo::Tile<Coord32, Fire>, landor::geo::MapError>>);
    SUCCEED();
}


TEST(MapResult, ResultsCarryValuesAndErrors)
{
    landor::geo::MapResult<std::uint8_t> value = 7;
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(*value, 7);

    landor::geo::MapResult<std::uint8_t> out_of_bounds =
        std::unexpected(landor::geo::MapErrorCode::OutOfBounds);
    EXPECT_FALSE(out_of_bounds.has_value());
    EXPECT_EQ(out_of_bounds.error(),
              landor::geo::MapError {landor::geo::MapErrorCode::OutOfBounds});

    landor::geo::MapResult<void> ok {};
    EXPECT_TRUE(ok.has_value());

    landor::geo::MapResult<void> cache_full =
        std::unexpected(landor::geo::MapErrorCode::CacheFull);
    EXPECT_FALSE(cache_full.has_value());
    EXPECT_EQ(cache_full.error(),
              landor::geo::MapError {landor::geo::MapErrorCode::CacheFull});
}


TEST(MapResult, PublicCheckedAccessReturnsMapResult)
{
    // The access path is not implemented yet; these checks pin the declared
    // return types without calling the unresolved methods.
    static_assert(std::is_same_v<
        decltype(std::declval<const TestMap&>().template value<Fire>(
            std::declval<Coord32>())),
        landor::geo::MapResult<Fire::value_type>>);

    static_assert(std::is_same_v<
        decltype(std::declval<const TestMap&>().at(std::declval<Coord32>())),
        landor::geo::MapResult<TestMap::tile_type>>);

    static_assert(std::is_same_v<
        decltype(std::declval<const TestMap&>().at(
            std::declval<typename TestMap::scalar_type>(),
            std::declval<typename TestMap::scalar_type>())),
        landor::geo::MapResult<TestMap::tile_type>>);
    SUCCEED();
}

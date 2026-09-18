// Compile-time contract tests for the terminal layer fallback provider seam
// defined by src/world/layer_fallback.hpp.
//
// The contract is a compile-time one:
//
//     provider + LayerT + world coordinate
//         -> exactly LayerT::value_type
//
// queried through a const provider, with no absence outcome. The GoogleTest
// bodies below exist to carry the static assertions; they assert nothing at
// runtime on purpose.

#include <gtest/gtest.h>

#include "world/coord.hpp"
#include "world/layer.hpp"
#include "world/layer_fallback.hpp"

#include <cstdint>


namespace
{

using landor::geo::Coord32;

/// Byte-sized test layer, compatible with the current Tile byte packing.
struct Fire
{
    static constexpr landor::geo::LayerId id = 3;
    using value_type = std::uint8_t;
};

/// Wider test layer for concept checks only. The current Tile byte packing
/// forbids combining it with Fire in one Tile or Map, so it never appears in
/// those here.
struct Elevation
{
    static constexpr landor::geo::LayerId id = 4;
    using value_type = std::uint16_t;
};

static_assert(landor::geo::Layer<Fire>);
static_assert(landor::geo::Layer<Elevation>);

/// Terminal fallback in the intended shape: the layer is an explicit
/// template argument and the world coordinate type is deduced.
struct TestFallback
{
    template<landor::geo::Layer LayerT, typename PositionT>
    [[nodiscard]]
    typename LayerT::value_type
    value(PositionT) const
    {
        return {};
    }
};

/// Returns the wrong type for one layer. Exact return-type checking must
/// reject it rather than relying on an implicit conversion.
struct WrongValueTypeFallback
{
    template<typename LayerT>
    [[nodiscard]]
    std::uint16_t
    value(Coord32) const
    {
        return 0;
    }
};

/// Supplies the exact value type for Fire only. The aggregate concept must
/// fail for any layer set containing another layer.
struct FireOnlyFallback
{
    template<landor::geo::Layer LayerT>
        requires std::same_as<LayerT, Fire>
    [[nodiscard]]
    typename LayerT::value_type
    value(Coord32) const
    {
        return 0;
    }
};

/// Exposes the operation on non-const objects only. Map borrows the provider
/// as const, so this must not satisfy the concept.
struct NonConstFallback
{
    template<typename LayerT>
    [[nodiscard]]
    typename LayerT::value_type
    value(Coord32)
    {
        return {};
    }
};

} // namespace


TEST(LayerFallbackContract, ProviderSuppliesExactValueTypes)
{
    static_assert(landor::geo::LayerFallbackFor<TestFallback, Coord32, Fire>);
    static_assert(landor::geo::LayerFallbackFor<TestFallback, Coord32, Elevation>);
    static_assert(
        landor::geo::LayerFallbackProvider<TestFallback, Coord32, Fire, Elevation>);
    SUCCEED();
}


TEST(LayerFallbackContract, WrongReturnTypeIsRejected)
{
    // uint16_t would narrow for Fire; the concept checks the exact type and
    // must not accept it through implicit conversion.
    static_assert(!landor::geo::LayerFallbackFor<WrongValueTypeFallback, Coord32, Fire>);

    // The same provider answers Elevation exactly, showing the check is
    // per-layer exactness rather than blanket rejection.
    static_assert(landor::geo::LayerFallbackFor<WrongValueTypeFallback, Coord32, Elevation>);
    static_assert(
        !landor::geo::LayerFallbackProvider<WrongValueTypeFallback, Coord32, Fire, Elevation>);
    SUCCEED();
}


TEST(LayerFallbackContract, MissingLayerSupportFailsTheAggregateConcept)
{
    static_assert(landor::geo::LayerFallbackFor<FireOnlyFallback, Coord32, Fire>);
    static_assert(!landor::geo::LayerFallbackFor<FireOnlyFallback, Coord32, Elevation>);
    static_assert(landor::geo::LayerFallbackProvider<FireOnlyFallback, Coord32, Fire>);
    static_assert(
        !landor::geo::LayerFallbackProvider<FireOnlyFallback, Coord32, Fire, Elevation>);
    SUCCEED();
}


TEST(LayerFallbackContract, NonConstOnlyProviderIsRejected)
{
    static_assert(!landor::geo::LayerFallbackFor<NonConstFallback, Coord32, Fire>);
    static_assert(!landor::geo::LayerFallbackProvider<NonConstFallback, Coord32, Fire>);
    SUCCEED();
}

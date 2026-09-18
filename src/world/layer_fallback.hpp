#pragma once

#include "layer.hpp"

#include <concepts>
#include <type_traits>


namespace landor::geo
{

/**
 * Terminal fallback provider contract for Map layer resolution.
 *
 * Per-layer resolution asks sources in priority order:
 *
 *     materialized/working state
 *         -> authored Placement contributions
 *         -> terminal fallback provider
 *
 * The terminal fallback provider is the final, always-answerable source of a
 * supported layer value. It covers everything previously described loosely
 * as "procedural generation or layer default": deterministic procedural
 * baseline generation, a fixed or per-layer default value, or a mixture
 * depending on the layer. Map does not need to know which of these a given
 * layer uses.
 *
 * The concrete operation Map needs is deliberately small:
 *
 *     provider + LayerT + world position
 *         -> exactly LayerT::value_type
 *
 * A conforming provider offers a const member operation of the shape:
 *
 *     struct SomeFallback
 *     {
 *         template<typename LayerT>
 *         [[nodiscard]]
 *         typename LayerT::value_type
 *         value(CoordT position) const;
 *     };
 *
 * The exact member template spelling is open; the semantic contract is:
 *
 * - Queryable through a const reference. Map borrows the provider and
 *   resolution never mutates it.
 * - Always answers. For a fixed provider state, every layer of the Map's
 *   layer set and every valid Map coordinate produce a value. Absence is
 *   not a valid answer, so the result is the value type itself; there is
 *   no std::optional or std::expected here.
 * - Untouched baseline. For a fixed provider state, layer and coordinate,
 *   repeated calls represent the same untouched baseline world value.
 *   Cache residency, Chunk filling and Storage activity must not change
 *   the answer.
 * - Exact value type. The return type is exactly LayerT::value_type.
 *   Implicit conversion at this seam could hide widening or narrowing and
 *   would move the layer's value decision out of the layer type.
 * - World-ignorant. The provider may hold whatever seed or configuration
 *   its own deterministic generation needs, but it must not depend on
 *   Cache, Chunk residency, Patch, Placement, Storage, SourceId or
 *   runtime dirty state. Those are Map and source-composition concerns.
 * - No operational failures. I/O failure belongs to the higher source
 *   layers, for example Storage-backed authored sources. The terminal
 *   fallback answers from its own state.
 *
 * Provider variation is a compile-time type choice. No inheritance,
 * virtual functions, RTTI, registration or type erasure is required.
 */
template<typename Provider, typename CoordT, typename LayerT>
concept LayerFallbackFor =
    Layer<LayerT>
    && requires(const Provider& provider, CoordT position)
    {
        {
            provider.template value<LayerT>(position)
        } -> std::same_as<typename LayerT::value_type>;
    };


/**
 * Aggregate LayerFallbackFor over a complete layer set.
 *
 * A provider satisfies this when it supplies the exact value type for every
 * layer in the set, which is what a Map built with those layers needs from
 * its terminal fallback dependency.
 */
template<typename Provider, typename CoordT, typename... Layers>
concept LayerFallbackProvider =
    (... && LayerFallbackFor<Provider, CoordT, Layers>);


} // namespace landor::geo

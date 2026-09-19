#pragma once

#include "authored_layer_source.hpp"
#include "layer_source.hpp"
#include "runtime_layer_source.hpp"

#include <cstdint>
#include <expected>
#include <variant>


namespace landor::geo
{

/**
 * Map-local outcomes of checked Map access.
 *
 * These failures are owned by Map itself rather than by a lower source seam:
 *
 *   - OutOfBounds: the requested world coordinate does not belong to this
 *     Map. It is detected before any Cache or Storage work begins.
 *   - CacheFull: the bounded Cache cannot accept another spatial slot and
 *     no replacement/eviction policy exists yet, so the miss fails instead
 *     of evicting resident state.
 */
enum class MapErrorCode : std::uint8_t
{
    OutOfBounds,
    CacheFull
};


/**
 * Error domain of checked Map access.
 *
 * Map-level outcomes use MapErrorCode. Failures that originate below Map
 * preserve their own exact lower-level error domains instead of being
 * flattened into one large Map enum:
 *
 *   - LayerSourceError: the exact `.layer` parse/read error, regardless of
 *     the source role that produced it (authored or runtime);
 *   - AuthoredLayerSourceError: the exact authored Patch/source geometry
 *     compatibility error;
 *   - RuntimeLayerSourceError: the exact runtime Map/source geometry
 *     compatibility error (D-35/D-36).
 *
 * storage::Error never appears directly in this variant: the layer source
 * reader already maps Storage failures to LayerSourceError::StorageFailed,
 * and Map keeps that layering.
 */
using MapError = std::variant<
    MapErrorCode,
    LayerSourceError,
    AuthoredLayerSourceError,
    RuntimeLayerSourceError>;


/**
 * Result of checked Map access.
 *
 * T is either one layer value (Map::value<LayerT>) or one complete Tile
 * (Map::at). Success carries T; failure carries the exact MapError produced
 * by the seam that failed.
 */
template<typename T>
using MapResult = std::expected<T, MapError>;


} // namespace landor::geo

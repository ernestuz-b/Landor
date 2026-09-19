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
 * Persistence-specific failure outcomes of Map write-back.
 *
 * These belong to Map persistence, not to checked world access, so they do
 * not share the MapError domain of map_result.hpp:
 *
 *   - MissingRuntimeBinding: a dirty plane genuinely differs from freshly
 *     resolved backing state, but this Map has no runtime binding for the
 *     layer, so no runtime overlay source exists that the plane could be
 *     persisted into;
 *   - UnencodableValue: a live value that differs from its backing answer
 *     cannot be stored as a version 1.0 runtime contribution byte: ASCII
 *     space (0x20) means "no contribution" and LF (0x0A) is structural, so
 *     neither can express a stored override.
 */
enum class MapPersistenceErrorCode : std::uint8_t
{
    MissingRuntimeBinding,
    UnencodableValue
};


/**
 * Failure domain of Map write-back.
 *
 * Persistence-specific outcomes are MapPersistenceErrorCode. Failures at
 * the lower seams keep their exact domains: the exact LayerSourceError for
 * .layer open/read/write failures, the exact AuthoredLayerSourceError for
 * authored Patch/source geometry failures, and the exact
 * RuntimeLayerSourceError for runtime Map/source geometry failures.
 *
 * No MapErrorCode outcome belongs here: flush<LayerT>() performs no bounds
 * checking (its coordinates derive from resident chunk geometry) and cannot
 * run out of Cache capacity (it writes to Storage, not to the Cache).
 */
using MapPersistenceError = std::variant<
    MapPersistenceErrorCode,
    LayerSourceError,
    AuthoredLayerSourceError,
    RuntimeLayerSourceError>;


/**
 * Checked result of one Map write-back operation.
 */
using MapPersistenceResult = std::expected<void, MapPersistenceError>;

} // namespace landor::geo

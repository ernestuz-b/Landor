#pragma once

#include "area.hpp"
#include "layer.hpp"
#include "layer_source.hpp"
#include "../storage/types.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>


namespace landor::geo
{

/**
 * Connects one Map layer with its runtime/materialised overlay source.
 *
 * This is the runtime twin of patch.hpp's `LayerBinding`. The two types
 * have the same small physical shape, but different semantic owners and
 * must not be conflated:
 *
 *     LayerBinding:          authored Patch layer -> authored source
 *     RuntimeLayerBinding:   Map layer            -> runtime source
 *
 * The span a Map borrows is scoped to that one Map instance, so a binding
 * `{ layer, source }` identifies the logical runtime overlay source of
 * `(Map::id(), layer)`:
 *
 *     one Map
 *         × one Layer
 *             = at most one logical runtime overlay source
 *
 * The runtime identity is therefore (MapId, LayerId) — never a PatchId,
 * PlacementId, Cache slot or Cache Chunk (see DESIGN_DECISIONS.md, D-35).
 *
 * The `source` is an identity only. RuntimeLayerBinding does not know
 * whether the data comes from a host file, an SD-card file, a flash region
 * or some other storage implementation, and it carries no path, filename
 * or registration state. SourceId allocation and registration remain
 * Storage/implementation concerns.
 */
struct RuntimeLayerBinding
{
    LayerId           layer;
    storage::SourceId source;
};


/**
 * Errors produced when validating one Map area against one parsed runtime
 * layer source layout.
 *
 * The values keep the failures distinct so callers can react to them
 * separately: a Map whose geometry a dense runtime source cannot represent,
 * a size disagreement, and a position disagreement are different
 * diagnoses.
 */
enum class RuntimeLayerSourceError : std::uint8_t
{
    /// The Map area is empty. A dense runtime source cannot represent an
    /// empty Map; nothing is translated or normalized.
    InvalidMapGeometry,

    /// The source `D` dimensions differ from the Map area extent.
    DimensionMismatch,

    /// The source `P` position differs from the Map area minimum.
    PositionMismatch
};


/**
 * Validates the dense v1 runtime source geometry contract between one Map
 * area and one already-parsed layer source layout.
 *
 * A runtime source is the materialised overlay for one logical Map layer
 * and is world-oriented: it covers the Map's complete logical `Area`, and
 * no Placement transform and no Patch participate (see
 * DESIGN_DECISIONS.md, D-35). For dense v1 runtime sources the contract is:
 *
 *   - the source `P` position equals `map_area.min()`;
 *   - the source `D` dimensions equal the Map area extent exactly,
 *     `max - min + 1` per axis.
 *
 * Consequently the source-local coordinate is simply
 * `world - map_area.min()`, and source-local `(0, 0)` is the Map's minimum
 * world coordinate.
 *
 * This pins the logical source, not a physical object: the source may be
 * much larger than RAM and never has to be loaded, rewritten or
 * materialized in whole. Storage remains free to represent the logical
 * source however its backend requires, and no filename is pinned. Runtime
 * identity is independent of `CacheChunkSide`: cache granularity is an
 * implementation choice and must not move saved-world identity.
 *
 * The helper validates geometry only. It does not care which SourceId
 * produced the layout, which storage root it lives in, or whether another
 * Map uses a different source. It performs no Storage I/O: the caller
 * passes a layout already obtained, for example through
 * `open_layer_source()`.
 *
 * Numeric safety:
 *
 *   - The extent is computed from the min/max corners in a 64-bit signed
 *     intermediate and compared against the `uint32_t` dimensions without
 *     narrowing. It deliberately does not use `Area::width()`/`height()`:
 *     for narrow scalar types those wrap when the true extent exceeds the
 *     scalar maximum (a 128-cell Coord8 area reports `width() == -128`).
 *     That Area accessor limitation is separate from this contract; the
 *     validator avoids it by computing the extent from the corners.
 *   - The source `P` (Coord32) is compared after widening both sides to
 *     64 bits. It is never cast into the Map's coordinate type first, so
 *     an out-of-range `P` (for example 128 against an `Area<Coord8>`)
 *     reports PositionMismatch instead of wrapping to -128.
 */
template<typename CoordT>
[[nodiscard]] constexpr
std::expected<void, RuntimeLayerSourceError>
validate_runtime_layer_source(
    const Area<CoordT>& map_area,
    const LayerSourceLayout& source) noexcept
{
    // Rule 1: the Map area is non-empty.
    if (map_area.is_empty())
    {
        return std::unexpected(RuntimeLayerSourceError::InvalidMapGeometry);
    }

    // Rule 2: the D dimensions equal the Map extent computed from the
    // corners in a wide signed intermediate.
    //
    // Non-empty implies max >= min on both axes, so each extent lies in
    // [1, scalar_max + 1] and fits exactly in std::int64_t for the
    // supported widths.
    const std::int64_t width =
        static_cast<std::int64_t>(map_area.max().x())
        - static_cast<std::int64_t>(map_area.min().x()) + 1;
    const std::int64_t height =
        static_cast<std::int64_t>(map_area.max().y())
        - static_cast<std::int64_t>(map_area.min().y()) + 1;

    if (width != static_cast<std::int64_t>(source.width)
        || height != static_cast<std::int64_t>(source.height))
    {
        return std::unexpected(RuntimeLayerSourceError::DimensionMismatch);
    }

    // Rule 3: the P position equals the Map area minimum, compared in a
    // wide intermediate so neither side narrows.
    const auto map_origin = map_area.min();
    const auto source_position = source.natural_position;

    if (static_cast<std::int64_t>(map_origin.x())
            != static_cast<std::int64_t>(source_position.x())
        || static_cast<std::int64_t>(map_origin.y())
            != static_cast<std::int64_t>(source_position.y()))
    {
        return std::unexpected(RuntimeLayerSourceError::PositionMismatch);
    }

    return {};
}


namespace detail
{

/// Does one layer id belong to a (non-empty) layer pack?
///
/// Recursive over the pack: a pack-empty tail is handled with
/// if constexpr so the discarded branch never forms an empty-pack call.
template<Layer First, Layer... Rest>
[[nodiscard]] constexpr bool
layer_id_in_pack(LayerId layer) noexcept
{
    if (layer == First::id)
    {
        return true;
    }

    if constexpr (sizeof...(Rest) > 0)
    {
        return layer_id_in_pack<Rest...>(layer);
    }
    else
    {
        return false;
    }
}

} // namespace detail


/**
 * Catalogue precondition for one Map runtime layer binding span.
 *
 * The span is scoped to one particular Map instance, so the invariants are
 * relative to that Map's layer set (LayerTs...):
 *
 *   - every binding names a layer supported by the Map;
 *   - a LayerId appears at most once in the span.
 *
 * There is deliberately no precedence between duplicate bindings: a
 * duplicate is invalid configuration, not "first wins" or "last wins"
 * (D-35). An empty span is always valid: it means the Map currently has no
 * persistent runtime overlay source for any of its layers, which is not
 * the same as an unsupported layer.
 */
template<Layer... LayerTs>
[[nodiscard]] constexpr bool
valid_runtime_layer_bindings(
    std::span<const RuntimeLayerBinding> bindings) noexcept
{
    for (std::size_t i = 0; i < bindings.size(); ++i)
    {
        const LayerId layer = bindings[i].layer;

        if constexpr (sizeof...(LayerTs) > 0)
        {
            if (!detail::layer_id_in_pack<LayerTs...>(layer))
            {
                return false;
            }
        }
        else
        {
            // A Map with no supported layers cannot bind any runtime source.
            return false;
        }

        for (std::size_t j = i + 1; j < bindings.size(); ++j)
        {
            if (bindings[j].layer == layer)
            {
                return false;
            }
        }
    }

    return true;
}


} // namespace landor::geo

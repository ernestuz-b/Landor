#pragma once

#include "area.hpp"
#include "layer.hpp"
#include "layer_source.hpp"
#include "../storage/types.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <span>
#include <variant>


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
    /// The Map area cannot be represented by a dense v1 runtime source:
    /// the area is empty, its extent exceeds the dense v1 per-axis
    /// uint32 cell count, or its minimum does not fit the source's
    /// Coord32 position. Nothing is translated or normalized.
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


/**
 * Lazily materialise the runtime overlay source of one Map layer.
 *
 * Creates the blank dense v1 runtime source for the complete Map area —
 * `P` = the Map area minimum, `D` = the Map area extent, every cell the
 * ASCII space (0x20) no-contribution byte (D-39) — and verifies the created
 * source against the same geometry contract that
 * validate_runtime_layer_source() applies to runtime sources.
 *
 * The helper does not replace an existing source (the underlying create()
 * will not), makes no path or filename decision above Storage, and performs
 * no SourceId allocation or registration: the caller passes the identity the
 * Map's runtime binding already reserved for this layer.
 *
 * Error mapping: a Map area the dense v1 source cannot represent fails with
 * RuntimeLayerSourceError::InvalidMapGeometry without touching Storage;
 * format/storage-level failures from create_blank_layer_source() pass
 * through as LayerSourceError; a created source that failed the runtime
 * geometry contract passes through as RuntimeLayerSourceError.
 */
template<storage::StorageBackend Backend, typename CoordT>
[[nodiscard]] std::expected<
    LayerSourceLayout,
    std::variant<LayerSourceError, RuntimeLayerSourceError>>
materialize_runtime_layer_source(
    Backend& storage,
    storage::SourceId source,
    const Area<CoordT>& map_area)
{
    using MaterializeError = std::variant<LayerSourceError, RuntimeLayerSourceError>;

    // Same geometry contract as validation: reject an unrepresentable Map
    // area before creating anything.
    if (map_area.is_empty())
    {
        return std::unexpected(MaterializeError(RuntimeLayerSourceError::InvalidMapGeometry));
    }

    const std::int64_t width =
        static_cast<std::int64_t>(map_area.max().x())
        - static_cast<std::int64_t>(map_area.min().x()) + 1;
    const std::int64_t height =
        static_cast<std::int64_t>(map_area.max().y())
        - static_cast<std::int64_t>(map_area.min().y()) + 1;

    // A dense v1 source holds at most uint32 cells per axis; a wider Map is
    // a geometry the dense format cannot represent, so it is a
    // RuntimeLayerSourceError rather than a format Overflow.
    if (width > static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max())
        || height > static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max()))
    {
        return std::unexpected(MaterializeError(RuntimeLayerSourceError::InvalidMapGeometry));
    }

    // The source position is a Coord32: the Map minimum must fit it exactly,
    // compared in a wide intermediate rather than cast and wrapped.
    const auto min_x = map_area.min().x();
    const auto min_y = map_area.min().y();
    if (static_cast<std::int64_t>(min_x) < static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::min())
        || static_cast<std::int64_t>(min_x) > static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max())
        || static_cast<std::int64_t>(min_y) < static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::min())
        || static_cast<std::int64_t>(min_y) > static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max()))
    {
        return std::unexpected(MaterializeError(RuntimeLayerSourceError::InvalidMapGeometry));
    }

    const auto layout = create_blank_layer_source(
        storage,
        source,
        static_cast<std::uint32_t>(width),
        static_cast<std::uint32_t>(height),
        Coord32(static_cast<std::int32_t>(min_x), static_cast<std::int32_t>(min_y)));
    if (!layout)
    {
        return std::unexpected(MaterializeError(layout.error()));
    }

    // Defensive: a source derived from the same area must satisfy the
    // runtime geometry contract. Keep the exact error if it ever does not.
    const auto validated = validate_runtime_layer_source(map_area, *layout);
    if (!validated)
    {
        return std::unexpected(MaterializeError(validated.error()));
    }

    return *layout;
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

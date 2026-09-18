#pragma once

#include "layer_source.hpp"
#include "patch.hpp"

#include <cstdint>
#include <expected>


namespace landor::geo
{

/**
 * Errors produced when validating one authored Patch against one parsed
 * dense v1 authored layer source layout.
 *
 * The values keep the failures distinct so callers can react to them
 * separately: a Patch whose local geometry v1 sources cannot represent, a
 * size disagreement, and a position disagreement are different diagnoses.
 */
enum class AuthoredLayerSourceError : std::uint8_t
{
    /// The Patch local area is empty or does not begin at (0, 0).
    /// Dense v1 sources carry no local origin offset, so such a Patch is
    /// incompatible with them; nothing is translated or normalized.
    InvalidPatchGeometry,

    /// The Patch local width or height differs from the source D
    /// dimensions.
    DimensionMismatch,

    /// The Patch natural position differs from the source P position.
    PositionMismatch
};


/**
 * Validates the dense v1 authored source geometry contract between one
 * authored Patch and one already-parsed layer source layout.
 *
 * This is the explicit compatibility seam between Patch metadata and the
 * parsed `.layer` metadata. For dense v1 authored sources
 * (see LAYER_SOURCE_FORMAT.md) the contract is:
 *
 *   - the source rectangle is anchored at Patch-local (0, 0), so
 *     `patch.local_area()` must be non-empty and begin at (0, 0); a local
 *     area starting anywhere else cannot be represented by a v1 source and
 *     is rejected, not translated or normalized;
 *   - the source `D` dimensions equal the Patch local extent exactly;
 *   - the source `P` position equals `Patch::natural_position()`.
 *
 * The helper validates geometry only. It does not care which LayerId is
 * bound, which SourceId produced the layout, where the source lives, or
 * whether another Patch uses the same source. It performs no Storage I/O:
 * the caller passes a layout already obtained, for example through
 * `open_layer_source()`. The intended future Map composition is:
 *
 *     open_layer_source(...)
 *         -> validate_authored_layer_source(...)
 *         -> use source
 *
 * The checks are deliberately not enforced inside the `Patch`
 * constructor: `Patch` is authored-world metadata, while this contract is
 * specific to the dense v1 source format. An invalid pairing is detected
 * here, before that Patch/source pairing is used.
 *
 * Numeric safety:
 *
 *   - The local extent is computed from the min/max corners in a
 *     64-bit signed intermediate and compared against the `uint32_t`
 *     dimensions without narrowing. It deliberately does not use
 *     `Area::width()`/`height()`: for narrow scalar types those wrap when
 *     the true extent exceeds the scalar maximum (a 128-cell Coord8 area
 *     reports `width() == -128`). That Area accessor limitation is
 *     separate from this contract; the validator avoids it by computing
 *     the extent from the corners.
 *   - The natural position is compared after widening both sides to
 *     64 bits. The source `P` (Coord32) is never cast into the Patch's
 *     coordinate type first, so an out-of-range `P` (for example 128
 *     against a `Patch<Coord8>`) reports PositionMismatch instead of
 *     wrapping to -128.
 */
template<typename CoordT>
[[nodiscard]] constexpr
std::expected<void, AuthoredLayerSourceError>
validate_authored_layer_source(
    const Patch<CoordT>& patch,
    const LayerSourceLayout& source) noexcept
{
    // Rule 1: the local area is non-empty and anchored at (0, 0).
    const auto& local_area = patch.local_area();
    if (local_area.is_empty() || local_area.min() != CoordT {0, 0})
    {
        return std::unexpected(AuthoredLayerSourceError::InvalidPatchGeometry);
    }

    // Rule 2: the D dimensions equal the local extent.
    //
    // Non-empty plus min == (0, 0) implies max >= 0 on both axes, so each
    // extent lies in [1, scalar_max + 1] and fits exactly in std::int64_t
    // for the supported widths.
    const std::int64_t width =
        static_cast<std::int64_t>(local_area.max().x())
        - static_cast<std::int64_t>(local_area.min().x()) + 1;
    const std::int64_t height =
        static_cast<std::int64_t>(local_area.max().y())
        - static_cast<std::int64_t>(local_area.min().y()) + 1;

    if (width != static_cast<std::int64_t>(source.width)
        || height != static_cast<std::int64_t>(source.height))
    {
        return std::unexpected(AuthoredLayerSourceError::DimensionMismatch);
    }

    // Rule 3: the P position equals the natural position, compared in a
    // wide intermediate so neither side narrows.
    const auto patch_position = patch.natural_position();
    const auto source_position = source.natural_position;

    if (static_cast<std::int64_t>(patch_position.x())
            != static_cast<std::int64_t>(source_position.x())
        || static_cast<std::int64_t>(patch_position.y())
            != static_cast<std::int64_t>(source_position.y()))
    {
        return std::unexpected(AuthoredLayerSourceError::PositionMismatch);
    }

    return {};
}


} // namespace landor::geo

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

#include "coord.hpp"

namespace landor::geo
{

/** Axis-aligned rectangular region on the tile grid.
 *
 * Internally stores min (top-left) and max (bottom-right) corners, always
 * maintained such that min.x <= max.x and min.y <= max.y. An area with
 * min > max in either axis is considered empty.
 *
 * Template parameter CoordT must be a landor::geo::Coord<N> type; the size method
 * uses the scalar type's integer arithmetic.
 */
template <typename CoordT = Coord32>
class Area {

public:
    /// Underlying coordinate type.
    using coord_type = CoordT;
    /// Scalar type of the underlying coordinate.
    using scalar_type = typename CoordT::scalar_type;

    // --- Construction -----------------------------------------------------

    /// Default-construct an empty area (no cells).
    constexpr Area() noexcept = default;

    /// Construct from two corners. Corners are sorted internally so that
    /// min.x <= max.x and min.y <= max.y regardless of input order.
    constexpr Area(CoordT c1, CoordT c2) noexcept { assign(c1, c2); }

    // --- Accessors --------------------------------------------------------

    /** Min corner (inclusive) — top-left in screen coordinates. */
    [[nodiscard]] constexpr CoordT min() const noexcept { return m_min; }

    /** Max corner (inclusive) — bottom-right in screen coordinates. */
    [[nodiscard]] constexpr CoordT max() const noexcept { return m_max; }

    /** Width in cells: max.x - min.x + 1. Zero for degenerate areas. */
    [[nodiscard]] constexpr scalar_type width() const noexcept {
        return (m_max.x() >= m_min.x())
                   ? static_cast<scalar_type>(m_max.x() - m_min.x() + 1)
                   : static_cast<scalar_type>(0);
    }

    /** Height in cells: max.y - min.y + 1. Zero for degenerate areas. */
    [[nodiscard]] constexpr scalar_type height() const noexcept {
        return (m_max.y() >= m_min.y())
                   ? static_cast<scalar_type>(m_max.y() - m_min.y() + 1)
                   : static_cast<scalar_type>(0);
    }

    /** True if this area contains zero cells (degenerate or inverted). */
    [[nodiscard]] constexpr bool is_empty() const noexcept {
        return m_min.x() > m_max.x() || m_min.y() > m_max.y();
    }

    // --- Containment ------------------------------------------------------

    /// True if coord lies within [min, max] inclusive on both axes.
    [[nodiscard]] constexpr bool contains(CoordT coord) const noexcept {
        return !is_empty() && coord.x() >= m_min.x() && coord.x() <= m_max.x()
            && coord.y() >= m_min.y() && coord.y() <= m_max.y();
    }

    /// True if every cell of other lies within this area.
    [[nodiscard]] constexpr bool contains(Area other) const noexcept {
        if (other.is_empty()) return true;
        return contains(other.min()) && contains(other.max());
    }

    /// True if any cell is shared between this area and other.
    [[nodiscard]] constexpr bool intersects(Area other) const noexcept {
        if (is_empty() || other.is_empty()) return false;
        return !(m_max.x() < other.m_min.x() || other.m_max.x() < m_min.x()
              || m_max.y() < other.m_min.y() || other.m_max.y() < m_min.y());
    }

    // --- Operations -------------------------------------------------------

    /** Construct from four scalar values as (min_x, min_y, max_x, max_y).
     * Automatically normalises min/max.
     */
    [[nodiscard]] static constexpr Area from_coords(
        scalar_type min_x, scalar_type min_y,
        scalar_type max_x, scalar_type max_y) noexcept {
        return Area{CoordT{min_x, min_y}, CoordT{max_x, max_y}};
    }

    /** Return the intersection of this area with other, or nullopt if they
     * do not overlap.
     */
    [[nodiscard]] constexpr std::optional<Area> intersection(
        Area other) const noexcept {
        if (!intersects(other)) return std::nullopt;
        CoordT new_min{std::max(m_min.x(), other.m_min.x()),
                       std::max(m_min.y(), other.m_min.y())};
        CoordT new_max{std::min(m_max.x(), other.m_max.x()),
                       std::min(m_max.y(), other.m_max.y())};
        return Area{new_min, new_max};
    }

    /** Return the union (bounding box) of this area and other. */
    [[nodiscard]] constexpr Area union_area(Area other) const noexcept {
        if (is_empty()) return other;
        if (other.is_empty()) return *this;
        return from_coords(std::min(m_min.x(), other.m_min.x()),
                           std::min(m_min.y(), other.m_min.y()),
                           std::max(m_max.x(), other.m_max.x()),
                           std::max(m_max.y(), other.m_max.y()));
    }

    /** Clamp this area to fit within another, returning the clipped region.
     * If this area is outside, returns an empty area.
     */
    [[nodiscard]] constexpr Area clamp(Area outer) const noexcept {
        if (is_empty() || outer.is_empty()) return Area{};
        scalar_type lo_x = std::max(m_min.x(), outer.m_min.x());
        scalar_type hi_x = std::min(m_max.x(), outer.m_max.x());
        scalar_type lo_y = std::max(m_min.y(), outer.m_min.y());
        scalar_type hi_y = std::min(m_max.y(), outer.m_max.y());
        if (lo_x > hi_x || lo_y > hi_y) return Area{};
        Area result;
        result.assign(CoordT{lo_x, lo_y}, CoordT{hi_x, hi_y});
        return result;
    }

    /** Split this area into two approximately-equal halves along the longer
     * axis. Returns a pair of non-empty areas (the "remainder" may be 1-cell
     * wide/tall if the dimension is odd). If this area is empty, returns
     * two empty areas.
     */
    [[nodiscard]] constexpr std::pair<Area, Area> split() const noexcept {
        if (is_empty()) return {{}, {}};
        scalar_type w = width();
        scalar_type h = height();
        if (w >= h) {
            // Split along x-axis.
            scalar_type mid = static_cast<scalar_type>(
                static_cast<int64_t>(m_min.x())
                + static_cast<int64_t>(m_max.x() - m_min.x()) / 2);
            return {Area{m_min, CoordT(static_cast<scalar_type>(mid), m_max.y())},
                    Area{CoordT(static_cast<scalar_type>(mid) + 1, m_min.y()), m_max}};
        } else {
            // Split along y-axis (top half / bottom half).
            scalar_type mid = static_cast<scalar_type>(
                static_cast<int64_t>(m_min.y())
                + static_cast<int64_t>(m_max.y() - m_min.y()) / 2);
            return {Area{m_min, CoordT(m_max.x(), static_cast<scalar_type>(mid))},
                    Area{CoordT(m_min.x(), static_cast<scalar_type>(mid) + 1), m_max}};
        }
    }

    /// Assign new corners, re-normalizing min/max automatically.
    constexpr void assign(CoordT c1, CoordT c2) noexcept {
        scalar_type mx1 = c1.x(), my1 = c1.y();
        scalar_type mx2 = c2.x(), my2 = c2.y();
        if (mx1 > mx2) std::swap(mx1, mx2);
        if (my1 > my2) std::swap(my1, my2);
        m_min = CoordT{mx1, my1};
        m_max = CoordT{mx2, my2};
    }

    /// Equality: both corners match exactly.
    [[nodiscard]] constexpr bool operator==(Area const&) const = default;

    /// Inequality via equality.
    [[nodiscard]] constexpr bool operator!=(Area const&) const = default;

private:
    CoordT m_min{1, 1};
    CoordT m_max{0, 0};
};

// ---------------------------------------------------------------------------
// Convenience typedefs — matching coord.hpp conventions.
// ---------------------------------------------------------------------------

/// int8_t per axis — adequate for areas up to ~±127 tiles on each side.
using Area8  = Area<Coord8>;

/// int16_t per axis — areas up to ~±32767 tiles. Good compromise for
/// Pi Zero / RP2044 where RAM is plentiful enough.
using Area16 = Area<Coord16>;

/// int32_t per axis — large worlds, or when coordinate arithmetic
/// (dist_sq, intermediate multiplies) needs extra headroom.
using Area32 = Area<Coord32>;

/// Stream output for debugging — format "[(x1,y1)-(x2,y2)]".
template <typename T>
std::ostream& operator<<(std::ostream& os, Area<T> a) noexcept {
    return os << '[' << a.min() << '-' << a.max() << ']';
}

} // namespace landor::geo

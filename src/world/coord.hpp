#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <iosfwd>
#include <ostream>
#include <type_traits>
#include <utility>

namespace Geo {

/** Cardinal direction on the grid.
 *
 * Ordinals 0..3 map east, south, west, north — matching the natural
 * rotation order used by neighbour8() below.
 */
enum class Dir : uint8_t { East = 0, South = 1, West = 2, North = 3 };

// ---------------------------------------------------------------------------
// Coordinate dimensions — always 2 regardless of scalar width.
// ---------------------------------------------------------------------------

/** Number of dimensions (constant 2). */
inline constexpr uint8_t coord_dim_count = 2;

/** Two-dimensional integer coordinate on the tile grid.
 *
 * Intentionally plain — no invariants, no hidden allocation. Coordinates can be
 * negative (temporary offsets during arithmetic) and must compare by stable total
 * order (§9.12 of DESIGN_STATE.md): x first, then y. This order is part of
 * determinism — container iteration over coords must not diverge between builds.
 *
 * Template parameter T controls memory footprint per coordinate. On constrained
 * targets, narrow types (int8_t, int16_t) are the primary budget lever.
 */
template <typename T> requires std::is_integral_v<T> class Coord {
public:
    /** Scalar type exposed as a nested typedef. Useful for templates that
     * need the underlying integer width without repeating the template arg.
     */
    using scalar_type = T;

    // --- Construction -----------------------------------------------------

    /** Construct from named axes. Origin defaults to (0, 0). */
    constexpr Coord(T x = T{0}, T y = T{0}) noexcept : m_x(x), m_y(y) {}

    /// Construct from [x, y] array element access.
    constexpr Coord(std::array<T, 2> const& a) noexcept
        : m_x(a[0]), m_y(a[1]) {}

    // --- Element access ---------------------------------------------------

    /** x axis (horizontal / east-west). */
    [[nodiscard]] constexpr T x() const noexcept { return m_x; }

    /** y axis (vertical / north-south). */
    [[nodiscard]] constexpr T y() const noexcept { return m_y; }

    /** Access by dimension index: 0 → x, 1 → y. */
    [[nodiscard]] constexpr T at(uint8_t dim) const noexcept {
        return dim == 0 ? m_x : m_y;
    }

    /// Mutable access by dimension index.
    [[nodiscard]] constexpr T& at(uint8_t dim) noexcept {
        return dim == 0 ? m_x : m_y;
    }

    /// Convert to a std::array. Useful for hash combinators and structured bindings.
    [[nodiscard]] constexpr std::array<T, 2> to_array() const noexcept {
        return {m_x, m_y};
    }

    // --- Arithmetic -------------------------------------------------------

    /// Component-wise addition. Wraps on overflow (defined, like fixed-width integer math).
    [[nodiscard]] constexpr Coord operator+(Coord rhs) const noexcept {
        return Coord{T(m_x + rhs.m_x), T(m_y + rhs.m_y)};
    }

    /// Component-wise subtraction. Wraps on overflow.
    [[nodiscard]] constexpr Coord operator-(Coord rhs) const noexcept {
        return Coord{T(m_x - rhs.m_x), T(m_y - rhs.m_y)};
    }

    /// Negate both components.
    [[nodiscard]] constexpr Coord operator-() const noexcept {
        return Coord(static_cast<T>(-m_x), static_cast<T>(-m_y));
    }

    /// Scalar multiply. Wraps on overflow.
    [[nodiscard]] constexpr Coord operator*(T s) const noexcept {
        return Coord{T(m_x * s), T(m_y * s)};
    }

    /// Scalar multiply (rhs — commutative).
    [[nodiscard]] friend constexpr Coord operator*(T s, Coord c) noexcept {
        return c * s;
    }

    /// Scalar divide (truncates toward zero, like built-in /).
    [[nodiscard]] constexpr Coord operator/(T s) const noexcept {
        return Coord{T(m_x / s), T(m_y / s)};
    }

    /// Component-wise remainder (modulus analogue; sign follows dividend).
    [[nodiscard]] constexpr Coord operator%(Coord rhs) const noexcept {
        return Coord{T(m_x % rhs.m_x), T(m_y % rhs.m_y)};
    }

    /// Compound add.
    constexpr Coord& operator+=(Coord rhs) noexcept {
        m_x += rhs.m_x;
        m_y += rhs.m_y;
        return *this;
    }

    /// Compound subtract.
    constexpr Coord& operator-=(Coord rhs) noexcept {
        m_x -= rhs.m_x;
        m_y -= rhs.m_y;
        return *this;
    }

    /// Compound scalar multiply.
    constexpr Coord& operator*=(T s) noexcept {
        m_x *= s;
        m_y *= s;
        return *this;
    }

    /// Compound scalar divide.
    constexpr Coord& operator/=(T s) noexcept {
        m_x /= s;
        m_y /= s;
        return *this;
    }

    // --- Dot product & geometric helpers ----------------------------------

    /// Dot product of two coordinates (treating them as vectors).
    [[nodiscard]] constexpr T dot(Coord rhs) const noexcept {
        return m_x * rhs.m_x + m_y * rhs.m_y;
    }

    /** Squared Euclidean distance between this coord and another.
     *
     * Cheaper than sqrt(); use for radius comparisons where the exact
     * distance value is not needed (e.g., FOV, visibility checks).
     * Promotes to int64_t for the intermediate multiply to reduce overflow
     * risk on narrow scalars:
     *   int8_t  × int8_t  → up to ±32 760 fits in int16_t
     *   int16_t × int16_t → up to ±2 147 418 035 fits in int64_t
     *   int32_t × int32_t → product may overflow int64_t at extremes
     */
    [[nodiscard]] constexpr int64_t dist_sq(Coord other) const noexcept {
        auto dx = static_cast<int64_t>(other.m_x) - m_x;
        auto dy = static_cast<int64_t>(other.m_y) - m_y;
        if constexpr (sizeof(T) <= 2)
            return dx * dx + dy * dy;              // fits in int64_t
        else
            return static_cast<int64_t>(dx * dx + dy * dy);  // possible overflow at extremes
    }

    /// Manhattan (L1) distance. Useful for movement cost estimation on
    /// orthogonal grids.
    [[nodiscard]] static constexpr T manhattan(Coord a, Coord b) noexcept {
        return static_cast<T>(std::abs(a.m_x - b.m_x) + std::abs(a.m_y - b.m_y));
    }

    /// Chebyshev (L-infinity) distance — max(|dx|, |dy|). The natural metric
    /// for 8-directional (king's move) grid movement.
    [[nodiscard]] static constexpr T chebyshev(Coord a, Coord b) noexcept {
        return static_cast<T>(
            std::max(std::abs(a.m_x - b.m_x), std::abs(a.m_y - b.m_y)));
    }

    // --- Clamping ---------------------------------------------------------

    /// Clamp each axis to [lo, hi]. x in [lo_x, hi_x], y in [lo_y, hi_y].
    [[nodiscard]] constexpr Coord clamp(T lo_x, T hi_x,
                                        T lo_y, T hi_y) const noexcept {
        return {std::clamp(m_x, lo_x, hi_x), std::clamp(m_y, lo_y, hi_y)};
    }

    /// Clamp to a square centred at the origin: [-radius, +radius] on each axis.
    [[nodiscard]] constexpr Coord clamp_radius(T radius) const noexcept {
        return clamp(-radius, radius, -radius, radius);
    }

    /** Return a copy wrapped modulo other on each axis (result always
     * non-negative for positive moduli). Suitable for toroidal maps.
     */
    [[nodiscard]] constexpr Coord wrap_mod(Coord modulus) const noexcept {
        return {static_cast<T>((m_x % modulus.m_x + modulus.m_x) % modulus.m_x),
                static_cast<T>((m_y % modulus.m_y + modulus.m_y) % modulus.m_y)};
    }

    // --- Bounds testing ---------------------------------------------------

    /// True when both coordinates are zero. Useful as a sentinel check.
    [[nodiscard]] constexpr bool is_zero() const noexcept {
        return m_x == T{0} && m_y == T{0};
    }

    /// True when *all* coordinates are strictly negative.
    [[nodiscard]] constexpr bool all_negative() const noexcept {
        return m_x < T{0} && m_y < T{0};
    }

    /// True when *any* coordinate is negative.
    [[nodiscard]] constexpr bool has_negative() const noexcept {
        return m_x < T{0} || m_y < T{0};
    }

    /// True when *both* coordinates are non-negative (first quadrant).
    [[nodiscard]] constexpr bool non_negative() const noexcept {
        return m_x >= T{0} && m_y >= T{0};
    }

    // --- Reflections & rotations ------------------------------------------

    /// Reflect across the x-axis (negate y).
    [[nodiscard]] constexpr Coord reflect_x() const noexcept {
        return Coord(m_x, static_cast<T>(-m_y));
    }

    /// Reflect across the y-axis (negate x).
    [[nodiscard]] constexpr Coord reflect_y() const noexcept {
        return Coord(static_cast<T>(-m_x), m_y);
    }

    /// Reflect across the line y = x (swap axes).
    [[nodiscard]] constexpr Coord reflect_diagonal() const noexcept {
        return {m_y, m_x};
    }

    /// Rotate 90° counter-clockwise around the origin.
    [[nodiscard]] constexpr Coord rotate_90ccw() const noexcept {
        return Coord{T{-m_y}, m_x};
    }

    /// Rotate 180° around the origin.
    [[nodiscard]] constexpr Coord rotate_180() const noexcept {
        return Coord(static_cast<T>(-m_x), static_cast<T>(-m_y));
    }

    /// Rotate 90° clockwise around the origin.
    [[nodiscard]] constexpr Coord rotate_90cw() const noexcept {
        return Coord{m_y, T{-m_x}};
    }

    /// Apply a 90° ccw rotation to *this*.
    constexpr void rotate_90ccw_inplace() noexcept {
        T tmp = m_x;
        m_x = -m_y;
        m_y = tmp;
    }

    // --- Neighbour helpers ------------------------------------------------

    /// Return the neighbour coord in the given cardinal direction.
    [[nodiscard]] constexpr Coord neighbour(Dir dir) const noexcept {
        switch (dir) {
        case Dir::East:  return Coord{T(m_x + 1), m_y};
        case Dir::South: return Coord{T(m_x), T(m_y + 1)};
        case Dir::West:  return Coord{T(m_x - 1), m_y};
        case Dir::North: return Coord{T(m_x), T(m_y - 1)};
        }
        return *this;  // unreachable — suppresses -Wswitch
    }

    /** Return the 8-connected neighbour (including diagonals) at the given
     * ordinal matching Dir ordering: 0=east, 1=south-east, 2=south, 3=south-west,
     * 4=west, 5=north-west, 6=north, 7=north-east.
     */
    [[nodiscard]] constexpr Coord neighbour8(uint8_t octant) const noexcept {
        T dx = T{0}, dy = T{0};
        switch (octant) {
        case 0:  dx =  T{1}; dy =  T{0}; break;  // east
        case 1:  dx =  T{1}; dy =  T{1}; break;  // south-east
        case 2:  dx =  T{0}; dy =  T{1}; break;  // south
        case 3:  dx = -T{1}; dy =  T{1}; break;  // south-west
        case 4:  dx = -T{1}; dy =  T{0}; break;  // west
        case 5:  dx = -T{1}; dy = -T{0}; break;  // north-west
        case 6:  dx =  T{0}; dy = -T{1}; break;  // north
        case 7:  dx =  T{1}; dy = -T{1}; break;  // north-east
        default: return *this;                       // out-of-range
        }
        return Coord{T(m_x + dx), T(m_y + dy)};
    }

    // --- Cross-axis helpers -----------------------------------------------

    /// Swap x and y (equivalent to reflect_diagonal, but mutating).
    constexpr void swap_axes() noexcept { std::swap(m_x, m_y); }

    // --- Equality & ordering ----------------------------------------------

    /// Equality: both axes match.
    [[nodiscard]] constexpr bool operator==(Coord const&) const = default;

    /// Inequality via equality.
    [[nodiscard]] constexpr bool operator!=(Coord const&) const = default;

    /// Three-way comparison — lexicographic: x first, then y.
    ///
    /// This is the **coordinate order** mandated by DESIGN_STATE.md §9 invariant 12:
    /// iteration and tie-break orders must be explicit and stable.
    [[nodiscard]] constexpr auto operator<=>(Coord const&) const = default;

private:
    T m_x = T{0};
    T m_y = T{0};
};

// ---------------------------------------------------------------------------
// Convenience typedefs — the three widths you'll actually use on embedded.
// ---------------------------------------------------------------------------

/// int8_t per axis — adequate for maps up to ~±127 tiles on each side.
/// Ideal for Pico-class: 2 bytes per coord vs 8 with int32_t.
using Coord8  = Coord<int8_t>;

/// int16_t per axis — maps up to ~±32767 tiles. Good compromise for
/// Pi Zero / RP2044 where RAM is plentiful enough.
using Coord16 = Coord<int16_t>;

/// int32_t per axis — large worlds, or when coordinate arithmetic
/// (dist_sq, intermediate multiplies) needs extra headroom.
using Coord32 = Coord<int32_t>;

/// Origin (0, 0) for the most common width (int32_t). Use Coord<N>::origin
/// if you need a zero of a different width.
inline constexpr Coord32 coord_origin{0, 0};

/// Stream output for debugging — format "(x,y)".
template <typename T>
std::ostream& operator<<(std::ostream& os, Coord<T> c) noexcept {
    return os << '(' << c.x() << ',' << c.y() << ')';
}

}  // namespace Geo

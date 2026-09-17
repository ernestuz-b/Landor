#pragma once

#include "area.hpp"
#include "layer.hpp"

#include <cstdint>


namespace landor::geo
{

/**
 * Spatial unit used by the Cache / I/O machinery.
 *
 * Chunk is deliberately reserved for the storage side of Map. It is not the
 * generic name for an arbitrary simulation rectangle; game algorithms use
 * Area/Region for that.
 *
 * One Chunk refers to one layer over one aligned square of the map. A cache
 * fill for N layers can therefore require N Chunks for the same spatial area.
 *
 * Chunk sizes may differ inside the I/O machinery, but every Chunk is aligned
 * to the grid defined by its own side length. The Cache has its own canonical
 * chunk side; a smaller request is promoted to the containing canonical Chunk
 * before residency/fetch decisions are made.
 *
 * Chunk intentionally does not contain a Storage SourceId. Which backing
 * source supplies a layer is a separate resolution concern (Patch placement,
 * procedural/default state, working state, etc.). Keeping source binding out
 * of Chunk prevents the geometric I/O unit from becoming tied to one source
 * model.
 */
template<typename CoordT = Coord32>
class Chunk
{
public:
    using coord_type  = CoordT;
    using scalar_type = typename CoordT::scalar_type;
    using side_type   = std::uint32_t;
    using area_type   = Area<CoordT>;

    constexpr Chunk(
        LayerId layer,
        coord_type origin,
        side_type side) noexcept
        : m_layer(layer),
          m_origin(origin),
          m_side(side)
    {
    }

    [[nodiscard]] constexpr LayerId layer() const noexcept
    {
        return m_layer;
    }

    [[nodiscard]] constexpr coord_type origin() const noexcept
    {
        return m_origin;
    }

    [[nodiscard]] constexpr side_type side() const noexcept
    {
        return m_side;
    }

    [[nodiscard]] constexpr bool is_empty() const noexcept
    {
        return m_side == 0;
    }

    /**
     * True when the origin belongs to the grid defined by this Chunk's side.
     */
    [[nodiscard]] constexpr bool aligned() const noexcept
    {
        if (m_side == 0)
            return false;

        const auto side = static_cast<std::int64_t>(m_side);
        const auto x    = static_cast<std::int64_t>(m_origin.x());
        const auto y    = static_cast<std::int64_t>(m_origin.y());

        return (x % side) == 0 && (y % side) == 0;
    }

    [[nodiscard]] constexpr area_type area() const noexcept
    {
        if (m_side == 0)
            return {};

        const auto delta = static_cast<std::int64_t>(m_side) - 1;
        const auto max_x = static_cast<scalar_type>(
            static_cast<std::int64_t>(m_origin.x()) + delta);
        const auto max_y = static_cast<scalar_type>(
            static_cast<std::int64_t>(m_origin.y()) + delta);

        return area_type {
            m_origin,
            coord_type {max_x, max_y}
        };
    }

    [[nodiscard]] constexpr bool contains(coord_type position) const noexcept
    {
        return area().contains(position);
    }

    /**
     * Return the aligned Chunk of `side` containing position.
     *
     * This is the operation used when a small world request is promoted to the
     * Cache's canonical fetch/residency granularity.
     */
    [[nodiscard]] static constexpr Chunk containing(
        LayerId layer,
        coord_type position,
        side_type side) noexcept
    {
        if (side == 0)
            return Chunk {layer, position, 0};

        const auto aligned_x = align_down(position.x(), side);
        const auto aligned_y = align_down(position.y(), side);

        return Chunk {
            layer,
            coord_type {aligned_x, aligned_y},
            side
        };
    }

    template<Layer LayerT>
    [[nodiscard]] static constexpr Chunk containing(
        coord_type position,
        side_type side) noexcept
    {
        return containing(LayerT::id, position, side);
    }

    [[nodiscard]] constexpr bool operator==(const Chunk&) const noexcept = default;

private:
    [[nodiscard]] static constexpr scalar_type align_down(
        scalar_type value,
        side_type side) noexcept
    {
        const auto wide_side  = static_cast<std::int64_t>(side);
        const auto wide_value = static_cast<std::int64_t>(value);

        auto remainder = wide_value % wide_side;
        if (remainder < 0)
            remainder += wide_side;

        return static_cast<scalar_type>(wide_value - remainder);
    }

    LayerId    m_layer {};
    coord_type m_origin {};
    side_type  m_side = 0;
};


} // namespace landor::geo

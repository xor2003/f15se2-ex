#ifndef F15_MATH_MAP_POSITION_HPP
#define F15_MATH_MAP_POSITION_HPP
#include "rotation.hpp"
#include "interpolation.hpp"

namespace f15::math {
template<class B> struct MapBoundary;
template<class B> class MapMath;
// Coarse map coordinates, not fine view coordinates or render heights.
template<class B> class MapPosition {
    static_assert(std::is_same_v<B, FixedBackend> || std::is_same_v<B, ModernBackend>);
    using Rep = std::conditional_t<std::is_same_v<B, FixedBackend>, std::int16_t, double>;
    Rep x_{}, y_{};
    MapPosition(Rep x, Rep y) : x_(x), y_(y) {
        if constexpr (std::is_same_v<B, ModernBackend>)
            if (!std::isfinite(x) || !std::isfinite(y)) throw std::domain_error("non-finite map position");
    }
    friend struct MapBoundary<B>;
    friend class GuidanceMath<B>;
    friend class MapMath<B>;
public:
    MapPosition() = default;
    bool operator==(MapPosition other) const { return x_ == other.x_ && y_ == other.y_; }
    bool operator!=(MapPosition other) const { return !(*this == other); }
};
template<class B> class MapMath {
public:
    // Per-axis lerp; fixed reproduces the legacy lerpLinear word math.
    static MapPosition<B> interpolate(MapPosition<B> from, MapPosition<B> to, FrameFraction fraction) {
        if constexpr (std::is_same_v<B, FixedBackend>) {
            const auto num = fraction.numerator_, den = fraction.denominator_;
            const auto lerpAxis = [num, den](std::int16_t a, std::int16_t b) {
                const auto delta = std::int64_t(b) - a;
                const auto magnitude = delta < 0 ? -delta : delta;
                if (magnitude && num > INT64_MAX / magnitude)
                    throw std::overflow_error("map interpolation product overflow");
                return static_cast<std::int16_t>(a + static_cast<std::int32_t>(delta * num / den));
            };
            return MapPosition<B>(lerpAxis(from.x_, to.x_), lerpAxis(from.y_, to.y_));
        } else {
            const double t = double(fraction.numerator_) / fraction.denominator_;
            return MapPosition<B>(from.x_ + (to.x_ - from.x_) * t,
                                  from.y_ + (to.y_ - from.y_) * t);
        }
    }
};
}
#endif

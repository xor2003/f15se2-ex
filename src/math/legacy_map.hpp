#ifndef F15_MATH_LEGACY_MAP_HPP
#define F15_MATH_LEGACY_MAP_HPP
#include "map_position.hpp"
#include "horizontal.hpp"
#include <cmath>
namespace f15::math {
template<class B> struct MapBoundary {
    using Rep = std::conditional_t<std::is_same_v<B, FixedBackend>, std::int16_t, double>;
    static MapPosition<B> position(Rep x, Rep y) { return MapPosition<B>(x, y); }
    static MapPosition<B> position(ViewCoordinate<B, ViewXAxis> x, ViewCoordinate<B, ViewYAxis> y) {
        return position(HorizontalMath<B>::mapUnitsX(x), HorizontalMath<B>::mapUnitsY(y));
    }
    static Rep x(MapPosition<B> p) { return p.x_; }
    static Rep y(MapPosition<B> p) { return p.y_; }
    /* Legacy 16-bit map word; wraps modulo 2^16 through a defined conversion. */
    static std::int16_t wordX(MapPosition<B> p) { return word(p.x_); }
    static std::int16_t wordY(MapPosition<B> p) { return word(p.y_); }
private:
    static std::int16_t word(Rep v) {
        if constexpr (std::is_same_v<B, FixedBackend>) return v;
        else {
            const auto wrapped = static_cast<std::int64_t>(std::fmod(v, 65536.0));
            const auto bits = static_cast<std::uint16_t>(wrapped);
            return static_cast<std::int16_t>(bits < 32768 ? bits : static_cast<int>(bits) - 65536);
        }
    }
};
namespace legacy {
using Maps = MapBoundary<GameBackend>;
inline MapPosition<GameBackend> mapPosition(std::int16_t x, std::int16_t y) {
    return Maps::position(x, y);
}
// Current position derived from the fine view coordinates (quantized to map
// units under fixed, fractional under modern).
inline MapPosition<GameBackend> mapPosition(ViewCoordinate<GameBackend, ViewXAxis> x,
                                            ViewCoordinate<GameBackend, ViewYAxis> y) {
    return Maps::position(x, y);
}
// The coarse map words stored in g_viewX_/g_viewY_ and the frozen layouts.
inline std::int16_t mapWordX(MapPosition<GameBackend> p) { return Maps::wordX(p); }
inline std::int16_t mapWordY(MapPosition<GameBackend> p) { return Maps::wordY(p); }
}
}
#endif

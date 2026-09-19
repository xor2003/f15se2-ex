#ifndef F15_MATH_MAP_POSITION_HPP
#define F15_MATH_MAP_POSITION_HPP
#include "rotation.hpp"

namespace f15::math {
template<class B> struct MapBoundary;
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
public:
    MapPosition() = default;
};
}
#endif

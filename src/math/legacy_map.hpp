#ifndef F15_MATH_LEGACY_MAP_HPP
#define F15_MATH_LEGACY_MAP_HPP
#include "map_position.hpp"
#include "horizontal.hpp"
#include "../inttype.h"
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
    /* Fine-coordinate rep reads at the int32 storage/render boundary; modern
     * truncates only where the consumer is a word-domain field. */
    static std::int32_t fineWord(FineCoord<B> v) {
        if constexpr (std::is_same_v<B, FixedBackend>) return v.value_;
        else return static_cast<std::int32_t>(v.value_);
    }
    static typename FineCoord<B>::StepRep fineRep(FineCoord<B> v) { return v.value_; }
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
// int32 fine-coordinate rep for word-domain stores (snapshots).
inline std::int32_t fineWord(FineCoord<GameBackend> v) { return Maps::fineWord(v); }
// Untruncated fine-coordinate rep for render caches (g_projInterpX).
inline typename FineCoord<GameBackend>::StepRep fineRep(FineCoord<GameBackend> v) { return Maps::fineRep(v); }

/* Word-unit offset from the typed position to stored map coordinates. Each
 * argument keeps its original promotion (uint16 zero-extends, int16
 * sign-extends) — identical to the int16/word subtraction the call replaced. */
template<class B = GameBackend>
inline MapOffset<B> mapOffset(MapPosition<B> p, int x, int y) {
    using Rep = typename MapOffset<B>::Rep;
    return {static_cast<Rep>(MapBoundary<B>::x(p)) - x, static_cast<Rep>(MapBoundary<B>::y(p)) - y};
}

/* rangeApprox on a typed offset. Fixed reproduces the original exactly —
 * abs16Compat's (int16) wrap, max + min/2, 0x7fff cap. Modern keeps the
 * fraction, wraps deltas onto the same ring, and drops the cap (a legacy
 * int16-return limit, not a gameplay bound). */
template<class B = GameBackend>
inline auto mapRange(MapOffset<B> offset) {
    if constexpr (std::is_same_v<B, FixedBackend>) {
        const int dx = abs16Compat(offset.dx), dy = abs16Compat(offset.dy);
        const int dist = dx > dy ? (dy >> 1) + dx : (dx >> 1) + dy;
        return static_cast<int>(static_cast<std::int16_t>(dist > 0x7fff ? 0x7fff : dist));
    } else {
        const auto wrap = [](double v) {
            const double r = std::fmod(v, 65536.0);
            const double w = r >= 32768.0 ? r - 65536.0 : (r < -32768.0 ? r + 65536.0 : r);
            return std::fabs(w);
        };
        const double dx = wrap(offset.dx), dy = wrap(offset.dy);
        return dx > dy ? dx + dy * 0.5 : dy + dx * 0.5;
    }
}

/* rangeApprox on a plain word-domain delta pair (both sides already coarse
 * words — no typed position to preserve). Same fixed/modern split as
 * mapRange. */
template<class B = GameBackend>
inline auto mapRangeDelta(int dx, int dy) {
    using Rep = typename MapOffset<B>::Rep;
    return mapRange(MapOffset<B>{static_cast<Rep>(dx), static_cast<Rep>(dy)});
}

/* abs on a word-domain scalar where the original wrote abs((int16)v):
 * fixed keeps the (int16) wrap + abs of the word; modern takes the
 * un-narrowed magnitude — the word wrap was a width limit, not gameplay. */
template<class B = GameBackend, class T>
inline auto wordAbs(T v) {
    if constexpr (std::is_same_v<B, FixedBackend>)
        return std::abs(static_cast<int>(static_cast<std::int16_t>(v)));
    else
        return std::fabs(static_cast<double>(v));
}
}
}
#endif

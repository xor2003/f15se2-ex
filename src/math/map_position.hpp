#ifndef F15_MATH_MAP_POSITION_HPP
#define F15_MATH_MAP_POSITION_HPP
#include "rotation.hpp"
#include "interpolation.hpp"
#include <cmath>
#include <stdexcept>

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

/* Word-unit difference of two map coordinates: int for the fixed backend (the
 * original int16-promoted subtraction), continuous for modern. The raw
 * difference is unwrapped — the ring wrap happens inside each consumer exactly
 * where the original expression did it (abs16Compat's internal (int16) cast in
 * rangeApprox, explicit (uint16) casts in side tests). */
template<class B> struct MapOffset {
    using Rep = std::conditional_t<std::is_same_v<B, FixedBackend>, int, double>;
    Rep dx{}, dy{};
    /* The (uint16)delta side-test, wrapped onto [0, 65536) without losing the
     * modern fraction. */
    Rep ringX() const { return ring(dx); }
    Rep ringY() const { return ring(dy); }
private:
    static Rep ring(Rep v) {
        if constexpr (std::is_same_v<B, FixedBackend>) return static_cast<std::uint16_t>(v);
        else {
            const auto r = std::fmod(v, 65536.0);
            return r < 0 ? r + 65536.0 : r;
        }
    }
};
/* Fine (map << 5) object coordinate on the 21-bit ring the object tables used:
 * fine = mapWord * 32 + sub-tile fraction. int32 for the fixed backend, double
 * for modern so per-step sub-fine-unit motion is not truncated. Construction
 * wraps onto the ring, mirroring the & 0x1FFFFF mask at every original site. */
template<class B> class FineCoord {
    static constexpr std::int32_t ringMask = 0x1FFFFF;
    static constexpr double ringSize = 2097152.0; // ringMask + 1
    using Rep = std::conditional_t<std::is_same_v<B, FixedBackend>, std::int32_t, double>;
    Rep value_{};
    explicit FineCoord(Rep v) : value_(wrap(v)) {}
    static Rep wrap(Rep v) {
        if constexpr (std::is_same_v<B, FixedBackend>) return v & ringMask;
        else {
            if (!std::isfinite(v)) throw std::domain_error("non-finite fine coordinate");
            const auto r = std::fmod(v, ringSize);
            return r < 0 ? r + ringSize : r;
        }
    }
    friend struct MapBoundary<B>;
    friend class GuidanceMath<B>;
public:
    using StepRep = Rep;
    FineCoord() = default;
    /* Seed or wrap a fine-unit value onto the object ring. Fixed callers pass
     * the original int32 expression; modern may keep a fractional rep. */
    static FineCoord fromRep(Rep v) { return FineCoord(v); }
    // The (fine + step) & ring advance the original wrote at each integration site.
    FineCoord advanced(Rep step) const { return FineCoord(value_ + step); }
    /* The posX == 0 free-slot / origin convention. */
    bool isZero() const { return value_ == 0; }
    /* Signed ring delta (this - other fine units) centered on [-ring/2, ring/2):
     * the fineWrapDelta formula — mask the difference, fold the half-ring bit. */
    Rep deltaFrom(Rep other) const {
        if constexpr (std::is_same_v<B, FixedBackend>) {
            const Rep d = (value_ - other) & ringMask;
            return (d & (ringMask + 1) / 2) ? d - (ringMask + 1) : d;
        } else {
            Rep d = std::fmod(value_ - other, ringSize);
            if (d >= ringSize / 2) d -= ringSize;
            else if (d < -ringSize / 2) d += ringSize;
            return d;
        }
    }
    // Derived coarse map word (fine >> 5); modern keeps the fraction until the floor.
    std::uint16_t mapWord() const {
        if constexpr (std::is_same_v<B, FixedBackend>) return static_cast<std::uint16_t>(value_ >> 5);
        else return static_cast<std::uint16_t>(std::floor(value_ / 32.0));
    }
    /* Per-axis lerp; fixed reproduces the legacy lerpLinear word math. */
    static FineCoord interpolate(FineCoord a, FineCoord b, FrameFraction fraction) {
        if constexpr (std::is_same_v<B, FixedBackend>) {
            const auto num = fraction.numerator_, den = fraction.denominator_;
            const auto delta = std::int64_t(b.value_) - a.value_;
            const auto magnitude = delta < 0 ? -delta : delta;
            if (magnitude && num > INT64_MAX / magnitude)
                throw std::overflow_error("fine interpolation product overflow");
            return FineCoord(a.value_ + static_cast<std::int32_t>(delta * num / den));
        } else {
            const double t = double(fraction.numerator_) / fraction.denominator_;
            return FineCoord(a.value_ + (b.value_ - a.value_) * t);
        }
    }
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

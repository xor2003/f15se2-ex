#ifndef F15_MATH_ALTITUDE_HPP
#define F15_MATH_ALTITUDE_HPP

#include "flight_control.hpp"

namespace f15::math {
template<class B> struct AltitudeBoundary;
template<class B> class AltitudeMath;
struct FlightAltitudeUnit {};
struct ClimbRateUnit {};
struct TerrainHeightUnit {};
struct RenderHeightUnit {};
struct AirspeedSampleUnit {};

// Units are the engine's flight-altitude and compressed scene-height scales,
// not interchangeable SI distances. Only boundary adapters expose numbers.
template<class B, class Unit> class VerticalQuantity {
    static_assert(std::is_same_v<B, FixedBackend> || std::is_same_v<B, ModernBackend>);
    static_assert(std::is_same_v<Unit, FlightAltitudeUnit> || std::is_same_v<Unit, ClimbRateUnit> ||
                  std::is_same_v<Unit, TerrainHeightUnit> || std::is_same_v<Unit, RenderHeightUnit> ||
                  std::is_same_v<Unit, AirspeedSampleUnit>);
    using FixedRep = std::conditional_t<std::is_same_v<Unit, FlightAltitudeUnit>, std::uint32_t,
                     std::conditional_t<std::is_same_v<Unit, AirspeedSampleUnit>, std::uint16_t, std::int16_t>>;
    using Rep = std::conditional_t<std::is_same_v<B, FixedBackend>, FixedRep, double>;
    Rep value_{};
    explicit VerticalQuantity(Rep value) : value_(value) {
        if constexpr (std::is_same_v<B, ModernBackend>)
            if (!std::isfinite(value)) throw std::overflow_error("non-finite vertical result");
    }
    friend struct AltitudeBoundary<B>;
    friend class AltitudeMath<B>;
    friend class AirspeedMath<B>;
    friend class PropulsionMath<B>;
    friend class AerodynamicsMath<B>;
    friend class GuidanceMath<B>;
public:
    VerticalQuantity() = default;
    bool isZero() const { return value_ == 0; }
    bool isNegative() const { return value_ < 0; }
};
template<class B> using FlightAltitude = VerticalQuantity<B, FlightAltitudeUnit>;
template<class B> using ClimbRate = VerticalQuantity<B, ClimbRateUnit>;
template<class B> using TerrainHeight = VerticalQuantity<B, TerrainHeightUnit>;
template<class B> using RenderHeight = VerticalQuantity<B, RenderHeightUnit>;
template<class B> using AirspeedSample = VerticalQuantity<B, AirspeedSampleUnit>;

template<class B> class AltitudeMath {
    static std::int16_t word(std::int64_t v) {
        const auto bits = static_cast<std::uint16_t>(v);
        return static_cast<std::int16_t>(bits < 32768 ? bits : static_cast<int>(bits) - 65536);
    }
    static double expandedTerrain(double height) {
        if (height < 8192) return height;
        if (height < 12288) return (height - 8192) * 2 + 8192;
        return (height - 12288) * 4 + 16384;
    }
public:
    static RenderHeight<B> captureAltitudeHold(RenderHeight<B> height) {
        // Minimum capture height is autopilot policy in scene-height units.
        return height.value_ < 1000 ? RenderHeight<B>(1000) : height;
    }
    static ClimbRate<B> climb(AirspeedSample<B> speed, Coefficient<B> sineOfFlightPath) {
        if constexpr (std::is_same_v<B, FixedBackend>) {
            const auto product = static_cast<std::int64_t>(speed.value_ / 10) * sineOfFlightPath.value_;
            const auto rounded = product + 16384;
            return ClimbRate<B>(word(rounded / 32768 - (rounded % 32768 < 0 ? 1 : 0)));
        } else return ClimbRate<B>(speed.value_ / 10 * sineOfFlightPath.value_);
    }
    static FlightAltitude<B> integrate(FlightAltitude<B> altitude, ClimbRate<B> climb, SimulationStep<B> step) {
        if constexpr (std::is_same_v<B, FixedBackend>)
            return FlightAltitude<B>(altitude.value_ + static_cast<std::uint32_t>(climb.value_ / step.value_));
        else return FlightAltitude<B>(altitude.value_ + climb.value_ * step.value_);
    }
    static FlightAltitude<B> constrain(FlightAltitude<B> altitude, TerrainHeight<B> ground) {
        auto a = altitude.value_;
        if constexpr (std::is_same_v<B, FixedBackend>) {
            if (static_cast<std::uint16_t>(a) > 0xf230 ||
                static_cast<std::uint16_t>(a) < static_cast<std::uint16_t>(ground.value_))
                a = static_cast<std::uint32_t>(ground.value_);
            // Original flight ceiling is compatibility policy, not a limit
            // on the modern backend's altitude representation.
            if (a > 60000) a = 60000;
        } else {
            const auto floor = expandedTerrain(ground.value_);
            if (a < floor) a = floor;
        }
        return FlightAltitude<B>(a);
    }
    static RenderHeight<B> renderHeight(FlightAltitude<B> altitude) {
        auto a = altitude.value_;
        if constexpr (std::is_same_v<B, FixedBackend>) {
            if (a < 8192) return RenderHeight<B>(word(a));
            if (a < 16384) return RenderHeight<B>(word((a - 8192) / 2 + 8192));
            return RenderHeight<B>(word((a - 16384) / 4 + 12288));
        } else {
            if (a < 8192) return RenderHeight<B>(a);
            if (a < 16384) return RenderHeight<B>((a - 8192) / 2 + 8192);
            return RenderHeight<B>((a - 16384) / 4 + 12288);
        }
    }
    static FlightAltitude<B> landingApproach(FlightAltitude<B> altitude, TerrainHeight<B> ground, int divisor) {
        if (divisor <= 0) throw std::domain_error("landing divisor must be positive");
        if constexpr (std::is_same_v<B, FixedBackend>) {
            const auto g = static_cast<std::uint32_t>(ground.value_);
            auto a = altitude.value_ - (altitude.value_ - g) / static_cast<std::uint32_t>(divisor);
            const auto minimum = static_cast<std::uint32_t>(ground.value_ + 5);
            if (a < minimum) a = minimum;
            return FlightAltitude<B>(a);
        } else {
            const auto floor = expandedTerrain(ground.value_);
            auto a = altitude.value_ - (altitude.value_ - floor) / divisor;
            if (a < floor + 5) a = floor + 5;
            return FlightAltitude<B>(a);
        }
    }
    static FlightAltitude<B> obstacleEscape(FlightAltitude<B> altitude) {
        return FlightAltitude<B>(altitude.value_ + 500);
    }
};
}
#endif

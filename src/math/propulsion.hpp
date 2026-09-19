#ifndef F15_MATH_PROPULSION_HPP
#define F15_MATH_PROPULSION_HPP
#include "flight_control.hpp"
#include "airspeed.hpp"
#include <algorithm>

namespace f15::math {
template<class B> struct PropulsionBoundary;

struct FuelLoadUnit {};
struct FlightLoadUnit {};
// Legacy fuel counts and sixteenths of a G, respectively. Modern values retain
// fractions in these same scales; neither is an engine-thrust command.
template<class B, class Unit> class PropulsionQuantity {
    static_assert(std::is_same_v<B, FixedBackend> || std::is_same_v<B, ModernBackend>);
    static_assert(std::is_same_v<Unit, FuelLoadUnit> || std::is_same_v<Unit, FlightLoadUnit>);
    using FixedRep = std::conditional_t<std::is_same_v<Unit, FuelLoadUnit>, std::int16_t, std::int32_t>;
    using Rep = std::conditional_t<std::is_same_v<B, FixedBackend>, FixedRep, double>;
    Rep value_{};
    explicit PropulsionQuantity(Rep value) : value_(value) {
        if constexpr (std::is_same_v<B, ModernBackend>)
            if (!std::isfinite(value)) throw std::overflow_error("non-finite propulsion quantity");
    }
    friend class PropulsionMath<B>;
    friend struct PropulsionBoundary<B>;
    friend class AerodynamicsMath<B>;
public:
    PropulsionQuantity() = default;
};
template<class B> using FuelLoad = PropulsionQuantity<B, FuelLoadUnit>;
template<class B> using FlightLoad = PropulsionQuantity<B, FlightLoadUnit>;
enum class LandingGear { Retracted, Extended };

// Engine command units: 100 is military power, 144 is full afterburner.
// This quantity is not a force in newtons or a normalized [0, 1] fraction.
template<class B> class EngineThrust {
    static_assert(std::is_same_v<B, FixedBackend> || std::is_same_v<B, ModernBackend>);
    using Rep = std::conditional_t<std::is_same_v<B, FixedBackend>, std::int16_t, double>;
    Rep value_{};
    explicit EngineThrust(Rep value) : value_(value) {
        if constexpr (std::is_same_v<B, ModernBackend>)
            if (!std::isfinite(value)) throw std::overflow_error("non-finite engine thrust");
    }
    friend class PropulsionMath<B>;
    friend struct PropulsionBoundary<B>;
public:
    EngineThrust() = default;
    bool isZero() const { return value_ == 0; }
    bool operator==(EngineThrust other) const { return value_ == other.value_; }
};

template<class B> class PropulsionMath {
    static constexpr int fullAfterburner = 144;
    static constexpr int thrustLostPerHit = 4;
    static constexpr int responseSeconds = 4;
    static constexpr double referenceTicksPerSecond = 15;
    static constexpr int pitchDragScale = 80;
    static constexpr int speedAtMilitaryPower = 800;
    static constexpr int militaryPower = 100;
    static constexpr int maximumTargetKnots = 899;
    static constexpr int velocityUnitsPerKnot = 27;
    static constexpr int heightBand = 128;
    static constexpr int heightScale = 1024;
    static constexpr int fuelBand = 512;
    static constexpr int fuelBase = 100;
    static constexpr int fuelScale = 90;
    static constexpr int dragLoadLimit = 128;
    static constexpr int gearDragDivisor = 8;
    static std::int16_t word(std::int64_t value) {
        const auto bits = std::uint16_t(value);
        return static_cast<std::int16_t>(bits < 32768 ? int(bits) : int(bits) - 65536);
    }
    static std::int64_t floorDivide(std::int64_t value, int divisor) {
        return value / divisor - (value % divisor < 0 ? 1 : 0);
    }
public:
    static FlightSpeed<B> targetSpeed(EngineThrust<B> thrust, Coefficient<B> sinePitch,
                                      RenderHeight<B> height, FuelLoad<B> fuel,
                                      FlightLoad<B> load, LandingGear gear) {
        if constexpr (std::is_same_v<B, FixedBackend>) {
            const auto pitchDrag = word(floorDivide(std::int64_t(sinePitch.value_) * pitchDragScale + 16384, 32768));
            auto speed = word((std::int64_t(thrust.value_) - pitchDrag) * speedAtMilitaryPower / militaryPower);
            speed = word(floorDivide((std::uint16_t(height.value_) / heightBand + heightScale) * std::int64_t(speed), heightScale));
            speed = word(std::int64_t(speed) * (fuelBase - floorDivide(fuel.value_, fuelBand)) / fuelScale);
            const auto factor = std::int64_t(dragLoadLimit) - load.value_;
            const auto product = std::int64_t(speed) * factor;
            // The old expression has signed 32-bit intermediates. Reject its
            // undefined overflow domain rather than manufacture fixed parity.
            if (factor > INT32_MAX || product < INT32_MIN || product > INT32_MAX)
                throw std::overflow_error("target-speed load product overflow");
            speed = word(floorDivide(product, dragLoadLimit));
            if (gear == LandingGear::Extended) speed = word(speed - floorDivide(speed, gearDragDivisor));
            return FlightSpeed<B>(std::clamp(int(speed), 0, maximumTargetKnots) * velocityUnitsPerKnot);
        } else {
            double speed = (thrust.value_ - sinePitch.value_ * pitchDragScale) * speedAtMilitaryPower / militaryPower;
            speed *= (height.value_ / heightBand + heightScale) / heightScale;
            speed *= (fuelBase - fuel.value_ / fuelBand) / fuelScale;
            speed *= (dragLoadLimit - load.value_) / dragLoadLimit;
            if (gear == LandingGear::Extended) speed *= 1.0 - 1.0 / gearDragDivisor;
            if (!std::isfinite(speed)) throw std::overflow_error("non-finite target speed");
            return FlightSpeed<B>(std::clamp(speed, 0.0, double(maximumTargetKnots)) * velocityUnitsPerKnot);
        }
    }
    // Hits are a discrete damage count, not a backend-dependent quantity.
    static bool requiresDamageLimit(EngineThrust<B> requested, std::int16_t hits) {
        return hits != 0 && requested.value_ > fullAfterburner - int(hits) * thrustLostPerHit;
    }
    static EngineThrust<B> limitForDamage(EngineThrust<B> requested, std::int16_t hits) {
        if (!requiresDamageLimit(requested, hits)) return requested;
        const int ceiling = std::max(0, fullAfterburner - int(hits) * thrustLostPerHit);
        using Rep = typename EngineThrust<B>::Rep;
        return EngineThrust<B>(static_cast<Rep>(ceiling));
    }
    static EngineThrust<B> advance(EngineThrust<B> current, EngineThrust<B> target, SimulationStep<B> step) {
        if constexpr (std::is_same_v<B, FixedBackend>) {
            // Freeze both divisions and the one-unit-per-tick increment.
            int result = current.value_ + (int(target.value_) - current.value_) / responseSeconds / step.value_;
            if (target.value_ > result) ++result;
            if (target.value_ < result) result = target.value_;
            return EngineThrust<B>(static_cast<std::int16_t>(result));
        } else {
            if (target.value_ <= current.value_) return target;
            const double difference = target.value_ - current.value_;
            if (!std::isfinite(difference)) throw std::overflow_error("engine thrust difference overflow");
            // Exact integration of dT/dt = (target-T)/4 + 15 until target.
            // The additive rate replaces the legacy 15 Hz one-unit tick step.
            const double increment = (difference + responseSeconds * referenceTicksPerSecond) *
                                     -std::expm1(-step.value_ / responseSeconds);
            return EngineThrust<B>(current.value_ + std::min(difference, increment));
        }
    }
};
}
#endif

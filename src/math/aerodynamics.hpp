#ifndef F15_MATH_AERODYNAMICS_HPP
#define F15_MATH_AERODYNAMICS_HPP
#include "airspeed.hpp"
#include "propulsion.hpp"

namespace f15::math {
enum class StallSeverity { Normal, Severe };
template<class B> struct StallResponse { bool stalled; Angle<B> noseDrop; };
template<class B> struct LoadResponse { FlightLoad<B> load; PitchCommand<B> pitch; };
// The legacy "lift force" is an angular correction, not a force in newtons.
template<class B> class AerodynamicsMath {
    static constexpr int baseCornerKnots = 100;
    static constexpr int altitudeBand = 64;
    static constexpr int altitudeScale = 1024;
    static constexpr int rootLoadScale = 4;
    static constexpr int cornerLoadScale = 8;
    static constexpr int velocityUnitsPerKnot = 27;
    static constexpr int maximumLoad = 128; // Eight G, in sixteenths.
    static constexpr int pitchLoadDivisor = 2;
    static constexpr double commandRadiansPerSecond = 128 * (6.28318530717958647692 / 65536);
    static int signedWord(std::int64_t v) {
        const auto bits = std::uint16_t(v);
        return bits < 32768 ? int(bits) : int(bits) - 65536;
    }
public:
    static YawRate<B> turnRate(FlightLoad<B> load, FlightSpeed<B> speed,
                               Coefficient<B> sineRoll, Coefficient<B> cosinePitch) {
        constexpr int loadScale = 16;
        constexpr int turnScale = 128;
        constexpr int speedBand = 512;
        constexpr int baseDenominator = 32;
        if constexpr (std::is_same_v<B, FixedBackend>) {
            const auto roundedProduct = [](int value, int coefficient) {
                const auto product = std::int64_t(value) * coefficient + 16384;
                return signedWord(product / 32768 - (product % 32768 < 0 ? 1 : 0));
            };
            const int bankTurn = roundedProduct(signedWord(std::int64_t(load.value_) * loadScale), sineRoll.value_);
            const int denominator = std::uint16_t(speed.value_) / speedBand + baseDenominator;
            const int yaw = signedWord(bankTurn * turnScale / denominator);
            return YawRate<B>(static_cast<std::int16_t>(roundedProduct(yaw, cosinePitch.value_)));
        } else {
            const double denominator = speed.value_ / speedBand + baseDenominator;
            if (denominator <= 0) throw std::domain_error("non-positive turn-rate denominator");
            const double rate = load.value_ * sineRoll.value_ * (loadScale * turnScale) /
                denominator * cosinePitch.value_ * (6.28318530717958647692 / 65536);
            if (!std::isfinite(rate)) throw std::overflow_error("non-finite turn rate");
            return YawRate<B>(rate);
        }
    }
    // The table is legacy aerodynamic data, not an untyped runtime quantity.
    static FlightLoad<B> bankLoad(Angle<B> roll, const std::uint8_t (&table)[128]) {
        if constexpr (std::is_same_v<B, FixedBackend>) {
            const int angle = signedWord(roll.value_.raw());
            return FlightLoad<B>(table[(std::abs(angle) / 256) & 127]);
        } else {
            constexpr double pi = 3.141592653589793238462643383279502884;
            const double position = std::abs(roll.value_) * (128 / pi);
            if (!std::isfinite(position)) throw std::domain_error("non-finite bank angle");
            const int bin = static_cast<int>(position);
            const double fraction = position - bin;
            const double lower = table[bin & 127];
            return FlightLoad<B>(lower + fraction * (double(table[(bin + 1) & 127]) - lower));
        }
    }
    static LoadResponse<B> loadResponse(FlightLoad<B> bank, PitchCommand<B> pitch, bool airborne) {
        if constexpr (std::is_same_v<B, FixedBackend>) {
            const auto load = std::int64_t(bank.value_) + (airborne ? pitch.value_ / pitchLoadDivisor : 0);
            if (load < INT32_MIN || load > INT32_MAX)
                throw std::overflow_error("flight load overflow");
            if (load <= maximumLoad) return {FlightLoad<B>(static_cast<std::int32_t>(load)), pitch};
            // The old clamp accepts signed words and checks the upper bound
            // first, even when a negative pitch makes its bounds inverted.
            const int limit = signedWord(std::int64_t(maximumLoad) - bank.value_);
            const int command = limit > pitch.value_ ? pitch.value_ :
                limit >= 0 ? limit : limit <= -16384 ? pitch.value_ : 0;
            return {FlightLoad<B>(maximumLoad), PitchCommand<B>(static_cast<std::int16_t>(command))};
        } else {
            const double command = pitch.value_ / commandRadiansPerSecond;
            const double load = bank.value_ + (airborne ? command / pitchLoadDivisor : 0);
            if (!std::isfinite(load)) throw std::overflow_error("non-finite flight load");
            if (load <= maximumLoad) return {FlightLoad<B>(load), pitch};
            const double limit = maximumLoad - bank.value_;
            const double limited = limit > command ? command : std::max(0.0, limit);
            return {FlightLoad<B>(maximumLoad), PitchCommand<B>(limited * commandRadiansPerSecond)};
        }
    }
    static CornerSpeed<B> cornerSpeed(FlightAltitude<B> altitude, FlightLoad<B> load) {
        if constexpr (std::is_same_v<B, FixedBackend>) {
            // Preserve the unsigned dword product, then each signed-word store.
            const auto altitudeProduct = std::uint32_t(baseCornerKnots *
                std::uint32_t(altitude.value_ / altitudeBand + altitudeScale));
            const int altitudeSpeed = signedWord(altitudeProduct / altitudeScale);
            const auto rootArgument = std::int64_t(load.value_) * rootLoadScale;
            if (rootArgument < INT32_MIN || rootArgument > INT32_MAX)
                throw std::overflow_error("corner-speed load overflow");
            const int root = fixed::integerSqrtCompatible(signedWord(rootArgument));
            const auto product = std::int64_t(root) * altitudeSpeed;
            const int speed = signedWord(product / cornerLoadScale - (product % cornerLoadScale < 0 ? 1 : 0));
            return CornerSpeed<B>(static_cast<std::int16_t>(signedWord(std::abs(speed))));
        } else {
            const double altitudeSpeed = baseCornerKnots + altitude.value_ *
                (double(baseCornerKnots) / (altitudeBand * altitudeScale));
            const double root = std::sqrt(std::abs(load.value_)) * 2;
            return CornerSpeed<B>(std::abs(altitudeSpeed * (root / cornerLoadScale)));
        }
    }
    static StallSpeed<B> stallThreshold(CornerSpeed<B> corner) {
        if constexpr (std::is_same_v<B, FixedBackend>)
            return StallSpeed<B>(static_cast<std::int16_t>(signedWord(int(corner.value_) * velocityUnitsPerKnot)));
        else return StallSpeed<B>(corner.value_ * velocityUnitsPerKnot);
    }
    static bool aboveStall(FlightSpeed<B> speed, StallSpeed<B> threshold) {
        if constexpr (std::is_same_v<B, FixedBackend>) return std::uint16_t(speed.value_) > std::uint16_t(threshold.value_);
        else return speed.value_ > threshold.value_;
    }
    static bool belowStall(FlightSpeed<B> speed, StallSpeed<B> threshold) {
        if constexpr (std::is_same_v<B, FixedBackend>) return std::uint16_t(speed.value_) < std::uint16_t(threshold.value_);
        else return speed.value_ < threshold.value_;
    }
    static StallResponse<B> stallResponse(FlightSpeed<B> speed, StallSpeed<B> threshold,
                                          StallSeverity severity, SimulationStep<B> step) {
        if (severity != StallSeverity::Normal && severity != StallSeverity::Severe)
            throw std::domain_error("invalid stall severity");
        if (!belowStall(speed, threshold)) return {false, {}};
        const int divisor = severity == StallSeverity::Severe ? 2 : 4;
        if constexpr (std::is_same_v<B, FixedBackend>) {
            // Legacy stall correction is per tick, without frequency scaling.
            const int deficit = int(std::uint16_t(threshold.value_)) - int(std::uint16_t(speed.value_));
            return {true, Angle<B>(fixed::Angle16(deficit / divisor))};
        } else {
            const double deficit = threshold.value_ - speed.value_;
            if (!std::isfinite(deficit)) throw std::overflow_error("stall speed deficit overflow");
            // Calibrate to the current 15 Hz simulation, retaining fractions at other rates.
            const double radians = deficit * ((15 * step.value_ / divisor) * (6.28318530717958647692 / 65536));
            return {true, Angle<B>(radians)};
        }
    }
    static Angle<B> liftCorrection(StallSpeed<B> stallSpeed, FlightSpeed<B> speed) {
        if constexpr (std::is_same_v<B, FixedBackend>) {
            // abs(INT_MIN) and abs(INT_MAX)+1 were undefined in the old formula.
            if (speed.value_ <= -INT32_MAX || speed.value_ == INT32_MAX)
                throw std::overflow_error("lift denominator outside native signed range");
            const auto magnitude = speed.value_ < 0 ? -speed.value_ : speed.value_;
            auto result = signedWord(signedWord(stallSpeed.value_) * 3072 / (magnitude + 1));
            if (std::uint16_t(result) > 8192) result = 8192;
            return Angle<B>(fixed::Angle16(result));
        } else {
            const auto correction = stallSpeed.value_ / (std::abs(speed.value_) + 1) * 3072;
            if (!std::isfinite(correction)) throw std::overflow_error("non-finite lift correction");
            return Angle<B>(std::clamp(correction, 0.0, 8192.0) * (6.28318530717958647692 / 65536));
        }
    }
    static Angle<B> pitchTrim(Angle<B> lift, Coefficient<B> cosineRoll) {
        if constexpr (std::is_same_v<B, FixedBackend>) {
            const auto correction = signedWord(lift.value_.raw() - 768);
            const auto product = std::int64_t(correction) * cosineRoll.value_ + 16384;
            const auto rounded = product / 32768 - (product % 32768 < 0 ? 1 : 0);
            return Angle<B>(fixed::Angle16(signedWord(rounded)));
        } else {
            const auto result = (lift.value_ - 768 * (6.28318530717958647692 / 65536)) * cosineRoll.value_;
            if (!std::isfinite(result)) throw std::overflow_error("non-finite pitch trim");
            return Angle<B>(result);
        }
    }
};
}
#endif

#ifndef F15_MATH_PROPULSION_HPP
#define F15_MATH_PROPULSION_HPP
#include "flight_control.hpp"
#include <algorithm>

namespace f15::math {
template<class B> struct PropulsionBoundary;

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
public:
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

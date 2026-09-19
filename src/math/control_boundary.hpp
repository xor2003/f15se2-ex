#ifndef F15_MATH_CONTROL_BOUNDARY_HPP
#define F15_MATH_CONTROL_BOUNDARY_HPP
#ifndef F15_MATH_BOUNDARY_ACCESS
#error "Raw control conversion is restricted to reviewed boundary adapters"
#endif

#include "flight_control.hpp"
#include <stdexcept>

namespace f15::math {
template<> struct ControlBoundary<FixedBackend> {
    static RollCommand<FixedBackend> roll(std::int32_t units) { return RollCommand<FixedBackend>(units); }
    static PitchCommand<FixedBackend> pitch(std::int16_t units) { return PitchCommand<FixedBackend>(units); }
    static YawRate<FixedBackend> yaw(std::int16_t wordsPerSecond) { return YawRate<FixedBackend>(wordsPerSecond); }
    static int roll(RollCommand<FixedBackend> value) { return value.value_; }
    static std::int16_t pitch(PitchCommand<FixedBackend> value) { return value.value_; }
    static std::int16_t yaw(YawRate<FixedBackend> value) { return value.value_; }
    static SimulationStep<FixedBackend> frequency(int hz) {
        if (hz <= 0) throw std::domain_error("simulation frequency must be positive");
        return SimulationStep<FixedBackend>(hz);
    }
    static JoystickSample joystick(std::uint8_t roll, std::uint8_t pitch) { return {roll, pitch}; }
};
template<> struct ControlBoundary<ModernBackend> {
    template<class Axis> static AxisRate<ModernBackend, Axis> radiansPerSecond(double value) {
        if (!std::isfinite(value)) throw std::domain_error("non-finite angular rate");
        return AxisRate<ModernBackend, Axis>(value);
    }
    template<class Axis> static double radiansPerSecond(AxisRate<ModernBackend, Axis> value) { return value.value_; }
    static SimulationStep<ModernBackend> seconds(double dt) {
        if (!std::isfinite(dt) || dt <= 0 || dt > 1) throw std::domain_error("simulation step must be in (0,1] seconds");
        return SimulationStep<ModernBackend>(dt);
    }
    static SimulationStep<ModernBackend> frequency(int hz) {
        if (hz <= 0) throw std::domain_error("simulation frequency must be positive");
        return seconds(1.0 / hz);
    }
    static JoystickSample joystick(std::uint8_t roll, std::uint8_t pitch) { return {roll, pitch}; }
};
} // namespace f15::math
#endif

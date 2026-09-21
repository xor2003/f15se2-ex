#ifndef F15_MATH_LEGACY_FLIGHT_CONTROL_HPP
#define F15_MATH_LEGACY_FLIGHT_CONTROL_HPP
#define F15_MATH_BOUNDARY_ACCESS
#include "control_boundary.hpp"
#undef F15_MATH_BOUNDARY_ACCESS

namespace f15::math::legacy {
using Controls = ControlBoundary<GameBackend>;
inline RollCommand<GameBackend> rollCommand(int value) { return Controls::roll(value); }
inline PitchCommand<GameBackend> pitchCommand(int value) {
    const auto bits = static_cast<std::uint16_t>(value);
    return Controls::pitch(static_cast<std::int16_t>(bits <= 32767 ? bits : static_cast<int>(bits) - 65536));
}
inline int rollInput(RollCommand<GameBackend> value) { return Controls::roll(value); }
inline std::int16_t pitchInput(PitchCommand<GameBackend> value) { return Controls::pitch(value); }
/* Analog response matching the byte-stick curve's endpoints: roll ±126 and
 * pitch +42/−21 in 128-word-rate units (128·2π/65536 rad/s), plus the
 * 8000/32768 axis deadzone axisByte applies. Linear between — the nibble
 * quantization is a legacy limit, not behavior to reproduce. */
inline AnalogResponse analogResponseForLegacyCurve() {
    using M = ControlBoundary<ModernBackend>;
    constexpr double wordRate = 128.0 * (6.28318530717958647692 / 65536);
    return {M::radiansPerSecond<RollAxis>(126 * wordRate),
            M::radiansPerSecond<PitchAxis>(42 * wordRate),
            M::radiansPerSecond<PitchAxis>(21 * wordRate), 8000.0 / 32768.0};
}
inline int updateControlFromWords(RollCommand<GameBackend> &roll, PitchCommand<GameBackend> &pitch,
                                 int (*update)(int *, std::int16_t *)) {
    int r = rollInput(roll);
    auto p = pitchInput(pitch);
    const auto originalRoll = r;
    const auto originalPitch = p;
    const int result = update(&r, &p);
    // A legacy callback can only express word-sized changes. Preserve the
    // caller's fractional state on axes the callback leaves untouched.
    if (r != originalRoll) roll = rollCommand(r);
    if (p != originalPitch) pitch = pitchCommand(p);
    return result;
}
} // namespace f15::math::legacy
#endif

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
inline int updateControlFromWords(RollCommand<GameBackend> &roll, PitchCommand<GameBackend> &pitch,
                                 int (*update)(int *, std::int16_t *)) {
    int r = rollInput(roll);
    auto p = pitchInput(pitch);
    const int result = update(&r, &p);
    roll = rollCommand(r);
    pitch = pitchCommand(p);
    return result;
}
} // namespace f15::math::legacy
#endif

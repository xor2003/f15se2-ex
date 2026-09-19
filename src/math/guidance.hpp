#ifndef F15_MATH_GUIDANCE_HPP
#define F15_MATH_GUIDANCE_HPP
#include "altitude.hpp"
#include "airspeed.hpp"
#include "propulsion.hpp"
#include "map_position.hpp"
#include <algorithm>

namespace f15::math {
enum class RecoveryDirection { South = -1, Neutral = 0, North = 1 };
template<class B> struct RecoveryApproach {
    Angle<B> bearing;
    RenderHeight<B> height;
    bool exitSlowMotion;
    bool allowBrakes;
};
template<class B> class GuidanceMath {
    static constexpr int headingLimit = 5120;
    static constexpr int climbLimit = 3072;
    static constexpr int heightGain = 16;
    static constexpr int rollDivisor = 64;
    static constexpr int pitchDivisor = 128;
    static constexpr int rollLimit = 24;
    static constexpr int pitchLimit = 8;
    static constexpr double wordRadians = 6.28318530717958647692 / 65536;
    static int word(int value) {
        const auto bits = std::uint16_t(value);
        return bits < 32768 ? int(bits) : int(bits) - 65536;
    }
    static int floorDivide(int value, int divisor) {
        return value / divisor - (value % divisor < 0 ? 1 : 0);
    }
public:
    static RecoveryApproach<B> recoveryApproach(MapPosition<B> target, MapPosition<B> player,
        Angle<B> heading, bool carrier, RecoveryDirection direction, bool inCorridor) {
        int ns = static_cast<int>(direction);
        if constexpr (std::is_same_v<B, FixedBackend>) {
            const auto clamp = [](int value, int low, int high) {
                value = word(value);
                return value > high ? high : value >= low ? value : value <= -16384 ? high : low;
            };
            int dx = word(int(target.x_) - player.x_), dy = word(int(target.y_) - player.y_);
            if (!carrier) ns = dy > 0 ? -1 : dy < 0 ? 1 : 0;
            dy = word(dy + (carrier ? 30 : 64) * ns);
            int error = word(std::abs(word(heading.value_.raw())));
            if (ns == -1) {
                dx = word(-dx); dy = word(-dy);
                error = word(std::abs(word(int(heading.value_.raw()) - 32768)));
            }
            int height = clamp((std::abs(dx) + std::abs(dy)) * 2 + error / 32, 50, 4096);
            const bool slow = height < 4096;
            if (carrier) height += 100;
            if (inCorridor && std::abs(error) < 512) height = -20;
            dy = word(int(target.y_) + (carrier ? 28 : 56) * ns);
            dy = word(dy + clamp(std::abs(dx) * 4 + error / 16, 0, 3072) * ns);
            const bool brakes = error <= 16384;
            if (!brakes) { dx = target.x_; height = 4096; }
            else dx = word(int(target.x_) + ns * dx * 2);
            return {Angle<B>(fixed::computeBearing(word(dx - player.x_), word(int(player.y_) - dy))),
                RenderHeight<B>(static_cast<std::int16_t>(height)), slow, brakes};
        } else {
            double dx = target.x_ - player.x_, dy = target.y_ - player.y_;
            if (!std::isfinite(dx) || !std::isfinite(dy)) throw std::overflow_error("recovery map difference overflow");
            if (!carrier) ns = dy > 0 ? -1 : dy < 0 ? 1 : 0;
            const double originalDx = dx, originalDy = dy;
            dy += (carrier ? 30 : 64) * ns;
            const double error = std::abs((heading - (ns == -1 ? Angle<B>(32768 * wordRadians) : Angle<B>{})).value_) / wordRadians;
            if (ns == -1) dx = -dx;
            double height = std::clamp((std::abs(dx) + std::abs(dy)) * 2 + error / 32, 50.0, 4096.0);
            const bool slow = height < 4096;
            if (carrier) height += 100;
            if (inCorridor && error < 512) height = -20;
            const double aimY = -originalDy - ((carrier ? 28 : 56) + std::clamp(std::abs(dx) * 4 + error / 16, 0.0, 3072.0)) * ns;
            const bool brakes = error <= 16384;
            const double aimX = originalDx + (brakes ? ns * dx * 2 : 0);
            if (!brakes) height = 4096;
            if (!std::isfinite(aimX) || !std::isfinite(aimY)) throw std::overflow_error("recovery aim overflow");
            return {Angle<B>(aimX == 0 && aimY == 0 ? 32768 * wordRadians : std::atan2(aimX, aimY)),
                RenderHeight<B>(height), slow, brakes};
        }
    }

    static EngineThrust<B> recoveryThrust(Angle<B> bankTarget, RenderHeight<B> approachHeight) {
        if constexpr (std::is_same_v<B, FixedBackend>) {
            const int request = std::abs(word(bankTarget.value_.raw())) / 256 + approachHeight.value_ / 64;
            return EngineThrust<B>(static_cast<std::int16_t>(std::clamp(request, 35, 80)));
        } else {
            const double request = std::abs(bankTarget.value_) / (256 * wordRadians) + approachHeight.value_ / 64;
            if (!std::isfinite(request)) throw std::domain_error("non-finite recovery thrust input");
            return EngineThrust<B>(std::clamp(request, 35.0, 80.0));
        }
    }

    // Speed is the indicated-speed sample used by guidance, expressed in the
    // flight-speed scale (27 units per knot), not a newly integrated velocity.
    static Angle<B> recoveryBank(Angle<B> bearing, Angle<B> heading,
        FlightSpeed<B> indicatedSpeed, bool inCorridor) {
        if (indicatedSpeed.value_ < 0)
            throw std::domain_error("negative recovery indicated speed");
        if (inCorridor) return {};
        if constexpr (std::is_same_v<B, FixedBackend>) {
            const int limit = (indicatedSpeed.value_ / 27 / 16) * 256;
            // Widen the doubled result before encoding its signed-word store.
            const int error = word(int(bearing.value_.raw()) - int(heading.value_.raw()));
            return Angle<B>(fixed::Angle16(static_cast<std::uint16_t>(std::clamp(error, -limit, limit) * 2)));
        } else {
            const double limit = indicatedSpeed.value_ / 27 / 16 * (256 * wordRadians);
            return Angle<B>(std::clamp((bearing - heading).value_, -limit, limit) * 2);
        }
    }

    static FlightCommands<B> recoveryAttitude(RenderHeight<B> target, RenderHeight<B> height,
        EulerAngles<B> attitude, Angle<B> bankTarget, Angle<B> trim) {
        if constexpr (std::is_same_v<B, FixedBackend>) {
            const int roll = -std::clamp(floorDivide(word(int(bankTarget.value_.raw()) -
                int(attitude.roll.value_.raw())), rollDivisor), -32, 32);
            const int pitchTarget = std::clamp(floorDivide(int(target.value_) - height.value_, 8) +
                floorDivide(word(trim.value_.raw()), pitchDivisor), -24, 24);
            const int pitch = std::clamp(pitchTarget - floorDivide(word(attitude.pitch.value_.raw()),
                pitchDivisor), -16, 16);
            return {RollCommand<B>(roll), PitchCommand<B>(static_cast<std::int16_t>(pitch))};
        } else {
            const double rollError = (bankTarget - attitude.roll).value_;
            const double pitchTarget = (target.value_ - height.value_) / 8 +
                trim.value_ / (pitchDivisor * wordRadians);
            if (!std::isfinite(rollError) || !std::isfinite(pitchTarget) || !std::isfinite(attitude.pitch.value_))
                throw std::domain_error("non-finite recovery guidance input");
            const double roll = -std::clamp(rollError / (rollDivisor * wordRadians), -32.0, 32.0);
            const double pitch = std::clamp(std::clamp(pitchTarget, -24.0, 24.0) -
                attitude.pitch.value_ / (pitchDivisor * wordRadians), -16.0, 16.0);
            return {RollCommand<B>(roll * pitchDivisor * wordRadians), PitchCommand<B>(pitch * pitchDivisor * wordRadians)};
        }
    }

    // Targets use the scene-height scale stored by the original autopilot,
    // not expanded flight altitude. Recovery-waypoint steering is separate.
    static FlightCommands<B> altitudeHold(RenderHeight<B> target, RenderHeight<B> height,
        EulerAngles<B> attitude, Angle<B> bearing, Angle<B> headingOffset, Angle<B> trim) {
        if constexpr (std::is_same_v<B, FixedBackend>) {
            const int headingError = word(int(headingOffset.value_.raw()) -
                int(attitude.yaw.value_.raw()) + int(bearing.value_.raw()));
            const int bankTarget = std::clamp(headingError, -headingLimit, headingLimit) * 2;
            const int roll = -std::clamp(floorDivide(word(bankTarget - int(attitude.roll.value_.raw())),
                rollDivisor), -rollLimit, rollLimit);
            const int pitchTarget = std::clamp((int(target.value_) - height.value_) * heightGain -
                word(trim.value_.raw()), -headingLimit, climbLimit);
            const int pitch = std::clamp(floorDivide(pitchTarget - word(attitude.pitch.value_.raw()),
                pitchDivisor), -pitchLimit, pitchLimit);
            return {RollCommand<B>(roll), PitchCommand<B>(static_cast<std::int16_t>(pitch))};
        } else {
            const double headingError = (headingOffset - attitude.yaw + bearing).value_;
            const double bankTarget = std::clamp(headingError, -headingLimit * wordRadians,
                headingLimit * wordRadians) * 2;
            const double rollError = (Angle<B>(bankTarget) - attitude.roll).value_;
            const double altitudeError = (target.value_ - height.value_) * (heightGain * wordRadians) - trim.value_;
            if (!std::isfinite(rollError) || !std::isfinite(altitudeError) || !std::isfinite(attitude.pitch.value_))
                throw std::domain_error("non-finite altitude-hold input");
            const double pitchTarget = std::clamp(altitudeError, -headingLimit * wordRadians, climbLimit * wordRadians);
            const double roll = -std::clamp(rollError / (rollDivisor * wordRadians), -double(rollLimit), double(rollLimit));
            const double pitch = std::clamp((pitchTarget - attitude.pitch.value_) / (pitchDivisor * wordRadians),
                -double(pitchLimit), double(pitchLimit));
            return {RollCommand<B>(roll * pitchDivisor * wordRadians), PitchCommand<B>(pitch * pitchDivisor * wordRadians)};
        }
    }
};
}
#endif

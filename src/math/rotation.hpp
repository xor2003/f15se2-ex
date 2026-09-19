#ifndef F15_MATH_ROTATION_HPP
#define F15_MATH_ROTATION_HPP

#include "fixed_math.hpp"
#include <array>
#include <cmath>
#include <type_traits>

namespace f15::math {
template<class B> class AirspeedMath;
template<class B> class AerodynamicsMath;
template<class B> class PropulsionMath;
template<class B> class GuidanceMath;

struct FixedBackend {};
struct ModernBackend {};
template<class B> class RotationMath;
template<class B> class PoseInterpolation;
template<class B> class FlightControlMath;
template<class B> class AltitudeMath;
template<class B> class HorizontalMath;
template<class B> struct Boundary;

// Representation is deliberately absent from the public quantity API.
template<class B> class Angle {
    using Rep = std::conditional_t<std::is_same_v<B, FixedBackend>, fixed::Angle16, double>;
    Rep value_{};
    explicit Angle(Rep value) : value_(value) {
        if constexpr (std::is_same_v<B, ModernBackend>) {
            constexpr double pi = 3.141592653589793238462643383279502884;
            value_ = std::remainder(value_, 2 * pi);
            if (value_ >= pi) value_ -= 2 * pi;
        }
    }
    friend class RotationMath<B>;
    friend class PoseInterpolation<B>;
    friend class FlightControlMath<B>;
    friend class AerodynamicsMath<B>;
    friend class GuidanceMath<B>;
    friend struct Boundary<B>;
public:
    Angle() = default;
    Angle operator-() const { return Angle(-value_); }
    Angle operator+(Angle other) const { return Angle(value_ + other.value_); }
    Angle operator-(Angle other) const { return Angle(value_ - other.value_); }
    Angle &operator+=(Angle other) { return *this = *this + other; }
    Angle &operator-=(Angle other) { return *this = *this - other; }
    bool operator==(Angle other) const {
        if constexpr (std::is_same_v<B, FixedBackend>) return value_.raw() == other.value_.raw();
        else return value_ == other.value_;
    }
    bool operator!=(Angle other) const { return !(*this == other); }
    static Angle quarterTurn() {
        if constexpr (std::is_same_v<B, FixedBackend>) return Angle(fixed::Angle16(0x4000));
        else return Angle(1.57079632679489661923);
    }
    static Angle halfTurn() { return quarterTurn() + quarterTurn(); }
};

template<class B> struct EulerAngles {
    Angle<B> yaw, pitch, roll;
};

template<class B> struct RecoveredAttitude {
    EulerAngles<B> angles;
    bool needsRefresh;
};

template<class B> class Coefficient {
    using Rep = std::conditional_t<std::is_same_v<B, FixedBackend>, std::int16_t, double>;
    Rep value_{};
    explicit Coefficient(Rep value) : value_(value) {}
    friend class RotationMath<B>;
    friend class AltitudeMath<B>;
    friend class HorizontalMath<B>;
    friend class AirspeedMath<B>;
    friend class PropulsionMath<B>;
    friend class AerodynamicsMath<B>;
    friend struct Boundary<B>;
public:
    Coefficient() = default;
};

template<class B> class Matrix3 {
    using Rep = std::conditional_t<std::is_same_v<B, FixedBackend>, fixed::Matrix3x3Q15,
                                   std::array<double, 9>>;
    Rep value_{};
    explicit Matrix3(Rep value) : value_(value) {}
    friend class RotationMath<B>;
    friend struct Boundary<B>;
public:
    Matrix3() = default;
    static Matrix3 identity() {
        if constexpr (std::is_same_v<B, FixedBackend>) {
            Rep result;
            result(0, 0) = result(1, 1) = result(2, 2) = 32767;
            return Matrix3(result);
        } else {
            return Matrix3({1, 0, 0, 0, 1, 0, 0, 0, 1});
        }
    }
    Matrix3 operator*(const Matrix3 &other) const {
        if constexpr (std::is_same_v<B, FixedBackend>) {
            return Matrix3(value_ * other.value_);
        } else {
            Rep result{};
            for (int row = 0; row < 3; ++row)
                for (int col = 0; col < 3; ++col)
                    for (int k = 0; k < 3; ++k)
                        result[row * 3 + col] += value_[row * 3 + k] * other.value_[k * 3 + col];
            return Matrix3(result);
        }
    }
};

template<class B> struct RotationTerms {
    Coefficient<B> sinYaw, cosYaw, sinPitch, cosPitch, sinRoll, cosRoll;
};

template<> class RotationMath<FixedBackend> {
    const std::int16_t *table_;
    Matrix3<FixedBackend> axisDelta(Angle<FixedBackend> angle, int axis) const {
        auto result = Matrix3<FixedBackend>::identity();
        const auto s = sine(angle).value_, c = cosine(angle).value_;
        const int a = axis == 0 ? 4 : 0, b = axis == 2 ? 4 : 8;
        const int positive = axis == 0 ? 7 : axis == 1 ? 2 : 1;
        const int negative = axis == 0 ? 5 : axis == 1 ? 6 : 3;
        result.value_(a / 3, a % 3) = result.value_(b / 3, b % 3) = c;
        result.value_(positive / 3, positive % 3) = s;
        result.value_(negative / 3, negative % 3) = static_cast<std::int16_t>(-s);
        return result;
    }
public:
    template<std::size_t N>
    explicit RotationMath(const std::int16_t (&table)[N]) : table_(table) {
        static_assert(N >= 257, "interpolated sine requires a guard sample");
    }
    Coefficient<FixedBackend> sine(Angle<FixedBackend> angle) const {
        return Coefficient<FixedBackend>(fixed::sine(angle.value_, table_).raw());
    }
    Coefficient<FixedBackend> cosine(Angle<FixedBackend> angle) const {
        return Coefficient<FixedBackend>(fixed::cosine(angle.value_, table_).raw());
    }
    RotationTerms<FixedBackend> terms(EulerAngles<FixedBackend> angles) const {
        return {sine(angles.yaw), cosine(angles.yaw), sine(angles.pitch),
                cosine(angles.pitch), sine(angles.roll), cosine(angles.roll)};
    }
    Matrix3<FixedBackend> rotation(EulerAngles<FixedBackend> angles) const {
        return Matrix3<FixedBackend>(fixed::Matrix3x3Q15::rotation(
            angles.yaw.value_, angles.pitch.value_, angles.roll.value_, table_));
    }
    // Historical object builder: not simply transpose(rotation(angles)).
    Matrix3<FixedBackend> objectRotation(EulerAngles<FixedBackend> angles) const {
        return Matrix3<FixedBackend>(fixed::Matrix3x3Q15::inverseRotation(
            angles.yaw.value_, angles.pitch.value_, angles.roll.value_, table_));
    }
    Matrix3<FixedBackend> yawDelta(Angle<FixedBackend> angle) const { return axisDelta(angle, 1); }
    Matrix3<FixedBackend> pitchDelta(Angle<FixedBackend> angle) const { return axisDelta(angle, 0); }
    Matrix3<FixedBackend> rollDelta(Angle<FixedBackend> angle) const { return axisDelta(angle, 2); }

    RecoveredAttitude<FixedBackend> recover(const Matrix3<FixedBackend> &matrix,
                                           bool rollWasNonzero) const {
        const auto &m = matrix.value_.values();
        const auto pitch = fixed::valueToAngle(-static_cast<int>(m[5]), table_);
        const int cp = fixed::cosine(pitch, table_).raw();
        auto fold = [](fixed::Angle16 angle, int s, int c) {
            if (s <= 0 && c < 0) angle = angle + fixed::Angle16(0x8000);
            if (s > 0 && c < 0) angle = fixed::Angle16(0x8000) - angle;
            if (s < 0 && c > 0) angle = -angle;
            return angle;
        };
        auto axis = [&](int s, int c) {
            const int component = std::abs(s) < 0x5a81 ? s : c;
            const auto bits = fixed::signedRatio16Bits(component, cp);
            const int ratio = bits <= 32767 ? bits : static_cast<int>(bits) - 65536;
            auto angle = fixed::valueToAngle(std::abs(ratio), table_);
            if (std::abs(s) >= 0x5a81) angle = fixed::Angle16(0x4000) - angle;
            return fold(angle, s, c);
        };
        const auto yaw = cp ? axis(m[2], m[8]) : fold(fixed::valueToAngle(m[1], table_), m[3], m[4]);
        const auto roll = cp ? axis(m[3], m[4]) : fixed::Angle16{};
        const int signedPitch = pitch.raw() <= 32767 ? pitch.raw() : static_cast<int>(pitch.raw()) - 65536;
        const bool refresh = (signedPitch > 0x38e3 && signedPitch < 0x4001) ||
                             (signedPitch < -0x38e3 && signedPitch > -0x4001) ||
                             (rollWasNonzero && roll.raw() == 0);
        return {{Angle<FixedBackend>(yaw), Angle<FixedBackend>(pitch), Angle<FixedBackend>(roll)}, refresh};
    }
};

template<> class RotationMath<ModernBackend> {
public:
    Coefficient<ModernBackend> sine(Angle<ModernBackend> angle) const {
        return Coefficient<ModernBackend>(std::sin(angle.value_));
    }
    Coefficient<ModernBackend> cosine(Angle<ModernBackend> angle) const {
        return Coefficient<ModernBackend>(std::cos(angle.value_));
    }
    RotationTerms<ModernBackend> terms(EulerAngles<ModernBackend> angles) const {
        return {sine(angles.yaw), cosine(angles.yaw), sine(angles.pitch),
                cosine(angles.pitch), sine(angles.roll), cosine(angles.roll)};
    }
    Matrix3<ModernBackend> rotation(EulerAngles<ModernBackend> angles) const {
        const auto t = terms(angles);
        const double sy = t.sinYaw.value_, cy = t.cosYaw.value_;
        const double sp = t.sinPitch.value_, cp = t.cosPitch.value_;
        const double sr = t.sinRoll.value_, cr = t.cosRoll.value_;
        return Matrix3<ModernBackend>({sp*sr*sy + cy*cr, sp*cr*sy - cy*sr, sy*cp,
                                      sr*cp, cr*cp, -sp,
                                      sp*sr*cy - sy*cr, sp*cr*cy + sy*sr, cy*cp});
    }
    Matrix3<ModernBackend> objectRotation(EulerAngles<ModernBackend> angles) const {
        // Preserve the legacy Euler convention, but without intermediate truncation.
        const auto negated = rotation({-angles.yaw, -angles.pitch, -angles.roll});
        std::array<double, 9> result{};
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 3; ++col)
                result[row * 3 + col] = negated.value_[col * 3 + row];
        return Matrix3<ModernBackend>(result);
    }
    Matrix3<ModernBackend> yawDelta(Angle<ModernBackend> angle) const {
        return rotation({angle, {}, {}});
    }
    Matrix3<ModernBackend> pitchDelta(Angle<ModernBackend> angle) const {
        return rotation({{}, angle, {}});
    }
    Matrix3<ModernBackend> rollDelta(Angle<ModernBackend> angle) const {
        return rotation({{}, {}, -angle});
    }
    RecoveredAttitude<ModernBackend> recover(const Matrix3<ModernBackend> &matrix,
                                            bool /*rollWasNonzero*/) const {
        const auto &m = matrix.value_;
        const double cp = std::hypot(m[3], m[4]);
        const double pitch = std::atan2(-m[5], cp);
        // At a pole only the combined yaw/roll is observable. Choose roll zero.
        const double yaw = cp > 1e-12 ? std::atan2(m[2], m[8]) : std::atan2(-m[6], m[0]);
        const double roll = cp > 1e-12 ? std::atan2(m[3], m[4]) : 0;
        return {{Angle<ModernBackend>(yaw), Angle<ModernBackend>(pitch), Angle<ModernBackend>(roll)}, false};
    }
};

// This caller-level operation stays typed for either backend.
template<class B>
Matrix3<B> cameraRotation(const RotationMath<B> &math, EulerAngles<B> camera) {
    return math.rotation({-camera.yaw, -camera.pitch, -camera.roll});
}

} // namespace f15::math
#endif

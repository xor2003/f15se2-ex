#ifndef F15_MATH_ROTATION_HPP
#define F15_MATH_ROTATION_HPP

#include "fixed_math.hpp"
#include <array>
#include <cmath>
#include <type_traits>

namespace f15::math {

struct FixedBackend {};
struct ModernBackend {};
template<class B> class RotationMath;
template<class B> struct Boundary;

// Representation is deliberately absent from the public quantity API.
template<class B> class Angle {
    using Rep = std::conditional_t<std::is_same_v<B, FixedBackend>, fixed::Angle16, double>;
    Rep value_{};
    explicit Angle(Rep value) : value_(value) {}
    friend class RotationMath<B>;
    friend struct Boundary<B>;
public:
    Angle() = default;
    Angle operator-() const { return Angle(-value_); }
    Angle operator+(Angle other) const { return Angle(value_ + other.value_); }
    Angle operator-(Angle other) const { return Angle(value_ - other.value_); }
};

template<class B> struct EulerAngles {
    Angle<B> yaw, pitch, roll;
};

template<class B> class Coefficient {
    using Rep = std::conditional_t<std::is_same_v<B, FixedBackend>, std::int16_t, double>;
    Rep value_{};
    explicit Coefficient(Rep value) : value_(value) {}
    friend class RotationMath<B>;
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
};

// This caller-level operation stays typed for either backend.
template<class B>
Matrix3<B> cameraRotation(const RotationMath<B> &math, EulerAngles<B> camera) {
    return math.rotation({-camera.yaw, -camera.pitch, -camera.roll});
}

} // namespace f15::math
#endif

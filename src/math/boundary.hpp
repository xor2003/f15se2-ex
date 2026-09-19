#ifndef F15_MATH_BOUNDARY_HPP
#define F15_MATH_BOUNDARY_HPP

#ifndef F15_MATH_BOUNDARY_ACCESS
#error "Raw math conversion is restricted to reviewed boundary adapters"
#endif

#include "rotation.hpp"
#include <stdexcept>

namespace f15::math {

template<> struct Boundary<FixedBackend> {
    static Angle<FixedBackend> angleWord(std::uint16_t word) {
        return Angle<FixedBackend>(fixed::Angle16::raw(word));
    }
    static std::uint16_t angleWord(Angle<FixedBackend> angle) { return angle.value_.raw(); }
    static std::int16_t coefficientWord(Coefficient<FixedBackend> value) { return value.value_; }
    static Matrix3<FixedBackend> matrixWords(const std::int16_t *words) {
        fixed::Matrix3x3Q15 matrix;
        for (int i = 0; i < 9; ++i) matrix(i / 3, i % 3) = words[i];
        return Matrix3<FixedBackend>(matrix);
    }
    static void matrixWords(const Matrix3<FixedBackend> &matrix, std::int16_t *words) {
        for (int i = 0; i < 9; ++i) words[i] = matrix.value_.values()[i];
    }
};

template<> struct Boundary<ModernBackend> {
    static Angle<ModernBackend> radians(double radians) {
        if (!std::isfinite(radians)) throw std::domain_error("non-finite angle");
        return Angle<ModernBackend>(std::remainder(radians, 2.0 * pi));
    }
    static Angle<ModernBackend> angleWord(std::uint16_t word) {
        return radians(static_cast<double>(word) * (2.0 * pi / 65536.0));
    }
    static double radians(Angle<ModernBackend> angle) { return angle.value_; }
    static double coefficient(Coefficient<ModernBackend> value) { return value.value_; }
    static std::array<double, 9> matrix(const Matrix3<ModernBackend> &matrix) { return matrix.value_; }
private:
    static constexpr double pi = 3.141592653589793238462643383279502884;
};

} // namespace f15::math
#endif

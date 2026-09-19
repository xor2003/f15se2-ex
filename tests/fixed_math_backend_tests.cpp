#include "fixed_math.hpp"
#include "egcode.h"
#include "egdata.h"
#include "egmath.h"
#include "egflight.h"

#include <cstdio>
#include <cstdlib>
#include <cstdint>

extern int rangeApprox(int deltaX, int deltaY);
extern int16 sinMul(int16 angle, int16 value);

static void check(bool ok, const char *operation, int a, int b = 0) {
    if (!ok) {
        std::fprintf(stderr, "%s mismatch for %d, %d\n", operation, a, b);
        std::exit(1);
    }
}

int main() {
    namespace fixed = f15::fixed;
    // Compare the unchanged engine entry point before migrating its callers.
    // In particular, this is not floor(sqrt(abs(value))): isqrt(8) is 3.
    for (int value = -32768; value <= 32767; ++value) {
        check(fixed::integerSqrtCompatible(value) == ::isqrt(static_cast<int16>(value)),
              "legacy Newton square root", value);
    }
    check(::isqrt(-32768) == 1 && ::isqrt(0) == 1 && ::isqrt(8) == 3,
          "square-root exceptional/rounded results", 8);
    // Exhaust the angle domain against the production table and real core.
    for (int raw = 0; raw < 65536; ++raw) {
        const auto angle = fixed::Angle16(raw);
        check(fixed::sine(angle, g_angleLut).raw() == ::sine(static_cast<int16>(raw)), "sine", raw);
        check(fixed::cosine(angle, g_angleLut).raw() == ::cosine(static_cast<int16>(raw)), "cosine", raw);
    }
    const int samples[] = {-32768, -32767, -20000, -16384, -1, 0, 1, 127, 16384, 20000, 32767};
    for (int a : samples) {
        for (int b : samples) {
            check(fixed::Q15::multiplyToInt(a, b) == ::fixedMulQ14(a, b), "multiply", a, b);
            check(fixed::computeBearing(a, b).raw() == static_cast<uint16>(::computeBearing(a, b)), "bearing", a, b);
            // Engine entry points store these results in signed words.
            check(static_cast<int16>(fixed::rangeApprox(a, b)) == ::rangeApprox(a, b), "range", a, b);
            check(static_cast<int16>(fixed::sinMul(fixed::Angle16(a), b, g_angleLut)) == ::sinMul(a, b), "sinMul", a, b);
            check(static_cast<int16>(fixed::cosMul(fixed::Angle16(a), b, g_angleLut)) == ::cosMul(a, b), "cosMul", a, b);
        }
    }
    const std::int32_t words[] = {-2147483647 - 1, -1073741824, -1, 0, 1, 2147483647};
    for (auto value : words) {
        for (int count = 0; count < 32; ++count) {
            long left = value, right = value;
            ::shiftLongLeftInPlace(count, &left);
            ::shiftLongRightInPlace(count, &right);
            check(fixed::shiftLongLeft(value, count) == left, "left shift", value, count);
            check(fixed::shiftLongRight(value, count) == right, "right shift", value, count);
        }
    }
    const int angles[] = {0, 1, 0x1234, 0x3fff, 0x4000, 0x7fff, 0x8000, 0xc000, 0xffff};
    for (int yaw : angles) {
        for (int pitch : angles) {
            for (int roll : angles) {
                int16 expected[9], expectedProduct[9];
                ::buildRotationMatrixFar(expected, yaw, pitch, roll);
                const auto matrix = fixed::Matrix3x3Q15::rotation(
                    fixed::Angle16(yaw), fixed::Angle16(pitch), fixed::Angle16(roll), g_angleLut);
                ::multiplyMatrix3x3Far(expected, expected, expectedProduct);
                const auto product = matrix * matrix;
                for (int i = 0; i < 9; ++i) {
                    check(matrix.values()[i] == expected[i], "rotation matrix", yaw, i);
                    check(product.values()[i] == expectedProduct[i], "matrix product", yaw, i);
                }
            }
        }
    }
    std::puts("fixed math backend comparisons passed");
}

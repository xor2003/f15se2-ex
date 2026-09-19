#ifndef F15_TEST_MATH_ROTATION_REFERENCE_HPP
#define F15_TEST_MATH_ROTATION_REFERENCE_HPP

// Frozen from eg3drast.c at 9018a8b. Do not replace with production math calls.
// Widened arithmetic spells out the old signed shifts and low-word stores so
// this oracle itself can run under UBSan on any host word size.
#include <array>
#include <cstdint>

namespace rotation_reference {
using Matrix = std::array<std::int16_t, 9>;
inline std::int64_t floorDivide(std::int64_t value, std::int64_t divisor) {
    return value / divisor - (value % divisor < 0 ? 1 : 0);
}
inline std::int16_t word(std::int64_t value) {
    const auto bits = static_cast<std::uint16_t>(value);
    return static_cast<std::int16_t>(bits <= 32767 ? bits : static_cast<int>(bits) - 65536);
}
inline std::int16_t sine(int angle, const std::int16_t *table) {
    const auto bits = static_cast<std::uint16_t>(angle);
    const int index = bits / 256;
    return word(table[index] + floorDivide(
        (table[index + 1] - table[index]) * (bits % 256) + 128, 256));
}
inline std::int16_t product(int a, int b) {
    return word(floorDivide(static_cast<std::int64_t>(a) * b, 32768));
}
inline std::int16_t sum(int a, int b, int c, int d, bool subtract) {
    const auto first = static_cast<std::int64_t>(a) * b;
    const auto second = static_cast<std::int64_t>(c) * d;
    return word(floorDivide(first + (subtract ? -second : second), 32768));
}
inline Matrix rotation(int yaw, int pitch, int roll, const std::int16_t *table, bool object = false) {
    const int sy = sine(yaw, table), cy = sine(yaw + 16384, table);
    const int p = sine(roll, table), ro = sine(roll + 16384, table);
    const int r = sine(pitch, table), d = sine(pitch + 16384, table);
    const int si = product(r, p), bp = product(r, ro);
    if (object)
        return {sum(cy, ro, si, sy, true), word(-product(p, d)), sum(si, cy, sy, ro, false),
                sum(bp, sy, cy, p, false), product(ro, d), sum(sy, p, bp, cy, true),
                word(-product(sy, d)), word(r), product(cy, d)};
    return {sum(si, sy, cy, ro, false), sum(bp, sy, cy, p, true), product(sy, d),
            product(p, d), product(ro, d), word(-r), sum(si, cy, sy, ro, true),
            sum(bp, cy, sy, p, false), product(cy, d)};
}
inline Matrix multiply(const Matrix &a, const Matrix &b) {
    Matrix result{};
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col) {
            std::int64_t sum = 0;
            for (int k = 0; k < 3; ++k)
                sum += static_cast<std::int64_t>(a[row * 3 + k]) * b[k * 3 + col];
            result[row * 3 + col] = word(floorDivide(sum, 32768));
        }
    return result;
}
} // namespace rotation_reference
#endif

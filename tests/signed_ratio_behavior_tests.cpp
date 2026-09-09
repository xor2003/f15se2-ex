#include "inttype.h"

#include <cstdint>
#include <iostream>

extern uint16 signedRatio16(int16 numerator, int16 denominator);

int main() {
    // Sweep every numerator with positive and negative divisors, including
    // boundary values. Non-power-of-two inputs expose multiplication by 255
    // instead of -1; simple half/quarter ratios can accidentally still pass.
    const int divisors[] = {-32768, -32767, -12345, -1, 1, 12345, 32767};
    for (int denominator : divisors) {
        for (int numerator = -32768; numerator <= 32767; ++numerator) {
            const uint16 expected = static_cast<uint16>(
                (static_cast<std::int64_t>(numerator) * 32768) / denominator);
            const uint16 actual = signedRatio16(
                static_cast<int16>(numerator), static_cast<int16>(denominator));
            if (actual != expected) {
                std::cerr << "signedRatio16(" << numerator << ", "
                          << denominator << "): expected " << expected
                          << ", got " << actual << '\n';
                return 1;
            }
        }
    }
    return 0;
}

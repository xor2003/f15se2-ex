#ifndef F15_TEST_ATTITUDE_REFERENCE_HPP
#define F15_TEST_ATTITUDE_REFERENCE_HPP

#include "math_rotation_reference.hpp"
#include <cstdlib>

// Frozen egflight.c recovery at 4d0efd4; intentionally independent of typed math.
// The byte-wise quadrant writes are expressed as word wrapping for host portability.
namespace rotation_reference {
struct Attitude {
    std::int16_t yaw = 0, pitch = 0, roll = 0;
    bool dirty = false;
};
inline Attitude recover(const Matrix &g_orientMatrix, bool g_rollWasNonzero,
                        const std::int16_t *table) {
    using int16 = std::int16_t;
    Attitude result;
    auto &g_ourHead = result.yaw;
    auto &g_ourPitch = result.pitch;
    auto &g_ourRoll = result.roll;
    auto &g_orientationDirty = result.dirty;
    auto valueToAngle = [&](int value) {
        if (value == 0x8000) return 0xc000;
        const int magnitude = std::abs(value);
        int angle = 0;
        for (int index = (magnitude >> 9) + 1; index >= 0; --index) {
            if (table[index] <= magnitude) {
                const int span = table[index + 1] - table[index];
                angle = static_cast<int>(static_cast<std::int64_t>(magnitude - table[index]) * 256 / span)
                        + index * 256;
                break;
            }
        }
        return value < 0 ? -angle : angle;
    };
    auto complementAngle = [&](int value) { return 16384 - valueToAngle(value); };
    auto cosine = [&](int angle) { return sine(angle + 16384, table); };
    auto signedRatio16 = [](int16 n, int16 d) {
        const std::uint64_t magnitude = (static_cast<std::uint64_t>(std::abs(int(n))) * 65536 /
                                         std::abs(int(d))) / 2;
        const auto signedMagnitude = static_cast<std::int64_t>(magnitude);
        return word((n < 0) != (d < 0) ? -signedMagnitude : signedMagnitude);
    };


    int16 cosPitch;

    g_ourPitch = valueToAngle(-g_orientMatrix[5]);
    cosPitch = cosine(g_ourPitch);
    if (cosPitch != 0) {
        /* signedRatio16 returns the DOS 16-bit word pattern; the sign lives in
         * bit 15, so recover it as int16 before abs (a plain (int) leaves a
         * negative ratio as a huge positive and garbles the decoded angle). */
        if (abs(g_orientMatrix[2]) < 0x5a81) {
            g_ourHead = valueToAngle(abs((int16)signedRatio16(g_orientMatrix[2], cosPitch)));
        } else {
            g_ourHead = complementAngle(abs((int16)signedRatio16(g_orientMatrix[8], cosPitch)));
        }
        if (g_orientMatrix[2] <= 0 && g_orientMatrix[8] < 0) {
            g_ourHead = word(g_ourHead + 0x8000);
        }
        if (g_orientMatrix[2] > 0 && g_orientMatrix[8] < 0) {
            g_ourHead = 0x8000 - g_ourHead;
        }
        if (g_orientMatrix[2] < 0 && g_orientMatrix[8] > 0) {
            g_ourHead = -g_ourHead;
        }
        if (abs(g_orientMatrix[3]) < 0x5a81) {
            g_ourRoll = valueToAngle(abs((int16)signedRatio16(g_orientMatrix[3], cosPitch)));
        } else {
            g_ourRoll = complementAngle(abs((int16)signedRatio16(g_orientMatrix[4], cosPitch)));
        }
        if (g_orientMatrix[3] <= 0 && g_orientMatrix[4] < 0) {
            g_ourRoll = word(g_ourRoll + 0x8000);
        }
        if (g_orientMatrix[3] > 0 && g_orientMatrix[4] < 0) {
            g_ourRoll = 0x8000 - g_ourRoll;
        }
        if (g_orientMatrix[3] < 0 && g_orientMatrix[4] > 0) {
            /* Force MSC to emit sub ax, ax; sub ax, g_ourRoll. */
            g_ourRoll = 0x10000 - g_ourRoll;
        }
    } else {
        g_ourRoll = 0;
        g_ourHead = valueToAngle(g_orientMatrix[1]);
        if (g_orientMatrix[3] <= 0 && g_orientMatrix[4] < 0) {
            g_ourHead = word(g_ourHead + 0x8000);
        }
        if (g_orientMatrix[3] > 0 && g_orientMatrix[4] < 0) {
            g_ourHead = 0x8000 - g_ourHead;
        }
        if (g_orientMatrix[3] < 0 && g_orientMatrix[4] > 0) {
            g_ourHead = -g_ourHead;
        }
    }
    if (g_ourPitch > 0x38e3 && g_ourPitch < 0x4001) {
        g_orientationDirty = 1;
    }
    if (g_ourPitch < (int16)0xc71d && g_ourPitch > (int16)0xbfff) {
        g_orientationDirty = 1;
    }
    if (g_rollWasNonzero != 0 && g_ourRoll == 0) {
        g_orientationDirty = 1;
    }
    return result;
}
} // namespace rotation_reference
#endif

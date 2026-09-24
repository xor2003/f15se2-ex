/* Characterization of the START-side DOS math kernels in stmath.c against the
 * centralized f15::fixed implementations. stmath's approxDistance, calcBearing
 * and clampValue are literal copies of the egame formulas; this test pins that
 * equivalence over the full input domain so the duplicates can safely delegate
 * to the shared kernels. Run BEFORE changing stmath.c — it must pass on the
 * unmodified code. */
#include "stmath.h"
#include "egmath.h"
#include "fixed_math.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>

/* egdata.c's regnStr points at this buffer. ELF dead-section linking discards
 * the reference in normal builds; sanitizer instrumentation keeps it alive,
 * so the stub is required when ASan/UBSan are enabled. */
char aRegn_xxx[] = "regn.xxx";

namespace {

enum : int { kTestFailureExitCode = 1 };

[[noreturn]] void failAt(const char *what, int a, int b, int c, long got, long want) {
    std::cerr << "failed: " << what << " a=" << a << " b=" << b << " c=" << c
              << " got=" << got << " want=" << want << '\n';
    std::exit(kTestFailureExitCode);
}

/* Representative delta set: every power-of-two boundary, the wrap edges and
 * the sign boundaries, plus dense interior coverage via the outer loop. */
const int kEdgeDeltas[] = {
    -32768, -32767, -24576, -20000, -16384, -16383, -10000, -8192, -8191,
    -5000, -4096, -4095, -2048, -1024, -1000, -513, -512, -511, -257, -256,
    -255, -129, -128, -127, -65, -64, -63, -33, -32, -31, -17, -16, -15,
    -9, -8, -7, -5, -4, -3, -2, -1,
    0,
    1, 2, 3, 4, 5, 7, 8, 9, 15, 16, 17, 31, 32, 33, 63, 64, 65, 127, 128,
    129, 255, 256, 257, 511, 512, 513, 1000, 1024, 2048, 4095, 4096, 5000,
    8191, 8192, 10000, 16383, 16384, 20000, 24576, 32766, 32767,
};

void approxDistanceEquivalence() {
    /* Dense interior square: catches octant-fold and sum boundaries. */
    for (int dx = -1100; dx <= 1100; dx += 1) {
        for (int dy = -1100; dy <= 1100; dy += 1) {
            const int got = approxDistance((int16)dx, (int16)dy);
            /* approxDistance returns int16: the word-width store wraps a
             * negative double--32768 sum where rangeApprox's int return would
             * not. The dedup contract is equivalence through the int16 cast. */
            const int want = (int16)f15::fixed::rangeApprox(dx, dy);
            if (got != want) failAt("approxDistance", dx, dy, 0, got, want);
        }
    }
    /* Full dx domain x edge dy: wrap/cap coverage incl. abs(-32768). */
    for (int dx = -32768; dx <= 32767; ++dx) {
        for (const int dy : kEdgeDeltas) {
            const int got = approxDistance((int16)dx, (int16)dy);
            const int want = (int16)f15::fixed::rangeApprox(dx, dy);
            if (got != want) failAt("approxDistance", dx, dy, 0, got, want);
        }
    }
}

void calcBearingEquivalence() {
    for (int dx = -1100; dx <= 1100; dx += 1) {
        for (int dy = -1100; dy <= 1100; dy += 1) {
            const int got = calcBearing((int16)dx, (int16)dy);
            const int want = f15::fixed::computeBearing(dx, dy).signedRaw();
            if (got != want) failAt("calcBearing", dx, dy, 0, got, want);
        }
    }
    for (int dx = -32768; dx <= 32767; ++dx) {
        for (const int dy : kEdgeDeltas) {
            const int got = calcBearing((int16)dx, (int16)dy);
            const int want = f15::fixed::computeBearing(dx, dy).signedRaw();
            if (got != want) failAt("calcBearing", dx, dy, 0, got, want);
        }
    }
}

void clampValueEquivalence() {
    const int bounds[][2] = {
        {-100, 100}, {0, 0x7fff}, {-0x4000, 0x4000}, {5, 5}, {-10, -5},
        {-32768, 32767}, {0, 1}, {-1, 0}, {100, -100}, {0x7fff, 0x7fff},
    };
    for (const auto &b : bounds) {
        for (int v = -32768; v <= 32767; ++v) {
            const int got = clampValue((int16)v, (int16)b[0], (int16)b[1]);
            const int want = clampRange((int16)v, (int16)b[0], (int16)b[1]);
            if (got != want) failAt("clampValue", v, b[0], b[1], got, want);
        }
    }
}

void scaleCoordByLevelPins() {
    /* LOD scaler: the terrain-neighbour search's coordinate zoom. */
    const struct { int level; uint32 coord; uint32 want; } cases[] = {
        {4, 0x12345, 0x12345 >> 6}, {3, 0x12345, 0x12345 >> 4},
        {2, 0x12345, 0x12345 >> 2}, {1, 0x12345, 0x12345},
        {0, 0x12345, 0x12345 << 1},
        {4, 0xffffffffu, 0xffffffffu >> 6}, {0, 0x80000000u, 0},
        {2, 0, 0},
    };
    for (const auto &c : cases) {
        const uint32 got = scaleCoordByLevel((int16)c.level, c.coord);
        if (got != c.want) failAt("scaleCoordByLevel", c.level, (int)c.coord, 0,
                                  (long)got, (long)c.want);
    }
}

} // namespace

int main() {
    approxDistanceEquivalence();
    calcBearingEquivalence();
    clampValueEquivalence();
    scaleCoordByLevelPins();
    std::cout << "stmath kernel characterization passed\n";
    return 0;
}

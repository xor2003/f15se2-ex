#include "stmath.h"
#include "const.h"
#include "egmath.h"
#include "fixed_math.hpp"

#include <stdlib.h>

// debugcom: custom_manhattan_distance
/* Delegates to f15::fixed::rangeApprox — the same max+min/2 formula with the
 * 0x7fff cap and the abs(-32768) quirk. The int16 result cast keeps the DOS
 * word-store wrap on the negative double--32768 edge (stmath_kernel_tests
 * pins the equivalence over the full domain). */
int16 approxDistance(int16 dx, int16 dy) {
    return (int16)f15::fixed::rangeApprox(dx, dy);
}

/* Delegates to f15::fixed::computeBearing — the same octant fold, 2^14 ratio
 * and 0x1333/0xb00 polynomial approximation as the egame bearing helper. */
int16 calcBearing(int16 dx, int16 dy) {
    return f15::fixed::computeBearing(dx, dy).signedRaw();
}

/* Delegates to clampRange — same clamp including the <= -0x4000 wrap-to-max
 * quirk. */
int16 clampValue(int16 val, int16 lo, int16 hi) {
    return clampRange(val, lo, hi);
}

uint32 scaleCoordByLevel(int16 level, uint32 coord) {
    switch (level) {
    case 4:
        return coord >> 6;
    case 3:
        return coord >> 4;
    case 2:
        return coord >> 2;
    case 1:
        return coord;
    default: // case 0
        return coord << 1;
    }
}

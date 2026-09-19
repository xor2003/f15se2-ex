#ifndef F15_SE2_FIXED_MATH_HPP
#define F15_SE2_FIXED_MATH_HPP

#include <array>
#include <cstdint>
#include <utility>

namespace f15::fixed {

/* These constants name the scale factors and bit patterns used by the original
 * 8086 routines. Keep them close to the math so replacement call sites do not
 * have to remember what an unlabelled 0x4000 or shift-by-14 means. */
enum LegacyAngle : std::uint16_t {
    /* 16-bit "word degrees": one full turn is implicit wrap at 0x10000. */
    kAngleNorth = 0x0000u,
    kAngleQuarterTurn = 0x4000u,
    kAngleHalfTurn = 0x8000u,
    kAngleThreeQuarterTurn = 0xC000u,
};

enum LegacyTrig : int {
    /* Sine table addressing: high byte selects one of 256 samples. */
    kAngleTableEntries = 256,
    /* The original lookup reads idx + 1, so the C table has guard slots. */
    kAngleTableExtraSlots = 4,
    kAngleTableSize = kAngleTableEntries + kAngleTableExtraSlots,
    kAngleIndexShift = 8,
    /* Low byte is the interpolation fraction between adjacent samples. */
    kAngleFractionMask = 0xFF,
    /* Add-before-shift rounding used by the sine interpolation asm. */
    kSineInterpolationRound = 0x80,
    /* valueToAngle starts near magnitude / 512, then walks backward. */
    kAsinTableShift = 9,
    /* Each sine table index corresponds to 0x100 angle units. */
    kWordDegreeStep = 256,
    /* Special-case input used by the original Iasin routine. */
    kSignedWordMinPattern = 0x8000,
};

enum LegacyFixed : int {
    /* fixedMulQ14 is effectively rounded Q15: result ~= (a*b)/32768. */
    kQ15ProductShift = 15,
    kQ15RoundBitShift = 14,
    kQ15RoundBit = 1,
    /* Matrix/raster code doubles 16x16 products before taking the high word. */
    kDoubleProductShift = 1,
    kWordBits = 16,
    kLongBits = 32,
    /* Carry bit folded into some high-word rounding paths. */
    kLowWordCarryBit = 0x8000,
    kDivideOverflowSaturation = 0x7F00,
    kFullDivideByZeroResult = -1,
    kRandomScaleShift = 15,
    kSqrtSmallThreshold = 4,
    kSqrtInitialShift = 2,
    kSqrtConvergenceDelta = 1,
};

enum LegacyApproximation : int {
    /* rangeApprox saturates to signed-word positive max. */
    kRangeApproxMax = 0x7FFF,
    /* Distance approximation is max(abs) + min(abs)/2. */
    kRangeApproxHalfShift = 1,
    /* computeBearing scales the smaller/larger-axis ratio by 2^14. */
    kBearingRatioShift = 14,
    /* Empirical atan-like polynomial constants from egmath.c/egcode.asm. */
    kBearingScaleBase = 0x2800,
    kBearingRatioBias = 0x1333,
    kBearingCurvature = 0x0B00,
    /* hudPitchScale multiplies by this before keeping the high byte. */
    kHudPitchScale = 360,
    kClampWrapFloor = -0x4000,
    kProjectionByteShift = 8,
    kProjectionInvalidWord = 0x8000,
    kProjectionAspectShift = 2,
};

enum LegacyProjection : std::uint32_t {
    kDwordSignBit = 0x80000000u,
    kProjectionInvalidDword = 0x80008000u,
};

enum ClipOutcode : int {
    kClipInside = 0,
    kClipRight = 0x01,
    kClipBottom = 0x02,
    kClipTop = 0x04,
    kClipLeft = 0x08,
};

enum MatrixShape : int {
    kMatrixRows = 3,
    kMatrixCols = 3,
    kMatrixElements = kMatrixRows * kMatrixCols,
};

class Angle16 {
public:
    static constexpr std::uint16_t quarter_turn = kAngleQuarterTurn;
    static constexpr std::uint16_t half_turn = kAngleHalfTurn;
    static constexpr std::uint16_t three_quarter_turn = kAngleThreeQuarterTurn;

    constexpr Angle16() = default;
    /* Construction intentionally truncates to 16 bits; this is how the DOS
     * code stores heading/pitch/roll and how angle wrap is represented. */
    constexpr explicit Angle16(int value) : raw_(static_cast<std::uint16_t>(value)) {}

    /* Use raw() when the caller already has a wrapped DOS angle word. */
    static constexpr Angle16 raw(std::uint16_t value) {
        Angle16 angle;
        angle.raw_ = value;
        return angle;
    }

    constexpr std::uint16_t raw() const { return raw_; }
    /* signedRaw() is useful for old code that compares angles as int16. */
    constexpr std::int16_t signedRaw() const { return static_cast<std::int16_t>(raw_); }
    constexpr int tableIndex() const { return (raw_ >> kAngleIndexShift) & kAngleFractionMask; }
    constexpr int fraction() const { return raw_ & kAngleFractionMask; }

    /* Arithmetic is modulo 2^16, matching assignments to int16/uint16 angle
     * globals in the original code. */
    constexpr Angle16 operator+(Angle16 other) const {
        return raw(static_cast<std::uint16_t>(raw_ + other.raw_));
    }

    constexpr Angle16 operator-(Angle16 other) const {
        return raw(static_cast<std::uint16_t>(raw_ - other.raw_));
    }

    constexpr Angle16 operator-() const {
        return raw(static_cast<std::uint16_t>(-raw_));
    }

private:
    std::uint16_t raw_ = 0;
};

class Q15 {
public:
    constexpr Q15() = default;
    constexpr explicit Q15(int value) : raw_(static_cast<std::int16_t>(value)) {}

    static constexpr Q15 raw(std::int16_t value) {
        Q15 q;
        q.raw_ = value;
        return q;
    }

    constexpr std::int16_t raw() const { return raw_; }
    constexpr int asInt() const { return raw_; }

    /* Exact fixedMulQ14 contract:
     *   p = signed 16x16 product
     *   result = arithmetic(p >> 15) + bit14(p)
     * The returned int may be +32768 for -32768 * -32768; do not narrow it
     * unless the legacy destination was a word store. */
    static constexpr int multiplyToInt(int a, int b) {
        const std::int32_t p = static_cast<std::int32_t>(a) * static_cast<std::int32_t>(b);
        return static_cast<int>(arithmeticShiftRight(p, kQ15ProductShift) +
                                (arithmeticShiftRight(p, kQ15RoundBitShift) & kQ15RoundBit));
    }

    constexpr Q15 operator*(Q15 other) const {
        return Q15(multiplyToInt(raw_, other.raw_));
    }

private:
    /* C++ does not require signed right shift to be arithmetic. Spell it out so
     * the result stays compatible across host compilers. */
    static constexpr std::int32_t arithmeticShiftRight(std::int32_t value, int count) {
        return static_cast<std::int32_t>(value >= 0
            ? static_cast<std::int64_t>(value) / (std::int64_t{1} << count)
            : -((-static_cast<std::int64_t>(value) + ((std::int64_t{1} << count) - 1)) /
                (std::int64_t{1} << count)));
    }

    std::int16_t raw_ = 0;
};

struct WordPair32 {
    /* DOS code often treats a 32-bit value as adjacent low/high 16-bit words.
     * The high half is signed so JOIN32 preserves negative long values. */
    std::uint16_t lo = 0;
    std::int16_t hi = 0;

    static constexpr WordPair32 split(std::int32_t value) {
        return WordPair32{static_cast<std::uint16_t>(value),
                          static_cast<std::int16_t>(value >> kWordBits)};
    }

    constexpr std::int32_t join() const {
        return static_cast<std::int32_t>(static_cast<std::int64_t>(hi) * 65536 + lo);
    }
};

constexpr int highWord(std::int32_t value) {
    return static_cast<std::int16_t>(value >> kWordBits);
}

/* Return the high word after 32-bit modulo arithmetic. This is the version used
 * when the assembly would overflow/wrap a doubled product before reading DX. */
constexpr int highWordWrapped(std::uint32_t value) {
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(value >> kWordBits));
}

/* Public arithmetic shift helper for formulas outside Q15 that still mirror
 * old signed-long shifts. */
constexpr std::int32_t arithmeticShiftRight(std::int32_t value, int count) {
    // Counts are in [0, 31]; widen before negation, including INT32_MIN.
    return static_cast<std::int32_t>(value >= 0
        ? static_cast<std::int64_t>(value) / (std::int64_t{1} << count)
        : -((-static_cast<std::int64_t>(value) + ((std::int64_t{1} << count) - 1)) /
            (std::int64_t{1} << count)));
}

constexpr std::int32_t shiftLongLeft(std::int32_t value, int count) {
    /* MSC long-left helpers behave as a 32-bit register shift. Use unsigned
     * bits to avoid host signed-overflow UB, then reinterpret the result. */
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(value) << count);
}

constexpr std::int32_t shiftLongRight(std::int32_t value, int count) {
    /* Matching right shift is arithmetic for signed longs. */
    return arithmeticShiftRight(value, count);
}

constexpr int lowCarry(std::int32_t value) {
    return (static_cast<std::uint16_t>(value) & kLowWordCarryBit) ? 1 : 0;
}

/* The original inputs here are word-sized magnitudes. This helper deliberately
 * keeps the simple branch form instead of std::abs overload selection. */
constexpr int abs16Compatible(int value) {
    /* MSC 16-bit abs(-32768) overflows and leaves the value negative. Several
     * original math helpers inherit that edge case, so keep it explicit. */
    const int wordValue = static_cast<std::int16_t>(value);
    return wordValue == static_cast<int>(static_cast<std::int16_t>(0x8000))
        ? wordValue
        : (wordValue < 0 ? -wordValue : wordValue);
}

constexpr int signOf(int value) {
    return value == 0 ? 0 : (value > 0 ? 1 : -1);
}

constexpr int clampValue(int value, int minVal, int maxVal) {
    return value > maxVal ? maxVal : (value < minVal ? minVal : value);
}

constexpr int clampRange(int value, int minVal, int maxVal) {
    /* egmath.c rng(): below-min values at or below -0x4000 wrap to max. */
    if (value > maxVal) {
        return maxVal;
    }
    if (value >= minVal) {
        return value;
    }
    return value <= kClampWrapFloor ? maxVal : minVal;
}

constexpr int randomRangeFromRand(int randValue, int maxVal) {
    /* randomRange(Max) is just the C rand() output scaled by Max. */
    return static_cast<int>((static_cast<std::int32_t>(randValue) * maxVal) >> kRandomScaleShift);
}

constexpr int integerSqrtCompatible(int value) {
    /* egflight.c isqrt(): take abs(input), return 1 for 0..3, then iterate
     * Newton division until the quotient and guess differ by at most 1. */
    value = abs16Compatible(value);
    if (value < kSqrtSmallThreshold) {
        return 1;
    }
    int guess = value >> kSqrtInitialShift;
    int quotient = 0;
    do {
        quotient = value / guess;
        guess = (guess + quotient) >> 1;
    } while (abs16Compatible(guess - quotient) > kSqrtConvergenceDelta);
    return guess;
}

/* 16x16 signed multiply, then 32-bit wrap, then doubled. This mirrors:
 *   IMUL word
 *   SHL AX,1 / RCL DX,1
 * and is not the same contract as fixedMulQ14's rounded integer result. */
constexpr std::uint32_t doubleProductBits(int a, int b) {
    const std::uint32_t p = static_cast<std::uint32_t>(
        static_cast<std::int32_t>(a) * static_cast<std::int32_t>(b));
    return p << kDoubleProductShift;
}

constexpr int q15HighWord(int a, int b) {
    return highWordWrapped(doubleProductBits(a, b));
}

constexpr int doubledHighWordWithCarry(std::int32_t value) {
    /* dirRound/LOCARRY idiom: double an accumulated signed long, read its high
     * word, then add carry from bit 15 of the doubled low word. */
    const std::uint32_t doubled = static_cast<std::uint32_t>(value) << kDoubleProductShift;
    return static_cast<std::int16_t>(highWordWrapped(doubled) + ((doubled & kLowWordCarryBit) ? 1 : 0));
}

/* Some raster paths add the low-word carry after reading the doubled high word.
 * Keep this separate from q15HighWord() because both idioms exist. */
constexpr int q15HighWordRounded(int a, int b) {
    const std::int32_t p = static_cast<std::int32_t>(a) * static_cast<std::int32_t>(b);
    return doubledHighWordWithCarry(p);
}

constexpr std::uint32_t unsignedDiv32By16Full(std::uint32_t num, std::uint16_t den) {
    std::uint32_t rem = 0;
    std::uint32_t q = 0;
    if (den == 0) {
        return static_cast<std::uint32_t>(kFullDivideByZeroResult);
    }
    for (int i = 0; i < kLongBits; ++i) {
        rem <<= 1;
        if (num & kDwordSignBit) {
            rem |= 1;
        }
        num <<= 1;
        q <<= 1;
        if (rem >= den) {
            rem -= den;
            q |= 1;
        }
    }
    return q;
}

constexpr std::uint16_t unsignedDiv32By16Saturated(std::uint32_t num, std::uint16_t den) {
    const std::uint32_t q = unsignedDiv32By16Full(num, den);
    if (den == 0 || q > kRangeApproxMax) {
        return kDivideOverflowSaturation;
    }
    return static_cast<std::uint16_t>(q);
}

constexpr std::int32_t signedDivFull(std::int32_t num, int den) {
    const bool negative = (num < 0) != (den < 0);
    const std::uint32_t absNum = num < 0 ? 0u - static_cast<std::uint32_t>(num) : static_cast<std::uint32_t>(num);
    const std::uint16_t absDen = static_cast<std::uint16_t>(den < 0 ? -den : den);
    const std::uint32_t q = unsignedDiv32By16Full(absNum, absDen);
    return static_cast<std::int32_t>(negative ? 0u - q : q);
}

constexpr int signedDiv32By16Saturated(std::int32_t num, int den) {
    const bool negative = (num < 0) != (den < 0);
    const std::uint32_t absNum = num < 0 ? 0u - static_cast<std::uint32_t>(num) : static_cast<std::uint32_t>(num);
    const std::uint16_t absDen = static_cast<std::uint16_t>(den < 0 ? -den : den);
    const std::uint16_t q = unsignedDiv32By16Saturated(absNum, absDen);
    return negative ? -static_cast<int>(q) : static_cast<int>(q);
}

constexpr int clipMulDiv(int a, int b, int denominator) {
    return signedDiv32By16Saturated(static_cast<std::int32_t>(a) * b, denominator);
}

template <typename Table>
constexpr Q15 sine(Angle16 angle, const Table &table) {
    /* Lookup contract:
     *   index = high byte
     *   fraction = low byte
     *   value = table[index] + round((table[index+1]-table[index])*fraction/256)
     * The table must include the guard entry at index + 1. */
    const int idx = angle.tableIndex();
    const int frac = angle.fraction();
    const int v0 = table[idx];
    const int v1 = table[idx + 1];
    const std::int32_t step = static_cast<std::int32_t>(v1 - v0) * frac;
    return Q15(v0 + static_cast<int>(arithmeticShiftRight(step + kSineInterpolationRound, kAngleIndexShift)));
}

template <typename Table>
constexpr Q15 cosine(Angle16 angle, const Table &table) {
    /* Cosine is not its own table in the original; it is sine shifted by 90 deg. */
    return sine(angle + Angle16::raw(Angle16::quarter_turn), table);
}

template <typename Table>
constexpr int sinMul(Angle16 angle, int value, const Table &table) {
    /* This is egmath.c sinMul/sinX: table sine value multiplied by any scalar. */
    return Q15::multiplyToInt(sine(angle, table).asInt(), value);
}

template <typename Table>
constexpr int cosMul(Angle16 angle, int value, const Table &table) {
    /* Preserve the legacy layering: cosMul calls sinMul(angle + quarter turn). */
    return sinMul(angle + Angle16::raw(Angle16::quarter_turn), value, table);
}

constexpr int rangeApprox(int dx, int dy) {
    /* Fast distance approximation used by targeting and AI. It is intentionally
     * not Euclidean distance: max(abs(dx),abs(dy)) + min(abs(dx),abs(dy))/2. */
    dx = abs16Compatible(dx);
    dy = abs16Compatible(dy);
    const std::int32_t dist = dx > dy
        ? static_cast<std::int32_t>(dy >> kRangeApproxHalfShift) + dx
        : static_cast<std::int32_t>(dx >> kRangeApproxHalfShift) + dy;
    return dist > kRangeApproxMax ? kRangeApproxMax : static_cast<int>(dist);
}

constexpr Angle16 computeBearing(int deltaX, int deltaY) {
    deltaX = static_cast<std::int16_t>(deltaX);
    deltaY = static_cast<std::int16_t>(deltaY);
    /* Axis-aligned cases are exact and avoid division by zero. The signs match
     * the original map convention: +Y is north for this helper. */
    if (deltaX == 0) {
        return Angle16(deltaY > 0 ? kAngleNorth : kAngleHalfTurn);
    }
    if (deltaY == 0) {
        return Angle16(deltaX > 0 ? kAngleQuarterTurn : kAngleThreeQuarterTurn);
    }

    const int absX = abs16Compatible(deltaX);
    const int absY = abs16Compatible(deltaY);
    /* Work in the smaller/larger octant, then mirror the result into the final
     * quadrant. swapped means X is the dominant axis. */
    const bool swapped = absX > absY;
    const std::int32_t numer = static_cast<std::int32_t>(swapped ? absY : absX) * (1 << kBearingRatioShift);
    const int denom = swapped ? absX : absY;
    const int ratio = static_cast<std::int16_t>(numer / denom);
    /* atan approximation from egmath.c:
     * angle = (base - abs(bias-ratio)*curvature/2^14) * ratio / 2^14 */
    const int difference = kBearingRatioBias - ratio;
    const int magnitude = difference < 0 ? -difference : difference;
    const int angle = static_cast<std::int16_t>(arithmeticShiftRight(
        (kBearingScaleBase - ((magnitude * kBearingCurvature) >> kBearingRatioShift)) * ratio,
        kBearingRatioShift));

    int result = 0;
    if (deltaX > 0) {
        result = deltaY > 0 ? (swapped ? kAngleQuarterTurn - angle : angle)
                            : (swapped ? angle + kAngleQuarterTurn : kAngleHalfTurn - angle);
    } else {
        result = deltaY > 0 ? (swapped ? angle + kAngleThreeQuarterTurn : -angle)
                            : (swapped ? kAngleThreeQuarterTurn - angle : angle + kAngleHalfTurn);
    }
    return Angle16(result);
}

template <typename Table>
constexpr Angle16 valueToAngle(int value, const Table &table) {
    /* Original Iasin special-cases the bit pattern 0x8000 and returns -90 deg
     * (0xC000 in unsigned angle space). */
    if (value == kSignedWordMinPattern) {
        return Angle16(kAngleThreeQuarterTurn);
    }

    const int magnitude = value < 0 ? -value : value;
    int angle = 0;
    /* Start near magnitude/512 and walk backward until the lower table bracket
     * is found, then interpolate within the 0x100-wide angle step. */
    for (int tableIndex = (magnitude >> kAsinTableShift) + 1; tableIndex >= 0; --tableIndex) {
        if (table[tableIndex] <= magnitude) {
            const int tableSpan = table[tableIndex + 1] - table[tableIndex];
            angle = static_cast<int>(
                (static_cast<std::int32_t>(magnitude - table[tableIndex]) * kWordDegreeStep) /
                static_cast<std::int32_t>(tableSpan)) + tableIndex * kWordDegreeStep;
            break;
        }
    }

    return Angle16(value < 0 ? -angle : angle);
}

template <typename Table>
constexpr Angle16 complementAngle(int value, const Table &table) {
    /* Original Iacos is implemented as quarter-turn minus Iasin. */
    return Angle16(Angle16::quarter_turn) - valueToAngle(value, table);
}

constexpr std::uint16_t signedRatio16Bits(int numerator, int denominator) {
    /* egflight.c signedRatio16 / IntDiv:
     * divide two signed 15-bit fractions as abs(numerator) / abs(denominator),
     * scale by 0x8000, then restore the combined sign as a word bit pattern.
     * The original has no divide-by-zero guard; callers provide nonzero denom. */
    const bool negative = (numerator < 0) != (denominator < 0);
    const std::uint32_t absNumerator = static_cast<std::uint32_t>(numerator < 0 ? -numerator : numerator);
    const std::uint32_t absDenominator = static_cast<std::uint32_t>(denominator < 0 ? -denominator : denominator);
    const std::uint16_t magnitude = static_cast<std::uint16_t>(((absNumerator << kWordBits) / absDenominator) >> 1);
    return negative ? static_cast<std::uint16_t>(-static_cast<int>(magnitude)) : magnitude;
}

constexpr int hudPitchScale(int anglePitch) {
    /* eghudm.c hudPitchScale: pitch ladder offset keeps the high byte of
     * unsigned(pitch) * 360. */
    return static_cast<int>((static_cast<std::uint32_t>(static_cast<std::uint16_t>(anglePitch)) *
                             kHudPitchScale) >> kAngleIndexShift);
}

struct ProjectedPoint {
    std::int32_t x = 0;
    std::int32_t y = 0;
};

constexpr ProjectedPoint projectVertexToScreen(std::int32_t camX,
                                               std::int32_t camY,
                                               int depthDivisor,
                                               int viewCenterX,
                                               int viewCenterY,
                                               bool halfScaleRender,
                                               int extraScaleShift) {
    int divisor = depthDivisor;
    if (halfScaleRender) {
        divisor <<= 1;
    }
    if (extraScaleShift) {
        divisor = static_cast<int>(arithmeticShiftRight(divisor, extraScaleShift));
    }
    if (divisor <= 0) {
        return ProjectedPoint{static_cast<std::int32_t>(kProjectionInvalidDword),
                              static_cast<std::int32_t>(kProjectionInvalidDword)};
    }
    const std::int32_t xNum = arithmeticShiftRight(camX, kProjectionByteShift);
    const std::int32_t yBase = arithmeticShiftRight(camY, kProjectionByteShift);
    const std::int32_t yNum = arithmeticShiftRight(yBase, kProjectionAspectShift) - yBase;
    return ProjectedPoint{signedDivFull(xNum, divisor) + viewCenterX,
                          signedDivFull(yNum, divisor) + viewCenterY};
}

constexpr int clipOutcode(int x, int y, int maxX, int maxY) {
    int code = kClipInside;
    if (x < 0) {
        code |= kClipLeft;
    } else if (x > maxX) {
        code |= kClipRight;
    }
    if (y < 0) {
        code |= kClipTop;
    } else if (y > maxY) {
        code |= kClipBottom;
    }
    return code;
}

struct Line2D {
    int x1 = 0;
    int y1 = 0;
    int x2 = 0;
    int y2 = 0;
    bool visible = false;
};

constexpr Line2D clipHorizonLine(int x1, int y1, int x2, int y2, int maxX, int maxY) {
    int oc1 = clipOutcode(x1, y1, maxX, maxY);
    int oc2 = clipOutcode(x2, y2, maxX, maxY);
    int guard = 0;
    while (oc1 | oc2) {
        int oc = kClipInside;
        int nx = 0;
        int ny = 0;
        if (oc1 & oc2) {
            return Line2D{x1, y1, x2, y2, false};
        }
        if (++guard > 8) {
            return Line2D{x1, y1, x2, y2, false};
        }
        oc = oc1 ? oc1 : oc2;
        if (oc & kClipLeft) {
            ny = y1 + clipMulDiv(x1 * -1, y2 - y1, x2 - x1);
            nx = 0;
        } else if (oc & kClipRight) {
            ny = y1 + clipMulDiv(maxX - x1, y2 - y1, x2 - x1);
            nx = maxX;
        } else if (oc & kClipTop) {
            nx = x1 + clipMulDiv(y1 * -1, x2 - x1, y2 - y1);
            ny = 0;
        } else {
            nx = x1 + clipMulDiv(maxY - y1, x2 - x1, y2 - y1);
            ny = maxY;
        }
        if (oc == oc1) {
            x1 = nx;
            y1 = ny;
            oc1 = clipOutcode(x1, y1, maxX, maxY);
        } else {
            x2 = nx;
            y2 = ny;
            oc2 = clipOutcode(x2, y2, maxX, maxY);
        }
    }
    return Line2D{x1, y1, x2, y2, true};
}

constexpr std::pair<std::int16_t, std::int16_t> rotateHudLadderPoint(int x, int y, Q15 sinR, Q15 cosR) {
    /* eghudm.c hudRotateLadder for one vertex pair. */
    const int nx = q15HighWord(sinR.asInt(), x) - q15HighWord(cosR.asInt(), y);
    const std::uint32_t sinProduct = static_cast<std::uint32_t>(
        static_cast<std::int32_t>(sinR.asInt()) * y);
    const std::uint32_t cosProduct = static_cast<std::uint32_t>(
        static_cast<std::int32_t>(cosR.asInt()) * x);
    const int sinHi = highWordWrapped((sinProduct << kDoubleProductShift) + sinProduct);
    const int cosHi = highWordWrapped((cosProduct << kDoubleProductShift) + cosProduct);
    const int ny = arithmeticShiftRight(static_cast<std::int16_t>(sinHi), 1) +
                   arithmeticShiftRight(static_cast<std::int16_t>(cosHi), 1);
    return {static_cast<std::int16_t>(nx), static_cast<std::int16_t>(ny)};
}

class Matrix3x3Q15 {
public:
    using Storage = std::array<std::int16_t, kMatrixElements>;

    constexpr std::int16_t &operator()(int row, int col) { return values_[row * kMatrixCols + col]; }
    constexpr std::int16_t operator()(int row, int col) const { return values_[row * kMatrixCols + col]; }
    constexpr const Storage &values() const { return values_; }

    template <typename Table>
    static constexpr Matrix3x3Q15 rotation(Angle16 yaw, Angle16 pitch, Angle16 roll, const Table &table) {
        /* buildRotationMatrixFar/buildRotationMatrix composition. The names
         * match the old scratch variables closely:
         *   sy/cy = yaw sine/cosine
         *   r/d   = pitch ring sine/cosine
         *   p/ro  = roll sine/cosine
         * Matrix entries are high words of doubled products/product sums. */
        Matrix3x3Q15 m;
        const int sy = sine(yaw, table).asInt();
        const int cy = cosine(yaw, table).asInt();
        const int p = sine(roll, table).asInt();
        const int ro = cosine(roll, table).asInt();
        const int r = sine(pitch, table).asInt();
        const int d = cosine(pitch, table).asInt();
        const int msi = q15HighWord(r, p);
        const int mbp = q15HighWord(r, ro);

        m(0, 0) = static_cast<std::int16_t>(q15Sum(msi, sy, cy, ro, false));
        m(0, 1) = static_cast<std::int16_t>(q15Sum(mbp, sy, cy, p, true));
        m(0, 2) = static_cast<std::int16_t>(q15HighWord(sy, d));
        m(1, 0) = static_cast<std::int16_t>(q15HighWord(p, d));
        m(1, 1) = static_cast<std::int16_t>(q15HighWord(ro, d));
        m(1, 2) = static_cast<std::int16_t>(-r);
        m(2, 0) = static_cast<std::int16_t>(q15Sum(msi, cy, sy, ro, true));
        m(2, 1) = static_cast<std::int16_t>(q15Sum(mbp, cy, sy, p, false));
        m(2, 2) = static_cast<std::int16_t>(q15HighWord(cy, d));
        return m;
    }

    template <typename Table>
    static constexpr Matrix3x3Q15 inverseRotation(Angle16 yaw, Angle16 pitch, Angle16 roll, const Table &table) {
        /* buildInverseRotationMatrix composition. This is not simply operator
         * transpose of rotation() because it follows the original formula and
         * preserves the same intermediate rounding points. */
        Matrix3x3Q15 m;
        const int sy = sine(yaw, table).asInt();
        const int cy = cosine(yaw, table).asInt();
        const int p = sine(roll, table).asInt();
        const int ro = cosine(roll, table).asInt();
        const int r = sine(pitch, table).asInt();
        const int d = cosine(pitch, table).asInt();
        const int msi = q15HighWord(r, p);
        const int mbp = q15HighWord(r, ro);

        m(0, 0) = static_cast<std::int16_t>(q15Sum(cy, ro, msi, sy, true));
        m(0, 1) = static_cast<std::int16_t>(-q15HighWord(p, d));
        m(0, 2) = static_cast<std::int16_t>(q15Sum(msi, cy, sy, ro, false));
        m(1, 0) = static_cast<std::int16_t>(q15Sum(mbp, sy, cy, p, false));
        m(1, 1) = static_cast<std::int16_t>(q15HighWord(ro, d));
        m(1, 2) = static_cast<std::int16_t>(q15Sum(sy, p, mbp, cy, true));
        m(2, 0) = static_cast<std::int16_t>(-q15HighWord(sy, d));
        m(2, 1) = static_cast<std::int16_t>(r);
        m(2, 2) = static_cast<std::int16_t>(q15HighWord(cy, d));
        return m;
    }

    constexpr Matrix3x3Q15 operator*(const Matrix3x3Q15 &other) const {
        Matrix3x3Q15 result;
        for (int row = 0; row < kMatrixRows; ++row) {
            for (int col = 0; col < kMatrixCols; ++col) {
                /* Legacy 3x3 multiply accumulates three already-doubled product
                 * bit patterns, wraps modulo 32 bits, then stores the high word. */
                const std::uint32_t acc =
                    doubleProductBits((*this)(row, 0), other(0, col)) +
                    doubleProductBits((*this)(row, 1), other(1, col)) +
                    doubleProductBits((*this)(row, 2), other(2, col));
                result(row, col) = static_cast<std::int16_t>(highWordWrapped(acc));
            }
        }
        return result;
    }

    constexpr std::int32_t rotateVectorComponent(int axis, int vecX, int vecY, int vecZ) const {
        /* egtgt2.c rotateVectorComponent uses matrix[axis], [3+axis], [6+axis]
         * and the historical vector order X,Z,Y. It uses fixedMulQ14, not the
         * matrix high-word multiply used by egseg1. */
        return static_cast<std::int32_t>(Q15::multiplyToInt(values_[axis], vecX)) +
               static_cast<std::int32_t>(Q15::multiplyToInt(values_[kMatrixCols + axis], vecZ)) +
               static_cast<std::int32_t>(Q15::multiplyToInt(values_[2 * kMatrixCols + axis], vecY));
    }

private:
    static constexpr int q15Sum(int a, int b, int c, int d, bool subtract) {
        /* Product-sum helper for the exact two-term formulas in the original
         * rotation builder. Unsigned arithmetic preserves modulo-32-bit wrap. */
        const std::uint32_t t = doubleProductBits(a, b);
        const std::uint32_t u = doubleProductBits(c, d);
        return highWordWrapped(subtract ? t - u : t + u);
    }

    Storage values_{};
};

} // namespace f15::fixed

#endif /* F15_SE2_FIXED_MATH_HPP */

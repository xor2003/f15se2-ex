#include "fixed_math.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <tuple>

namespace {

using f15::fixed::Angle16;
using f15::fixed::Matrix3x3Q15;
using f15::fixed::Q15;
using f15::fixed::WordPair32;
using namespace f15::fixed;

enum TestSamples : int {
    kRawAngleMax = 0xFFFF,
    kSampleAngle = 0x1234,
    kSampleAngleIndex = 0x12,
    kSampleAngleFraction = 0x34,
    kSignedWordMin = -32768,
    kSignedWordMinPlusOne = -32767,
    kSignedWordMax = 32767,
};

enum TestWordPairBits : std::uint32_t {
    kSampleDword = 0x12345678u,
    kSignedDwordMinPattern = 0x80000000u,
    kSignedDwordMaxPattern = 0x7FFFFFFFu,
};

enum TestQ15Samples : int {
    kPositiveHalfQ15 = kAngleQuarterTurn,
    kNegativeQuarterQ15 = -0x2000,
    kPositiveThreeEighthsQ15 = 0x3000,
};

constexpr std::int32_t sar32(std::int32_t value, int count) {
    const std::int64_t divisor = std::int64_t{1} << count;
    const std::int64_t quotient = value / divisor;
    return static_cast<std::int32_t>(quotient - (value < 0 && value % divisor != 0));
}

template <std::size_t N>
constexpr std::array<std::int16_t, N> makeRampTable() {
    std::array<std::int16_t, N> table{};
    for (std::size_t i = 0; i < N; ++i) {
        table[i] = static_cast<std::int16_t>(static_cast<int>(i) * 97 - 12000);
    }
    return table;
}

constexpr std::array<std::int16_t, kAngleTableSize> makeAsinTable() {
    std::array<std::int16_t, kAngleTableSize> table{};
    for (std::size_t i = 0; i < table.size(); ++i) {
        table[i] = static_cast<std::int16_t>(static_cast<int>(i) * 512);
    }
    return table;
}

const auto kRampTable = makeRampTable<kAngleTableSize>();
const auto kAsinTable = makeAsinTable();

template <typename Table>
int legacySine(int angle, const Table &table) {
    const auto a = static_cast<std::uint16_t>(angle);
    const int idx = (a >> kAngleIndexShift) & kAngleFractionMask;
    const int frac = a & kAngleFractionMask;
    const int v0 = table[idx];
    const int v1 = table[idx + 1];
    const std::int32_t step = static_cast<std::int32_t>(v1 - v0) * frac;
    return v0 + static_cast<int>(sar32(step + kSineInterpolationRound, kAngleIndexShift));
}

int legacyFixedMulQ14(int a, int b) {
    const std::int32_t p = static_cast<std::int32_t>(a) * static_cast<std::int32_t>(b);
    return static_cast<int>(sar32(p, kQ15ProductShift) + (sar32(p, kQ15RoundBitShift) & kQ15RoundBit));
}

std::uint32_t legacyDoubleProductBits(int a, int b) {
    return static_cast<std::uint32_t>(static_cast<std::int32_t>(a) * static_cast<std::int32_t>(b)) << kDoubleProductShift;
}

int legacyHighWord(std::uint32_t value) {
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(value >> kWordBits));
}

int legacyQ15High(int a, int b) {
    return legacyHighWord(legacyDoubleProductBits(a, b));
}

int legacyQ15Sum(int a, int b, int c, int d, bool subtract) {
    const std::uint32_t t = legacyDoubleProductBits(a, b);
    const std::uint32_t u = legacyDoubleProductBits(c, d);
    return legacyHighWord(subtract ? t - u : t + u);
}

int legacyDoubledHighWordWithCarry(std::int32_t value) {
    const std::uint32_t doubled = static_cast<std::uint32_t>(value) << kDoubleProductShift;
    return static_cast<std::int16_t>(legacyHighWord(doubled) + ((doubled & kLowWordCarryBit) ? 1 : 0));
}

std::int32_t legacyShiftLongLeft(std::int32_t value, int count) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(value) << count);
}

std::int32_t legacyShiftLongRight(std::int32_t value, int count) {
    return sar32(value, count);
}

int legacyAbs16(int value) {
    /* Original DOS int abs(-32768) overflows and leaves the value negative. */
    const int wordValue = static_cast<std::int16_t>(value);
    return wordValue == static_cast<std::int16_t>(0x8000) ? wordValue : std::abs(wordValue);
}

int legacyRangeApprox(int dx, int dy) {
    dx = legacyAbs16(dx);
    dy = legacyAbs16(dy);
    const std::int32_t dist = dx > dy
        ? static_cast<std::int32_t>(dy >> kRangeApproxHalfShift) + dx
        : static_cast<std::int32_t>(dx >> kRangeApproxHalfShift) + dy;
    return dist > kRangeApproxMax ? kRangeApproxMax : static_cast<int>(dist);
}

int legacyClampValue(int value, int minVal, int maxVal) {
    return value > maxVal ? maxVal : (value < minVal ? minVal : value);
}

int legacyClampRange(int value, int minVal, int maxVal) {
    if (value > maxVal) {
        return maxVal;
    }
    if (value >= minVal) {
        return value;
    }
    return value <= kClampWrapFloor ? maxVal : minVal;
}

int legacySignOf(int value) {
    return value == 0 ? 0 : (value > 0 ? 1 : -1);
}

int legacyRandomRangeFromRand(int randValue, int maxVal) {
    return static_cast<int>((static_cast<std::int32_t>(randValue) * maxVal) >> kRandomScaleShift);
}

int legacyIntegerSqrt(int value) {
    int quotient;
    int guess;
    value = legacyAbs16(value);
    if (value < kSqrtSmallThreshold) {
        return 1;
    }
    guess = value >> kSqrtInitialShift;
    do {
        quotient = value / guess;
        guess = (guess + quotient) >> 1;
    } while (legacyAbs16(guess - quotient) > kSqrtConvergenceDelta);
    return guess;
}

Angle16 legacyBearing(int deltaX, int deltaY) {
    if (deltaX == 0) {
        return Angle16(deltaY > 0 ? kAngleNorth : kAngleHalfTurn);
    }
    if (deltaY == 0) {
        return Angle16(deltaX > 0 ? kAngleQuarterTurn : kAngleThreeQuarterTurn);
    }

    int angle;
    int result;
    std::int32_t numer;
    int denom;
    int swapped;
    int ratio;
    if (legacyAbs16(deltaX) > legacyAbs16(deltaY)) {
        numer = static_cast<std::int32_t>(legacyAbs16(deltaY)) << kBearingRatioShift;
        denom = legacyAbs16(deltaX);
        swapped = 1;
    } else {
        numer = static_cast<std::int32_t>(legacyAbs16(deltaX)) << kBearingRatioShift;
        denom = legacyAbs16(deltaY);
        swapped = 0;
    }
    ratio = static_cast<int>(numer / denom);
    angle = static_cast<int>(((kBearingScaleBase -
                               ((static_cast<std::int32_t>(std::abs(kBearingRatioBias - ratio)) *
                                 kBearingCurvature) >> kBearingRatioShift)) *
                              static_cast<std::int32_t>(ratio)) >> kBearingRatioShift);
    if (deltaX > 0) {
        if (deltaY > 0) {
            result = swapped ? kAngleQuarterTurn - angle : angle;
        } else {
            result = swapped ? angle + kAngleQuarterTurn : kAngleHalfTurn - angle;
        }
    } else {
        if (deltaY > 0) {
            result = swapped ? angle + kAngleThreeQuarterTurn : -angle;
        } else {
            result = swapped ? kAngleThreeQuarterTurn - angle : angle + kAngleHalfTurn;
        }
    }
    return Angle16(result);
}

template <typename Table>
Angle16 legacyValueToAngle(int value, const Table &table) {
    if (value == kSignedWordMinPattern) {
        return Angle16(kAngleThreeQuarterTurn);
    }
    const int magnitude = std::abs(value);
    int angle = 0;
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

std::uint16_t legacySignedRatio16Bits(int numerator, int denominator) {
    const bool negative = (numerator < 0) != (denominator < 0);
    const std::uint32_t absNumerator = static_cast<std::uint32_t>(numerator < 0 ? -numerator : numerator);
    const std::uint32_t absDenominator = static_cast<std::uint32_t>(denominator < 0 ? -denominator : denominator);
    const std::uint16_t magnitude = static_cast<std::uint16_t>(((absNumerator << kWordBits) / absDenominator) >> 1);
    return negative ? static_cast<std::uint16_t>(-static_cast<int>(magnitude)) : magnitude;
}

int legacyHudPitchScale(int anglePitch) {
    return static_cast<int>((static_cast<std::uint32_t>(static_cast<std::uint16_t>(anglePitch)) *
                             kHudPitchScale) >> kAngleIndexShift);
}

std::uint32_t legacyUnsignedDiv32By16Full(std::uint32_t num, std::uint16_t den) {
    std::uint32_t rem = 0;
    std::uint32_t q = 0;
    if (den == 0) {
        return static_cast<std::uint32_t>(kFullDivideByZeroResult);
    }
    for (int i = 0; i < kLongBits; ++i) {
        rem <<= 1;
        if (num & kSignedDwordMinPattern) {
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

std::uint16_t legacyUnsignedDiv32By16Saturated(std::uint32_t num, std::uint16_t den) {
    const std::uint32_t q = legacyUnsignedDiv32By16Full(num, den);
    if (den == 0 || q > kRangeApproxMax) {
        return kDivideOverflowSaturation;
    }
    return static_cast<std::uint16_t>(q);
}

std::int32_t legacySignedDivFull(std::int32_t num, int den) {
    const bool negative = (num < 0) != (den < 0);
    const std::uint32_t absNum = static_cast<std::uint32_t>(num < 0 ? -num : num);
    const std::uint16_t absDen = static_cast<std::uint16_t>(den < 0 ? -den : den);
    const std::uint32_t q = legacyUnsignedDiv32By16Full(absNum, absDen);
    return negative ? -static_cast<std::int32_t>(q) : static_cast<std::int32_t>(q);
}

int legacySignedDiv32By16Saturated(std::int32_t num, int den) {
    const bool negative = (num < 0) != (den < 0);
    const std::uint32_t absNum = static_cast<std::uint32_t>(num < 0 ? -num : num);
    const std::uint16_t absDen = static_cast<std::uint16_t>(den < 0 ? -den : den);
    const std::uint16_t q = legacyUnsignedDiv32By16Saturated(absNum, absDen);
    return negative ? -static_cast<int>(q) : static_cast<int>(q);
}

int legacyClipMulDiv(int a, int b, int denominator) {
    return legacySignedDiv32By16Saturated(static_cast<std::int32_t>(a) * b, denominator);
}

f15::fixed::ProjectedPoint legacyProjectVertexToScreen(std::int32_t camX,
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
        divisor = static_cast<int>(sar32(divisor, extraScaleShift));
    }
    if (divisor <= 0) {
        return {static_cast<std::int32_t>(kProjectionInvalidDword),
                static_cast<std::int32_t>(kProjectionInvalidDword)};
    }
    const std::int32_t xNum = sar32(camX, kProjectionByteShift);
    const std::int32_t yBase = sar32(camY, kProjectionByteShift);
    const std::int32_t yNum = sar32(yBase, kProjectionAspectShift) - yBase;
    return {legacySignedDivFull(xNum, divisor) + viewCenterX,
            legacySignedDivFull(yNum, divisor) + viewCenterY};
}

std::pair<std::int16_t, std::int16_t> legacyRotateHudLadderPoint(int x, int y, int sinR, int cosR) {
    const int nx = legacyQ15High(sinR, x) - legacyQ15High(cosR, y);
    const std::uint32_t sinProduct = static_cast<std::uint32_t>(static_cast<std::int32_t>(sinR) * y);
    const std::uint32_t cosProduct = static_cast<std::uint32_t>(static_cast<std::int32_t>(cosR) * x);
    const int sinHi = legacyHighWord((sinProduct << kDoubleProductShift) + sinProduct);
    const int cosHi = legacyHighWord((cosProduct << kDoubleProductShift) + cosProduct);
    const int ny = sar32(static_cast<std::int16_t>(sinHi), 1) +
                   sar32(static_cast<std::int16_t>(cosHi), 1);
    return {static_cast<std::int16_t>(nx), static_cast<std::int16_t>(ny)};
}

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(1);
    }
}

void testAnglesAndTableLookup() {
    require((Angle16(kRawAngleMax) + Angle16(2)).raw() == 1, "Angle16 wraps on addition");
    require((Angle16(1) - Angle16(2)).raw() == kRawAngleMax, "Angle16 wraps on subtraction");
    require(Angle16(kSampleAngle).tableIndex() == kSampleAngleIndex, "Angle16 table index is high byte");
    require(Angle16(kSampleAngle).fraction() == kSampleAngleFraction, "Angle16 interpolation fraction is low byte");

    const int sampleAngles[] = {
        kAngleNorth,
        1,
        kSineInterpolationRound - 1,
        kSineInterpolationRound,
        kAngleFractionMask,
        kWordDegreeStep,
        kSampleAngle,
        kAngleQuarterTurn - 1,
        kAngleQuarterTurn,
        kAngleHalfTurn - 1,
        kAngleHalfTurn,
        kAngleThreeQuarterTurn - 1,
        kAngleThreeQuarterTurn,
        static_cast<int>(kAngleHalfTurn) + kAngleFractionMask,
        kRawAngleMax,
    };
    for (int angle : sampleAngles) {
        require(f15::fixed::sine(Angle16(angle), kRampTable).asInt() == legacySine(angle, kRampTable),
                "sine interpolation matches legacy formula");
        require(f15::fixed::cosine(Angle16(angle), kRampTable).asInt() == legacySine(angle + kAngleQuarterTurn, kRampTable),
                "cosine is sine plus quarter turn");
    }
}

void testFixedMultiply() {
    const int values[] = {
        kSignedWordMin,
        kSignedWordMinPlusOne,
        -20000,
        -16384,
        -1,
        0,
        1,
        12345,
        16384,
        20000,
        32766,
        kSignedWordMax,
    };
    for (int a : values) {
        for (int b : values) {
            require(Q15::multiplyToInt(a, b) == legacyFixedMulQ14(a, b),
                    "fixedMulQ14 rounding matches legacy formula");
            require(f15::fixed::q15HighWord(a, b) == legacyQ15High(a, b),
                    "q15HighWord matches wrapped doubled-product high word");
            require(f15::fixed::q15HighWordRounded(a, b) ==
                        legacyDoubledHighWordWithCarry(static_cast<std::int32_t>(a) * b),
                    "q15HighWordRounded matches high-word plus low-carry idiom");
        }
    }
    require(Q15::multiplyToInt(kSignedWordMin, kSignedWordMin) == kSignedWordMinPattern,
            "fixedMulQ14 preserves 0x8000 positive int result");
    require(f15::fixed::q15HighWord(kSignedWordMin, kSignedWordMin) == kSignedWordMin,
            "matrix-style q15 high word wraps to 0x8000");
}

void testLongShiftsAndCarryRounding() {
    const std::int32_t values[] = {
        0,
        1,
        -1,
        static_cast<std::int32_t>(kSampleDword),
        static_cast<std::int32_t>(kSignedDwordMinPattern),
        static_cast<std::int32_t>(kSignedDwordMaxPattern),
    };
    for (std::int32_t value : values) {
        for (int count : {0, 1, 3, 8, 15}) {
            require(f15::fixed::shiftLongLeft(value, count) == legacyShiftLongLeft(value, count),
                    "shiftLongLeft matches 32-bit register shift");
            require(f15::fixed::shiftLongRight(value, count) == legacyShiftLongRight(value, count),
                    "shiftLongRight matches arithmetic signed-long shift");
        }
        require(f15::fixed::doubledHighWordWithCarry(value) == legacyDoubledHighWordWithCarry(value),
                "doubledHighWordWithCarry matches dirRound/LOCARRY idiom");
    }
}

void testLegacyApproximations() {
    const int clampSamples[] = {
        kSignedWordMin,
        -20000,
        kClampWrapFloor,
        kClampWrapFloor + 1,
        -11,
        -10,
        -9,
        0,
        9,
        10,
        11,
        20000,
        kSignedWordMax,
    };
    for (int value : clampSamples) {
        require(f15::fixed::clampValue(value, -10, 10) == legacyClampValue(value, -10, 10),
                "clampValue matches plain clamp");
        require(f15::fixed::clampRange(value, -10, 10) == legacyClampRange(value, -10, 10),
                "clampRange matches wrap-floor clamp");
        require(f15::fixed::signOf(value) == legacySignOf(value),
                "signOf matches -1/0/1 contract");
    }
    for (auto [randValue, maxVal] : {std::pair{0, 100}, std::pair{1, 100},
                                     std::pair{16384, 100}, std::pair{32767, 2000}}) {
        require(f15::fixed::randomRangeFromRand(randValue, maxVal) ==
                    legacyRandomRangeFromRand(randValue, maxVal),
                "randomRangeFromRand matches rand scaling");
    }
    const int sqrtSamples[] = {kSignedWordMin, kSignedWordMinPlusOne, -10000, -1024, -17, -4, -3, -1,
                               0, 1, 3, 4, 15, 16, 17, 255, 1024, 4096, 16384, kSignedWordMax};
    for (int value : sqrtSamples) {
        require(f15::fixed::integerSqrtCompatible(value) == legacyIntegerSqrt(value),
                "integerSqrtCompatible matches legacy Newton iteration");
    }

    for (auto [dx, dy] : {std::pair<int, int>{0, 0}, std::pair<int, int>{10, 4},
                          std::pair<int, int>{-10, 4}, std::pair<int, int>{4, -10},
                          std::pair<int, int>{-4, -10}, std::pair<int, int>{40000, 40000},
                          std::pair<int, int>{kSignedWordMin, 0},
                          std::pair<int, int>{0, kSignedWordMin},
                          std::pair<int, int>{kSignedWordMax, kSignedWordMin}}) {
        require(f15::fixed::rangeApprox(dx, dy) == legacyRangeApprox(dx, dy),
                "rangeApprox matches max plus half-min cap");
    }

    for (auto [dx, dy] : {std::pair<int, int>{0, 1}, std::pair<int, int>{0, -1},
                          std::pair<int, int>{1, 0}, std::pair<int, int>{-1, 0},
                          std::pair<int, int>{100, 100}, std::pair<int, int>{100, -50},
                          std::pair<int, int>{-50, 100}, std::pair<int, int>{-100, -20},
                          std::pair<int, int>{7, -123}, std::pair<int, int>{123, 7},
                          std::pair<int, int>{-123, 7}, std::pair<int, int>{7, 123},
                          std::pair<int, int>{-7, -123},
                          std::pair<int, int>{kSignedWordMax, 1},
                          std::pair<int, int>{1, kSignedWordMax}}) {
        require(f15::fixed::computeBearing(dx, dy).raw() == legacyBearing(dx, dy).raw(),
                "computeBearing matches legacy quadrant approximation");
    }
}

void testDivisionProjectionAndHudRotation() {
    for (auto [num, den] : {std::pair<std::uint32_t, std::uint16_t>{0, 1},
                            std::pair<std::uint32_t, std::uint16_t>{1000, 3},
                            std::pair<std::uint32_t, std::uint16_t>{kSignedDwordMaxPattern, kRangeApproxMax},
                            std::pair<std::uint32_t, std::uint16_t>{kSignedDwordMinPattern, 1},
                            std::pair<std::uint32_t, std::uint16_t>{1234, 0}}) {
        require(f15::fixed::unsignedDiv32By16Full(num, den) == legacyUnsignedDiv32By16Full(num, den),
                "unsignedDiv32By16Full matches long division");
        require(f15::fixed::unsignedDiv32By16Saturated(num, den) == legacyUnsignedDiv32By16Saturated(num, den),
                "unsignedDiv32By16Saturated matches overflow cap");
    }

    for (auto [num, den] : {std::pair<std::int32_t, int>{1000, 3},
                            std::pair<std::int32_t, int>{-1000, 3},
                            std::pair<std::int32_t, int>{1000, -3},
                            std::pair<std::int32_t, int>{-1000, -3},
                            std::pair<std::int32_t, int>{1234, 0}}) {
        require(f15::fixed::signedDivFull(num, den) == legacySignedDivFull(num, den),
                "signedDivFull matches full signed divide");
        require(f15::fixed::signedDiv32By16Saturated(num, den) == legacySignedDiv32By16Saturated(num, den),
                "signedDiv32By16Saturated matches saturated signed divide");
    }

    for (auto [a, b, d] : {std::tuple{10, 20, 3}, std::tuple{-10, 20, 3},
                           std::tuple{10000, 10000, 1}, std::tuple{10, 20, 0}}) {
        require(f15::fixed::clipMulDiv(a, b, d) == legacyClipMulDiv(a, b, d),
                "clipMulDiv matches saturated signed multiply/divide");
    }

    for (auto [x, y, maxX, maxY] : {std::tuple{0, 0, 319, 199}, std::tuple{-1, 0, 319, 199},
                                    std::tuple{320, 200, 319, 199}, std::tuple{10, -5, 319, 199}}) {
        require(f15::fixed::clipOutcode(x, y, maxX, maxY) ==
                    ((x < 0 ? kClipLeft : (x > maxX ? kClipRight : kClipInside)) |
                     (y < 0 ? kClipTop : (y > maxY ? kClipBottom : kClipInside))),
                "clipOutcode matches renderer outcode bits");
    }

    const auto inside = f15::fixed::clipHorizonLine(10, 10, 20, 20, 100, 100);
    require(inside.visible && inside.x1 == 10 && inside.y1 == 10 && inside.x2 == 20 && inside.y2 == 20,
            "clipHorizonLine leaves fully visible lines unchanged");
    const auto leftReject = f15::fixed::clipHorizonLine(-10, 10, -5, 20, 100, 100);
    require(!leftReject.visible, "clipHorizonLine rejects shared outside half-plane");
    const auto leftCross = f15::fixed::clipHorizonLine(-10, 50, 10, 50, 100, 100);
    require(leftCross.visible && leftCross.x1 == 0 && leftCross.y1 == 50 &&
                leftCross.x2 == 10 && leftCross.y2 == 50,
            "clipHorizonLine clips against left edge");
    const auto topCross = f15::fixed::clipHorizonLine(50, -10, 50, 10, 100, 100);
    require(topCross.visible && topCross.x1 == 50 && topCross.y1 == 0 &&
                topCross.x2 == 50 && topCross.y2 == 10,
            "clipHorizonLine clips against top edge");

    for (auto [camX, camY, div] : {std::tuple<std::int32_t, std::int32_t, int>{4096, -2048, 16},
                                   std::tuple<std::int32_t, std::int32_t, int>{-8192, 4096, 8},
                                   std::tuple<std::int32_t, std::int32_t, int>{4096, 4096, 0}}) {
        const auto got = f15::fixed::projectVertexToScreen(camX, camY, div, 160, 100, false, 0);
        const auto expected = legacyProjectVertexToScreen(camX, camY, div, 160, 100, false, 0);
        require(got.x == expected.x && got.y == expected.y,
                "projectVertexToScreen matches perspective divide formula");
    }

    for (auto [x, y, sinR, cosR] : {std::tuple<int, int, int, int>{10, 20, kPositiveHalfQ15, 0},
                                    std::tuple<int, int, int, int>{-100, 50, kNegativeQuarterQ15, kPositiveThreeEighthsQ15},
                                    std::tuple<int, int, int, int>{32767, -32768, 32767, -32768}}) {
        const auto got = f15::fixed::rotateHudLadderPoint(x, y, Q15(sinR), Q15(cosR));
        const auto expected = legacyRotateHudLadderPoint(x, y, sinR, cosR);
        require(got == expected, "rotateHudLadderPoint matches HUD ladder math");
    }
}

void testInverseTrig() {
    const int inverseTrigSamples[] = {kSignedWordMin, kSignedWordMinPlusOne, -4096, -2048, -513, -512,
                                      -1, 0, 1, 511, 512, 2048, 4096, 32766, kSignedWordMax};
    for (int value : inverseTrigSamples) {
        require(f15::fixed::valueToAngle(value, kAsinTable).raw() == legacyValueToAngle(value, kAsinTable).raw(),
                "valueToAngle matches table search and interpolation");
        const auto complement = f15::fixed::complementAngle(value, kAsinTable);
        require(complement.raw() == (Angle16(kAngleQuarterTurn) - legacyValueToAngle(value, kAsinTable)).raw(),
                "complementAngle is quarter turn minus valueToAngle");
    }
    require(f15::fixed::valueToAngle(kSignedWordMinPattern, kAsinTable).raw() == kAngleThreeQuarterTurn,
            "valueToAngle preserves 0x8000 special case");
}

void testFlightHudHelpers() {
    for (auto [num, den] : {std::pair{0, 1}, std::pair{1, 1}, std::pair{-1, 1},
                            std::pair{1, -1}, std::pair{-1, -1}, std::pair{16383, 32767},
                            std::pair{-16383, 32767}, std::pair{32767, 32767},
                            std::pair{-32767, 32767}, std::pair{-12000, -24000},
                            std::pair{12345, 23456}}) {
        require(f15::fixed::signedRatio16Bits(num, den) == legacySignedRatio16Bits(num, den),
                "signedRatio16Bits matches IntDiv word-scaled quotient");
    }

    const int pitches[] = {0, 1, -1, kAngleQuarterTurn, kAngleHalfTurn, kRawAngleMax};
    for (int pitch : pitches) {
        require(f15::fixed::hudPitchScale(pitch) == legacyHudPitchScale(pitch),
                "hudPitchScale matches high-byte pitch ladder scale");
    }
}

void testWordPairsAndMatrices() {
    for (std::int32_t value : {0, 1, -1, static_cast<std::int32_t>(kSampleDword),
                               static_cast<std::int32_t>(kSignedDwordMinPattern),
                               static_cast<std::int32_t>(kSignedDwordMaxPattern)}) {
        require(WordPair32::split(value).join() == value, "WordPair32 round trips lo/hi words");
    }

    Matrix3x3Q15 a;
    Matrix3x3Q15 b;
    a(0, 0) = kRangeApproxMax;
    a(1, 1) = kRangeApproxMax;
    a(2, 2) = kRangeApproxMax;
    b(0, 0) = kSignedWordMin;
    b(1, 1) = kAngleQuarterTurn;
    b(2, 2) = kAngleQuarterTurn / 2;
    b(0, 1) = 1234;
    b(1, 2) = -5678;

    const Matrix3x3Q15 product = a * b;
    for (int row = 0; row < kMatrixRows; ++row) {
        for (int col = 0; col < kMatrixCols; ++col) {
            const std::uint32_t acc =
                legacyDoubleProductBits(a(row, 0), b(0, col)) +
                legacyDoubleProductBits(a(row, 1), b(1, col)) +
                legacyDoubleProductBits(a(row, 2), b(2, col));
            require(product(row, col) == legacyHighWord(acc), "Matrix3x3Q15 multiply matches legacy high-word dot product");
        }
    }

    const Matrix3x3Q15 rotation = Matrix3x3Q15::rotation(Angle16(0), Angle16(0), Angle16(0), kRampTable);
    const Matrix3x3Q15 inverse = Matrix3x3Q15::inverseRotation(Angle16(0), Angle16(0), Angle16(0), kRampTable);
    const int sy = legacySine(0, kRampTable);
    const int cy = legacySine(kAngleQuarterTurn, kRampTable);
    const int p = legacySine(0, kRampTable);
    const int ro = legacySine(kAngleQuarterTurn, kRampTable);
    const int r = legacySine(0, kRampTable);
    const int d = legacySine(kAngleQuarterTurn, kRampTable);
    const int msi = legacyQ15High(r, p);
    const int mbp = legacyQ15High(r, ro);
    const std::int16_t expectedRotation[kMatrixElements] = {
        static_cast<std::int16_t>(legacyQ15Sum(msi, sy, cy, ro, false)),
        static_cast<std::int16_t>(legacyQ15Sum(mbp, sy, cy, p, true)),
        static_cast<std::int16_t>(legacyQ15High(sy, d)),
        static_cast<std::int16_t>(legacyQ15High(p, d)),
        static_cast<std::int16_t>(legacyQ15High(ro, d)),
        static_cast<std::int16_t>(-r),
        static_cast<std::int16_t>(legacyQ15Sum(msi, cy, sy, ro, true)),
        static_cast<std::int16_t>(legacyQ15Sum(mbp, cy, sy, p, false)),
        static_cast<std::int16_t>(legacyQ15High(cy, d)),
    };
    for (int row = 0; row < kMatrixRows; ++row) {
        for (int col = 0; col < kMatrixCols; ++col) {
            require(rotation(row, col) == expectedRotation[row * kMatrixCols + col],
                    "rotation matrix uses legacy composition formula");
        }
    }

    const std::int16_t expectedInverse[kMatrixElements] = {
        static_cast<std::int16_t>(legacyQ15Sum(cy, ro, msi, sy, true)),
        static_cast<std::int16_t>(-legacyQ15High(p, d)),
        static_cast<std::int16_t>(legacyQ15Sum(msi, cy, sy, ro, false)),
        static_cast<std::int16_t>(legacyQ15Sum(mbp, sy, cy, p, false)),
        static_cast<std::int16_t>(legacyQ15High(ro, d)),
        static_cast<std::int16_t>(legacyQ15Sum(sy, p, mbp, cy, true)),
        static_cast<std::int16_t>(-legacyQ15High(sy, d)),
        static_cast<std::int16_t>(r),
        static_cast<std::int16_t>(legacyQ15High(cy, d)),
    };
    for (int row = 0; row < kMatrixRows; ++row) {
        for (int col = 0; col < kMatrixCols; ++col) {
            require(inverse(row, col) == expectedInverse[row * kMatrixCols + col],
                    "inverse rotation matrix uses legacy composition formula");
        }
    }

    for (int axis = 0; axis < kMatrixCols; ++axis) {
        const int vecX = 3000;
        const int vecY = -2000;
        const int vecZ = 1000;
        const std::int32_t expected =
            legacyFixedMulQ14(b(0, axis), vecX) +
            legacyFixedMulQ14(b(1, axis), vecZ) +
            legacyFixedMulQ14(b(2, axis), vecY);
        require(b.rotateVectorComponent(axis, vecX, vecY, vecZ) == expected,
                "rotateVectorComponent matches legacy X,Z,Y column access");
    }
}

} // namespace

int main() {
    const auto minimum = std::int32_t{-2147483647 - 1};
    require(WordPair32::split(minimum).join() == minimum, "minimum word-pair round trip");
    require(WordPair32::split(-1).join() == -1, "negative word-pair round trip");
    for (int count = 0; count < 32; ++count) {
        const auto expected = static_cast<std::int32_t>(
            static_cast<std::int64_t>(minimum) / (std::int64_t{1} << count));
        require(f15::fixed::shiftLongRight(minimum, count) == expected, "minimum arithmetic shift");
    }
    require(signedDivFull(minimum, 2) == -1073741824, "minimum signed dividend");
    require(signedDiv32By16Saturated(minimum, 2) == -0x7f00, "minimum saturated dividend");
    require(f15::fixed::computeBearing(-32768, 1).raw() == 0, "bearing retains word ratio truncation");
    testAnglesAndTableLookup();
    testFixedMultiply();
    testLongShiftsAndCarryRounding();
    testLegacyApproximations();
    testDivisionProjectionAndHudRotation();
    testInverseTrig();
    testFlightHudHelpers();
    testWordPairsAndMatrices();
    std::cout << "fixed_math_tests passed\n";
    return 0;
}

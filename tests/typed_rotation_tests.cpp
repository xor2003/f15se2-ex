#include "math/legacy_rotation.hpp"
using f15::math::legacy::signedAngle;
using f15::math::legacy::angleFromWord;
using f15::math::legacy::angleMagnitude;
using f15::math::legacy::angleMagnitudeCompat;
#define F15_MATH_BOUNDARY_ACCESS
#include "math/boundary.hpp"
#undef F15_MATH_BOUNDARY_ACCESS
#include "math_rotation_reference.hpp"
#include "math_attitude_reference.hpp"
#include "math/interpolation.hpp"
#include "egdata.h"
#include "egcode.h"
#include "eg3dcam.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <utility>

extern void applyRotationDelta(const f15::math::Matrix3<f15::math::FixedBackend> &,
                               const f15::math::Matrix3<f15::math::FixedBackend> &);
extern void rebuildOrientation();
extern void computeAttitudeAngles();

namespace {
using namespace f15::math;
using F = FixedBackend;
using M = ModernBackend;
using FC = Boundary<F>;
using MC = Boundary<M>;

void require(bool value, const char *message) {
    if (!value) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
template<class B> EulerAngles<B> angles(int yaw, int pitch, int roll) {
    return {Boundary<B>::angleWord(static_cast<std::uint16_t>(yaw)),
            Boundary<B>::angleWord(static_cast<std::uint16_t>(pitch)),
            Boundary<B>::angleWord(static_cast<std::uint16_t>(roll))};
}
rotation_reference::Matrix words(const Matrix3<F> &matrix) {
    rotation_reference::Matrix result{};
    FC::matrixWords(matrix, result.data());
    return result;
}
void orthogonal(const Matrix3<M> &matrix, double tolerance) {
    const auto m = MC::matrix(matrix);
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col) {
            double dot = 0;
            for (int k = 0; k < 3; ++k) dot += m[row * 3 + k] * m[col * 3 + k];
            require(std::isfinite(dot) && std::abs(dot - (row == col ? 1.0 : 0.0)) < tolerance,
                    "modern rotation lost orthogonality");
        }
    const double determinant = m[0]*(m[4]*m[8]-m[5]*m[7]) - m[1]*(m[3]*m[8]-m[5]*m[6])
                             + m[2]*(m[3]*m[7]-m[4]*m[6]);
    require(std::abs(determinant - 1) < tolerance, "modern rotation changed handedness");
}

void fixedAndCallers() {
    const RotationMath<F> math(g_angleLut);
    for (int a = 0; a < 65536; ++a) {
        require(FC::coefficientWord(math.sine(FC::angleWord(a))) ==
                rotation_reference::sine(a, g_angleLut), "fixed sine differs from frozen oracle");
    }
    std::uint32_t seed = 0x914723u;
    auto next = [&]() { seed = seed * 1664525u + 1013904223u; return static_cast<int>(seed >> 16); };
    const int edges[] = {0, 1, 0x3fff, 0x4000, 0x7fff, 0x8000, 0xbfff, 0xc000, 0xffff};
    for (int sample = 0; sample < 10000; ++sample) {
        const int yaw = sample < 729 ? edges[sample / 81] : next();
        const int pitch = sample < 729 ? edges[sample / 9 % 9] : next();
        const int roll = sample < 729 ? edges[sample % 9] : next();
        const auto a = angles<F>(yaw, pitch, roll);
        const auto expected = rotation_reference::rotation(yaw, pitch, roll, g_angleLut);
        const auto matrix = math.rotation(a);
        require(words(matrix) == expected, "fixed rotation differs from frozen oracle");
        require(words(math.objectRotation(a)) == rotation_reference::rotation(yaw, pitch, roll, g_angleLut, true),
                "fixed object rotation differs from frozen oracle");
        int16 actual[9];
        require(buildRotationMatrixFar(actual, yaw, pitch, roll) == 0, "legacy return changed");
        require(std::equal(expected.begin(), expected.end(), actual), "production rotation adapter changed");
        require(g_rotSinYaw == rotation_reference::sine(yaw, g_angleLut) &&
                g_rotCosYaw == rotation_reference::sine(yaw + 16384, g_angleLut) &&
                g_sphereRadius == rotation_reference::sine(pitch, g_angleLut) &&
                g_sphereDistZ == rotation_reference::sine(pitch + 16384, g_angleLut) &&
                g_spherePitch == rotation_reference::sine(roll, g_angleLut) &&
                g_sphereRoll == rotation_reference::sine(roll + 16384, g_angleLut), "horizon terms changed");
        setViewRotation(static_cast<int16>(yaw), static_cast<int16>(pitch), static_cast<int16>(roll));
        const auto camera = rotation_reference::rotation(-yaw, -pitch, -roll, g_angleLut);
        require(std::equal(camera.begin(), camera.end(), g_viewRotMatrix), "camera caller changed");
        require(words(cameraRotation(math, a)) == camera, "typed camera caller changed");

        const int by = next(), bp = next(), br = next();
        const auto b = rotation_reference::rotation(by, bp, br, g_angleLut);
        const auto product = rotation_reference::multiply(expected, b);
        require(words(matrix * FC::matrixWords(b.data())) == product, "fixed product changed");
        g_rotationCounter = 7;
        g_orientationDirty = 0;
        applyRotationDelta(FC::matrixWords(expected.data()), FC::matrixWords(b.data()));
        require(product == words(g_orientMatrix) &&
                product == words(g_matrixScratch) &&
                g_rotationCounter == 8 && g_orientationDirty == 1, "flight rotation caller changed");
        g_ourHead = angleFromWord(static_cast<int16>(yaw)); g_ourPitch = angleFromWord(static_cast<int16>(pitch)); g_ourRoll = angleFromWord(static_cast<int16>(roll));
        rebuildOrientation();
        require(expected == words(g_orientMatrix) &&
                g_rotationCounter == 0 && g_orientationDirty == 0, "flight rebuild caller changed");
    }
    // General word matrices exercise accumulator wrap, not only near-unit rotations.
    for (int sample = 0; sample < 2000; ++sample) {
        rotation_reference::Matrix a{}, b{};
        for (int i = 0; i < 9; ++i) { a[i] = rotation_reference::word(next()); b[i] = rotation_reference::word(next()); }
        require(words(FC::matrixWords(a.data()) * FC::matrixWords(b.data())) == rotation_reference::multiply(a, b),
                "fixed arbitrary matrix accumulator wrap changed");
    }
    /* Magnitude adapters: exhaustive word oracle. angleMagnitude matches the
     * hosted abs(int16) sites (word 0x8000 -> +32768); angleMagnitudeCompat
     * matches abs16Compat (0x8000 stays -32768), which post-abs (int16) casts
     * re-wrap to as well. */
    for (int w = 0; w < 65536; ++w) {
        const auto angle = angleFromWord(w);
        require(angleMagnitude(angle) == std::abs(static_cast<int>(static_cast<int16>(w))),
                "fixed angleMagnitude differs from abs(int16)");
        require(angleMagnitudeCompat(angle) == abs16Compat(static_cast<int16>(w)),
                "fixed angleMagnitudeCompat differs from abs16Compat");
        require(angleMagnitudeCompat(angle) == static_cast<int16>(std::abs(static_cast<int>(static_cast<int16>(w)))),
                "post-abs int16 cast and abs16Compat disagree");
    }
    for (int sample = 0; sample < 20000; ++sample) {
        const int a = sample < 81 ? edges[sample / 9] : next();
        const int b = sample < 81 ? edges[sample % 9] : next();
        require(angleMagnitude(angleFromWord(a) - angleFromWord(b)) ==
                std::abs(static_cast<int>(static_cast<int16>(a - b))), "fixed magnitude diff changed");
        require(angleMagnitudeCompat(angleFromWord(a) - angleFromWord(b)) ==
                abs16Compat(static_cast<int16>(a - b)), "fixed compat magnitude diff changed");
    }
}

void modernPrecision() {
    const RotationMath<M> math;
    constexpr double pi = 3.14159265358979323846;
    for (int raw = 0; raw < 65536; ++raw) {
        const auto angle = MC::angleWord(raw);
        require(std::abs(MC::coefficient(math.sine(angle)) - std::sin(raw * (2*pi/65536))) < 1e-14,
                "modern sine accuracy");
    }
    for (int i = 0; i < 1000; ++i) {
        const auto a = angles<M>(i * 61, i * 127, i * 211);
        orthogonal(math.rotation(a), 2e-14);
        orthogonal(math.objectRotation(a), 2e-14);
        const auto rebuilt = MC::matrix(math.rotation(math.recover(math.rotation(a), false).angles));
        const auto original = MC::matrix(math.rotation(a));
        for (int j = 0; j < 9; ++j)
            require(std::abs(rebuilt[j] - original[j]) < 2e-14, "modern attitude round trip");
        const auto product = math.objectRotation(a) * cameraRotation(math, a);
        const auto m = MC::matrix(product);
        for (int j = 0; j < 9; ++j)
            require(std::abs(m[j] - (j % 4 == 0 ? 1.0 : 0.0)) < 2e-14, "modern object/camera convention");
    }
    auto accumulated = math.rotation({});
    const auto tiny = math.rotation({MC::radians(1e-7), {}, {}});
    for (int tick = 0; tick < 100000; ++tick) accumulated = accumulated * tiny;
    orthogonal(accumulated, 5e-11);
    const auto m = MC::matrix(accumulated);
    require(std::abs(m[2] - std::sin(0.01)) < 1e-12, "modern state discarded sub-word increments");
    auto pitchState = Matrix3<M>::identity(), rollState = Matrix3<M>::identity();
    const auto tinyAngle = MC::radians(1e-7);
    for (int tick = 0; tick < 30000; ++tick) {
        pitchState = pitchState * math.pitchDelta(tinyAngle);
        rollState = rollState * math.rollDelta(tinyAngle);
    }
    require(std::abs(MC::radians(math.recover(pitchState, false).angles.pitch) - 0.003) < 1e-13 &&
            std::abs(MC::radians(math.recover(rollState, false).angles.roll) + 0.003) < 1e-13,
            "modern axis state lost precision or reversed the flight delta convention");
    bool rejected = false;
    try { (void)MC::radians(std::numeric_limits<double>::infinity()); }
    catch (const std::domain_error &) { rejected = true; }
    require(rejected, "nonfinite boundary input accepted");
    /* Magnitude reads keep the sub-word fraction: a roll just inside the
     * 0x3000-word gate stays inside it, where signedAngle() would round out. */
    const auto justInside = MC::radians((0x3000 - 0.4) * (2 * pi / 65536));
    require(angleMagnitude(justInside) < 0x3000 &&
            static_cast<int16>(MC::angleWord(justInside)) == 0x3000,
            "modern magnitude lost the sub-word fraction");
    require(std::abs(angleMagnitude(MC::radians(-1.25)) - 1.25 * (32768.0 / pi)) < 1e-9,
            "modern magnitude units changed");
    require(angleMagnitudeCompat(MC::radians(-pi)) == 32768.0,
            "modern compat magnitude kept the DOS quirk");
    const auto diff = MC::angleWord(0x1234) - MC::angleWord(0x5678);
    require(std::abs(angleMagnitude(diff) - std::abs(static_cast<int16>(0x1234 - 0x5678))) < 0.5,
            "modern angle difference broke short-arc semantics");
}

void attitudeAndDeltas() {
    const RotationMath<F> fixed(g_angleLut);
    const RotationMath<M> modern;
    auto checkRecovery = [&](const rotation_reference::Matrix &matrix, bool previousRoll) {
        const auto reference = rotation_reference::recover(matrix, previousRoll, g_angleLut);
        const auto typed = FC::matrixWords(matrix.data());
        const auto recovered = fixed.recover(typed, previousRoll);
        const bool matches = FC::angleWord(recovered.angles.yaw) == static_cast<uint16>(reference.yaw) &&
                FC::angleWord(recovered.angles.pitch) == static_cast<uint16>(reference.pitch) &&
                FC::angleWord(recovered.angles.roll) == static_cast<uint16>(reference.roll) &&
                recovered.needsRefresh == reference.dirty;
        if (!matches) {
            std::fprintf(stderr, "recovery expected %d,%d,%d,%d got %u,%u,%u,%d; matrix:",
                reference.yaw, reference.pitch, reference.roll, reference.dirty,
                FC::angleWord(recovered.angles.yaw), FC::angleWord(recovered.angles.pitch),
                FC::angleWord(recovered.angles.roll), recovered.needsRefresh);
            for (auto value : matrix) std::fprintf(stderr, " %d", value);
            std::fputc('\n', stderr);
        }
        require(matches, "fixed recovery differs from frozen reference");
        g_orientMatrix = typed;
        g_orientationDirty = 0;
        g_rollWasNonzero = previousRoll;
        computeAttitudeAngles();
        require(signedAngle(g_ourHead) == reference.yaw && signedAngle(g_ourPitch) == reference.pitch &&
                signedAngle(g_ourRoll) == reference.roll && bool(g_orientationDirty) == reference.dirty,
                "production recovery differs from frozen reference");
    };
    for (int raw = 0; raw < 65536; ++raw) {
        const int16 s = rotation_reference::sine(raw, g_angleLut);
        const int16 c = rotation_reference::sine(raw + 16384, g_angleLut);
        const int16 minus = rotation_reference::word(-s);
        const auto angle = FC::angleWord(raw);
        const rotation_reference::Matrix yaw{c, 0, s, 0, 32767, 0, minus, 0, c};
        const rotation_reference::Matrix pitch{32767, 0, 0, 0, c, minus, 0, s, c};
        const rotation_reference::Matrix roll{c, s, 0, minus, c, 0, 0, 0, 32767};
        require(words(fixed.yawDelta(angle)) == yaw && words(fixed.pitchDelta(angle)) == pitch &&
                words(fixed.rollDelta(angle)) == roll, "fixed axis delta changed");
        checkRecovery(rotation_reference::rotation(raw, raw * 3, raw * 7, g_angleLut), raw % 2);
        auto matrix = yaw;
        matrix[5] = rotation_reference::word(raw);
        checkRecovery(matrix, false); // Exhaust the pitch recovery input, including both endpoints.
    }
    for (double pitch : {-1.5707963267948966, 1.5707963267948966}) {
        const auto matrix = modern.rotation({MC::radians(0.7), MC::radians(pitch), MC::radians(-0.3)});
        const auto rebuilt = modern.rotation(modern.recover(matrix, true).angles);
        for (int j = 0; j < 9; ++j)
            require(std::abs(MC::matrix(matrix)[j] - MC::matrix(rebuilt)[j]) < 1e-14,
                    "modern vertical attitude convention");
    }
    // Persistent state and the actual flight caller are checked tick by tick.
    auto expected = rotation_reference::rotation(1200, 400, 800, g_angleLut);
    g_orientMatrix = FC::matrixWords(expected.data());
    g_rotationCounter = 0;
    for (int tick = 0; tick < 4096; ++tick) {
        const auto delta = fixed.rollDelta(FC::angleWord(tick % 129 - 64));
        expected = rotation_reference::multiply(expected, words(delta));
        applyRotationDelta(g_orientMatrix, delta);
        require(words(g_orientMatrix) == expected, "persistent fixed orientation drifted from baseline");
        checkRecovery(expected, false);
    }
    int rotations = 0;
    bool dirty = false;
    g_rotationCounter = 0;
    g_orientationDirty = 0;
    for (int tick = 0; tick < 8192; ++tick) {
        const auto roll = fixed.rollDelta(FC::angleWord(31));
        const auto pitch = fixed.pitchDelta(FC::angleWord(-17));
        const auto yaw = fixed.yawDelta(FC::angleWord(23));
        expected = rotation_reference::multiply(expected, words(roll));
        applyRotationDelta(g_orientMatrix, roll);
        if (!(++rotations % 8)) dirty = true;
        expected = rotation_reference::multiply(expected, words(pitch));
        applyRotationDelta(g_orientMatrix, pitch);
        if (!(++rotations % 8)) dirty = true;
        expected = rotation_reference::multiply(words(yaw), expected);
        applyRotationDelta(yaw, g_orientMatrix);
        if (!(++rotations % 8)) dirty = true;
        const auto attitude = rotation_reference::recover(expected, false, g_angleLut);
        dirty = dirty || attitude.dirty;
        computeAttitudeAngles();
        require(g_rotationCounter == rotations && bool(g_orientationDirty) == dirty &&
                signedAngle(g_ourHead) == attitude.yaw && signedAngle(g_ourPitch) == attitude.pitch && signedAngle(g_ourRoll) == attitude.roll,
                "fixed update/recovery refresh schedule changed");
        if (dirty) {
            expected = rotation_reference::rotation(attitude.yaw, attitude.pitch, attitude.roll, g_angleLut);
            rebuildOrientation();
            rotations = 0;
            dirty = false;
        }
        require(words(g_orientMatrix) == expected, "fixed complete orientation sequence changed");
    }
}

void typedEulerState() {
    using Pose = PoseInterpolation<F>;
    for (int word = 0; word < 65536; ++word) {
        const auto a = FC::angleWord(word);
        require(signedAngle(a) == rotation_reference::word(word), "angle signed boundary changed");
        const auto advanced = a + Angle<F>::quarterTurn();
        require(FC::angleWord(advanced) == static_cast<uint16>(word + 16384), "typed angle wrap changed");
        for (int delta : {-32768, -16385, -16384, -16383, -257, -1, 0, 1, 257, 16383, 16384, 32767}) {
            const auto b = FC::angleWord(word + delta);
            const auto actual = Pose::interpolate({a, a, a}, {b, b, b}, FrameFraction::fromTicks(1, 3));
            const auto expected = static_cast<uint16>((delta <= -16384 || delta >= 16384) ?
                                                     word + delta : word + delta / 3);
            require(FC::angleWord(actual.yaw) == expected && FC::angleWord(actual.pitch) == expected &&
                    FC::angleWord(actual.roll) == expected, "fixed pose interpolation changed");
        }
    }
    const auto zero = angles<F>(0, 0, 0), flip = angles<F>(100, 0x4000, 50);
    const auto snapped = Pose::interpolate(zero, flip, FrameFraction::fromTicks(1, 2));
    require(snapped.yaw == flip.yaw && snapped.pitch == flip.pitch && snapped.roll == flip.roll,
            "gimbal snap failed to keep Euler triple coherent");

    auto roll = FC::angleWord(0x8000), pitch = FC::angleWord(0xffff);
    const int changed = f15::math::legacy::updateAttitudeFromWords(roll, pitch,
        [](int16 *r, int16 *p) {
            require(*r == -32768 && *p == -1, "platform attitude input changed");
            *r = 123; *p = -456;
            return 1;
        });
    require(changed == 1 && signedAngle(roll) == 123 && signedAngle(pitch) == -456,
            "platform attitude output changed");
    require(f15::math::legacy::updateAttitudeFromWords(roll, pitch, [](int16 *, int16 *) { return 0; }) == 0 &&
            signedAngle(roll) == 123 && signedAngle(pitch) == -456, "inactive platform adapter changed state");

    using ModernPose = PoseInterpolation<M>;
    const auto tiny = MC::radians(1e-9);
    const auto interpolated = ModernPose::interpolate({}, {tiny, tiny, tiny}, FrameFraction::fromTicks(1, 3));
    require(std::abs(MC::radians(interpolated.pitch) - 1e-9 / 3) < 1e-23,
            "modern interpolation quantized fractional angles");
    require(Angle<M>::halfTurn() == -Angle<M>::halfTurn(), "modern angle has inconsistent half-turn representation");
    const auto wrapA = MC::radians(3.14159265358979323846 - 0.01);
    const auto wrapB = MC::radians(-3.14159265358979323846 + 0.01);
    const auto middle = ModernPose::interpolate({wrapA, {}, {}}, {wrapB, {}, {}}, FrameFraction::fromTicks(1, 2));
    require(std::abs(std::abs(MC::radians(middle.yaw)) - 3.14159265358979323846) < 1e-14,
            "modern pose did not use shortest wraparound arc");
    for (const auto interval : {std::pair<int64, int64>{0, 0}, {-1, 10}, {11, 10},
                                {std::numeric_limits<int64>::max(), std::numeric_limits<int64>::max()}}) {
        bool rejected = false;
        try { (void)FrameFraction::fromTicks(interval.first, interval.second); }
        catch (const std::domain_error &) { rejected = true; }
        require(rejected, "invalid interpolation interval accepted");
    }
}

void attitudeShadow() {
    using f15::math::legacy::wordRep;
    using f15::math::legacy::uwordRep;
    using f15::math::legacy::wordClamp;
    using f15::math::legacy::attitudeStep;
    using f15::math::legacy::wordProductQ14;
    using f15::math::legacy::objectAttitudeSet;
    using f15::math::legacy::objectAttitudeAdvance;
    /* shiftedDown/dividedBy reproduce the int16 word ops exactly under fixed:
     * >> is the arithmetic (floor) shift, / truncates toward zero. */
    for (const int w : {0, 1, -1, 0x4000, -0x4000, -32768, 32767, 0x1234, -0x1234, -7, 7}) {
        const auto a = FC::angleWord(static_cast<std::uint16_t>(static_cast<std::int16_t>(w)));
        require(FC::angleWord(a.shiftedDown(3)) ==
                static_cast<std::uint16_t>(static_cast<std::int16_t>(
                    static_cast<std::int16_t>(w) >> 3)), "fixed shiftedDown diverged from int16 >>");
        require(FC::angleWord(a.dividedBy(2)) ==
                static_cast<std::uint16_t>(static_cast<std::int16_t>(
                    static_cast<std::int16_t>(w) / 2)), "fixed dividedBy diverged from int16 /");
        require(FC::angleWord(a.shiftedDown(0)) == static_cast<std::uint16_t>(static_cast<std::int16_t>(w)),
                "fixed shiftedDown(0) changed the word");
    }
    /* Modern keeps the fraction through the same named ops. */
    {
        const auto a = MC::angleWord(0x4001);
        const double expected = MC::radians(a) / 8;
        require(std::abs(MC::radians(a.shiftedDown(3)) - expected) < 1e-18,
                "modern shiftedDown quantized the fraction");
        require(std::abs(MC::radians(a.dividedBy(2)) - MC::radians(a) / 2) < 1e-18,
                "modern dividedBy quantized the fraction");
    }
    /* wordRep: signed word-domain view — int16 under fixed, fractional words
     * under modern (no arc wrap: a raw-domain rep for control-signal diffs). */
    for (const int w : {0, 1, -1, -32768, 32767, 0x4000}) {
        require(wordRep<F>(FC::angleWord(static_cast<std::uint16_t>(static_cast<std::int16_t>(w)))) == w,
                "fixed wordRep diverged from the int16 word");
    }
    require(std::abs(wordRep<M>(MC::radians(0.3)) - 0.3 * 65536 / (2 * 3.14159265358979323846)) < 1e-6,
            "modern wordRep lost the fraction");
    /* uwordRep: the (uint16) cast domain — negative words wrap high. */
    require(uwordRep<F>(FC::angleWord(0xffff)) == 0xffff, "fixed uwordRep diverged from (uint16)");
    {
        const double w = uwordRep<M>(MC::radians(-0.5 * 2 * 3.14159265358979323846 / 65536));
        require(std::abs(w - 65535.5) < 1e-6, "modern uwordRep did not wrap negative words high");
    }
    /* wordClamp: clampRange semantics including the <= -0x4000 -> max quirk. */
    require(wordClamp(0x500, -0x400, 0x400) == 0x400, "wordClamp upper bound changed");
    require(wordClamp(-0x500, -0x400, 0x400) == -0x400, "wordClamp lower bound changed");
    require(wordClamp(-0x4000, -0x400, 0x400) == 0x400, "wordClamp lost the -0x4000 wrap quirk");
    require(wordClamp(0x300, -0x400, 0x400) == 0x300, "wordClamp changed an in-range value");
    require(std::abs(wordClamp(300.4, -0x400, 0x400) - 300.4) < 1e-12,
            "wordClamp quantized a fractional in-range value");
    require(std::abs(wordClamp(1100.9, -0x400, 0x400) - 0x400) < 1e-12,
            "wordClamp missed a fractional upper bound");
    /* attitudeStep: words / divisor as an Angle — fixed truncates like the
     * int16 divide; modern keeps the quotient fractional. */
    require(FC::angleWord(attitudeStep<F>(-104, 15)) ==
            static_cast<std::uint16_t>(static_cast<std::int16_t>(-104 / 15)),
            "fixed attitudeStep diverged from int / int");
    require(FC::angleWord(attitudeStep<F>(0x2000 * 7, 15)) ==
            static_cast<std::uint16_t>(static_cast<std::int16_t>(0x2000 * 7 / 15)),
            "fixed attitudeStep mishandled a product wider than int16");
    require(std::abs(MC::radians(attitudeStep<M>(104.0, 15)) - 104.0 / 15 * 2 * 3.14159265358979323846 / 65536) < 1e-18,
            "modern attitudeStep lost the fractional quotient");
    /* wordProductQ14: the (uint16 rep * speed) >> 14 moveAmt product. */
    for (const int uw : {0, 1, 0x8000, 0xffff, 0x4000}) {
        for (const int16 sp : {0, 1, -1, 100, -100, 32000}) {
            require(wordProductQ14<F>(static_cast<std::uint16_t>(uw), sp) ==
                    static_cast<int16>(static_cast<std::uint32_t>(uw) * static_cast<std::int32_t>(sp) >> 14),
                    "fixed wordProductQ14 diverged from the int32 shift product");
        }
    }
    require(wordProductQ14<M>(65535.5, 100) == static_cast<int16>(std::floor(65535.5 * 100 / 16384)),
            "modern wordProductQ14 lost the fraction");
    /* objectAttitudeSet/Advance: shadow stays authoritative, packed word
     * mirrors it; fixed wraps like the int16 store, modern keeps fractions. */
    {
        Angle<F> shadow;
        int16 packed = 0;
        objectAttitudeSet<F>(shadow, packed, FC::angleWord(0x1234));
        require(packed == 0x1234, "set did not mirror into the packed word");
        objectAttitudeAdvance<F>(shadow, packed, FC::angleWord(0x7fff));
        require(packed == static_cast<int16>(0x1234 + 0x7fff), "fixed advance did not wrap like int16 +=");
        objectAttitudeSet<F>(shadow, packed, Angle<F>{});
        require(packed == 0 && shadow == Angle<F>{}, "zero set failed");
    }
    {
        Angle<M> shadow;
        int16 packed = 0;
        const auto step = attitudeStep<M>(1.0, 3); /* ~0.333 words/tick */
        objectAttitudeAdvance<M>(shadow, packed, step);
        require(packed == 0, "modern packed word moved before a full word");
        objectAttitudeAdvance<M>(shadow, packed, step);
        objectAttitudeAdvance<M>(shadow, packed, step);
        require(packed == 1, "modern packed word did not round the accumulated rep");
        require(std::abs(wordRep<M>(shadow) - 1.0) < 1e-9, "modern shadow lost the fraction");
    }
}

void linearShadow() {
    using f15::math::legacy::objectLinearSet;
    using f15::math::legacy::objectLinearAdvance;
    using f15::math::legacy::wordLerp;
    using F = f15::math::FixedBackend;
    using M = f15::math::ModernBackend;
    /* objectLinearSet: fixed narrows like the int16 store, packed mirrors. */
    {
        std::int16_t shadow = 0, packed = 0;
        objectLinearSet<F>(shadow, packed, 140);
        require(shadow == 140 && packed == 140, "fixed linear set changed the word");
        objectLinearSet<F>(shadow, packed, 40000);
        require(shadow == static_cast<int16>(40000) && packed == static_cast<int16>(40000),
                "fixed linear set lost the int16 wrap");
    }
    /* objectLinearAdvance: fixed wraps like int16 += of the narrowed delta. */
    {
        std::int16_t shadow = 0, packed = 0;
        objectLinearAdvance<F>(shadow, packed, static_cast<int32>(0x8000));
        require(shadow == static_cast<int16>(0x8000) && packed == static_cast<int16>(0x8000),
                "fixed linear advance lost the int16 wrap");
        objectLinearAdvance<F>(shadow, packed, 5.9);
        require(shadow == static_cast<int16>(static_cast<int16>(0x8000) + 5),
                "fixed advance did not narrow the fractional delta");
    }
    /* Modern: fractional deltas accumulate in the shadow; packed mirrors
     * only once a full word accrues. */
    {
        double shadow = 0;
        std::int16_t packed = 0;
        objectLinearSet<M>(shadow, packed, 100);
        require(packed == 100, "modern linear set did not mirror");
        objectLinearAdvance<M>(shadow, packed, 0.4);
        require(packed == 100, "modern packed word moved before a full word");
        objectLinearAdvance<M>(shadow, packed, 0.4);
        objectLinearAdvance<M>(shadow, packed, 0.4);
        require(packed == 101, "modern packed word did not round the accumulation");
        require(std::abs(shadow - 101.2) < 1e-12, "modern linear shadow lost the fraction");
        /* A wide modern delta is not narrowed to int16. */
        objectLinearAdvance<M>(shadow, packed, 70000.5);
        require(std::abs(shadow - 70101.7) < 1e-6, "modern advance narrowed a wide delta");
    }
    /* wordLerp: truncating int64 lerp under fixed, fractional under modern. */
    for (const int a : {0, 100, -300, 32000, -32000}) {
        for (const int b : {0, 50, -100, 30000, -30000}) {
            for (const int num : {0, 1, 7}) {
                const int den = 8;
                const int16 oracle = static_cast<int16>(
                    a + static_cast<int32>(static_cast<int64>(b - a) * num / den));
                require(wordLerp<F>(static_cast<int16>(a), static_cast<int16>(b), num, den) == oracle,
                        "fixed wordLerp diverged from the int64 lerp");
            }
        }
    }
    require(std::abs(wordLerp<M>(0.0, 10.0, 1, 4) - 2.5) < 1e-12,
            "modern wordLerp lost the fraction");
    /* Projectile.alt flag aliasing: bit0 set/clear moves the packed word by
     * ±1 and folds into the shadow, like the original byte ops. */
    {
        std::int16_t shadow = 0, packed = 0;
        using f15::math::legacy::objectLinearFlag0;
        using f15::math::legacy::objectLinearInterpFlagged;
        using f15::math::legacy::objectLinearSetFlagged;
        objectLinearSet<F>(shadow, packed, 300);
        objectLinearFlag0<F>(shadow, packed, true);
        require(packed == 301 && shadow == 301, "fixed flag0 set diverged from alt |= 1");
        objectLinearFlag0<F>(shadow, packed, true);
        require(packed == 301 && shadow == 301, "fixed flag0 set was not idempotent");
        objectLinearFlag0<F>(shadow, packed, false);
        require(packed == 300 && shadow == 300, "fixed flag0 clear diverged from alt &= ~1");
        /* Advance keeps the flag's ±1 in the value, like alt += v did. */
        objectLinearFlag0<F>(shadow, packed, true);
        objectLinearAdvance<F>(shadow, packed, 5);
        require(packed == 306 && shadow == 306, "fixed advance after flag0 lost the flag contribution");
    }
    {
        double shadow = 0;
        std::int16_t packed = 0;
        using f15::math::legacy::objectLinearFlag0;
        using f15::math::legacy::objectLinearInterpFlagged;
        using f15::math::legacy::objectLinearSetFlagged;
        objectLinearSet<M>(shadow, packed, 100);
        objectLinearAdvance<M>(shadow, packed, 0.4);
        objectLinearFlag0<M>(shadow, packed, true);
        require(packed == 101 && (packed & 1), "modern flag0 did not set packed bit0");
        require(std::abs(shadow - 100.4 - 1) < 1e-9,
                "modern flag0 did not fold the flag into the shadow");
        /* Flagged interp: packed low bit forced from the next snap's flag. */
        objectLinearInterpFlagged<M>(shadow, packed, 0.0, 10.6, true, 1, 2);
        require((packed & 1) == 1 && packed == 5,
                "modern flagged interp lost the next-snap flag or rounded wrong");
        objectLinearInterpFlagged<M>(shadow, packed, 0.0, 10.6, false, 1, 2);
        require((packed & 1) == 0 && packed == 4,
                "modern flagged interp did not clear bit0");
        objectLinearSetFlagged<M>(shadow, packed, 42.0, true);
        require(packed == 43 && (packed & 1), "modern flagged set lost the flag bit");
    }
    /* Fixed flagged interp against the original ((lerp & ~1) | (pn & 1)). */
    {
        using f15::math::legacy::objectLinearInterpFlagged;
        std::int16_t shadow = 0, packed = 0;
        objectLinearInterpFlagged<F>(shadow, packed, (int16)300, (int16)500, true, 1, 2);
        const int16 oracle = (int16)(((int16)(300 + (int32)((int64)(500 - 300) * 1 / 2)) & ~1) | 1);
        require(packed == oracle && shadow == oracle, "fixed flagged interp diverged from the masked lerp");
    }
}
} // namespace

int main() {
    fixedAndCallers();
    modernPrecision();
    attitudeAndDeltas();
    typedEulerState();
    attitudeShadow();
    linearShadow();
    std::puts("typed rotation backends and production callers passed");
}

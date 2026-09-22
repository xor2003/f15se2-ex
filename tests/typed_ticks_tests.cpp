#include "math/ticks.hpp"
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <type_traits>

using f15::math::Ticks;
using f15::math::TickDuration;
using f15::math::SimRate;

/* Ticks is deliberately not templated: the sim clock is an int16 word under
 * both backends and its wrap/phase semantics are gameplay behavior. These
 * checks exercise the named operations against literal int16 oracles so the
 * class cannot drift from the original arithmetic. */

namespace {

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(1);
    }
}

/* The exact expressions the migrated call sites used to spell out. */
int oraclePhase(std::int16_t v, int period) { return v & (period - 1); }
bool oracleBit(std::int16_t v, int b) { return (v & (1 << b)) != 0; }
int oracleRing(std::int16_t v, int shift, int count) { return (v >> shift) & (count - 1); }
int oracleURing(std::int16_t v, int shift, int count) {
    return (((std::uint16_t)v) >> shift) & (count - 1);
}
int oracleMod(std::int16_t v, int n) { return v % n; }
unsigned oracleUMod(std::int16_t v, int n) { return ((std::uint16_t)v) % (unsigned)n; }

void testWordRoundTrip() {
    require(Ticks{}.word() == 0, "default Ticks is zero");
    require(Ticks::fromWord(1234).word() == 1234, "fromWord/word round-trips");
    require(Ticks::fromWord(-1).word() == -1, "fromWord keeps the -1 sentinel");
    require(Ticks::fromWord(-1).uword() == 0xFFFF, "uword zero-extends");
    require(Ticks{}.isZero() && !Ticks::fromWord(1).isZero(), "isZero");
    require(Ticks::fromWord(5) == Ticks::fromWord(5), "equality");
    require(Ticks::fromWord(5) != Ticks::fromWord(6), "inequality");
    require(Ticks::fromWord(-2) < Ticks::fromWord(3), "signed compare");
}

void testIncrementWrap() {
    Ticks t = Ticks::fromWord(32767);
    ++t;
    require(t.word() == -32768, "++ wraps int16 max -> min like the original");
    Ticks z = Ticks::fromWord(-1);
    z++;
    require(z.word() == 0, "postfix ++ wraps -1 -> 0");
}

void testOffsetWrap() {
    require(Ticks::fromWord(32767).offset(1).word() == -32768,
            "offset wraps across int16 max");
    require(Ticks::fromWord(-32768).offset(-1).word() == 32767,
            "offset wraps below int16 min");
    require(Ticks::fromWord(100).offset(-150).word() == -50,
            "negative offset for ring deltas");
}

void testPhases() {
    const std::int16_t cases[] = {0, 1, 7, 8, 0x7F, 0x80, -1, -32768, 32767, 0x1234, -0x1234};
    for (std::int16_t v : cases) {
        Ticks t = Ticks::fromWord(v);
        require(t.phase(2) == oraclePhase(v, 2), "phase(2)");
        require(t.phase(4) == oraclePhase(v, 4), "phase(4)");
        require(t.phase(8) == oraclePhase(v, 8), "phase(8)");
        require(t.phase(16) == oraclePhase(v, 16), "phase(16)");
        require(t.phase(128) == oraclePhase(v, 128), "phase(128)");
        for (int b = 0; b < 15; ++b)
            require(t.bit(b) == oracleBit(v, b), "bit()");
        require(t.ring(1, 8) == oracleRing(v, 1, 8), "ring(1,8)");
        require(t.ring(2, 4) == oracleRing(v, 2, 4), "ring(2,4)");
        require(t.ring(4, 8) == oracleRing(v, 4, 8), "ring(4,8)");
        require(t.ring(10, 8) == oracleRing(v, 10, 8), "ring(10,8)");
        require(t.uring(1, 8) == oracleURing(v, 1, 8), "uring(1,8)");
        require(t.shifted(1) == (v >> 1), "shifted keeps int16 arithmetic shift");
        require(t.shifted(8) == (v >> 8), "shifted(8)");
        require(t.mod(7) == oracleMod(v, 7), "mod keeps signed remainder");
        require(t.umod(7) == oracleUMod(v, 7), "umod keeps unsigned remainder");
        require(t.umod(64) == oracleUMod(v, 64), "umod(64)");
    }
    /* The (frameTick >> 8) & 8 idiom equals bit(11) — the rewrite used at the
     * roll-command sites must select the same bit. */
    for (std::int16_t v : cases)
        require(Ticks::fromWord(v).bit(11) == (((v >> 8) & 8) != 0),
                "bit(11) == (v >> 8) & 8");
}

void testDeadlineSemantics() {
    /* Sentinel pattern: disarmed deadline is -1, equality against the clock
     * fires the event, offset arms it relative to now. */
    Ticks deadline = Ticks::fromWord(-1);
    Ticks clock = Ticks::fromWord(100);
    require(deadline.word() == -1 && deadline != clock, "disarmed sentinel");
    deadline = clock.offset(20);
    require(deadline.word() == 120, "armed deadline = clock + delay");
    require(deadline == Ticks::fromWord(120), "deadline equality fires");
}

void testDurationSemantics() {
    /* Countdown: arm, decrement, expire. */
    TickDuration timer = TickDuration::fromWord(3);
    require(timer.isPositive() && !timer.isZero(), "armed countdown");
    --timer;
    require(timer.word() == 2, "prefix decrement");
    require(timer--.word() == 2 && timer.equals(1), "postfix returns old value");
    require(timer--.atMost(1) && timer.isZero(), "postfix atMost sees the old value");
    timer = TickDuration::fromWord(-1);
    require(timer.isNegative() && timer.atMost(0), "negative timer state");
    /* Elapsed: increment with int16 wrap, phase, shift. */
    TickDuration elapsed = TickDuration::fromWord(32767);
    ++elapsed;
    require(elapsed.word() == -32768, "elapsed ++ wraps int16");
    elapsed = TickDuration::fromWord(0x123);
    require(elapsed.phase(8) == 3 && elapsed.shifted(4) == (0x123 >> 4),
            "elapsed phase/shift");
    require(elapsed.ring(4, 8) == ((0x123 >> 4) & 7), "elapsed ring selector");
    /* Duration deltas and countdown-window elapsed. */
    require(TickDuration::fromWord(120).elapsedSince(TickDuration::fromWord(100)) == 20,
            "elapsedSince delta");
    require(TickDuration::fromWord(7).elapsedWithin(10) == 3,
            "elapsedWithin = total - remaining");
    /* Hit-effect decay: v -= sign(v). */
    timer = TickDuration::fromWord(-8);
    timer.stepTowardZero();
    require(timer.word() == -7, "stepTowardZero from negative");
    timer = TickDuration::fromWord(2);
    timer.stepTowardZero();
    timer.stepTowardZero();
    timer.stepTowardZero();
    require(timer.isZero(), "stepTowardZero settles at zero");
}

void testSimRate() {
    SimRate rate = SimRate::fromWord(15);
    /* perTick is the original v / scaling integer divide for int operands —
     * including truncation toward zero for negatives. */
    require(rate.perTick(300) == 20 && rate.perTick(-300) == -20,
            "perTick integer divide, both signs");
    require(rate.perTick(7) == 0, "perTick truncates sub-rate values");
    /* Double operands keep the fraction — the modern step-rep divide. */
    require(rate.perTick(10.5) == 0.7, "perTick preserves double fraction");
    /* scaled/shifted reproduce n * scaling and scaling << n. */
    require(rate.scaled(3) == 45 && rate.scaled(-4) == -60, "scaled both signs");
    require(rate.shifted(1) == 30 && rate.shifted(4) == 240, "shifted");
    require(rate.minus(1) == 14 && rate.word() == 15, "minus/word");
    require(rate.exceeds(14) && rate.atMost(15) && rate.equals(15) &&
                !rate.below(15) && rate.atLeast(15),
            "threshold compares");
    require(SimRate::fromWord(4) != rate, "rate equality");
}

void testNoPrimitiveConversion() {
    static_assert(!std::is_constructible_v<Ticks, int>,
                  "no implicit int construction");
    static_assert(!std::is_constructible_v<Ticks, std::int16_t>,
                  "no implicit int16 construction");
    static_assert(!std::is_convertible_v<Ticks, int>,
                  "no implicit conversion to int");
    static_assert(!std::is_convertible_v<Ticks, std::int16_t>,
                  "no implicit conversion to int16");
    static_assert(!std::is_assignable_v<Ticks &, int>,
                  "no implicit int assignment");
    static_assert(!std::is_constructible_v<TickDuration, int>,
                  "duration: no implicit int construction");
    static_assert(!std::is_convertible_v<TickDuration, int>,
                  "duration: no implicit conversion to int");
    static_assert(!std::is_assignable_v<TickDuration &, int>,
                  "duration: no implicit int assignment");
    static_assert(!std::is_same_v<Ticks, TickDuration>,
                  "instants and durations are distinct domains");
    static_assert(!std::is_constructible_v<SimRate, int>,
                  "rate: no implicit int construction");
    static_assert(!std::is_convertible_v<SimRate, int>,
                  "rate: no implicit conversion to int");
    static_assert(!std::is_assignable_v<SimRate &, int>,
                  "rate: no implicit int assignment");
}

} // namespace

int main() {
    testWordRoundTrip();
    testIncrementWrap();
    testOffsetWrap();
    testPhases();
    testDeadlineSemantics();
    testDurationSemantics();
    testSimRate();
    testNoPrimitiveConversion();
    std::cout << "typed_ticks_tests passed\n";
    return 0;
}

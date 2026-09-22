#ifndef F15_MATH_TICKS_HPP
#define F15_MATH_TICKS_HPP
#include <cstdint>
namespace f15::math {
/* Simulation clock tick. frameTick and deadline state are int16 words whose
 * wrap, low-bit phases and arithmetic shifts are gameplay semantics; Ticks
 * keeps those operations explicit so raw arithmetic cannot mix with typed
 * quantities or silently change the phase patterns. Time is integral — the
 * same semantics apply under both math backends, so it is not templated. */
class Ticks {
    std::int16_t value_{};
    explicit Ticks(std::int16_t v) : value_(v) {}
public:
    Ticks() = default;
    /* Boundary words: serialization, RNG seeding, packed fields, diagnostics. */
    static Ticks fromWord(std::int16_t v) { return Ticks(v); }
    std::int16_t word() const { return value_; }
    /* The (uint16) read some sites relied on — zero-extends to 0..65535. */
    std::uint16_t uword() const { return static_cast<std::uint16_t>(value_); }
    /* Defined int16 wrap — the original relied on int16 increment overflow. */
    Ticks &operator++() {
        value_ = static_cast<std::int16_t>(static_cast<std::uint16_t>(value_) + 1u);
        return *this;
    }
    Ticks operator++(int) { Ticks t = *this; ++*this; return t; }
    /* Tick offset with the same int16 wrap (deadline arms, ring deltas). */
    Ticks offset(int n) const {
        return Ticks(static_cast<std::int16_t>(static_cast<std::uint16_t>(value_) +
                                             static_cast<std::uint16_t>(n)));
    }
    friend bool operator==(Ticks a, Ticks b) { return a.value_ == b.value_; }
    friend bool operator!=(Ticks a, Ticks b) { return !(a == b); }
    friend bool operator<(Ticks a, Ticks b) { return a.value_ < b.value_; }
    /* value & (period - 1): the low-bit phase the original masked for. */
    int phase(int period) const { return value_ & (period - 1); }
    /* value & (1 << bit): single-bit phase tests. */
    bool bit(int b) const { return (value_ & (1 << b)) != 0; }
    /* (value >> shift) & (count - 1): rotating slot selectors. */
    int ring(int shift, int count) const { return (value_ >> shift) & (count - 1); }
    /* (uword >> shift) & (count - 1): selectors on the zero-extended view. */
    int uring(int shift, int count) const { return (uword() >> shift) & (count - 1); }
    /* int16 >> n: the original's arithmetic (sign-extending) shift. */
    int shifted(int n) const { return value_ >> n; }
    /* int16 % n / uword() % n with the original's sign semantics. */
    int mod(int n) const { return value_ % n; }
    unsigned umod(int n) const { return uword() % n; }
    /* Disarmed/sentinel tests for deadline-style state. */
    bool isZero() const { return value_ == 0; }
};

/* Tick-domain duration/count: countdown timers (arm, decrement, expire) and
 * elapsed counters (increment, phase, compare) measured in sim ticks. The
 * countdown/elapsed semantics differ from Ticks — a duration has no instant
 * phase meaning and an instant never counts down — so they are distinct
 * types and cannot mix without explicit word() reads. Same int16 rep under
 * both backends; wrap matches the original int16 fields. */
class TickDuration {
    std::int16_t value_{};
    explicit TickDuration(std::int16_t v) : value_(v) {}
public:
    TickDuration() = default;
    static TickDuration fromWord(std::int16_t v) { return TickDuration(v); }
    std::int16_t word() const { return value_; }

    bool isZero() const { return value_ == 0; }
    bool isPositive() const { return value_ > 0; }
    bool isNegative() const { return value_ < 0; }
    /* Threshold compares vs a raw count — the count is a literal boundary,
     * not a conversion. */
    bool equals(int n) const { return value_ == n; }
    bool below(int n) const { return value_ < n; }
    bool exceeds(int n) const { return value_ > n; }
    bool atMost(int n) const { return value_ <= n; }

    friend bool operator==(TickDuration a, TickDuration b) { return a.value_ == b.value_; }
    friend bool operator!=(TickDuration a, TickDuration b) { return !(a == b); }

    /* Elapsed counters — defined int16 wrap like the original fields. */
    TickDuration &operator++() {
        value_ = static_cast<std::int16_t>(static_cast<std::uint16_t>(value_) + 1u);
        return *this;
    }
    TickDuration operator++(int) { TickDuration t = *this; ++*this; return t; }
    /* Countdown timers. */
    TickDuration &operator--() {
        value_ = static_cast<std::int16_t>(static_cast<std::uint16_t>(value_) - 1u);
        return *this;
    }
    TickDuration operator--(int) { TickDuration t = *this; --*this; return t; }
    /* v -= sign(v): the hit-effect decay idiom. */
    void stepTowardZero() {
        if (value_ > 0) value_ = static_cast<std::int16_t>(value_ - 1);
        else if (value_ < 0) value_ = static_cast<std::int16_t>(value_ + 1);
    }

    /* Elapsed-tick phases and shifts (missionTick cadences). */
    int phase(int period) const { return value_ & (period - 1); }
    int shifted(int n) const { return value_ >> n; }
    /* (value >> shift) & (count - 1): rotating selectors on elapsed ticks. */
    int ring(int shift, int count) const { return (value_ >> shift) & (count - 1); }
    /* Duration arithmetic: word deltas between two counts. */
    int elapsedSince(TickDuration start) const { return value_ - start.value_; }
    /* total - remaining: elapsed inside a countdown window of `total` ticks. */
    int elapsedWithin(int total) const { return total - value_; }
};
}
#endif

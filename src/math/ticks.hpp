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
}
#endif

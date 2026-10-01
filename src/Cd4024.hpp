#pragma once
// The CD4024B, as its datasheet draws it (TI CD4024B, 7-stage ripple-carry binary
// counter/divider): the chip the Emiz Instruments CV2 clock divider is built on.
//
// "The counter is advanced one count on the negative-going transition of each clock
// pulse. ... A high level on the RESET line resets the counter to its zero state
// independent of the clock." The clock is ignored for as long as RESET is high.
// Outputs Q1..Q7 are the seven stages: Q1 = /2 ... Q7 = /128, each one bit of the
// count, so each is a 50 % square.
//
//   pin 1 CLOCK   pin 2 RESET   pin 12 Q1   11 Q2   9 Q3   6 Q4   5 Q5   4 Q6   3 Q7
//
// Not modelled: the ripple delay between stages (the stages change in sequence, one
// propagation delay apart; here they change together).
//
// No Rack dependency, so tests/ drives it bare.

#include <cstdint>

namespace cd4024 {

static const int kStages = 7;

struct Cd4024 {
    uint8_t count = 0;          // 0..127; bit n-1 is Qn
    bool clk = false;           // last clock level seen, for the falling edge

    /** The clock and RESET pins' new levels. Returns true when the clock fell and
        the counter advanced (false while RESET holds it, or on any other change). */
    bool process(bool clock, bool reset) {
        bool fell = clk && !clock;
        clk = clock;
        if (reset) { count = 0; return false; }
        if (!fell) return false;
        count = (uint8_t)((count + 1) & 0x7f);
        return true;
    }

    /** Qn, n = 1..7: /2 to /128. */
    bool q(int n) const { return (count >> (n - 1)) & 1; }
};

} // namespace cd4024

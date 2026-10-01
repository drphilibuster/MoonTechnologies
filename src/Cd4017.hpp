#pragma once
// The 74HC4017, as its datasheet draws it (Nexperia 74HC4017; 74HCT4017, rev. 8):
// a 5-stage Johnson decade counter with ten decoded outputs. The Baby8 uses it.
//
// "The counter is advanced by either a LOW-to-HIGH transition at CP0 while CP1 is
// LOW or a HIGH-to-LOW transition at CP1 while CP0 is HIGH. ... A HIGH on MR resets
// the counter to zero (Q0 = Q5-9 = HIGH; Q1 to Q9 = LOW) independent of the clock
// inputs." MR is level-sensitive and overrides the clock. Q5-9 is LOW while the
// counter is in states 5 to 9.
//
//   Johnson counter: five flip-flops, each taking the one before it, the first
//   taking the inverse of the last: 00000, 10000, 11000, 11100, 11110, 11111,
//   01111, 00111, 00011, 00001, and round. Each state decodes to one output.
//
//   pin  3 Q0  2 Q1  4 Q2  7 Q3  10 Q4  1 Q5  5 Q6  6 Q7  9 Q8  11 Q9
//   pin 12 Q5-9 (carry)   13 CP1   14 CP0   15 MR
//
// Not modelled: the automatic code correction (an illegal code returns to a proper
// count within 11 clocks), which only matters from a random power-up state, and
// the propagation delays.
//
// No Rack dependency, so tests/ drives it bare.

namespace cd4017 {

struct Cd4017 {
    bool ff[5] = { false, false, false, false, false };
    bool cp0 = false, cp1 = false;      // last levels seen, for edge detection

    /** What a clock input's new levels did: nothing, or the counter should
        advance. Separate from advance() so a caller that wants the count to move
        some other way (the module's down direction) can use the chip's own edge
        logic and apply its own step. */
    enum Action { NONE, ADVANCE, RESET };

    /** The function table. Levels in; what the chip does about it out. */
    Action sense(bool newCp0, bool newCp1, bool mr) {
        bool rise0 = newCp0 && !cp0, fall1 = !newCp1 && cp1;
        bool adv = (rise0 && !newCp1) || (fall1 && newCp0);
        cp0 = newCp0;
        cp1 = newCp1;
        if (mr) return RESET;
        return adv ? ADVANCE : NONE;
    }

    void reset() { for (int i = 0; i < 5; i++) ff[i] = false; }

    void advance() {
        bool next = !ff[4];
        for (int i = 4; i > 0; i--) ff[i] = ff[i - 1];
        ff[0] = next;
    }

    /** The decoded outputs: exactly one of Q0..Q9 is high. */
    bool q(int n) const {
        switch (n) {
            case 0: return !ff[0] && !ff[4];
            case 1: return ff[0] && !ff[1];
            case 2: return ff[1] && !ff[2];
            case 3: return ff[2] && !ff[3];
            case 4: return ff[3] && !ff[4];
            case 5: return ff[4] && ff[0];
            case 6: return !ff[0] && ff[1];
            case 7: return !ff[1] && ff[2];
            case 8: return !ff[2] && ff[3];
            case 9: return !ff[3] && ff[4];
        }
        return false;
    }

    /** Q5-9, the carry: LOW in states 5 to 9. */
    bool carry() const { return !ff[4]; }

    /** Which output is high, 0..9. */
    int index() const {
        for (int n = 0; n < 10; n++) if (q(n)) return n;
        return 0;      // an illegal code, which advance() from a legal one cannot make
    }

    /** Sets the flip-flops to a given state. The chip has no way to be told this;
        it exists for a caller that has its own idea of where the count is (the
        module's down direction, restoring a patch). */
    void setIndex(int n) {
        static const unsigned code[10] = { 0x00, 0x01, 0x03, 0x07, 0x0F, 0x1F, 0x1E, 0x1C, 0x18, 0x10 };
        n = n < 0 ? 0 : (n > 9 ? 9 : n);
        for (int i = 0; i < 5; i++) ff[i] = (code[n] >> i) & 1u;
    }
};

} // namespace cd4017

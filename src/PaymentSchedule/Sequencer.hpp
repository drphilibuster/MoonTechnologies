#pragma once
// Payment Schedule's counter and tap loop, wired from the chips that make them in
// the Modular in a Week Day 10 circuits, with no Rack dependency so tests/ can
// drive it. The module's .cpp keeps the jacks, the quantizer and the panel.
//
//  * The counter is Baby8's 74HC4017 (src/Cd4017.hpp). STEPS is the reset-line
//    jumper: the output Qn that is wired back to MR, so the counter is in states
//    0 to n-1 and the instant it reaches n it is reset. RESET is the MR pin itself,
//    a level that holds the counter at 0 and overrides the clock. RUN is the AND
//    gate in front of CP0 (the Start/Stop switch). The chip counts one way only;
//    DIR down is this module's addition, and is documented as one.
//
//  * The tap loop is the Tiny Dazzler / Magic Pulsewave 4031 recorder/looper
//    (src/Cd4031.hpp): MODE CONTROL and RECIRCULATE on ground, DATA IN fed by a
//    diode OR of the TAP input and Q, and CLEAR, a normally-closed switch in Q's
//    path. Its clock is the sequencer's clock, so the loop is 64 clocks long
//    whatever STEPS is: "clock speed sets pattern time and resolution".
//    A tap is only recorded if it is high when the clock rises, because that is
//    when DATA IN is read; and CLEAR does not empty the register, it stops Q being
//    fed back, so the old pattern drains over the next 64 clocks.
//
// The 4017 and the 4031 are separate chips on one clock. RESET restarts the steps
// and leaves the loop where it was; the loop does not follow DIR.

#include <cstdint>

#include "../Cd4017.hpp"
#include "../Cd4031.hpp"

namespace paysched {

static const int kSteps = 8;
static const int kLoopStages = cd4031::kStages;

struct Sequencer {
    cd4017::Cd4017 counter;
    cd4031::Cd4031 loop;
    bool running = true;
    bool armCycleStop = false;

    int step() const { return counter.index(); }

    /** The loop's output, the 4031's Q: high while the pattern has a hit under
        the current clock. */
    bool loopGate() const { return loop.q(); }

    void reset() {
        counter.reset();
        loop.clear();
        running = true;
        armCycleStop = false;
    }

    /** A trigger at CYCLE: run from step 1 and stop at the next wrap. */
    void startCycle() {
        running = true;
        armCycleStop = true;
        counter.reset();
    }

    /** One sample.
        clockHigh:  the clock input's level.
        resetHigh:  the MR pin's level.
        up:         the direction in force at this sample.
        n:          STEPS, 1..8.
        tapHigh:    the tap source's level, already gated by RECORD.
        clearHeld:  CLEAR is pressed, opening the loop.
        Returns true when the count wrapped, which is EOC. */
    bool process(bool clockHigh, bool resetHigh, bool up, int n, bool tapHigh, bool clearHeld) {
        // RUN is an AND in front of CP0 and, in this module, in front of the loop's
        // clock too, so a stopped sequencer holds both.
        bool clk = clockHigh && running;

        bool wrapped = false;
        cd4017::Cd4017::Action act = counter.sense(clk, false, resetHigh);
        if (act == cd4017::Cd4017::RESET) {
            counter.reset();
        }
        else if (act == cd4017::Cd4017::ADVANCE) {
            int prev = counter.index();
            if (up) {
                counter.advance();
                if (counter.index() == n) {        // Qn is wired to MR
                    counter.reset();
                    wrapped = true;
                }
            }
            else {
                counter.setIndex((prev - 1 + n) % n);
                wrapped = (prev == 0);
            }
        }
        if (counter.index() >= n)                   // STEPS turned down past the count
            counter.reset();

        // The loop: DATA IN is the diode OR of the tap and Q through the CLEAR
        // switch; both are read as the clock rises.
        bool dataIn = tapHigh || (loop.q() && !clearHeld);
        loop.setClock(clk, dataIn, false);

        if (wrapped && armCycleStop) {
            running = false;
            armCycleStop = false;
        }
        return wrapped;
    }

    /** A saved patch from before the loop was a register: eight slots, one per
        step, repeating every STEPS clocks. Tiled along the 64 stages so that the
        gate that was at step s comes out at step s. (n that does not divide 64
        cannot tile exactly; the pattern is cut at 64.) */
    void importSlots(const bool* slots, int n, int step) {
        uint64_t v = 0;
        for (int k = 0; k < kLoopStages; k++)
            if (slots[(step + k) % n])
                v |= (uint64_t) 1 << (kLoopStages - 1 - k);
        loop.stages = v;
    }
};

} // namespace paysched

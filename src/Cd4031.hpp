#pragma once
// The CD4031B, as its datasheet draws it (CD4031B Types, Harris / TI SCHS036B).
//
// "64 D-type, master-slave flip-flop stages and one stage which is a D-type
// master flip-flop only (referred to as a 1/2 stage)." The logic level at the
// first stage's input is shifted one stage at each POSITIVE-going clock
// transition. Two data inputs and a MODE CONTROL select between them: MODE LOW
// takes DATA IN 1, MODE HIGH takes RECIRCULATE (DATA IN 2). The chip does not
// recirculate by itself -- the second input is a pin, and connecting Q to it is
// the caller's job. There is no reset.
//
//   pin  1  RECIRCULATE (DATA IN 2)        pin 16  VDD
//   pin  2  CLOCK IN                       pin 15  DATA IN 1
//   pin  5  Q'  the 1/2 stage              pin 10  MODE CONTROL
//   pin  6  Q   stage 64                   pin  9  CLD, a delayed clock (not modelled)
//   pin  7  Q-bar                          pin  8  VSS
//
// Q' is Q a half clock later: it takes Q's data on the next negative-going
// transition. The static truth table has the 64-stage register holding its
// contents with the clock high or low, so nothing is lost when the clock stops.
//
// No Rack dependency, so tests/ drives it bare.

#include <cstdint>

namespace cd4031 {

static const int kStages = 64;

struct Cd4031 {
    uint64_t stages = 0;     // bit 0 is stage 1 (the input end); bit 63 is stage 64, Q
    bool half = false;       // the 1/2 stage, Q'
    bool modeControl = false;
    bool clock = false;      // the last clock level seen, for edge detection

    bool q() const { return (stages >> 63) & 1u; }       // pin 6
    bool qBar() const { return !q(); }                   // pin 7
    bool qHalf() const { return half; }                  // pin 5

    /** The inputs at the moment of a positive-going clock transition: DATA IN 1
        (pin 15) and RECIRCULATE (pin 1). */
    void clockRising(bool dataIn1, bool recirculate) {
        bool in = modeControl ? recirculate : dataIn1;
        stages = (stages << 1) | (in ? 1u : 0u);
    }

    /** The negative-going transition: the 1/2 stage takes stage 64's data. */
    void clockFalling() { half = q(); }

    /** Drives the clock pin with a level. Edges are found here, so a caller with
        a level (a gated clock, a Schmitt trigger's state) cannot get them wrong.
        Returns true if the register shifted. */
    bool setClock(bool high, bool dataIn1, bool recirculate) {
        bool shifted = false;
        if (high && !clock) { clockRising(dataIn1, recirculate); shifted = true; }
        else if (!high && clock) clockFalling();
        clock = high;
        return shifted;
    }

    void clear() { stages = 0; half = false; clock = false; }
};

} // namespace cd4031

#pragma once
// The CD4006B, as its datasheet draws it (CD4006BMS: SYC / Intersil / RCA).
//
// "Four separate shift register sections: two sections of four stages and two
// sections of five stages with an output tap at the fourth stage." Each section
// has its own data input; one clock is common to all; data move on the
// NEGATIVE-going transition of the clock. The five-stage sections are a four-stage
// run and one more stage, and bring out both.
//
//   pin  1  D1      section 1 input        pin 14  VDD
//   pin  2  D1+4'   section 1, stage 4, delayed half a clock
//   pin  3  CLOCK
//   pin  4  D2      section 2 input        pin 13  D1+4   section 1, stage 4
//   pin  5  D3      section 3 input        pin 12  D2+5   section 2, stage 5
//   pin  6  D4      section 4 input        pin 11  D2+4   section 2, stage 4
//   pin  7  VSS                            pin 10  D3+4   section 3, stage 4
//                                          pin  9  D4+5   section 4, stage 5
//                                          pin  8  D4+4   section 4, stage 4
//
// That is the whole of what comes out: six stage outputs and the delayed copy.
// A stage in the middle of a section has no pin, so a register built from one
// chip can only be tapped where the sections end.
//
// No Rack dependency, so tests/ drives it bare.

#include <cstdint>

namespace cd4006 {

static const int kSections = 4;
/** Stages in each section, in pin order D1..D4. */
static const int kSectionStages[kSections] = { 4, 5, 4, 5 };
/** Whether a section also brings out its fourth stage ahead of its last. Only the
    five-stage ones: in a four-stage section the fourth stage is the last. */
static const bool kHasFourthTap[kSections] = { false, true, false, true };

struct Cd4006 {
    bool s1[4] = {}, s2[5] = {}, s3[4] = {}, s4[5] = {};

    // The pin outputs, by the datasheet's names.
    bool pin13() const { return s1[3]; }    // D1+4
    bool pin11() const { return s2[3]; }    // D2+4
    bool pin12() const { return s2[4]; }    // D2+5
    bool pin10() const { return s3[3]; }    // D3+4
    bool pin8()  const { return s4[3]; }    // D4+4
    bool pin9()  const { return s4[4]; }    // D4+5

    /** One negative-going clock transition: every stage takes its predecessor's
        value and each section's first stage takes its own input (pins 1, 4, 5, 6).
        The inputs and every output are read before anything moves, which is how a
        register behaves; a caller wiring one section's output to another's input
        passes the outputs as they stand before the edge. */
    void clockFalling(bool d1, bool d2, bool d3, bool d4) {
        shift(s1, d1);
        shift(s2, d2);
        shift(s3, d3);
        shift(s4, d4);
    }

    void clear() {
        for (int i = 0; i < 4; i++) { s1[i] = false; s3[i] = false; }
        for (int i = 0; i < 5; i++) { s2[i] = false; s4[i] = false; }
    }

private:
    template <int N>
    static void shift(bool (&s)[N], bool in) {
        for (int i = N - 1; i > 0; i--) s[i] = s[i - 1];
        s[0] = in;
    }
};

} // namespace cd4006

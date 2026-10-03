#pragma once
// One TomTomTom voice as the circuit it is (Modular in a Week Day 9, `TomTomTom.pdf`, rev 1.1,
// "TomTomTom Twin-T Drum Module", Kristian Blasol / SourceryStudios, 2019-09-20), solved by
// `src/Mna.hpp` with the CD4069UB of `src/Cd4069.hpp`. Rack-free, so tests/ drives it bare.
//
//   Gate -- C1 1u -- X --R1 47k-- P --R3 100k-- GND       D1 1N4448 from GND (anode) to X
//   LM358 (+) = P, (-) = 0.119 V (R2 100k from VCC over R15 1k to GND); output = trigger node T
//   T carries C4 (to IN1) and C5 (to OUT1), both 0.1 uF, and R10 to ground (the "twin-T" C leg)
//   IN1 --R5-- MID --R7-- OUT1, C6 0.22 uF from MID to ground  (the "twin-T" R leg)
//   CD4069 inverter U4.a: IN1 -> OUT1.  OUT1 --R12 100k-- IN2;  inverter U4.b: IN2 -> OUT2,
//   with R16 100k and C7 0.01 uF from OUT2 back to IN2.  OUT2 is the jack.
//
// R5 = R7 and R10 are the three resistors the sheet says to change "as you'd like your sound":
// tom 1 47k / 3k9, tom 2 33k / 2k2, tom 3 68k / 6k8 (kBoards).
//
// WHAT THE DRAWING DOES, which is not what the caption implies: the LM358 drives T. Its output is
// the gate (a comparator against 0.119 V behind a 1 uF differentiator whose 147 ms decay outlasts
// every gate here), so T is the LM358's output pin: high for the length of the gate, low
// otherwise. R10 hangs from T. With an ideal op-amp output R10 would do nothing at all and the
// "twin-T" would be a plain RC ladder closed around the inverter, but the LM358 is not ideal at
// the bottom: TI SLOS068 Figure 5-47 (typical output sinking characteristics) shows that below
// about 25 uA of sink current the output is a RESISTOR to ground of roughly 8 kohm (10 mV at 1 uA),
// which then jumps to a 0.5 V flat as the sink transistor takes over (30 uA to ~10 mA). So at
// rest T is R10 in parallel with ~8 k -- R10 is the twin-T's third leg, as the caption says -- and
// bigger swings are clamped at 0.5 V by the sink. Figure 5-46 (sourcing) gives the high side:
// V+ - 1.1 V at light load, about 40 ohms. Both are modelled here (rLow, the 0.5 V clamp diode,
// vohDrop, roHigh), switched with the comparator state; the comparator's own output
// slews at 0.3 V/us.
//
// ASSUMED, because the drawing does not say: VCC = 12 V (the MiaW rail; the 4069's VDD and the
// LM358's V+), the 1N4448's SPICE parameters (Is 2.52 nA, n 1.752: the standard 1N4148 card),
// and that the gate is the module's own 0..10 V gate (Kickback's GATE outputs). The LM358 is
// a comparator with its datasheet's gain (100 V/mV typical) and slew rate (0.3 V/us). The sink
// resistance, its 0.5 V knee and the 40 ohm source are read off the datasheet's log-log plots
// (about +-30 %). The jack is unloaded, and the DC on it (the inverter's self-bias, about 6 V)
// is blocked by the caller -- the board has no output coupling capacitor.

#include "../Cd4069.hpp"
#include "../Mna.hpp"

#include <cmath>

namespace kickback {

struct TomBoard { double r, r10; };
static const TomBoard kTomBoards[3] = { { 47e3, 3.9e3 }, { 33e3, 2.2e3 }, { 68e3, 6.8e3 } };

struct TomParams {
    double vcc = 12.0;
    double c1 = 1e-6, r1 = 47e3, r3 = 100e3;
    double refTop = 100e3, refBot = 1e3;        // R2 and R15: the comparator's reference divider
    double c45 = 0.1e-6, c6 = 0.22e-6;
    double r12 = 100e3, r16 = 100e3, c7 = 0.01e-6;
    double d1Is = 2.52e-9, d1Nvt = 1.752 * mna::kVt;
    // LM358, TI SLOS068: AOL 100 V/mV typ, SR 0.3 V/us; output stage from Figures 5-46 / 5-47
    double aol = 1e5, slew = 0.3e6, vos = 0.0;
    double vohDrop = 1.1, roHigh = 40.0;        // sourcing: V+ - 1.1 V, 40 ohms
    double vol = 0.0, rLow = 8e3, sinkKnee = 0.5; // sinking: 8 kohm to ground, clamped at 0.5 V
    bool sinkClamp = true;                      // false: the 0.5 V clamp is left out (for the tests)
    cd4069::Params chip = cd4069::typical();
};

class TomCircuit {
public:
    enum Node { X, P, T, IN1, MID, OUT1, IN2, OUT2, kNodes };
    enum Fixed { GATE = 0, DRIVE = 1, VDD = 2, KNEE = 3, kFixed };

    mna::Circuit c;
    TomParams q;
    double h = 1.0 / 192000.0;
    double trig = 0.0;                          // the LM358's output voltage, slewed
    bool highState = false;
    double lastGate = 0.0;
    double rest[kNodes] = {};
    bool ok = true;

    TomCircuit() { build(); }
    explicit TomCircuit(const TomParams& p) : q(p) { build(); }

    /** The comparator's reference: the (-) input. */
    double reference() const { return q.vcc * q.refBot / (q.refTop + q.refBot); }
    double high() const { return q.vcc - q.vohDrop; }
    /** The clamp diode's far end, so the pin sits at `sinkKnee` while it conducts (~1 mA). */
    double kneeRef() const { return q.sinkClamp ? q.sinkKnee - 0.62 : q.vcc + 1.0; }

    /** The three resistors the sheet says to change: R5 = R7 = r, and R10. */
    void setResistors(double r, double r10) {
        c.setResistor(iR5, r); c.setResistor(iR7, r); c.setResistor(iR10, r10);
        rr = r; rr10 = r10;
    }

    /** Sets the step and finds the quiescent point. */
    void prepare(double stepSeconds) {
        h = stepSeconds;
        c.h = h;
        double tgt[kFixed] = { 0.0, q.vol, q.vcc, kneeRef() };
        c.setResistor(iRo, q.rLow);
        highState = false;
        ok = c.solveDc(tgt, kFixed);
        trig = q.vol;
        c.fixedV[GATE] = 0.0; c.fixedV[DRIVE] = q.vol; c.fixedV[VDD] = q.vcc; c.fixedV[KNEE] = kneeRef();
        for (int i = 0; i < kNodes; i++) rest[i] = c.v[i];
        // One settling step so the capacitors' trapezoidal memory is consistent.
        c.step();
    }

    /** One step of h seconds with the gate at `gate` volts. Returns Newton iterations. */
    int step(double gate) {
        c.fixedV[GATE] = gate;
        double want = q.vol + q.aol * (c.v[P] - reference() - q.vos);
        double hi = high();
        if (want > hi) want = hi;
        if (want < q.vol) want = q.vol;
        double dv = q.slew * h;
        if (want > trig + dv) trig += dv;
        else if (want < trig - dv) trig -= dv;
        else trig = want;
        c.fixedV[DRIVE] = trig;
        // The output stage is a different circuit in its two states: a 40 ohm follower to V+ - 1.1 V,
        // or a resistor to ground with a 0.5 V clamp. Changing resistance and clamp reference
        // when the slewed output crosses 1 V.
        bool hiNow = trig > 1.0;
        if (hiNow != highState) {
            highState = hiNow;
            c.setResistor(iRo, hiNow ? q.roHigh : q.rLow);
            c.fixedV[KNEE] = hiNow ? q.vcc + 1.0 : kneeRef();
        }
        // A step in the gate meets the clamp diode D1 with the capacitor's trapezoidal memory
        // pointing the wrong way, and the node rings back (X jumped from -0.9 V to +3.2 V). Take the
        // step as backward Euler: forget the capacitor currents and use g = C/h.
        bool edge = std::fabs(gate - lastGate) > 0.2;
        lastGate = gate;
        if (edge) {
            for (int i = 0; i < c.ne; i++) if (c.e[i].kind == mna::Circuit::CAP) c.e[i].iPrev = 0.0;
            c.h = 2.0 * h;
            int it = c.step();
            c.h = h;
            for (int i = 0; i < c.ne; i++) if (c.e[i].kind == mna::Circuit::CAP) c.e[i].iPrev = 0.0;
            return it;
        }
        return c.step();
    }

    double out() const { return c.v[OUT2]; }
    double restOut() const { return rest[OUT2]; }
    /** Largest departure of any node from rest, volts. */
    double disturbance() const {
        double w = 0.0;
        for (int i = 0; i < kNodes; i++) { double d = std::fabs(c.v[i] - rest[i]); if (d > w) w = d; }
        return w;
    }

    int iR5 = -1, iR7 = -1, iR10 = -1, iRo = -1;
    double rr = 47e3, rr10 = 3.9e3;

private:
    void build() {
        using namespace mna;
        c.n = kNodes;
        int gate = fixed(GATE), drive = fixed(DRIVE), vdd = fixed(VDD);
        c.addCapacitor(gate, X, q.c1);
        c.addDiode(GND, X, q.d1Is, q.d1Nvt);
        c.addResistor(X, P, q.r1);
        c.addResistor(P, GND, q.r3);
        iRo = c.addResistor(drive, T, q.rLow);
        c.addDiode(T, fixed(KNEE), 1e-14, mna::kVt);        // the sink transistor's 0.5 V knee
        c.addDiode(GND, T, 1e-14, mna::kVt);                // the output pin's substrate diode
        c.addCapacitor(IN1, T, q.c45);
        c.addCapacitor(OUT1, T, q.c45);
        iR10 = c.addResistor(T, GND, kTomBoards[0].r10);
        iR5 = c.addResistor(IN1, MID, kTomBoards[0].r);
        iR7 = c.addResistor(MID, OUT1, kTomBoards[0].r);
        c.addCapacitor(MID, GND, q.c6);
        cd4069::addInverter(c, IN1, OUT1, vdd, GND, q.chip);
        c.addResistor(OUT1, IN2, q.r12);
        c.addResistor(OUT2, IN2, q.r16);
        c.addCapacitor(OUT2, IN2, q.c7);
        cd4069::addInverter(c, IN2, OUT2, vdd, GND, q.chip);
        prepare(h);
    }
};

} // namespace kickback

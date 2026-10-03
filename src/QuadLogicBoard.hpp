#pragma once
// The Day 8 "MiaW Quad Logic Module" (Sourcery Studios, Kristian Blasol, 2019-09-11,
// MiawResearch/day8/Schematic_Quad Logic Module_Sheet_1_20190911091444.pdf + BOM), one
// OR channel at a time, as the circuit and not as a truth table.
//
// One channel is two comparator inputs, one OR gate and one follower:
//
//   Y_in -- 1k -- (+) TL074 (-) -- Vref          (+) also 100k to GND
//                    out -- 1N4448 -- NODE --+-- 4071 input
//                                            +-- 2k7 -- LED -- GND
//   4071 OR output -- (BC547C base)   collector +12 V
//                     emitter -- 1k -- Y -- 1k -- GND      (Y is the jack)
//                     emitter -- 2k7 -- LED -- GND
//
//   Vref = +12 V * 10k / (100k + 10k) = 1.0909 V, shared by all eight comparators.
//   Supplies: TL074 +-12 V, 4071 and the collectors +12 V, VSS = GND.
//
// What this gives, by construction (the tests check each against the parts):
//   * an input reads "true" when (+) = Vin * 100k/101k exceeds 1.0909 V, i.e. above
//     1.102 V at the jack; an unpatched input is pulled to 0 V by its 100k;
//   * the comparator's high level is NOT 12 V: it is the TL074's EMF less the 1N4448 and
//     the 2k7 + LED load, a few volts below the rail; the 4071 input sees about 9 V
//     (above its VIH min of 8.6 V at 12 V, which is why the board works) and 0 V low;
//   * the OR gate swings 0..12 V behind ~200 ohm into the BC547C base;
//   * the jack Y is the emitter divided by two (1k / 1k), roughly 5.6 V when true, 0 V
//     when false.
//
// ASSUMED, not on the schematic or datasheets (see docs/AuditLogic.md):
//   * the LED (Everlight 204-10SURD, red) conducts with its anode on the 2k7 side, which
//     the schematic's symbols are ambiguous about; Vf = 2.0 V at 20 mA, n = 2;
//   * the 4071's input clamp to VSS as the same small diode (it only matters for a node that
//     would otherwise float with the comparator low; a residual ~1.6 V left on the node by
//     the LED's knee after a falling edge is not carried -- the model is memoryless there,
//     and 1.6 V reads low in any case);
//   * 1N4448 as the 1N4148 SPICE set used elsewhere in this repo (Is 2.52 nA, n 1.752);
//   * BC547C as Is 7.6e-14, BF 520, BR 10, VAF 100 (the set Kickback's BC547 uses);
//   * the TL074 output stage and 4071 drive as derived in their headers;
//   * the follower's jack drives no load (Rack inputs are ideal).
//
// No Rack dependency, so tests/ drives it bare.

#include "Mna.hpp"
#include "Cd4071.hpp"
#include "Tl074Comparator.hpp"

#include <vector>

namespace quadlogic {

struct Board {
    // The board's passive parts (BOM).
    double rIn = 1e3, rPulldown = 100e3, rRefTop = 100e3, rRefBottom = 10e3;
    double rNode = 2700.0;                          // R4 etc.: 2k7 from NODE to its LED
    double rEmit = 1000.0, rJack = 1000.0, rLed = 2700.0;   // R40, R41, R42
    // Semiconductors.
    double dIs = 2.52e-9, dNVt = 1.752 * mna::kVt;                 // 1N4448 (as 1N4148)
    double ledNVt = 2.0 * mna::kVt, ledIs = 20e-3 / std::exp(2.0 / (2.0 * mna::kVt));
    double qIs = 7.6e-14, qBF = 520.0, qBR = 10.0, qVAF = 100.0;   // BC547C
    double cdIs = 2.52e-9, cdNVt = 1.752 * mna::kVt;               // 4071 input clamp to VSS (assumed)
    // Chips and rail.
    tl074::Comparator op;
    double vdd = 12.0;                               // 4071 and collectors; the +12 V rail

    static constexpr double kLutStep = 0.01;
    static constexpr int kLutN = 2401;               // EMF -12 .. +12 V

    double vref = 0.0;                               // the shared 1.09 V
    double yHigh = 0.0, yLow = 0.0;                  // the jack with the OR true / false
    double ledHighA = 0.0;                           // the Y LED's current with the OR true
    std::vector<double> lut;                         // comparator EMF -> 4071 input node

    Board() { prepare(); }

    /** Rebuild everything that depends on the parts above. Call after changing any. */
    void prepare() {
        vref = op.vcc * rRefBottom / (rRefTop + rRefBottom);
        buildNodeLut();
        solveFollower(true, yHigh, ledHighA);
        double dummy;
        solveFollower(false, yLow, dummy);
    }

    /** The comparator's (+) pin for a jack voltage; an unpatched jack is 0 V. */
    double plus(double vin, bool patched) const {
        return patched ? vin * rPulldown / (rIn + rPulldown) : 0.0;
    }

    /** The voltage at the 4071 input for a jack voltage. */
    double node(double vin, bool patched) const {
        return nodeFromEmf(op.emf(plus(vin, patched), vref));
    }

    /** One OR channel: the voltage at its Y jack. */
    double channel(double a, bool aPatched, double b, bool bPatched) const {
        bool hi = cd4071::or2(node(a, aPatched), node(b, bPatched), vdd);
        return hi ? yHigh : yLow;
    }

    /** The logic outcome alone. */
    bool orTrue(double a, bool aPatched, double b, bool bPatched) const {
        return cd4071::or2(node(a, aPatched), node(b, bPatched), vdd);
    }

    double nodeFromEmf(double e) const {
        double x = (e + 12.0) / kLutStep;
        if (x <= 0.0) return lut[0];
        if (x >= kLutN - 1) return lut[kLutN - 1];
        int i = (int)x;
        double f = x - i;
        return lut[i] + (lut[i + 1] - lut[i]) * f;
    }

private:
    void buildNodeLut() {
        using namespace mna;
        enum { OUT, NODE, LEDA, N };
        Circuit c;
        c.n = N;
        c.addResistor(fixed(0), OUT, op.rout);
        c.addDiode(OUT, NODE, dIs, dNVt);
        c.addResistor(NODE, LEDA, rNode);
        c.addDiode(LEDA, GND, ledIs, ledNVt);
        // With the comparator low the diode and the LED both block and NODE would float; in the
        // chip it sits on the 4071's input protection (a diode from VSS), which is what holds it
        // at about 0 V. Parameters: the same small-signal diode (assumed).
        c.addDiode(GND, NODE, cdIs, cdNVt);
        lut.assign(kLutN, 0.0);
        double target[1] = { -12.0 };
        c.solveDc(target, 1);
        for (int i = 0; i < kLutN; i++) {
            c.fixedV[0] = -12.0 + i * kLutStep;
            if (c.step(60) < 0) {                    // lost the thread: re-ramp from zero
                double t[1] = { c.fixedV[0] };
                c.solveDc(t, 1);
            }
            lut[i] = c.v[NODE];
        }
    }

    /** The follower with the 4071 output true or false: the jack voltage and the Y LED's current. */
    void solveFollower(bool high, double& yOut, double& ledA) const {
        using namespace mna;
        enum { B, E, Y, L, N };
        Circuit c;
        c.n = N;
        c.addResistor(fixed(1), B, cd4071::rout(vdd));
        c.addNpn(fixed(0), B, E, qIs, qBF, qBR, qVAF);
        c.addResistor(E, Y, rEmit);
        c.addResistor(Y, GND, rJack);
        c.addResistor(E, L, rLed);
        int led = c.addDiode(L, GND, ledIs, ledNVt);
        double target[2] = { vdd, cd4071::outputSource(high, vdd) };
        c.solveDc(target, 2);
        yOut = c.v[Y];
        ledA = c.diodeCurrent(led);
    }
};

} // namespace quadlogic

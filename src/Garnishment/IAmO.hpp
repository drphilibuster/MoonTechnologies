#pragma once
// "I AM O" (Quincas Synth DIY guy; Modular in a Week Day 2, `Schematic_I-AM-O`, drawn by
// Kristian Blasol, 2018-09-06) as a circuit: a 2N5457 JFET with its source grounded,
//
//   In --R1 100k-- gate            AM --R2 100-- drain --C1 100n-- Out
//
// The gate is driven through 100k from In; the AM signal reaches the drain through 100 ohms
// and leaves through the 100 nF. So the carrier is divided between R2 and the channel, and
// the channel is a resistance the gate voltage sets: about 1/(2 Idss / |Vto|) with the gate
// at zero, and open (no division) when the gate is pinched off. At any real signal level the
// channel also runs out of current, and the gate's two junctions conduct when the drain or
// the gate goes far enough negative, so none of it is a clean multiplier. It is solved
// (`src/Mna.hpp`), not shaped.
//
// The 2N5457's numbers, from the onsemi datasheet (2N5457-D): IDSS 1 to 5 mA (3 typical),
// VGS(off) -0.5 to -6 V, |Yfs| 3000 umho typical (which for a square-law channel means
// Vto = -2 * IDSS / Yfs = -2.0 V, so Beta = IDSS / Vto^2 = 0.75 mA/V^2), output admittance
// 10 umho typical at 15 V (lambda = 10u / 3m = 3.3e-3 per volt), gate leakage 1 nA at -15 V.
// ASSUMED: the typical part (a real one is anything in the min-to-max ranges: give or take
// 2 dB of the stage's depth), the output loaded by 100k, no gate capacitances (4.5 pF Ciss
// and 1.5 pF Crss do nothing at audio and would make the 100k gate resistor a stiff pole).
//
// No Rack dependency, so tests/ drives it bare.

#include "../Mna.hpp"

#include <cmath>

namespace garnishment {

struct IAmO {
    enum Node { G, D, OUT, kNodes };
    enum Fixed { VIN = 0, VAM, kFixed };

    static constexpr double kIdss = 3e-3, kVto = -2.0, kLambda = 10e-6 / 3e-3;
    static constexpr double kBeta = kIdss / (kVto * kVto);
    static constexpr double kGateIs = 1e-14;       // 1 nA at -15 V is leakage, not Is; this is the forward knee
    static constexpr double kNVt = 0.025852;
    static constexpr double kLoad = 100e3;

    mna::Circuit ckt;
    bool ok = true;
    int sub = 2;
    double callRate = 48000.0, inPrev = 0.0, amPrev = 0.0;
    bool started = false;
    int lastIters = 0;

    IAmO() {
        using namespace mna;
        ckt.n = kNodes;
        ckt.addResistor(fixed(VIN), G, 100e3);                    // R1
        ckt.addJfet(D, G, GND, kBeta, kVto, kLambda);             // Q1
        ckt.addDiode(G, GND, kGateIs, kNVt);                      // gate-source junction
        ckt.addDiode(G, D, kGateIs, kNVt);                        // gate-drain junction
        ckt.addResistor(fixed(VAM), D, 100.0);                    // R2
        ckt.addCapacitor(D, OUT, 100e-9);                         // C1
        ckt.addResistor(OUT, GND, kLoad);
    }

    void reset() { started = false; }

    void begin(double rate) {
        callRate = rate;
        sub = (int)std::ceil(96000.0 / rate);
        if (sub < 1) sub = 1;
        double tgt[kFixed] = { 0.0, 0.0 };
        ok = ckt.solveDc(tgt, kFixed);
        ckt.h = 1.0 / (rate * sub);
        inPrev = amPrev = 0.0;
        started = true;
    }

    /** `gateVolts` on In, `amVolts` on AM; returns the Out volts. */
    double process(double gateVolts, double amVolts, double rate) {
        if (!started || rate != callRate) begin(rate);
        double y = 0.0;
        for (int k = 1; k <= sub; k++) {
            double t = k / (double)sub;
            ckt.fixedV[VIN] = inPrev + (gateVolts - inPrev) * t;
            ckt.fixedV[VAM] = amPrev + (amVolts - amPrev) * t;
            lastIters = ckt.step();
            if (lastIters < 0) ok = false;
            y = ckt.v[OUT];
        }
        inPrev = gateVolts;
        amPrev = amVolts;
        if (!ok) { begin(rate); y = 0.0; }
        return y;
    }
};

} // namespace garnishment

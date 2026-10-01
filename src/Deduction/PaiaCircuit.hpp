#pragma once
// The PAiA 2720-3L as a circuit: the Modular in a Week Day 7 schematic
// (`Schematic_paia2720-3b_2021-03-30`, Kristian Blasol, 2021-03-19) solved node by node.
//
//   IN --R1 680k--+--B1 (Q1 base; R3 150k to the supply, R5 27k to ground)
//   Q1 BC549C: collector C (R4 6k8 to the supply, and Q2's base), emitter E1
//     (R6 2k2 to ground; C5 33u and the R11 trimmer to ground for AC gain)
//   Q2 BC549C emitter follower: R9 4k7, then C7 0.1u to OUT
//   twin-T from B1 to X, with X coupled to C through C4 0.1u:
//     R7 68k - M1 - R8 68k, C6 .22u from M1 to ground
//     C2 1n  - M2 - C3 1n,  D1 1N4148 from M2 to ground, fed from CONTROL by R10 220k
//   supply: 12 V through R2 1k, filtered by C1 100u
//
// The control current through R10 sets the diode's dynamic resistance, which moves the
// twin-T's tuning; the diode is also the thing that distorts the signal across it, so it is
// simply left in the circuit. Solved by `src/Mna.hpp`.
//
// ASSUMED, because the drawing does not say: the supply is 12 V; the BC549C's SPICE-level
// parameters are typical datasheet values (Is from Vbe = 0.58 V at 0.5 mA, hFE 520, VAF 100 V);
// the output is loaded by 100k; the R11 trimmer sits where it is told to (the schematic has
// no value for its setting); no junction capacitances.
//
// No Rack dependency, so tests/ drives it bare.

#include "../Mna.hpp"

#include <cmath>
#include <complex>

namespace deduction {

struct PaiaCircuit {
    // nodes
    enum Node { B1, C, E1, F, E2, OUT, M1, M2, X, VCCF, kNodes };
    // fixed sources
    enum Fixed { VIN = 0, VCTL, VCC, kFixed };

    static constexpr double kVcc = 12.0;
    static constexpr double kIs = 7.6e-14, kBF = 520.0, kBR = 10.0, kVAF = 100.0;
    static constexpr double kDiodeIs = 2.52e-9, kDiodeNVt = 1.752 * 0.025852;
    static constexpr double kLoad = 100e3;

    mna::Circuit ckt;
    int trimmer = -1;
    bool ok = true;
    int lastIters = 0;

    PaiaCircuit() { build(); }

    void build() {
        using namespace mna;
        ckt = Circuit();
        ckt.n = kNodes;
        const int in = fixed(VIN), ctl = fixed(VCTL), vcc = fixed(VCC);
        ckt.addResistor(in, B1, 680e3);                 // R1
        ckt.addResistor(vcc, VCCF, 1e3);                // R2
        ckt.addCapacitor(VCCF, GND, 100e-6);            // C1
        ckt.addResistor(VCCF, B1, 150e3);               // R3
        ckt.addResistor(B1, GND, 27e3);                 // R5
        ckt.addResistor(VCCF, C, 6.8e3);                // R4
        ckt.addNpn(C, B1, E1, kIs, kBF, kBR, kVAF);     // Q1
        ckt.addResistor(E1, GND, 2.2e3);                // R6
        ckt.addCapacitor(E1, F, 33e-6);                 // C5
        trimmer = ckt.addResistor(F, GND, 500.0);       // R11
        ckt.addNpn(VCCF, C, E2, kIs, kBF, kBR, kVAF);   // Q2
        ckt.addResistor(E2, GND, 4.7e3);                // R9
        ckt.addCapacitor(E2, OUT, 0.1e-6);              // C7
        ckt.addResistor(OUT, GND, kLoad);
        ckt.addResistor(B1, M1, 68e3);                  // R7
        ckt.addResistor(M1, X, 68e3);                   // R8
        ckt.addCapacitor(M1, GND, 0.22e-6);             // C6
        ckt.addCapacitor(B1, M2, 1e-9);                 // C2
        ckt.addCapacitor(M2, X, 1e-9);                  // C3
        ckt.addDiode(M2, GND, kDiodeIs, kDiodeNVt);     // D1
        ckt.addResistor(ctl, M2, 220e3);                // R10
        ckt.addCapacitor(X, C, 0.1e-6);                 // C4
    }

    /** Sets the R11 trimmer, ohms. */
    void setTrimmer(double ohms) { ckt.setResistor(trimmer, ohms < 1.0 ? 1.0 : ohms); }

    /** Finds the operating point for the given control voltage, and sets the step. */
    void start(double controlVolts, double stepSeconds) {
        double tgt[kFixed] = { 0.0, controlVolts, kVcc };
        ok = ckt.solveDc(tgt, kFixed);
        ckt.h = stepSeconds;
    }

    /** One step: input and control volts in, the output node's volts out. */
    double step(double vin, double vctl) {
        ckt.fixedV[VIN] = vin;
        ckt.fixedV[VCTL] = vctl;
        ckt.fixedV[VCC] = kVcc;
        lastIters = ckt.step();
        if (lastIters < 0) ok = false;
        return ckt.v[OUT];
    }
};

/** The module's CUTOFF is a frequency; the board's tuning is a control voltage. This is the
    table between them: for each control voltage, the frequency of the twin-T's peak, found
    by sweeping the circuit's own small-signal response (R11 at 100 ohms, a middling gain).
    Built once, on the UI thread. */
struct PaiaTuning {
    static const int N = 81;
    static constexpr double kMaxControl = 12.0;
    double volts[N], hz[N];

    PaiaTuning() {
        for (int i = 0; i < N; i++) {
            // 0 V, then log-spaced from 0.1 V (the diode's knee is near 0.3 V) to the maximum
            volts[i] = i == 0 ? 0.0 : 0.1 * std::pow(kMaxControl / 0.1, (i - 1) / (double)(N - 2));
            PaiaCircuit c;
            c.setTrimmer(100.0);
            c.start(volts[i], 1.0 / 96000.0);
            double best = -1e9, bestF = 500.0;
            const int M = 160;
            double g[M], f[M];
            for (int k = 0; k < M; k++) {
                f[k] = 100.0 * std::pow(200.0, k / (double)(M - 1));        // 100 Hz .. 20 kHz
                g[k] = std::abs(c.ckt.acGain(f[k], PaiaCircuit::VIN, PaiaCircuit::OUT));
                if (g[k] > best) { best = g[k]; bestF = f[k]; }
            }
            hz[i] = bestF;
        }
        for (int i = 1; i < N; i++) if (hz[i] < hz[i - 1]) hz[i] = hz[i - 1];   // monotone
    }

    static const PaiaTuning& get() {
        static const PaiaTuning t;
        return t;
    }

    /** The control voltage that puts the peak at `f` Hz (clamped to what the board reaches). */
    double controlFor(double f) const {
        if (f <= hz[0]) return volts[0];
        if (f >= hz[N - 1]) return volts[N - 1];
        for (int i = 1; i < N; i++) {
            if (f <= hz[i]) {
                if (hz[i] == hz[i - 1]) return volts[i];
                double t = std::log(f / hz[i - 1]) / std::log(hz[i] / hz[i - 1]);
                return volts[i - 1] + t * (volts[i] - volts[i - 1]);
            }
        }
        return volts[N - 1];
    }
};

/** The circuit as a voice: runs at least 96 kHz inside, however fast it is called, with the
    input interpolated between calls. Signals are 1.0 = 5 V. */
struct PaiaVoice {
    PaiaCircuit ckt;
    bool started = false;
    int sub = 2;
    double callRate = 48000.0;
    double vinPrev = 0.0, vcPrev = 0.0, trimPrev = -1.0;

    void reset() { started = false; }

    void begin(double vc, double rate) {
        callRate = rate;
        sub = (int)std::ceil(96000.0 / rate);
        if (sub < 1) sub = 1;
        ckt.setTrimmer(1000.0);
        ckt.start(vc, 1.0 / (rate * sub));
        vinPrev = 0.0;
        vcPrev = vc;
        trimPrev = -1.0;
        started = true;
    }

    /** `x` is the input (1.0 = 5 V), `fcHz` the cutoff, `res` 0..1 the R11 trimmer (1k down to
        1 ohm), `drive` 0..1 the input level (1/8 to 8). Returns the output (1.0 = 5 V). */
    double process(double x, double fcHz, double res, double drive, double rate) {
        double vc = PaiaTuning::get().controlFor(fcHz);
        if (!started || rate != callRate) begin(vc, rate);
        double trim = 1000.0 * std::pow(10.0, -3.0 * res);
        if (trim != trimPrev) { ckt.setTrimmer(trim); trimPrev = trim; }
        double vin = x * std::exp2((drive - 0.5) * 6.0) * 5.0;
        double y = 0.0;
        for (int k = 1; k <= sub; k++) {
            double t = k / (double)sub;
            y = ckt.step(vinPrev + (vin - vinPrev) * t, vcPrev + (vc - vcPrev) * t);
        }
        vinPrev = vin;
        vcPrev = vc;
        if (!ckt.ok) { begin(vc, rate); y = 0.0; }
        return y * 0.2;
    }
};

} // namespace deduction

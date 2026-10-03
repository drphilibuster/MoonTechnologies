#pragma once
// The reverse-avalanche VCO as the BOARD (Modular in a Week Day 1, `1.4 Schematic_ReverseAvalancheOscillator.pdf`,
// Kristian Blasol, 2018-08-13), parts and all, as the optional "Avalanche core: board" of Six Figures.
// The default core (Avalanche.hpp, `Strike`) asks for a frequency and works out a resistance; this one
// has no frequency in it at all. The frequency is whatever the circuit does.
//
// THE SCHEMATIC, read from the PDF (node names are this file's):
//
//   VCC --R1 1k-- A --(R2 10k pot, wiper tied to its far end: x * 10k) || (R3 LDR of the vactrol)-- N
//   N --C1 10 uF or C2 1 uF (SW1; its common pin goes to K)-- K,   K --LED1-- ground
//   Q1 BC337 emitter on N, collector on K, base cut: the emitter-base junction in reverse breakdown
//   N --R4 100k-- P2 ("Output from Osc circuit") --C3 1 uF-- POS of the TL072, which R7 1k holds to ground
//   TL072 non-inverting, R6 220k feedback, R5 1k to ground: gain 221, OUT = P3 ("Buffered, amplified output")
//   P1 CV --R8 330-- R9 100k pot (top; wiper = the divider's output; bottom to ground) -- wiper -- LED3 (optional)
//        -- LED2 (the vactrol's LED) -- ground
//
// So the charge goes VCC -> R1 -> (R2 || LDR) -> C -> LED1 -> ground and the capacitor C sits across the
// junction. The resistance it charges through is R1 + R2 || LDR; the time constant is that times C. The
// capacitor's voltage runs between the junction's release and strike voltages (Avalanche.hpp: 7.3 V and a
// strike drawn each cycle from the microplasma law around 8.2 V), N is that plus LED1's drop, P2 is N
// through R4 into C3 + R7, P3 is the TL072's x221 of what R7 sees.
//
// THE FREQUENCY LAW, derived from that circuit and nothing else. Between strikes the capacitor charges toward
// Vs = VCC - V_LED1 through R = R1 + R2 || LDR:  V(t) = Vs - (Vs - Vrel) exp(-t / RC).  A cycle ends when the
// junction strikes; the strike is a first-passage of the microplasma process, rate r(V) = r0 exp((V - 8.2 V) /
// e-fold) with r0 pinned by lcamtuf's measurement (Avalanche.hpp), so
//     P(no strike by V) = exp(-r0 RC G(V)),  G(V) = integral from lo to V of exp((u - 8.2) / ef) / (Vs - u) du.
// The median cycle is the one that strikes at V_m with r0 RC G(V_m) = ln 2, and lasts
//     T_median = RC ln((Vs - Vrel) / (Vs - V_m)),     f = 1 / T_median.
// V_m is a function of RC (Avalanche.hpp `StrikeTable::median`): the junction strikes later the faster
// the capacitor charges. For the older, fixed-threshold law (strike at 8.2 V) this is f = 1 / (RC ln(1/q))
// with q = 0.695, and it is up to ~16 % off at the board's fastest settings. The frequency ranges that
// follow (for R2 and the range switch, no CV) are printed by tests/SixFigures/test_board.cpp.
//
// Here the law is not evaluated: the capacitor is integrated and the hazard accumulated on the way, so the
// resistance (the pot, the vactrol's LDR with its memory, DRIFT) may change in the middle of a cycle and the
// strike time stays exact (the hazard is integrated along the real charge, the strike found to a fraction
// of a sample). tests/SixFigures/test_board.cpp checks the integrator against the closed form and against
// an independent stepped simulation with its own calibration.
//
// WHAT IS ASSUMED (see docs/SixFigures.md): VCC = 12 V, LED1 = 1.8 V and constant, the junction's numbers
// (a 2N2222's, Avalanche.hpp), the strike e-fold (20 mV), R2 linear and its clockwise end the low
// resistance (RATE = 1 is R2 = 0), the LDR the VTL5C3 (Vactrol.hpp; dark = 20 Mohm with no CV), LED3 the
// same kind of LED as LED2 when fitted, C1/C2 exactly 10 uF/1 uF, the TL072 an ideal part with the 3 MHz
// gain-bandwidth and a +-10.5 V swing, and no BC337 measured.
//
// No Rack dependency, so tests/ drives it bare.

#include "Avalanche.hpp"
#include "../Vactrol.hpp"

#include <cmath>
#include <cstdint>

namespace sixfigures {
namespace avalanche {
namespace board {

static const double kR1 = 1e3, kR2 = 10e3, kR8 = 330.0, kR9 = 100e3;   // the schematic's resistors
static const double kC1 = 10e-6, kC2 = 1e-6;                            // C1 (LFO side) and C2 (audio side)

/** The vactrol LED's forward voltage at `i` amps (its Shockley law, Vactrol.hpp). */
inline double ledVf(double i) {
    const double is = 0.020 / std::expm1(vactrol::kLedVf20 / vactrol::kLedNVt);
    return vactrol::kLedNVt * std::log1p(i / is) + i * vactrol::kLedRs;
}

/** The current through LED2 (and LED3 if fitted, taken to be the same LED) when the CV jack is at `vcv`
    and R9, the CV amount pot, is at `amount` (0 = wiper at ground, 1 = wiper at the top). R8 330 and R9
    are a divider: the wiper sees Vth = vcv Rb / (Rt + Rb) behind Rth = Rt || Rb, Rt = R8 + (1 - a) R9 and
    Rb = a R9. Zero for a CV at or below zero (the LEDs do not conduct backwards here). */
inline double cvLedCurrent(double vcv, double amount, bool led3) {
    if (!(vcv > 0.0) || !(amount > 0.0)) return 0.0;
    const double a = amount > 1.0 ? 1.0 : amount;
    const double rt = kR8 + (1.0 - a) * kR9, rb = a * kR9;
    const double vth = vcv * rb / (rt + rb), rth = rt * rb / (rt + rb);
    if (!led3) return vactrol::ledCurrent(vth, rth);
    // two LEDs in series: f(I) = I Rth + 2 Vf(I) - Vth is increasing, bisect
    double lo = 0.0, hi = vth / rth;
    if (!(hi > 0.0)) return 0.0;
    for (int k = 0; k < 80; k++) {
        const double m = 0.5 * (lo + hi);
        (m * rth + 2.0 * ledVf(m) < vth ? lo : hi) = m;
    }
    return 0.5 * (lo + hi);
}

/** The output the board mode hands the panel's OUT jack. */
enum Tap {
    TAP_SAW = 0,       // the module's own saw (+-1 here, the module scales it to +-5 V): the capacitor's charge, normalised
    TAP_TL072 = 1,     // P3, the TL072 x221 "buffered, amplified output", volts
    TAP_NODE = 2,      // P2, "Output from Osc circuit", volts (a DC level near 9.6 V and a ripple through R4)
    TAP_CAP = 3,       // N, the oscillator node itself (before R4), volts
    NUM_TAPS = 4,
};

/** One voice of the board. Feed `process()` the pot, the range, the CV, once per sample. */
struct Board {
    // The parts. Public, so the tests can build broken variants.
    StrikeTable tab = defaultTable();  // the cumulative hazard (median strike voltage); tab.ef, tab.r0 are the law
    double efold = kStrikeEfold;
    double r0 = defaultTable().r0;     // per second at the nominal 8.2 V and a 1 s time constant
    double vs = source();              // what the capacitor charges toward
    vactrol::Vactrol ldr;              // R3 and LED2
    Tl072Stage out;                    // R4, C3, R7, the TL072

    // State.
    double vc = kVRelease;             // the capacitor's voltage
    double lambda = 0.0;               // the hazard accumulated since the last reset
    double e = 1.0;                    // this cycle's unit-exponential draw
    double pendingVc = kVRelease;      // the previous sample's capacitor voltage, with its band-limiting
    double dt = 0.0;
    microplasma::Rng rng;
    // Read-outs.
    double rTotal = kR1;               // the resistance it is charging through right now
    double hz = 0.0;                   // the median frequency at that resistance (updated each cycle)
    double lastStrike = kVStrike;      // the last cycle's strike voltage
    double lastPeriod = 0.0;           // the last cycle's length, seconds (exact, not a whole number of samples)
    double tCycle = 0.0;               // time since the last strike
    bool wrapped = false;              // a strike happened in the last sample
    // cvLedCurrent cache
    double cvV = 1e30, cvA = 1e30, cvI = 0.0;
    bool cvLed3 = false;

    explicit Board(uint32_t seed = 0xB0A2D01u) : rng(seed) { e = -std::log(rng.uniform()); }

    void setRate(double fs) {
        dt = 1.0 / fs;
        ldr.configure(dt);
        ldr.settle(0.0);
        out.setRate(fs);
        vc = pendingVc = kVRelease;
        lambda = 0.0;
        tCycle = 0.0;
        hz = 0.0;
    }
    void reset() {
        vc = pendingVc = kVRelease;
        lambda = 0.0;
        tCycle = 0.0;
        e = -std::log(rng.uniform());
        ldr.settle(0.0);
        if (dt > 0.0) out.setRate(1.0 / dt);
        hz = 0.0;
    }

    /** Change the strike law's e-fold (a different junction; the tests' broken variants). */
    void setEfold(double ef) { tab = StrikeTable(ef); efold = tab.ef; r0 = tab.r0; }

    /** The median frequency for a time constant `rc` (the closed form above). */
    double medianHz(double rc) const { return 1.0 / timeTo(tab.median(rc), rc); }

    struct Out { double saw, p3, p2, node; };

    /** One sample. `rate01` is the RATE knob (1 = R2 at 0 ohms, the fast end), `lfo` the range switch
        (the 10 uF), `cv` the volts at the CV jack, `cvAmount` the R9 pot 0..1, `led3` whether the optional
        CV LED is fitted, `speed` a multiplier on the whole time constant's rate (DRIFT; 1 = none). */
    Out process(double rate01, bool lfo, double cv, double cvAmount, bool led3, double speed = 1.0) {
        if (cv != cvV || cvAmount != cvA || led3 != cvLed3) {
            cvV = cv; cvA = cvAmount; cvLed3 = led3;
            cvI = cvLedCurrent(cv, cvAmount, led3);
        }
        const double g = ldr.step(cvI);
        const double rl = 1.0 / g;
        const double r2 = kR2 * (1.0 - (rate01 < 0.0 ? 0.0 : rate01 > 1.0 ? 1.0 : rate01));
        const double rp = r2 > 0.0 ? r2 * rl / (r2 + rl) : 0.0;
        rTotal = kR1 + rp;
        const double rc = rTotal * (lfo ? kC1 : kC2) / speed;
        if (hz == 0.0) hz = medianHz(rc);

        const double hsub = rc / 200.0;
        double rem = dt, corrPost = 0.0, corrPre = 0.0;
        wrapped = false;
        for (int guard = 0; rem > 1e-15 && guard < 4096; guard++) {
            const double h = rem < hsub ? rem : hsub;
            const double v0 = vc, v1 = vs - (vs - v0) * std::exp(-h / rc);
            const double a0 = std::exp((v0 - kVStrike) / efold);
            const double y = (v1 - v0) / efold;
            const double growth = y < 1e-9 ? 1.0 : std::expm1(y) / y;
            const double dl = r0 * a0 * h * growth;            // integral of r(V(t)) dt, V linear over the substep
            if (lambda + dl >= e) {
                const double s = (v1 - v0) / h;
                double tau = (efold / s) * std::log1p((e - lambda) * s / (r0 * a0 * efold));
                if (!(tau > 0.0)) tau = 0.0;
                if (tau > h) tau = h;
                const double vk = vs - (vs - v0) * std::exp(-tau / rc);
                rem -= tau;
                tCycle += tau;
                lastPeriod = tCycle;
                tCycle = 0.0;
                const double u = rem / dt;                      // how far past the strike this sample's end is
                const double step = vk - kVRelease;
                corrPost += 0.5 * step * (1.0 - u) * (1.0 - u); // polyBLEP, the sample after the strike...
                corrPre -= 0.5 * step * u * u;                  // ...and the one before it
                lastStrike = vk;
                vc = kVRelease;
                lambda = 0.0;
                e = -std::log(rng.uniform());
                wrapped = true;
                hz = medianHz(rc);
            } else {
                vc = v1;
                lambda += dl;
                rem -= h;
                tCycle += h;
            }
        }
        // one sample of latency: the previous sample is complete once this one's strike is known
        const double vcOut = pendingVc + corrPre;
        pendingVc = vc + corrPost;
        Out o;
        o.saw = 2.0 * (vcOut - kVRelease) / (kVStrike - kVRelease) - 1.0;
        o.node = vcOut + kVLed;
        o.p3 = out.run(o.node);
        o.p2 = out.p2;
        return o;
    }
};

} // namespace board
} // namespace avalanche
} // namespace sixfigures

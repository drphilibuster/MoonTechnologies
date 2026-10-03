#pragma once
// A vactrol (LED + CdS photoresistor in one light-tight case) as the PerkinElmer / EG&G Vactec
// VTL5C3: the LED's current-voltage law, the cell's resistance against LED current, and the
// cell's response in time, memory included. Rack-free, so tests/ drives it bare.
//
// WHICH PART, AND WHY. The Modular-in-a-Week schematics (Day 2 `Schematic_Vactrol.pdf`,
// Day 9 `Percussive Noise Voice.pdf`) name no part: they draw "LDR Vactrol" and the second
// says "Vactrol 5-10k to 500k ohm". The VTL5C3 is the part the hobby calls "a vactrol" (it is
// the one the Buchla 292 lowpass gate uses, and the one Doepfer and the DIY shops sell as
// the stock axial), its on-resistance at a CV-through-330-ohm LED current (5-25 mA) is the
// 2-6 kohm the schematic's "5-10k" reads as, and its resistance at a few hundred microamps
// is the "500k". THIS IS AN INFERENCE: the course files do not say VTL5C3, and the Silonex
// NSL-32 (60-150 ohm on, a different and much faster cell) and a DIY LED + GL5528 would
// each give different numbers. To change part, edit the tables below.
//
// SOURCES (all read 2026-10-03).
//  * PerkinElmer Optoelectronics, "Photoconductive Cells and Analog Optoisolators
//    (Vactrols)" databook p.45-46, VTL5C3: 1 mA 30 kohm, 40 mA 1.5 kohm, dark >= 10 Mohm 10 s
//    after the LED goes out, R(0.5 mA) / R(5 mA) = 20, turn-on to 63 % in 2.5 ms, turn-off to
//    100 kohm in 35 ms max, LED 1.65 V typ at 20 mA, "a small light history memory".
//    https://www.qsl.net/wa1ion/vactrol/vactrol.pdf (a copy of those pages). The two curve
//    plots on page 46 (resistance against LED current for four light-adapt histories, and
//    resistance against time after the LED is applied or removed at 10 and 40 mA) were
//    DIGITISED BY EYE from that PDF and are what the tables here are fitted to.
//  * modularsynthesis.com/vactrols/vactrols.htm (measured samples: VTL5C3 26 kohm at 1 mA,
//    5.5 kohm at 5 mA, 3.3 kohm at 10 mA, 1.4 kohm at 40 mA; turn-off to 100 kohm 18 ms).
//  * Parker and D'Angelo, "A Digital Model of the Buchla Lowpass-Gate", DAFx-13: the vactrol's
//    fall is slower than its rise and the speed depends on the level (its own model is
//    declared "ad hoc"; it is not used here).
//
// THE MODEL. Three stages.
//  1. LED: Vf follows a Shockley diode (n*Vt 50.1 mV, 1 ohm bulk, 1.63 V at 20 mA, fitted to the
//     datasheet's input-characteristic plot), in series with whatever drives it (330 ohm).
//  2. Steady state: R(I) is read off the datasheet's curve (log-log through the anchors below,
//     the mean of the four adapt-history curves) and added in parallel with the dark
//     conductance. The conductance is then SPLIT into three pools: a slow one that can hold at
//     most C3 siemens, a medium one that can hold at most C2, and a fast one that takes the
//     rest (x*c/(x+c) each, so a dim cell lives in the slow pools and a bright one is mostly
//     fast). That split IS the memory: after a bright flash the fast pool is gone in a few
//     milliseconds, the medium pool in ~10 ms and the slow pool takes ~60 ms to let go,
//     and the slow pool's few microsiemens are what hold the resistance at 100 kohm-1 Mohm
//     for the last two thirds of the datasheet's decay curve at either 10 or 40 mA.
//  3. Time: each pool follows its target through two cascaded one-poles when rising
//     (1.45 ms then 1.23 ms; a plain exponential fits the turn-on curve badly, the cell's
//     response starts slowly) and, when falling, through the first one-pole then its own
//     decay constant (0.8, 6.3 and 61 ms). The numbers are a least-squares fit, in log
//     resistance, to 40 points read off the response-time plots plus two databook figures
//     (turn-on to 63 % in 2.5 ms, turn-off to 100 kohm in about 19 ms from 10 mA; the model
//     gives 2.9 and 19.1 ms); the residual is about 20-30 % rms. The 10 mA turn-on curve
//     from a 1 Mohm start is not reproduced (the model is 2-3x too quick in its first ms: the
//     plot's starting state is not known, so the cell's start there is a guess).
//
// NOT MODELLED (no VTL5C3 number exists for them): temperature (the databook says only "very
// low temperature coefficient"; the NSL-32SR2's 0.7 %/C is a different part), signal-level
// dependence of the cell (a CdS cell is linear at audio levels well under its 250 V rating),
// and part-to-part spread (Xvive and Vactec samples of the same number differ by 2-3x at 1 mA).
// A multi-second dark recovery tail is also absent: the datasheet only bounds it (R >= 10 Mohm
// at 10 s) and the fit needs none.

#include <cmath>

namespace vactrol {

// --- LED ------------------------------------------------------------------------------------
static const double kLedNVt = 0.0501;     // n * kT/q, volts
static const double kLedRs  = 1.0;        // bulk resistance, ohms
static const double kLedVf20 = 1.63;      // junction voltage at 20 mA (1.65 V terminal, datasheet typ.)
static const double kMaxLedCurrent = 0.040;   // absolute maximum rating, amps

/** The current into an LED fed from `vdrive` volts through `rSeries` ohms. Zero for a drive
    at or below zero (reverse breakdown at 3 V is not reached by the circuits here). */
inline double ledCurrent(double vdrive, double rSeries) {
    if (!(vdrive > 0.0)) return 0.0;
    const double is = 0.020 / std::expm1(kLedVf20 / kLedNVt);
    const double rt = rSeries + kLedRs;
    // f(Vj) = Vj + rt*Is*(exp(Vj/nVt) - 1) - vdrive is convex and increasing, so Newton from a
    // point where f > 0 comes down onto the root monotonically.
    double i0 = (vdrive - 1.0) / rt;
    double vj = (i0 > 1e-12) ? kLedNVt * std::log1p(i0 / is) : vdrive;
    if (vj > vdrive) vj = vdrive;
    for (int k = 0; k < 40; k++) {
        double e = is * std::expm1(vj / kLedNVt);
        double f = vj + rt * e - vdrive;
        double d = 1.0 + rt * (e + is) / kLedNVt;
        double step = f / d;
        vj -= step;
        if (std::fabs(step) < 1e-12) break;
    }
    double i = (vdrive - vj) / rt;
    return i > 0.0 ? i : 0.0;
}

// --- steady-state cell ----------------------------------------------------------------------
static const double kDarkConductance = 1.0 / 20e6;   // ASSUMED 20 Mohm; the databook says >= 10 Mohm

/** The anchors: LED current in mA against cell resistance in ohms (VTL5C3, mean of the four
    adapt-history curves of the databook's plot, read by eye; the spec points are 1 mA 30 kohm,
    40 mA 1.5 kohm and R(0.5 mA) / R(5 mA) = 20). */
static const int kAnchors = 9;
static const double kAnchorMa[kAnchors] = { 0.1, 0.2, 0.5, 1.0, 2.0, 5.0, 10.0, 20.0, 40.0 };
static const double kAnchorR[kAnchors]  = { 1.2e6, 520e3, 115e3, 46e3, 16e3, 5.5e3, 3.0e3, 1.7e3, 1.25e3 };

/** The illuminated conductance in siemens for an LED current in amps (steady state, adapted),
    dark conductance not included. Log-log interpolation; below 0.1 mA the first segment's
    slope continues (so the conductance runs to zero with the current). */
inline double steadyLight(double iLed) {
    if (!(iLed > 0.0)) return 0.0;
    const double ma = iLed * 1e3;
    int s;
    if (ma <= kAnchorMa[0]) s = 0;
    else if (ma >= kAnchorMa[kAnchors - 1]) s = kAnchors - 2;
    else { s = 0; while (ma > kAnchorMa[s + 1]) s++; }
    double slope = (std::log(kAnchorR[s + 1]) - std::log(kAnchorR[s])) /
                   (std::log(kAnchorMa[s + 1]) - std::log(kAnchorMa[s]));
    double lnR = std::log(kAnchorR[s]) + slope * (std::log(ma) - std::log(kAnchorMa[s]));
    double g = std::exp(-lnR);
    double gd = kDarkConductance;
    if (ma > kAnchorMa[0]) return g > gd ? g - gd : 0.0;
    // below the table: G ~ I^1.2, anchored to the table's first point
    double g0 = std::exp(-std::log(kAnchorR[0])) - gd;
    return (g0 > 0.0 ? g0 : 0.0) * std::pow(ma / kAnchorMa[0], -slope);
}

/** Steady-state cell resistance for an LED current, ohms (dark included). */
inline double steadyResistance(double iLed) {
    return 1.0 / (kDarkConductance + steadyLight(iLed));
}

// --- time response --------------------------------------------------------------------------
static const double kTauStage1 = 1.446e-3;               // first rising / falling stage, all pools
static const double kTauRise2  = 1.232e-3;               // second rising stage
static const double kTauFall[3] = { 0.802e-3, 6.283e-3, 61.39e-3 };   // fast, medium, slow pool
static const double kCapMedium = 149.7e-6;               // the most the medium pool can hold, siemens
static const double kCapSlow   = 4.771e-6;               // the most the slow pool can hold, siemens

/** One cell. Feed `step()` the LED current every sample, read back the conductance. The
    public constants are the fitted part; tests overwrite them to build broken variants. */
struct Vactrol {
    double tauRise1 = kTauStage1, tauRise2 = kTauRise2;
    double tauFall[3] = { kTauFall[0], kTauFall[1], kTauFall[2] };
    double capMedium = kCapMedium, capSlow = kCapSlow;

    double s[3] = {0, 0, 0};     // first stage per pool
    double g[3] = {0, 0, 0};     // the pools' conductance
    double aS = 0, aR = 0, aF[3] = {0, 0, 0};
    double dt = 0;

    static double soft(double x, double c) { return x > 0.0 ? x * c / (x + c) : 0.0; }

    /** Split a light conductance into the fast, medium and slow pools' targets. */
    void split(double gl, double out[3]) const {
        double slow = soft(gl, capSlow);
        double rest = gl - slow;
        double med = soft(rest, capMedium);
        out[2] = slow; out[1] = med; out[0] = rest - med;
    }

    /** Dark-adapted (every pool empty). */
    void reset() { for (int i = 0; i < 3; i++) s[i] = g[i] = 0.0; }

    /** Settled at `iLed` (a cell that has sat at that current for a long time). */
    void settle(double iLed) {
        double t[3]; split(steadyLight(iLed), t);
        for (int i = 0; i < 3; i++) s[i] = g[i] = t[i];
    }

    /** Recompute the per-sample coefficients (call after changing any constant above). */
    void configure(double sampleTime) {
        dt = sampleTime;
        aS = 1.0 - std::exp(-dt / tauRise1);
        aR = 1.0 - std::exp(-dt / tauRise2);
        for (int i = 0; i < 3; i++) aF[i] = 1.0 - std::exp(-dt / tauFall[i]);
    }
    void setSampleTime(double sampleTime) { if (sampleTime != dt) configure(sampleTime); }

    /** Advance one sample with LED current `iLed` (amps); returns the cell conductance (siemens). */
    double step(double iLed) {
        double t[3]; split(steadyLight(iLed), t);
        for (int i = 0; i < 3; i++) {
            s[i] += (t[i] - s[i]) * aS;
            g[i] += (s[i] - g[i]) * (s[i] > g[i] ? aR : aF[i]);
        }
        return conductance();
    }

    double conductance() const { return kDarkConductance + g[0] + g[1] + g[2]; }
    double resistance() const { return 1.0 / conductance(); }
};

}  // namespace vactrol

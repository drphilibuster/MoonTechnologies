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
// each give different numbers. The part is now a DESCRIPTOR (`Part`, below): the VTL5C3 stays the
// default and is bit-identical to what this header always did, and three more are selectable
// (see "THE OTHER PARTS" at the end of this comment). The descriptor carries the LED law, the dark
// conductance, the anchor table and the time constants; nothing else in the model is part-specific.
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
//
// THE OTHER PARTS (added 2026-10-03; each is a `Part`, picked with `part(PART_...)`).
// Every number below is either read off the cited sheet or listed as ASSUMED. Curve plots were
// digitised from the PDFs' vector/raster plots by locating the plotted line against the axis
// ticks (about 3 % in resistance), not read from a table.
//
//  PART_NSL32SR2  Silonex / Advanced Photonix NSL-32SR2.
//    * Advanced Photonix NSL-32SR2 datasheet REV 12-02-15, https://www.farnell.com/datasheets/2013774.pdf:
//      LED Vf <= 2.5 V at 20 mA, 25 mA max; R(on) <= 40 ohm at 20 mA, 140 ohm at 1 mA (typ);
//      R(off) 1 Mohm min, 5 Mohm typ, 10 s after the LED goes out; rise to 63 % of the final
//      conductance 5 ms typ (the sheet gives no current for it); decay to 100 kohm 5 ms typ
//      after 16 mA is removed; cell 0.7 %/C; "Resistance vs. Current" plot (log-log, 0.1-40 mA).
//    * Anchors: that plot, digitised: 1.1 kohm at 0.1 mA, 234 ohm at 0.5, 122 at 1, 48 at 5, 32 at
//      10, 24 at 20, 19 at 40 mA. (The plotted 1 mA value is 13 % under the table's 140 ohm typ:
//      the plot is used, it is the curve of one cell.)
//    * Time: ONE pool, no memory. The sheet gives two numbers (rise 5 ms, decay-to-100k 5 ms) and
//      no decay curve, so no slow tail can be fitted and none is invented. A single exponential
//      of 0.6 ms after a 0.1 ms first stage reproduces "100 kohm in 5 ms from 16 mA"; the rise is
//      the same first stage into a 4.9 ms second one (63 % in 5.0 ms at 5 mA; see the tests).
//    * ASSUMED: LED Vf 2.0 V at 20 mA with n*Vt 52 mV (the sheet bounds Vf only, <= 2.5 V), bulk
//      1 ohm, dark conductance 1/5 Mohm (the typ off-resistance), no memory tail.
//
//  PART_NSL32SR3  Silonex NSL-32SR3 (the older sheet: also what the surplus parts in circulation
//    are). Silonex Inc. NSL-32SR3 datasheet 104058 REV 3, https://logosfoundation.org/instrum_gwr/tinti/datasheets/Silonex_NSL-32SR2_60721.pdf
//    (the file is named SR2; its page is headed SR3): R(on) 60 ohm max at 20 mA, 150 ohm at 5 mA;
//    R(off) 25 Mohm min; rise 5 ms (63 %, 5 mA); decay 10 ms (to 100 kohm, 5 mA); Vf <= 2.5 V.
//    * Anchors: its plot, digitised: 2.05 kohm at 0.1 mA, 500 ohm at 0.5, 293 at 1, 115 at 5, 80 at
//      10, 61 at 20, 48 at 40 mA. It is about 2.4x the SR2's resistance at 1 mA, which is the sheets'
//      own disagreement and not a digitising error.
//    * Time: as the SR2 (one pool), with the decay constant refitted to "100 kohm in 10 ms from 5 mA".
//    * ASSUMED: LED as the SR2; dark conductance 1/25 Mohm (the sheet's minimum, no typ given).
//    * A measured surplus SR3 (modularsynthesis.com/vactrols/vactrols.htm, one sample, 5 mA drive)
//      turned on to 63 % of 640 ohm in 26 (us, the page's unit symbol is lost) and took 120 ms to
//      reach 100 kohm: twelve times the datasheet's 10 ms. THAT SAMPLE IS NOT THE MODEL: it is one aged
//      surplus part, the sheet is the only repeatable source, and the decay tail it shows is the
//      memory this model does not give the NSL parts. See "not modelled" below.
//
//  PART_GL5528  A DIY vactrol: a 5 mm green LED and a GL5528 CdS photoresistor in a heat-shrink
//    tube (what Mod Wiggler / Eurorack DIY builds use).
//    * GL55 Series CdS Photoresistor Manual (the GL5528 row: 10-20 kohm at 10 lux, dark >= 1 Mohm
//      ten seconds after 10 lux, gamma 0.6, rise 20 ms, decay 30 ms, 150 V, 100 mW, 540 nm peak),
//      https://passionelectronique.fr/wp-content/uploads/datasheet-photoresistance-LDR-GL5528-CdS.pdf ;
//      a second copy of the family sheet (Handsontec) gives 8-20 kohm, rise 45 ms / fall 55 ms
//      for the "GL55" family in general:
//      https://www.handsontec.com/dataspecs/sensor/GL55-LDR.pdf . The per-type table of the first
//      is used (it is the one that names the GL5528's own row).
//    * Resistance against illuminance: R = R10 * (E / 10 lux)^(-gamma), R10 = 14.1 kohm (the
//      geometric mean of the sheet's 10-20 kohm), gamma 0.6 (the sheet; its Fig. 3 band falls 16.7 ->
//      4.6 kohm from 10 to 100 lux at its lower edge, gamma 0.56). The sheet's curve is for a lamp.
//    * What a bare LED + LDR pair does is a MATTER OF GEOMETRY and the sheets say nothing about it. The one
//      anchor taken: ultra-bright green LEDs at ~10 mA took DIY vactrols down to ~400 ohm (a Mod
//      Wiggler thread, "Photoresistor and led type for diy vactrol", read through a search
//      summary; the page itself refused automated access). That fixes 378 lux per mA of LED
//      current at this coupling (E = 378 lux/mA * I, linear in current), and gives 6.4 kohm at 0.1 mA,
//      1.6 kohm at 1 mA, 400 ohm at 10 mA. Rich Holmes ("Vactrol information",
//      https://richardsholmes.com/topics/synth/vactrol-information/) notes the same thing in
//      words: even at 0.5 mA a green LED against the cell is far brighter than the 10 lux the
//      sheets test at, so the sheet's R(on) is not the vactrol's.
//    * Time: ONE pool. Rise 20 ms to 63 %, decay 30 ms (the sheet defines neither; the PerkinElmer
//      photocell convention that Holmes quotes -- 63 % on the way up, 37 % on the way down -- is
//      assumed). The first stage is 1 ms and the others are fitted so the 63 % and 37 % points land
//      on 20 and 30 ms (see the tests). A bare LDR is slower on the way up than a VTL5C3's selected
//      cell and the DIY threads report a decay that "rings" for 100 ms and a recovery to megohms
//      taking seconds; that tail is NOT modelled (see below).
//    * ASSUMED: the LED (5 mm green, Vf 2.1 V at 20 mA, n*Vt 52 mV, bulk 1 ohm, 30 mA limit), the
//      coupling constant above, a dark conductance of 1/2 Mohm (the sheet says >= 1 Mohm, forum
//      reports a recovery to ~2 Mohm), and every cell of the same type being the same cell (the
//      sheet allows 10-20 kohm; DIY builders report 2-3x between samples).
//
// NOT MODELLED for the three added parts: the light-history tail (the datasheets give no curve to
// fit one to, and the model would be guessing), temperature (the NSL-32's 0.7 %/C is quoted, not
// applied), sample-to-sample spread, and the LED's efficiency droop with current.

#include <cmath>

namespace vactrol {

// --- LED ------------------------------------------------------------------------------------
static const double kLedNVt = 0.0501;     // n * kT/q, volts
static const double kLedRs  = 1.0;        // bulk resistance, ohms
static const double kLedVf20 = 1.63;      // junction voltage at 20 mA (1.65 V terminal, datasheet typ.)
static const double kMaxLedCurrent = 0.040;   // absolute maximum rating, amps

// --- steady-state cell: the VTL5C3's constants (the other parts carry theirs in their descriptor) --
static const double kDarkConductance = 1.0 / 20e6;   // ASSUMED 20 Mohm; the databook says >= 10 Mohm

/** The anchors: LED current in mA against cell resistance in ohms (VTL5C3, mean of the four
    adapt-history curves of the databook's plot, read by eye; the spec points are 1 mA 30 kohm,
    40 mA 1.5 kohm and R(0.5 mA) / R(5 mA) = 20). */
static const int kAnchors = 9;
static const double kAnchorMa[kAnchors] = { 0.1, 0.2, 0.5, 1.0, 2.0, 5.0, 10.0, 20.0, 40.0 };
static const double kAnchorR[kAnchors]  = { 1.2e6, 520e3, 115e3, 46e3, 16e3, 5.5e3, 3.0e3, 1.7e3, 1.25e3 };

// --- time response: the VTL5C3's constants -----------------------------------------------------
static const double kTauStage1 = 1.446e-3;               // first rising / falling stage, all pools
static const double kTauRise2  = 1.232e-3;               // second rising stage
static const double kTauFall[3] = { 0.802e-3, 6.283e-3, 61.39e-3 };   // fast, medium, slow pool
static const double kCapMedium = 149.7e-6;               // the most the medium pool can hold, siemens
static const double kCapSlow   = 4.771e-6;               // the most the slow pool can hold, siemens

// --- the part descriptor ---------------------------------------------------------------------
/** Everything that makes one vactrol a different one. */
struct Part {
    const char* name;
    // LED: Shockley diode, junction voltage `ledVf20` at 20 mA, `ledNVt` volts per e-fold, bulk `ledRs`.
    double ledNVt, ledRs, ledVf20, maxLedCurrent;
    // Cell: dark conductance and the resistance (ohms) against LED current (mA) anchors, log-log.
    double darkG;
    int nAnchors;
    const double* anchorMa;
    const double* anchorR;
    // Time: two rising stages, three falling pools (fast, medium, slow), the pools' capacities.
    double tauRise1, tauRise2, tauFall[3], capMedium, capSlow;
};

enum PartId { PART_VTL5C3 = 0, PART_NSL32SR2 = 1, PART_NSL32SR3 = 2, PART_GL5528 = 3, PART_COUNT = 4 };

// NSL-32SR2: Advanced Photonix sheet REV 12-02-15 plot, digitised.
static const double kNsl2Ma[9] = { 0.1, 0.2, 0.5, 1.0, 2.0, 5.0, 10.0, 20.0, 40.0 };
static const double kNsl2R[9]  = { 1075.0, 553.0, 234.0, 122.0, 81.5, 48.2, 32.4, 24.0, 19.0 };
// NSL-32SR3: Silonex sheet 104058 REV 3 plot, digitised.
static const double kNsl3R[9]  = { 2050.0, 1110.0, 500.0, 293.0, 189.0, 115.0, 80.5, 60.7, 47.6 };
// LED + GL5528: R = 14.1 kohm * (378 lux/mA * I / 10 lux)^-0.6.
static const double kDiyR[9]   = { 6349, 4189, 2417, 1595, 1052, 607, 401, 264, 174 };

/** The parts, indexed by PartId. The VTL5C3 row is the constants above, so the default part is
    the model this header has always been. */
static const Part kParts[PART_COUNT] = {
    { "VTL5C3 (PerkinElmer)",
      kLedNVt, kLedRs, kLedVf20, kMaxLedCurrent, kDarkConductance, kAnchors, kAnchorMa, kAnchorR,
      kTauStage1, kTauRise2, { kTauFall[0], kTauFall[1], kTauFall[2] }, kCapMedium, kCapSlow },
    { "NSL-32SR2 (Silonex / API)",
      0.052, 1.0, 2.0, 0.025, 1.0 / 5e6, 9, kNsl2Ma, kNsl2R,
      0.1e-3, 4.9e-3, { 0.59e-3, 0.0, 0.0 }, 0.0, 0.0 },
    { "NSL-32SR3 (Silonex)",
      0.052, 1.0, 2.0, 0.025, 1.0 / 25e6, 9, kNsl2Ma, kNsl3R,
      0.1e-3, 4.9e-3, { 1.45e-3, 0.0, 0.0 }, 0.0, 0.0 },
    { "LED + GL5528 (DIY)",
      0.052, 1.0, 2.1, 0.030, 1.0 / 2e6, 9, kNsl2Ma, kDiyR,
      1.0e-3, 19.0e-3, { 29.0e-3, 0.0, 0.0 }, 0.0, 0.0 },
};

inline const Part& part(int id) { return kParts[id < 0 ? 0 : (id >= PART_COUNT ? PART_COUNT - 1 : id)]; }

// --- LED ------------------------------------------------------------------------------------
/** The current into an LED fed from `vdrive` volts through `rSeries` ohms. Zero for a drive
    at or below zero (reverse breakdown at 3 V is not reached by the circuits here). */
inline double ledCurrent(double vdrive, double rSeries, const Part& p) {
    if (!(vdrive > 0.0)) return 0.0;
    const double nvt = p.ledNVt;
    const double is = 0.020 / std::expm1(p.ledVf20 / nvt);
    const double rt = rSeries + p.ledRs;
    // f(Vj) = Vj + rt*Is*(exp(Vj/nVt) - 1) - vdrive is convex and increasing, so Newton from a
    // point where f > 0 comes down onto the root monotonically.
    double i0 = (vdrive - 1.0) / rt;
    double vj = (i0 > 1e-12) ? nvt * std::log1p(i0 / is) : vdrive;
    if (vj > vdrive) vj = vdrive;
    for (int k = 0; k < 40; k++) {
        double e = is * std::expm1(vj / nvt);
        double f = vj + rt * e - vdrive;
        double d = 1.0 + rt * (e + is) / nvt;
        double step = f / d;
        vj -= step;
        if (std::fabs(step) < 1e-12) break;
    }
    double i = (vdrive - vj) / rt;
    return i > 0.0 ? i : 0.0;
}
/** The VTL5C3's LED. */
inline double ledCurrent(double vdrive, double rSeries) { return ledCurrent(vdrive, rSeries, kParts[0]); }

// --- steady-state cell ----------------------------------------------------------------------
/** The illuminated conductance in siemens for an LED current in amps (steady state, adapted),
    dark conductance not included. Log-log interpolation; below the table's first current that
    segment's slope continues (so the conductance runs to zero with the current). */
inline double steadyLight(double iLed, const Part& p) {
    if (!(iLed > 0.0)) return 0.0;
    const double ma = iLed * 1e3;
    const double* aMa = p.anchorMa;
    const double* aR = p.anchorR;
    const int n = p.nAnchors;
    int s;
    if (ma <= aMa[0]) s = 0;
    else if (ma >= aMa[n - 1]) s = n - 2;
    else { s = 0; while (ma > aMa[s + 1]) s++; }
    double slope = (std::log(aR[s + 1]) - std::log(aR[s])) /
                   (std::log(aMa[s + 1]) - std::log(aMa[s]));
    double lnR = std::log(aR[s]) + slope * (std::log(ma) - std::log(aMa[s]));
    double g = std::exp(-lnR);
    double gd = p.darkG;
    if (ma > aMa[0]) return g > gd ? g - gd : 0.0;
    // below the table: G ~ I^1.2, anchored to the table's first point
    double g0 = std::exp(-std::log(aR[0])) - gd;
    return (g0 > 0.0 ? g0 : 0.0) * std::pow(ma / aMa[0], -slope);
}
inline double steadyLight(double iLed) { return steadyLight(iLed, kParts[0]); }

/** Steady-state cell resistance for an LED current, ohms (dark included). */
inline double steadyResistance(double iLed, const Part& p) {
    return 1.0 / (p.darkG + steadyLight(iLed, p));
}
inline double steadyResistance(double iLed) { return steadyResistance(iLed, kParts[0]); }

// --- time response --------------------------------------------------------------------------

/** One cell. Feed `step()` the LED current every sample, read back the conductance. The
    public constants are the fitted part; tests overwrite them to build broken variants. */
struct Vactrol {
    const Part* p = &kParts[0];
    int partId = 0;
    double tauRise1 = kTauStage1, tauRise2 = kTauRise2;
    double tauFall[3] = { kTauFall[0], kTauFall[1], kTauFall[2] };
    double capMedium = kCapMedium, capSlow = kCapSlow;

    double s[3] = {0, 0, 0};     // first stage per pool
    double g[3] = {0, 0, 0};     // the pools' conductance
    double aS = 0, aR = 0, aF[3] = {0, 0, 0};
    double dt = 0;

    static double soft(double x, double c) { return x > 0.0 ? x * c / (x + c) : 0.0; }

    /** Make this cell a different part: its curve, its LED, its time constants. The state is kept
        (a part changed under a running cell lets its pools settle to the new part's targets). */
    void setPart(int id) {
        if (id < 0) id = 0;
        if (id >= PART_COUNT) id = PART_COUNT - 1;
        if (id == partId) return;
        partId = id;
        p = &kParts[id];
        tauRise1 = p->tauRise1; tauRise2 = p->tauRise2;
        for (int i = 0; i < 3; i++) tauFall[i] = p->tauFall[i];
        capMedium = p->capMedium; capSlow = p->capSlow;
        if (dt > 0.0) configure(dt);
    }

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
        double t[3]; split(steadyLight(iLed, *p), t);
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
        double t[3]; split(steadyLight(iLed, *p), t);
        for (int i = 0; i < 3; i++) {
            s[i] += (t[i] - s[i]) * aS;
            g[i] += (s[i] - g[i]) * (s[i] > g[i] ? aR : aF[i]);
        }
        return conductance();
    }

    double conductance() const { return p->darkG + g[0] + g[1] + g[2]; }
    double resistance() const { return 1.0 / conductance(); }
};

}  // namespace vactrol

#pragma once
// The HEF4066BT quad bilateral switch (one switch of four), as the 4066 Quad Gated Switch
// board uses it: VDD = +12 V, VSS = GND, so the signal range is 0..VDD, not +-VDD.
//
// Sources (all read 2026-10-03; the numbers below are the datasheets', not memory):
//   [N]  Nexperia HEF4066B, Rev. 12, 25 July 2024 (assets.nexperia.com/documents/data-sheet/HEF4066B.pdf)
//          Table 7  RON at 25 C, ISW = 200 uA: peak 350/80/60, rail (0 V) 115/50/40,
//                   rail (VDD) 120/65/50 ohm at VDD 5/10/15 V
//          Fig. 6   typical RON as a function of VI at VDD = 5, 10, 15 V (digitised here)
//          Table 8  tPHZ 80/65/60, tPLZ 80/70/70, tPZH 40/20/15, tPZL 45/20/15 ns
//          Table 12 THD 0.25 % (5 V) / 0.04 % (10, 15 V) at 0.5 VDD p-p, 1 kHz, RL 10k;
//                   Vct 50 mV (control to signal, 10 V, RL 10k, CL 15 pF; Fig. 11 defines it
//                   as the span between the two edge spikes);
//                   crosstalk and OFF isolation -50 dB at 1 MHz, RL 1k; -3 dB at 90 MHz
//          Table 4  VI limited to -0.5 V .. VDD + 0.5 V, IIK +-10 mA: the signal pins have
//                   clamp diodes to VSS and VDD
//   [R]  RCA/Harris CD4066B datasheet (resources.ampheo.com/static/datasheets/harris-corporation/cd4066bk.pdf):
//          ron max over temperature (-55/-40/+25/+85/+125 C): 800/850/1050/1200/1300 (5 V),
//          310/330/400/500/550 (10 V), 200/210/240/300/320 (15 V) ohm -- the only published
//          temperature dependence found (Nexperia's Table 7 is 25 C only);
//          Cios (switch feedthrough) 0.5 pF, Cis = Cos = 8 pF (5 V supply).
//
// What this gives: Ron depends on the signal level, on VDD and on temperature; the pins
// clamp one diode drop outside 0..VDD (so a bipolar signal loses its negative half); the
// OFF switch leaks the signal through 0.5 pF; the enable edge injects a small charge; and
// the switch is make-before-break by (tPHZ - tPZH), not break-before-make.
//
// ASSUMED (the datasheets are silent): the clamp diodes' Is (0.6 V at 1 mA, n = 1); the
// capacitance at the Z node (Cos 8 pF + the 5 pF load of the isolation test); that
// Fig. 6's Ron-versus-VI at 12 V is the conductance-weighted interpolation between the 10 V
// and 15 V curves at the same VI/VDD; that RCA's temperature ratios hold for the
// Nexperia part.
//
// No Rack dependency, so tests/ drives it bare.

#include <cmath>

namespace hef4066 {

static const double kVt = 0.025852;                   // kT/q at 300 K

struct Pt { double v, r; };                            // volts, ohms

// Fig. 6, digitised from the PDF at 400 dpi (axis: 2.22 px per ohm, 59.2 px per volt).
// 5 V: peak read off page 5's coarser render (+-15 %); 10 V and 15 V +-2 ohm.
static const Pt kRon5[]  = { {0.0,115}, {0.5,128}, {1.0,165}, {1.5,250}, {2.0,330}, {2.5,350}, {2.8,345},
                              {3.0,320}, {3.36,243}, {3.87,150}, {4.2,133}, {5.0,115} };
static const Pt kRon10[] = { {0.0,48.6}, {1.0,59}, {2.0,69}, {2.8,75}, {3.5,70}, {4.1,67.6}, {5.0,69},
                              {6.0,72}, {7.5,79.7}, {8.5,88}, {8.9,89.6}, {9.5,82}, {10.0,73} };
static const Pt kRon15[] = { {0.0,38.7}, {1.5,43}, {2.85,47}, {5.0,44.6}, {6.7,41}, {8.0,44.5}, {10.0,49.5},
                              {11.5,55}, {12.9,59.5}, {15.0,45.5} };

inline double curve(const Pt* p, int n, double v) {
    if (v <= p[0].v) return p[0].r;
    for (int i = 1; i < n; i++)
        if (v <= p[i].v)
            return p[i - 1].r + (p[i].r - p[i - 1].r) * (v - p[i - 1].v) / (p[i].v - p[i - 1].v);
    return p[n - 1].r;
}

/** RON at 25 C for a signal at vi, supply vdd (clamped to the 5..15 V the datasheet gives
    curves for). The three curves are read at the same fraction of the supply, and the
    conductances (not the resistances) are interpolated in VDD. */
inline double ronTypical(double vdd, double vi) {
    if (vdd < 5.0) vdd = 5.0;
    if (vdd > 15.0) vdd = 15.0;
    if (vi < 0.0) vi = 0.0;
    if (vi > vdd) vi = vdd;
    double x = vi / vdd;
    double g5  = 1.0 / curve(kRon5,  sizeof(kRon5)  / sizeof(Pt), x * 5.0);
    double g10 = 1.0 / curve(kRon10, sizeof(kRon10) / sizeof(Pt), x * 10.0);
    double g15 = 1.0 / curve(kRon15, sizeof(kRon15) / sizeof(Pt), x * 15.0);
    double g = vdd <= 10.0 ? g5 + (g10 - g5) * (vdd - 5.0) / 5.0
                           : g10 + (g15 - g10) * (vdd - 10.0) / 5.0;
    return 1.0 / g;
}

/** The ratio of RON at tempC to RON at 25 C, from RCA's maximum ron per temperature. */
inline double tempFactor(double vdd, double tempC) {
    static const double T[5]   = { -55.0, -40.0, 25.0, 85.0, 125.0 };
    static const double F5[5]  = { 800.0 / 1050, 850.0 / 1050, 1.0, 1200.0 / 1050, 1300.0 / 1050 };
    static const double F10[5] = { 310.0 / 400, 330.0 / 400, 1.0, 500.0 / 400, 550.0 / 400 };
    static const double F15[5] = { 200.0 / 240, 210.0 / 240, 1.0, 300.0 / 240, 320.0 / 240 };
    if (vdd < 5.0) vdd = 5.0;
    if (vdd > 15.0) vdd = 15.0;
    if (tempC < T[0]) tempC = T[0];
    if (tempC > T[4]) tempC = T[4];
    int i = 0;
    while (i < 3 && tempC > T[i + 1]) i++;
    double f = (tempC - T[i]) / (T[i + 1] - T[i]);
    double a = F5[i] + (F5[i + 1] - F5[i]) * f;
    double b = F10[i] + (F10[i + 1] - F10[i]) * f;
    double c = F15[i] + (F15[i + 1] - F15[i]) * f;
    return vdd <= 10.0 ? a + (b - a) * (vdd - 5.0) / 5.0 : b + (c - b) * (vdd - 10.0) / 5.0;
}

inline double lerp3(double vdd, double a5, double a10, double a15) {
    if (vdd < 5.0) vdd = 5.0;
    if (vdd > 15.0) vdd = 15.0;
    return vdd <= 10.0 ? a5 + (a10 - a5) * (vdd - 5.0) / 5.0 : a10 + (a15 - a10) * (vdd - 10.0) / 5.0;
}

struct OnState {
    double vin = 0.0;       // the Y pin (after the source resistance and the clamps)
    double vout = 0.0;      // the Z pin
    double ron = 0.0;       // the channel resistance used
    int iterations = 0;
};

struct Chip {
    double vdd = 12.0;                  // the board's VCC
    double tempC = 25.0;
    // The clamp diodes of the signal pins (Table 4: VI limited to -0.5 .. VDD + 0.5 V).
    double clampIs = 8.4e-14;           // 0.6 V at 1 mA, n = 1  (assumed)
    // Capacitances, farads.
    double cFeed = 0.5e-12;             // Cios [R]; reproduces Table 12's -50 dB at 1 MHz, RL 1k
    double cNode = 13e-12;              // Cos 8 pF [R] + CL 5 pF of the isolation test [N]
    // Control-to-signal charge: each edge of nE is a spike of Vct/2 = 25 mV at 10 V
    // (Fig. 11b: Vct spans the +/- spikes) on CL 15 pF + Cos 8 pF, scaled by VDD.
    double edgeSpikeAt10V = 0.025;
    double cSpike = 23e-12;

    double ron(double vi) const { return ronTypical(vdd, vi) * tempFactor(vdd, tempC); }

    double tPHZ() const { return 1e-9 * lerp3(vdd, 80, 65, 60); }
    double tPLZ() const { return 1e-9 * lerp3(vdd, 80, 70, 70); }
    double tPZH() const { return 1e-9 * lerp3(vdd, 40, 20, 15); }
    double tPZL() const { return 1e-9 * lerp3(vdd, 45, 20, 15); }
    /** Charge one nE edge leaves on the Z node, coulombs. */
    double edgeCharge() const { return edgeSpikeAt10V * (vdd / 10.0) * cSpike; }

    /** Current out of a pin into the two clamp diodes (positive above VDD, negative below 0)
        and its slope. */
    void clamp(double v, double& i, double& g) const {
        double dHi, dLo;
        double eHi = expl_((v - vdd) / kVt, dHi), eLo = expl_(-v / kVt, dLo);
        i = clampIs * (eHi - 1.0) - clampIs * (eLo - 1.0);
        g = clampIs / kVt * (dHi + dLo);
    }

    /** The ON switch driven through `rs` from `vsrc` and loaded by `rl` to ground: both
        pins, Ron at the mean pin voltage. Newton on the two node equations. */
    OnState solveOn(double vsrc, double rs, double rl) const {
        OnState s;
        double vi = vsrc < -0.6 ? -0.6 : (vsrc > vdd + 0.6 ? vdd + 0.6 : vsrc);
        double vo = vi;
        double r = ron(vi);
        vo = vi * rl / (rl + r);
        for (int it = 0; it < 60; it++) {
            s.iterations = it + 1;
            r = ron(0.5 * (vi + vo));
            double ci, cg, co, cog;
            clamp(vi, ci, cg);
            clamp(vo, co, cog);
            double f1 = (vsrc - vi) / rs - ci - (vi - vo) / r;
            double f2 = (vi - vo) / r - vo / rl - co;
            double a = -1.0 / rs - cg - 1.0 / r, b = 1.0 / r;
            double c = 1.0 / r, d = -1.0 / r - 1.0 / rl - cog;
            double det = a * d - b * c;
            double dvi = (-f1 * d + b * f2) / det;
            double dvo = (-a * f2 + c * f1) / det;
            if (dvi > 1.0) dvi = 1.0;
            if (dvi < -1.0) dvi = -1.0;
            if (dvo > 1.0) dvo = 1.0;
            if (dvo < -1.0) dvo = -1.0;
            vi += dvi;
            vo += dvo;
            if (std::fabs(dvi) < 1e-10 && std::fabs(dvo) < 1e-10) break;
        }
        s.vin = vi;
        s.vout = vo;
        s.ron = ron(0.5 * (vi + vo));
        return s;
    }

private:
    static double expl_(double x, double& d) {
        if (x > 40.0) {
            static const double e40 = std::exp(40.0);
            d = e40;
            return e40 * (1.0 + x - 40.0);
        }
        double e = std::exp(x);
        d = e;
        return e;
    }
};

} // namespace hef4066

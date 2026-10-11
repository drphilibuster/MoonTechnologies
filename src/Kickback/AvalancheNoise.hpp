#pragma once
// The noise source of the Percussive Noise Voice (Modular in a Week Day 9,
// `Percussive Noise Voice.pdf`, circuit by A_Magic_Pulsewave, drawn by Kristian Blasol,
// 2019-10-17) as a circuit: three BC549s, T3 a transistor whose emitter-base junction is
// run in reverse breakdown as the noise source ("an EBC transistor facing backwards").
//
//   VCC --R4 4k7-- C (T2's collector)        T2 BC549, emitter to ground
//   C --R3 47k-- A --C3 0.1u-- ground        A is T3's emitter (the junction's n side)
//   T3's base is T2's base (its collector is left open), so the junction runs from A back
//   into T2's base: reverse biased, and in breakdown once A is a breakdown voltage above it
//   C --C4 1u-- B1 (T1's base), B1 --R7 1M-- VCC, B1 --C5-- ground, T1's base-emitter to ground
//
// T2's base current is the breakdown current, so T2's collector (and so A, through R3) is
// pulled down until the junction just conducts: the loop biases itself with the junction at
// the edge of breakdown, and the avalanche's noise current is amplified by T2 into the signal
// C4 hands to T1. B1 is what T1 sees, and is the output here.
//
// Solved by `src/Mna.hpp`. The breakdown's noise is a current source across the junction with
// the shot-noise spectrum multiplied by the avalanche gain, 2 q I M^2, white up to the solver's
// rate. T1's collector side (D3, R8, the envelope, C6) and the trigger envelope are not part
// of this: the module has its own envelope, vactrol and filters.
//
// ASSUMED, because neither the drawing nor a BC549 datasheet says: the supply is 12 V; the
// junction breaks down at 8 V (a BC549's VEBO rating is only 5 V, real junctions avalanche at
// 7-10 V; this is the number to measure on a real part) with an e-fold every 100 mV; the
// avalanche gain is M = 30; and C5 is the snare's 0.1 uF. The absolute noise level is
// therefore not known, and the output is normalised by the caller; the spectrum and the
// nonlinearity are the circuit's.
//
// MICROPLASMAS (added 2026-10-03). Real junction breakdown is not a Gaussian shot-noise source:
// it starts at a few defect regions ("microplasmas", Haitz 1964) that switch on and off at
// random, so the junction current is a sum of constant-height telegraph pulses (src/Microplasma.hpp
// has the physics and the literature). Each is a switched zener in series with a spreading
// resistance, in parallel with the bulk junction, solved with the rest of the circuit; the
// feedback through R3 and T2 (a pulse pulls the junction down and quenches itself) is the
// circuit's own. `microplasma = false` gives the shot-noise-only model that preceded it (a
// Gaussian current, 2 q I M^2) and the tests keep it as the reference for the linear parts.
// The pulse sizes, rates and Rs below are ASSUMED (no BC549 / 2N3904 data exists); only their
// structure and the statistics that follow from it are the literature's.
//
// A second variant is the Tiny Dazzler Electronics original of the same circuit
// (`Tiny Dazzler Schematic.png`, 2011): same topology and values, 2N3904s, +13.5 V.
//
// No Rack dependency, so tests/ drives it bare.

#include "../Mna.hpp"
#include "../Microplasma.hpp"

#include <cmath>
#include <cstdint>

namespace kickback {


/** The avalanche circuit's Newton step with the circuit's own structure written out: the same
    equations `mna::Circuit` stamps for it (same elements, same trapezoidal capacitors, same gmin,
    same 1 V update limit and 1 uV stop), but the four plasma nodes -- each tied only to the emitter
    node and the base -- are eliminated by hand and the remaining four unknowns (A, B2, C2, B1) are
    solved as a 4x4. About a third of the generic solver's cost; the generic one stays as the reference
    (`AvalancheNoise::fast = false`) and tests/Kickback/test_avalanche_fast.cpp holds the two together. */
struct FastAvalanche {
    enum { A, B2, C2, B1, kN = 4, kP = 4 };
    double v[kN + kP] = {};                 // A B2 C2 B1 P0..P3
    double vPrev[3] = {}, iPrev[3] = {};    // C3, C4, C5
    double cap[3] = {};                     // their farads
    double g4 = 0, g3 = 0, g7 = 0;          // R4, R3, R7
    double npnIs = 0, npnBF = 0, npnBR = 0, npnVAF = 0;
    double dIs = 0, dNvt = 0;               // T1's base-emitter diode
    double jIs = 0, jNvt = 0, jBv = 0, jIbv = 0, jNvtBr = 0;      // the bulk junction
    double zIs[kP] = {}, zNvt[kP] = {}, zIbv[kP] = {}, zNvtBr[kP] = {}, gR[kP] = {};
    double gmin = 1e-12, h = 1.0;

    // exp(x) and its slope; below -700 the answer is 0 to the last bit (an off plasma's breakdown term
    // is exp(-1e5)), so the call is skipped.
    static double ex(double x, double& d) {
        if (x < -700.0) { d = 0.0; return 0.0; }
        return mna::expLim(x, d);
    }

    void load(const mna::Circuit& c, const int* plasmaZ) {
        for (int i = 0; i < kN + kP; i++) v[i] = c.v[i];
        const int capIdx[3] = { 3, 6, 8 };
        for (int k = 0; k < 3; k++) { const mna::Circuit::Elem& e = c.e[capIdx[k]]; cap[k] = e.p0; vPrev[k] = e.vPrev; iPrev[k] = e.iPrev; }
        g4 = c.e[0].p0; g3 = c.e[2].p0; g7 = c.e[7].p0;
        const mna::Circuit::Elem& q = c.e[1]; npnIs = q.p0; npnBF = q.p1; npnBR = q.p2; npnVAF = q.p3;
        dIs = c.e[9].p0; dNvt = c.e[9].p1;
        const mna::Circuit::Elem& j = c.e[4]; jIs = j.p0; jNvt = j.p1; jBv = j.p2; jIbv = j.p3; jNvtBr = j.p4;
        for (int k = 0; k < kP; k++) {
            gR[k] = c.e[10 + 2 * k].p0;
            const mna::Circuit::Elem& z = c.e[plasmaZ[k]];
            zIs[k] = z.p0; zNvt[k] = z.p1; zIbv[k] = z.p3; zNvtBr[k] = z.p4;
        }
        gmin = c.gmin; h = c.h;
    }

    /** One time step. `vcc` the supply, `inoise` the noise current A -> B2, `zBv[k]` each plasma's
        breakdown voltage now. Returns the Newton iterations, negative if it did not converge. */
    int step(double vcc, double inoise, const double* zBv, int maxIter) {
        const double gC[3] = { 2.0 * cap[0] / h, 2.0 * cap[1] / h, 2.0 * cap[2] / h };
        int result = -maxIter;
        for (int it = 1; it <= maxIter; it++) {
            double M[kN][kN + 1];
            for (int i = 0; i < kN; i++) { for (int j = 0; j <= kN; j++) M[i][j] = 0.0; M[i][i] = gmin; M[i][kN] = -gmin * v[i]; }
            const double vA = v[A], vB = v[B2], vC = v[C2], vD = v[B1];
            double d;

            // R4: vcc -> C2
            { double g = g4, i = g * (vcc - vC); M[C2][C2] += g; M[C2][kN] += i; }
            // R3: C2 -> A
            { double g = g3, i = g * (vC - vA);
              M[C2][C2] += g; M[C2][A] -= g; M[C2][kN] -= i;
              M[A][A] += g;  M[A][C2] -= g; M[A][kN] += i; }
            // R7: vcc -> B1
            { double g = g7, i = g * (vcc - vD); M[B1][B1] += g; M[B1][kN] += i; }
            // C3: A -> gnd, C5: B1 -> gnd
            { double g = gC[0], i = g * (vA - vPrev[0]) - iPrev[0]; M[A][A] += g; M[A][kN] -= i; }
            { double g = gC[2], i = g * (vD - vPrev[2]) - iPrev[2]; M[B1][B1] += g; M[B1][kN] -= i; }
            // C4: C2 -> B1
            { double g = gC[1], i = g * ((vC - vD) - vPrev[1]) - iPrev[1];
              M[C2][C2] += g; M[C2][B1] -= g; M[C2][kN] -= i;
              M[B1][B1] += g; M[B1][C2] -= g; M[B1][kN] += i; }
            // T1's base-emitter diode: B1 -> gnd
            { double e = ex(vD / dNvt, d), i = dIs * (e - 1.0), g = dIs * d / dNvt; M[B1][B1] += g; M[B1][kN] -= i; }
            // the bulk junction: B2 -> A, with its breakdown
            { double vd = vB - vA, d1, d2;
              double e1 = ex(vd / jNvt, d1), e2 = ex(-(vd + jBv) / jNvtBr, d2);
              double i = jIs * (e1 - 1.0) - jIbv * e2, g = jIs * d1 / jNvt + jIbv * d2 / jNvtBr;
              M[B2][B2] += g; M[B2][A] -= g; M[B2][kN] -= i;
              M[A][A] += g;   M[A][B2] -= g; M[A][kN] += i; }
            // the noise current, A -> B2
            M[A][kN] -= inoise; M[B2][kN] += inoise;
            // T2: collector C2, base B2, emitter gnd
            { const double Is = npnIs, BF = npnBF, BR = npnBR, VAF = npnVAF;
              double vbe = vB, vbc = vB - vC, def, der;
              double ef = ex(vbe / mna::kVt, def), er = ex(vbc / mna::kVt, der);
              double f = 1.0 - vbc / VAF;
              double Ic = Is * (ef - er) * f - Is / BR * (er - 1.0);
              double Ib = Is / BF * (ef - 1.0) + Is / BR * (er - 1.0);
              double dIc_dvbe = Is * def / mna::kVt * f;
              double dIc_dvbc = -Is * der / mna::kVt * f - Is * (ef - er) / VAF - Is / BR * der / mna::kVt;
              double dIb_dvbe = Is / BF * def / mna::kVt;
              double dIb_dvbc = Is / BR * der / mna::kVt;
              // collector row: dVb = dvbe + dvbc, dVc = -dvbc ; base row likewise
              M[C2][B2] += dIc_dvbe + dIc_dvbc; M[C2][C2] += -dIc_dvbc; M[C2][kN] -= Ic;
              M[B2][B2] += dIb_dvbe + dIb_dvbc; M[B2][C2] += -dIb_dvbc; M[B2][kN] -= Ib; }

            // the plasmas: R from A to Pk, a switched zener from B2 to Pk. Eliminate Pk.
            double dk[kP], rP[kP], rA[kP], rB[kP];
            for (int k = 0; k < kP; k++) {
                const double vP = v[kN + k];
                double iR = gR[k] * (vA - vP);
                double vd = vB - vP, d1, d2;
                double e1 = ex(vd / zNvt[k], d1), e2 = ex(-(vd + zBv[k]) / zNvtBr[k], d2);
                double iZ = zIs[k] * (e1 - 1.0) - zIbv[k] * e2, gz = zIs[k] * d1 / zNvt[k] + zIbv[k] * d2 / zNvtBr[k];
                const double gr = gR[k];
                // A row: +iR out, B2 row: +iZ out
                M[A][A] += gr;   M[A][kN] -= iR;
                M[B2][B2] += gz; M[B2][kN] -= iZ;
                // Pk row (F = -iR - iZ + gmin*vP): J = [-gr on A, -gz on B2, gr+gz+gmin on Pk]
                dk[k] = gr + gz + gmin;
                rA[k] = -gr; rB[k] = -gz;
                rP[k] = (iR + iZ) - gmin * vP;
                // columns Pk in rows A, B2 are (-gr) and (-gz); substitute dPk = (rP - rA dA - rB dB)/dk
                const double cA = -gr, cB = -gz, inv = 1.0 / dk[k];
                M[A][A]   -= cA * rA[k] * inv;  M[A][B2]  -= cA * rB[k] * inv;  M[A][kN]  -= cA * rP[k] * inv;
                M[B2][A]  -= cB * rA[k] * inv;  M[B2][B2] -= cB * rB[k] * inv;  M[B2][kN] -= cB * rP[k] * inv;
            }

            // 4x4, partial pivoting
            for (int c = 0; c < kN; c++) {
                int piv = c;
                for (int r = c + 1; r < kN; r++) if (std::fabs(M[r][c]) > std::fabs(M[piv][c])) piv = r;
                if (std::fabs(M[piv][c]) < 1e-300) return -it;
                if (piv != c) for (int j = c; j <= kN; j++) { double t = M[c][j]; M[c][j] = M[piv][j]; M[piv][j] = t; }
                double inv = 1.0 / M[c][c];
                for (int r = c + 1; r < kN; r++) {
                    double f = M[r][c] * inv;
                    if (f == 0.0) continue;
                    for (int j = c; j <= kN; j++) M[r][j] -= f * M[c][j];
                }
            }
            double dx[kN + kP];
            for (int r = kN - 1; r >= 0; r--) {
                double sum = M[r][kN];
                for (int j = r + 1; j < kN; j++) sum -= M[r][j] * dx[j];
                dx[r] = sum / M[r][r];
            }
            for (int k = 0; k < kP; k++) dx[kN + k] = (rP[k] - rA[k] * dx[A] - rB[k] * dx[B2]) / dk[k];

            double worst = 0.0;
            for (int i = 0; i < kN + kP; i++) {
                double x = dx[i];
                if (x > 1.0) x = 1.0;
                if (x < -1.0) x = -1.0;
                v[i] += x;
                if (std::fabs(x) > worst) worst = std::fabs(x);
            }
            if (worst < 1e-6) { result = it; break; }
        }
        // the capacitors' memory, as Circuit::step leaves it
        const double vcs[3] = { v[A], v[C2] - v[B1], v[B1] };
        for (int k = 0; k < 3; k++) { iPrev[k] = gC[k] * (vcs[k] - vPrev[k]) - iPrev[k]; vPrev[k] = vcs[k]; }
        return result;
    }
};

struct AvalancheNoise {
    enum Node { A, B2, C2, B1, P0, P1, P2, P3, kNodes };
    static constexpr int kPlasmas = 4;
    /** Which board: the Percussive Noise Voice (BC549, 12 V) or the Tiny Dazzler original (2N3904, 13.5 V). */
    enum Variant { PERCUSSIVE = 0, DAZZLER = 1 };
    enum Fixed { VCC = 0, kFixed };

    static constexpr double kVcc = 12.0;
    static constexpr double kIs = 7.6e-14, kBF = 520.0, kBR = 10.0, kVAF = 100.0;
    static constexpr double kBv = 8.0, kIbv = 1e-3, kNvtBr = 0.1;
    static constexpr double kM = 30.0;
    static constexpr double kQ = 1.602176634e-19;

    mna::Circuit ckt;
    int junction = -1, noiseSrc = -1;
    int plasmaZ[kPlasmas] = { -1, -1, -1, -1 };
    bool ok = true;
    double rate = 192000.0;
    uint32_t rng = 0x9E3779B9u;
    bool started = false;

    Variant variant = PERCUSSIVE;
    double vcc = kVcc;
    double bv = kBv;                      // the bulk junction's breakdown voltage
    bool microplasma = true;              // false: the shot-noise-only model
    bool fast = true;                     // the hand-eliminated solver (FastAvalanche); false: the generic mna::Circuit
    FastAvalanche fa;
    microplasma::Params mp[kPlasmas];
    microplasma::Switch sw[kPlasmas];
    microplasma::Rng mrng;

    /** `c5` is the schematic's "Cap1 value" across T1's base: 0.1 uF for the snare, 1 nF for the hi-hat. */
    explicit AvalancheNoise(Variant v = PERCUSSIVE, double c5 = 0.1e-6) : variant(v) {
        using namespace mna;
        double is = kIs, bf = kBF, br = kBR, vaf = kVAF;
        if (v == DAZZLER) {                                       // 2N3904 (SPICE model values)
            vcc = 13.5; is = 6.734e-15; bf = 416.4; br = 0.7371; vaf = 74.03;
            bv = 7.5;                                             // ASSUMED: VEBO is 6 V min on the datasheet
        }
        ckt.n = kNodes;
        const int vc = fixed(VCC);
        ckt.addResistor(vc, C2, 4.7e3);                           // R4
        ckt.addNpn(C2, B2, GND, is, bf, br, vaf);                 // T2
        ckt.addResistor(C2, A, 47e3);                             // R3
        ckt.addCapacitor(A, GND, 0.1e-6);                         // C3
        junction = ckt.addZener(B2, A, 1e-14, 0.025852, bv, kIbv, kNvtBr);   // T3: base -> emitter
        noiseSrc = ckt.addCurrent(A, B2);
        ckt.addCapacitor(C2, B1, 1e-6);                           // C4
        ckt.addResistor(vc, B1, 1e6);                             // R7
        ckt.addCapacitor(B1, GND, c5);                            // C5
        ckt.addDiode(B1, GND, is / bf, 0.025852);                 // T1's base-emitter
        // the microplasmas: a spreading resistance from the emitter node A to each plasma's own
        // node, and a switched zener from there to the base. Off, its breakdown voltage is 1 kV.
        // Their breakdown voltages sit just below where the bulk junction rests (bv - 0.65 V), from
        // a fraction of a volt under to a few tens of millivolts over it, so a few are active.
        static const double off[kPlasmas]   = { 0.66,  0.70,  0.76,  0.84 };   // V below the bulk knee
        static const double rs[kPlasmas]    = { 40e3,  80e3,  150e3, 300e3 };
        static const double nuOn[kPlasmas]  = { 30.0,  20.0,  15.0,  10.0 };
        static const double vs[kPlasmas]    = { 0.02,  0.03,  0.03,  0.04 };
        static const double nuOff[kPlasmas] = { 6e4,   2e4,   8e3,   3e3 };
        static const double iq[kPlasmas]    = { 0.5e-6, 1e-6, 1e-6,  1e-6 };
        for (int k = 0; k < kPlasmas; k++) {
            mp[k].vb = bv - off[k]; mp[k].rs = rs[k]; mp[k].nuOn = nuOn[k];
            mp[k].vs = vs[k]; mp[k].nuOff = nuOff[k]; mp[k].iq = iq[k];
            ckt.addResistor(A, P0 + k, rs[k]);
            plasmaZ[k] = ckt.addZener(B2, P0 + k, 1e-14, 0.025852, 1000.0, 1e-6, 0.01);
        }
        mrng.seed(0x6D1C0DEu);
    }

    void seed(uint32_t s) { rng = s ? s : 0x9E3779B9u; mrng.seed((s ? s : 1u) * 2654435761u + 12345u); }

    /** One standard-normal sample (Box-Muller from an xorshift32). */
    double gauss() {
        auto u = [this]() {
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            return (rng + 0.5) / 4294967296.0;
        };
        double a = u(), b = u();
        return std::sqrt(-2.0 * std::log(a)) * std::cos(2.0 * 3.14159265358979323846 * b);
    }

    /** Finds the operating point and sets the step; the noise is off while it settles. */
    void start(double solverRate) {
        rate = solverRate;
        for (int k = 0; k < kPlasmas; k++) { sw[k].on = false; ckt.e[plasmaZ[k]].p2 = 1000.0; }
        double tgt[kFixed] = { vcc };
        ok = ckt.solveDc(tgt, kFixed, 48);
        ckt.h = 1.0 / rate;
        fa.load(ckt, plasmaZ);
        started = true;
    }

    /** The current the microplasmas carry now, amps from the emitter node to the base. */
    double plasmaCurrent() const {
        double s = 0.0;
        for (int k = 0; k < kPlasmas; k++) if (sw[k].on) s -= ckt.diodeCurrent(plasmaZ[k]);
        return s;
    }
    /** The bulk junction's voltage (emitter node over base) at the present solution. */
    double junctionVoltage() const { return ckt.v[A] - ckt.v[B2]; }

    /** One solver step; returns B1's voltage. `noiseOn` false gives the quiet circuit. */
    double step(bool noiseOn = true) {
        if (!started) start(rate);
        ckt.fixedV[VCC] = vcc;
        double ib0 = -ckt.diodeCurrent(junction);
        double ib = ib0 < 0.0 ? 0.0 : ib0;                  // the breakdown current, A -> base
        if (microplasma && noiseOn) {
            double vj = junctionVoltage(), h = 1.0 / rate;
            for (int k = 0; k < kPlasmas; k++) {
                double i = sw[k].on ? -ckt.diodeCurrent(plasmaZ[k]) : 0.0;
                bool was = sw[k].on;
                sw[k].step(mp[k], vj, i, h, mrng);
                if (sw[k].on != was) ckt.e[plasmaZ[k]].p2 = sw[k].on ? mp[k].vb : 1000.0;
                if (sw[k].on) ib += i > 0.0 ? i : 0.0;
            }
        } else if (!noiseOn) {
            for (int k = 0; k < kPlasmas; k++) { sw[k].on = false; ckt.e[plasmaZ[k]].p2 = 1000.0; }
        }
        double sigma = std::sqrt(2.0 * kQ * ib * kM * kM * rate * 0.5);
        ckt.setCurrent(noiseSrc, noiseOn ? sigma * gauss() : 0.0);
        if (fast) {
            double zBv[kPlasmas];
            for (int k = 0; k < kPlasmas; k++) zBv[k] = ckt.e[plasmaZ[k]].p2;
            double inoise = ckt.e[noiseSrc].p0;
            if (fa.step(vcc, inoise, zBv, 80) < 0) ok = false;
            for (int i = 0; i < kNodes; i++) ckt.v[i] = fa.v[i];
            return fa.v[B1];
        }
        if (ckt.step(80) < 0) ok = false;       // a plasma switching on needs a few more iterations than a quiet step
        return ckt.v[B1];
    }
};

/** The circuit as a noise source at the audio rate: the solver runs at a whole multiple of it
    (at least 192 kHz), B1's DC is taken off, and a fourth-order low-pass at 0.4 of the audio
    rate (two biquads) takes it down before every K-th sample is kept. The noise current's
    density is per hertz, so the output has the circuit's own spectrum at every rate; `kNorm`
    sets its level to the white noise this replaced (rms 1/sqrt(3) in the band at 48 kHz). */
struct AvalancheSource {
    static constexpr double kNormMicroplasma = 2.474;   // 0.57735 / 0.2334: the 48 kHz rms of the snare setting (C5 0.1 uF), measured over 3 seeds x 30 s, 44.1 to 192 kHz within 0.01 dB
    // The same 0.57735 (white's rms) for the other three boards the drum voices use, each the 48 kHz rms
    // measured over 3 seeds x 29 s (44.1 to 96 kHz within 0.3 dB for the 0.1 uF settings and 0.9 dB for
    // the 1 nF ones, whose band is wider): the Percussive Noise Voice's HiHat (BC549, C5 = 1 nF), and
    // the Tiny Dazzler's snare (2N3904, C1 = 0.1 uF) and HighHat (C1 = 1 nF).
    static constexpr double kNormHat = 1.8219, kNormDazzlerSnare = 1.7825, kNormDazzlerHat = 1.2360;
    /** The normalisation for a board: which of the four measured settings (C5 below 10 nF is the hi-hat's). */
    static double normFor(AvalancheNoise::Variant v, double c5) {
        const bool hat = c5 < 10e-9;
        if (v == AvalancheNoise::DAZZLER) return hat ? kNormDazzlerHat : kNormDazzlerSnare;
        return hat ? kNormHat : kNormMicroplasma;
    }
    static constexpr double kNorm = 1065.0;   // 0.57735 / 0.5421 mV: the 48 kHz rms of this circuit, as volts

    AvalancheNoise ckt;
    AvalancheNoise::Variant variant = AvalancheNoise::PERCUSSIVE;
    bool microplasma = true;            // false: the shot-noise-only model (kNorm is for that one)
    double c5 = 0.1e-6;                 // the schematic's Cap1: 0.1 uF snare, 1 nF hi-hat
    double norm = kNormMicroplasma;
    uint32_t seed0 = 0x5EAF00Du;
    int K = 4;
    double minSolver = 192000.0;        // the solver's rate is the smallest whole multiple of the audio rate that reaches this
    double fs = 48000.0, dc = 0.0;
    double hp = 0.0, hpA = 0.001;       // microplasma pulses rectify through T1's base: a one-pole DC block at 8 Hz
    struct Bq { double b0, b1, b2, a1, a2, z1 = 0, z2 = 0;
        double run(double x) { double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; } };
    Bq q[2];
    bool ready = false;

    explicit AvalancheSource(uint32_t seed = 0x5EAF00Du,
                             AvalancheNoise::Variant v = AvalancheNoise::PERCUSSIVE,
                             bool plasmas = true, double c5Farads = 0.1e-6)
        : variant(v), microplasma(plasmas), c5(c5Farads), norm(plasmas ? normFor(v, c5Farads) : kNorm), seed0(seed) {}

    /** The solver's floor in hertz, 192 kHz (full) or 96 kHz (eco: half the cost, band power within 0.4 dB
        of full out to 20 kHz over 4 seeds x 20 s; 48 kHz is not offered, it is 2.7 dB down above 16 kHz). Restarts the circuit. */
    void setSolverRate(double hz) { minSolver = hz; setRate(fs); }

    void setRate(double audioRate) {
        fs = audioRate;
        hpA = 2.0 * 3.14159265358979323846 * 8.0 / fs;
        K = (int)std::ceil(minSolver / fs);
        if (K < 1) K = 1;
        double fc = 0.4 * fs, w0 = 2.0 * 3.14159265358979323846 * fc / (fs * K);
        double Qs[2] = { 0.5411961, 1.3065630 };               // 4th-order Butterworth
        for (int i = 0; i < 2; i++) {
            double al = std::sin(w0) / (2.0 * Qs[i]), c = std::cos(w0), a0 = 1.0 + al;
            q[i].b0 = (1.0 - c) / 2.0 / a0; q[i].b1 = (1.0 - c) / a0; q[i].b2 = q[i].b0;
            q[i].a1 = -2.0 * c / a0; q[i].a2 = (1.0 - al) / a0;
        }
        reset();
    }

    void reset() {
        ckt = AvalancheNoise(variant, c5);
        ckt.microplasma = microplasma;
        ckt.seed(seed0);
        ckt.start(fs * K);
        dc = ckt.ckt.v[AvalancheNoise::B1];
        for (int i = 0; i < 2; i++) q[i].z1 = q[i].z2 = 0.0;
        hp = 0.0;
        ready = true;
    }

    /** One audio-rate sample. */
    inline float next() {
        double y = 0.0;
        for (int k = 0; k < K; k++) {
            double v = ckt.step() - dc;
            y = q[1].run(q[0].run(v));
        }
        if (microplasma) { hp += (y - hp) * hpA; y -= hp; }
        return (float)(y * norm);
    }
};

} // namespace kickback

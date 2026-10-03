#pragma once
// Accrual: the "4069 VCO" of the Modular in a Week course's Unfinished projects folder
// (Schematic_4069vco, kristian.borgstedt, EasyEDA, 2019-12-06), solved.
//
// THE CIRCUIT (read from the PDF; every value below is the sheet's)
//
//   exponential converter   Tune P1 / Fine P2 / CV1 P3 / CV2 P4 through R1 100k / R2 1M / R3 100k /
//                           R4 100k into node A, which a 1k trimmer R21 + R6 1k5 hold near ground.
//                           Q1 BC560 (PNP): base A, emitter QB, collector VEE. R7 1M: QB to VCC.
//                           Q2 BC550 (NPN): base QB, emitter GND, collector X. So VBE(Q2) - VEB(Q1)
//                           = A, Ic(Q2) = Ic(Q1) exp(A / Vt): a one-transistor-pair exponential.
//   integrator              U1.1 (CD4069UB inverter) X -> Z with C1 2n2 from X to Y, R8 680 Y to Z
//                           and D1 (1N4448, anode Y, cathode Z) across R8: a Miller integrator whose
//                           input current is Q2's collector current.
//   Schmitt reset           Z -R9 10k-> P3 -> U1.2 -> U1.3 -> S, R10 22k from S back to P3 (a
//                           positive-feedback comparator of two inverters), D2 (anode S, cathode X)
//                           shorting the integrator when S goes high.
//   outputs                 SAW = C2 220n from Z, R11 100k to ground.
//                           PWM: R12 47k from Z, R13 100k from the PW pot's wiper, R15 68k to VCC
//                           and R17 100k from Q3's emitter into the summing node N of U1.4, with
//                           R14 100k from U1.4's output back to N; U1.5 and U1.6 square it up;
//                           PULSE = C3 220n, R16 100k. Q3 (BC550) is an emitter follower, R18 10k to
//                           VEE, from the PWM jack through R20 100k and R19 1M.
//
// THE MODEL (what runs in Rack; the full netlist is solved only in tests, as the oracle)
//
//   1. The exponential pair is solved exactly (2x2 Newton on the two junction equations, with the
//      Early effect, the base currents and R7), per voice, warm-started. Out comes Q2's collector
//      current at zero Early correction, I_base = Is exp(VBE / Vt).
//   2. The integrator + Schmitt is a relaxation oscillator whose slow variable is the voltage on C1.
//      While S is low, the inverter and its loads sit on a DC curve Z(X) (the CD4069UB solved by
//      `Mna.hpp`, loaded by R9+R10 and R12): C1's voltage W = Z - X and Q2's current I(X) =
//      I_base (1 + (X - VB)/VAF) give dW/dt = I(X)/C1. `Plant` tabulates the curve once per supply:
//      the ramp is then a table lookup, exact to the table, not a straight line. The Schmitt
//      starts to trip at the FOLD of that curve (where the S-low equilibrium ceases to exist),
//      found by continuation, not at a threshold typed in; the transient of (3) says where S
//      actually crosses VDD/2 at each ramp current, which is the model's trip.
//   3. The ~1 us reset (S high, D2 and D1 conduct, C1 is dumped) is a transient of the same netlist
//      at 0.02..100 ns steps, run once per supply and input capacitance at fifteen ramp currents
//      (octave spaced, 30 nA .. 1.3 mA) with Q2 and the front end around its base in it: the reset
//      throws Z up 1.5 V and X up 2.5 V in a microsecond, Q2's Cjc kicks its base and cuts it off for
//      about a microsecond, and that is charge on C1. It leaves C1's voltage and the time S crossed
//      VDD/2. In the audio-rate model it is an EVENT: located to a fraction of a sample, a polyBLEP
//      step on SAW, and the PULSE threshold crossings inside it found on the stored path of Z.
//   4. The PWM stage is memoryless: PULSE is high while Z is below the threshold where the summing
//      node's currents balance with U1.4 at its self-bias (identical inverters, so U1.5 trips there
//      too): Zth = VM - R12 * [ (Vw - VM)/(R13 + Rpot) + (VCC - VM)/R15 + (QE - VM)/R17 ].
//   5. C2/R11 and C3/R16 are one-pole high-passes (7.2 Hz), exactly as drawn: the outputs are AC
//      coupled, so a low note sags and the pulse's mean is removed.
//
// HOW IT IS CHECKED: tests/Accrual steps the WHOLE netlist of the PDF (16 unknowns, 3 BJTs, 6
// inverters, 2 diodes, 12 capacitors) with an adaptive trapezoid and compares: period within 0.1 %
// from 4.5 Hz to 4 kHz at 12 V, within 0.4 % at 9, 10 and 15 V and at 5, 15 and 20 pF; the shape
// of Z over a cycle to 3 mV rms; PULSE duty to 0.2 % of the cycle.
//
// WHAT IS ASSUMED (the sheet does not say; each is a named constant or a parameter)
//   * the pots' ends (Tune/Fine/CV are bare pads): the knobs are +-5 V by default (menu: +-12 V);
//   * the PW pot's resistance (kPwPot, 100k linear), its ends are VCC and ground (pads P10, P11);
//   * VDD = VCC = +12 V (the 4069's supply pins are unlabelled on the sheet); menu: 9, 10, 12, 15 V;
//   * the BC550/BC560 SPICE sets (below) and the 1N4448's (Cd40106.hpp's 1N4148 set); 300 K;
//   * the CD4069UB: Cd4069.hpp's fit to the datasheet's typical curves. Its small-signal gain at the
//     self-biased point at 12 V is -23.1 V/V (selfBiasedGain; the datasheet only bounds it, -11 to
//     -30). The Plant takes the fit as an argument: across that whole bound the pitch moves from
//     133 to 152 Hz at Tune 0 (tests/Accrual prints it);
//   * the datasheet's typical 10 pF input capacitance (the maximum is 15) at the three inverter
//     inputs in the Schmitt loop and at X, plus Q2's and Q1's Cje/Cjc. The input capacitance sets
//     how long S stays high and so how deep the reset dumps C1: it moves the absolute pitch by
//     about -5 % per +5 pF (menu: 5, 10, 15, 20 pF). It does not touch 1 V/oct;
//   * unpatched CV1/CV2/PWM are OPEN, as on the board (they are resistors to nothing).
// NOT MODELLED: temperature (the pair is Q1 PNP / Q2 NPN, so its tempco is not cancelled and a
// real board drifts); the 1.5 V, 1 us overshoot spike the reset throws on SAW (the reset is a step
// there; the spike is in the path used for the PULSE crossings); PULSE edge lag in the PWM chain
// (the oracle's edges trail the ideal threshold crossing by up to about a microsecond at 2 kHz, a
// quarter of a percent of duty); the PWM node loading Z differently on its two sides (0.04 % of
// the period at worst); supply ripple; the outputs' load; the 50 ns .. 1 us dependence of S's
// rise on the ramp current beyond the fifteen currents interpolated in log current.
//
// No Rack dependency, so tests/ drives it bare.

#include "../Mna.hpp"
#include "../Cd4069.hpp"

#include <cmath>
#include <algorithm>
#include <vector>

namespace accrual {

// --- the sheet's parts -------------------------------------------------------------------------
static const double R1 = 100e3, R2 = 1e6, R3 = 100e3, R4 = 100e3, R6 = 1500.0, R7 = 1e6;
static const double R8 = 680.0, R9 = 10e3, R10 = 22e3, R11 = 100e3, R12 = 47e3, R13 = 100e3;
static const double R14 = 100e3, R15 = 68e3, R16 = 100e3, R17 = 100e3, R18 = 10e3, R19 = 1e6, R20 = 100e3;
static const double C1 = 2.2e-9, C2 = 220e-9, C3 = 220e-9;
static const double kVcc = 12.0, kVee = -12.0;
static const double kPwPot = 100e3;                  // ASSUMED (the sheet draws only the wiper wire)
static const double kCin = 10e-12;                   // CD4069UB input capacitance, typical (TI SCHS054E)
static const double kDiodeIs = 2.52e-9;              // 1N4148/4448, as src/Cd40106.hpp
static const double kDiodeNVt = 1.752 * mna::kVt;
static const double kHpTau = 100e3 * 220e-9;         // C2 R11 = C3 R16 = 22 ms

struct Bjt { double is, bf, br, vaf, cje, cjc; };
/** BC550C, Philips SPICE set (diyaudio / mikrocontroller.net copies of the Philips file; the
    datasheet itself, Fairchild BC546-550, gives only hFE 420-800 and Cob 3.5 pF). */
inline Bjt bc550c() { Bjt q = { 7.049e-15, 493.2, 2.886, 23.89, 11.5e-12, 5.5e-12 }; return q; }
/** BC560C: Is, VAF, Cje, Cjc from the diyaudio set (IS=60f VAF=160 CJE=19p CJC=3.9p); BF and BR
    ASSUMED (520, the middle of the datasheet's 420-800 for the C group, and 4: that set's BF=900
    is outside the datasheet's range). */
inline Bjt bc560c() { Bjt q = { 60e-15, 520.0, 4.0, 160.0, 19e-12, 3.9e-12 }; return q; }

// --- the exponential converter -----------------------------------------------------------------
/** The summing node and the pair, solved exactly. `s` = sum of V_i / R_i over the patched inputs
    (the Tune and Fine pots always are), `g` = their conductances plus 1/(R21 + R6). */
struct Front {
    Bjt q1 = bc560c(), q2 = bc550c();
    double qb = 0.55, a = 0.0;      // the last solution, the next one's start
    double vb = 0.55;               // Q2's base-emitter voltage
    double ibase = 0.0;             // Is2 exp(VBE/Vt): the collector current before the Early term

    void reset() { qb = 0.55; a = 0.0; }

    void solve(double s, double g) {
        const double Vt = mna::kVt;
        double q = qb, av = a;
        for (int it = 0; it < 60; it++) {
            double e1 = std::exp(std::min((q - av) / Vt, 60.0));
            double f1 = 1.0 + (av - kVee) / q1.vaf;                       // Q1's Early term
            double ie1 = q1.is * e1 * f1 + q1.is / q1.bf * (e1 - 1.0);
            double ib1 = q1.is / q1.bf * (e1 - 1.0) - q1.is / q1.br;      // out of Q1's base into A
            double e2 = std::exp(std::min(q / Vt, 60.0));
            double ib2 = q2.is / q2.bf * (e2 - 1.0) - q2.is / q2.br;
            double F1 = (kVcc - q) / R7 - ib2 - ie1;
            double F2 = s - g * av + ib1;
            double de1 = e1 / Vt;
            double dIe1_dq = q1.is * de1 * f1 + q1.is / q1.bf * de1;
            double dIe1_da = -dIe1_dq + q1.is * e1 / q1.vaf;
            double dIb1_dq = q1.is / q1.bf * de1, dIb1_da = -dIb1_dq;
            double dIb2_dq = q2.is / q2.bf * e2 / Vt;
            double J11 = -1.0 / R7 - dIb2_dq - dIe1_dq, J12 = -dIe1_da;
            double J21 = dIb1_dq, J22 = -g + dIb1_da;
            double det = J11 * J22 - J12 * J21;
            double dq = (-F1 * J22 + F2 * J12) / det, da = (-F2 * J11 + F1 * J21) / det;
            const double lim = 0.05;
            dq = std::max(-lim, std::min(lim, dq)); da = std::max(-lim, std::min(lim, da));
            q += dq; av += da;
            if (std::fabs(dq) < 1e-12 && std::fabs(da) < 1e-12) break;
        }
        qb = q; a = av; vb = q;
        ibase = q2.is * std::exp(q / Vt);
    }
};

/** Resistance R21 that makes one volt on a CV input one octave, given which CV inputs are patched.
    (slope of A with volts = Vt ln 2, to first order; solved by bisection on the exact front end.) */
inline double calibrateTrim(bool cv1, bool cv2) {
    double lo = 0.0, hi = 5000.0;
    for (int i = 0; i < 60; i++) {
        double r = 0.5 * (lo + hi);
        double g = 1.0 / R1 + 1.0 / R2 + (cv1 ? 1.0 / R3 : 0.0) + (cv2 ? 1.0 / R4 : 0.0) + 1.0 / (r + R6);
        Front f0, f1;
        f0.solve(0.0, g);
        f1.solve(1.0 / R3, g);                     // one volt at CV1 (R3 stands in for either)
        double oct = std::log2(f1.ibase / f0.ibase);
        if (oct > 1.0) hi = r; else lo = r;
    }
    return 0.5 * (lo + hi);                         // R21, the trimmer alone
}

// --- the PWM threshold -------------------------------------------------------------------------
/** Q3, the emitter follower that carries the PWM jack to the summing node: its base and emitter
    solved exactly (2x2 Newton, warm-started), R17 loading the emitter towards `vm`. The jack is
    open if it is not patched (the base then rests on R19 and its own base current). */
struct PwmFront {
    double qpw = 0.0, qe = -0.6;
    double solve(double vm, double vpwm, bool connected) {
        const Bjt q = bc550c();
        const double Vt = mna::kVt;
        for (int it = 0; it < 60; it++) {
            double ef = std::exp(std::min((qpw - qe) / Vt, 60.0));
            double f = 1.0 + (kVcc - qpw) / q.vaf;
            double ib = q.is / q.bf * (ef - 1.0) - q.is / q.br;
            double ic = q.is * ef * f + q.is / q.br;
            double Ab = q.is / q.bf * ef / Vt, Ac = q.is * ef / Vt * f;
            double F0 = (ic + ib) - (qe - kVee) / R18 - (qe - vm) / R17;      // KCL at QE
            double F1 = (connected ? (vpwm - qpw) / R20 : 0.0) - qpw / R19 - ib;  // KCL at QPW
            double J00 = Ab + Ac - q.is * ef / q.vaf, J01 = -(Ab + Ac) - 1.0 / R18 - 1.0 / R17;
            double J10 = -(connected ? 1.0 / R20 : 0.0) - 1.0 / R19 - Ab, J11 = Ab;
            double det = J00 * J11 - J01 * J10;
            double dp = (-F0 * J11 + F1 * J01) / det, de = (-F1 * J00 + F0 * J10) / det;
            dp = std::max(-0.1, std::min(0.1, dp)); de = std::max(-0.1, std::min(0.1, de));
            qpw += dp; qe += de;
            if (std::fabs(dp) < 1e-11 && std::fabs(de) < 1e-11) break;
        }
        return qe;
    }
};
inline double pwmEmitter(double vm, double vpwm, bool connected) {
    PwmFront f;
    return f.solve(vm, vpwm, connected);
}

/** Z below which PULSE is high. `pot` 0..1 is the PW wiper's position (1 = VCC end). */
inline double pulseThreshold(double vm, double pot, double qe) {
    double voc = kVcc * pot, rth = kPwPot * pot * (1.0 - pot);
    return vm - R12 * ((voc - vm) / (R13 + rth) + (kVcc - vm) / R15 + (qe - vm) / R17);
}

// --- the integrator and its reset, tabulated -----------------------------------------------------
static const int kTab = 2048;
static const int kReset = 64;

struct Plant {
    double vdd = 12.0, vm = 6.0;
    int n = 0;
    double W[kTab], Zt[kTab], U[kTab], Psi0[kTab], Psi1[kTab];
    double P3t[kTab], P4t[kTab], St[kTab];                       // the other nodes, to start a transient from   // ascending W = Z - X
    double wTrip = 0.0, zTrip = 0.0, uTrip = 0.0;                // the fold
    double vb0 = 0.55;                                           // base voltage the Psi are for
    double fbar = 1.15;                                          // mean Early factor, for I R8
    // the reset, transient-solved at fifteen ramp currents (octave spaced) (the escape from the fold takes longer the
    // slower the ramp, and what D1/D2 leave on C1 depends a little on how hard Q2 is pulling)
    static const int kRs = 15;
    struct Reset {
        double current;          // Q2's current the transient was run at
        double dur;              // from the fold to the end of the transient, seconds
        double vc1;              // C1's voltage then, with Q2's own charge since the fold removed
        double vc1Up = 0;        // C1's voltage when S crossed VDD/2 upward: where the ramp ends
        double vc1Raw = 0, tFallRel = 0;   // (diagnostics) C1's voltage as simulated; S's fall, after S's rise
        double zr[kReset];       // Z over the reset, uniform in time from 0 to dur
    };
    Reset rs[kRs];
    double leak = 0.0;           // D2's reverse current out of X, amps (the 1N4448 model's Is)
    double fLeak = 1.2;          // the Early factor that makes I_base + leak / fLeak first-order exact
    bool ok = false;
    double vc1Ref = 0.0;         // a typical post-reset C1 voltage (1 uA), for the max-rate clamp
    double vc2 = 0.0;            // the voltage C2 settles to (the mean of Z)

    /** The reset for a ramp current: duration and C1 voltage interpolated in log(I), the path of
        Z from the nearest run. */
    void resetAt(double i, double& dur, double& vc1, double& vUp, const double*& path) const {
        double x = std::log(i);
        int k = 0;
        while (k < kRs - 2 && x > std::log(rs[k + 1].current)) k++;
        double x0 = std::log(rs[k].current), x1 = std::log(rs[k + 1].current);
        double f = (x - x0) / (x1 - x0);
        f = std::max(0.0, std::min(1.0, f));
        dur = std::exp(std::log(rs[k].dur) + f * (std::log(rs[k + 1].dur) - std::log(rs[k].dur)));
        vc1 = rs[k].vc1 + f * (rs[k + 1].vc1 - rs[k].vc1);
        vUp = rs[k].vc1Up + f * (rs[k + 1].vc1Up - rs[k].vc1Up);
        path = (f < 0.5 ? rs[k] : rs[k + 1]).zr;
    }

    // ---- lookups ----
    int find(const double* a, double x) const {
        int lo = 0, hi = n - 1;
        if (x <= a[0]) return 0;
        if (x >= a[n - 1]) return n - 2;
        while (hi - lo > 1) { int m = (lo + hi) >> 1; if (a[m] <= x) lo = m; else hi = m; }
        return lo;
    }
    double interp(const double* y, const double* x, int i, double xv) const {
        double d = x[i + 1] - x[i];
        double t = d > 0 ? (xv - x[i]) / d : 0.0;
        return y[i] + (y[i + 1] - y[i]) * t;
    }
    double zOfW(double w) const { int i = find(W, w); return interp(Zt, W, i, w); }
    double uOfW(double w) const { int i = find(W, w); return interp(U, W, i, w); }
    double psi(double w, double dvb = 0.0) const {
        int i = find(W, w);
        return interp(Psi0, W, i, w) + dvb * interp(Psi1, W, i, w);
    }
    /** W for a phase (inverse of psi at vb0). */
    double wOfPsi(double ph, double dvb = 0.0) const {
        // Psi0 + dvb Psi1 is monotone in W
        int lo = 0, hi = n - 1;
        if (dvb == 0.0) {
            if (ph <= Psi0[0]) return W[0];
            if (ph >= Psi0[n - 1]) return W[n - 1];
            while (hi - lo > 1) { int m = (lo + hi) >> 1; if (Psi0[m] <= ph) lo = m; else hi = m; }
            double d = Psi0[lo + 1] - Psi0[lo];
            return W[lo] + (W[lo + 1] - W[lo]) * (d > 0 ? (ph - Psi0[lo]) / d : 0.0);
        }
        double c0 = Psi0[0] + dvb * Psi1[0], c1 = Psi0[n - 1] + dvb * Psi1[n - 1];
        if (ph <= c0) return W[0];
        if (ph >= c1) return W[n - 1];
        while (hi - lo > 1) { int m = (lo + hi) >> 1; if (Psi0[m] + dvb * Psi1[m] <= ph) lo = m; else hi = m; }
        double a0 = Psi0[lo] + dvb * Psi1[lo], a1 = Psi0[lo + 1] + dvb * Psi1[lo + 1];
        return W[lo] + (W[lo + 1] - W[lo]) * (a1 > a0 ? (ph - a0) / (a1 - a0) : 0.0);
    }
    /** W at which the inverter's output is `z` (Z is monotone in W). */
    double wOfZ(double z) const {
        int i = find(Zt, z);
        return interp(W, Zt, i, z);
    }

    // ---- construction ----
    /** The S-low equilibrium of the integrator inverter and its loads, X held at `u`. */
    struct Dc {
        mna::Circuit c;
        enum { Z, P3, P4, S };
        int d2 = -1;
        void build(double vdd, double vm, double vc2, const cd4069::Params& q = cd4069::typical()) {
            c = mna::Circuit();
            c.n = 4;
            const int X = mna::fixed(0), VDD = mna::fixed(1), VM = mna::fixed(2), VC2 = mna::fixed(3);
            cd4069::addInverter(c, X, Z, VDD, mna::GND, q);           // U1.1
            c.addResistor(Z, P3, R9);
            cd4069::addInverter(c, P3, P4, VDD, mna::GND, q);         // U1.2
            cd4069::addInverter(c, P4, S, VDD, mna::GND, q);          // U1.3
            c.addResistor(S, P3, R10);
            d2 = c.addDiode(S, X, kDiodeIs, kDiodeNVt);               // D2
            c.addResistor(Z, VM, R12);                                // the PWM node, at its self-bias
            c.addResistor(Z, VC2, R11);                               // C2 R11: R11 to the voltage C2 holds
            c.fixedV[0] = vdd - 0.5; c.fixedV[1] = vdd; c.fixedV[2] = vm; c.fixedV[3] = vc2;
        }
    };

    cd4069::Params inv = cd4069::typical();        // the inverter's fitted device parameters
    void build(double vddIn, double cinF = kCin, const cd4069::Params& q = cd4069::typical());
    bool runReset(double I, double cin, Reset& out);
};

// --- a small adaptive transient driver (also used by the oracle in tests) ----------------------
template <class C>
struct Transient {
    C* c;
    double t = 0.0, h = 1e-9, hMin = 2e-11, hMax = 50e-9, dvMax = 0.08;
    explicit Transient(C* cc) : c(cc) {}
    /** One accepted step; returns false if it could not converge at hMin. */
    bool advance(double tEnd) {
        for (int tries = 0; tries < 60; tries++) {
            C save = *c;
            double hh = std::min(h, std::max(tEnd - t, hMin));
            c->h = hh;
            int it = c->step(30);
            double worst = 0.0;
            for (int i = 0; i < c->n; i++) worst = std::max(worst, std::fabs(c->v[i] - save.v[i]));
            if (it < 0 || worst > dvMax) {
                if (hh <= hMin * 1.0001) { if (it >= 0) { t += hh; return true; } *c = save; return false; }
                *c = save; h = hh * 0.5; continue;
            }
            t += hh;
            if (worst < 0.2 * dvMax) h = std::min(hMax, hh * 1.5);
            else h = hh;
            return true;
        }
        return false;
    }
};

inline void Plant::build(double vddIn, double cinF, const cd4069::Params& q) {
    vdd = vddIn;
    inv = q;
    vm = cd4069::selfBias(vdd, inv);
    ok = false;
    // ---- 1. the DC curve, X swept downward from the top rail until the Schmitt trips ----
    double vc2 = vm;                       // the mean of Z: C2's charge; iterated below
    for (int pass = 0; pass < 3; pass++) {
    Dc dc; dc.build(vdd, vm, vc2, inv);
    const double target[4] = { vdd - 0.5, vdd, vm, vc2 };
    dc.c.solveDc(target, 4, 40);
    n = 0;
    double u = vdd - 0.5;
    double leakMid = 0.0;
    auto rec = [&](double uu) {
        if (n >= kTab) return;
        W[n] = dc.c.v[Dc::Z] - uu; Zt[n] = dc.c.v[Dc::Z]; U[n] = uu;
        P3t[n] = dc.c.v[Dc::P3]; P4t[n] = dc.c.v[Dc::P4]; St[n] = dc.c.v[Dc::S];
        if (uu > 0.5 * vdd + 1.0) leakMid = -dc.c.diodeCurrent(dc.d2);
        n++;
    };
    rec(u);
    double du = 0.02;
    Dc keep = dc;
    while (u > 0.3 && n < kTab - 1) {
        Dc save = dc;
        double un = u - du;
        dc.c.fixedV[0] = un;
        bool good = dc.c.solveHere(60) && dc.c.v[Dc::S] < 0.5 * vdd;
        double wn = dc.c.v[Dc::Z] - un;
        if (!good) {
            dc = save;
            if (du < 2e-8) break;
            du *= 0.5; continue;
        }
        if (std::fabs(wn - W[n - 1]) > 0.03 && du > 1e-7) { dc = save; du *= 0.5; continue; }
        u = un; rec(u); keep = dc;
        if (std::fabs(wn - W[n - 1 > 0 ? n - 2 : 0]) < 0.02) du = std::min(du * 1.5, 0.02);
    }
    if (n < 16) return;
    wTrip = W[n - 1]; zTrip = Zt[n - 1]; uTrip = U[n - 1];
    leak = leakMid;
    // the time-mean of Z over a cycle (weights dW / f, the same for every ramp rate), starting
    // from a typical post-reset C1 voltage: what C2 charges to
    {
        double a = 0.0, b = 0.0; bool first = true;
        for (int k = 1; k < n; k++) {
            if (W[k] < -2.85) continue;
            (void)first;
            double dW = W[k] - W[k - 1];
            double fa = 1.0 + (U[k - 1] - vb0) / bc550c().vaf, fb = 1.0 + (U[k] - vb0) / bc550c().vaf;
            a += 0.5 * dW * (Zt[k - 1] / fa + Zt[k] / fb);
            b += 0.5 * dW * (1.0 / fa + 1.0 / fb);
        }
        vc2 = a / b;
    }
    }                                       // pass
    this->vc2 = vc2;
    // ---- 2. the phase tables ----
    const double vaf = bc550c().vaf;
    Psi0[0] = Psi1[0] = 0.0;
    double favg = 0.0, wsum = 0.0;
    for (int k = 1; k < n; k++) {
        double dW = W[k] - W[k - 1];
        double f0a = 1.0 + (U[k - 1] - vb0) / vaf, f0b = 1.0 + (U[k] - vb0) / vaf;
        Psi0[k] = Psi0[k - 1] + 0.5 * dW * (1.0 / f0a + 1.0 / f0b);
        // d/dvb of 1/f = +1/(vaf f^2) (f falls as vb rises, 1/f rises)
        Psi1[k] = Psi1[k - 1] + 0.5 * dW * (1.0 / (vaf * f0a * f0a) + 1.0 / (vaf * f0b * f0b));
        favg += 0.5 * dW * (f0a + f0b); wsum += dW;
    }
    fbar = favg / wsum;
    // ---- 3. the reset: a transient of the integrator + Schmitt at 10 ns, at fifteen currents ----
    static const double currents[kRs] = { 0.03e-6, 0.1e-6, 0.2e-6, 0.5e-6, 1e-6, 2e-6, 5e-6, 10e-6,
                                           20e-6, 40e-6, 80e-6, 160e-6, 320e-6, 640e-6, 1280e-6 };
    for (int r = 0; r < kRs; r++)
        if (!runReset(currents[r], cinF, rs[r])) return;
    vc1Ref = rs[4].vc1;
    // ---- 4. the leak's first-order Early factor: int dW/f over int dW/f^2, over the used span ----
    {
        double s1 = 0.0, s2 = 0.0;
        for (int k = 1; k < n; k++) {
            if (W[k] < vc1Ref) continue;
            double dW = W[k] - W[k - 1];
            double fa = 1.0 + (U[k - 1] - vb0) / vaf, fb = 1.0 + (U[k] - vb0) / vaf;
            s1 += 0.5 * dW * (1.0 / fa + 1.0 / fb);
            s2 += 0.5 * dW * (1.0 / (fa * fa) + 1.0 / (fb * fb));
        }
        fLeak = s1 / s2;
    }
    ok = true;
}

inline bool Plant::runReset(double I, double cin, Reset& out) {
    typedef mna::CircuitT<16, 40, 6> Net;
    Net c; c.n = 9;
    enum { X, Y, Z, P3, P4, S, SAW, QBN, A_ };
    const int VDD = mna::fixed(0), VM = mna::fixed(1), VEE = mna::fixed(2), VCC = mna::fixed(3), VSRC = mna::fixed(4);
    cd4069::addInverter(c, X, Z, VDD, mna::GND, inv);
    const int cap1 = c.addCapacitor(Y, X, C1);
    c.addResistor(Y, Z, R8);
    c.addDiode(Y, Z, kDiodeIs, kDiodeNVt);                      // D1
    c.addResistor(Z, P3, R9);
    cd4069::addInverter(c, P3, P4, VDD, mna::GND, inv);
    cd4069::addInverter(c, P4, S, VDD, mna::GND, inv);
    c.addResistor(S, P3, R10);
    c.addDiode(S, X, kDiodeIs, kDiodeNVt);                      // D2
    c.addResistor(Z, VM, R12);
    const int cap2 = c.addCapacitor(Z, SAW, C2);
    c.addResistor(SAW, mna::GND, R11);
    // Q2 and the front end around its base, as drawn: Q1, R7, the summing node held by the trimmer
    // R6 + R21 (400 ohm, calibrated) and the four input resistors to a source whose value sets
    // Q2's collector current to I at the fold; and the junction capacitances. In a microsecond the
    // reset kicks Q2's base through Cjc and cuts Q2 off for a moment, and that is charge.
    const Bjt q2 = bc550c(), q1 = bc560c();
    const double gin = 1.0 / R1 + 1.0 / R2 + 1.0 / R3 + 1.0 / R4, rg = 400.0 + R6;
    c.addPnp(VEE, A_, QBN, q1.is, q1.bf, q1.br, q1.vaf);
    c.addResistor(QBN, VCC, R7);
    c.addNpn(X, QBN, mna::GND, q2.is, q2.bf, q2.br, q2.vaf);
    c.addResistor(A_, mna::GND, rg);
    c.addResistor(A_, VSRC, 1.0 / gin);
    c.addCapacitor(QBN, A_, q1.cje);
    c.addCapacitor(A_, VEE, q1.cjc);
    c.addCapacitor(QBN, mna::GND, q2.cje);
    c.addCapacitor(QBN, X, q2.cjc);
    c.addCapacitor(P3, mna::GND, cin);
    c.addCapacitor(P4, mna::GND, cin);
    c.addCapacitor(X, mna::GND, cin);
    // find the source: bisection on the exact front end for collector current I at the fold's X
    const double x0 = U[std::max(0, n - 1)];
    Front fr;
    double lo = -12.0, hi = 12.0;
    for (int it = 0; it < 100; it++) {
        double vs = 0.5 * (lo + hi);
        fr.solve(vs * gin, gin + 1.0 / rg);
        double ic = fr.ibase * (1.0 + (x0 - fr.vb) / q2.vaf);
        if (ic > I) hi = vs; else lo = vs;
    }
    const double vsrc = 0.5 * (lo + hi);
    fr.solve(vsrc * gin, gin + 1.0 / rg);
    c.fixedV[0] = vdd; c.fixedV[1] = vm; c.fixedV[2] = kVee; c.fixedV[3] = kVcc; c.fixedV[4] = vsrc;
    const double vbq = fr.qb, va0 = fr.a;
    // start a little (>= 20 mV, up to 200 us of ramp) below the fold, on the S-low branch, and let the ramp walk in
    int k0 = n - 1;
    const double lead = std::max(0.02, std::min(0.12, 200e-6 * I * 1.2 / C1));   // volts below the fold
    while (k0 > 0 && W[k0] > wTrip - lead) k0--;
    const double zt = Zt[k0];
    c.v[X] = U[k0]; c.v[Z] = zt; c.v[Y] = zt - I * R8;
    c.v[P3] = P3t[k0]; c.v[P4] = P4t[k0]; c.v[S] = St[k0]; c.v[QBN] = vbq; c.v[A_] = va0;
    c.v[SAW] = zt - vc2;
    for (int i = 0; i < c.ne; i++) { c.e[i].vPrev = 0.0; c.e[i].iPrev = 0.0; }
    c.e[cap1].vPrev = c.v[Y] - c.v[X]; c.e[cap1].iPrev = I;
    c.e[cap2].vPrev = zt - c.v[SAW]; c.e[cap2].iPrev = c.v[SAW] / R11;
    for (int i = 0; i < c.ne; i++) {
        if (c.e[i].kind != Net::CAP || i == cap1 || i == cap2) continue;
        c.e[i].vPrev = c.volt(c.e[i].a) - c.volt(c.e[i].b);
    }
    Transient<Net> tr(&c);
    tr.h = 1e-10; tr.hMax = 100e-9;
    std::vector<double> ts, zs, vcs;
    double tFall = -1.0, tFold = -1.0, vUp = 0.0;
    bool up = false;
    const double tMax = 8e-3;
    ts.push_back(0.0); zs.push_back(c.v[Z]); vcs.push_back(c.v[Y] - c.v[X]);
    while (tr.t < tMax) {
        if (!tr.advance(tMax)) return false;
        ts.push_back(tr.t); zs.push_back(c.v[Z]); vcs.push_back(c.v[Y] - c.v[X]);
        if (!up && c.v[S] > 0.5 * vdd) { up = true; tFold = tr.t; vUp = c.v[Y] - c.v[X]; }
        if (up && tFall < 0 && c.v[S] < 0.5 * vdd) tFall = tr.t;
        if (tFall >= 0 && tr.t > tFall + 4e-6) break;           // past Q2's recovery from the Cjc kick
    }
    if (tFall < 0 || tFold < 0) return false;
    const double tEnd = ts.back();
    out.current = I;
    out.dur = tEnd - tFold;
    out.vc1 = vcs.back() - I * (tEnd - tFold) / C1;
    out.vc1Raw = vcs.back(); out.tFallRel = tFall - tFold; out.vc1Up = vUp;
    size_t j = 0;
    for (int k = 0; k < kReset; k++) {
        double tt = tFold + out.dur * k / (kReset - 1);
        while (j + 2 < ts.size() && ts[j + 1] < tt) j++;
        double d = ts[j + 1] - ts[j];
        double f = d > 0 ? (tt - ts[j]) / d : 0.0;
        out.zr[k] = zs[j] + (zs[j + 1] - zs[j]) * f;
    }
    return true;
}

// --- one voice, at the audio rate ----------------------------------------------------------------
struct Edge { double at; double h; };            // seconds into the sample, jump in volts

struct Voice {
    const Plant* pl = nullptr;
    Front front;
    double phi = 0.0;               // ramp phase: Psi of C1's voltage
    bool inReset = false;
    double resetPos = 0.0;          // seconds into the reset
    double curDur = 0.0, curVc1 = 0.0; const double* curPath = nullptr;   // the reset in progress
    bool stepDone = false;          // the saw's step (at the reset's middle) has been emitted
    double iPrev = 0.0;             // Q2's base current at the previous sample's end
    bool pulseHigh = false, primed = false;
    bool bandLimit = true;          // band-limit the edges (off only in tests, to show what it does)
    double sawPrev = 0, sawCarry = 0, pulsePrev = 0, pulseCarry = 0;   // polyBLEP, one sample behind
    double sawLp = 0, pulseLp = 0;  // the C2 R11 and C3 R16 high-passes
    double clock = 0.0, lastTrip = -1.0, period = 0.0;
    double zNow = 0.0;              // Z at the end of the last sample (the integrator's output)
    double highTime = 0.0;          // seconds PULSE has been high, from the exact edge times (diagnostic)
    Edge sawE[8], pulE[24];
    int nS = 0, nP = 0;

    void setPlant(const Plant* p) { pl = p; reset(); }
    void reset() {
        front.reset(); inReset = false; resetPos = 0; primed = false; phi = 0; iPrev = 0;
        clock = 0; lastTrip = -1; period = 0; sawLp = pulseLp = 0;
    }
    double frequency() const { return period > 0.0 ? 1.0 / period : 0.0; }

    /** Advance one sample of `dt` seconds. `ibase` is Q2's base current at the END of the sample
        (the front end solved there), `vb` its base voltage, `zth` the PULSE threshold. Writes the
        two outputs in volts (AC coupled, one sample behind the circuit). */
    void process(double dt, double ibase, double vb, double zth, double& saw, double& pulse) {
        const Plant& P = *pl;
        const double dvb = vb - P.vb0;
        const double vaf = bc550c().vaf;
        ibase += P.leak / P.fLeak;                               // D2's reverse current, first-order exact
        const double fTrip = 1.0 + (P.uTrip - vb) / vaf;
        // the oscillator will not run faster than it can be sampled: a cycle (the ramp plus the reset)
        // is kept to about 2 samples, just under half the sampling rate
        {
            double span = P.psi(P.rs[4].vc1Up, dvb) - P.psi(P.vc1Ref, dvb);
            double rampMin = std::max(0.35 * dt, 2.2 * dt - P.rs[4].dur);
            double imax = span * C1 / rampMin;
            if (ibase > imax) ibase = imax;
        }
        if (!primed) {
            primed = true;
            double d0, v0, u0; const double* pth;
            P.resetAt(ibase * (1.0 + (P.uTrip - vb) / vaf), d0, v0, u0, pth);
            phi = P.psi(v0, dvb);
            iPrev = ibase;
            pulseHigh = P.zOfW(v0) < zth;
            sawLp = 0.5 * (P.zTrip + P.zOfW(v0));
            pulseLp = 0.5 * P.vdd;
        }
        const double I0 = iPrev, I1 = ibase;
        const double lr = std::log(I1 / I0);
        const bool flat = std::fabs(lr) < 1e-9;
        auto charge = [&](double a, double b) {
            if (flat) return I0 * (b - a) / C1;
            return I0 * (std::exp(lr * b / dt) - std::exp(lr * a / dt)) * dt / lr / C1;
        };
        auto timeFor = [&](double a, double q) {
            if (flat) return a + q * C1 / I0;
            double v = std::exp(lr * a / dt) + q * C1 * lr / (I0 * dt);
            if (v <= 0.0) return 1e30;
            return dt * std::log(v) / lr;
        };
        const double dNow = R8 * I1 * P.fbar;
        double dT, vT, vUp; const double* pT;
        P.resetAt(I1 * fTrip, dT, vT, vUp, pT);                  // where S crosses at this ramp current
        const double phiTrip = P.psi(vUp, dvb);
        nS = nP = 0;
        const bool highAtStart = pulseHigh;
        auto pulseEdge = [&](double at, bool toHigh) {
            if (nP < 24) { pulE[nP].at = at; pulE[nP].h = toHigh ? P.vdd : -P.vdd; nP++; }
            pulseHigh = toHigh;
        };
        double t = 0.0;
        for (int guard = 0; guard < 8 && t < dt * (1.0 - 1e-12); guard++) {
            if (inReset) {
                const double take = std::min(curDur - resetPos, dt - t);
                // the saw's step, at the middle of the reset
                const double mid = 0.5 * curDur;
                if (!stepDone && resetPos + take >= mid) {
                    double at = t + (mid - resetPos);
                    if (nS < 8) { sawE[nS].at = at; sawE[nS].h = P.zOfW(curVc1 + I1 * curDur / C1 + dNow) - P.zTrip; nS++; }
                    stepDone = true;
                }
                // the pulse's crossings on the stored path of Z
                const double dk = curDur / (kReset - 1);
                int k0 = (int)(resetPos / dk); if (k0 > kReset - 2) k0 = kReset - 2;
                for (int k = k0; k < kReset - 1; k++) {
                    double a = std::max(resetPos, k * dk), b = std::min(resetPos + take, (k + 1) * dk);
                    if (b <= a) { if (k * dk > resetPos + take) break; continue; }
                    double za = curPath[k] + (curPath[k + 1] - curPath[k]) * ((a - k * dk) / dk);
                    double zb = curPath[k] + (curPath[k + 1] - curPath[k]) * ((b - k * dk) / dk);
                    bool wantA = za < zth, wantB = zb < zth;
                    if (wantA != pulseHigh) pulseEdge(t + (a - resetPos), wantA);
                    if (wantB != pulseHigh) {
                        double f = (zth - za) / (zb - za);
                        pulseEdge(t + (a - resetPos) + f * (b - a), wantB);
                    }
                }
                resetPos += take; t += take;
                if (resetPos >= curDur - 1e-15) {
                    inReset = false; stepDone = false;
                    // Q2 kept pulling through the reset: its charge is added back in phase units
                    phi = P.psi(curVc1, dvb) + I1 * curDur / C1;
                }
                continue;
            }
            // ---- a ramp segment from t ----
            const double wNow = P.wOfPsi(phi, dvb);
            const double zStart = P.zOfW(wNow + dNow);
            if ((zStart < zth) != pulseHigh) pulseEdge(t, zStart < zth);
            const double need = phiTrip - phi;
            const double q = charge(t, dt);
            double qEnd = std::min(q, need);
            if (pulseHigh && zth > zStart && zth < P.zTrip) {
                double phTh = P.psi(P.wOfZ(zth) - dNow, dvb);
                if (phTh > phi && phTh <= phi + qEnd)
                    pulseEdge(timeFor(t, phTh - phi), false);
            }
            if (q < need) { phi += q; t = dt; break; }
            // the Schmitt trips
            const double tt = std::min(timeFor(t, need), dt);
            phi = phiTrip;
            t = tt;
            const double absT = clock + tt;
            if (lastTrip >= 0.0) period = absT - lastTrip;
            lastTrip = absT;
            inReset = true; resetPos = 0.0; stepDone = false;
            curDur = dT; curVc1 = vT; curPath = pT;
        }
        {   // the exact time PULSE was high in this sample, from the edges
            bool st = highAtStart; double tcur = 0.0;
            for (int k = 0; k < nP; k++) {
                if (st) highTime += pulE[k].at - tcur;
                tcur = pulE[k].at; st = pulE[k].h > 0.0;
            }
            if (st) highTime += dt - tcur;
        }
        clock += dt;
        iPrev = I1;
        // ---- the naive values at the end of the sample ----
        double zEnd;
        if (inReset) zEnd = resetPos < 0.5 * curDur ? P.zTrip : P.zOfW(curVc1 + I1 * curDur / C1 + dNow);
        else zEnd = P.zOfW(P.wOfPsi(phi, dvb) + dNow);
        zNow = zEnd;
        const double pulseNaive = pulseHigh ? P.vdd : 0.0;
        auto blep = [&](double naive, const Edge* e, int ne, double& pn, double& pc) {
            double pre = 0.0, carry = 0.0;
            for (int k = 0; k < ne; k++) {
                double dd = (dt - e[k].at) / dt;
                if (dd < 0) dd = 0; if (dd > 1) dd = 1;
                double half = bandLimit ? 0.5 * e[k].h : 0.0;
                pre += half * dd * dd;
                carry -= half * (1.0 - dd) * (1.0 - dd);
            }
            double out = pn + pc + pre;
            pn = naive; pc = carry;
            return out;
        };
        double sawX = blep(zEnd, sawE, nS, sawPrev, sawCarry);
        double pulX = blep(pulseNaive, pulE, nP, pulsePrev, pulseCarry);
        const double al = 1.0 - std::exp(-dt / kHpTau);
        sawLp += al * (sawX - sawLp);  saw = sawX - sawLp;
        pulseLp += al * (pulX - pulseLp);  pulse = pulX - pulseLp;
    }
};

} // namespace accrual

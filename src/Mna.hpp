#pragma once
// A small nonlinear circuit solver: modified nodal analysis, Newton's method, trapezoidal
// capacitors. Built for boards of a dozen nodes that run in real time, one sample at a
// time, so it has no allocation, a fixed size, and starts each step from the last answer.
//
// Nodes are numbered 0..n-1. `GND` is ground; the ideal voltage sources (the signal, the
// supply, a control voltage) are "fixed" nodes, `fixed(k)`, whose value is set before each
// step. Every element connects any of these.
//
//   resistor   R(a, b, ohms)
//   capacitor  C(a, b, farads)            trapezoidal, 2C/h companion
//   diode      D(anode, cathode, Is, nVt)  Shockley
//   NPN        Q(c, b, e, Is, BF, BR, VAF) Ebers-Moll transport form with the Early effect
//   zener      Z(anode, cathode, Is, nVt, BV, IBV, nVtBr) a diode with SPICE-style reverse breakdown
//   current    I(a, b)                      a source whose value the caller sets every step
//   N-JFET     J(d, g, s, beta, Vto, lambda) Shichman-Hodges channel, symmetric in d and s
//              (the gate junctions are separate diodes, so a caller decides which to fit)
//
// The junction exponentials continue linearly past 40 thermal voltages, so a bad guess
// cannot overflow, and each Newton update is limited to 1 V so it cannot throw a junction
// to the other side of the world.
//
// No Rack dependency, so tests/ drives it bare.

#include <cmath>
#include <complex>

namespace mna {

static const int kMaxNodes = 16;
static const int kMaxElems = 40;
static const int kMaxFixed = 4;
static const int kCols = 16 + 1 + 4;      // kMaxNodes + rhs + kMaxFixed
static const int GND = -1;
inline int fixed(int k) { return -2 - k; }

static const double kVt = 0.025852;     // kT/q at 300 K

/** exp(x) that is linear beyond 40, and its derivative. */
inline double expLim(double x, double& d) {
    if (x > 40.0) {
        static const double e40 = std::exp(40.0);
        d = e40;
        return e40 * (1.0 + x - 40.0);
    }
    double e = std::exp(x);
    d = e;
    return e;
}

/** The square-law channel's drain current for vgs, vds >= 0, and its slopes. */
inline double jfetId(double vgs, double vds, double beta, double vto, double lambda,
                     double& gm, double& gds) {
    double vov = vgs - vto;
    if (vov <= 0.0) { gm = gds = 0.0; return 0.0; }
    double cl = 1.0 + lambda * vds;
    if (vds < vov) {                               // triode
        double core = vds * (2.0 * vov - vds);
        gm = beta * 2.0 * vds * cl;
        gds = beta * (2.0 * (vov - vds) * cl + core * lambda);
        return beta * core * cl;
    }
    gm = beta * 2.0 * vov * cl;                    // saturation
    gds = beta * vov * vov * lambda;
    return beta * vov * vov * cl;
}

struct Circuit {
    int n = 0;                          // unknown nodes
    double fixedV[kMaxFixed] = {};
    double v[kMaxNodes] = {};           // node voltages (the solution)
    double gmin = 1e-12;                // to ground at every node, so a floating one is defined

    enum Kind { RES, CAP, DIODE, NPN, NJFET, ZENER, ISRC };
    struct Elem {
        Kind kind;
        int a, b, c;                    // terminals (NPN: collector, base, emitter)
        double p0, p1, p2, p3, p4;      // R: ohms | C: farads | D: Is, nVt | NPN: Is, BF, BR, VAF
        double vPrev, iPrev;            // capacitor's state
    };
    Elem e[kMaxElems];
    int ne = 0;

    double h = 1.0;                     // the time step, seconds; 0 means "DC" (capacitors open)
    bool dc = true;

    int addResistor(int a, int b, double ohms) { return add({ RES, a, b, 0, 1.0 / ohms, 0, 0, 0, 0, 0, 0 }); }
    int addCapacitor(int a, int b, double farads) { return add({ CAP, a, b, 0, farads, 0, 0, 0, 0, 0, 0 }); }
    int addDiode(int a, int k, double is, double nvt) { return add({ DIODE, a, k, 0, is, nvt, 0, 0, 0, 0, 0 }); }
    int addNpn(int c, int b, int em, double is, double bf, double br, double vaf) {
        return add({ NPN, c, b, em, is, bf, br, vaf, 0, 0, 0 });
    }
    int addJfet(int d, int g, int src, double beta, double vto, double lambda) {
        return add({ NJFET, d, g, src, beta, vto, lambda, 0, 0, 0, 0 });
    }
    /** A diode whose reverse current is Ibv * exp(-(v + BV) / nVtBr) (SPICE's breakdown: the
        junction passes `ibv` amps at -BV volts and gains an e-fold every nVtBr). */
    int addZener(int a, int k, double is, double nvt, double bv, double ibv, double nvtBr) {
        return add({ ZENER, a, k, 0, is, nvt, bv, ibv, nvtBr, 0, 0 });
    }
    /** A current source from a to b; its value is set with setCurrent() before each step. */
    int addCurrent(int a, int b) { return add({ ISRC, a, b, 0, 0, 0, 0, 0, 0, 0, 0 }); }
    void setCurrent(int idx, double amps) { e[idx].p0 = amps; }
    /** The current through a diode or zener, anode to cathode, at the present solution. */
    double diodeCurrent(int idx) const {
        const Elem& el = e[idx];
        double vd = volt(el.a) - volt(el.b), d;
        double i = el.p0 * (expLim(vd / el.p1, d) - 1.0);
        if (el.kind == ZENER) i -= el.p3 * expLim(-(vd + el.p2) / el.p4, d);
        return i;
    }
    void setResistor(int idx, double ohms) { e[idx].p0 = 1.0 / ohms; }

    double volt(int node) const {
        if (node >= 0) return v[node];
        if (node == GND) return 0.0;
        return fixedV[-2 - node];
    }

    /** Solve for the operating point with the capacitors open, ramping the sources from
        zero in `steps` stages so Newton always has a close start. Leaves each capacitor
        charged to what it sees. `target` are the fixed voltages to arrive at. */
    bool solveDc(const double* target, int nFixed, int steps = 24) {
        dc = true;
        for (int i = 0; i < n; i++) v[i] = 0.0;
        bool ok = true;
        for (int s = 1; s <= steps; s++) {
            for (int k = 0; k < nFixed; k++) fixedV[k] = target[k] * s / steps;
            ok = newton(80) && ok;
        }
        for (int i = 0; i < ne; i++)
            if (e[i].kind == CAP) { e[i].vPrev = volt(e[i].a) - volt(e[i].b); e[i].iPrev = 0.0; }
        dc = false;
        return ok;
    }

    /** One time step of `h` seconds with the fixed voltages as set. Returns the Newton
        iterations used (negative if it did not converge in the allowance). */
    int step(int maxIter = 25) {
        dc = false;
        int it = newtonIter(maxIter);
        for (int i = 0; i < ne; i++) {
            Elem& el = e[i];
            if (el.kind != CAP) continue;
            double g = 2.0 * el.p0 / h;
            double vc = volt(el.a) - volt(el.b);
            el.iPrev = g * (vc - el.vPrev) - el.iPrev;
            el.vPrev = vc;
        }
        return it;
    }

    /** Small-signal gain at `freq` Hz from the fixed source `drive` to node `out`, about
        the current operating point: the Jacobian's conductances plus j*omega*C. */
    std::complex<double> acGain(double freq, int drive, int out) {
        typedef std::complex<double> cx;
        double J[kMaxNodes][kCols];
        for (int i = 0; i < n; i++) for (int j = 0; j < kCols; j++) J[i][j] = 0.0;
        for (int i = 0; i < n; i++) J[i][i] += gmin;
        bool wasDc = dc;
        dc = true;                                 // capacitors out of the real part
        for (int k = 0; k < ne; k++) stamp(e[k], J);
        dc = wasDc;
        cx A[kMaxNodes][kMaxNodes + 1];
        double w = 2.0 * 3.14159265358979323846 * freq;
        for (int i = 0; i < n; i++) {
            for (int j = 0; j < n; j++) A[i][j] = cx(J[i][j], 0.0);
            A[i][n] = cx(-J[i][kMaxNodes + 1 + drive], 0.0);     // -dF/dVfixed
        }
        for (int k = 0; k < ne; k++) {
            if (e[k].kind != CAP) continue;
            double c = w * e[k].p0;
            int a = e[k].a, b = e[k].b;
            if (a >= 0) { A[a][a] += cx(0, c); if (b >= 0) A[a][b] -= cx(0, c); }
            if (b >= 0) { A[b][b] += cx(0, c); if (a >= 0) A[b][a] -= cx(0, c); }
            // a capacitor to a fixed node would also drive the rhs; none here ties to a source
        }
        for (int c0 = 0; c0 < n; c0++) {
            int piv = c0;
            for (int r = c0 + 1; r < n; r++) if (std::abs(A[r][c0]) > std::abs(A[piv][c0])) piv = r;
            if (piv != c0) for (int j = c0; j <= n; j++) { cx t = A[c0][j]; A[c0][j] = A[piv][j]; A[piv][j] = t; }
            for (int r = c0 + 1; r < n; r++) {
                cx f = A[r][c0] / A[c0][c0];
                for (int j = c0; j <= n; j++) A[r][j] -= f * A[c0][j];
            }
        }
        for (int r = n - 1; r >= 0; r--) {
            cx sum = A[r][n];
            for (int j = r + 1; j < n; j++) sum -= A[r][j] * A[j][n];
            A[r][n] = sum / A[r][r];
        }
        return A[out][n];
    }

private:
    int add(const Elem& x) { e[ne] = x; return ne++; }

    bool newton(int maxIter) { return newtonIter(maxIter) > 0; }

    int newtonIter(int maxIter) {
        for (int it = 1; it <= maxIter; it++) {
            double J[kMaxNodes][kCols];
            for (int i = 0; i < n; i++) {
                for (int j = 0; j < kCols; j++) J[i][j] = 0.0;
                J[i][i] += gmin;
                J[i][n] -= gmin * v[i];        // residual F = sum of currents leaving; rhs = -F
            }
            for (int k = 0; k < ne; k++) stamp(e[k], J);
            if (!solve(J)) return -it;
            double worst = 0.0;
            for (int i = 0; i < n; i++) {
                double dx = J[i][n];
                if (dx > 1.0) dx = 1.0;
                if (dx < -1.0) dx = -1.0;
                v[i] += dx;
                if (std::fabs(dx) > worst) worst = std::fabs(dx);
            }
            if (worst < 1e-6) return it;
        }
        return -maxIter;
    }

    /** The matrix column for a node's voltage: its own, a fixed source's, or none. */
    static int col(int node) {
        if (node >= 0) return node;
        if (node == GND) return -1;
        return kMaxNodes + 1 + (-2 - node);
    }

    void branch(double J[][kCols], int a, int b, double g, double iab) {
        // a current iab flows a -> b, with slope g w.r.t. (Va - Vb)
        int ca = col(a), cb = col(b);
        if (a >= 0) {
            J[a][n] -= iab;
            if (ca >= 0) J[a][ca] += g;
            if (cb >= 0) J[a][cb] -= g;
        }
        if (b >= 0) {
            J[b][n] += iab;
            if (cb >= 0) J[b][cb] += g;
            if (ca >= 0) J[b][ca] -= g;
        }
    }

    void stamp(const Elem& el, double J[][kCols]) {
        switch (el.kind) {
            case RES: {
                double vab = volt(el.a) - volt(el.b);
                branch(J, el.a, el.b, el.p0, el.p0 * vab);
                break;
            }
            case CAP: {
                if (dc) break;
                double g = 2.0 * el.p0 / h;
                double vab = volt(el.a) - volt(el.b);
                double i = g * (vab - el.vPrev) - el.iPrev;
                branch(J, el.a, el.b, g, i);
                break;
            }
            case DIODE: {
                double vd = volt(el.a) - volt(el.b), d;
                double ex = expLim(vd / el.p1, d);
                double i = el.p0 * (ex - 1.0), g = el.p0 * d / el.p1;
                branch(J, el.a, el.b, g, i);
                break;
            }
            case ZENER: {
                double vd = volt(el.a) - volt(el.b), d1, d2;
                double ex = expLim(vd / el.p1, d1), eb = expLim(-(vd + el.p2) / el.p4, d2);
                double i = el.p0 * (ex - 1.0) - el.p3 * eb;
                double g = el.p0 * d1 / el.p1 + el.p3 * d2 / el.p4;
                branch(J, el.a, el.b, g, i);
                break;
            }
            case ISRC: {
                if (el.a >= 0) J[el.a][n] -= el.p0;
                if (el.b >= 0) J[el.b][n] += el.p0;
                break;
            }
            case NJFET: {
                int d = el.a, g = el.b, sn = el.c;
                double vd = volt(d), vg = volt(g), vs = volt(sn);
                double gm, gds, I;                 // I flows drain -> source
                double dVg, dVd, dVs;
                if (vd >= vs) {
                    I = jfetId(vg - vs, vd - vs, el.p0, el.p1, el.p2, gm, gds);
                    dVg = gm; dVd = gds; dVs = -gm - gds;
                } else {                           // the channel is symmetric: swap the ends
                    I = -jfetId(vg - vd, vs - vd, el.p0, el.p1, el.p2, gm, gds);
                    dVg = -gm; dVs = -gds; dVd = gm + gds;
                }
                if (d >= 0) {
                    J[d][n] -= I;
                    if (col(g) >= 0) J[d][col(g)] += dVg;
                    if (col(d) >= 0) J[d][col(d)] += dVd;
                    if (col(sn) >= 0) J[d][col(sn)] += dVs;
                }
                if (sn >= 0) {
                    J[sn][n] += I;
                    if (col(g) >= 0) J[sn][col(g)] -= dVg;
                    if (col(d) >= 0) J[sn][col(d)] -= dVd;
                    if (col(sn) >= 0) J[sn][col(sn)] -= dVs;
                }
                break;
            }
            case NPN: {
                double vb = volt(el.b), vc = volt(el.a), ve = volt(el.c);
                double Is = el.p0, BF = el.p1, BR = el.p2, VAF = el.p3;
                double vbe = vb - ve, vbc = vb - vc, def, der;
                double ef = expLim(vbe / kVt, def), er = expLim(vbc / kVt, der);
                double f = 1.0 - vbc / VAF;
                double Ic = Is * (ef - er) * f - Is / BR * (er - 1.0);
                double Ib = Is / BF * (ef - 1.0) + Is / BR * (er - 1.0);
                double dIc_dvbe = Is * def / kVt * f;
                double dIc_dvbc = -Is * der / kVt * f - Is * (ef - er) / VAF - Is / BR * der / kVt;
                double dIb_dvbe = Is / BF * def / kVt;
                double dIb_dvbc = Is / BR * der / kVt;
                int c = el.a, b = el.b, em = el.c;
                // currents leaving: collector +Ic, base +Ib, emitter -(Ic+Ib)
                // d/dVb = dvbe + dvbc, d/dVe = -dvbe, d/dVc = -dvbc
                struct T { int node; double i; double dIdVb, dIdVe, dIdVc; };
                T t[3] = {
                    { c,  Ic,        dIc_dvbe + dIc_dvbc, -dIc_dvbe, -dIc_dvbc },
                    { b,  Ib,        dIb_dvbe + dIb_dvbc, -dIb_dvbe, -dIb_dvbc },
                    { em, -(Ic + Ib), -(dIc_dvbe + dIc_dvbc + dIb_dvbe + dIb_dvbc),
                                       dIc_dvbe + dIb_dvbe, dIc_dvbc + dIb_dvbc },
                };
                for (int k = 0; k < 3; k++) {
                    if (t[k].node < 0) continue;
                    J[t[k].node][n] -= t[k].i;
                    if (col(b) >= 0)  J[t[k].node][col(b)]  += t[k].dIdVb;
                    if (col(em) >= 0) J[t[k].node][col(em)] += t[k].dIdVe;
                    if (col(c) >= 0)  J[t[k].node][col(c)]  += t[k].dIdVc;
                }
                break;
            }
        }
    }

    // Solve the augmented system in place by Gaussian elimination with partial pivoting;
    // the solution is left in column n.
    bool solve(double A[][kCols]) {
        for (int col = 0; col < n; col++) {
            int piv = col;
            for (int r = col + 1; r < n; r++)
                if (std::fabs(A[r][col]) > std::fabs(A[piv][col])) piv = r;
            if (std::fabs(A[piv][col]) < 1e-300) return false;
            if (piv != col)
                for (int j = col; j <= n; j++) { double t = A[col][j]; A[col][j] = A[piv][j]; A[piv][j] = t; }
            double inv = 1.0 / A[col][col];
            for (int r = col + 1; r < n; r++) {
                double f = A[r][col] * inv;
                if (f == 0.0) continue;
                for (int j = col; j <= n; j++) A[r][j] -= f * A[col][j];
            }
        }
        for (int r = n - 1; r >= 0; r--) {
            double s = A[r][n];
            for (int j = r + 1; j < n; j++) s -= A[r][j] * A[j][n];
            A[r][n] = s / A[r][r];
        }
        return true;
    }
};

} // namespace mna

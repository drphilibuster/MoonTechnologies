#pragma once
// The whole Schematic_4069vco netlist in one Mna circuit, stepped with a trapezoid at an
// adaptive 0.02 ns .. 20 us step: the ORACLE the audio-rate model in src/Accrual/Vco.hpp is
// checked against. Nothing here is shared with the model except the part values and the
// device models (Mna.hpp, Cd4069.hpp), which both must use.
#include "../../src/Accrual/Vco.hpp"
#include <vector>
#include <cstdio>

namespace fullnet {
using namespace accrual;
typedef mna::CircuitT<24, 96, 12> Big;
enum { A, QB, X, Y, Z, P3, P4, S, SAW, N, M8, P10, P12, PULSE, QPW, QE, NODES };
enum { F_VCC, F_VEE, F_VDD, F_TUNE, F_FINE, F_CV1, F_CV2, F_WIPER, F_PWM, F_N };

struct Net {
    Big c;
    double vdd = 12.0;
    int cap1 = -1;
    void build(double r21, bool cv1, bool cv2, bool pwm, double vddV, double pot, double cin = kCin,
               bool junctionCaps = true, int cinMask = 63) {
        c = Big(); c.n = NODES; vdd = vddV;
        const int VCC = mna::fixed(F_VCC), VEE = mna::fixed(F_VEE), VDD = mna::fixed(F_VDD);
        c.addResistor(mna::fixed(F_TUNE), A, R1);
        c.addResistor(mna::fixed(F_FINE), A, R2);
        if (cv1) c.addResistor(mna::fixed(F_CV1), A, R3);
        if (cv2) c.addResistor(mna::fixed(F_CV2), A, R4);
        c.addResistor(A, mna::GND, r21 + R6);
        const Bjt b1 = bc560c(), b2 = bc550c(), b3 = bc550c();
        c.addPnp(VEE, A, QB, b1.is, b1.bf, b1.br, b1.vaf);
        c.addResistor(QB, VCC, R7);
        c.addNpn(X, QB, mna::GND, b2.is, b2.bf, b2.br, b2.vaf);
        cap1 = c.addCapacitor(Y, X, C1);
        c.addResistor(Y, Z, R8);
        c.addDiode(Y, Z, kDiodeIs, kDiodeNVt);
        cd4069::addInverter(c, X, Z, VDD, mna::GND);
        c.addResistor(Z, P3, R9);
        cd4069::addInverter(c, P3, P4, VDD, mna::GND);
        cd4069::addInverter(c, P4, S, VDD, mna::GND);
        c.addResistor(S, P3, R10);
        c.addDiode(S, X, kDiodeIs, kDiodeNVt);
        c.addCapacitor(Z, SAW, C2);
        c.addResistor(SAW, mna::GND, R11);
        c.addResistor(Z, N, R12);
        c.addResistor(mna::fixed(F_WIPER), N, R13 + kPwPot * pot * (1.0 - pot));
        c.addResistor(M8, N, R14);
        c.addResistor(VCC, N, R15);
        c.addResistor(QE, N, R17);
        cd4069::addInverter(c, N, M8, VDD, mna::GND);
        cd4069::addInverter(c, M8, P10, VDD, mna::GND);
        cd4069::addInverter(c, P10, P12, VDD, mna::GND);
        c.addCapacitor(P12, PULSE, C3);
        c.addResistor(PULSE, mna::GND, R16);
        c.addNpn(VCC, QPW, QE, b3.is, b3.bf, b3.br, b3.vaf);
        c.addResistor(QE, VEE, R18);
        if (pwm) c.addResistor(mna::fixed(F_PWM), QPW, R20);
        c.addResistor(QPW, mna::GND, R19);
        // parasitics: the 4069's input capacitance, and the transistors' junction capacitances
        const int ins[6] = { X, P3, P4, N, M8, P10 };
        for (int k = 0; k < 6; k++) if (cin > 0 && (cinMask >> k & 1)) c.addCapacitor(ins[k], mna::GND, cin);
        if (junctionCaps) {
            c.addCapacitor(QB, A, b1.cje); c.addCapacitor(A, VEE, b1.cjc);
            c.addCapacitor(QB, mna::GND, b2.cje); c.addCapacitor(QB, X, b2.cjc);
            c.addCapacitor(QPW, QE, b3.cje); c.addCapacitor(QPW, VCC, b3.cjc);
        }
        c.fixedV[F_VCC] = kVcc; c.fixedV[F_VEE] = kVee; c.fixedV[F_VDD] = vddV;
        c.fixedV[F_WIPER] = kVcc * pot;
    }
    void setInputs(double tune, double fine, double v1, double v2, double vpwm) {
        c.fixedV[F_TUNE] = tune; c.fixedV[F_FINE] = fine; c.fixedV[F_CV1] = v1;
        c.fixedV[F_CV2] = v2; c.fixedV[F_PWM] = vpwm;
    }
    /** The DC start (capacitors open). It is solved with the four signal inputs at zero, where
        Newton always converges, and the real inputs applied afterwards: the transient then walks
        to the true operating point in microseconds, whatever the pitch. */
    bool start() {
        double keep[F_N];
        for (int k = 0; k < F_N; k++) keep[k] = c.fixedV[k];
        double tgt[F_N];
        for (int k = 0; k < F_N; k++) tgt[k] = keep[k];
        tgt[F_TUNE] = tgt[F_FINE] = tgt[F_CV1] = tgt[F_CV2] = 0.0;
        bool ok = c.solveDc(tgt, F_N, 40);
        for (int k = 0; k < F_N; k++) c.fixedV[k] = keep[k];
        return ok;
    }
};

struct Trace {
    std::vector<double> t, z, saw, pulse, s, x, p12;
    std::vector<double> trips;          // times S crossed vdd/2 upward
    std::vector<double> pulseUp;
};

/** Run `tEnd` seconds from the DC start. Records every accepted step. */
inline bool run(Net& n, double tEnd, Trace& tr, double hMax = 20e-6, double dv = 0.08) {
    mna::CircuitT<24, 96, 12>* c = &n.c;
    Transient<Big> T(c);
    T.hMax = hMax; T.dvMax = dv; T.h = 1e-9;
    double sPrev = c->v[S], tPrev = 0.0, pPrev = c->v[PULSE];
    bool ok = true;
    while (T.t < tEnd) {
        double t0 = T.t;
        if (!T.advance(tEnd)) { ok = false; break; }
        if (sPrev < 0.5 * n.vdd && c->v[S] >= 0.5 * n.vdd) {
            double f = (0.5 * n.vdd - sPrev) / (c->v[S] - sPrev);
            tr.trips.push_back(t0 + f * (T.t - t0));
        }
        sPrev = c->v[S];
        (void)tPrev; (void)pPrev;
        tr.t.push_back(T.t); tr.z.push_back(c->v[Z]); tr.saw.push_back(c->v[SAW]);
        tr.pulse.push_back(c->v[PULSE]); tr.s.push_back(c->v[S]); tr.x.push_back(c->v[X]);
        tr.p12.push_back(c->v[P12]);
    }
    return ok;
}

/** Run until `want` S-rising crossings have been seen (or `tMax` seconds). */
inline bool runTrips(Net& n, Trace& tr, size_t want, double tMax, double hMax = 20e-6, double dv = 0.05) {
    Transient<Big> T(&n.c);
    T.hMax = hMax; T.dvMax = dv; T.h = 1e-9;
    double sPrev = n.c.v[S];
    while (T.t < tMax && tr.trips.size() < want) {
        double t0 = T.t;
        if (!T.advance(tMax)) return false;
        if (sPrev < 0.5 * n.vdd && n.c.v[S] >= 0.5 * n.vdd) {
            double f = (0.5 * n.vdd - sPrev) / (n.c.v[S] - sPrev);
            tr.trips.push_back(t0 + f * (T.t - t0));
        }
        sPrev = n.c.v[S];
        tr.t.push_back(T.t); tr.z.push_back(n.c.v[Z]); tr.saw.push_back(n.c.v[SAW]);
        tr.pulse.push_back(n.c.v[PULSE]); tr.s.push_back(n.c.v[S]); tr.x.push_back(n.c.v[X]);
        tr.p12.push_back(n.c.v[P12]);
    }
    return tr.trips.size() >= want;
}

} // namespace fullnet

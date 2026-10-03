#pragma once
// The Day 10 "Quad gate controlled switch" (kristian.borgstedt, 2020-03-16,
// MiawResearch/day10/4066_quad_gated_switch.pdf), one channel at a time, as the circuit.
//
// One channel:
//
//   Gate -- (+) TL074 (-) -- Vref = VCC * 10k / (100k + 10k) = 1.0909 V   (no hysteresis,
//                 out -- 1N4448 -- NODE --+-- 1k -- BC549 base (emitter GND)           no input resistor)
//                                         |
//   toggle L: pin 1 (E bus) is NODE (direct: the switch is ON when the gate is HIGH) or the
//   BC549 collector (inverted: ON when the gate is LOW; collector pulled to VCC by 1k).
//   E bus -- 1k -- LED -- GND, and the 4066's nE pin (its own clamp diode to VSS).
//   Y = "A", Z = "B", VDD = VCC = 12 V, VSS = GND.
//
// What this gives, by construction (tests/AuditLogic/test_referral.cpp checks each):
//   * one comparison threshold at 1.0909 V; the module's old 0.1 V / 1 V Schmitt pair is
//     not on the board;
//   * the nE pin never sees 12 V: with the comparator saturated the E bus sits at about
//     5.8 V (direct) or 7.0 V (inverted, LED lit), inside the datasheet's undefined band
//     (VIL max 3.4 V, VIH min 8.6 V at 12 V). The model still switches on the comparator's
//     logic; whether a real part trips there is part-dependent and is reported, not modelled;
//   * the enable arrives late by the comparator's slew to the E node's mid-swing plus the
//     4066's tPZH/tPHZ, so the edge lands part-way into a sample;
//   * the toggle's off-side switch (the module's ROUTE A-C second output) is a second
//     channel with the opposite toggle: it turns OFF after tPHZ/tPLZ and the other ON after
//     tPZH/tPZL, so the pair is make-before-break by ~45-50 ns, not break-before-make.
//
// ASSUMED, not on the schematic or datasheets (docs/AuditLogic.md):
//   * the source driving A has 1 k output resistance and the load on B/C is 100 k (Rack's
//     ports are ideal; a hardware Eurorack module is about that);
//   * an unpatched gate or A reads 0 V (a floating comparator input would not);
//   * 1N4448 as the 1N4148 set used elsewhere in this repo, the LED as Vf 2.0 V at 20 mA
//     n = 2, BC549C as Is 7.6e-14, BF 520, BR 10, VAF 100;
//   * TL074 output stage and slew: tl074::Comparator (EMF rail - 0.79 V behind 526 ohm) and
//     20 V/us typical (TI SLOS080W, TL07xC; minimum 8). Not modelled: Q1 storage time on the
//     inverted path, input offset, the E pin's capacitance (about 10 ns with the 1k).
//
// No Rack dependency, so tests/ drives it bare.

#include "Hef4066.hpp"
#include "Mna.hpp"
#include "Tl074Comparator.hpp"

#include <cmath>

namespace referral {

/** The E bus for one toggle position. */
struct Drive {
    bool inverted = false;
    double eOn = 0.0, eOff = 0.0;         // volts at the nE pin with the switch logically ON / OFF
    double ledOnA = 0.0;                  // the channel LED's current when ON
    double emfMid = 0.0;                  // comparator EMF at which E is half way between eOn and eOff
    double delayToOn = 0.0, delayToOff = 0.0;   // comparator slew from the opposite rail to emfMid, s
};

struct Board {
    // Passive parts (schematic).
    double rRefTop = 100e3, rRefBottom = 10e3, rBase = 1e3, rPull = 1e3, rLed = 1e3;
    // Semiconductors (assumed sets, see above).
    double dIs = 2.52e-9, dNVt = 1.752 * mna::kVt;
    double ledNVt = 2.0 * mna::kVt, ledIs = 20e-3 / std::exp(2.0 / (2.0 * mna::kVt));
    double qIs = 7.6e-14, qBF = 520.0, qBR = 10.0, qVAF = 100.0;
    tl074::Comparator op;
    double slew = 20e6;                   // V/s
    hef4066::Chip chip;
    // The world around the switch (assumed).
    double rSource = 1000.0, rLoad = 100e3;

    double vref = 0.0;
    Drive drive[2];                       // [0] direct (Hi On), [1] inverted (Lo On)

    Board() { prepare(); }

    void prepare() {
        op.vcc = chip.vdd;
        vref = op.vcc * rRefBottom / (rRefTop + rRefBottom);
        for (int k = 0; k < 2; k++) buildDrive(k == 1, drive[k]);
    }

    /** E-pin voltage (and the LED's current) for a comparator EMF. */
    double eVolts(bool inverted, double emf, double* ledA = nullptr) const {
        using namespace mna;
        enum { P, N, B, C, L, E, NN };
        Circuit c;
        c.n = NN;
        c.addResistor(fixed(0), P, op.rout);
        c.addDiode(P, N, dIs, dNVt);
        c.addResistor(N, B, rBase);
        c.addNpn(C, B, GND, qIs, qBF, qBR, qVAF);
        c.addResistor(fixed(1), C, rPull);
        c.addResistor(inverted ? C : N, E, 1e-3);          // the toggle
        c.addResistor(E, L, rLed);
        int led = c.addDiode(L, GND, ledIs, ledNVt);
        c.addDiode(GND, E, chip.clampIs, kVt);              // nE's clamp to VSS
        double t[2] = { emf, op.vcc };
        c.solveDc(t, 2);
        if (ledA) *ledA = c.diodeCurrent(led);
        return c.v[E];
    }

private:
    void buildDrive(bool inverted, Drive& d) const {
        d.inverted = inverted;
        double hi = op.emfHigh(), lo = op.emfLow();
        double eAtHi = eVolts(inverted, hi), eAtLo = eVolts(inverted, lo);
        double ledHi = 0.0, ledLo = 0.0;
        eVolts(inverted, hi, &ledHi);
        eVolts(inverted, lo, &ledLo);
        // Logical ON is the comparator high for the direct toggle, low for the inverted.
        d.eOn = inverted ? eAtLo : eAtHi;
        d.eOff = inverted ? eAtHi : eAtLo;
        d.ledOnA = inverted ? ledLo : ledHi;
        double mid = 0.5 * (d.eOn + d.eOff);
        double a = lo, b = hi;                               // bisect E(emf) = mid
        bool rising = eAtHi > eAtLo;
        for (int i = 0; i < 50; i++) {
            double m = 0.5 * (a + b);
            double e = eVolts(inverted, m);
            if ((e > mid) == rising) b = m; else a = m;
        }
        d.emfMid = 0.5 * (a + b);
        // Direct: ON is the comparator rising; inverted: ON is it falling.
        double up = (d.emfMid - lo) / slew, down = (hi - d.emfMid) / slew;
        d.delayToOn = inverted ? down : up;
        d.delayToOff = inverted ? up : down;
    }
};

/** One analogue switch with its own enable timeline: the times at which the chip's
    conduction changes, so a sample knows what fraction of it the switch was ON. */
struct Switch {
    bool logicalOn = false;               // what the E pin has been told
    bool conducting = false;              // what the chip is doing
    int ne = 0;
    double et[4] = {};                    // seconds from the start of the next sample
    bool es[4] = {};
    double hpY = 0.0, hpU = 0.0;          // the OFF-state feedthrough filter
    double ramp = 0.0;                    // the declick fade (module's own)

    void reset() { *this = Switch(); }

    void push(double t, bool on) {
        if (ne == 4) {                    // full: drop the oldest
            for (int i = 1; i < 4; i++) { et[i - 1] = et[i]; es[i - 1] = es[i]; }
            ne = 3;
        }
        int i = ne++;
        while (i > 0 && et[i - 1] > t) { et[i] = et[i - 1]; es[i] = es[i - 1]; i--; }
        et[i] = t;
        es[i] = on;
    }

    /** Advance one sample; returns the fraction of it the chip conducted. ups/downs count the
        conduction changes that happened inside it. */
    double advance(double dt, int& ups, int& downs) {
        ups = downs = 0;
        double on = 0.0, t0 = 0.0;
        bool cur = conducting;
        int used = 0;
        while (used < ne && et[used] < dt) {
            double t = et[used] < 0.0 ? 0.0 : et[used];
            if (cur) on += t - t0;
            t0 = t;
            if (es[used] != cur) { if (es[used]) ups++; else downs++; }
            cur = es[used];
            used++;
        }
        if (cur) on += dt - t0;
        for (int i = used; i < ne; i++) { et[i - used] = et[i] - dt; es[i - used] = es[i]; }
        ne -= used;
        conducting = cur;
        return on / dt;
    }
};

struct Out {
    double b = 0.0, c = 0.0;
    bool onB = false, onC = false;        // the E-bus logic (the channel LEDs)
    double wB = 0.0, wC = 0.0;            // fraction of the sample each chip switch conducted
};

/** A gated channel: comparator, E drive, the B switch and (ROUTE A-B/A-C) the C switch. */
struct Channel {
    bool primed = false;
    double prevGate = 0.0;
    bool cmpHigh = false;
    bool hiOnPrev = true;
    Switch sw[2];                         // [0] to B, [1] to C

    void reset() { *this = Channel(); }

    /** One sample. `aOther` is the other channel's A pin (crosstalk). */
    Out process(const Board& bd, double gate, double a, double aOther, double dt,
                bool hiOn, bool routeAC, bool declick) {
        const hef4066::Chip& chip = bd.chip;
        Out o;
        if (!primed) {
            primed = true;
            prevGate = gate;
            cmpHigh = gate > bd.vref;
            hiOnPrev = hiOn;
            bool onB = hiOn ? cmpHigh : !cmpHigh;
            sw[0].logicalOn = sw[0].conducting = onB;
            sw[1].logicalOn = sw[1].conducting = routeAC && !onB;
            sw[0].ramp = onB ? 1.0 : 0.0;
            sw[1].ramp = sw[1].logicalOn ? 1.0 : 0.0;
        }
        bool nowHigh = gate > bd.vref;
        bool cmpChanged = nowHigh != cmpHigh;
        double f = 0.0;
        if (cmpChanged) {
            double d = gate - prevGate;
            f = d != 0.0 ? (bd.vref - prevGate) / d : 0.0;
            f = f < 0.0 ? 0.0 : (f > 1.0 ? 1.0 : f);
        }
        bool toggleChanged = hiOn != hiOnPrev;
        cmpHigh = nowHigh;
        hiOnPrev = hiOn;
        prevGate = gate;

        bool onB = hiOn ? cmpHigh : !cmpHigh;
        bool want[2] = { onB, routeAC && !onB };
        bool highLevel = a > 0.5 * chip.vdd;

        double vOut[2];
        for (int k = 0; k < 2; k++) {
            Switch& s = sw[k];
            // Direct for B when Hi On; the C switch has the opposite toggle.
            bool inverted = (k == 0) ? !hiOn : hiOn;
            if (want[k] != s.logicalOn) {
                s.logicalOn = want[k];
                double t = 0.0;
                if (cmpChanged || toggleChanged) {
                    const Drive& dr = bd.drive[inverted ? 1 : 0];
                    if (cmpChanged) t = f * dt + (want[k] ? dr.delayToOn : dr.delayToOff);
                }
                t += want[k] ? (highLevel ? chip.tPZH() : chip.tPZL())
                             : (highLevel ? chip.tPHZ() : chip.tPLZ());
                if (declick && !want[k]) t += 0.001;     // chip lets go after the fade
                s.push(t, want[k]);
            }
            // The declick fade (module's own): a 1 ms ramp on the signal into the chip.
            if (declick) {
                double step = dt / 0.001;
                double target = s.logicalOn ? 1.0 : 0.0;
                if (s.ramp < target) s.ramp = std::fmin(target, s.ramp + step);
                else if (s.ramp > target) s.ramp = std::fmax(target, s.ramp - step);
            }
            else {
                s.ramp = s.logicalOn ? 1.0 : 0.0;
            }

            int ups, downs;
            double w = s.advance(dt, ups, downs);
            if (k == 1 && !routeAC && !s.conducting && w == 0.0 && ups == 0 && downs == 0) {
                vOut[k] = 0.0;                              // C is unused in A-B mode
                s.hpY = s.hpU = 0.0;
                continue;
            }

            // OFF-state: the signal leaks through Cios into the load, and the other channel's
            // pin couples in the same way (crosstalk, -50 dB at 1 MHz into 1 k, same 0.5 pF).
            double u = chip.cFeed * a + chip.cFeed * aOther;
            double rc = bd.rLoad * chip.cNode * 2.0 / dt;
            double rcf = bd.rLoad * 2.0 / dt;
            double y = (rcf * (u - s.hpU) - (1.0 - rc) * s.hpY) / (1.0 + rc);
            s.hpU = u;
            s.hpY = y;
            double vOff = y;

            double vOn = 0.0, reff = bd.rLoad;
            if (w > 0.0 || ups + downs > 0) {
                hef4066::OnState st = chip.solveOn(a * (declick ? s.ramp : 1.0), bd.rSource, bd.rLoad);
                vOn = st.vout;
                double rch = bd.rSource + st.ron;
                reff = bd.rLoad * rch / (bd.rLoad + rch);
            }
            double v = w * vOn + (1.0 - w) * vOff;
            // Control feedthrough: each nE edge leaves its charge on the Z node, which relaxes
            // through whatever resistance it sees; the area is q * R, written into one sample.
            v += (ups - downs) * chip.edgeCharge() * reff / dt;
            vOut[k] = v;
            if (k == 0) o.wB = w; else o.wC = w;
        }
        o.b = vOut[0];
        o.c = vOut[1];
        o.onB = sw[0].logicalOn;
        o.onC = sw[1].logicalOn;
        return o;
    }
};

} // namespace referral

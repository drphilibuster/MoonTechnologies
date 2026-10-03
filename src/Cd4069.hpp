#pragma once
// The CD4069UB unbuffered CMOS inverter, as the analog part it is when a resistor is put from its
// output to its input (TI datasheet SCHS054E, Figure 16 "High-Input Impedance Amplifier"): one
// P-channel and one N-channel MOSFET, and the input clamp diodes of Figure 10, solved by
// `src/Mna.hpp`. Rack-free, so tests/ drives it bare.
//
// WHAT IS FITTED, AND TO WHAT. The datasheet gives no transistor parameters. It gives curves, and
// the device parameters below were fitted (tests/.. and MiawResearch/datasheets/cd4069ub.pdf) to
// the TYPICAL curves, read off the 400 dpi page by colour:
//   Figure 4  typical output-low (N-channel sink) current, VGS = 5, 10, 15 V, VDS to VGS
//   Figure 6  typical output-high (P-channel source) current, the same
//   Figure 3  typical voltage-transfer curve at VDD = 10 V and 15 V, and the supply current at 10 V
// The curves are a level-1 square law with mobility degradation 1/(1+theta*vov) and channel-length
// modulation; that shape fits them to about 10-20 %, which is the honest size of the error, and it
// reproduces what the amplifier needs: the switching point, and the voltage gain around it.
//
// THE GAIN. This is the number that makes the circuit work or not, and the datasheet does not
// print it. It is the slope of the typical VTC at its midpoint, and that curve is drawn as a
// polyline whose steep part is a few pixels, so it is only bounded: about 23-30 V/V at VDD = 10 V
// from the 10 V curve, about 13-15 V/V at 15 V from the better-resolved 15 V curve. The model
// lands at 28 V/V at 10 V, 21 at 12 V, 15 at 15 V (it falls as VDD rises: a longer channel drop
// for the same lambda). A secondhand figure of 30 dB at 12 V (RCA, quoted on forums, not seen in a
// primary source) would be 1 - 3 dB above this. `lambda` is the lever; the tests sweep it.
//
// NOT MODELLED: the input capacitance (10-15 pF), propagation delay (30 ns), the series resistor
// in the input network of Figure 10 (its value is not given and, being after the clamp diodes, it
// does not limit their current), the temperature, the body effect (source at a rail either way)
// and the quiescent leakage.

#include "Mna.hpp"

#include <cmath>

namespace cd4069 {

struct Device { double beta, vt, lambda, theta; };

struct Params {
    Device n, p;
    double clampIs = 1e-14;             // the input protection diodes: ordinary silicon junctions
    double clampNvt = mna::kVt;
};

/** The fitted typical part. */
inline Params typical() {
    Params q;
    q.n = { 0.00064223, 1.7709, 0.015919, 0.25008 };
    q.p = { 0.00038018, 0.99716, 0.016434, 0.10007 };
    return q;
}

struct Inverter { int nmos, pmos, clampHi, clampLo; };

/** Adds one inverter between `in` and `out`, supplied between `vdd` and `vss` (any node codes,
    fixed sources included), with its input clamp diodes. */
template <class Circ>
inline Inverter addInverter(Circ& c, int in, int out, int vdd, int vss, const Params& q = typical()) {
    Inverter inv;
    inv.nmos = c.addMosfet(out, in, vss, q.n.beta, q.n.vt, q.n.lambda, q.n.theta, +1);
    inv.pmos = c.addMosfet(out, in, vdd, q.p.beta, q.p.vt, q.p.lambda, q.p.theta, -1);
    inv.clampHi = c.addDiode(in, vdd, q.clampIs, q.clampNvt);
    inv.clampLo = c.addDiode(vss, in, q.clampIs, q.clampNvt);
    return inv;
}

/** The static transfer curve: output voltage for `vin` at supply `vdd`. */
inline double transfer(double vin, double vdd, const Params& q = typical()) {
    mna::Circuit c;
    c.n = 1;
    addInverter(c, mna::fixed(0), 0, mna::fixed(1), mna::GND, q);
    double target[2] = { vin, vdd };
    c.solveDc(target, 2, 40);
    return c.v[0];
}

/** The supply current at `vin` (the sum of the two channels' current, the P device's). */
inline double supplyCurrent(double vin, double vdd, const Params& q = typical()) {
    mna::Circuit c;
    c.n = 1;
    mna::Circuit::Elem* dummy = nullptr; (void)dummy;
    Inverter inv = addInverter(c, mna::fixed(0), 0, mna::fixed(1), mna::GND, q);
    double target[2] = { vin, vdd };
    c.solveDc(target, 2, 40);
    (void)inv;
    // The N device's drain current equals the P device's (nothing else loads the output); read it
    // from the model's own law so the number is the one the solver balanced.
    double gm, gds;
    double vo = c.v[0];
    return mna::mosId(vin, vo, q.n.beta, q.n.vt, q.n.lambda, q.n.theta, gm, gds);
}

/** Small-signal gain dVout/dVin at the operating point where the input is `vin`. */
inline double gainAt(double vin, double vdd, const Params& q = typical()) {
    mna::Circuit c;
    c.n = 1;
    addInverter(c, mna::fixed(0), 0, mna::fixed(1), mna::GND, q);
    double target[2] = { vin, vdd };
    c.solveDc(target, 2, 40);
    return c.acGain(1.0, 0, 0).real();
}

/** The self-biased point: the input at which the output equals it (what a feedback resistor
    settles to). */
inline double selfBias(double vdd, const Params& q = typical()) {
    double lo = 0.0, hi = vdd;
    for (int i = 0; i < 50; i++) {
        double m = 0.5 * (lo + hi);
        if (transfer(m, vdd, q) > m) lo = m; else hi = m;
    }
    return 0.5 * (lo + hi);
}

/** The small-signal gain at the self-biased point (negative: it inverts). */
inline double selfBiasedGain(double vdd, const Params& q = typical()) {
    return gainAt(selfBias(vdd, q), vdd, q);
}

} // namespace cd4069

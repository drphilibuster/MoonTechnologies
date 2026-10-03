#pragma once
// Microplasma noise: how a reverse-biased silicon junction really behaves just under and at
// avalanche breakdown, as a statistical model rather than a coloured Gaussian.
//
// The physics (Haitz, "Model for the Electrical Behavior of a Microplasma", J. Appl. Phys. 35,
// 1370, 1964; McKay, Phys. Rev. 94, 877, 1954 and Champlin, J. Appl. Phys. 30, 1039, 1959 for the
// observation; Cova & Ghioni, "Semiconductor-based detectors", NIST/Springer ch. 4 for the
// passive-quenching picture):
//
//   * Breakdown does not start across the whole junction. It starts at a few small defect
//     regions where the field is highest, each a "microplasma" with its own breakdown voltage
//     Vb. A microplasma is a bistable switch in series with a spreading resistance Rs: OFF it
//     carries nothing; ON it carries (V - Vb) / Rs.
//   * Each one jumps ON at random with a rate p01 that rises exponentially with the voltage
//     over Vb (an avalanche chain starting from a stray carrier succeeds with a probability
//     that rises steeply with overvoltage), and OFF at random with a rate p10 that falls
//     steeply with the current it carries (the chain dies when the number of carriers in the
//     plasma fluctuates to zero, and that is likely only while the current is small: below
//     "a few tens of microamps" in Cova's account of passive quenching).
//   * So the junction current is a sum of rectangular, constant-height pulses with exponentially
//     distributed ON and OFF durations: a random telegraph signal (RTS), several of them when
//     there are several microplasmas, on top of the (much smaller) multiplied shot noise of the
//     ordinary uniform breakdown. Its spectrum is a sum of Lorentzians, its amplitude
//     distribution is not Gaussian but multi-modal, and the rates move exponentially with bias
//     (which is why a real avalanche noise source "pops" and changes character with its supply).
//
// The SHAPE of p01 (exponential in overvoltage) and p10 (exponential in current) follows
// Haitz's argument; the NUMBERS are not published for the BC549/BC337/2N3904 and are the
// module's ASSUMPTION (see Params defaults). The statistics the model is held to (exponential
// dwell times, Lorentzian spectra, bias dependence, non-Gaussian amplitudes) are tested in
// tests/Kickback/test_microplasma.cpp against closed forms.
//
// No Rack dependency, no allocation, so tests/ drives it bare.

#include <cmath>
#include <cstdint>

namespace microplasma {

/** One microplasma. vb in volts, rs in ohms, nuOn the turn-on rate (per second) at v = vb,
    vs the volts per e-fold of the turn-on rate, nuOff the turn-off rate (per second) at zero
    current, iq the amps per e-fold of the turn-off rate. */
struct Params {
    double vb = 7.2, rs = 50e3, nuOn = 20.0, vs = 0.03, nuOff = 2e4, iq = 1e-6;
};

/** Turn-on rate at junction voltage `vj` (per second). The exponent is capped so a wild
    voltage cannot overflow. */
inline double turnOn(const Params& p, double vj) {
    double x = (vj - p.vb) / p.vs;
    if (x > 30.0) x = 30.0;
    if (x < -60.0) x = -60.0;
    return p.nuOn * std::exp(x);
}

/** Turn-off rate while carrying `i` amps (per second). */
inline double turnOff(const Params& p, double i) {
    double x = i / p.iq;
    if (x < 0.0) x = 0.0;
    if (x > 60.0) x = 60.0;
    return p.nuOff * std::exp(-x);
}

/** The ON current at junction voltage `vj`: the overvoltage across the spreading resistance. */
inline double onCurrent(const Params& p, double vj) {
    return vj > p.vb ? (vj - p.vb) / p.rs : 0.0;
}

/** A two-state Markov process with rates r01 (off to on) and r10 (on to off): the stationary
    probability of being on. */
inline double onFraction(double r01, double r10) { return r01 / (r01 + r10); }

/** One-sided power spectral density, A^2/Hz, of a telegraph signal of height `di` with
    constant rates: 4 di^2 p (1 - p) tau / (1 + (2 pi f tau)^2), tau = 1 / (r01 + r10). */
inline double lorentzianPsd(double f, double di, double r01, double r10) {
    double tau = 1.0 / (r01 + r10), p = onFraction(r01, r10), w = 2.0 * 3.14159265358979323846 * f * tau;
    return 4.0 * di * di * p * (1.0 - p) * tau / (1.0 + w * w);
}

/** A small xorshift32 source, so a stream is a function of its seed alone. */
struct Rng {
    uint32_t s = 0x9E3779B9u;
    explicit Rng(uint32_t seed = 0x9E3779B9u) : s(seed ? seed : 0x9E3779B9u) {}
    void seed(uint32_t x) { s = x ? x : 0x9E3779B9u; }
    /** Uniform in (0, 1). */
    double uniform() {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return (s + 0.5) / 4294967296.0;
    }
    double gauss() {
        double a = uniform(), b = uniform();
        return std::sqrt(-2.0 * std::log(a)) * std::cos(2.0 * 3.14159265358979323846 * b);
    }
};

/** The state of one microplasma: on or off. `step` advances it by `h` seconds given the
    junction voltage and, if it is on, the current it is carrying, and returns the new state.
    Exact for rates constant over the step (probability 1 - exp(-rate h)). */
struct Switch {
    bool on = false;
    bool step(const Params& p, double vj, double iOn, double h, Rng& rng) {
        double r = on ? turnOff(p, iOn) : turnOn(p, vj);
        if (rng.uniform() < -std::expm1(-r * h)) on = !on;
        return on;
    }
};

} // namespace microplasma

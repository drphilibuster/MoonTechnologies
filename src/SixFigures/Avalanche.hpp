#pragma once
// The Reverse Avalanche VCO (Modular in a Week Day 1, `1.4_ReverseAvalanche`, Kristian Blasol,
// 2018-08-13, after Kassutronics) as the relaxation oscillator it is:
//
//   VCC --R (R1 1k + R2 10k || vactrol LDR)-- N        N --C (10 uF or 1 uF)-- K
//   Q1 BC337 with its base cut: emitter on N, collector on K, in reverse breakdown
//   K --LED1-- ground          (the supply current passes the LED)
//
// The capacitor charges through R toward the supply less the LED's drop until the junction
// strikes; the junction then dumps the capacitor until the current falls below its holding
// current and lets go, and it charges again. Only the relaxation is modelled: charge time, the
// shape of the charge, and where it starts and stops. The frequency itself is the module's
// RATE (this module asks for a frequency and works out the resistance), so what this circuit
// contributes is the waveform: an exponential charge between two voltages 0.9 V apart.
//
// MEASURED, but on a 2N2222, not a BC337 (published by lcamtuf, "Cursed circuits #6: reverse
// avalanche oscillator", blog.coredump.cx, reproduced by Hackaday 2026-07-14): the reversed
// junction strikes at 8.2 V, the capacitor falls to 7.3 V (read as 10 V and 9.1 V at the node,
// with the LED's 1.8 V on it) before the junction lets go, it needs about 5 mA to hold, and
// 14 V, 1k and 1 mF oscillate at 5.8 Hz (this model gives 4.9 Hz for exactly 1 mF; a
// capacitor 20 % under its marking, common for electrolytics, closes the gap). A BC337 is the
// same kind of part and its numbers are expected to be close, but no BC337 has been measured:
// they are ASSUMED to carry over. The flyback (the junction's on-resistance times C) is taken as
// instantaneous (the module's DRIFT is the slow wander).
//
// STRIKE JITTER (added 2026-10-03). The junction does not strike at a fixed voltage. The
// reverse-biased junction breaks down through microplasmas (src/Microplasma.hpp; Haitz 1964),
// whose turn-on rate rises exponentially with the voltage across them, so the capacitor's
// strike voltage is the first-passage time of a Poisson process whose rate climbs as the
// capacitor charges: P(not yet struck at V) = exp(-Lambda(V)), Lambda(V) = integral of
// r(u) / (dV/dt)(u) du over the charge, r(u) = r0 exp((u - Vn) / vs), dV/dt = (Vs - u) / RC.
// `Strike` draws one such voltage per cycle (the Gumbel-like law that follows) and turns it
// into the length and height of that cycle. r0 is fixed by the published measurement: the
// median strike is 8.2 V at the measured 1 s time constant (1 k x 1 mF); `kStrikeEfold` (the
// volts per e-fold of the rate) is ASSUMED. Slower time constants strike earlier, faster later
// (statistical lag), so the median voltage moves with the pitch; the cycle length is
// normalised to the median so the RATE knob still sets the median pitch.
//
// The TL072 "buffered, amplified output" (`Tl072Stage`: 1 + 220k/1k = 221, AC-coupled by C3 1 uF
// into R4 100k) is here too, as a function; Cores.hpp does not route to it (no panel output).
//
// No Rack dependency, so tests/ drives it bare.

#include "../Microplasma.hpp"

#include <cmath>
#include <cstdint>
#include <vector>

namespace sixfigures {
namespace avalanche {

static const double kVcc      = 12.0;   // the board's supply (ASSUMED: the course's +12 V)
static const double kVLed     = 1.8;    // red LED1 at a few mA (the measured circuit's figure)
static const double kVStrike  = 8.2;    // capacitor voltage at which the junction conducts
static const double kVRelease = 7.3;    // capacitor voltage at which it lets go
static const double kIHold    = 5e-3;   // holding current, amps

/** The voltage the capacitor charges toward: the supply less the LED. */
inline double source(double vcc = kVcc, double vled = kVLed) { return vcc - vled; }

/** The charge's shape factor: (Vs - strike) / (Vs - release). 0.695 here. */
inline double q(double vcc = kVcc, double vled = kVLed) {
    double vs = source(vcc, vled);
    return (vs - kVStrike) / (vs - kVRelease);
}

/** Charge time from release to strike through `r` ohms into `c` farads. */
inline double chargeTime(double r, double c, double vcc = kVcc, double vled = kVLed) {
    return r * c * -std::log(q(vcc, vled));
}

/** Frequency of the oscillation, flyback taken as instantaneous. */
inline double frequency(double r, double c, double vcc = kVcc, double vled = kVLed) {
    return 1.0 / chargeTime(r, c, vcc, vled);
}

/** The resistance that gives `hz` with capacitor `c`. */
inline double resistanceFor(double hz, double c, double vcc = kVcc, double vled = kVLed) {
    return 1.0 / (hz * c * -std::log(q(vcc, vled)));
}

/** The smallest resistance that still oscillates: below it the supply alone carries the
    holding current through the junction once the capacitor has fallen, and it latches on. */
inline double minimumResistance(double vcc = kVcc, double vled = kVLed) {
    return (source(vcc, vled) - kVRelease) / kIHold;
}

/** One cycle's capacitor voltage, normalised to 0..1 across the 0.9 V swing, at `phase`
    in [0, 1): the exponential charge from release to strike. */
inline double charge(double phase, double vcc = kVcc, double vled = kVLed) {
    double qq = q(vcc, vled);
    return (1.0 - std::pow(qq, phase)) / (1.0 - qq);
}

static const double kStrikeEfold = 0.02;   // volts per e-fold of the strike rate (ASSUMED; Haitz's shape, no BC337 data)
static const double kRcRef = 1.0;          // the measured circuit: 1 k x 1 mF = 1 s

/** The charge's own time (seconds) to reach `v` from the release voltage, for time constant `rc`
    and source `vs`: rc ln((vs - release) / (vs - v)). */
inline double timeTo(double v, double rc, double vcc = kVcc, double vled = kVLed) {
    double vs = source(vcc, vled);
    return rc * std::log((vs - kVRelease) / (vs - v));
}

/** The cumulative hazard table: G(V) = integral from kLo to V of exp((u - Vn) / vs) / (Vs - u) du,
    so Lambda(V) = r0 rc G(V). Built once per supply, 1 mV apart. */
struct StrikeTable {
    double vcc, vled, vs, lo, step, ef;
    std::vector<double> g;
    double r0;            // per second, at the nominal strike voltage
    explicit StrikeTable(double efold = kStrikeEfold, double vcc_ = kVcc, double vled_ = kVLed)
        : vcc(vcc_), vled(vled_), ef(efold) {
        vs = source(vcc, vled);
        lo = kVStrike - 0.6;
        double hi = kVStrike + 0.9;
        if (hi > vs - 0.3) hi = vs - 0.3;
        step = 0.001;
        int n = (int)((hi - lo) / step) + 1;
        g.assign(n, 0.0);
        auto f = [&](double u) { return std::exp((u - kVStrike) / ef) / (vs - u); };
        for (int i = 1; i < n; i++) {          // Simpson on each 1 mV cell
            double a = lo + (i - 1) * step, b = a + step;
            g[i] = g[i - 1] + step / 6.0 * (f(a) + 4.0 * f(0.5 * (a + b)) + f(b));
        }
        r0 = std::log(2.0) / (kRcRef * at(kVStrike));
    }
    /** G(v), linearly interpolated (0 below the table, the last value above it). */
    double at(double v) const {
        double x = (v - lo) / step;
        if (x <= 0.0) return 0.0;
        int i = (int)x;
        if (i >= (int)g.size() - 1) return g.back();
        return g[i] + (x - i) * (g[i + 1] - g[i]);
    }
    /** The voltage at which G reaches `target` (clamped to the table). */
    double invert(double target) const {
        if (target <= 0.0) return lo;
        if (target >= g.back()) return lo + (g.size() - 1) * step;
        int a = 0, b = (int)g.size() - 1;
        while (b - a > 1) { int m = (a + b) / 2; (g[m] < target ? a : b) = m; }
        double t = (target - g[a]) / (g[b] - g[a]);
        return lo + (a + t) * step;
    }
    /** The strike voltage for a unit-exponential draw `e` (= -ln u) at time constant `rc`. */
    double strikeFor(double e, double rc) const { return invert(e / (r0 * rc)); }
    /** Median strike voltage at time constant `rc`. */
    double median(double rc) const { return invert(std::log(2.0) / (r0 * rc)); }
    /** P(struck by V) = 1 - exp(-r0 rc G(V)). */
    double cdf(double v, double rc) const { return 1.0 - std::exp(-r0 * rc * at(v)); }
};

inline const StrikeTable& defaultTable() { static const StrikeTable t; return t; }

/** One cycle's randomness. `draw(freq)` is called at each reset: it picks this cycle's strike
    voltage and sets `theta` (the cycle's length relative to the median cycle at this pitch,
    1 on average) and `warp` (how far along the nominal charge curve one unit of phase goes, so
    the voltage reached is the drawn strike voltage). With theta = warp = 1 the saw is the
    old fixed-threshold one, bit for bit. */
struct Strike {
    float theta = 1.f, warp = 1.f;
    microplasma::Rng rng;
    explicit Strike(uint32_t seed = 0x57121CEu) : rng(seed) {}

    void draw(double hz, const StrikeTable& t = defaultTable()) {
        double rc = 1.0 / (hz * -std::log(q()));            // time constant that gives `hz` at the nominal swing
        double e = -std::log(rng.uniform());
        double vk = t.strikeFor(e, rc), vm = t.median(rc);
        double tk = timeTo(vk, rc), tm = timeTo(vm, rc), t0 = rc * -std::log(q());
        theta = (float)(tk / tm);
        warp = (float)(tm / t0);
    }
    void reset() { theta = warp = 1.f; }
};

/** Band-limiting step correction (the standard 2-sample polynomial BLEP), `t` in [0, 1). */
inline float blep(float t, float dt) {
    if (t < dt) { float x = t / dt; return x + x - x * x - 1.f; }
    if (t > 1.f - dt) { float x = (t - 1.f) / dt; return x * x + x + x + 1.f; }
    return 0.f;
}

/** The avalanche saw for a cycle that is `theta` long and runs the charge curve `warp` times as
    fast (see `Strike`): bipolar, band-limited at the reset. theta = warp = 1 is the fixed-threshold
    saw, bit for bit. */
inline float saw(float phase, float dt, float theta = 1.f, float warp = 1.f) {
    float c = (float)charge(phase * warp), cEnd = (float)charge(theta * warp);
    return 2.f * c - 1.f - cEnd * blep(phase / theta, dt / theta);
}

/** The TL072 "buffered, amplified output" (P3): C3 1 uF into R4 100k to ground, then a
    non-inverting stage of gain 1 + 220k / 1k = 221 with the part's gain-bandwidth (3 MHz typ)
    giving a pole at 3 MHz / 221 = 13.6 kHz and the output stopping short of the +-12 V rails
    (ASSUMED +-10.5 V; the datasheet's +-12 V is at +-15 V supplies). Input in volts at the
    oscillator node, output in volts. */
struct Tl072Stage {
    static constexpr double kGain = 221.0, kGbw = 3e6, kRail = 10.5;
    double hpA = 0, lpA = 0, hpZ = 0, lpZ = 0, xPrev = 0;
    void setRate(double fs) {
        hpA = std::exp(-2.0 * 3.14159265358979323846 * (1.0 / (2.0 * 3.14159265358979323846 * 100e3 * 1e-6)) / fs);
        lpA = std::exp(-2.0 * 3.14159265358979323846 * (kGbw / kGain) / fs);
        hpZ = lpZ = xPrev = 0;
    }
    double run(double x) {
        hpZ = hpA * (hpZ + x - xPrev);            // one-pole high-pass: C3 into R4
        xPrev = x;
        lpZ = lpA * lpZ + (1.0 - lpA) * (hpZ * kGain);
        return lpZ > kRail ? kRail : (lpZ < -kRail ? -kRail : lpZ);
    }
};

} // namespace avalanche
} // namespace sixfigures

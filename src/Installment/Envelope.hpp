#pragma once
// INSTALLMENT's two envelope circuits, solved as circuits instead of drawn as curves.
//
//   AD  PHOBoSapiens "Moon Base Xplorer: AD/AR" rev 1.1 (Day 5): a TLC555 whose output
//       charges and discharges a 10 uF capacitor through two 1N4148s, one per direction,
//       each in series with its own 500k pot and a shared 1k. The capacitor is what the
//       LM358 follower hands on. A 555 gives the rise its end: the capacitor reaches the
//       threshold and the latch lets go.
//   AR  Look Mum No Computer ADAR2 (Day 5, drawn by kristian.borgstedt): a TL072 section
//       as a comparator (gate against about 2.1 V) whose output charges and discharges a
//       1 uF capacitor through two 1N4448s, one per direction, each with its own 1M pot;
//       the other section is a follower, and a diode on its output drops the negative half.
//
// Both are the same machine, a source that flips between two levels behind a diode-steered
// resistor into a capacitor, and `SteeredRc` is that machine. What differs is what drives
// it: a latch with a threshold, or a comparator with a rail.
//
// What the module adds to the boards: the panel's ATTACK and RELEASE are times, not pot
// positions, so each is turned into the resistance that gives that time, and the pots'
// 500k / 1M ends and the 1k / 10 uF / 1 uF values do not limit it (the RANGE switch and the
// CV inputs have always reached past them). The LED, the output diode's loading and the
// INV stage are not modelled: the output is the follower, scaled so the envelope's peak is
// the module's 10 V.
//
// ASSUMED, because the drawings do not say (each is named where it is used):
//   * the TLC555's divider resistors (only their ratio matters, and the datasheet gives
//     none); the ADAR2's TL072 runs on +-12 V (its negative pin is not drawn, but a diode
//     in front of the output and an unpolarised 1 uF capacitor only make sense if the
//     capacitor can go negative); the TL072's output reaches 1.5 V short of each rail.
//
// No Rack dependency, so tests/ drives it bare.

#include <cmath>

namespace installment {
namespace circuit {

// --- the 1N4148 / 1N4448 (the SPICE models agree to this precision) -----------------

static const double kDiodeIs  = 2.52e-9;              // A
static const double kDiodeNVt = 1.752 * 0.025852;     // V, n * kT/q at 300 K

/** The current through a diode in series with `R`, when `delta` volts is across the pair,
    and its slope dI/d(delta). Zero for delta <= 0 (the reverse leakage is far below
    anything else here). Newton's method on x = the diode's own voltage:
    x + R*Is*(e^(x/nVt) - 1) = delta, a convex curve started from the right of its root. */
inline double diodeSeries(double delta, double R, double& slope) {
    if (!(delta > 0.0)) { slope = 0.0; return 0.0; }
    double x = delta < 0.9 ? delta : 0.9;
    double e = 1.0, hp = 1.0;
    for (int k = 0; k < 80; k++) {
        e = std::exp(x / kDiodeNVt);
        double h = x + R * kDiodeIs * (e - 1.0) - delta;
        hp = 1.0 + R * kDiodeIs / kDiodeNVt * e;
        double dx = h / hp;
        x -= dx;
        if (x < 0.0) x = 0.0;
        if (std::fabs(dx) < 1e-13) break;
    }
    e = std::exp(x / kDiodeNVt);
    hp = 1.0 + R * kDiodeIs / kDiodeNVt * e;
    slope = (kDiodeIs / kDiodeNVt * e) / hp;
    return kDiodeIs * std::expm1(x / kDiodeNVt);
}

/** The diode's voltage at a given current. */
inline double diodeDrop(double i) {
    return i > 0.0 ? kDiodeNVt * std::log(i / kDiodeIs + 1.0) : 0.0;
}

/** A capacitor with two opposed diodes, each in series with a resistor, to a source. */
struct SteeredRc {
    double v = 0.0;     // the capacitor, volts

    /** One sample. The capacitor charges through `rUp` while the source is above it and
        discharges through `rDown` while it is below. A single linearised implicit step
        (the diodes' slope is steep, the capacitor never overshoots the source). */
    double step(double source, double rUp, double rDown, double C, double dt) {
        double delta = source - v, g, I;
        if (delta > 0.0) I = diodeSeries(delta, rUp, g);
        else             I = -diodeSeries(-delta, rDown, g);
        double k = dt / C;
        v += k * I / (1.0 + k * g);
        return v;
    }
};

// --- AD: the TLC555 --------------------------------------------------------------------

namespace ad {
static const double kVdd      = 12.0;
static const double kC        = 10e-6;      // the timing capacitor
static const double kRseries  = 1000.0;     // the 1k between OUT and the diodes
static const double kRfloor   = 20.0;       // a time quicker than the board's parts reach is
                                            // made with less resistance than it has
static const double kCvPullup = 51e3;       // from CV (pin 5) to VDD
// ASSUMED: the three equal resistors of the 555's divider. The TLC555 datasheet does not
// give them; a bipolar 555 has 5k. With 100k the 51k would raise the threshold to 10.3 V,
// past the 10 V zener on the capacitor, and the circuit could never finish a rise.
static const double kRdivider = 5e3;
// The base network: the gate goes through 1 nF and 10k to a BC547B whose base has 100k to
// ground, so only a fast step makes it conduct (a high-pass, 110 us).
static const double kTrigTau  = 110e-6;
static const double kBaseDiv  = 100.0 / 110.0;
static const double kVbe      = 0.65;
/** The capacitor below which the fall is called finished (the diode's knee). */
static const double kLow      = 0.5;

/** The threshold the CV pin sets: VDD * 2R / (R || 51k + 2R). */
inline double threshold() {
    double top = kRdivider * kCvPullup / (kRdivider + kCvPullup);
    return kVdd * 2.0 * kRdivider / (top + 2.0 * kRdivider);
}

/** The total resistance in the charge path that makes the rise to the threshold take
    `seconds`. The diode's drop is taken at the mean charging current, twice round. */
inline double attackR(double seconds) {
    double vth = threshold();
    double vd = 0.5, R = 1e3;
    for (int k = 0; k < 3; k++) {
        double vs = kVdd - vd;
        R = seconds / (kC * std::log(vs / (vs - vth)));
        if (R < kRfloor) R = kRfloor;
        double iMean = (vs - 0.5 * vth) / R;
        vd = diodeDrop(iMean);
        if (vd > 0.9) vd = 0.9;
    }
    return R;
}

/** Release is the capacitor's own time constant: `seconds` = five of them. */
inline double decayR(double seconds) {
    double R = seconds / (5.0 * kC);
    return R < kRfloor ? kRfloor : R;
}
} // namespace ad

struct Ad555 {
    SteeredRc rc;
    bool latch = false;     // the 555's output
    bool active = false;    // a rise has started and its fall has not finished
    bool primed = false;
    double hp = 0.0, gatePrev = 0.0;

    void reset() { *this = Ad555(); }

    /** One sample. `gate` volts in; `out` is 0..10 V; `eoc` is true on the sample the
        fall finishes. LOOP is the module's: the end of a fall triggers the next rise. */
    void process(double gate, bool loop, double attackSec, double releaseSec,
                 double dt, double& out, bool& eoc) {
        eoc = false;
        if (!primed) { gatePrev = gate; primed = true; }
        double alpha = ad::kTrigTau / (ad::kTrigTau + dt);
        hp = alpha * (hp + gate - gatePrev);
        gatePrev = gate;
        bool trigLow = hp * ad::kBaseDiv > ad::kVbe;      // the BC547B has pulled TRIG down
        if (trigLow) { latch = true; active = true; }

        double rUp = ad::attackR(attackSec), rDown = ad::decayR(releaseSec);
        rc.step(latch ? ad::kVdd : 0.0, rUp, rDown, ad::kC, dt);

        double vth = ad::threshold();
        if (latch && rc.v >= vth && !trigLow) latch = false;   // TRIG overrides THRES
        if (!latch && active && rc.v < ad::kLow) {
            eoc = true;
            active = false;
            if (loop) { latch = true; active = true; }
        }
        double v = rc.v > 0.0 ? rc.v : 0.0;
        out = v * 10.0 / vth;
        if (out > 10.0) out = 10.0;
    }
};

// --- AR: the TL072 ----------------------------------------------------------------------

namespace ar {
static const double kRail     = 10.5;       // +-12 V supply, output 1.5 V short of it (ASSUMED)
static const double kC        = 1e-6;
static const double kRfloor   = 20.0;
static const double kVref     = 12.0 * 10e3 / (47e3 + 10e3);   // R9 against R8, from +12 V
// The gate's path to the comparator: D2, R7 and R6 (100k each), D3, then R10 (100k) to
// ground. The comparator trips when that last 100k carries kVref.
/** The gate voltage that trips it, solved: the same current runs through both diodes. */
inline double gateThreshold() {
    double i = kVref / 100e3;
    return 300e3 * i + 2.0 * diodeDrop(i);
}
/** The rise never reaches the rail: as the capacitor nears it the diode has only the little
    voltage left to push current with, and it slows to a crawl (a 1N4448 at 1 uA still drops
    about 0.27 V). The peak the envelope settles to in practice is the rail less that, and it
    is what the output is scaled to, so a held gate reads 10 V. */
static const double kPeak = kRail - 0.27;
/** Attack time is gate to 90 % of the peak, from rest at the negative rail (the last tenth
    is the diode's crawl and has no time of its own). The charge path's resistance that does
    it, with the diode's drop taken at the mean charging current, a few times round. */
inline double attackR(double seconds) {
    const double target = 0.9 * kPeak, start = -0.99 * kRail;
    double vd = 0.5, R = 1e3;
    for (int k = 0; k < 4; k++) {
        double vs = kRail - vd;
        R = seconds / (kC * std::log((vs - start) / (vs - target)));
        if (R < kRfloor) R = kRfloor;
        vd = diodeDrop((vs - 0.5 * (start + target)) / R);
        if (vd > 0.9) vd = 0.9;
    }
    return R;
}
/** Release time, gate down to 2 % of the peak: the capacitor is heading for -10.5 V from
    the peak, so the visible part is 0.66 time constants. */
static const double kReleaseTaus = 0.661;
/** Above this the rise is finished (90 % of the peak). */
static const double kHigh = 0.9 * kPeak;
} // namespace ar

struct ArTl072 {
    SteeredRc rc;
    bool looping = false;
    bool wasPositive = false;

    ArTl072() { rc.v = -0.99 * ar::kRail; }
    void reset() { *this = ArTl072(); }

    /** One sample. LOOP is the module's: at zero the comparator is held high until the peak. */
    void process(double gate, bool loop, double attackSec, double releaseSec,
                 double dt, double& out, bool& eoc) {
        eoc = false;
        if (loop) {
            if (rc.v <= 0.0) looping = true;
            if (rc.v >= ar::kHigh) looping = false;
        }
        else looping = false;
        bool high = gate > ar::gateThreshold() || looping;

        double rUp = ar::attackR(attackSec);
        double rDown = releaseSec / ar::kReleaseTaus / ar::kC;
        if (rDown < ar::kRfloor) rDown = ar::kRfloor;
        rc.step(high ? ar::kRail : -ar::kRail, rUp, rDown, ar::kC, dt);

        bool positive = rc.v > 0.0;
        if (wasPositive && !positive) eoc = true;
        wasPositive = positive;
        double v = positive ? rc.v : 0.0;
        out = v * 10.0 / ar::kPeak;
        if (out > 10.0) out = 10.0;
    }
};

} // namespace circuit
} // namespace installment

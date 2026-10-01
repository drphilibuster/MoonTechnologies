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
// instantaneous, and the junction's cycle-to-cycle jitter is not modelled (the module's DRIFT
// is the slow wander).
//
// No Rack dependency, so tests/ drives it bare.

#include <cmath>

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

} // namespace avalanche
} // namespace sixfigures

#pragma once
// The CD4071B quad 2-input OR gate, at the level its datasheet commits to.
//
// Source: TI SCHS056D (Harris), MiawResearch/datasheets/cd4071b.pdf, p.1 "STATIC
// ELECTRICAL CHARACTERISTICS" (25 C column) and Fig. 1, 4, 6, 8, 10.
//
//   guaranteed input levels   VIL max  1.5 / 3 / 4 V       at VDD = 5 / 10 / 15 V
//                             VIH min  3.5 / 7 / 11 V
//   output levels, no load    VOH min  VDD - 0.05 V   VOL max 0.05 V (typ: the rails)
//   output drive, 25 C typ    IOH 1 mA @ 0.4 V below VDD (5 V), 2.6 mA @ 0.5 V (10 V),
//                             6.8 mA @ 1.5 V (15 V); IOL the same on the other rail
//   switching point           the transfer curve (Fig. 1) turns over at VDD / 2
//
// So a gate is: a comparator at VDD/2 on each input, a logical OR, and a source of 0 or
// VDD behind an output resistance. The output resistance is the datasheet's typical
// drive current at the test point it quotes: R = (that drop) / (that current), which is
// 400 / 192 / 221 ohm at 5 / 10 / 15 V. Between those supplies it is interpolated
// linearly; outside, held.
//
// Propagation delay (60 ns at 10 V) is far below a sample and not modelled. The input
// protection network (clamps to VDD and VSS) does not conduct for anything the board
// can present and is not modelled.
//
// No Rack dependency, so tests/ drives it bare.

namespace cd4071 {

/** Piecewise-linear through (5, a), (10, b), (15, c), clamped outside. */
inline double atSupply(double vdd, double a, double b, double c) {
    if (vdd <= 5.0)  return a;
    if (vdd <= 10.0) return a + (b - a) * (vdd - 5.0) / 5.0;
    if (vdd <= 15.0) return b + (c - b) * (vdd - 10.0) / 5.0;
    return c;
}

/** Highest input voltage guaranteed to read low. */
inline double vilMax(double vdd) { return atSupply(vdd, 1.5, 3.0, 4.0); }
/** Lowest input voltage guaranteed to read high. */
inline double vihMin(double vdd) { return atSupply(vdd, 3.5, 7.0, 11.0); }
/** Where the transfer curve turns over (Fig. 1). */
inline double threshold(double vdd) { return 0.5 * vdd; }
/** Output resistance, either rail, ohms (typical drive current, see the header). */
inline double rout(double vdd) { return atSupply(vdd, 0.4 / 1e-3, 0.5 / 2.6e-3, 1.5 / 6.8e-3); }

/** The logic of one gate: true when either input is above the switching point. */
inline bool or2(double a, double b, double vdd) {
    double t = threshold(vdd);
    return a > t || b > t;
}

/** The open-circuit voltage the gate drives its output to, behind rout(). */
inline double outputSource(bool high, double vdd) { return high ? vdd : 0.0; }

} // namespace cd4071

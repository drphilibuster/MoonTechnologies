#pragma once
// The CD4046B's VCO section, as Six Figures' "4046" core uses it: the chip alone, INH grounded,
// R2 not fitted (so f_min = 0), C1 across pins 6-7, R1 on pin 11, VCOIN on pin 9. The phase
// comparators are not part of this header; the module's PLL is its own addition (docs/SixFigures.md).
//
// Self-contained: <cmath> only, so tests/SixFigures can build it without Rack.
//
// WHAT IS DATASHEET AND WHAT IS NOT
//
//  * f0, the centre frequency (VCOIN = VDD/2, R2 = infinity) as a function of C1, R1 and VDD, is the
//    nine lines of Fig. 7 of the Nexperia HEF4046B data sheet (Rev. 5, 18 Nov 2011; pin-for-pin the
//    CD4046B, and the same figure as RCA/TI's Fig. 4). That figure is a vector graphic in the PDF, so
//    the knots below are read off the plotted polylines to within the page's resolution (about 0.3 %),
//    not eyeballed. The plot has two or three knots per line; between them the line is straight in
//    log-log, which is how the figure draws it. Between the nine lines (VDD in 5/10/15 V, R1 in
//    10k/100k/1M) the model interpolates linearly in log f against log R1 and log VDD: INTERPOLATED.
//    Beyond a line's last knot (C1 > ~1 uF, e.g. the LFO range's 5 uF) it continues the last segment.
//  * f(VCOIN): the data sheet's design information says f0 = 0.5 * fmax for a VCO without offset
//    (R2 = infinity), and draws the transfer as a straight line from f = 0. So
//    f = 2 f0 VCOIN / VDD, zero at VSS, saturating at the chip's own speed limit.
//  * That ceiling is the "maximum operating frequency" row (R1 = 10k, C1 = 50 pF, VCOIN = VDD): the
//    Nexperia typicals 1.0 / 2.0 / 2.7 MHz at 5 / 10 / 15 V. Vendors disagree (RCA: 0.6/1.2/1.6,
//    onsemi: 0.7/1.4/1.9 for R1 = 5k); Nexperia is used because its Fig. 7 is the source of f0.
//    It is a speed limit, not a function of R1 or C1, and never matters at audio frequencies.
//  * Source-follower offset (1.7 V) belongs to SFout, which this board does not use; it is not in
//    the VCO law. (onsemi's supply-current note writes (VCOIN - 1.65 V)/R1; the Fig. 7 data fit
//    f0 ~ VCOIN much better than f0 ~ VCOIN - 1.65, see tests/SixFigures/test_cd4046.cpp.)
//
//  * NOT in any data sheet, and an ESTIMATE: the impedance of the R1 pin. The VCO's frequency
//    follows the current the chip drives out of pin 11. With nothing patched that current is
//    VCOIN / (R1 + kRint), and kRint = 2.2 kohm reproduces the shortfall of the 10 kohm lines
//    against the 100 kohm and 1 Mohm lines of Fig. 7 (the ratio of their f0 * R1 products). A signal patched
//    straight onto pin 11 (the board's "SIGIN / Sync / RingMod" jack has no series resistor) sets the
//    node's voltage through its own source impedance, and the VCO's frequency is proportional to the
//    current the chip then has to supply (r1PinCurrent()), so it is exactly linear in the patched
//    voltage until a limit bites: a current modulation, i.e. linear FM. The chip's R1 drive is taken as source-only
//    (a signal above VCOIN stops the oscillator) and the node cannot fall below one diode drop under
//    VSS (the input protection diodes). Those two limits are assumptions.

#include <cmath>

namespace sixfigures {
namespace cd4046 {

/** Supply the module's 4046 is assumed to run from (the board's VCC; a modular 12 V). Assumed. */
static const double kVdd = 12.0;
/** The board's timing parts. C1 is the module's RANGE switch: the board's 10 nF, and a 500x larger
    capacitor for the LFO range (the module's own addition; the board has one C1). */
static const double kR1 = 100e3;
static const double kC1Audio = 10e-9;
static const double kC1Lfo = 5e-6;

/** Estimated output resistance of the R1-pin drive (see the header comment). */
static const double kRint = 2200.0;
/** Lowest voltage the R1 node can be pulled to: VSS less a protection diode. Assumed. */
static const double kNodeFloor = -0.6;

struct Line {
	double r1, vdd;
	int n;
	double c_pF[3];  // C1 at each knot, in pF
	double f_Hz[3];  // f0 at each knot
};

/** Fig. 7 of the HEF4046B data sheet, one row per plotted line. */
static const Line kFig7[9] = {
	{1e4, 15, 3, {50.32, 1064.0, 1.005e6}, {1.738e6, 1.022e5, 109.6}},
	{1e4, 10, 3, {51.93, 897.7, 1.005e6},  {1.191e6, 8.624e4, 83.08}},
	{1e4, 5,  3, {51.93, 604.3, 9.789e5},  {4.81e5, 5.8e4, 39.54}},
	{1e5, 15, 3, {50.63, 348.0, 9.912e5},  {2.288e5, 3.312e4, 12.73}},
	{1e5, 10, 2, {50.0, 1.016e6, 0.0},     {1.713e5, 8.944, 0.0}},
	{1e5, 5,  2, {49.38, 1.016e6, 0.0},    {8.462e4, 4.887, 0.0}},
	{1e6, 15, 2, {49.38, 9.942e5, 0.0},    {2.172e4, 1.254, 0.0}},
	{1e6, 10, 2, {49.38, 9.789e5, 0.0},    {1.489e4, 1.025, 0.0}},
	{1e6, 5,  2, {49.38, 4.964e5, 0.0},    {8884.0, 1.013, 0.0}},
};

/** f0 on one line at C1 (pF): straight in log-log between knots, last segment continued either way. */
inline double lineF0(const Line& L, double cpF) {
	int i = 0;
	while (i < L.n - 2 && cpF > L.c_pF[i + 1])
		i++;
	double x0 = std::log(L.c_pF[i]), x1 = std::log(L.c_pF[i + 1]);
	double y0 = std::log(L.f_Hz[i]), y1 = std::log(L.f_Hz[i + 1]);
	return std::exp(y0 + (y1 - y0) * (std::log(cpF) - x0) / (x1 - x0));
}

/** Centre frequency f0 (VCOIN = VDD/2, R2 = infinity) for any VDD, R1, C1 (farads). */
inline double centerFreq(double vdd, double r1, double c1) {
	const double cpF = c1 * 1e12;
	// The 3 x 3 lattice is ordered R1 = 10k,100k,1M outer and VDD = 15,10,5 inner.
	double lr = std::log(r1), lv = std::log(vdd);
	int ri = lr < std::log(1e5) ? 0 : 1;          // lower R1 line of the pair bracketing r1
	double rLo = std::log(ri == 0 ? 1e4 : 1e5), rHi = std::log(ri == 0 ? 1e5 : 1e6);
	double tr = (lr - rLo) / (rHi - rLo);
	int vi = lv < std::log(10.0) ? 0 : 1;         // pair 5-10 V, or 10-15 V
	double vLo = std::log(vi == 0 ? 5.0 : 10.0), vHi = std::log(vi == 0 ? 10.0 : 15.0);
	double tv = (lv - vLo) / (vHi - vLo);
	// Row of kFig7 for (R1 index, VDD): VDD 15 -> 0, 10 -> 1, 5 -> 2.
	auto at = [&](int rIdx, int vdd5_10_15) {      // vdd5_10_15: 0 = 5 V, 1 = 10 V, 2 = 15 V
		return std::log(lineF0(kFig7[rIdx * 3 + (2 - vdd5_10_15)], cpF));
	};
	double acc = 0;
	for (int a = 0; a < 2; a++)
		for (int b = 0; b < 2; b++) {
			double w = (a ? tr : 1 - tr) * (b ? tv : 1 - tv);
			acc += w * at(ri + a, vi + b);
		}
	return std::exp(acc);
}

/** The "maximum operating frequency" speed limit, Nexperia typicals at R1 = 10k, C1 = 50 pF. */
inline double speedLimit(double vdd) {
	static const double v[3] = {5, 10, 15}, f[3] = {1.0e6, 2.0e6, 2.7e6};
	int i = vdd < 10 ? 0 : 1;
	double t = (vdd - v[i]) / (v[i + 1] - v[i]);
	double r = f[i] + (f[i + 1] - f[i]) * t;
	return r < 1e5 ? 1e5 : r;
}

/** What is patched onto pin 11: a Thevenin source, or nothing. */
struct Jack {
	bool patched;
	double volts;
	double ohms;
	Jack() : patched(false), volts(0), ohms(1000) {}
	Jack(double v, double r) : patched(true), volts(v), ohms(r) {}
};

/** Current the chip drives out of pin 11, amps. The drive is VCOIN behind kRint into R1 to ground in
    parallel with whatever is patched. Source-only. */
inline double r1PinCurrent(double vcoin, double r1, const Jack& jack) {
	double g = 1.0 / kRint + 1.0 / r1, gi = vcoin / kRint;
	if (jack.patched) {
		double gj = 1.0 / (jack.ohms > 1e-3 ? jack.ohms : 1e-3);
		g += gj;
		gi += jack.volts * gj;
	}
	double vn = gi / g;
	if (vn < kNodeFloor)
		vn = kNodeFloor;
	double i = (vcoin - vn) / kRint;
	return i > 0 ? i : 0;
}

/** Output frequency, Hz. R2 not fitted, INH low. VCOIN is clamped to the rails by the input diodes. */
inline double freq(double vcoin, double vdd, double r1, double c1, const Jack& jack = Jack()) {
	double v = vcoin < 0 ? 0 : (vcoin > vdd ? vdd : vcoin);
	double f0 = centerFreq(vdd, r1, c1);
	double f = 2.0 * f0 * (r1 + kRint) / vdd * r1PinCurrent(v, r1, jack);
	double cap = speedLimit(vdd);
	return f > cap ? cap : f;
}

/** The VCOIN that gives `f` with nothing patched; clamps to the rails (the chip cannot go beyond). */
inline double vcoinFor(double f, double vdd, double r1, double c1) {
	double v = f * vdd / (2.0 * centerFreq(vdd, r1, c1));
	return v < 0 ? 0 : (v > vdd ? vdd : v);
}

} // namespace cd4046
} // namespace sixfigures

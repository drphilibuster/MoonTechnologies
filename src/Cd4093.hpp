#pragma once
// The CD4093B quad 2-input NAND Schmitt trigger, as its datasheet draws it, and the relaxation
// oscillator one gate makes with a resistor and a capacitor.
//
// Source: TI CD4093B (SCHS115D, Harris data, rev. Sept 2003)
//   https://www.ti.com/lit/ds/symlink/cd4093b.pdf
//   "Static Electrical Characteristics" (page 2) at TA = 25 C: positive trigger threshold VP and
//   negative trigger threshold VN, min/typ/max, at VDD = 5, 10, 15 V; and, per the footnotes,
//   in two wirings: (a) one input switching with the other held at VDD, (b) both inputs tied
//   together. The two differ -- (b) has the higher VN -- and the circuits this header is for
//   (Rene Schmitz's YASH: every gate drawn with its inputs tied) are wiring (b).
//   IOL/IOH typical sink/source currents and the "Dynamic Electrical Characteristics" propagation
//   delay tPHL = tPLH (typ 190 / 90 / 65 ns at 5 / 10 / 15 V, CL = 50 pF).
//   The values were read from the page image, the datasheet's tables being bitmaps.
//
// NOT the same table as the CD40106B's (src/Cd40106.hpp): the inverter's wiring-(a) thresholds are
// the same numbers, but the 4093's tied-input rows are not, and the delay and drive differ, so
// Cd40106.hpp is not reused.
//
// What is modelled: the gate's logic with the datasheet's thresholds (interpolated linearly in
// VDD between the three points, extended along the end segments between 3 V and 18 V), the output
// as a switch to a rail through the on resistance the sink/source rows give (VO / IO at the test
// point, so a little above the true small-signal value), the typical propagation delay, and the
// astable in closed form with edges positioned to a fraction of a sample.
// Not modelled: input capacitance (5 pF typ), input leakage, output transition time (40 ns at
// 15 V), temperature, supply current spikes.
//
// No Rack dependency, so tests/ drives it bare.

#include <cmath>

namespace cd4093 {

enum Corner { CORNER_MIN = -1, CORNER_TYP = 0, CORNER_MAX = 1 };
// (a) one input driven, the other at VDD; (b) both inputs tied.
enum Wiring { ONE_INPUT = 0, TIED = 1 };

// --- the datasheet, TA = 25 C, VDD = 5, 10, 15 V ------------------------------------------------
static const double kVpMin[2][3] = { { 2.2, 4.6, 6.8 },  { 2.6, 5.6, 6.3 } };
static const double kVpTyp[2][3] = { { 2.9, 5.9, 8.8 },  { 3.3, 7.0, 9.4 } };
static const double kVpMax[2][3] = { { 3.6, 7.1, 10.8 }, { 4.0, 8.2, 12.7 } };
static const double kVnMin[2][3] = { { 0.9, 2.5, 4.0 },  { 1.4, 3.4, 4.8 } };
static const double kVnTyp[2][3] = { { 1.9, 3.9, 5.8 },  { 2.3, 5.1, 7.3 } };
static const double kVnMax[2][3] = { { 2.8, 5.2, 7.4 },  { 3.2, 6.6, 9.6 } };
// IOL typ at VOL = 0.4 / 0.5 / 1.5 V; IOH typ at VDD - VOH = 0.4 / 0.5 / 1.5 V.
static const double kRonTyp[3] = { 0.4 / 1.0e-3, 0.5 / 2.6e-3, 1.5 / 6.8e-3 };
static const double kTpdTyp[3] = { 190e-9, 90e-9, 65e-9 };

inline double interp(const double* y, double vdd) {
	if (vdd < 3.0) vdd = 3.0;
	if (vdd > 18.0) vdd = 18.0;
	if (vdd <= 10.0) return y[0] + (y[1] - y[0]) * (vdd - 5.0) / 5.0;
	return y[1] + (y[2] - y[1]) * (vdd - 10.0) / 5.0;
}

struct Thresholds { double vp, vn; };

inline Thresholds thresholds(double vdd, int corner = CORNER_TYP, int wiring = TIED) {
	const double* vp = corner < 0 ? kVpMin[wiring] : corner > 0 ? kVpMax[wiring] : kVpTyp[wiring];
	const double* vn = corner < 0 ? kVnMin[wiring] : corner > 0 ? kVnMax[wiring] : kVnTyp[wiring];
	Thresholds t;
	t.vp = interp(vp, vdd);
	t.vn = interp(vn, vdd);
	return t;
}
inline double ron(double vdd) { return interp(kRonTyp, vdd); }
inline double tpd(double vdd) { return interp(kTpdTyp, vdd); }

// --- the gate with its inputs tied: an inverting Schmitt -----------------------------------------
struct Inverter {
	bool out = true;
	bool process(double vin, double vdd, int corner = CORNER_TYP, int wiring = TIED) {
		Thresholds t = thresholds(vdd, corner, wiring);
		if (vin >= t.vp) out = false;
		else if (vin <= t.vn) out = true;
		return out;
	}
	void reset() { out = true; }
};

// --- the astable, in closed form ------------------------------------------------------------------
struct Timing { double tHigh, tLow, period; };

/** The textbook law with the on resistance and the delay left in or out. Output high charges the
    node from the trough to VP through R + Ron; output low discharges it from the peak to VN. With
    a delay d the node overshoots to Vpk = VDD - (VDD - VP) e^(-d/tau) and undershoots to
    Vtr = VN e^(-d/tau) before the output follows. */
inline Timing timing(double r, double c, double vdd, int corner = CORNER_TYP,
                     bool withRon = true, bool withTpd = true) {
	Thresholds th = thresholds(vdd, corner, TIED);
	double tau = (r + (withRon ? ron(vdd) : 0.0)) * c;
	double d = withTpd ? tpd(vdd) : 0.0;
	double e = std::exp(-d / tau);
	double vtr = th.vn * e;
	double vpk = vdd - (vdd - th.vp) * e;
	Timing t;
	t.tHigh = tau * std::log((vdd - vtr) / (vdd - th.vp)) + d;
	t.tLow = tau * std::log(vpk / th.vn) + d;
	t.period = t.tHigh + t.tLow;
	return t;
}

struct Edge { double at; int dir; };   // seconds into the step; +1 rising output, -1 falling

struct Astable {
	double v = 0.0;            // the capacitor / input node, volts
	bool high = true;          // the output
	double pending = -1.0;     // seconds until the output follows the input (propagation delay)
	bool fresh = true;
	Edge edges[8];
	int nEdges = 0;

	void reset() { *this = Astable(); }

	/** Advance `dt` seconds with `r` from output to input (the whole series resistance, without
	    the gate's own), `c` from input to ground, supply `vdd`. The output edges inside the step
	    are left in edges[0..nEdges). */
	void step(double dt, double r, double c, double vdd, int corner = CORNER_TYP) {
		nEdges = 0;
		const Thresholds th = thresholds(vdd, corner, TIED);
		const double tau = (r + ron(vdd)) * c;
		const double d = tpd(vdd);
		if (fresh) {
			fresh = false;
			v = th.vn * std::exp(-d / tau);
			high = true;
			pending = -1.0;
		}
		double left = dt, now = 0.0;
		for (int guard = 0; guard < 24 && left > 1e-18; guard++) {
			double limit = left;
			if (pending >= 0.0 && pending < limit) limit = pending;
			double used = limit;
			bool crossed = false;
			const double target = high ? vdd : 0.0;
			if (pending < 0.0 && (high ? (v >= th.vp) : (v <= th.vn))) {
				used = 0.0; crossed = true;
			}
			else {
				const double v1 = target + (v - target) * std::exp(-limit / tau);
				if (pending < 0.0 && (high ? (v1 >= th.vp) : (v1 <= th.vn))) {
					const double goal = high ? th.vp : th.vn;
					const double tc = tau * std::log((target - v) / (target - goal));
					used = tc < limit ? tc : limit;
					v = goal;
					crossed = true;
				}
				else {
					v = v1;
				}
			}
			now += used; left -= used;
			if (pending >= 0.0) {
				pending -= used;
				if (pending <= 1e-18) flip(now);
			}
			else if (crossed) {
				if (d <= 0.0) flip(now); else pending = d;
			}
		}
	}

private:
	void flip(double now) {
		high = !high;
		pending = -1.0;
		if (nEdges < 8) { edges[nEdges].at = now; edges[nEdges].dir = high ? 1 : -1; nEdges++; }
	}
};

} // namespace cd4093

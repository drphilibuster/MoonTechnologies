#pragma once
// The CD40106B / HEF40106B hex Schmitt-trigger inverter, as its datasheet draws it, and the
// relaxation oscillator one inverter makes with a resistor and a capacitor.
//
// Sources:
//   TI CD40106B (SCHS097F, rev. March 2017)  https://www.ti.com/lit/ds/symlink/cd40106b.pdf
//     "Electrical Characteristics: Static" at TA = 25 C: positive trigger threshold VP, negative
//     trigger threshold VN, hysteresis VH, IOL/IOH sink and source current, VOL/VOH; and
//     "Switching Characteristics": propagation delay tPHL = tPLH. The datasheet gives all of them
//     at VDD = 5, 10 and 15 V, each with a minimum, a typical and a maximum.
//   Nexperia HEF40106B is the same function; its thresholds differ a little and are not used.
//   The board: MiaW Day 1 "1.1 Schematic_40106 Hex oscillator bank" (Kristian Blasol, 2019-05-11):
//     an astable per inverter, a pot from output to input and a capacitor from input to ground,
//     plus an "In" jack through a 1N4448 and 1k into the same node. And Day 9 "XORbell" (from
//     Elliot Williams' Logic Noise): six astables, each a 100k pot and 0.1 uF.
//
// What is modelled
//   * The inverter: output goes low once the input reaches VP, high once it falls to VN, and holds
//     in between. VP and VN are the datasheet's values, interpolated linearly in VDD between the
//     5, 10 and 15 V points (and extended along the end segments between 3 V and 18 V). A corner
//     selects the datasheet's minimum, typical or maximum row; every part sits somewhere in that
//     band and the datasheet does not say where.
//   * The output as a switch to the rail through the on resistance the datasheet's sink/source
//     rows give: VO / IO at the specified test point (0.4 V at 1 mA at 5 V, 0.5 V at 2.6 mA at
//     10 V, 1.5 V at 6.8 mA at 15 V, typical). That is a secant to a point near the knee of the
//     output curve, so it is a little above the true small-signal resistance; the datasheet's
//     output curves (its Figure 5) are not tabulated and are not used.
//   * The propagation delay, typical, equal for both edges: the node keeps charging towards the
//     old rail for that long after the input crosses, so the thresholds are overshot slightly.
//   * The astable, exactly: the capacitor node is integrated through its true exponential,
//     closed form while nothing else touches the node, and the injection path (a 1N4448 -- the
//     SPICE parameters Is = 2.52 nA, n = 1.752, the same as Installment/Envelope.hpp uses -- and
//     1k) stepped numerically with the trapezoid rule when something is injected. The inverter's
//     input protection diodes to VDD and VSS are a generic silicon junction (Is = 1e-14 A,
//     n = 1; ASSUMED -- the datasheet gives no parameters) and only matter when the injection
//     pushes the node outside the rails.
//   * Edge positions are found to a fraction of a sample, and the output carries a polyBLEP
//     correction at each one, so a sample-rate astable sounds like the circuit and not like a
//     counter.
//
// Not modelled: the supply current spikes, the input capacitance and leakage (pA), the output
// transition time (100 ns at 5 V down to 40 ns at 15 V), temperature, and the other five
// inverters sharing the die.
//
// No Rack dependency, so tests/ drives it bare.

#include <cmath>

namespace cd40106 {

enum Corner { CORNER_MIN = -1, CORNER_TYP = 0, CORNER_MAX = 1 };

// --- the datasheet, at VDD = 5, 10, 15 V (TA = 25 C) ---------------------------------------
static const double kVpMin[3] = { 2.2, 4.6,  6.8 };
static const double kVpTyp[3] = { 2.9, 5.9,  8.8 };
static const double kVpMax[3] = { 3.6, 7.1, 10.8 };
static const double kVnMin[3] = { 0.9, 2.5,  4.0 };
static const double kVnTyp[3] = { 1.9, 3.9,  5.8 };
static const double kVnMax[3] = { 2.8, 5.2,  7.4 };
// Output resistance from VO / IO at the datasheet's test points, typical row.
static const double kRonTyp[3] = { 0.4 / 1.0e-3, 0.5 / 2.6e-3, 1.5 / 6.8e-3 };
// Propagation delay (CL = 50 pF, RL = 200 k), typical, seconds.
static const double kTpdTyp[3] = { 140e-9, 70e-9, 60e-9 };

/** Piecewise-linear through the 5 / 10 / 15 V points, the end segments extended; VDD limited to
    the datasheet's 3..18 V recommended range. */
inline double interp(const double* y, double vdd) {
	double x = vdd < 3.0 ? 3.0 : (vdd > 18.0 ? 18.0 : vdd);
	if (x <= 10.0) return y[0] + (y[1] - y[0]) * (x - 5.0) / 5.0;
	return y[1] + (y[2] - y[1]) * (x - 10.0) / 5.0;
}

struct Thresholds { double vp, vn; };

inline Thresholds thresholds(double vdd, int corner = CORNER_TYP) {
	const double* p = corner < 0 ? kVpMin : (corner > 0 ? kVpMax : kVpTyp);
	const double* n = corner < 0 ? kVnMin : (corner > 0 ? kVnMax : kVnTyp);
	Thresholds t = { interp(p, vdd), interp(n, vdd) };
	return t;
}
inline double ron(double vdd) { return interp(kRonTyp, vdd); }
inline double tpd(double vdd) { return interp(kTpdTyp, vdd); }

// --- the astable, in closed form ---------------------------------------------------------------
struct Timing { double tHigh, tLow, period; };

/** The astable's timing with a resistor `r` from output to input and `c` from input to ground, no
    injection. With the on resistance and delay left out it is the textbook law,
        tHigh = R C ln[(VDD - VN) / (VDD - VP)],   tLow = R C ln[VP / VN],
    (high = the output high, charging the node from VN to VP). With them, R becomes R + Ron and
    each phase is longer by one propagation delay, plus the part of it spent travelling on past the
    threshold: the node overshoots to Vpk = VDD - (VDD - VP) e^(-d/tau) before the output falls and
    undershoots to Vtr = VN e^(-d/tau) before it rises, and the next phase starts from there. */
inline Timing timing(double r, double c, double vdd, int corner = CORNER_TYP,
                     bool withRon = true, bool withTpd = true) {
	Thresholds th = thresholds(vdd, corner);
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

/** The resistor that gives `period` seconds with `c` (bisection; the period is monotone in R). */
inline double resistanceForPeriod(double period, double c, double vdd, int corner = CORNER_TYP,
                                  bool withRon = true, bool withTpd = true) {
	double lo = 1.0, hi = 1.0e10;
	for (int i = 0; i < 80; i++) {
		double mid = std::sqrt(lo * hi);
		if (timing(mid, c, vdd, corner, withRon, withTpd).period < period) lo = mid; else hi = mid;
	}
	return std::sqrt(lo * hi);
}

// --- the inverter by itself -------------------------------------------------------------------
/** A Schmitt inverter's logic: the output state for the input voltage, with the datasheet's
    thresholds at the given supply. Holds between VN and VP. */
struct Inverter {
	bool out = true;      // output high (input read low) at power-up
	/** Returns the output level. */
	bool process(double vin, double vdd, int corner = CORNER_TYP) {
		Thresholds t = thresholds(vdd, corner);
		if (vin >= t.vp) out = false;
		else if (vin <= t.vn) out = true;
		return out;
	}
	void reset() { out = true; }
};

// --- the 1N4448 and the input protection diodes ----------------------------------------------
static const double kInjIs  = 2.52e-9;                // 1N4148/4448 SPICE
static const double kInjNVt = 1.752 * 0.025852;       // V
static const double kProtIs = 1.0e-14;                // ASSUMED generic junction
static const double kProtNVt = 0.025852;
static const double kInjR   = 1000.0;                 // the schematic's 1k

/** Current through a diode in series with `rs` when `delta` volts is across the pair (zero for
    delta <= 0), and its slope; Newton on the diode's own voltage x + rs Is (e^(x/nVt) - 1) = delta. */
inline double diodeSeries(double delta, double rs, double& slope) {
	if (!(delta > 0.0)) { slope = 0.0; return 0.0; }
	double x = delta < 0.9 ? delta : 0.9;
	for (int k = 0; k < 80; k++) {
		double e = std::exp(x / kInjNVt);
		double h = x + rs * kInjIs * (e - 1.0) - delta;
		double hp = 1.0 + rs * kInjIs / kInjNVt * e;
		double dx = h / hp;
		x -= dx;
		if (x < 0.0) x = 0.0;
		if (std::fabs(dx) < 1e-13) break;
	}
	double e = std::exp(x / kInjNVt);
	double hp = 1.0 + rs * kInjIs / kInjNVt * e;
	slope = (kInjIs / kInjNVt * e) / hp;
	return kInjIs * std::expm1(x / kInjNVt);
}

/** The injection input the schematic draws: In jack, 1N4448, 1k, timing node. */
inline double injectionCurrent(double vin, double node) {
	double s;
	return diodeSeries(vin - node, kInjR, s);
}

// --- the astable, stepped ---------------------------------------------------------------------
struct Edge { double at; int dir; };   // seconds into the step; +1 rising output, -1 falling

struct Astable {
	double v = 0.0;            // the capacitor / input node, volts
	bool high = true;          // the output
	double pending = -1.0;     // seconds until the output follows the input (propagation delay)
	bool fresh = true;         // start in the steady-state trough on the first step
	double clock = 0.0;        // seconds run, for the period read-out
	double lastRise = -1.0;
	double lastPeriod = 0.0;
	Edge edges[8];
	int nEdges = 0;

	void reset() { *this = Astable(); }

	/** The measured frequency: the last full rise-to-rise period, or the time since the last
	    rise when that is longer (an oscillator that has stopped reads slower and slower). */
	double frequency() const {
		double p = lastPeriod;
		if (lastRise >= 0.0 && clock - lastRise > p) p = clock - lastRise;
		return p > 0.0 ? 1.0 / p : 0.0;
	}

	/** Advance `dt` seconds with a resistor `r` (output to input), capacitor `c`, supply `vdd`,
	    and `vin` volts at the In jack (the diode path is ignored while it is below 0.2 V, where
	    it cannot conduct). The output edges inside the step are left in edges[0..nEdges). */
	void step(double dt, double r, double c, double vdd, int corner = CORNER_TYP, double vin = 0.0) {
		nEdges = 0;
		const Thresholds th = thresholds(vdd, corner);
		const double rt = r + ron(vdd);
		const double tau = rt * c;
		const double d = tpd(vdd);
		const bool inj = vin > 0.2;
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
			if (inj) integrateNumeric(limit, rt, c, vdd, th, vin, used, crossed);
			else integrateExact(limit, tau, vdd, th, used, crossed);
			now += used; left -= used; clock += used;
			if (pending >= 0.0) {
				pending -= used;
				if (pending <= 1e-18) flip(now, dt);
			}
			else if (crossed) {
				if (d <= 0.0) flip(now, dt); else pending = d;
			}
		}
	}

private:
	void flip(double now, double dt) {
		(void)dt;
		high = !high;
		pending = -1.0;
		if (nEdges < 8) { edges[nEdges].at = now; edges[nEdges].dir = high ? 1 : -1; nEdges++; }
		if (high) {
			if (lastRise >= 0.0) lastPeriod = (clock + 0.0) - lastRise;
			lastRise = clock;
		}
	}

	void integrateExact(double limit, double tau, double vdd, const Thresholds& th,
	                    double& used, bool& crossed) {
		const double target = high ? vdd : 0.0;
		used = limit;
		crossed = false;
		const double v1 = target + (v - target) * std::exp(-limit / tau);
		if (pending < 0.0) {
			if (high ? (v >= th.vp) : (v <= th.vn)) { used = 0.0; crossed = true; return; }
			if (high ? (v1 >= th.vp) : (v1 <= th.vn)) {
				const double tc = high ? tau * std::log((vdd - v) / (vdd - th.vp))
				                       : tau * std::log(v / th.vn);
				used = tc < limit ? tc : limit;
				v = high ? th.vp : th.vn;
				crossed = true;
				return;
			}
		}
		v = v1;
	}

	// The node's net current (into the capacitor) at x, and its slope.
	double nodeCurrent(double x, double target, double rt, double vdd, double vin, double& slope) const {
		double s1;
		double i = (target - x) / rt + diodeSeries(vin - x, kInjR, s1);
		slope = -1.0 / rt - s1;
		// input protection: to VDD above it, to VSS below zero
		double a = (x - vdd) / kProtNVt; if (a > 40.0) a = 40.0;
		double ea = std::exp(a);
		i -= kProtIs * (ea - 1.0);
		slope -= kProtIs * ea / kProtNVt;
		double b = (-x) / kProtNVt; if (b > 40.0) b = 40.0;
		double eb = std::exp(b);
		i += kProtIs * (eb - 1.0);
		slope -= kProtIs * eb / kProtNVt;
		return i;
	}

	// One trapezoid step of length h from x0.
	double trap(double x0, double h, double target, double rt, double c, double vdd, double vin) const {
		double s0, s1;
		double i0 = nodeCurrent(x0, target, rt, vdd, vin, s0);
		double x = x0 + h / c * i0;
		for (int k = 0; k < 30; k++) {
			double i1 = nodeCurrent(x, target, rt, vdd, vin, s1);
			double f = c * (x - x0) / h - 0.5 * (i0 + i1);
			double fp = c / h - 0.5 * s1;
			double dx = f / fp;
			x -= dx;
			if (std::fabs(dx) < 1e-11) break;
		}
		return x;
	}

	void integrateNumeric(double limit, double rt, double c, double vdd, const Thresholds& th,
	                      double vin, double& used, bool& crossed) {
		const double target = high ? vdd : 0.0;
		const double tauFast = (rt < kInjR ? rt : kInjR) * c;
		double hmax = 0.04 * tauFast;
		if (hmax < limit / 160.0) hmax = limit / 160.0;
		crossed = false;
		if (pending < 0.0 && (high ? (v >= th.vp) : (v <= th.vn))) { used = 0.0; crossed = true; return; }
		double t = 0.0;
		while (t < limit * (1.0 - 1e-12)) {
			double h = limit - t < hmax ? limit - t : hmax;
			double x1 = trap(v, h, target, rt, c, vdd, vin);
			if (pending < 0.0 && (high ? (x1 >= th.vp) : (x1 <= th.vn))) {
				const double goal = high ? th.vp : th.vn;
				double hh = h * (goal - v) / (x1 - v);
				for (int k = 0; k < 6; k++) {
					double xh = trap(v, hh, target, rt, c, vdd, vin);
					if (std::fabs(xh - goal) < 1e-10 || xh == v) break;
					hh *= (goal - v) / (xh - v);
					if (hh > h) hh = h;
				}
				used = t + hh;
				v = goal;
				crossed = true;
				return;
			}
			v = x1;
			t += h;
		}
		used = limit;
	}
};

// --- band-limited square from the astable ------------------------------------------------------
/** An Astable read out as a bipolar (+1 / -1) square with polyBLEP at each edge. The correction
    for an edge reaches the sample before it, so the output runs one sample behind the circuit. */
struct Osc {
	Astable a;
	double prevNaive = 1.0;
	double prevCarry = 0.0;
	bool primed = false;

	void reset() { a.reset(); prevNaive = 1.0; prevCarry = 0.0; primed = false; }

	/** `vin` in volts at the injection jack (0 for none). Returns the square. */
	float process(double dt, double r, double c, double vdd, int corner = CORNER_TYP, double vin = 0.0) {
		a.step(dt, r, c, vdd, corner, vin);
		double pre = 0.0, carry = 0.0;
		for (int k = 0; k < a.nEdges; k++) {
			double dd = (dt - a.edges[k].at) / dt;       // fraction of the step before its end
			double half = a.edges[k].dir;                // jump of 2 -> half = +-1
			pre += half * dd * dd;
			carry -= half * (1.0 - dd) * (1.0 - dd);
		}
		if (!primed) { primed = true; prevNaive = a.high ? 1.0 : -1.0; prevCarry = carry; return (float)prevNaive; }
		double out = prevNaive + prevCarry + pre;
		prevNaive = a.high ? 1.0 : -1.0;
		prevCarry = carry;
		return (float)out;
	}
};

} // namespace cd40106

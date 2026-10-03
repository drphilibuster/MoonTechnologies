#pragma once
// Rene Schmitz's YASH ("Yet Another Sample and Hold", 1999): the trigger and sample-pulse
// circuit that feeds an LF398 (src/Lf398.hpp), and the free-running oscillator behind the
// trigger jack.
//
// Source: https://schmitzbits.de/yash.png (schematic, copy in MiawResearch/research17/yash.png)
//         https://schmitzbits.de/sah.html (his page; "3 us gives about 1%" for the pulse)
// Values read from the image:
//   Trigger path: jack -> 4k7 -> base of a BC548 (1N4148 base to ground), collector pulled up by
//     2.2k through an LED -> gate 1 (1/4 4093, inputs tied) -> 470 pF -> node with 10k to ground
//     -> gate 2 -> gate 3 -> 5k6 -> pin 8 of the LF398, with 1k8 from pin 8 to ground.
//   Oscillator: one 4093 gate with its inputs tied to a node that has 470 nF to ground; from the
//     output back to that node a 1 Meg potentiometer wired as a variable resistor in series
//     with 2k2. The oscillator's output goes to the same line as the trigger jack's switching
//     contact, so with nothing plugged in the oscillator is the trigger.
//   The 4093's supply is the unlabelled arrow on "pin 14": ASSUMED +15 V (the LF398's own rail;
//     the 5k6 / 1k8 divider gives 3.5 V of logic high from it, above the LF398's 2.4 V maximum
//     logic threshold, and would give only 1.2 V from 5 V, below its 1.4 V typical).
//
// What is modelled
//   * The BC548 stage's switching thresholds, from an Ebers-Moll collector current (ASSUMED
//     IS = 2e-14, BF = 290, red LED 1.9 V): the base voltage at which the collector crosses gate
//     1's VN (rising trigger) and VP (falling). About 0.70 V and 0.69 V.
//   * The differentiator exactly: 470 pF into 10k with gate 1's on resistance in series, gate 2's
//     datasheet VP / VN (tied-input rows) deciding when it flips, gate 2's input protection
//     diode clamping the negative spike at -0.6 V, and each gate's propagation delay. The
//     pulse is 4.8 us * ln(14.7 V / VN): 3.4 us with the typical VN of 7.3 V, which is
//     Schmitz's "3 us pulses". Retriggering inside a pulse extends it; a falling trigger edge
//     inside one cuts it short, as the circuit does.
//   * The oscillator with the datasheet's thresholds and delay (src/Cd4093.hpp).
//
// Not modelled: the BC548's turn-on and storage times (tens of ns to a us, on the trigger
// edge's timing only), the LED, 5 pF of gate input capacitance (about 1% on the pulse), the
// divider's loading of gate 3, the LF398's logic input current (2 to 10 uA).
//
// No Rack dependency, so tests/ drives it bare.

#include "../Cd4093.hpp"
#include <cmath>

namespace yash {

static const double kVdd    = 15.0;
static const double kRdiff  = 10e3;
static const double kCdiff  = 470e-12;
static const double kCosc   = 470e-9;
static const double kRoscFixed = 2.2e3;
static const double kPot    = 1e6;
static const double kRtrig  = 4.7e3;
static const double kRcoll  = 2.2e3;
static const double kVled   = 1.9;       // ASSUMED red LED
static const double kBjtIs  = 2e-14;     // ASSUMED
static const double kBjtBf  = 290.0;     // ASSUMED (BC548B typical)
static const double kClamp  = 0.6;       // gate input protection diode
static const double kDividerTop = 5.6e3, kDividerBot = 1.8e3;

struct TrigThresholds { double on, off; };

/** The base voltage at which the BC548's collector sits exactly at `vc`. */
inline double baseVoltageForCollector(double vc, double vdd) {
	double ic = (vdd - kVled - vc) / kRcoll;
	if (ic <= 0.0) return 0.0;
	double vbe = 0.025852 * std::log(ic / kBjtIs + 1.0);
	return vbe + (ic / kBjtBf) * kRtrig;
}

inline TrigThresholds trigThresholds(int corner = cd4093::CORNER_TYP, double vdd = kVdd) {
	cd4093::Thresholds th = cd4093::thresholds(vdd, corner, cd4093::TIED);
	TrigThresholds t;
	t.on = baseVoltageForCollector(th.vn, vdd);    // collector falls through gate 1's VN
	t.off = baseVoltageForCollector(th.vp, vdd);   // and rises through its VP
	return t;
}

/** Closed-form pulse width for one isolated trigger edge, delays excluded: the time the
    differentiator node spends above gate 2's VN. */
inline double pulseWidth(int corner = cd4093::CORNER_TYP, double vdd = kVdd) {
	double ron = cd4093::ron(vdd);
	double tau = (kRdiff + ron) * kCdiff;
	double v0 = vdd * kRdiff / (kRdiff + ron);
	cd4093::Thresholds th = cd4093::thresholds(vdd, corner, cd4093::TIED);
	if (v0 <= th.vn) return 0.0;
	return tau * std::log(v0 / th.vn);
}

/** Pin 8's voltage while the pulse is high, loaded by the divider (gate 3 through its on
    resistance into 5k6 + 1k8). */
inline double pin8High(double vdd = kVdd) {
	double rl = kDividerTop + kDividerBot;
	double ron = cd4093::ron(vdd);
	return vdd * kDividerBot / (rl + ron);
}

/** A trigger voltage crossing the BC548's thresholds, found by straight-line interpolation
    between two audio samples. */
struct TrigInput {
	bool on = false;
	TrigThresholds th = trigThresholds();
	void reset() { on = false; }
	/** Edges inside the step: t[k] seconds from its start, dir[k] = +1 / -1. Returns how many. */
	int process(double v0, double v1, double dt, double* t, int* dir) {
		if (!on && v1 >= th.on) {
			double tc = (v1 > v0) ? (th.on - v0) / (v1 - v0) * dt : 0.0;
			if (tc < 0.0) tc = 0.0;
			if (tc > dt) tc = dt;
			t[0] = tc; dir[0] = 1; on = true;
			return 1;
		}
		if (on && v1 <= th.off) {
			double tc = (v1 < v0) ? (v0 - th.off) / (v0 - v1) * dt : 0.0;
			if (tc < 0.0) tc = 0.0;
			if (tc > dt) tc = dt;
			t[0] = tc; dir[0] = -1; on = false;
			return 1;
		}
		return 0;
	}
};

struct Pulse { double a, b; };

/** Gate 1 -> 470p / 10k -> gate 2 -> gate 3 -> divider -> LF398 pin 8. */
struct PulseChain {
	int corner = cd4093::CORNER_TYP;
	double vdd = kVdd;

	void reset() {
		x = 0.0; g1 = false; g2Low = false; pin8 = false; winStart = 0.0;
		nG1 = 0; nPin = 0;
	}

	/** A trigger edge (the BC548 switching) at `t` seconds into the step about to be run. */
	void pushTrigger(double t, int dir) {
		// BC548 delay ASSUMED zero; gate 1's propagation delay is the datasheet's.
		insertG1(t + cd4093::tpd(vdd), dir);
	}

	/** Run one step of `dt` seconds. The stretches pin 8 is high are returned in `out`
	    (a == 0 if it was already high, b == dt if it still is). */
	int advance(double dt, Pulse* out, int maxOut) {
		const cd4093::Thresholds th = cd4093::thresholds(vdd, corner, cd4093::TIED);
		const double ron = cd4093::ron(vdd);
		const double tau = (kRdiff + ron) * kCdiff;
		const double k = kRdiff / (kRdiff + ron);      // node voltage = x * k
		const double d23 = 2.0 * cd4093::tpd(vdd);
		double tcur = 0.0;
		for (int guard = 0; guard < 64; guard++) {
			double te = (nG1 > 0 && g1T[0] < dt) ? g1T[0] : -1.0;
			if (g2Low) {
				double v2 = x * k;
				double tc = v2 <= th.vn ? tcur : tcur + tau * std::log(v2 / th.vn);
				if (tc < dt && (te < 0.0 || tc <= te)) {
					x *= std::exp(-(tc - tcur) / tau);
					tcur = tc;
					g2Low = false;
					pushPin(tc + d23, false);
					continue;
				}
			}
			if (te < 0.0) break;
			x *= std::exp(-(te - tcur) / tau);
			tcur = te;
			int dir = g1Dir[0];
			popG1();
			if (dir > 0 && !g1) { g1 = true; x += vdd; }
			else if (dir < 0 && g1) { g1 = false; x -= vdd; }
			else continue;
			if (x * k < -kClamp) x = -kClamp / k;
			double v2 = x * k;
			if (!g2Low && v2 >= th.vp) { g2Low = true; pushPin(te + d23, true); }
			else if (g2Low && v2 <= th.vn) { g2Low = false; pushPin(te + d23, false); }
		}
		x *= std::exp(-(dt - tcur) / tau);
		// pin 8's events inside the step become windows
		int n = 0;
		int used = 0;
		for (int i = 0; i < nPin; i++) {
			if (pinT[i] >= dt) break;
			used++;
			if (pinHigh[i]) { if (!pin8) { pin8 = true; winStart = pinT[i]; } }
			else if (pin8) {
				pin8 = false;
				if (n < maxOut) { out[n].a = winStart; out[n].b = pinT[i]; n++; }
			}
		}
		if (pin8) {
			if (n < maxOut) { out[n].a = winStart; out[n].b = dt; n++; }
			winStart = 0.0;
		}
		for (int i = used; i < nPin; i++) { pinT[i - used] = pinT[i] - dt; pinHigh[i - used] = pinHigh[i]; }
		nPin -= used;
		for (int i = 0; i < nG1; i++) g1T[i] -= dt;
		return n;
	}

	double x = 0.0;          // volts across the series Ron + 10k (source minus capacitor)
	bool g1 = false;         // gate 1's output
	bool g2Low = false;      // gate 2's output
	bool pin8 = false;       // sample mode
	double winStart = 0.0;

private:
	enum { N = 16 };
	double g1T[N]; int g1Dir[N]; int nG1 = 0;
	double pinT[N]; bool pinHigh[N]; int nPin = 0;

	void insertG1(double t, int dir) {
		if (nG1 >= N) return;
		int i = nG1;
		while (i > 0 && g1T[i - 1] > t) { g1T[i] = g1T[i - 1]; g1Dir[i] = g1Dir[i - 1]; i--; }
		g1T[i] = t; g1Dir[i] = dir; nG1++;
	}
	void popG1() {
		for (int i = 1; i < nG1; i++) { g1T[i - 1] = g1T[i]; g1Dir[i - 1] = g1Dir[i]; }
		nG1--;
	}
	void pushPin(double t, bool high) {
		if (nPin >= N) return;
		int i = nPin;
		while (i > 0 && pinT[i - 1] > t) { pinT[i] = pinT[i - 1]; pinHigh[i] = pinHigh[i - 1]; i--; }
		pinT[i] = t; pinHigh[i] = high; nPin++;
	}
};

/** The free-running oscillator behind the trigger jack. `potFraction` is the 1 Meg pot's
    resistance as a fraction of 1 Meg (0 .. 1). */
struct Oscillator {
	cd4093::Astable a;
	int corner = cd4093::CORNER_TYP;
	void reset() { a.reset(); }
	static double seriesR(double potFraction) {
		if (potFraction < 0.0) potFraction = 0.0;
		if (potFraction > 1.0) potFraction = 1.0;
		return kRoscFixed + kPot * potFraction;
	}
	/** Returns the number of output edges in the step; positions in a.edges. */
	int step(double dt, double rSeries) {
		a.step(dt, rSeries, kCosc, kVdd, corner);
		return a.nEdges;
	}
};

} // namespace yash

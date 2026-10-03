// The CD40106B Schmitt inverter and its astable (src/Cd40106.hpp): the datasheet's thresholds,
// the period law T = R C ln[...] against its own closed form and against an independent
// brute-force simulation of the same circuit, the supply dependence, the 1N4448 + 1k injection
// path against an independent integration, and negative controls: each check below is also run
// against a deliberately broken reference and must FAIL there, so a green run cannot be a test
// that compares the model with itself.

#include "../../src/Cd40106.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace cd40106;

static int checks = 0, failures = 0;
static void check(const char* what, bool ok) {
	checks++;
	if (!ok) { failures++; printf("  FAIL  %s\n", what); }
}
static void checkv(const char* what, double got, double want, double tol) {
	checks++;
	if (!(std::fabs(got - want) <= tol)) {
		failures++;
		printf("  FAIL  %s: got %.8g, want %.8g (tol %.3g)\n", what, got, want, tol);
	}
}
static void checkrel(const char* what, double got, double want, double rel) {
	checkv(what, got, want, std::fabs(want) * rel);
}
/** A negative control: `ok` must be false -- the comparison must notice the broken reference. */
static void control(const char* what, bool detected) {
	checks++;
	if (!detected) { failures++; printf("  FAIL  negative control did not fire: %s\n", what); }
}

// --- an independent reference for the astable ------------------------------------------------
// Datasheet numbers typed again, here, so a typo in the header is caught rather than copied.
struct Ref {
	double vp, vn, ron, tpd;
};
static Ref datasheetAt(double vdd, int corner) {
	// VP, VN at 5/10/15 V: rows MIN, TYP, MAX
	static const double VP[3][3] = { {2.2, 4.6, 6.8}, {2.9, 5.9, 8.8}, {3.6, 7.1, 10.8} };
	static const double VN[3][3] = { {0.9, 2.5, 4.0}, {1.9, 3.9, 5.8}, {2.8, 5.2, 7.4} };
	int row = corner + 1;
	auto lin = [&](const double* y) {
		if (vdd <= 10.0) return y[0] + (y[1] - y[0]) * (vdd - 5.0) / 5.0;
		return y[1] + (y[2] - y[1]) * (vdd - 10.0) / 5.0;
	};
	Ref r;
	r.vp = lin(VP[row]);
	r.vn = lin(VN[row]);
	static const double RON[3] = { 400.0, 500.0 / 2.6, 1500.0 / 6.8 };
	static const double TPD[3] = { 140e-9, 70e-9, 60e-9 };
	r.ron = lin(RON);
	r.tpd = lin(TPD);
	return r;
}

/** The textbook law with nothing else: R C [ ln((VDD - VN)/(VDD - VP)) + ln(VP/VN) ]. */
static double textbookPeriod(double r, double c, double vp, double vn, double vdd) {
	return r * c * (std::log((vdd - vn) / (vdd - vp)) + std::log(vp / vn));
}

/** Brute force: the node stepped with tiny midpoint steps, the output following the input
    crossing after `tpd`. Returns the mean period over cycles 3..8 (rising edge to rising edge). */
static double bruteForcePeriod(double r, double c, const Ref& ref, double vdd, bool useRon, bool useTpd) {
	const double rt = r + (useRon ? ref.ron : 0.0);
	const double tau = rt * c;
	const double h = tau / 40000.0;
	const double d = useTpd ? ref.tpd : 0.0;
	double v = 0.0, t = 0.0, flipAt = -1.0;
	bool high = true;
	std::vector<double> rises;
	for (long i = 0; i < 100000000 && rises.size() < 9; i++) {
		double target = high ? vdd : 0.0;
		// exact midpoint-free: use the exponential over h, which is exact for constant target
		v = target + (v - target) * std::exp(-h / tau);
		t += h;
		if (flipAt < 0.0) {
			if (high ? (v >= ref.vp) : (v <= ref.vn)) {
				if (d <= 0.0) { high = !high; if (high) rises.push_back(t); }
				else flipAt = t + d;
			}
		}
		else if (t >= flipAt) {
			flipAt = -1.0;
			high = !high;
			if (high) rises.push_back(t);
		}
	}
	return (rises[8] - rises[2]) / 6.0;
}

/** Run the model at a sample rate and return the mean rise-to-rise period over many cycles. */
static double steppedPeriod(double r, double c, double vdd, int corner, double fs, double vin = 0.0,
                            int cycles = 60) {
	Astable a;
	const double dt = 1.0 / fs;
	int rises = 0;
	double first = 0, last = 0, tnow = 0;
	for (long i = 0; i < 400000000L && rises < cycles + 3; i++) {
		a.step(dt, r, c, vdd, corner, vin);
		for (int k = 0; k < a.nEdges; k++) {
			if (a.edges[k].dir > 0) {
				double at = tnow + a.edges[k].at;
				rises++;
				if (rises == 3) first = at;
				last = at;
			}
		}
		tnow += dt;
		if (tnow > 4000.0 * r * c) break;
	}
	if (rises < cycles + 3) return 0.0;
	return (last - first) / (double)cycles;
}

// --- an independent reference for the injection path -------------------------------------------
// RK4 on C dV/dt = (target - V)/Rt + I_inj(V), I_inj from the 1N4448 + 1k found by bisection on the
// loop current. The input protection diodes are left out: the cases below stay inside the rails.
static double refInjection(double vin, double v) {
	double delta = vin - v;
	if (delta <= 0) return 0.0;
	double lo = 0.0, hi = delta / 1000.0;
	for (int i = 0; i < 200; i++) {
		double i1 = 0.5 * (lo + hi);
		double vd = 1.752 * 0.025852 * std::log(i1 / 2.52e-9 + 1.0);
		if (vd + i1 * 1000.0 < delta) lo = i1; else hi = i1;
	}
	return 0.5 * (lo + hi);
}
static double refInjectedPeriod(double r, double c, double vin, const Ref& ref, double vdd) {
	const double rt = r + ref.ron;
	const double tau = (rt < 1000.0 ? rt : 1000.0) * c;
	const double h = tau / 400.0;
	double v = 0.0, t = 0.0, flipAt = -1.0;
	bool high = true;
	std::vector<double> rises;
	auto f = [&](double x, double target) { return ((target - x) / rt + refInjection(vin, x)) / c; };
	for (long i = 0; i < 200000000 && rises.size() < 8 && t < 2000.0 * rt * c; i++) {
		double target = high ? vdd : 0.0;
		double k1 = f(v, target), k2 = f(v + 0.5 * h * k1, target), k3 = f(v + 0.5 * h * k2, target),
		       k4 = f(v + h * k3, target);
		double vn = v + h / 6.0 * (k1 + 2 * k2 + 2 * k3 + k4);
		double tn = t + h;
		if (flipAt < 0.0) {
			bool cr = high ? (vn >= ref.vp) : (vn <= ref.vn);
			if (cr) {
				// interpolate the crossing inside the step
				double goal = high ? ref.vp : ref.vn;
				double tc = t + h * (goal - v) / (vn - v);
				flipAt = tc + ref.tpd;
			}
		}
		if (flipAt >= 0.0 && tn >= flipAt) {
			flipAt = -1.0;
			high = !high;
			if (high) rises.push_back(tn);
		}
		v = vn; t = tn;
	}
	if (rises.size() < 8) return 0.0;
	return (rises[7] - rises[2]) / 5.0;
}

int main() {
	setvbuf(stdout, NULL, _IONBF, 0);
	printf("CD40106B Schmitt inverter and astable\n");

	// --- the datasheet's thresholds, at its own three supplies
	printf("  .. the datasheet's thresholds, at its own three supplies\n");
	for (int corner = -1; corner <= 1; corner++) {
		for (double vdd : { 5.0, 10.0, 15.0 }) {
			Ref r = datasheetAt(vdd, corner);
			Thresholds t = thresholds(vdd, corner);
			char w[96];
			snprintf(w, sizeof w, "VP at VDD %.0f V, corner %d", vdd, corner);
			checkv(w, t.vp, r.vp, 1e-12);
			snprintf(w, sizeof w, "VN at VDD %.0f V, corner %d", vdd, corner);
			checkv(w, t.vn, r.vn, 1e-12);
		}
	}
	// 12 V (the MiaW boards' VCC) lies on the 10..15 V segment
	{
		Thresholds t = thresholds(12.0);
		checkv("VP at 12 V (typical) = 5.9 + 0.4 * 2.9", t.vp, 5.9 + 0.4 * 2.9, 1e-12);
		checkv("VN at 12 V (typical) = 3.9 + 0.4 * 1.9", t.vn, 3.9 + 0.4 * 1.9, 1e-12);
	}
	// hysteresis VP - VN sits inside the datasheet's VH min..max band at each supply
	{
		const double vhMin[3] = { 0.3, 1.2, 1.6 }, vhMax[3] = { 1.6, 3.4, 5.0 };
		const double supply[3] = { 5.0, 10.0, 15.0 };
		for (int i = 0; i < 3; i++) {
			Thresholds t = thresholds(supply[i]);
			double vh = t.vp - t.vn;
			check("typical VP - VN lies in the datasheet's VH band", vh >= vhMin[i] && vh <= vhMax[i]);
		}
	}
	// min < typ < max for both thresholds everywhere
	for (double vdd = 4.0; vdd <= 16.0; vdd += 0.5) {
		Thresholds a = thresholds(vdd, CORNER_MIN), b = thresholds(vdd, CORNER_TYP), c = thresholds(vdd, CORNER_MAX);
		check("VP min < typ < max", a.vp < b.vp && b.vp < c.vp);
		check("VN min < typ < max", a.vn < b.vn && b.vn < c.vn);
		check("VN < VP always", c.vn < c.vp && a.vn < a.vp && b.vn < b.vp);
		check("VP below the rail, VN above ground", c.vp < vdd && a.vn > 0.0);
	}

	// --- the inverter's logic
	printf("  .. the inverter's logic\n");
	{
		Inverter inv;
		Thresholds t = thresholds(12.0);
		check("starts high", inv.out);
		check("a mid input holds the output high", inv.process(0.5 * (t.vp + t.vn), 12.0));
		check("below VP: still high", inv.process(t.vp - 0.01, 12.0));
		check("at VP: output goes low", !inv.process(t.vp, 12.0));
		check("falling to between: holds low", !inv.process(0.5 * (t.vp + t.vn), 12.0));
		check("just above VN: holds low", !inv.process(t.vn + 0.01, 12.0));
		check("at VN: output goes high", inv.process(t.vn, 12.0));
		// the reason AuditLogic's Rack 0.1/2 V window is not the chip's: at 12 V the chip needs 7.06 V
		Inverter inv2;
		inv2.process(2.5, 12.0);
		check("2.5 V does not trip a 12 V 40106 (Rack's 2 V Schmitt would)", inv2.out);
	}

	// --- the law: closed form, and an independent simulation
	printf("  .. the law: closed form, and an independent simulation\n");
	{
		// no on-resistance, no delay: the textbook formula, term by term
		for (double vdd : { 5.0, 9.0, 12.0, 15.0 }) {
			Ref ref = datasheetAt(vdd, 0);
			Timing tm = timing(100e3, 0.1e-6, vdd, CORNER_TYP, false, false);
			double tau = 100e3 * 0.1e-6;
			checkrel("tHigh = RC ln[(VDD-VN)/(VDD-VP)]", tm.tHigh, tau * std::log((vdd - ref.vn) / (vdd - ref.vp)), 1e-12);
			checkrel("tLow = RC ln[VP/VN]", tm.tLow, tau * std::log(ref.vp / ref.vn), 1e-12);
			checkrel("period = textbook", tm.period, textbookPeriod(100e3, 0.1e-6, ref.vp, ref.vn, vdd), 1e-12);
		}
		// with Ron and tpd: against brute force
		const double cases[][3] = { {100e3, 0.1e-6, 12.0}, {4.5e3, 68e-9, 12.0}, {1e3, 1e-9, 5.0},
		                            {1e3, 1e-9, 15.0}, {20e3, 10e-9, 9.0}, {47e3, 4.7e-9, 12.0} };
		for (auto& cs : cases) {
			for (int corner = -1; corner <= 1; corner++) {
				Ref ref = datasheetAt(cs[2], corner);
				double want = bruteForcePeriod(cs[0], cs[1], ref, cs[2], true, true);
				double got = timing(cs[0], cs[1], cs[2], corner).period;
				char w[128];
				snprintf(w, sizeof w, "timing() vs brute force: R %.0f C %.2g VDD %.0f corner %d", cs[0], cs[1], cs[2], corner);
				checkrel(w, got, want, 5e-4);
			}
		}
		// the 100k / 0.1 uF XORbell oscillator at 12 V typical: 8.1 ms-ish, ~123 Hz
		double f = 1.0 / timing(100e3, 0.1e-6, 12.0, CORNER_TYP, false, false).period;
		check("XORbell pot at full: about 123 Hz", f > 121.0 && f < 125.0);
		// round trip of the inverse
		for (double hz : { 20.0, 110.0, 440.0, 4000.0 }) {
			double r = resistanceForPeriod(1.0 / hz, 68e-9, 12.0);
			checkrel("resistanceForPeriod round trip", 1.0 / timing(r, 68e-9, 12.0).period, hz, 1e-9);
		}
	}

	// --- the stepped astable against the law, at several sample rates
	printf("  .. the stepped astable against the law, at several sample rates\n");
	{
		const double cases[][3] = { {100e3, 0.1e-6, 12.0}, {4.5e3, 68e-9, 12.0}, {30e3, 10e-9, 5.0},
		                            {30e3, 10e-9, 15.0}, {10e3, 100e-9, 9.0} };
		for (auto& cs : cases) {
			for (double fs : { 44100.0, 48000.0, 192000.0 }) {
				double want = timing(cs[0], cs[1], cs[2]).period;
				double got = steppedPeriod(cs[0], cs[1], cs[2], CORNER_TYP, fs);
				char w[128];
				snprintf(w, sizeof w, "stepped period vs law: R %.0f C %.2g VDD %.0f fs %.0f", cs[0], cs[1], cs[2], fs);
				checkrel(w, got, want, 2e-4);
			}
		}
		// corners move the period: min and max thresholds give a different law
		double tMin = timing(30e3, 10e-9, 12.0, CORNER_MIN).period;
		double tTyp = timing(30e3, 10e-9, 12.0, CORNER_TYP).period;
		double tMax = timing(30e3, 10e-9, 12.0, CORNER_MAX).period;
		check("corners give three different periods", std::fabs(tMin - tTyp) > 0.02 * tTyp && std::fabs(tMax - tTyp) > 0.02 * tTyp);
		checkrel("stepped MIN corner", steppedPeriod(30e3, 10e-9, 12.0, CORNER_MIN, 48000.0), tMin, 2e-4);
		checkrel("stepped MAX corner", steppedPeriod(30e3, 10e-9, 12.0, CORNER_MAX, 48000.0), tMax, 2e-4);
	}

	// --- supply dependence
	printf("  .. supply dependence\n");
	{
		// With thresholds that scale with VDD the ideal period barely moves; what moves is the duty
		// cycle, and (small R C) the on resistance and delay.
		double tp5 = timing(100e3, 0.1e-6, 5.0, CORNER_TYP, false, false).period;
		double tp10 = timing(100e3, 0.1e-6, 10.0, CORNER_TYP, false, false).period;
		double tp15 = timing(100e3, 0.1e-6, 15.0, CORNER_TYP, false, false).period;
		check("ideal period within 0.5 % across 5..15 V", std::fabs(tp5 - tp15) / tp10 < 0.005 && std::fabs(tp10 - tp15) / tp10 < 0.005);
		Timing a = timing(100e3, 0.1e-6, 5.0, CORNER_TYP, false, false);
		Timing b = timing(100e3, 0.1e-6, 15.0, CORNER_TYP, false, false);
		check("duty (tHigh / T) is 0.479 at 5 V", std::fabs(a.tHigh / a.period - 0.4790) < 0.001);
		check("duty rises with VDD (0.4865 at 15 V)", std::fabs(b.tHigh / b.period - 0.4865) < 0.001 && b.tHigh / b.period > a.tHigh / a.period);
		// at high rates the delay and on-resistance matter: 1 k + 1 nF is a ~1 us oscillator
		double s5 = steppedPeriod(1e3, 1e-9, 5.0, CORNER_TYP, 50e6, 0.0, 40);
		double s15 = steppedPeriod(1e3, 1e-9, 15.0, CORNER_TYP, 50e6, 0.0, 40);
		check("fast oscillator is slower at 5 V than 15 V (delay 140 ns vs 60 ns, Ron 400 vs 221)", s5 > s15 * 1.05);
		checkrel("... and each matches the closed form (5 V)", s5, timing(1e3, 1e-9, 5.0).period, 3e-3);
		checkrel("... and each matches the closed form (15 V)", s15, timing(1e3, 1e-9, 15.0).period, 3e-3);
		// a different supply is a different circuit even when the frequency is unmoved: the
		// capacitor swing (VP - VN) scales with it
		check("swing VP - VN at 5 V < at 15 V", thresholds(5.0).vp - thresholds(5.0).vn < thresholds(15.0).vp - thresholds(15.0).vn);
	}

	// --- the injection path: 1N4448 + 1k into the timing node
	printf("  .. the injection path: 1N4448 + 1k into the timing node\n");
	{
		const double R = 20e3, C = 10e-9, VDD = 12.0;
		Ref ref = datasheetAt(VDD, 0);
		double base = steppedPeriod(R, C, VDD, CORNER_TYP, 192000.0, 0.0, 40);
		// The diode only conducts once the jack is within about a diode drop of the node, which swings
		// between VN (4.66 V) and VP (7.06 V) at 12 V: below that the jack does nothing at all, above
		// it each extra tenth of a volt costs the low phase more.
		double prev = base;
		for (double vin : { 0.1, 3.0, 4.5, 5.0, 5.2, 5.4 }) {
			double got = steppedPeriod(R, C, VDD, CORNER_TYP, 192000.0, vin, 40);
			double want = refInjectedPeriod(R, C, vin, ref, VDD);
			char w[96];
			snprintf(w, sizeof w, "injected period vs independent RK4, In = %.1f V", vin);
			if (vin < 0.2) checkrel("In below 0.2 V is ignored", got, base, 1e-12);
			else checkrel(w, got, want, 3e-3);
			if (vin <= 4.5) checkrel("below the node's range the jack has no effect", got, base, 2e-3);
			else check("more injection, longer period", got > prev);
			prev = got;
		}
		check("5.4 V injection is 15 % or more slower than none", prev > 1.15 * base);
		// the same case at a coarser sample rate (events are placed inside the step)
		checkrel("injection at 44.1 kHz = at 192 kHz", steppedPeriod(R, C, VDD, CORNER_TYP, 44100.0, 4.5, 40),
		         steppedPeriod(R, C, VDD, CORNER_TYP, 192000.0, 4.5, 40), 2e-3);
		// a strong injection holds the node above VN: the oscillator stops (the In jacks gate it)
		Astable a;
		const double dt = 1.0 / 48000.0;
		int rises = 0;
		for (int i = 0; i < 48000; i++) { a.step(dt, R, C, VDD, CORNER_TYP, 10.0); for (int k = 0; k < a.nEdges; k++) rises += a.edges[k].dir > 0; }
		check("10 V into the In jack (above ~5.5 V here) stops the oscillator", rises <= 1);
		check("... with its output low (input held high)", !a.high);
		check("... and the read-out frequency falls toward zero", a.frequency() < 5.0);
		// the diode blocks a negative In: nothing happens
		checkrel("a negative In does nothing", steppedPeriod(R, C, VDD, CORNER_TYP, 48000.0, -5.0, 30), base, 1e-12);
		// with the supply at 5 V the protection diode clamps the node near the rail
		Astable b;
		double vmax = 0;
		for (int i = 0; i < 48000; i++) { b.step(dt, R, C, 5.0, CORNER_TYP, 10.0); if (b.v > vmax) vmax = b.v; }
		check("at 5 V, 10 V injection: node held within ~1 V of the rail by the input protection", vmax < 5.0 + 1.0);
	}

	// --- band-limited readout -----------------------------------------------------------------
	{
		Osc o;
		const double fs = 48000.0, dt = 1.0 / fs, R = 8e3, C = 68e-9, VDD = 12.0;
		Timing tm = timing(R, C, VDD);
		double mean = 0, mx = 0, mn = 0;
		long n = 0;
		int partial = 0;
		for (int i = 0; i < 480000; i++) {
			float y = o.process(dt, R, C, VDD);
			if (i > 100) { mean += y; n++; if (y > mx) mx = y; if (y < mn) mn = y; if (std::fabs(y) < 0.98f) partial++; }
		}
		mean /= (double)n;
		checkv("mean = 2 tHigh/T - 1", mean, 2.0 * tm.tHigh / tm.period - 1.0, 2e-3);
		check("mean is negative (duty < 50 %)", mean < -0.01);
		check("peaks stay near +-1", mx < 1.2f && mx > 0.99f && mn > -1.2f && mn < -0.99f);
		check("edges are smoothed (some samples are neither +1 nor -1)", partial > 1000);
	}

	// --- negative controls
	printf("  .. negative controls\n");
	{
		// The same comparisons, against references with one thing wrong each.
		const double R = 30e3, C = 10e-9, VDD = 12.0;
		double got = steppedPeriod(R, C, VDD, CORNER_TYP, 48000.0);
		Ref ref = datasheetAt(VDD, 0);
		double good = bruteForcePeriod(R, C, ref, VDD, true, true);
		check("(control baseline) model agrees with the right reference", std::fabs(got - good) < 5e-4 * good);

		control("the MIN-corner law is rejected by the typical-corner comparison", std::fabs(timing(R, C, VDD, CORNER_MIN).period - got) > 5e-4 * got);

		// Rack's own Schmitt window (2 V / 0.1 V) as the thresholds: a very different period
		double rackLaw = textbookPeriod(R, C, 2.0, 0.1, VDD);
		control("a fixed 2 V / 0.1 V window instead of the datasheet's is rejected", std::fabs(rackLaw - got) > 0.05 * got);

		// thresholds that do not scale with the supply: the period would depend strongly on VDD
		double fixed5 = textbookPeriod(R, C, 5.9, 3.9, 7.0);
		double fixed15 = textbookPeriod(R, C, 5.9, 3.9, 15.0);
		control("supply-independent thresholds would make the period track VDD, which the model does not",
		        std::fabs(fixed5 - fixed15) > 0.05 * fixed15);
		check("(the model's own period at 5 V vs 15 V differs < 1 %)",
		      std::fabs(timing(R, C, 5.0, CORNER_TYP, false, false).period - timing(R, C, 15.0, CORNER_TYP, false, false).period)
		      < 0.01 * timing(R, C, 10.0, CORNER_TYP, false, false).period);

		// leaving out the on-resistance or delay is visible at a fast setting
		double fast = steppedPeriod(1e3, 1e-9, 5.0, CORNER_TYP, 50e6, 0.0, 40);
		control("the law without Ron and delay is rejected at 1 k / 1 nF / 5 V",
		        std::fabs(timing(1e3, 1e-9, 5.0, CORNER_TYP, false, false).period - fast) > 0.05 * fast);
		control("the law without the delay alone is rejected at 1 k / 1 nF / 5 V",
		        std::fabs(timing(1e3, 1e-9, 5.0, CORNER_TYP, true, false).period - fast) > 0.02 * fast);

		// a mutation of the model itself: the stepped astable with a wrong target rail
		{
			Astable a;
			const double dt = 1.0 / 48000.0;
			int rises = 0; double t0 = 0, first = 0, last = 0, tnow = 0;
			for (int i = 0; i < 480000; i++) {
				a.step(dt, R, C, VDD, CORNER_TYP, 0.0);
				for (int k = 0; k < a.nEdges; k++) if (a.edges[k].dir > 0) { rises++; double at = tnow + a.edges[k].at; if (rises == 3) first = at; last = at; }
				tnow += dt;
			}
			(void)t0;
			double per = (last - first) / (double)(rises - 3);
			// feeding the model a supply that differs from the one the reference assumes (5 V vs 12 V
			// at a fast setting) must change the answer beyond tolerance
			double other = timing(R, C, 12.0, CORNER_TYP, true, true).period;
			check("(control baseline) mutation harness reproduces the law", std::fabs(per - other) < 2e-4 * other);
			control("a model run at the wrong VDD is rejected by the same comparison (30 k, 10 nF, 5 V vs 12 V)",
			        std::fabs(timing(R, C, 5.0).period - other) > 1e-4 * other);
		}
		// the injection reference must reject an injection-free model
		control("the injection comparison rejects 'no injection' at In = 5.2 V",
		        std::fabs(steppedPeriod(20e3, 10e-9, 12.0, CORNER_TYP, 192000.0, 0.0, 40)
		                  - refInjectedPeriod(20e3, 10e-9, 5.2, datasheetAt(12.0, 0), 12.0)) > 3e-3 * refInjectedPeriod(20e3, 10e-9, 5.2, datasheetAt(12.0, 0), 12.0));
	}

	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}

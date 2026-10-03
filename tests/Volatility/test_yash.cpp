// Volatility's SAMPLE & HOLD -- Rene Schmitz's YASH around an LF398 -- tested without Rack.
//
// The oracles are the datasheets' and the schematic's own numbers, not the model's:
//   * the LF398: 4 us to 0.1% on 1000 pF and 20 us on 0.01 uF for a 10 V step, 30 pA of leakage
//     doubling every 11 C, a hold step inversely proportional to Ch, feedthrough in dB, and the
//     aperture time, each checked against a hand calculation;
//   * the CD4093B: the thresholds it prints, the textbook astable period, and a brute-force
//     time-stepped RC with a Schmitt gate (a different integrator from the model's closed form);
//   * the pulse: a brute-force simulation of 470 pF into 10k with gate 2's thresholds, and
//     Schmitz's own "3 us pulses";
//   * sub-sample timing: the same physical scenario cut into audio steps of different length
//     must give the same held voltage.
// Each check is built so the thing it guards fails it; see the bottom of the file for the
// controls that are run inside this binary, and tests/Volatility/Makefile's note for the ones
// run by mutating the headers.

#include "../../src/Lf398.hpp"
#include "../../src/Cd4093.hpp"
#include "../../src/Volatility/Yash.hpp"

#include <cmath>
#include <cstdio>
#include <functional>

static int checks = 0;
static int failures = 0;

static void check(const char* what, bool ok, const char* detail = 0) {
	checks++;
	if (!ok) {
		failures++;
		printf("  FAIL  %s%s%s\n", what, detail ? ": " : "", detail ? detail : "");
	}
}
static void checkNear(const char* what, double got, double want, double tol) {
	checks++;
	if (!(std::fabs(got - want) <= tol)) {
		failures++;
		printf("  FAIL  %s: got %.9g, want %.9g +- %.3g\n", what, got, want, tol);
	}
}
static void checkRel(const char* what, double got, double want, double rel) {
	checkNear(what, got, want, std::fabs(want) * rel);
}

typedef std::function<double(double)> Fn;

// ---------------------------------------------------------------------------------------------
// The LF398
// ---------------------------------------------------------------------------------------------

/** A spec with every imperfection off, so one can be switched on at a time. */
static lf398::Spec clean(double ch) {
	lf398::Spec s;
	s.ch = ch; s.offset = 0.0; s.gainError = 0.0; s.holdStep10n = 0.0;
	s.leakage25 = 0.0; s.feedDb10n = 400.0; s.tjC = 25.0;
	return s;
}

/** Runs the channel with the logic high over [t0, t1] (absolute seconds) and the input `vin(t)`,
    in audio steps of `dt`, until `tEnd`. Returns the output at the end of every step in `outs`
    when given. */
static double runWindow(lf398::Channel& ch, const lf398::Spec& sp, const Fn& vin, double dt,
                        double t0, double t1, double tEnd) {
	double out = 0.0;
	long steps = (long) std::ceil(tEnd / dt - 1e-9);
	for (long i = 0; i < steps; i++) {
		double a = i * dt, b = a + dt;
		lf398::Window w;
		int n = 0;
		if (t1 > a && t0 < b) {
			w.a = (t0 > a ? t0 : a) - a;
			w.b = (t1 < b ? t1 : b) - a;
			n = 1;
		}
		out = ch.step(sp, dt, vin(a), vin(b), &w, n);
	}
	return out;
}

/** The time for the output to come within `tol` of a `step` volt step, found by running the
    channel with the logic high and the audio step as short as the sub-step. */
static double acquisitionTime(double ch, double stepV, double tol) {
	lf398::Spec sp = clean(ch);
	lf398::Channel c;
	c.step(sp, 1e-9, 0.0, 0.0, 0, 0);   // prime at 0 V
	const double dt = 20e-9;
	lf398::Window w; w.a = 0.0; w.b = dt;
	double t = 0.0;
	for (int i = 0; i < 20000; i++) {
		double out = c.step(sp, dt, stepV, stepV, &w, 1);
		t += dt;
		if (std::fabs(out - stepV) <= tol * std::fabs(stepV)) return t;
	}
	return -1.0;
}

static void testAcquisition() {
	// datasheet: 4 us to 0.1% for 10 V on 1000 pF; 20 us on 0.01 uF
	double t1n = acquisitionTime(1e-9, 10.0, 1e-3);
	checkRel("acquisition to 0.1%, 10 V, 1000 pF (datasheet 4 us)", t1n, 4e-6, 0.15);
	double t10n = acquisitionTime(10e-9, 10.0, 1e-3);
	checkRel("acquisition to 0.1%, 10 V, 0.01 uF (datasheet 20 us)", t10n, 20e-6, 0.10);
	// Schmitz: "3 us gives about 1%" for his 1 nF
	{
		lf398::Spec sp = clean(1e-9);
		lf398::Channel c;
		c.step(sp, 1e-9, 0.0, 0.0, 0, 0);
		lf398::Window w; w.a = 0.0; w.b = 3e-6;
		double out = c.step(sp, 3e-6, 10.0, 10.0, &w, 1);
		double err = (10.0 - out) / 10.0 * 100.0;
		check("error after 3 us on 1 nF is about 1% (0.3% .. 1.5%)", err > 0.3 && err < 1.5);
	}
	// charging current: 5 mA slews 5 V/us on 1 nF. With 10 V to go the output cannot have moved
	// faster than that, whatever the front end does.
	{
		lf398::Spec sp = clean(1e-9);
		lf398::Channel c;
		c.step(sp, 1e-9, 0.0, 0.0, 0, 0);
		lf398::Window w; w.a = 0.0; w.b = 1e-6;
		double out = c.step(sp, 1e-6, 10.0, 10.0, &w, 1);
		check("1 us after a 10 V step the output has moved no more than 5 V/us", out <= 5.0 + 1e-6);
		check("... and has moved", out > 3.0);
	}
	// a tiny step settles linearly -- a first-order lag, with no current limit in sight
	{
		lf398::Spec sp = clean(1e-9);
		lf398::Channel c;
		c.step(sp, 1e-9, 0.0, 0.0, 0, 0);
		lf398::Window w; w.a = 0.0; w.b = 2e-6;
		double out = c.step(sp, 2e-6, 0.01, 0.01, &w, 1);
		// front-end tau 0.6 us in series with Ch / gm = 10 ns: 1 - e^(-2/0.61) = 0.962
		checkRel("10 mV step, small-signal 2 us", out / 0.01, 1.0 - std::exp(-2.0 / 0.61), 0.02);
	}
}

static void testHold() {
	// --- droop: 30 pA typ / 200 pA max, doubling per 11 C, I / Ch, linear ---
	{
		lf398::Spec sp = lf398::spec(false, 1e-9, true, 1.0, 25.0, false);
		checkNear("leakage 30 pA at 25 C", lf398::leakageAmps(sp), 30e-12, 1e-15);
		checkNear("droop rate 1 nF: 30 pA / 1 nF = 0.03 V/s", lf398::droopRate(sp), 0.03, 1e-6);
		check("droop rate agrees with Figure 4's reading (0.035 V/s at 1000 pF, 25 C) within a factor 1.5",
		      lf398::droopRate(sp) > 0.035 / 1.5 && lf398::droopRate(sp) < 0.035 * 1.5);
		lf398::Spec s0 = sp; s0.offset = 0.0; s0.gainError = 0.0; s0.holdStep10n = 0.0; s0.feedDb10n = 400.0;
		lf398::Channel c;
		c.step(s0, 1e-3, 2.0, 2.0, 0, 0);
		lf398::Window w; w.a = 0.0; w.b = 1e-3;
		c.v = 2.0;
		double out = 0.0;
		for (int i = 0; i < 10000; i++) out = c.step(s0, 1e-3, 2.0, 2.0, 0, 0);   // 10 s of hold
		checkNear("held 10 s: 0.3 V up", out - 2.0, 0.3, 1e-3);
		// linear: the first 5 s are half
		c.v = 2.0;
		for (int i = 0; i < 5000; i++) out = c.step(s0, 1e-3, 2.0, 2.0, 0, 0);
		checkNear("droop is linear in time (5 s -> 0.15 V)", out - 2.0, 0.15, 1e-3);
		// sign
		lf398::Spec dn = lf398::spec(false, 1e-9, true, -1.0, 25.0, false);
		dn.offset = 0.0; dn.gainError = 0.0; dn.holdStep10n = 0.0; dn.feedDb10n = 400.0;
		lf398::Channel c2; c2.step(dn, 1e-3, 2.0, 2.0, 0, 0); c2.v = 2.0;
		for (int i = 0; i < 1000; i++) out = c2.step(dn, 1e-3, 2.0, 2.0, 0, 0);
		check("negative leakage droops down", out < 2.0 - 0.02);
		// worst case 200 pA
		lf398::Spec wc = lf398::spec(true, 1e-9, true, 1.0, 25.0, false);
		checkNear("worst-case droop 0.2 V/s on 1 nF", lf398::droopRate(wc), 0.2, 1e-6);
		// 1/Ch
		lf398::Spec big = lf398::spec(false, 10e-9, true, 1.0, 25.0, false);
		checkNear("droop on 10 nF is a tenth", lf398::droopRate(big), 0.003, 1e-7);
		// off
		lf398::Spec off = lf398::spec(false, 1e-9, false, 1.0, 25.0, false);
		checkNear("leakage off: no droop", lf398::droopRate(off), 0.0, 1e-15);
		// doubles per 11 C (footnote 3)
		lf398::Spec hot = lf398::spec(false, 1e-9, true, 1.0, 36.0, false);
		checkRel("leakage doubles at +11 C", lf398::leakageAmps(hot), 60e-12, 1e-9);
		lf398::Spec hot2 = lf398::spec(false, 1e-9, true, 1.0, 58.0, false);
		checkRel("... and quadruples at +33 C is 8x", lf398::leakageAmps(hot2), 240e-12, 1e-9);
		// self-heating: 4.5 mA * 30 V * 48.9 C/W = 6.6 C
		lf398::Spec sh = lf398::spec(false, 1e-9, true, 1.0, 25.0, true);
		checkNear("self-heating typ", sh.tjC - 25.0, 0.0045 * 30.0 * 48.9, 1e-9);
		checkRel("leakage with self-heating", lf398::leakageAmps(sh), 30e-12 * std::pow(2.0, 6.6015 / 11.0), 1e-3);
	}
	// --- hold step: -1 mV typ at 0.01 uF, inversely proportional to Ch ---
	{
		lf398::Spec sp = clean(10e-9);
		sp.holdStep10n = 1e-3;
		lf398::Channel c; c.step(sp, 1e-9, 0.0, 0.0, 0, 0);
		double out = runWindow(c, sp, [](double) { return 0.0; }, 1e-6, 2.2e-6, 40.3e-6, 100e-6);
		checkNear("hold step at 0.01 uF, 0 V: -1 mV", out, -1e-3, 2e-6);
		lf398::Spec s1 = clean(1e-9); s1.holdStep10n = 1e-3;
		lf398::Channel c1; c1.step(s1, 1e-9, 0.0, 0.0, 0, 0);
		out = runWindow(c1, s1, [](double) { return 0.0; }, 1e-6, 2.2e-6, 40.3e-6, 100e-6);
		checkNear("hold step at 1 nF is ten times that", out, -10e-3, 2e-5);
		// worst case table value 2.5 mV
		lf398::Spec sw = lf398::spec(true, 10e-9, false, 1.0, 25.0, false);
		checkNear("worst-case hold step 2.5 mV at 0.01 uF", std::fabs(lf398::holdStep(sw, 0.0)), 2.5e-3, 1e-9);
		// Figure 15: smaller at +10 V than at 0 V, larger at -10 V
		check("hold step falls with output voltage (Figure 15)",
		      std::fabs(lf398::holdStep(sp, 10.0)) < std::fabs(lf398::holdStep(sp, 0.0)) &&
		      std::fabs(lf398::holdStep(sp, -10.0)) > std::fabs(lf398::holdStep(sp, 0.0)));
		// it happens once, when the hold begins, not every sample
		double o1 = 0, o2 = 0;
		c.step(sp, 1e-3, 0.0, 0.0, 0, 0);
		o1 = c.v;
		c.step(sp, 1e-3, 0.0, 0.0, 0, 0);
		o2 = c.v;
		checkNear("no further step while holding", o2, o1, 1e-12);
	}
	// --- offset and gain error appear in what is held ---
	{
		lf398::Spec sp = clean(1e-9);
		sp.offset = 2e-3; sp.gainError = 4e-5;
		lf398::Channel c; c.step(sp, 1e-9, 3.0, 3.0, 0, 0);
		double out = runWindow(c, sp, [](double) { return 3.0; }, 1e-6, 1.2e-6, 60.3e-6, 100e-6);
		checkNear("held = 3 V * (1 - 4e-5) + 2 mV", out, 3.0 * (1 - 4e-5) + 2e-3, 2e-6);
	}
	// --- feedthrough in hold: -80 dB on 1000 pF, -90 dB on 0.01 uF, -70 / -80 worst case ---
	{
		const double fs = 48000.0, dt = 1.0 / fs;
		struct Case { double ch; bool worst; double db; } cases[4] = {
			{ 1e-9, false, 80.0 }, { 10e-9, false, 90.0 }, { 1e-9, true, 70.0 }, { 100e-9, false, 100.0 } };
		for (int k = 0; k < 4; k++) {
			lf398::Spec sp = lf398::spec(cases[k].worst, cases[k].ch, false, 1.0, 25.0, false);
			sp.offset = 0.0; sp.gainError = 0.0; sp.holdStep10n = 0.0;
			lf398::Channel c; c.step(sp, dt, 0.0, 0.0, 0, 0);
			double lo = 1e9, hi = -1e9;
			for (int i = 0; i < 480; i++) {
				double a = 5.0 * std::sin(2 * M_PI * 1000.0 * i * dt);
				double b = 5.0 * std::sin(2 * M_PI * 1000.0 * (i + 1) * dt);
				double out = c.step(sp, dt, a, b, 0, 0);
				if (out < lo) lo = out;
				if (out > hi) hi = out;
			}
			double pp = hi - lo;
			double wantPP = 10.0 * std::pow(10.0, -cases[k].db / 20.0);
			char what[80];
			snprintf(what, sizeof what, "feedthrough p-p, %.0f dB case %d", cases[k].db, k);
			checkRel(what, pp, wantPP, 0.02);
		}
	}
}

static void testAperture() {
	// aperture: 148 ns (positive step), 205 ns (negative) at 25 C; the held value is the input a
	// moment after the logic drops, less the front-end's lag behind a ramp.
	const double lagTotal = lf398::kTauFront + 1e-9 / lf398::kGm;   // 0.6 us + Ch / gm
	for (int dir = -1; dir <= 1; dir += 2) {
		lf398::Spec sp = clean(1e-9);
		const double S = dir * 1e5;   // V/s
		Fn vin = [S](double t) { return S * t; };
		const double tb = 30.35e-6;
		lf398::Channel c; c.step(sp, 1e-9, 0.0, 0.0, 0, 0);
		double out = runWindow(c, sp, vin, 1e-6, 10.1e-6, tb, 60e-6);
		double ap = lf398::aperture(sp, dir > 0);
		checkNear(dir > 0 ? "aperture, rising ramp" : "aperture, falling ramp", ap, dir > 0 ? 148e-9 : 205e-9, 1e-12);
		double want = S * (tb + ap - lagTotal);
		checkNear(dir > 0 ? "held value of a rising ramp" : "held value of a falling ramp", out, want, 0.002 * 0.05 + 2e-4);
		// without the aperture the held value would differ by S * ap = 15-20 mV x 1e-3
		double without = S * (tb - lagTotal);
		check("the aperture is resolvable by this check", std::fabs(want - without) > 0.012);
	}
	// temperature
	lf398::Spec hot = clean(1e-9); hot.tjC = 150.0;
	checkNear("aperture at 150 C, positive: 252 ns", lf398::aperture(hot, true), 252e-9, 1e-12);
	checkNear("aperture at 150 C, negative: 352 ns", lf398::aperture(hot, false), 352e-9, 1e-12);
}

static void testStepInvariance() {
	// One physical scenario, chopped into audio steps of different lengths: the logic window and
	// its aperture straddle step boundaries in some and not in others; the held voltage must not
	// notice. The input is a ramp, which linear interpolation represents exactly.
	lf398::Spec sp = lf398::spec(false, 1e-9, true, 1.0, 25.0, true);
	Fn vin = [](double t) { return 4.0 + 8e4 * t; };   // 80 V/ms
	const double t0 = 51.3e-6, t1 = 56.1e-6;
	double ref = 0.0;
	double dts[5] = { 0.25e-6, 1e-6, 4.7e-6, 1.0 / 48000.0, 1.0 / 44100.0 };
	for (int k = 0; k < 5; k++) {
		lf398::Channel c; c.step(sp, 1e-9, vin(0), vin(0), 0, 0);
		// run to exactly 400 us regardless of dt (last partial step trimmed by the loop below)
		double out = 0.0;
		double t = 0.0;
		while (t < 400e-6 - 1e-12) {
			double dt = dts[k];
			if (t + dt > 400e-6) dt = 400e-6 - t;
			lf398::Window w; int n = 0;
			if (t1 > t && t0 < t + dt) {
				w.a = (t0 > t ? t0 : t) - t; w.b = (t1 < t + dt ? t1 : t + dt) - t; n = 1;
			}
			out = c.step(sp, dt, vin(t), vin(t + dt), &w, n);
			t += dt;
		}
		// the output includes feedthrough of the input at the end: compare the cap alone
		out = c.v;
		if (k == 0) ref = out;
		char what[80];
		snprintf(what, sizeof what, "held value independent of audio step (dt = %.2f us)", dts[k] * 1e6);
		checkNear(what, out, ref, 5e-6);
	}
	// and the held value is the ramp a few microseconds after the window opened -- a lagged
	// input, not the input at the trigger instant and not the input at the next audio sample
	double heldLag = ref - vin(t1);
	check("held value is the input shortly after the window closed, not before", heldLag > -0.3 && heldLag < 0.2);
}

// ---------------------------------------------------------------------------------------------
// The CD4093B and the circuit around it
// ---------------------------------------------------------------------------------------------

static void testCd4093() {
	using namespace cd4093;
	// the printed values, tied inputs (b)
	Thresholds t5 = thresholds(5.0, CORNER_TYP, TIED);
	checkNear("VP typ 5 V tied", t5.vp, 3.3, 1e-9);
	checkNear("VN typ 5 V tied", t5.vn, 2.3, 1e-9);
	Thresholds t15 = thresholds(15.0, CORNER_TYP, TIED);
	checkNear("VP typ 15 V tied", t15.vp, 9.4, 1e-9);
	checkNear("VN typ 15 V tied", t15.vn, 7.3, 1e-9);
	Thresholds a15 = thresholds(15.0, CORNER_TYP, ONE_INPUT);
	checkNear("VP typ 15 V one input", a15.vp, 8.8, 1e-9);
	checkNear("VN typ 15 V one input", a15.vn, 5.8, 1e-9);
	check("the two wirings differ (tied has the higher VN)", t15.vn > a15.vn + 1.0);
	checkNear("VP max 15 V tied", thresholds(15.0, CORNER_MAX, TIED).vp, 12.7, 1e-9);
	checkNear("VN min 15 V tied", thresholds(15.0, CORNER_MIN, TIED).vn, 4.8, 1e-9);
	// interpolation between the printed supplies
	checkNear("VP typ at 12.5 V is the midpoint of 7.0 and 9.4", thresholds(12.5, CORNER_TYP, TIED).vp, 8.2, 1e-9);
	for (int c = -1; c <= 1; c++)
		for (int w = 0; w < 2; w++)
			for (double v = 5.0; v <= 15.0; v += 2.5) {
				Thresholds t = thresholds(v, c, w);
				check("VN < VP everywhere", t.vn < t.vp);
			}
	checkNear("tpd typ 15 V", tpd(15.0), 65e-9, 1e-15);
	checkNear("Ron at 15 V = 1.5 V / 6.8 mA", ron(15.0), 1.5 / 6.8e-3, 1e-9);
}

/** A time-stepped RC astable with a Schmitt gate: Euler with a tiny step, delay as a queue. A
    different integrator from the model's closed form. Returns the mean period over `cycles`. */
static double bruteAstable(double r, double c, double vdd, int corner, bool delay, bool ron, int cycles) {
	using namespace cd4093;
	Thresholds th = thresholds(vdd, corner, TIED);
	double rt = r + (ron ? cd4093::ron(vdd) : 0.0);
	double d = delay ? tpd(vdd) : 0.0;
	double dt = rt * c / 20000.0;
	double v = th.vn, t = 0.0;
	bool out = true;           // the gate's output, after its delay
	bool want = true;          // what the input says it should be
	double changeAt = -1.0;
	double lastRise = -1.0, firstRise = -1.0;
	int rises = 0;
	for (long i = 0; i < 400000000L && rises <= cycles; i++) {
		double target = out ? vdd : 0.0;
		v += (target - v) * dt / (rt * c);
		t += dt;
		bool w = want;
		if (want && v >= th.vp) w = false;
		else if (!want && v <= th.vn) w = true;
		if (w != want) { want = w; changeAt = t + d; }
		if (changeAt >= 0.0 && t >= changeAt) {
			out = want; changeAt = -1.0;
			if (out) {
				if (firstRise < 0.0) firstRise = t;
				lastRise = t; rises++;
			}
		}
	}
	return (lastRise - firstRise) / (rises - 1);
}

static void testAstable() {
	using namespace cd4093;
	// textbook: T = RC [ ln((VDD-VN)/(VDD-VP)) + ln(VP/VN) ] with ideal gate
	{
		Thresholds th = thresholds(15.0, CORNER_TYP, TIED);
		double rc = 100e3 * 470e-9;
		double want = rc * (std::log((15.0 - th.vn) / (15.0 - th.vp)) + std::log(th.vp / th.vn));
		Timing t = timing(100e3, 470e-9, 15.0, CORNER_TYP, false, false);
		checkRel("closed-form period, ideal gate, vs textbook", t.period, want, 1e-9);
		checkRel("0.5715 RC with the typical tied-input thresholds", t.period, 0.5715 * rc, 2e-3);
	}
	// the model's step() against the brute-force integrator, with Ron and delay on
	double rs[3] = { 2.2e3 + 1.0, 100e3, 502.2e3 };
	for (int k = 0; k < 3; k++) {
		double r = rs[k];
		// measure the model's period over many steps of 20.83 us
		Astable a;
		const double dt = 1.0 / 48000.0;
		double tAbs = 0.0, firstRise = -1.0, lastRise = -1.0;
		int rises = 0;
		double stop = 40.0 * timing(r, 470e-9, 15.0).period;
		if (stop < 0.05) stop = 0.05;
		while (tAbs < stop) {
			a.step(dt, r, 470e-9, 15.0);
			for (int e = 0; e < a.nEdges; e++)
				if (a.edges[e].dir > 0) {
					double at = tAbs + a.edges[e].at;
					if (firstRise < 0.0) firstRise = at;
					lastRise = at; rises++;
				}
			tAbs += dt;
		}
		double per = (lastRise - firstRise) / (rises - 1);
		double closed = timing(r, 470e-9, 15.0).period;
		char what[96];
		snprintf(what, sizeof what, "astable step() period vs closed form, R = %.1fk", r / 1e3);
		checkRel(what, per, closed, 1e-6);   // sub-sample edges: no quantisation to 20.8 us
		if (k >= 1) {
			double bf = bruteAstable(r, 470e-9, 15.0, CORNER_TYP, true, true, 6);
			snprintf(what, sizeof what, "astable period vs brute-force RC, R = %.1fk", r / 1e3);
			checkRel(what, closed, bf, 2e-3);
		}
	}
	// pot at 1 Meg: 3.7 Hz; at 0: 1.5 kHz
	checkRel("lowest frequency (1 Meg + 2k2)", 1.0 / timing(1002.2e3, 470e-9, 15.0).period, 3.7, 0.03);
	{
		double f = 1.0 / timing(2.2e3, 470e-9, 15.0).period;
		check("highest frequency (2k2 alone) about 1.5 kHz", f > 1300.0 && f < 1800.0);
	}
	// sub-sample positions: with a period that is not a multiple of the step, the edges must still
	// be spaced by the period to far better than a step
	{
		Astable a; const double dt = 1.0 / 48000.0;
		double tAbs = 0.0, prev = -1.0, worst = 0.0;
		double per = timing(20e3, 470e-9, 15.0).period;
		int n = 0;
		while (tAbs < 0.5) {
			a.step(dt, 20e3, 470e-9, 15.0);
			for (int e = 0; e < a.nEdges; e++)
				if (a.edges[e].dir > 0) {
					double at = tAbs + a.edges[e].at;
					if (prev >= 0.0) { double err = std::fabs((at - prev) - per); if (err > worst) worst = err; n++; }
					prev = at;
				}
			tAbs += dt;
		}
		check("enough cycles seen", n > 20);
		check("edge spacing error is far below one sample", worst < 1e-8);
	}
}

/** Brute force of the differentiator: 470 pF from gate 1's output (through its Ron) into 10k,
    gate 2 with the tied-input thresholds. Returns how long gate 2's input stays above VN after
    it crosses VP, with no propagation delay. Euler, 0.05 ns. */
static double brutePulse(double vdd, int corner) {
	using namespace cd4093;
	Thresholds th = thresholds(vdd, corner, TIED);
	double rOn = ron(vdd);
	double vc = 0.0, t = 0.0, dt = 0.05e-9;
	double above0 = -1.0;
	for (long i = 0; i < 400000; i++) {
		double i_c = (vdd - vc) / (rOn + 10e3);
		double v2 = i_c * 10e3;
		if (above0 < 0.0 && v2 >= th.vp) above0 = t;
		if (above0 >= 0.0 && v2 <= th.vn) return t - above0;
		vc += i_c * dt / 470e-12;
		t += dt;
	}
	return -1.0;
}

static void testPulse() {
	using namespace yash;
	for (int c = -1; c <= 1; c++) {
		double bf = brutePulse(15.0, c);
		double cf = pulseWidth(c);
		char what[80];
		snprintf(what, sizeof what, "pulse width closed form vs brute force, corner %d", c);
		checkRel(what, cf, bf, 2e-3);
	}
	double w = pulseWidth(cd4093::CORNER_TYP);
	check("typical pulse is Schmitz's 3 us (2.5 .. 4.5)", w > 2.5e-6 && w < 4.5e-6);
	check("min/typ/max VN give wide/typical/narrow", pulseWidth(-1) > pulseWidth(0) && pulseWidth(0) > pulseWidth(1));
	// divider: pin 8 high level is above the LF398's 2.4 V maximum threshold and below 5 V logic
	double p8 = pin8High();
	check("pin 8 high is above the LF398's logic threshold", p8 > 2.4 && p8 < 4.0);
	// BC548 stage
	TrigThresholds th = trigThresholds();
	check("trigger switches around 0.6 - 0.8 V", th.on > 0.6 && th.on < 0.8);
	check("trigger has a little hysteresis (on above off)", th.on > th.off);

	// the chain itself: one rising edge, measure the pin 8 window across audio-step boundaries
	for (int k = 0; k < 3; k++) {
		double dts[3] = { 1.0 / 48000.0, 0.5e-6, 7.3e-6 };
		double dt = dts[k];
		PulseChain ch;
		const double tTrig = 3.1e-6;
		double a = -1.0, b = -1.0;
		double t = 0.0;
		for (int i = 0; i < 4000 && b < 0.0; i++) {
			if (tTrig >= t && tTrig < t + dt) ch.pushTrigger(tTrig - t, +1);
			Pulse out[4];
			int n = ch.advance(dt, out, 4);
			for (int j = 0; j < n; j++) {
				if (out[j].a >= 0.0 && a < 0.0) a = t + out[j].a;
				if (out[j].b < dt) b = t + out[j].b;
			}
			t += dt;
		}
		double d3 = 3.0 * cd4093::tpd(15.0);
		char what[96];
		snprintf(what, sizeof what, "pulse starts three gate delays after the trigger (dt = %.2f us)", dt * 1e6);
		checkNear(what, a, tTrig + d3, 1e-12);
		snprintf(what, sizeof what, "pulse width through the chain (dt = %.2f us)", dt * 1e6);
		checkNear(what, b - a, pulseWidth(), 1e-12);
	}
	// a trigger held high gives one pulse, and its falling edge gives none
	{
		PulseChain ch; const double dt = 1.0 / 48000.0;
		int pulses = 0;
		for (int i = 0; i < 2000; i++) {
			if (i == 10) ch.pushTrigger(5e-6, +1);
			if (i == 1500) ch.pushTrigger(5e-6, -1);
			Pulse out[4];
			int n = ch.advance(dt, out, 4);
			for (int j = 0; j < n; j++) if (out[j].b < dt || out[j].a > 0.0) pulses++;
		}
		check("one pulse for a held trigger, none for its release", pulses == 1);
	}
	// a falling trigger edge inside the pulse cuts it short (gate 1's output swings back and
	// drags the node below VN)
	{
		PulseChain ch; const double dt = 0.2e-6;
		double a = -1, b = -1, t = 0;
		for (int i = 0; i < 200 && b < 0; i++) {
			if (i == 0) ch.pushTrigger(0.0, +1);
			if (i == 5) ch.pushTrigger(0.0, -1);        // 1.0 us later
			Pulse out[4];
			int n = ch.advance(dt, out, 4);
			for (int j = 0; j < n; j++) { if (a < 0) a = t + out[j].a; if (out[j].b < dt) b = t + out[j].b; }
			t += dt;
		}
		check("a release inside the pulse ends it early", b > 0 && (b - a) < 1.3e-6 && (b - a) > 0.8e-6);
	}
	// a second rising edge while the first pulse is still high extends it: edges 1.0 us apart
	// with nothing in between would need a release, so use the oscillator-style pair rise,
	// rise (the second ignored by gate 1, which is already high) -- no change in the window
	{
		PulseChain ch; const double dt = 0.2e-6;
		double a = -1, b = -1, t = 0;
		for (int i = 0; i < 200 && b < 0; i++) {
			if (i == 0) ch.pushTrigger(0.0, +1);
			if (i == 5) ch.pushTrigger(0.0, +1);
			Pulse out[4];
			int n = ch.advance(dt, out, 4);
			for (int j = 0; j < n; j++) { if (a < 0) a = t + out[j].a; if (out[j].b < dt) b = t + out[j].b; }
			t += dt;
		}
		checkNear("a repeated rise while high changes nothing", b - a, pulseWidth(), 1e-12);
	}
	// a trigger arriving in the recovery tail of the previous pulse still fires a full pulse
	{
		PulseChain ch; const double dt = 0.1e-6;
		int starts = 0; bool was = false;
		for (int i = 0; i < 1000; i++) {
			if (i == 0) ch.pushTrigger(0.0, +1);
			if (i == 100) ch.pushTrigger(0.0, -1);      // 10 us: node has nearly recovered
			if (i == 150) ch.pushTrigger(0.0, +1);      // 15 us
			Pulse out[4];
			int n = ch.advance(dt, out, 4);
			bool now = n > 0;
			if (now && !was) starts++;
			was = n > 0 && out[n - 1].b >= dt;
		}
		check("two separated triggers give two pulses", starts == 2);
	}
}

// ---------------------------------------------------------------------------------------------
// The whole S&H: trigger -> chain -> LF398
// ---------------------------------------------------------------------------------------------

/** The held capacitor voltage after one trigger edge at absolute time `tTrig`, the input a ramp. */
static double endToEnd(double dt, double tTrig, double S, double v0, lf398::Spec sp) {
	yash::PulseChain chain;
	lf398::Channel ch;
	Fn vin = [S, v0](double t) { return v0 + S * t; };
	ch.step(sp, 1e-9, vin(0), vin(0), 0, 0);
	double t = 0.0;
	while (t < 200e-6 - 1e-12) {
		double d = dt;
		if (t + d > 200e-6) d = 200e-6 - t;
		if (tTrig >= t && tTrig < t + d) chain.pushTrigger(tTrig - t, +1);
		yash::Pulse out[4];
		int n = chain.advance(d, out, 4);
		lf398::Window w[4];
		for (int j = 0; j < n; j++) { w[j].a = out[j].a; w[j].b = out[j].b; }
		ch.step(sp, d, vin(t), vin(t + d), w, n);
		t += d;
	}
	return ch.v;
}

static void testEndToEnd() {
	lf398::Spec sp = clean(1e-9);
	const double S = 5e4, v0 = 2.0;   // 50 V/ms
	const double tail = lf398::kTauFront + 1e-9 / lf398::kGm;
	const double ap = lf398::aperture(sp, true);
	// expected: the input at the end of the pulse plus the aperture, less the lag behind a ramp
	// (a 10 ns timing slack for the sub-step grid)
	double prev = 0.0;
	double dts[2] = { 1.0 / 48000.0, 3e-6 };
	for (int k = 0; k < 2; k++) {
		for (int i = 0; i < 6; i++) {
			double tTrig = 41.0e-6 + i * 3.7e-6;   // different phases inside a 20.8 us step
			double got = endToEnd(dts[k], tTrig, S, v0, sp);
			double tEnd = tTrig + 3.0 * cd4093::tpd(15.0) + yash::pulseWidth() + ap;
			double want = v0 + S * (tEnd - tail);
			char what[96];
			snprintf(what, sizeof what, "end to end, dt %.1f us, trigger at %.1f us", dts[k] * 1e6, tTrig * 1e6);
			checkNear(what, got, want, S * 25e-9);
			if (i > 0 && k == 0) {
				// sub-sample: moving the trigger by 3.7 us moves the held value by S * 3.7 us
				checkNear("a later trigger holds a proportionally later value", got - prev, S * 3.7e-6, S * 20e-9);
			}
			prev = got;
		}
	}
	// the held value is NOT the input at the trigger instant (an ideal S&H) and not the input at
	// the audio sample after it; these controls make sure the check could tell
	{
		double tTrig = 44.7e-6;
		double got = endToEnd(1.0 / 48000.0, tTrig, S, v0, sp);
		double ideal = v0 + S * tTrig;
		check("not an ideal sample at the trigger instant", std::fabs(got - ideal) > 0.12);
		double nextSample = v0 + S * 62.5e-6;   // end of the 20.8 us step holding 44.7 us
		check("not the next audio sample either", std::fabs(got - nextSample) > 0.3);
	}
}

static void testOscillatorDrivesChain() {
	// free-running: oscillator edges -> chain, pot mid-way. One pulse per oscillator period.
	yash::Oscillator osc; yash::PulseChain chain;
	const double dt = 1.0 / 48000.0;
	double R = yash::Oscillator::seriesR(0.5);
	double per = cd4093::timing(R, yash::kCosc, 15.0).period;
	int rises = 0, pulses = 0; double firstP = -1, lastP = -1; double t = 0;
	while (t < 3.0) {
		int ne = osc.step(dt, R);
		for (int e = 0; e < ne; e++) {
			if (osc.a.edges[e].dir > 0) rises++;
			chain.pushTrigger(osc.a.edges[e].at, osc.a.edges[e].dir);
		}
		yash::Pulse out[4];
		int n = chain.advance(dt, out, 4);
		// count pulse starts as windows whose a is not the carry-over at 0
		for (int j = 0; j < n; j++) if (out[j].a > 0.0) {
			pulses++;
			if (firstP < 0) firstP = t + out[j].a;
			lastP = t + out[j].a;
		}
		t += dt;
	}
	check("oscillator ran", rises > 10);
	check("one pulse per oscillator rise (+-1 for the ends / a window starting exactly at 0)",
	      pulses >= rises - 2 && pulses <= rises + 1);
	checkRel("pulse train period is the oscillator's", (lastP - firstP) / (pulses - 1), per, 1e-4);
	// pot range -> frequency
	double fLow = 1.0 / cd4093::timing(yash::Oscillator::seriesR(1.0), yash::kCosc, 15.0).period;
	double fHigh = 1.0 / cd4093::timing(yash::Oscillator::seriesR(0.0), yash::kCosc, 15.0).period;
	check("pot range 3.7 Hz .. 1.5 kHz", fLow > 3.4 && fLow < 3.9 && fHigh > 1300 && fHigh < 1800);
}

static void testTrigInput() {
	yash::TrigInput ti;
	double t[2]; int d[2];
	// a ramp 0 -> 10 V across a 20 us step crosses 0.70 V at 7% of it
	int n = ti.process(0.0, 10.0, 20e-6, t, d);
	check("rising edge found", n == 1 && d[0] == 1);
	checkNear("rising crossing at 0.7/10 of the step", t[0], ti.th.on / 10.0 * 20e-6, 1e-12);
	n = ti.process(10.0, 10.0, 20e-6, t, d);
	check("no second edge while high", n == 0);
	n = ti.process(10.0, 0.0, 20e-6, t, d);
	check("falling edge found", n == 1 && d[0] == -1);
	checkNear("falling crossing", t[0], (10.0 - ti.th.off) / 10.0 * 20e-6, 1e-12);
	// below the threshold nothing happens (1 V is enough, 0.5 V is not)
	n = ti.process(0.0, 0.5, 20e-6, t, d);
	check("0.5 V does not trigger", n == 0);
}

int main() {
	testAcquisition();
	testHold();
	testAperture();
	testStepInvariance();
	testCd4093();
	testAstable();
	testPulse();
	testEndToEnd();
	testOscillatorDrivesChain();
	testTrigInput();
	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}

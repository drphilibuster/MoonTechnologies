// The vactrol model: LED law, the VTL5C3's resistance against current, and its response in
// time (asymmetry, level dependence, the memory). Every claim is checked against the
// PerkinElmer databook's own numbers or against an independent solver, then the whole suite
// is run again against deliberately broken models, which must be caught.
#include "../../src/Vactrol.hpp"

#include <cmath>
#include <cstdio>
#include <functional>

static int checks = 0, failures = 0;
static bool quiet = false;
static void fail(const char* what, const char* detail) {
	failures++;
	if (!quiet) printf("  FAIL  %s: %s\n", what, detail);
}
#define CHECK(cond, what, ...) do { checks++; if (!(cond)) { char d_[200]; snprintf(d_, sizeof d_, __VA_ARGS__); fail(what, d_); } } while (0)

static const double kFs = 48000.0, kDt = 1.0 / kFs;

// ---- an independent LED solver: bisection on the same diode, no Newton ------------------------
static double ledBisect(double v, double rs) {
	const double is = 0.020 / std::expm1(vactrol::kLedVf20 / vactrol::kLedNVt);
	double lo = 0, hi = 1.0;
	for (int i = 0; i < 200; i++) {
		double mid = 0.5 * (lo + hi);
		double vf = vactrol::kLedNVt * std::log1p(mid / is) + vactrol::kLedRs * mid;
		if (vf + rs * mid > v) hi = mid; else lo = mid;
	}
	return 0.5 * (lo + hi);
}

struct Model {
	std::function<double(double, double)> led;
	std::function<double(double)> steadyR;
	std::function<void(vactrol::Vactrol&)> tweak;     // break the dynamics
};

static double timeUntil(vactrol::Vactrol& v, double iLed, double target, bool rising, double tmax) {
	for (int i = 1; i <= (int)(tmax * kFs); i++) {
		v.step(iLed);
		double r = v.resistance();
		if (rising ? r <= target : r >= target) return i * kDt;
	}
	return -1;
}

static void suite(const Model& m) {
	// --- LED: KCL against the independent solver, and the datasheet's 1.65 V at 20 mA -----------
	for (double v : {0.5, 1.2, 1.5, 1.7, 2.0, 3.0, 5.0, 10.0, 15.0})
		for (double rs : {330.0, 100.0, 1000.0}) {
			double a = m.led(v, rs), b = ledBisect(v, rs);
			CHECK(std::fabs(a - b) <= 1e-6 * b + 1e-12, "led", "V %.1f Rs %.0f: %.6g A vs bisection %.6g A", v, rs, a, b);
		}
	{
		double i = m.led(10.0, 330.0);
		CHECK(i > 0.024 && i < 0.026, "led", "10 V through 330 ohm gave %.3f mA (expect ~25)", i * 1e3);
		double i20 = m.led(1.65 + 0.020 * 0.0, 0.0);      // terminal 1.65 V, no series resistor
		CHECK(i20 > 0.017 && i20 < 0.023, "led", "1.65 V at the LED gave %.2f mA (datasheet typ 20 mA)", i20 * 1e3);
		CHECK(m.led(-3.0, 330.0) == 0.0 && m.led(0.0, 330.0) == 0.0, "led", "negative or zero drive must carry nothing");
		CHECK(m.led(1.0, 330.0) < 1e-6, "led", "1 V is below the knee but carried %.3g A", m.led(1.0, 330.0));
		double prev = 0; bool mono = true;
		for (double v = 0; v < 20; v += 0.05) { double c = m.led(v, 330.0); if (c < prev) mono = false; prev = c; }
		CHECK(mono, "led", "current not monotone in drive");
	}

	// --- steady-state resistance against the databook -----------------------------------------
	{
		double r1 = m.steadyR(1e-3), r40 = m.steadyR(40e-3), r05 = m.steadyR(0.5e-3), r5 = m.steadyR(5e-3);
		CHECK(r1 > 26e3 && r1 < 50e3, "R(I)", "1 mA gave %.0f ohm (databook 30 kohm typ, samples 26 kohm; curve family up to ~70 kohm)", r1);
		CHECK(r40 > 1.1e3 && r40 < 1.6e3, "R(I)", "40 mA gave %.0f ohm (databook 1.5 kohm, sample 1.4 kohm)", r40);
		CHECK(std::fabs(r05 / r5 - 20.0) < 3.0, "R(I)", "R(0.5 mA)/R(5 mA) = %.1f, databook slope 20", r05 / r5);
		CHECK(r5 > 4e3 && r5 < 9e3, "R(I)", "5 mA gave %.0f ohm (sample 5.5 kohm, 5-10k on-resistance in the MiaW schematic)", r5);
		CHECK(m.steadyR(0.0) >= 10e6, "R(I)", "dark resistance %.3g ohm below the databook's 10 Mohm minimum", m.steadyR(0.0));
		double prev = 1e30; bool mono = true;
		for (double ma = 0.0; ma < 60.0; ma += 0.01) { double r = m.steadyR(ma * 1e-3); if (r > prev * (1 + 1e-9)) mono = false; prev = r; }
		CHECK(mono, "R(I)", "resistance not monotone falling with current");
	}

	// --- dynamics --------------------------------------------------------------------------------
	{
		vactrol::Vactrol v; if (m.tweak) m.tweak(v); v.configure(kDt);
		// turn-off to 100 kohm from 10 mA: databook 35 ms max, measured samples 18 ms
		v.settle(10e-3);
		double t100 = timeUntil(v, 0.0, 100e3, false, 1.0);
		CHECK(t100 > 0.010 && t100 < 0.035, "decay", "10 mA -> 100 kohm took %.1f ms (databook <= 35, sample 18)", t100 * 1e3);
		// the databook plot: 1 Mohm reached ~100 ms after the LED goes out
		double t1m = timeUntil(v, 0.0, 1e6, false, 2.0);
		CHECK(t1m > 0.07 && t1m < 0.16, "decay", "10 mA -> 1 Mohm took %.0f ms (plot: ~100)", t1m * 1e3);
		// 10 s after: the databook's >= 10 Mohm
		v.settle(10e-3); for (int i = 0; i < (int)(10.0 * kFs); i++) v.step(0.0);
		CHECK(v.resistance() >= 10e6, "decay", "10 s after the LED went out only %.3g ohm", v.resistance());

		// turn-on to 63 % of the final conductance at 10 mA from a part that has been out for 30 ms
		v.settle(10e-3); for (int i = 0; i < (int)(0.030 * kFs); i++) v.step(0.0);
		double g0 = v.conductance(), gf = vactrol::kDarkConductance + vactrol::steadyLight(10e-3);
		double target = g0 + 0.63 * (gf - g0), tr = -1;
		for (int i = 1; i < (int)(0.1 * kFs); i++) { if (v.step(10e-3) >= target) { tr = i * kDt; break; } }
		CHECK(tr > 0.0018 && tr < 0.0036, "attack", "63%% rise took %.2f ms (databook 2.5 typ, sample 2.4)", tr * 1e3);

		// ASYMMETRY, in the databook's own terms: the fall to 100 kohm after 10 mA (turn-off) takes
		// several times the rise to 63 % of the final conductance (turn-on)
		CHECK(tr > 0 && t100 > 0 && t100 > tr * 3.0, "asymmetry",
		      "63%% rise %.1f ms, fall to 100 kohm %.1f ms: the fall must be much slower", tr * 1e3, t100 * 1e3);
	}
	{
		// THE MEMORY: two time constants. Resistance against time after release from 40 mA: the
		// early decade (1k -> 10k) is fast, the late decade (100k -> 1M) at least ten times slower.
		vactrol::Vactrol v; if (m.tweak) m.tweak(v); v.configure(kDt);
		v.settle(40e-3);
		double ta = timeUntil(v, 0.0, 1.0e4 * 1.0, false, 1.0);
		double tb = timeUntil(v, 0.0, 1.0e5, false, 1.0);
		double tc = timeUntil(v, 0.0, 1.0e6, false, 2.0);
		double earlyRate = std::log(10.0) / (tb - ta);     // decades per second: 10k -> 100k
		double lateRate  = std::log(10.0) / (tc - tb);     // 100k -> 1M
		CHECK(ta > 0 && tb > 0 && tc > 0 && earlyRate > lateRate * 2.0, "memory",
		      "resistance rose a decade in %.1f ms early (10k-100k) and %.1f ms late (100k-1M): not two time constants",
		      (tb - ta) * 1e3, (tc - tb) * 1e3);
		// the slow pool holds a few microsiemens regardless of how hard it was driven: the late
		// decade is the same from 10 mA and from 40 mA (the databook plot's two curves meet)
		vactrol::Vactrol w; if (m.tweak) m.tweak(w); w.configure(kDt);
		w.settle(10e-3); timeUntil(w, 0.0, 1.0e5, false, 1.0); double tc10 = timeUntil(w, 0.0, 1.0e6, false, 2.0);
		CHECK(tc10 > 0 && std::fabs(tc10 - (tc - tb)) < 0.4 * (tc - tb), "memory",
		      "late decade %.1f ms from 10 mA but %.1f ms from 40 mA", tc10 * 1e3, (tc - tb) * 1e3);
	}
	{
		// LIGHT HISTORY: a cell that has been lit for a second is still conducting 30 ms after the
		// LED goes out, more than one that saw only a 3 ms flash of the same current.
		vactrol::Vactrol a, b; if (m.tweak) { m.tweak(a); m.tweak(b); } a.configure(kDt); b.configure(kDt);
		for (int i = 0; i < (int)(1.0 * kFs); i++) a.step(25e-3);
		for (int i = 0; i < (int)(0.003 * kFs); i++) b.step(25e-3);
		for (int i = 0; i < (int)(0.030 * kFs); i++) { a.step(0.0); b.step(0.0); }
		CHECK(a.conductance() > 0 && b.conductance() > 0 && a.conductance() > 1.15 * b.conductance(), "history",
		      "30 ms after release: long-lit %.3g S vs flashed %.3g S (the lit cell should hold more)", a.conductance(), b.conductance());
		// HYSTERESIS: 20 ms after the current has been set to 1 mA, the cell that came down from
		// 25 mA reads a lower resistance than the one that came up from dark, which is below
		// the steady value only if it has not arrived yet.
		vactrol::Vactrol dn, up; if (m.tweak) { m.tweak(dn); m.tweak(up); } dn.configure(kDt); up.configure(kDt);
		dn.settle(25e-3); up.settle(0.0);
		for (int i = 0; i < (int)(0.020 * kFs); i++) { dn.step(1e-3); up.step(1e-3); }
		CHECK(dn.resistance() < 0.85 * up.resistance(), "history",
		      "same 1 mA, 20 ms on: from bright %.0f ohm, from dark %.0f ohm (history should separate them)", dn.resistance(), up.resistance());
		// and they do arrive: a second later both are at the steady value
		for (int i = 0; i < (int)(1.0 * kFs); i++) { dn.step(1e-3); up.step(1e-3); }
		double rs = vactrol::steadyResistance(1e-3);
		CHECK(std::fabs(dn.resistance() / rs - 1) < 0.02 && std::fabs(up.resistance() / rs - 1) < 0.02, "history",
		      "after a second at 1 mA: %.0f and %.0f ohm, steady value %.0f", dn.resistance(), up.resistance(), rs);
	}
	{
		// A dim cell lets go more slowly than a bright one (time to rise tenfold in resistance)
		vactrol::Vactrol a, b; if (m.tweak) { m.tweak(a); m.tweak(b); } a.configure(kDt); b.configure(kDt);
		a.settle(40e-3); b.settle(1e-3);
		double ra = a.resistance(), rb = b.resistance();
		double ta = timeUntil(a, 0.0, ra * 10.0, false, 1.0), tb = timeUntil(b, 0.0, rb * 10.0, false, 1.0);
		CHECK(ta > 0 && tb > ta, "level", "tenfold rise took %.1f ms from 40 mA and %.1f ms from 1 mA", ta * 1e3, tb * 1e3);
	}
	{
		// Fixed current: the output settles to the steady-state table (no drift, no overshoot).
		vactrol::Vactrol v; if (m.tweak) m.tweak(v); v.configure(kDt);
		double peak = 0;
		for (int i = 0; i < (int)(2.0 * kFs); i++) { v.step(5e-3); peak = std::fmax(peak, v.conductance()); }
		double gs = vactrol::kDarkConductance + vactrol::steadyLight(5e-3);
		CHECK(std::fabs(v.conductance() / gs - 1) < 1e-3 && peak < gs * 1.001, "settle", "settled to %.4g S, steady %.4g S, peak %.4g", v.conductance(), gs, peak);
	}
}

int main() {
	Model good;
	good.led = [](double v, double r) { return vactrol::ledCurrent(v, r); };      // (overloaded since the part option)
	good.steadyR = [](double i) { return vactrol::steadyResistance(i); };
	good.tweak = nullptr;
	printf("the VTL5C3 model...\n");
	suite(good);
	int goodChecks = checks, goodFail = failures;
	printf("  %d checks, %d failures\n", goodChecks, goodFail);

	// ---- negative controls: each broken model must be caught -----------------------------------
	struct Ctl { const char* name; Model m; };
	Ctl ctl[6];
	for (int i = 0; i < 6; i++) ctl[i].m = good;
	ctl[0].name = "LED with no forward drop (I = V/R)";
	ctl[0].m.led = [](double v, double r) { return v > 0 ? v / r : 0.0; };
	ctl[1].name = "flat 5k cell (no current dependence)";
	ctl[1].m.steadyR = [](double) { return 5e3; };
	ctl[2].name = "no memory pools (everything fast)";
	ctl[2].m.tweak = [](vactrol::Vactrol& v) { v.capSlow = 0; v.capMedium = 0; };
	ctl[3].name = "symmetric: every fall as quick as the rise";
	ctl[3].m.tweak = [](vactrol::Vactrol& v) { for (int i = 0; i < 3; i++) v.tauFall[i] = 1e-3; v.tauRise2 = 1e-3; };
	ctl[4].name = "one slow time constant only (all pools 70 ms)";
	ctl[4].m.tweak = [](vactrol::Vactrol& v) { for (int i = 0; i < 3; i++) v.tauFall[i] = 70e-3; };
	ctl[5].name = "instant attack";
	ctl[5].m.tweak = [](vactrol::Vactrol& v) { v.tauRise1 = 1e-6; v.tauRise2 = 1e-6; };
	int caught = 0;
	for (int i = 0; i < 6; i++) {
		int f0 = failures; quiet = true;
		suite(ctl[i].m);
		quiet = false;
		int hit = failures - f0;
		printf("  negative control '%s': %d checks failed%s\n", ctl[i].name, hit, hit ? "" : "  <-- NOT CAUGHT");
		if (hit) caught++;
		failures = f0;       // controls are expected to fail; do not count them
	}
	checks++;
	if (caught != 6) { failures++; printf("  FAIL  negative controls: only %d of 6 broken models were caught\n", caught); }

	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}

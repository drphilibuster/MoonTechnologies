// The vactrol PART option (src/Vactrol.hpp's `Part` descriptor): the VTL5C3 stays what it always
// was, bit for bit, and the Silonex NSL-32SR2, NSL-32SR3 and a DIY LED + GL5528 each reproduce
// the numbers on their own datasheet (the sources are listed in Vactrol.hpp). Then the suite is run
// against deliberately broken parts, which must be caught.
#include "../../src/Vactrol.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>

static int checks = 0, failures = 0;
static bool quiet = false;
static void fail(const char* what, const char* detail) {
	failures++;
	if (!quiet) printf("  FAIL  %s: %s\n", what, detail);
}
#define CHECK(cond, what, ...) do { checks++; if (!(cond)) { char d_[240]; snprintf(d_, sizeof d_, __VA_ARGS__); fail(what, d_); } } while (0)

static const double kFs = 192000.0;     // fine enough that sample quantisation is under 0.02 ms

// ---- the pre-option code, verbatim (the header's original ledCurrent / steadyLight) -----------
static double legacyLed(double vdrive, double rSeries) {
	if (!(vdrive > 0.0)) return 0.0;
	const double is = 0.020 / std::expm1(vactrol::kLedVf20 / vactrol::kLedNVt);
	const double rt = rSeries + vactrol::kLedRs;
	double i0 = (vdrive - 1.0) / rt;
	double vj = (i0 > 1e-12) ? vactrol::kLedNVt * std::log1p(i0 / is) : vdrive;
	if (vj > vdrive) vj = vdrive;
	for (int k = 0; k < 40; k++) {
		double e = is * std::expm1(vj / vactrol::kLedNVt);
		double f = vj + rt * e - vdrive;
		double d = 1.0 + rt * (e + is) / vactrol::kLedNVt;
		double step = f / d;
		vj -= step;
		if (std::fabs(step) < 1e-12) break;
	}
	double i = (vdrive - vj) / rt;
	return i > 0.0 ? i : 0.0;
}
static double legacySteady(double iLed) {
	using namespace vactrol;
	if (!(iLed > 0.0)) return 0.0;
	const double ma = iLed * 1e3;
	int s;
	if (ma <= kAnchorMa[0]) s = 0;
	else if (ma >= kAnchorMa[kAnchors - 1]) s = kAnchors - 2;
	else { s = 0; while (ma > kAnchorMa[s + 1]) s++; }
	double slope = (std::log(kAnchorR[s + 1]) - std::log(kAnchorR[s])) /
	               (std::log(kAnchorMa[s + 1]) - std::log(kAnchorMa[s]));
	double lnR = std::log(kAnchorR[s]) + slope * (std::log(ma) - std::log(kAnchorMa[s]));
	double g = std::exp(-lnR);
	double gd = kDarkConductance;
	if (ma > kAnchorMa[0]) return g > gd ? g - gd : 0.0;
	double g0 = std::exp(-std::log(kAnchorR[0])) - gd;
	return (g0 > 0.0 ? g0 : 0.0) * std::pow(ma / kAnchorMa[0], -slope);
}

// A cell built from a (possibly broken) Part: the cell's constants copied from it, as setPart does.
static vactrol::Vactrol cellOf(const vactrol::Part& P, double fs = kFs) {
	vactrol::Vactrol v;
	v.p = &P;
	v.tauRise1 = P.tauRise1; v.tauRise2 = P.tauRise2;
	for (int i = 0; i < 3; i++) v.tauFall[i] = P.tauFall[i];
	v.capMedium = P.capMedium; v.capSlow = P.capSlow;
	v.configure(1.0 / fs);
	return v;
}

static double rise63(const vactrol::Part& P, double iLed) {
	vactrol::Vactrol c = cellOf(P); c.reset();
	double gf = vactrol::steadyLight(iLed, P);
	for (int i = 1; i < (int)(kFs * 2); i++) { c.step(iLed); if (c.conductance() - P.darkG >= 0.632121 * gf) return i / kFs; }
	return -1;
}
static double fallToR(const vactrol::Part& P, double iLed, double r) {
	vactrol::Vactrol c = cellOf(P); c.settle(iLed);
	for (int i = 1; i < (int)(kFs * 30); i++) { c.step(0.0); if (c.resistance() >= r) return i / kFs; }
	return -1;
}
static double fall37(const vactrol::Part& P, double iLed) {
	vactrol::Vactrol c = cellOf(P); c.settle(iLed);
	double g0 = c.conductance() - P.darkG;
	for (int i = 1; i < (int)(kFs * 30); i++) { c.step(0.0); if (c.conductance() - P.darkG <= 0.367879 * g0) return i / kFs; }
	return -1;
}
/** The shape of the fall: time from R=a to R=b divided by time from R=b to R=c (equal decades
    of resistance). A single exponential gives ~1; a pool structure with a slow tail gives much more. */
static double tailRatio(const vactrol::Part& P, double iLed, double a, double b, double c) {
	vactrol::Vactrol v = cellOf(P); v.settle(iLed);
	double ta = -1, tb = -1, tc = -1;
	for (int i = 1; i < (int)(kFs * 60); i++) {
		v.step(0.0);
		double r = v.resistance();
		if (ta < 0 && r >= a) ta = i / kFs;
		if (tb < 0 && r >= b) tb = i / kFs;
		if (tc < 0 && r >= c) { tc = i / kFs; break; }
	}
	if (ta < 0 || tb < 0 || tc < 0) return -1;
	return (tc - tb) / (tb - ta);
}

enum { SR2 = 1, SR3 = 2, GL = 3 };

/** The datasheet checks for part `id`, measured on the Part `P` (the real one, or a broken copy). */
static void suiteFor(int id, const vactrol::Part& P) {
	using namespace vactrol;
	const char* nm = part(id).name;
	// LED law: the terminal voltage at 20 mA is the part's Vf (a 330 ohm resistor on top of it)
	{
		double vf = P.ledVf20 + P.ledRs * 0.020;
		double i = ledCurrent(vf + 330.0 * 0.020, 330.0, P);
		CHECK(std::fabs(i - 0.020) < 2e-4, nm, "drive of Vf + 6.6 V gives %.3f mA, not 20", i * 1e3);
		double vfExpect = id == SR2 || id == SR3 ? 2.0 : 2.1;          // assumed, inside the sheets' 2.5 V max
		CHECK(std::fabs(P.ledVf20 - vfExpect) < 0.01 && P.ledVf20 <= 2.5, nm, "LED Vf %.2f V at 20 mA", P.ledVf20);
	}
	if (id == SR2) {
		// Advanced Photonix NSL-32SR2 REV 12-02-15
		CHECK(steadyResistance(0.020, P) <= 40.0, nm, "R(20 mA) = %.1f ohm, sheet max 40", steadyResistance(0.020, P));
		double r1 = steadyResistance(0.001, P);
		CHECK(r1 > 100.0 && r1 < 160.0, nm, "R(1 mA) = %.1f ohm, sheet 140 typ (curve 122)", r1);
		CHECK(std::fabs(1.0 / P.darkG - 5e6) < 1e4 && 1.0 / P.darkG >= 1e6, nm, "R(off) = %.3g ohm, sheet 1 Mohm min / 5 Mohm typ", 1.0 / P.darkG);
		double tr = rise63(P, 0.005);
		CHECK(std::fabs(tr - 5e-3) < 0.25e-3, nm, "rise to 63 %% = %.3f ms, sheet 5", tr * 1e3);
		double tf = fallToR(P, 0.016, 1e5);
		CHECK(std::fabs(tf - 5e-3) < 0.25e-3, nm, "decay to 100 kohm from 16 mA = %.3f ms, sheet 5", tf * 1e3);
		CHECK(P.maxLedCurrent == 0.025, nm, "LED limit %.0f mA, sheet 25", P.maxLedCurrent * 1e3);
	}
	if (id == SR3) {
		// Silonex NSL-32SR3 104058 REV 3
		double r20 = steadyResistance(0.020, P);
		CHECK(r20 > 55.0 && r20 < 65.0, nm, "R(20 mA) = %.1f ohm, sheet 60 max", r20);
		double r5 = steadyResistance(0.005, P);
		CHECK(r5 > 100.0 && r5 <= 150.0, nm, "R(5 mA) = %.1f ohm, sheet 150", r5);
		CHECK(std::fabs(1.0 / P.darkG - 25e6) < 1e4, nm, "R(off) = %.3g ohm, sheet 25 Mohm min", 1.0 / P.darkG);
		double tr = rise63(P, 0.005);
		CHECK(std::fabs(tr - 5e-3) < 0.25e-3, nm, "rise to 63 %% = %.3f ms, sheet 5", tr * 1e3);
		double tf = fallToR(P, 0.005, 1e5);
		CHECK(std::fabs(tf - 10e-3) < 0.5e-3, nm, "decay to 100 kohm from 5 mA = %.3f ms, sheet 10", tf * 1e3);
	}
	if (id == SR2 || id == SR3) {
		// no memory in either: a single decay constant, so equal decades of resistance take equal times
		double q = tailRatio(P, 0.016, 1e3, 1e4, 1e5);
		CHECK(q > 0.8 && q < 1.25, nm, "fall is not one exponential (decade ratio %.2f)", q);
	}
	if (id == GL) {
		// GL55 series sheet, GL5528 row; LED coupling anchored at ~400 ohm for 10 mA
		double r10 = steadyResistance(10.0 / 378.0 * 1e-3, P);     // the current that makes 10 lux
		CHECK(r10 > 13e3 && r10 < 15.2e3, nm, "R at 10 lux = %.0f ohm, sheet 10-20 k (14.1 k used)", r10);
		double r100 = steadyResistance(100.0 / 378.0 * 1e-3, P);   // 100 lux
		double gamma = std::log10(r10 / r100);
		CHECK(std::fabs(gamma - 0.6) < 0.03, nm, "gamma %.3f, sheet 0.6", gamma);
		double r10mA = steadyResistance(0.010, P);
		CHECK(r10mA > 350.0 && r10mA < 450.0, nm, "R(10 mA) = %.0f ohm, report ~400", r10mA);
		CHECK(1.0 / P.darkG >= 1e6, nm, "dark resistance %.3g ohm under the sheet's 1 Mohm", 1.0 / P.darkG);
		double tr = rise63(P, 0.005);
		CHECK(std::fabs(tr - 20e-3) < 0.5e-3, nm, "rise to 63 %% = %.2f ms, sheet 20", tr * 1e3);
		double tf = fall37(P, 0.005);
		CHECK(std::fabs(tf - 30e-3) < 0.7e-3, nm, "decay to 37 %% = %.2f ms, sheet 30", tf * 1e3);
		double q = tailRatio(P, 0.005, 1e3, 1e4, 1e5);
		CHECK(q > 0.8 && q < 1.25, nm, "fall is not one exponential (decade ratio %.2f)", q);
		CHECK(P.maxLedCurrent == 0.030, nm, "LED limit %.0f mA", P.maxLedCurrent * 1e3);
	}
}

int main() {
	using namespace vactrol;
	printf("the part option...\n");

	// ---- the descriptor table ----------------------------------------------------------------
	CHECK(PART_COUNT == 4, "table", "%d parts", (int) PART_COUNT);
	for (int i = 0; i < PART_COUNT; i++) {
		const Part& P = part(i);
		bool ok = P.name && P.nAnchors == 9 && P.darkG > 0 && P.maxLedCurrent > 0;
		for (int k = 1; ok && k < P.nAnchors; k++) ok = P.anchorMa[k] > P.anchorMa[k - 1] && P.anchorR[k] < P.anchorR[k - 1];
		CHECK(ok, "table", "part %d (%s) has a malformed descriptor", i, P.name ? P.name : "?");
	}
	CHECK(&part(-3) == &part(0) && &part(99) == &part(PART_COUNT - 1), "table", "out-of-range ids are not clamped");

	// ---- the default is the VTL5C3 and is what the header always did --------------------------
	{
		const Part& P = part(PART_VTL5C3);
		CHECK(P.anchorMa == kAnchorMa && P.anchorR == kAnchorR && P.darkG == kDarkConductance &&
		      P.ledNVt == kLedNVt && P.ledVf20 == kLedVf20 && P.tauRise1 == kTauStage1 && P.tauFall[2] == kTauFall[2],
		      "default", "the VTL5C3 row is not the legacy constants");
		Vactrol v;
		CHECK(v.p == &kParts[0] && v.partId == 0 && v.tauRise1 == kTauStage1, "default", "a fresh cell is not the VTL5C3");
		// the code path: the verbatim pre-option functions against the descriptor ones
		double worstLed = 0, worstSteady = 0;
		for (double vd = -1.0; vd <= 15.0; vd += 0.0371) {
			double a = legacyLed(vd, 330.0), b = ledCurrent(vd, 330.0), c = ledCurrent(vd, 330.0, P);
			worstLed = std::fmax(worstLed, std::fmax(std::fabs(a - b), std::fabs(a - c)));
		}
		for (double ma = 0.01; ma < 60.0; ma *= 1.0137) {
			double a = legacySteady(ma * 1e-3), b = steadyLight(ma * 1e-3), c = steadyLight(ma * 1e-3, P);
			worstSteady = std::fmax(worstSteady, std::fmax(std::fabs(a - b), std::fabs(a - c)) / a);
		}
		CHECK(worstLed == 0.0, "default", "LED current differs from the pre-option code by %.3g A", worstLed);
		CHECK(worstSteady == 0.0, "default", "steady conductance differs from the pre-option code by %.3g (relative)", worstSteady);
		// and frozen numbers captured from the pre-option header (committed 8f1ee42), against libm differences
		struct Pt { int n; double i, g; };
		static const Pt kFrozen[] = {
			{ 10, 2.525171778928e-02, 7.889201017644e-06 }, { 100, 2.525171778928e-02, 3.027272364860e-04 },
			{ 480, 2.525171778928e-02, 6.491053832175e-04 }, { 4800, 2.525171778928e-02, 6.523356320505e-04 },
			{ 9600, 2.919654892085e-03, 9.716856364867e-05 }, { 14400, 0.0, 9.707558481951e-07 },
			{ 19200, 1.326457053379e-02, 4.201621756453e-04 }, { 30000, 0.0, 6.805067601928e-07 } };
		Vactrol c; c.configure(1.0 / 48000.0);
		size_t k = 0; bool okf = true; double bad = 0;
		for (int i = 1; i <= 30000 && k < sizeof kFrozen / sizeof kFrozen[0]; i++) {
			double vd = (i <= 4800) ? 10.0 : (i <= 9600 ? 2.5 : (i <= 14400 ? 0.0 : (i <= 24000 ? 6.0 : 0.0)));
			double cur = ledCurrent(vd, 330.0), g = c.step(cur);
			if (i == kFrozen[k].n) {
				double e1 = std::fabs(cur - kFrozen[k].i) / (kFrozen[k].i > 0 ? kFrozen[k].i : 1.0), e2 = std::fabs(g / kFrozen[k].g - 1.0);
				if (e1 > 1e-10 || e2 > 1e-10) { okf = false; bad = std::fmax(e1, e2); }
				k++;
			}
		}
		CHECK(okf, "default", "a VTL5C3 run no longer matches the numbers frozen from the pre-option header (%.3g)", bad);
		// setPart(0) on a fresh cell is a no-op, and a round trip through another part restores the constants
		Vactrol a, b; a.configure(1.0 / 48000.0); b.configure(1.0 / 48000.0);
		b.setPart(0);
		double dmax = 0;
		for (int i = 0; i < 5000; i++) dmax = std::fmax(dmax, std::fabs(a.step(0.01) - b.step(0.01)));
		CHECK(dmax == 0.0, "default", "setPart(0) changed a fresh cell");
		b.setPart(GL); b.setPart(0);
		CHECK(b.tauRise1 == kTauStage1 && b.tauRise2 == kTauRise2 && b.tauFall[0] == kTauFall[0] && b.tauFall[2] == kTauFall[2] &&
		      b.capMedium == kCapMedium && b.capSlow == kCapSlow && b.p == &kParts[0], "default", "a round trip through another part leaves the VTL5C3 changed");
	}

	// ---- each added part against its own sheet -------------------------------------------------
	for (int id = SR2; id <= GL; id++) suiteFor(id, part(id));
	printf("  %d checks, %d failures\n", checks, failures);

	// ---- the parts are different from each other, and in the order the sheets say ---------------
	{
		double r5[4], f16[4];
		for (int i = 0; i < 4; i++) { r5[i] = steadyResistance(0.005, part(i)); f16[i] = fallToR(part(i), 0.016, 1e5); }
		CHECK(r5[SR2] < r5[SR3] && r5[SR3] < r5[GL] && r5[GL] < r5[0], "order", "R(5 mA): %.0f %.0f %.0f %.0f", r5[0], r5[1], r5[2], r5[3]);
		CHECK(f16[SR2] < f16[SR3] && f16[SR3] < f16[0] && f16[0] < f16[GL], "order", "decay to 100k: %.1f %.1f %.1f %.1f ms", f16[0] * 1e3, f16[1] * 1e3, f16[2] * 1e3, f16[3] * 1e3);
		// the VTL5C3 has a memory the others do not
		CHECK(tailRatio(part(0), 0.016, 1e4, 1e5, 1e6) > 2.0, "order", "the VTL5C3 lost its memory tail");
		// setPart on a running cell retargets it: the same LED current then settles to the new part's value
		for (int id = 1; id < 4; id++) {
			Vactrol c; c.configure(1.0 / 48000.0);
			for (int i = 0; i < 48000; i++) c.step(0.005);
			double g0 = c.conductance();
			c.setPart(id);
			for (int i = 0; i < 48000 * 3; i++) c.step(0.005);
			double want = part(id).darkG + steadyLight(0.005, part(id));
			CHECK(c.partId == id && std::fabs(c.conductance() / want - 1.0) < 2e-3 && std::fabs(c.conductance() / g0 - 1.0) > 0.5,
			      "setPart", "after setPart(%d) the cell is at %.4g S (want %.4g, was %.4g)", id, c.conductance(), want, g0);
		}
	}

	// ---- negative controls: broken parts must fail their own sheet's checks ---------------------
	struct Ctl { const char* name; int id; Part p; };
	Ctl ctl[8];
	int n = 0;
	auto add = [&](const char* name, int id, std::function<void(Part&)> brk) { ctl[n].name = name; ctl[n].id = id; ctl[n].p = part(id); brk(ctl[n].p); n++; };
	add("SR2 with the SR3's curve", SR2, [](Part& p) { p.anchorR = part(SR3).anchorR; });
	add("SR3 with the SR2's curve", SR3, [](Part& p) { p.anchorR = part(SR2).anchorR; });
	add("SR2 with the VTL5C3's curve", SR2, [](Part& p) { p.anchorR = kAnchorR; });
	add("SR2 with a 20 ms decay", SR2, [](Part& p) { p.tauFall[0] = 3.5e-3; });
	add("SR3 with the VTL5C3's memory pools", SR3, [](Part& p) { p.capMedium = kCapMedium; p.capSlow = kCapSlow; p.tauFall[1] = kTauFall[1]; p.tauFall[2] = kTauFall[2]; });
	add("GL5528 with the SR2's speed", GL, [](Part& p) { p.tauRise2 = 4.9e-3; p.tauFall[0] = 0.59e-3; });
	add("GL5528 with gamma 1", GL, [](Part& p) { static double r[9]; for (int k = 0; k < 9; k++) r[k] = 400.0 * 10.0 / part(GL).anchorMa[k]; p.anchorR = r; });
	add("GL5528 with the LED of a VTL5C3", GL, [](Part& p) { p.ledVf20 = kLedVf20; });
	int caught = 0;
	for (int i = 0; i < n; i++) {
		int f0 = failures; quiet = true;
		suiteFor(ctl[i].id, ctl[i].p);
		quiet = false;
		int hit = failures - f0;
		printf("  negative control '%s': %d checks failed%s\n", ctl[i].name, hit, hit ? "" : "  <-- NOT CAUGHT");
		if (hit) caught++;
		failures = f0;
	}
	checks++;
	if (caught != n) { failures++; printf("  FAIL  negative controls: only %d of %d broken parts were caught\n", caught, n); }

	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}

// The CD4046B VCO (Cd4046.hpp) against the numbers printed in its data sheets, and what
// patching a signal onto the R1 pin does. The oracles are not the header's own tables:
//  * the Nexperia HEF4046B Rev. 5 "maximum frequency" rows and its design rule f0 = 0.5 fmax,
//  * the design-information equation fmax = 1/(R1 (C1 + 32 pF)), which the RCA/TI/onsemi sheets
//    say may be "in error by as much as a factor of 4",
//  * the TI/RCA statement that the transfer is a straight line from f = 0 at VSS,
//  * independent arithmetic (a log-log line through two Fig. 7 end points, written out here).
// Every check runs through a model callable, so the negative controls at the end can run the
// same checks against deliberately broken models and demand that they fail.

#include "../../src/SixFigures/Cd4046.hpp"

#include <cmath>
#include <cstdio>
#include <functional>

using namespace sixfigures::cd4046;

typedef std::function<double(double, double, double, double, const Jack&)> Model;

static int checks = 0, failures = 0;
static bool g_quiet = false;
static void check(const char* what, bool ok) {
	checks++;
	if (!ok) {
		failures++;
		if (!g_quiet)
			printf("  FAIL  %s\n", what);
	}
}
static void checkrel(const char* what, double got, double want, double rel) {
	checks++;
	if (!(std::fabs(got - want) <= rel * std::fabs(want))) {
		failures++;
		if (!g_quiet)
			printf("  FAIL  %s: got %.6g, want %.6g (rel tol %.3g)\n", what, got, want, rel);
	}
}

static Model realModel() {
	return [](double v, double vdd, double r1, double c1, const Jack& j) { return freq(v, vdd, r1, c1, j); };
}

// Runs every datasheet check against `m`.
static void suite(const Model& m) {
	// --- 1. Straight line from f = 0 at VSS, f0 at VDD/2, 2 f0 at VDD (below the speed limit). -----
	{
		const double vdd = 12, r1 = 100e3, c1 = 10e-9;
		const double f0 = m(vdd / 2, vdd, r1, c1, Jack());
		check("f(0 V) = 0 (R2 not fitted: f_min = 0)", m(0, vdd, r1, c1, Jack()) == 0.0);
		check("f(-3 V) = 0 (input diodes clamp at VSS)", m(-3, vdd, r1, c1, Jack()) == 0.0);
		checkrel("f(VDD) = 2 f0  (f0 = 0.5 fmax)", m(vdd, vdd, r1, c1, Jack()), 2 * f0, 1e-9);
		checkrel("f(VDD+5 V) = f(VDD) (clamped at the rail)", m(vdd + 5, vdd, r1, c1, Jack()), m(vdd, vdd, r1, c1, Jack()), 1e-12);
		for (int k = 1; k <= 11; k++) {
			double v = k;  // 1..11 V
			char what[96];
			snprintf(what, sizeof what, "linear: f(%g V) = f0 * VCOIN/(VDD/2)", v);
			checkrel(what, m(v, vdd, r1, c1, Jack()), f0 * v / (vdd / 2), 1e-9);
		}
	}

	// --- 2. f0 against Fig. 7, by independent arithmetic from its end points. ------------------------
	{
		// 100k, 10 V: 1.713e5 Hz at 50 pF down to 8.944 Hz at 1.016e6 pF, a straight log-log line.
		double slope = std::log(8.944 / 1.713e5) / std::log(1.016e6 / 50.0);
		double f0at10nF = 1.713e5 * std::pow(1e4 / 50.0, slope);  // ~ 887 Hz
		// f0 is the frequency at VCOIN = VDD/2; the model gives it back at exactly that input.
		checkrel("Fig.7 100k 10 V 10 nF: f0 ~ 887 Hz", m(5.0, 10.0, 100e3, 10e-9, Jack()), f0at10nF, 0.01);
		// 100k, 15 V: 2.288e5 at 50.63 pF to 12.73 at 9.912e5 pF.
		slope = std::log(12.73 / 2.288e5) / std::log(9.912e5 / 50.63);
		double f15 = 2.288e5 * std::pow(1e4 / 50.63, slope);
		checkrel("Fig.7 100k 15 V 10 nF", m(7.5, 15.0, 100e3, 10e-9, Jack()), f15, 0.02);
		// 1 Mohm, 5 V, 100 nF: line from 8884 Hz at 49.38 pF to 1.013 Hz at 4.964e5 pF.
		slope = std::log(1.013 / 8884.0) / std::log(4.964e5 / 49.38);
		double f1M = 8884.0 * std::pow(1e5 / 49.38, slope);
		checkrel("Fig.7 1M 5 V 100 nF", m(2.5, 5.0, 1e6, 100e-9, Jack()), f1M, 0.01);
		// f0 falls as 1/C1 (slope -1 +- 3 %) over three decades of C1, at every plotted R1 and VDD.
		for (double r1 = 1e5; r1 <= 1e6; r1 *= 10)
			for (double vdd = 5; vdd <= 15; vdd += 5) {
				double a = m(vdd / 2, vdd, r1, 1e-9, Jack()), b = m(vdd / 2, vdd, r1, 1e-6, Jack());
				double s = std::log(b / a) / std::log(1e3);
				check("log-log slope of f0 against C1 is -1 (+-4%: the plotted 1M, 10 V line is -0.969)", std::fabs(s + 1.0) < 0.04);
			}
	}

	// --- 3. Maximum frequency rows (R1 = 10k, C1 = 50 pF, VCOIN = VDD), Nexperia typicals. -----------
	{
		checkrel("fmax typ 5 V  1.0 MHz",  m(5, 5, 10e3, 50e-12, Jack()), 1.0e6, 0.05);
		checkrel("fmax typ 10 V 2.0 MHz",  m(10, 10, 10e3, 50e-12, Jack()), 2.0e6, 0.001);
		checkrel("fmax typ 15 V 2.7 MHz",  m(15, 15, 10e3, 50e-12, Jack()), 2.7e6, 0.001);
		// and the sheet's minimums (0.5 / 1.0 / 1.3 MHz) are exceeded.
		check("fmax >= datasheet min at 5 V",  m(5, 5, 10e3, 50e-12, Jack()) >= 0.5e6);
		check("fmax >= datasheet min at 10 V", m(10, 10, 10e3, 50e-12, Jack()) >= 1.0e6);
		check("fmax >= datasheet min at 15 V", m(15, 15, 10e3, 50e-12, Jack()) >= 1.3e6);
		// the speed limit does not touch audio: 100k / 10 nF at 12 V is far under it.
		check("no ceiling at audio", m(12, 12, 100e3, 10e-9, Jack()) < 1e4);
	}

	// --- 4. The design-information equation, within its own stated factor of 4. ----------------------
	{
		int n = 0, bad = 0;
		for (double vdd = 5; vdd <= 15; vdd += 5)
			for (double r1 = 10e3; r1 <= 1e6; r1 *= 10)
				for (double c1 = 100e-12; c1 <= 10e-9; c1 *= 10) {
					double design = 1.0 / (r1 * (c1 + 32e-12));
					double f = m(vdd, vdd, r1, c1, Jack());
					if (f >= speedLimit(vdd) * 0.999)
						continue;  // clipped by the speed limit; the design equation ignores it
					n++;
					if (f > 4 * design || f < design / 4)
						bad++;
				}
		check("model fmax within a factor of 4 of 1/(R1(C1+32pF)) over the sheet's component range", n > 10 && bad == 0);
	}

	// --- 5. Supply dependence: f0 rises with VDD at fixed R1, C1; 12 V sits between 10 V and 15 V. -----
	{
		double a = m(5.0, 10, 100e3, 10e-9, Jack()), b = m(6.0, 12, 100e3, 10e-9, Jack()),
		       c = m(7.5, 15, 100e3, 10e-9, Jack()), d = m(2.5, 5, 100e3, 10e-9, Jack());
		check("f0 grows with VDD (5 < 10 < 12 < 15 V)", d < a && a < b && b < c);
		// at fixed VCOIN the frequency FALLS as VDD rises (f = 2 f0 VCOIN/VDD, f0 growing slower than VDD
		// for 100k/10n between 10 V and 15 V): 5 V of control voltage at 10 V vs at 15 V.
		check("same VCOIN gives a lower f at a higher VDD (10 -> 15 V)",
		      m(5, 15, 100e3, 10e-9, Jack()) < m(5, 10, 100e3, 10e-9, Jack()));
	}

	// --- 6. The R1 pin. -----------------------------------------------------------------------------
	{
		const double vdd = 12, r1 = 100e3, c1 = 10e-9, v = 6;
		double base = m(v, vdd, r1, c1, Jack());
		check("jack present but not patched = unpatched", m(v, vdd, r1, c1, Jack()) == base);
		// A very high source impedance changes nothing.
		checkrel("patched through 1e12 ohm = unpatched", m(v, vdd, r1, c1, Jack(3.0, 1e12)), base, 1e-6);
		// A source sitting at the node's own voltage changes nothing, whatever its impedance.
		double vnode = v * r1 / (r1 + kRint);
		checkrel("source at the node's own voltage: no change (1k)", m(v, vdd, r1, c1, Jack(vnode, 1000.0)), base, 1e-9);
		checkrel("source at the node's own voltage: no change (10k)", m(v, vdd, r1, c1, Jack(vnode, 10e3)), base, 1e-9);
		// Current modulation: f is affine in the patched voltage (second difference zero) until a limit.
		double fm = m(v, vdd, r1, c1, Jack(0.0, 100e3));
		double fa = m(v, vdd, r1, c1, Jack(-0.2, 100e3)), fb = m(v, vdd, r1, c1, Jack(0.2, 100e3));
		checkrel("affine in the patched voltage (second difference)", fa + fb, 2 * fm, 1e-9);
		check("a positive patched voltage lowers f, a negative one raises it", fb < fm && fm < fa);
		// the slope: df/dVs = -2 f0 (R1+Rint)/VDD /(Rs) * (kRint-weighted node gain) - derived independently.
		{
			double f0 = centerFreq(vdd, r1, c1);
			double rs = 100e3, g = 1 / kRint + 1 / r1 + 1 / rs;
			double slopeExpected = -2 * f0 * (r1 + kRint) / vdd * (1 / rs) / g / kRint;
			checkrel("df/dVs from the node equation", (fb - fa) / 0.4, slopeExpected, 1e-6);
		}
		// A signal above VCOIN stops the oscillator (source-only drive).
		check("a patched voltage at or above VCOIN stops the VCO", m(v, vdd, r1, c1, Jack(8.0, 1000.0)) == 0.0);
		// A hard-grounded jack (a 0 ohm source at 0 V) takes over the node: R1 is shorted, f rises sharply.
		check("a low-impedance ground on pin 11 speeds the VCO up a lot", m(v, vdd, r1, c1, Jack(0.0, 100.0)) > 10 * base);
		// ...but never beyond the chip's speed limit.
		check("the speed limit holds under injection", m(v, vdd, r1, c1, Jack(-5.0, 1.0)) <= speedLimit(vdd) * 1.0000001);
		// Symmetric swing about the node voltage: mean of f equals the unpatched f (unclipped linear FM).
		double sum = 0;
		int n = 256;
		for (int k = 0; k < n; k++) {
			double vs = vnode + 0.3 * std::sin(2 * M_PI * k / n);
			sum += m(v, vdd, r1, c1, Jack(vs, 100e3));
		}
		checkrel("symmetric injection leaves the mean frequency alone", sum / n, base, 1e-6);
	}
}

// --- Things that are not the model's output, checked on their own. --------------------------------
static void tables() {
	// Fig. 7 reproduced at its own knots.
	for (int i = 0; i < 9; i++) {
		const Line& L = kFig7[i];
		for (int k = 0; k < L.n; k++) {
			char what[96];
			snprintf(what, sizeof what, "Fig.7 line %d knot %d is reproduced", i, k);
			checkrel(what, lineF0(L, L.c_pF[k]), L.f_Hz[k], 1e-9);
		}
	}
	// The lines nest as the figure draws them: higher VDD above lower, lower R1 above higher.
	for (double c = 100; c <= 1e6; c *= 3) {
		for (int r = 0; r < 3; r++) {
			double f15 = lineF0(kFig7[r * 3 + 0], c), f10 = lineF0(kFig7[r * 3 + 1], c), f5 = lineF0(kFig7[r * 3 + 2], c);
			check("lines ordered 15 V > 10 V > 5 V", f15 > f10 && f10 > f5);
		}
		for (int v = 0; v < 3; v++)
			check("lines ordered by R1 (10k > 100k > 1M)",
			      lineF0(kFig7[0 * 3 + v], c) > lineF0(kFig7[1 * 3 + v], c) && lineF0(kFig7[1 * 3 + v], c) > lineF0(kFig7[2 * 3 + v], c));
	}
	// Interpolation is continuous across the lattice lines (no seams at 10 V, 100 k).
	double e = 1e-6;
	checkrel("continuous across VDD = 10 V", centerFreq(10 - e, 100e3, 10e-9), centerFreq(10 + e, 100e3, 10e-9), 1e-4);
	checkrel("continuous across R1 = 100 k", centerFreq(10, 100e3 * (1 - e), 10e-9), centerFreq(10, 100e3 * (1 + e), 10e-9), 1e-4);
	check("f0 falls as R1 rises (continuous)", centerFreq(12, 50e3, 10e-9) > centerFreq(12, 100e3, 10e-9) &&
	                                           centerFreq(12, 100e3, 10e-9) > centerFreq(12, 300e3, 10e-9));
	// The R1-pin impedance estimate is not free: the 10k lines sit below the 100k/1M lines' f0*R1 by
	// 10-20 %, which kRint explains (R1/(R1+kRint) = 0.82 at 10k). Check that it is of that size.
	for (int v = 0; v < 3; v++) {
		double c = 1e5;  // 100 nF: well clear of the 10k lines' low-C bend
		double p10 = lineF0(kFig7[0 * 3 + v], c) * 1e4, p1M = lineF0(kFig7[2 * 3 + v], c) * 1e6;
		double ratio = p10 / p1M;
		double model = 1e4 / (1e4 + kRint) / (1e6 / (1e6 + kRint));
		check("kRint explains the 10k lines' shortfall to within 0.1", std::fabs(ratio - model) < 0.1);
	}
	// The sheet's linear-from-zero law fits Fig. 7 better than the supply-current note's (VCOIN-1.65):
	// the ratio f0(VDD)/f0(5 V) at fixed R1 C1 scales like VDD, not like (VDD/2-1.65).
	double a = lineF0(kFig7[2 * 3 + 1], 1e5) / lineF0(kFig7[2 * 3 + 2], 1e5);   // 1M: 10 V / 5 V
	double zero = 10.0 / 5.0, offs = (5 - 1.65) / (2.5 - 1.65);
	check("f0(10V)/f0(5V) is nearer VDD-proportional (2) than offset-proportional (3.9)",
	      std::fabs(a - zero) < std::fabs(a - offs));
	// The inverse.
	for (double f = 50; f < 1000; f += 150)
		checkrel("vcoinFor inverts freq", freq(vcoinFor(f, 12, 100e3, 10e-9), 12, 100e3, 10e-9), f, 1e-9);
	check("vcoinFor clamps at the rail", vcoinFor(1e9, 12, 100e3, 10e-9) == 12.0);
	check("freq finite everywhere", std::isfinite(freq(5, 12, 100e3, 5e-6, Jack(-9, 0))));
}

int main() {
	printf("CD4046B VCO\n");

	tables();
	suite(realModel());
	int base = failures;
	printf("  real model: %d checks, %d failures\n", checks, failures);

	// --- Negative controls: each broken model must fail the suite. ---------------------------------
	struct Mutant { const char* name; Model m; };
	Mutant mutants[] = {
		{"onsemi supply-note law: f ~ (VCOIN - 1.65 V), dead zone below it",
		 [](double v, double vdd, double r1, double c1, const Jack& j) {
			 (void)j;
			 double x = v - 1.65;
			 return x <= 0 ? 0.0 : 2 * centerFreq(vdd, r1, c1) * x / (vdd - 1.65);
		 }},
		{"no speed limit",
		 [](double v, double vdd, double r1, double c1, const Jack&) {
			 return 2 * centerFreq(vdd, r1, c1) * (v < 0 ? 0 : (v > vdd ? vdd : v)) / vdd;
		 }},
		{"jack ignored",
		 [](double v, double vdd, double r1, double c1, const Jack&) { return freq(v, vdd, r1, c1, Jack()); }},
		{"f0 independent of VDD (10 V line always)",
		 [](double v, double vdd, double r1, double c1, const Jack& j) {
			 return freq(v * 10.0 / vdd, 10.0, r1, c1, j) ;
		 }},
		{"R1 curve ignored (always the 100k line)",
		 [](double v, double vdd, double, double c1, const Jack& j) { return freq(v, vdd, 100e3, c1, j); }},
		{"bipolar drive (negative injection cannot speed the VCO up)",
		 [](double v, double vdd, double r1, double c1, const Jack& j) {
			 Jack k = j;
			 if (k.patched && k.volts < 0)
				 k.volts = 0;
			 return freq(v, vdd, r1, c1, k);
		 }},
		{"exponential response instead of linear",
		 [](double v, double vdd, double r1, double c1, const Jack& j) {
			 double f0 = centerFreq(vdd, r1, c1);
			 (void)j;
			 return f0 * std::pow(2.0, v / (vdd / 2) - 1);
		 }},
		{"a 1 uF stray added to C1",
		 [](double v, double vdd, double r1, double c1, const Jack& j) { return freq(v, vdd, r1, c1 + 1e-6, j); }},
	};
	int missed = 0;
	for (size_t i = 0; i < sizeof mutants / sizeof mutants[0]; i++) {
		int before = failures;
		g_quiet = true;
		suite(mutants[i].m);
		g_quiet = false;
		int caught = failures - before;
		failures = before;  // the mutant's failures are the point; do not count them
		printf("  negative control: %-70s %s (%d checks tripped)\n", mutants[i].name, caught ? "CAUGHT" : "MISSED", caught);
		if (!caught)
			missed++;
	}
	check("every negative control is caught", missed == 0);
	(void)base;

	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}

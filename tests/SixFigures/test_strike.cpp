// The reverse-avalanche oscillator's strike jitter and its TL072 output stage (Avalanche.hpp):
// the junction strikes when a microplasma fires, a Poisson event whose rate climbs as the
// capacitor charges, so the strike voltage is random. Checked against an independent stepped
// simulation of that process (a hazard per time step on a charging capacitor), against the
// published measurement the rate is calibrated to, and against negative controls.

#include "../../src/SixFigures/Avalanche.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace sixfigures::avalanche;

static int checks = 0, failures = 0;
static void check(const char* what, bool ok) {
	checks++;
	if (!ok) { failures++; printf("  FAIL  %s\n", what); }
}
static void checkv(const char* what, double got, double want, double tol) {
	checks++;
	if (!(std::fabs(got - want) <= tol)) {
		failures++;
		printf("  FAIL  %s: got %.6g, want %.6g (tol %.3g)\n", what, got, want, tol);
	}
}

// KS distance of samples from a CDF given as a function
template <class F>
static double ks(std::vector<double> x, F cdf) {
	std::sort(x.begin(), x.end());
	double d = 0, n = (double)x.size();
	for (size_t i = 0; i < x.size(); i++) {
		double f = cdf(x[i]);
		d = std::max(d, std::max(std::fabs(f - i / n), std::fabs(f - (i + 1) / n)));
	}
	return d;
}

// The independent simulation: a capacitor charging through R toward the source, h seconds a
// step; each step the junction strikes with probability 1 - exp(-r(V) h), r(V) the same
// exponential law the table integrates, with r0 taken from the published calibration (median
// strike 8.2 V at 1 s) by bisection on this simulation's own cumulative hazard. Returns the
// strike voltages of `cycles` cycles. The charge is advanced with the exact exponential.
static std::vector<double> simulateStrikes(double rc, double r0, double efold, int cycles, double h, uint32_t seed) {
	microplasma::Rng rng(seed);
	double vs = source();
	std::vector<double> out;
	for (int c = 0; c < cycles; c++) {
		double v = kVRelease, a = std::exp(-h / rc);
		for (long i = 0; i < 400000000L; i++) {
			v = vs - (vs - v) * a;
			double r = r0 * std::exp((v - kVStrike) / efold);
			if (rng.uniform() < -std::expm1(-r * h)) break;
			if (v > vs - 0.3) break;
		}
		out.push_back(v);
	}
	return out;
}

static void testTable() {
	printf("the cumulative hazard\n");
	StrikeTable t;
	// independent numeric integral of exp((u - Vn)/ef)/(Vs - u) by a fine midpoint rule
	double vs = source();
	auto G = [&](double v) {
		double a = kVStrike - 0.6, s = 0;
		int n = 200000;
		double d = (v - a) / n;
		for (int i = 0; i < n; i++) { double u = a + (i + 0.5) * d; s += std::exp((u - kVStrike) / kStrikeEfold) / (vs - u) * d; }
		return s;
	};
	for (double v : { 7.8, 8.0, 8.2, 8.3, 8.5 })
		checkv("table matches an independent fine integral within 0.3 %", t.at(v) / G(v), 1.0, 0.003);
	checkv("calibration: P(struck by 8.2 V) at the measured 1 s time constant is one half", t.cdf(kVStrike, kRcRef), 0.5, 1e-9);
	checkv("the median strike at 1 s is the measured 8.2 V", t.median(kRcRef), kVStrike, 0.002);
	double m10 = t.median(0.01), m1 = t.median(1.0), m100 = t.median(100.0);
	printf("    median strike voltage at RC = 10 ms / 1 s / 100 s: %.3f %.3f %.3f V\n", m10, m1, m100);
	check("statistical lag: charging faster, the junction strikes later (higher voltage)", m10 > m1 && m1 > m100);
	check("and the shift is real but modest (10 ms is under 0.45 V over the measured 8.2 V)", m10 - m1 > 0.05 && m10 - m1 < 0.45);
	check("the table is monotone", [&]() { for (size_t i = 1; i < t.g.size(); i++) if (t.g[i] < t.g[i - 1]) return false; return true; }());
}

static void testDistribution() {
	printf("the strike voltage distribution against a stepped simulation\n");
	StrikeTable t;
	for (double rc : { 1.0, 0.02 }) {
		double h = rc * 2e-5;
		int cycles = rc > 0.5 ? 1500 : 3000;
		std::vector<double> sim = simulateStrikes(rc, t.r0, kStrikeEfold, cycles, h, 77u + (uint32_t)(rc * 100));
		double d = ks(sim, [&](double v) { return t.cdf(v, rc); });
		double crit = 1.95 / std::sqrt((double)cycles);
		printf("    RC %.2f s: simulated median %.4f V vs table %.4f V; KS distance %.4f (0.1 %% critical %.4f)\n", rc, [&]() { std::vector<double> s = sim; std::sort(s.begin(), s.end()); return s[s.size() / 2]; }(), t.median(rc), d, crit);
		check("the simulated strike voltages follow the table's CDF (KS, 0.1 % level)", d < crit);
		// and the inverse-CDF draw
		microplasma::Rng rng(5u);
		std::vector<double> drawn;
		for (int i = 0; i < 20000; i++) drawn.push_back(t.strikeFor(-std::log(rng.uniform()), rc));
		double d2 = ks(drawn, [&](double v) { return t.cdf(v, rc); });
		check("the inverse-CDF draw follows the same CDF", d2 < 1.95 / std::sqrt(20000.0) + 0.002);
		// NEGATIVE CONTROL: a table with twice the e-fold (looser rate law) fails against the simulation
		StrikeTable wrong(2.0 * kStrikeEfold);
		double dw = ks(sim, [&](double v) { return wrong.cdf(v, rc); });
		check("NEGATIVE CONTROL: the simulation is rejected by a table with the wrong e-fold", dw > crit);
	}
}

static void testStrike() {
	printf("the per-cycle draw\n");
	for (double hz : { 5.0, 110.0, 440.0, 3000.0 }) {
		Strike s(0xABCDEFu);
		int n = 40000, below = 0;
		double sum = 0, sum2 = 0, wsum = 0;
		for (int i = 0; i < n; i++) {
			s.draw(hz);
			if (s.theta < 1.f) below++;
			sum += s.theta; sum2 += (double)s.theta * s.theta; wsum += s.warp;
		}
		double mean = sum / n, sd = std::sqrt(sum2 / n - mean * mean);
		printf("    %.0f Hz: median cycle = 1 (%.3f below it), mean %.4f, sd %.2f %%, warp %.3f\n", hz, (double)below / n, mean, sd * 100, wsum / n);
		checkv("half of the cycles are shorter than the median one (RATE sets the median pitch)", (double)below / n, 0.5, 0.012);
		check("cycle-to-cycle jitter is between 0.5 % and 12 % rms", sd > 0.005 && sd < 0.12);
		check("the cycle is shorter on average than its median: the strike law has a tail of early strikes (Gumbel for a minimum)", mean < 1.0 && mean > 0.97);
	}
	// every draw is bounded: the strike cannot be under the release or past the supply
	Strike s(3u);
	bool ok = true;
	for (int i = 0; i < 100000; i++) {
		s.draw(200.0);
		if (!(s.theta > 0.5f && s.theta < 2.0f && s.warp > 0.5f && s.warp < 2.0f)) ok = false;
	}
	check("theta and warp stay within 0.5 .. 2 over 100000 draws", ok);
	// determinism
	Strike a(9u), b(9u);
	bool same = true;
	for (int i = 0; i < 1000; i++) { a.draw(300.0); b.draw(300.0); if (a.theta != b.theta || a.warp != b.warp) same = false; }
	check("the same seed gives the same cycles", same);
	// NEGATIVE CONTROL: with a nearly deterministic junction (e-fold 0.5 mV) the jitter vanishes and the 0.5 % floor test above rejects it
	StrikeTable sharp(0.0005);
	Strike c(1u);
	double sum = 0, sum2 = 0;
	int n = 20000;
	for (int i = 0; i < n; i++) { c.draw(440.0, sharp); sum += c.theta; sum2 += (double)c.theta * c.theta; }
	double sd = std::sqrt(sum2 / n - (sum / n) * (sum / n));
	printf("    near-deterministic junction (0.5 mV e-fold): jitter %.3f %%\n", sd * 100);
	check("NEGATIVE CONTROL: a near-deterministic junction has no jitter, so the jitter test would fail on it", sd < 0.005);
}

static void testSaw() {
	printf("the saw\n");
	// theta = warp = 1: the fixed-threshold saw, bit for bit
	bool same = true;
	for (int i = 0; i < 2000; i++) {
		float ph = (float)i / 2000.f, dt = 0.01f;
		auto pb = [&](float t_) { if (t_ < dt) { float x = t_ / dt; return x + x - x * x - 1.f; } if (t_ > 1.f - dt) { float x = (t_ - 1.f) / dt; return x * x + x + x + 1.f; } return 0.f; };
		float want = 2.f * (float)charge(ph) - 1.f;
		want -= pb(ph);
		if (saw(ph, dt) != want) same = false;
	}
	check("with theta = warp = 1 the saw is the old fixed-threshold saw, bit for bit", same);
	// a longer cycle that struck higher peaks higher, a shorter one lower
	float hi = saw(1.1f - 0.001f, 0.001f, 1.1f, 1.1f), lo = saw(0.9f - 0.001f, 0.001f, 0.9f, 0.9f), mid = saw(1.f - 0.001f, 0.001f);
	check("a cycle that strikes later peaks higher and one that strikes earlier lower", hi > mid && mid > lo);
	// the reset step is band-limited: the sample straddling the wrap is between the two levels
	float before = saw(1.f - 0.0005f, 0.001f, 1.f, 1.f), after = saw(0.0005f, 0.001f, 1.f, 1.f);
	check("the step at the reset is smoothed (straddling samples lie between the two levels)", before < 1.f && after > -1.f);
}

static void testTl072() {
	printf("the TL072 stage\n");
	const double fs = 192000.0;
	auto gainAt = [&](double f, double amp) {
		Tl072Stage s;
		s.setRate(fs);
		double peak = 0;
		int n = (int)(fs * 2);
		for (int i = 0; i < n; i++) {
			double y = s.run(amp * std::sin(2 * 3.14159265358979323846 * f * i / fs));
			if (i > n / 2 && std::fabs(y) > peak) peak = std::fabs(y);
		}
		return peak / amp;
	};
	// The schematic: N -R4 100k- P2 -C3 1u- POS, R7 1k from POS to ground, then x221. Passband gain 221 x 1k / 101k.
	const double want = 221.0 * 1e3 / 101e3;
	checkv("gain in the passband is 221 x R7 / (R4 + R7) = 2.19 (within 1 %), at 1 kHz", gainAt(1000.0, 1e-3), want, 0.022);
	checkv("and at 100 Hz", gainAt(100.0, 1e-3), want, 0.03);
	double g0 = gainAt(1000.0, 1e-3);
	checkv("C3 into R4 + R7 high-passes at 1.58 Hz: 3 dB down there", 20 * std::log10(gainAt(1.576, 1e-3) / g0), -3.0, 0.5);
	checkv("the part's 3 MHz gain-bandwidth puts the pole at 13.6 kHz: 3 dB down", 20 * std::log10(gainAt(13575.0, 1e-3) / g0), -3.0, 0.5);
	// the oscillator's 0.9 V swing comes out at about 2 V peak to peak: well inside the 10.5 V rails
	Tl072Stage s;
	s.setRate(fs);
	double hiV = -1e9, loV = 1e9;
	for (int i = 0; i < (int)fs; i++) {
		double ph = std::fmod(i * 440.0 / fs, 1.0);
		double y = s.run(7.3 + 0.9 * charge(ph) + kVLed);
		if (i > (int)fs / 2) { hiV = std::max(hiV, y); loV = std::min(loV, y); }
	}
	printf("    0.9 V saw at 440 Hz through R4/C3/R7 and x221: output %.2f .. %.2f V\n", loV, hiV);
	check("the 0.9 V swing comes out near 2 V peak to peak (1.7 .. 2.2), centred", hiV - loV > 1.7 && hiV - loV < 2.2 && std::fabs(hiV + loV) < 0.6);
	check("and nowhere near the rails", hiV < 3.0 && loV > -3.0);
	// NEGATIVE CONTROL: the stage with a tenth of the gain fails the gain checks
	check("NEGATIVE CONTROL: a tenth of the gain is rejected by the gain check", std::fabs(g0 / 10.0 - want) > 0.022);
	check("NEGATIVE CONTROL: the earlier reading, a bare 221 (no R7 divider), is rejected too", std::fabs(221.0 - want) > 0.022);
}

int main() {
	testTable();
	testDistribution();
	testStrike();
	testSaw();
	testTl072();
	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}

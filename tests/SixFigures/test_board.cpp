// The reverse-avalanche VCO as the board (AvalancheBoard.hpp): the frequency law derived from the
// schematic's RC, checked against an INDEPENDENT stepped simulation of the same circuit (its own
// Euler-free exponential charge, its own hazard calibration by summation, none of the table's code);
// the CV path (R8 330, R9 100k pot, LED3, LED2 -> the vactrol's LDR); the output network (R4, C3,
// R7, the TL072 x221); and the default core staying bit for bit what it was. Negative controls break
// each in turn.

#include "../../src/SixFigures/AvalancheBoard.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace sixfigures::avalanche;
using namespace sixfigures::avalanche::board;

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
static double median(std::vector<double> v) { std::sort(v.begin(), v.end()); return v[v.size() / 2]; }

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

// ---- the independent simulation ------------------------------------------------------------------
// A capacitor charging through R toward Vs, h seconds a step; each step the junction strikes with
// probability 1 - exp(-r(V) h), r(V) = r0 exp((V - 8.2) / ef). r0 is calibrated HERE by summing the
// hazard along the deterministic 1 s charge to 8.2 V (median at the measured 1 k x 1 mF), no table.
static double calibrate(double ef) {
	const double vs = source(), rc = 1.0, h = 1e-5;
	double v = kVRelease, sum = 0.0;
	while (v < kVStrike) {
		v = vs - (vs - v) * std::exp(-h / rc);
		sum += std::exp((v - kVStrike) / ef) * h;
	}
	return std::log(2.0) / sum;
}
// returns the cycle lengths, seconds, of `cycles` cycles at time constant rc
static std::vector<double> simulateCycles(double rc, double r0, double ef, int cycles, uint32_t seed) {
	microplasma::Rng rng(seed);
	const double vs = source(), h = rc / 4000.0, a = std::exp(-h / rc);
	std::vector<double> out;
	for (int c = 0; c < cycles; c++) {
		double v = kVRelease, t = 0.0;
		for (long i = 0; i < 100000000L; i++) {
			v = vs - (vs - v) * a;
			t += h;
			if (rng.uniform() < -std::expm1(-r0 * std::exp((v - kVStrike) / ef) * h)) break;
		}
		out.push_back(t);
	}
	return out;
}

// ---- driving a Board --------------------------------------------------------------------------------
struct Run { std::vector<double> periods, strikes; double hzSeen; };
static Run runBoard(Board& b, double fs, double rate, bool lfo, double cv, double amt, bool led3, int cycles, double settle = 0.0) {
	b.setRate(fs);
	Run r;
	long n = 0;
	for (long i = 0; i < 400000000L && (int)r.periods.size() < cycles; i++, n++) {
		b.process(rate, lfo, cv, amt, led3);
		if (b.wrapped) {
			if (i / fs >= settle) { r.periods.push_back(b.lastPeriod); r.strikes.push_back(b.lastStrike); }
		}
	}
	r.hzSeen = b.hz;
	return r;
}
static double circuitR(double rate, double rLdr) {
	double r2 = kR2 * (1.0 - rate);
	return kR1 + (r2 > 0 ? r2 * rLdr / (r2 + rLdr) : 0.0);
}

// ---- the tests --------------------------------------------------------------------------------------
static void testCvPath() {
	printf("the CV path: R8 330, R9 100k, LED3, LED2\n");
	// Independent solution: the wiper node by bisection on Kirchhoff's current law, LED law written out here.
	auto vf = [](double i) { const double is = 0.020 / std::expm1(1.63 / 0.0501); return 0.0501 * std::log1p(i / is) + i * 1.0; };
	auto solve = [&](double vcv, double a, int leds) {
		const double rt = kR8 + (1 - a) * kR9, rb = a * kR9;
		double lo = 0, hi = vcv;                        // wiper voltage
		for (int k = 0; k < 200; k++) {
			double vw = 0.5 * (lo + hi);
			double lo2 = 0, hi2 = 1.0;                  // LED current at that wiper voltage
			for (int q = 0; q < 200; q++) { double m = 0.5 * (lo2 + hi2); (leds * vf(m) < vw ? lo2 : hi2) = m; }
			double iled = 0.5 * (lo2 + hi2);
			double kcl = (vcv - vw) / rt - vw / (rb > 0 ? rb : 1e-9) - iled;     // current in from the top - out
			(kcl > 0 ? lo : hi) = vw;
		}
		double vw = 0.5 * (lo + hi);
		double lo2 = 0, hi2 = 1.0;
		for (int q = 0; q < 200; q++) { double m = 0.5 * (lo2 + hi2); (leds * vf(m) < vw ? lo2 : hi2) = m; }
		return 0.5 * (lo2 + hi2);
	};
	for (double cv : { 2.0, 5.0, 10.0 })
		for (double a : { 0.3, 0.7, 1.0 })
			for (int l3 = 0; l3 < 2; l3++) {
				double want = solve(cv, a, 1 + l3), got = cvLedCurrent(cv, a, l3 != 0);
				checks++;
				if (!(std::fabs(got - want) <= 0.005 * want + 2e-8)) {
					failures++;
					printf("  FAIL  CV %.0f V, R9 %.1f, LED3 %d: %.5g A, want %.5g A\n", cv, a, l3, got, want);
				}
			}
	checkv("10 V, R9 full: (10 V - 1.66 V) / (330 + 1 ohm) = 25.2 mA through the vactrol LED (under the VTL5C3's 40 mA limit)", cvLedCurrent(10, 1.0, false) * 1e3, 25.2, 0.4);
	check("fitting LED3 costs a second diode drop: less current", cvLedCurrent(10, 1.0, true) < cvLedCurrent(10, 1.0, false) - 4e-3);
	check("R9 at ground: no current at any CV", cvLedCurrent(10, 0.0, false) == 0.0);
	check("negative CV: no current", cvLedCurrent(-5, 1.0, false) == 0.0 && cvLedCurrent(0, 1.0, false) == 0.0);
	check("R9 mid-travel with 5 V: a few microamps at most (the 100k pot starves the LED)", cvLedCurrent(5, 0.5, false) < 50e-6);
	bool mono = true;
	for (double cv = 0.5; cv < 10; cv += 0.5) for (double a = 0.1; a < 1.0; a += 0.1) if (cvLedCurrent(cv, a + 0.05, false) < cvLedCurrent(cv, a, false) - 1e-15 || cvLedCurrent(cv + 0.25, a, false) < cvLedCurrent(cv, a, false) - 1e-15) mono = false;
	check("the current rises with CV and with R9", mono);
}

static void testLaw() {
	printf("the frequency law against an independent simulation\n");
	const double ef = kStrikeEfold, r0sim = calibrate(ef);
	printf("    r0 by summation %.5g /s, by the table %.5g /s\n", r0sim, defaultTable().r0);
	checkv("the independent calibration agrees with the table's r0 (0.5 %)", r0sim / defaultTable().r0, 1.0, 0.005);
	struct Case { double rate; bool lfo; };
	const Case cases[] = { {1.0, false}, {0.5, false}, {0.0, false}, {1.0, true}, {0.5, true}, {0.0, true} };
	for (const Case& c : cases) {
		const double rldr = 1.0 / vactrol::kDarkConductance, R = circuitR(c.rate, rldr), C = c.lfo ? kC1 : kC2, rc = R * C;
		StrikeTable t;
		const double tClosed = timeTo(t.median(rc), rc);
		const double tOld = rc * -std::log(q());                      // the fixed-threshold law of the default core
		const int N = 2500;
		std::vector<double> sim = simulateCycles(rc, r0sim, ef, N, 11u + (uint32_t)(c.rate * 10) + (c.lfo ? 100 : 0));
		Board b(31u + (uint32_t)(c.rate * 10));
		Run run = runBoard(b, 48000.0, c.rate, c.lfo, 0.0, 0.0, false, N);
		const double tSim = median(sim), tBoard = median(run.periods);
		printf("    R2 %5.0f ohm, C %s: R %.0f ohm, RC %.3g s; median period: closed form %.4g, simulation %.4g, board %.4g s => %.2f Hz (fixed-threshold law %.2f Hz)\n",
		       kR2 * (1 - c.rate), c.lfo ? "10u" : " 1u", R, rc, tClosed, tSim, tBoard, 1.0 / tBoard, 1.0 / tOld);
		checkv("the integrator's median period matches the closed form (0.7 %)", tBoard / tClosed, 1.0, 0.007);
		checkv("the independent simulation matches the closed form (0.8 %)", tSim / tClosed, 1.0, 0.008);
		checkv("and the board matches the simulation (1.2 %)", tBoard / tSim, 1.0, 0.012);
		checkv("the displayed median frequency is the closed form", run.hzSeen, 1.0 / tClosed, 1.0 / tClosed * 0.002);
		// jitter
		double mean = 0, m2 = 0;
		for (double p : run.periods) mean += p;
		mean /= run.periods.size();
		for (double p : run.periods) m2 += (p - mean) * (p - mean);
		double jit = std::sqrt(m2 / run.periods.size()) / mean;
		check("cycle-to-cycle jitter is 0.5 .. 12 % rms", jit > 0.005 && jit < 0.12);
		// NEGATIVE CONTROLS on the audio-top case, where the laws differ most
		if (c.rate == 1.0 && !c.lfo) {
			check("the fixed-threshold law is more than 8 % off the simulation here (the old frequency law is not the circuit's)", std::fabs(tOld / tSim - 1.0) > 0.08);
			const double tNoR1 = timeTo(t.median((R - kR1) * C), (R - kR1) * C);
			check("NEGATIVE CONTROL: a board without R1 is rejected by the simulation check", std::fabs(tNoR1 / tSim - 1.0) > 0.012);
			Board w(31u);
			w.setEfold(5.0 * ef);
			Run wr = runBoard(w, 48000.0, c.rate, c.lfo, 0.0, 0.0, false, 800);
			double dw = ks(wr.strikes, [&](double v) { return t.cdf(v, rc); });
			check("NEGATIVE CONTROL: a junction with five times the e-fold is rejected by the strike-distribution test", dw > 1.95 / std::sqrt(800.0));
		}
	}
	// the strike voltage distribution the board draws, against the table's CDF (a different code path)
	for (const Case& c : { Case{1.0, false}, Case{0.0, true} }) {
		const double R = circuitR(c.rate, 1.0 / vactrol::kDarkConductance), C = c.lfo ? kC1 : kC2, rc = R * C;
		StrikeTable t;
		Board b(77u);
		Run run = runBoard(b, 48000.0, c.rate, c.lfo, 0.0, 0.0, false, 1500);
		double d = ks(run.strikes, [&](double v) { return t.cdf(v, rc); });
		printf("    strike voltages at RC %.3g s: median %.4f V (table %.4f V), KS %.4f\n", rc, median(run.strikes), t.median(rc), d);
		check("the board's strike voltages follow the table's CDF (KS, 0.1 % level)", d < 1.95 / std::sqrt(1500.0));
	}
}

static void testSampleRate() {
	printf("sample-rate independence (the strike is found to a fraction of a sample)\n");
	double t48 = 0, t192 = 0, t44 = 0;
	for (double fs : { 44100.0, 48000.0, 192000.0 }) {
		Board b(5u);
		Run r = runBoard(b, fs, 1.0, false, 0.0, 0.0, false, 3000);
		double m = median(r.periods);
		printf("    %.0f Hz: median period %.5g s\n", fs, m);
		(fs == 48000.0 ? t48 : fs == 192000.0 ? t192 : t44) = m;
	}
	checkv("44.1 kHz against 192 kHz within 0.6 % (statistical error of the median is 0.1 %)", t44 / t192, 1.0, 0.006);
	checkv("48 kHz against 192 kHz within 0.6 %", t48 / t192, 1.0, 0.006);
}

static void testSpans() {
	printf("the frequency spans (median frequency, no CV, LDR dark)\n");
	const double rldr = 1.0 / vactrol::kDarkConductance;
	double fa[2], fl[2];
	for (int lfo = 0; lfo < 2; lfo++)
		for (int e = 0; e < 2; e++) {
			double R = circuitR(e ? 0.0 : 1.0, rldr), rc = R * (lfo ? kC1 : kC2);
			StrikeTable t;
			(lfo ? fl : fa)[e] = 1.0 / timeTo(t.median(rc), rc);
		}
	printf("    audio (C2 1 uF):  %.1f Hz (RATE 0, R2 10k)  to  %.1f Hz (RATE 1, R2 0)\n", fa[1], fa[0]);
	printf("    LFO   (C1 10 uF): %.2f Hz (RATE 0, R2 10k)  to  %.2f Hz (RATE 1, R2 0)\n", fl[1], fl[0]);
	check("the audio span is about 218 Hz to 2.26 kHz", fa[1] > 205 && fa[1] < 235 && fa[0] > 2150 && fa[0] < 2380);
	check("the LFO span is about 23 Hz to 239 Hz", fl[1] > 21.5 && fl[1] < 24.8 && fl[0] > 225 && fl[0] < 255);
	check("the two ranges are a little under a decade apart (10 uF / 1 uF; the junction strikes later at the faster charge, so the ratio is 9.45)", fa[0] / fl[0] > 9.0 && fa[0] / fl[0] < 10.0);
	checkv("the pot spans 11:1 (R = 1k..11k)", fa[0] / fa[1], 11.0, 0.8);
}

static void testCv() {
	printf("CV through the vactrol\n");
	// steady state at full CV: R3 ~ 1.7k..2k in parallel with the pot
	{
		Board b(9u);
		b.setRate(48000.0);
		for (long i = 0; i < 48000 * 2; i++) b.process(0.0, false, 10.0, 1.0, false);
		const double i = cvLedCurrent(10.0, 1.0, false);
		const double rldr = vactrol::steadyResistance(i);
		const double want = circuitR(0.0, rldr);
		printf("    10 V, R9 full, pot at 10k: LED %.1f mA, LDR %.0f ohm, R %.0f ohm (board says %.0f)\n", i * 1e3, rldr, want, b.rTotal);
		checkv("the charging resistance is R1 + R2 || the settled LDR", b.rTotal / want, 1.0, 0.01);
		Board d(9u);
		Run rd = runBoard(d, 48000.0, 0.0, false, 0.0, 1.0, false, 1500);
		Run rc = runBoard(b, 48000.0, 0.0, false, 10.0, 1.0, false, 1500, 0.5);
		printf("    audio, pot at 10k: %.0f Hz with no CV, %.0f Hz with 10 V\n", 1.0 / median(rd.periods), 1.0 / median(rc.periods));
		check("10 V of CV takes the pot-at-10k audio pitch up about 4.5 times (R 11k -> ~2.5k)", 1.0 / median(rc.periods) > 3.8 * (1.0 / median(rd.periods)) && 1.0 / median(rc.periods) < 5.3 * (1.0 / median(rd.periods)));
		// NEGATIVE CONTROL: R9 at ground ignores CV
		Board n(9u);
		Run rn = runBoard(n, 48000.0, 0.0, false, 10.0, 0.0, false, 800, 0.5);
		check("NEGATIVE CONTROL: with R9 at ground the CV does nothing, and the test above would have failed", 1.0 / median(rn.periods) < 1.3 * (1.0 / median(rd.periods)));
	}
	// the vactrol's memory: the pitch follows CV with the cell's lag
	{
		Board b(3u);
		b.setRate(48000.0);
		for (long i = 0; i < 48000; i++) b.process(0.0, false, 0.0, 1.0, false);
		const double r0 = b.rTotal;
		double rAt1ms = 0, rAt5ms = 0, rAt100ms = 0;
		for (long i = 0; i < 48000; i++) {
			b.process(0.0, false, 10.0, 1.0, false);
			if (i == 48) rAt1ms = b.rTotal;
			if (i == 240) rAt5ms = b.rTotal;
			if (i == 4800) rAt100ms = b.rTotal;
		}
		printf("    CV step to 10 V: R %.0f ohm -> %.0f after 1 ms -> %.0f after 5 ms -> %.0f after 100 ms -> %.0f after 1 s\n", r0, rAt1ms, rAt5ms, rAt100ms, b.rTotal);
		check("the resistance falls over milliseconds, not at once", rAt1ms < r0 && rAt1ms > b.rTotal * 1.3);
		check("and has settled by 100 ms", rAt100ms <= rAt5ms && std::fabs(b.rTotal - rAt100ms) < 0.05 * rAt100ms);
	}
}

static void testOutputs() {
	printf("the TL072 and P2/N taps\n");
	const double fs = 192000.0;
	auto gainAt = [&](double f) {
		Tl072Stage s;
		s.setRate(fs);
		double peak = 0;
		int n = (int)(fs * 2);
		for (int i = 0; i < n; i++) {
			double y = s.run(1e-3 * std::sin(2 * 3.14159265358979323846 * f * i / fs));
			if (i > n / 2 && std::fabs(y) > peak) peak = std::fabs(y);
		}
		return peak / 1e-3;
	};
	const double want = 221.0 * 1e3 / 101e3;
	const double g0 = gainAt(1000.0);
	checkv("passband gain is 221 x R7 / (R4 + R7) = 2.19, not 221", g0, want, 0.03);
	checkv("and at 100 Hz", gainAt(100.0), want, 0.04);
	checkv("C3 + (R4 + R7) high-passes at 1.58 Hz: 3 dB down there", 20 * std::log10(gainAt(1.576) / g0), -3.0, 0.5);
	checkv("the 3 MHz gain-bandwidth puts the pole at 13.6 kHz: 3 dB down", 20 * std::log10(gainAt(13575.0) / g0), -3.0, 0.5);
	check("NEGATIVE CONTROL: the earlier (C3 into R4, no R7) reading gives a gain of 221, which the check above rejects", std::fabs(221.0 - want) > 0.03);
	// the board running at 440 Hz: P3, P2, N
	Board b(21u);
	b.setRate(48000.0);
	const double fsb = 48000.0;
	// pot such that ~440 Hz at audio: find a rate giving R ~ 4.5k
	double hiP3 = -1e9, loP3 = 1e9, hiP2 = -1e9, loP2 = 1e9, sumN = 0, sumP2 = 0, hiN = -1e9, loN = 1e9;
	long n = 0;
	for (long i = 0; i < (long)(fsb * 3); i++) {
		Board::Out o = b.process(0.7, false, 0.0, 0.0, false);
		if (i > (long)fsb * 2) { hiP3 = std::max(hiP3, o.p3); loP3 = std::min(loP3, o.p3); hiP2 = std::max(hiP2, o.p2); loP2 = std::min(loP2, o.p2); sumN += o.node; sumP2 += o.p2; hiN = std::max(hiN, o.node); loN = std::min(loN, o.node); n++; }
	}
	printf("    RATE 0.7, audio (%.0f Hz): N %.2f..%.2f V; P2 %.3f..%.3f V (mean %.3f); P3 %.2f..%.2f V\n", b.hz, loN, hiN, loP2, hiP2, sumP2 / n, loP3, hiP3);
	check("N is the capacitor plus LED1: it sits between 9.1 V and about 10.2 V", loN > 9.0 && hiN < 10.3);
	check("P3 is a ~2 V peak-to-peak signal about zero: it does not clip", (hiP3 - loP3) > 1.4 && (hiP3 - loP3) < 3.0 && hiP3 < 2.0 && loP3 > -2.0);
	check("P2 carries N's DC level (C3 blocks R7) and about 1 % of its swing", std::fabs(sumP2 / n - sumN / n) < 0.1 && (hiP2 - loP2) < 0.05 * (hiN - loN) + 0.02);
	check("P3 has no DC (the HPF)", std::fabs(0.5 * (hiP3 + loP3)) < 0.5);
}

static void testSaw() {
	printf("the saw tap\n");
	Board b(17u);
	b.setRate(48000.0);
	double hi = -9, lo = 9;
	bool smooth = true;
	double prev = 0;
	for (long i = 0; i < 48000; i++) {
		Board::Out o = b.process(1.0, false, 0.0, 0.0, false);
		if (i > 100) {
			hi = std::max(hi, o.saw); lo = std::min(lo, o.saw);
		}
		prev = o.saw;
	}
	(void)smooth; (void)prev;
	printf("    saw runs %.3f .. %.3f (the default core's runs -1 .. +1 at the nominal strike)\n", lo, hi);
	check("the saw starts near the release voltage (-1, a sample's charge above it) and peaks at the strike: at this pitch the median strike is 8.34 V, so above the nominal +1", lo < -0.7 && lo > -1.3 && hi > 0.9 && hi < 1.6);
	// BLEP: samples straddling a strike lie between the levels; the naive vc would not
	Board c(17u);
	c.setRate(48000.0);
	long straddle = 0, ok = 0;
	double last = -1;
	for (long i = 0; i < 48000; i++) {
		Board::Out o = c.process(1.0, false, 0.0, 0.0, false);
		if (i > 100 && last > 0.7 && o.saw < 0.5 && o.saw > -0.9) { ok++; }
		if (i > 100 && last > 0.7 && o.saw < -0.9) straddle++;
		last = o.saw;
	}
	printf("    resets: %ld land between the levels, %ld on the low level\n", ok, straddle);
	check("at least a third of the resets land between the two levels (band-limited)", ok > 0 && ok * 3 > (ok + straddle));
}

// the default core, unchanged (the regression). The reference sums were taken from the committed
// Avalanche.hpp (8f1ee42) before this option existed.
static void testDefaultRegression() {
	printf("the default avalanche core is unchanged\n");
	struct Ref { double hz, sum, sum2; int wraps; };
	const Ref refs[] = {
		{0.5,    8117.741695, 69406.079311, 2},
		{40.0,  24593.238559, 77051.346319, 167},
		{440.0, 37260.068325, 86786.378635, 1841},
		{3000.0, 47017.849676, 84476.730641, 12560},
	};
	for (const Ref& r : refs) {
		Strike s(0x57121CEu);
		float phase = 0, dt = (float)(r.hz / 48000.0);
		double a = 0, b2 = 0;
		int wraps = 0;
		for (int i = 0; i < 200000; i++) {
			phase += dt;
			if (phase >= s.theta) { phase -= s.theta; s.draw(r.hz); wraps++; }
			float y = saw(phase, dt, s.theta, s.warp);
			a += y; b2 += (double)y * y;
		}
		checkv("sum of the saw over 200000 samples is the reference", a, r.sum, std::fabs(r.sum) * 1e-6);
		checkv("sum of squares too", b2, r.sum2, r.sum2 * 1e-6);
		check("and the number of cycles", wraps == r.wraps);
	}
	// NEGATIVE CONTROL: change the strike e-fold and the reference no longer holds
	StrikeTable other(0.04);
	Strike s(0x57121CEu);
	float phase = 0, dt = (float)(440.0 / 48000.0);
	double a = 0;
	for (int i = 0; i < 200000; i++) {
		phase += dt;
		if (phase >= s.theta) { phase -= s.theta; s.draw(440.0, other); }
		a += saw(phase, dt, s.theta, s.warp);
	}
	check("NEGATIVE CONTROL: a different strike law moves the sum off the reference", std::fabs(a - 37260.068325) > 37260.068325 * 1e-6);
}

int main() {
	testCvPath();
	testLaw();
	testSampleRate();
	testSpans();
	testCv();
	testOutputs();
	testSaw();
	testDefaultRegression();
	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}

// The microplasma noise model (src/Microplasma.hpp) and its use in the Percussive Noise Voice /
// Tiny Dazzler noise circuit (src/Kickback/AvalancheNoise.hpp): statistical tests.
//
// Real avalanche noise is not Gaussian. A junction in breakdown carries a sum of random
// telegraph pulses (Haitz 1964), so the tests are the statistics of that process, each against
// a closed form: exponential dwell times (Kolmogorov-Smirnov), Lorentzian spectra, multi-level
// non-Gaussian amplitudes, exponential bias dependence of the rates; then the circuit's own
// consistency (KCL with a plasma on, the stepper's transition counts against the integral of
// its own rates) and the source's output statistics. Every claim has a negative control: a
// deliberately wrong model that the same test must reject.

#include "../../src/Kickback/AvalancheNoise.hpp"
#include "../../src/Microplasma.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <vector>

using namespace microplasma;
using kickback::AvalancheNoise;
using kickback::AvalancheSource;

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

// ---- helpers -------------------------------------------------------------------------------

// Kolmogorov-Smirnov distance of `t` (dwell times in seconds, sampled with step h) from an
// exponential distribution of rate `r`. Dwell times from a stepped process are geometric (they
// tie on multiples of h, which alone would put a step of r h / 2 into the distance), so each is
// dithered by a uniform fraction of a step first: that makes them continuous and, to O((r h)^2),
// exactly exponential if the process is.
static double ksExp(std::vector<double> t, double r, double h) {
	Rng dither(12345u);
	for (double& x : t) x -= h * dither.uniform();
	std::sort(t.begin(), t.end());
	double d = 0, n = (double)t.size();
	for (size_t i = 0; i < t.size(); i++) {
		double f = 1.0 - std::exp(-r * t[i]);
		d = std::max(d, std::max(std::fabs(f - (double)i / n), std::fabs(f - (double)(i + 1) / n)));
	}
	return d;
}
static double mean(const std::vector<double>& v) { double s = 0; for (double x : v) s += x; return s / v.size(); }
static double cv(const std::vector<double>& v) {
	double m = mean(v), s = 0;
	for (double x : v) s += (x - m) * (x - m);
	return std::sqrt(s / v.size()) / m;
}

struct Dwell { std::vector<double> on, off; };

// run a constant-rate switch: junction voltage and current are fixed, so the rates are
template <class StepFn>
static Dwell runTelegraph(StepFn step, long n, double h, std::vector<float>* trace = nullptr) {
	Dwell d;
	bool st = false;
	long run = 0;
	for (long i = 0; i < n; i++) {
		bool now = step();
		if (trace) trace->push_back(now ? 1.f : 0.f);
		if (now == st) run++;
		else {
			(st ? d.on : d.off).push_back(run * h);
			st = now; run = 1;
		}
	}
	return d;
}

// one-sided PSD of x (sampled at fs) at f by Welch averaging of Hann-windowed segments, in
// units^2/Hz
static double psd(const std::vector<float>& x, double fs, double f, int L = 8192) {
	int segs = (int)(x.size() / L);
	double acc = 0, wsum = 0;
	for (int i = 0; i < L; i++) { double w = 0.5 - 0.5 * std::cos(2 * M_PI * i / L); wsum += w * w; }
	for (int s = 0; s < segs; s++) {
		double mu = 0;
		for (int i = 0; i < L; i++) mu += x[s * L + i];
		mu /= L;
		double re = 0, im = 0;
		for (int i = 0; i < L; i++) {
			double w = 0.5 - 0.5 * std::cos(2 * M_PI * i / L), ph = 2 * M_PI * f * i / fs;
			double v = (x[s * L + i] - mu) * w;
			re += v * std::cos(ph); im += v * std::sin(ph);
		}
		acc += re * re + im * im;
	}
	return 2.0 * acc / segs / (fs * wsum);
}

static void moments(const std::vector<float>& x, double& rms, double& skew, double& kurt) {
	double m = 0, m2 = 0, m3 = 0, m4 = 0;
	for (float v : x) m += v;
	m /= x.size();
	for (float v : x) { double d = v - m; m2 += d * d; m3 += d * d * d; m4 += d * d * d * d; }
	m2 /= x.size(); m3 /= x.size(); m4 /= x.size();
	rms = std::sqrt(m2); skew = m3 / std::pow(m2, 1.5); kurt = m4 / (m2 * m2);
}

// ---- the telegraph process ----------------------------------------------------------------

static void testDwell() {
	printf("dwell times\n");
	Params p;                              // defaults: vb 7.2, nuOn 20/s at vb, e-fold 30 mV
	const double vj = p.vb + 0.06;         // 60 mV over: turn-on 20 e^2 = 147.8 /s
	const double iOn = 2e-6;               // turn-off 2e4 e^-2 = 2707 /s
	const double r01 = turnOn(p, vj), r10 = turnOff(p, iOn), h = 1.0 / 192000.0;
	checkv("turnOn(vb) is nuOn", turnOn(p, p.vb), p.nuOn, 1e-9);
	checkv("turnOn rises e-fold per vs", turnOn(p, p.vb + p.vs) / turnOn(p, p.vb), std::exp(1.0), 1e-9);
	checkv("turnOff(0) is nuOff", turnOff(p, 0.0), p.nuOff, 1e-9);
	checkv("turnOff falls e-fold per iq", turnOff(p, 0.0) / turnOff(p, p.iq), std::exp(1.0), 1e-9);
	checkv("onCurrent is the overvoltage over Rs", onCurrent(p, p.vb + 0.1), 0.1 / p.rs, 1e-12);
	check("and zero under vb", onCurrent(p, p.vb - 0.1) == 0.0);

	Rng rng(2024u);
	Switch sw;
	Dwell d = runTelegraph([&]() { return sw.step(p, vj, iOn, h, rng); }, 192000L * 400, h);
	printf("    %zu on pulses, %zu off gaps; mean on %.1f us (want %.1f), mean off %.2f ms (want %.2f)\n",
	       d.on.size(), d.off.size(), mean(d.on) * 1e6, 1e6 / r10, mean(d.off) * 1e3, 1e3 / r01);
	check("enough pulses to say something", d.on.size() > 5000 && d.off.size() > 5000);
	checkv("mean ON time is 1 / turnOff within 3 %", mean(d.on) * r10, 1.0, 0.03);
	checkv("mean OFF time is 1 / turnOn within 3 %", mean(d.off) * r01, 1.0, 0.03);
	double kOn = ksExp(d.on, r10, h), kOff = ksExp(d.off, r01, h);
	double crit = 1.95 / std::sqrt((double)d.on.size());          // KS 0.1 % level is 1.95 / sqrt(n)
	printf("    KS distance to the exponential: on %.4f, off %.4f (0.1 %% critical %.4f)\n", kOn, kOff, crit);
	check("ON dwell times are exponentially distributed (KS, 0.1 % level)", kOn < crit);
	check("OFF dwell times are exponentially distributed (KS, 0.1 % level)", kOff < 1.95 / std::sqrt((double)d.off.size()));
	checkv("coefficient of variation of the ON times is 1", cv(d.on), 1.0, 0.04);
	checkv("coefficient of variation of the OFF times is 1", cv(d.off), 1.0, 0.04);

	// bias dependence: 60 mV more over-voltage multiplies the turn-on rate by e^2 and the mean OFF time divides by it
	Rng rng2(7u);
	Switch sw2;
	double vj2 = vj + 0.06;
	Dwell d2 = runTelegraph([&]() { return sw2.step(p, vj2, iOn, h, rng2); }, 192000L * 100, h);
	checkv("60 mV more over-voltage shortens the mean OFF time by e^2 (within 4 %)", mean(d.off) / mean(d2.off), std::exp(0.06 / p.vs), 0.04 * std::exp(2.0));
	// the current dependence of the quench: carrying more current, the plasma stays on longer
	Rng rng3(9u);
	Switch sw3;
	double iHi = 4e-6;
	Dwell d3 = runTelegraph([&]() { return sw3.step(p, vj, iHi, h, rng3); }, 192000L * 400, h);
	checkv("twice the current lengthens the mean ON time by e^2 (within 4 %)", mean(d3.on) / mean(d.on), std::exp((iHi - iOn) / p.iq), 0.04 * std::exp(2.0));

	// NEGATIVE CONTROL 1: a switch that flips on a fixed timetable (periodic, not Poisson) with the
	// same mean dwell must fail the exponential test and the CV test.
	long nOn = (long)std::lround(1.0 / r10 / h), nOff = (long)std::lround(1.0 / r01 / h), cnt = 0;
	bool st = false;
	Dwell bad = runTelegraph([&]() {
		cnt++;
		if (cnt >= (st ? nOn : nOff)) { st = !st; cnt = 0; }
		return st;
	}, 192000L * 100, h);
	check("NEGATIVE CONTROL: a fixed-timetable switch is rejected by the exponential test",
	      ksExp(bad.on, r10, h) > 1.95 / std::sqrt((double)bad.on.size()));
	check("NEGATIVE CONTROL: and its dwell-time CV is ~0, not 1", cv(bad.on) < 0.05);
	// NEGATIVE CONTROL 2: the right process with the wrong rate (turn-off doubled) is rejected on the mean
	Rng rng4(11u);
	Switch sw4;
	Dwell wrong = runTelegraph([&]() { return sw4.step(p, vj, iOn, h * 2.0, rng4); }, 192000L * 100, h);
	check("NEGATIVE CONTROL: a stepper with the wrong time step fails the mean-dwell check",
	      std::fabs(mean(wrong.on) * r10 - 1.0) > 0.3);
}

static void testSpectrumAndAmplitude() {
	printf("spectrum and amplitude distribution of the pulse train\n");
	Params p;
	const double fs = 48000.0, h = 1.0 / fs;
	// pick rates so the Lorentzian corner is in the audio band: r01 = r10 = 4000/s  -> corner 4 kHz / pi... (r01 + r10) / 2 pi
	p.nuOn = 4000.0; p.nuOff = 4000.0; p.vs = 1.0;
	const double vj = p.vb, iOn = 0.0, di = 1e-6;
	Rng rng(31u);
	Switch sw;
	std::vector<float> x;
	long n = (long)(fs * 120);
	x.reserve(n);
	for (long i = 0; i < n; i++) x.push_back((float)(sw.step(p, vj, iOn, h, rng) ? di : 0.0) * 1e6f);      // microamps
	double r01 = turnOn(p, vj), r10 = turnOff(p, iOn);
	double worst = 0;
	for (double f : { 100., 300., 800., 1500., 3000., 6000., 10000. }) {
		// the stepped process is sampled, so its Lorentzian aliases; the 1-pole discrete model is exact for it
		double a = std::exp(-(r01 + r10) * h), pOn = r01 / (r01 + r10);
		double w = 2 * M_PI * f / fs;
		double var = pOn * (1 - pOn);                                       // microamp^2 (di = 1)
		double want = 2.0 * var * (1 - a * a) / (1 - 2 * a * std::cos(w) + a * a) * h;     // one-sided, discrete AR(1)
		double got = psd(x, fs, f);
		double dB = 10 * std::log10(got / want);
		worst = std::max(worst, std::fabs(dB));
		// and the continuous closed form agrees where f << fs
		if (f <= 800.) {
			double cont = lorentzianPsd(f, 1.0, r01, r10);
			checkv("the continuous Lorentzian agrees with the sampled AR(1) at low f (within 0.5 dB)", 10 * std::log10(want / cont), 0.0, 0.5);
		}
	}
	printf("    worst deviation of the simulated PSD from the Lorentzian / AR(1) closed form: %.2f dB\n", worst);
	check("the pulse train's spectrum is the Lorentzian within 0.6 dB at seven frequencies, 100 Hz to 10 kHz", worst < 0.6);
	// the corner: -3 dB at (r01 + r10) / 2 pi = 1273 Hz
	double fc = (r01 + r10) / (2 * M_PI);
	double lo = psd(x, fs, 50.0), at = psd(x, fs, fc);
	printf("    corner %.0f Hz: PSD there / PSD at 50 Hz = %.2f dB\n", fc, 10 * std::log10(at / lo));
	checkv("the corner is at (r01 + r10) / 2 pi: 3 dB down (within 0.6 dB)", 10 * std::log10(at / lo), -3.0, 0.6);

	// amplitude: a symmetric two-level signal has kurtosis exactly 1 (sub-Gaussian), skewness 0
	double rms, skew, kurt;
	moments(x, rms, skew, kurt);
	printf("    one plasma at 50 %%: kurtosis %.3f, skewness %.3f\n", kurt, skew);
	checkv("two-level amplitude: kurtosis 1.0", kurt, 1.0, 0.02);
	checkv("two-level amplitude: skewness 0", skew, 0.0, 0.03);
	// four independent plasmas of unequal sizes: a 16-level sum, still far from Gaussian only in its skew/kurtosis combination
	std::vector<float> y(n, 0.f);
	Rng r2(5u);
	double size[4] = { 1.0, 2.0, 3.5, 6.0 };
	for (int k = 0; k < 4; k++) {
		Switch s;
		Params q = p; q.nuOn = 1000.0 * (k + 1); q.nuOff = 6000.0 / (k + 1);
		for (long i = 0; i < n; i++) y[i] += (float)(s.step(q, vj, 0.0, h, r2) ? size[k] : 0.0);
	}
	moments(y, rms, skew, kurt);
	printf("    four unequal plasmas: kurtosis %.2f, skewness %.2f\n", kurt, skew);
	check("a sum of four unequal telegraph pulses is non-Gaussian (|skew| > 0.3 or kurtosis outside 2.7..3.3)",
	      std::fabs(skew) > 0.3 || kurt < 2.7 || kurt > 3.3);

	// NEGATIVE CONTROL: Gaussian white noise through the same estimators is kurtosis 3, skew 0, flat spectrum
	// and so is rejected by every one of the tests above.
	Rng g(77u);
	std::vector<float> w(n);
	for (long i = 0; i < n; i++) w[i] = (float)g.gauss();
	moments(w, rms, skew, kurt);
	check("NEGATIVE CONTROL: Gaussian noise has kurtosis ~3 (not 1)", std::fabs(kurt - 3.0) < 0.1);
	double flat = 10 * std::log10(psd(w, fs, 10000.) / psd(w, fs, 100.));
	check("NEGATIVE CONTROL: and a flat spectrum (no Lorentzian corner)", std::fabs(flat) < 0.6);
	check("NEGATIVE CONTROL: so the Lorentzian-corner test would fail on it", std::fabs(10 * std::log10(psd(w, fs, fc) / psd(w, fs, 50.0)) + 3.0) > 1.0);
}

// ---- in the circuit -----------------------------------------------------------------------

static void testCircuit() {
	printf("in the circuit\n");
	// KCL with a plasma forced on: its series resistor and its zener carry the same current, and the
	// base node sees exactly the bulk junction's plus the plasma's.
	{
		AvalancheNoise n;
		n.start(192000.0);
		check("quiet operating point converges", n.ok);
		double vjQuiet = n.junctionVoltage();
		check("the quiet junction rests under the bulk knee (7.0 to 7.6 V)", vjQuiet > 7.0 && vjQuiet < 7.6);
		int k = 1;
		n.sw[k].on = true;
		n.ckt.e[n.plasmaZ[k]].p2 = n.mp[k].vb;
		n.ckt.h = 1.0 / n.rate;
		n.ckt.step(80);
		double iz = -n.ckt.diodeCurrent(n.plasmaZ[k]);
		double ir = (n.ckt.v[AvalancheNoise::A] - n.ckt.v[AvalancheNoise::P0 + k]) / n.mp[k].rs;
		printf("    plasma %d on: %.3f uA through Rs, %.3f uA through its junction; V_j %.4f V (quiet %.4f V)\n", k, ir * 1e6, iz * 1e6, n.junctionVoltage(), vjQuiet);
		checkv("KCL at the plasma's node: Rs current equals its junction's within 1 nA", ir, iz, 1e-9);
		check("and it carries a few tenths to a few microamps", iz > 0.1e-6 && iz < 10e-6);
		check("the circuit pulls the junction down when the plasma conducts (negative feedback through R3 and T2)", n.junctionVoltage() < vjQuiet);
		check("and still converges", n.ok);
	}

	// the stepper's transitions against the integral of its own rates: expected turn-ons = sum over
	// off steps of turnOn(Vj) h, a Poisson count
	{
		AvalancheNoise n;
		n.seed(4242);
		n.start(192000.0);
		const double h = 1.0 / 192000.0;
		double expectOn[AvalancheNoise::kPlasmas] = {}, expectOff[AvalancheNoise::kPlasmas] = {};
		long nOn[AvalancheNoise::kPlasmas] = {}, nOff[AvalancheNoise::kPlasmas] = {};
		bool prev[AvalancheNoise::kPlasmas] = {};
		std::vector<double> dwell2;
		long run = 0;
		bool bad = false;
		for (long i = 0; i < 192000L * 12; i++) {
			double vj = n.junctionVoltage();
			for (int k = 0; k < AvalancheNoise::kPlasmas; k++) {
				if (!n.sw[k].on) expectOn[k] += -std::expm1(-turnOn(n.mp[k], vj) * h);
				else expectOff[k] += -std::expm1(-turnOff(n.mp[k], -n.ckt.diodeCurrent(n.plasmaZ[k])) * h);
			}
			n.step();
			if (!n.ok) bad = true;
			for (int k = 0; k < AvalancheNoise::kPlasmas; k++) {
				if (n.sw[k].on && !prev[k]) nOn[k]++;
				if (!n.sw[k].on && prev[k]) nOff[k]++;
				prev[k] = n.sw[k].on;
			}
			// dwell times of plasma 2 (the busiest)
			if (n.sw[2].on) run++;
			else if (run > 0) { dwell2.push_back(run * h); run = 0; }
		}
		check("the circuit converged on every step of 12 s", !bad);
		for (int k = 0; k < AvalancheNoise::kPlasmas; k++) {
			printf("    plasma %d: %ld turn-ons (expected %.1f), %ld turn-offs (expected %.1f)\n", k, nOn[k], expectOn[k], nOff[k], expectOff[k]);
			double sdOn = std::sqrt(std::max(expectOn[k], 1.0)), sdOff = std::sqrt(std::max(expectOff[k], 1.0));
			check("turn-on count is the integral of the turn-on rate (within 5 sigma)", std::fabs(nOn[k] - expectOn[k]) < 5 * sdOn);
			check("turn-off count is the integral of the turn-off rate (within 5 sigma)", std::fabs(nOff[k] - expectOff[k]) < 5 * sdOff);
		}
		int busy = 0;
		for (int k = 0; k < AvalancheNoise::kPlasmas; k++) if (nOn[k] > 100) busy++;
		check("at least three of the four plasmas are active at the default operating point", busy >= 3);
		// the live dwell times are close to exponential (the rates move a little with the circuit, so CV near 1, not exactly)
		if (dwell2.size() > 1000) {
			printf("    plasma 2 live ON dwell: %zu pulses, mean %.1f us, CV %.2f\n", dwell2.size(), mean(dwell2) * 1e6, cv(dwell2));
			check("a plasma's ON times in the live circuit have a CV of about 1 (0.8 to 1.3)", cv(dwell2) > 0.8 && cv(dwell2) < 1.3);
		}
	}

	// the supply moves the bias, and the pop rate with it
	{
		auto rate = [](double dv) {
			AvalancheNoise n;
			n.seed(1);
			n.vcc = 12.0 + dv;
			n.start(192000.0);
			long cnt = 0;
			bool prev[4] = {};
			for (long i = 0; i < 192000L * 3; i++) {
				n.step();
				for (int k = 0; k < 4; k++) { if (n.sw[k].on && !prev[k]) cnt++; prev[k] = n.sw[k].on; }
			}
			return cnt / 3.0;
		};
		double a = rate(-1.0), b = rate(0.0), c = rate(1.0);
		printf("    turn-ons per second at 11 / 12 / 13 V supply: %.0f %.0f %.0f\n", a, b, c);
		check("the supply moves the pop rate (it changes by more than 5 % across +-1 V)", std::fabs(a - c) > 0.05 * b);
	}
}

// ---- the source ---------------------------------------------------------------------------

static void testSource() {
	printf("the source\n");
	const double fs = 48000.0;
	auto grab = [&](AvalancheSource& s, double sec) {
		std::vector<float> x;
		for (int i = 0; i < (int)(fs / 2); i++) s.next();
		for (int i = 0; i < (int)(fs * sec); i++) x.push_back(s.next());
		return x;
	};
	double rms, skew, kurt;
	// snare setting (C5 0.1 uF)
	AvalancheSource snare(0x5EAF00Du, AvalancheNoise::PERCUSSIVE, true, 0.1e-6);
	snare.setRate(fs);
	std::vector<float> xs = grab(snare, 20);
	moments(xs, rms, skew, kurt);
	printf("    snare (C5 0.1 uF): rms %.4f, skew %.2f, kurtosis %.2f\n", rms, skew, kurt);
	check("the source keeps the level of the white noise it replaced (rms 0.577 at 48 kHz, within 3 %)", std::fabs(rms / 0.57735 - 1.0) < 0.03);
	check("it has no DC (mean under 2 % of the rms)", [&]() { double m = 0; for (float v : xs) m += v; return std::fabs(m / xs.size()) < 0.02 * rms; }());
	// hi-hat setting (C5 1 nF): the bandwidth reaches the pulses
	AvalancheSource hat(0x5EAF00Du, AvalancheNoise::PERCUSSIVE, true, 1e-9);
	hat.setRate(fs);
	std::vector<float> xh = grab(hat, 20);
	moments(xh, rms, skew, kurt);
	printf("    hi-hat (C5 1 nF): rms %.4f, skew %.2f, kurtosis %.2f\n", rms, skew, kurt);
	check("hi-hat setting: the output is strongly non-Gaussian (kurtosis above 4.5)", kurt > 4.5);
	check("hi-hat setting: and skewed (|skew| above 0.5): bursts, not a symmetric hiss", std::fabs(skew) > 0.5);
	// its spectrum is not the flat hiss: more LF than HF power, by a margin the Lorentzian sum predicts (at least 10 dB 500 Hz to 16 kHz)
	double l = psd(xh, fs, 500.0, 4096), hi = psd(xh, fs, 16000.0, 4096);
	printf("    hi-hat PSD 500 Hz vs 16 kHz: %.1f dB\n", 10 * std::log10(l / hi));
	check("hi-hat setting: spectrum falls 10 dB or more from 500 Hz to 16 kHz (Lorentzian sum, not white)", 10 * std::log10(l / hi) > 10.0);

	// NEGATIVE CONTROL: the shot-noise-only model is Gaussian with a flat hi-hat spectrum
	AvalancheSource shot(0x5EAF00Du, AvalancheNoise::PERCUSSIVE, false, 1e-9);
	shot.setRate(fs);
	std::vector<float> xg = grab(shot, 20);
	moments(xg, rms, skew, kurt);
	printf("    shot-noise-only (hi-hat): skew %.2f, kurtosis %.2f\n", skew, kurt);
	check("NEGATIVE CONTROL: the shot-noise-only model is Gaussian (kurtosis 2.7 to 3.4) and so fails the non-Gaussian tests", kurt > 2.7 && kurt < 3.4 && std::fabs(skew) < 0.3);
	double lg = psd(xg, fs, 500.0, 4096), hg = psd(xg, fs, 16000.0, 4096);
	check("NEGATIVE CONTROL: and its hi-hat spectrum is within 6 dB, 500 Hz to 16 kHz, so it fails the Lorentzian-sum test", std::fabs(10 * std::log10(lg / hg)) < 6.0);

	// level and statistics are the same at every sample rate
	double r[4];
	double rates[4] = { 44100.0, 48000.0, 96000.0, 192000.0 };
	for (int i = 0; i < 4; i++) {
		AvalancheSource s(0x5EAF00Du, AvalancheNoise::PERCUSSIVE, true, 0.1e-6);
		s.setRate(rates[i]);
		for (int k = 0; k < (int)(rates[i] / 2); k++) s.next();
		double acc = 0;
		int N = (int)(rates[i] * 8);
		for (int k = 0; k < N; k++) { double v = s.next(); acc += v * v; }
		r[i] = std::sqrt(acc / N);
	}
	double dB = 20 * std::log10(*std::max_element(r, r + 4) / *std::min_element(r, r + 4));
	printf("    rms 44.1..192 kHz: %.4f %.4f %.4f %.4f (%.2f dB)\n", r[0], r[1], r[2], r[3], dB);
	check("the level is the same at every sample rate to 0.4 dB", dB < 0.4);

	// determinism and seeds
	AvalancheSource a(5u), b(5u), c(6u);
	a.setRate(fs); b.setRate(fs); c.setRate(fs);
	bool same = true, diff = false;
	for (int i = 0; i < 20000; i++) {
		float p = a.next(), q = b.next(), s = c.next();
		if (p != q) same = false;
		if (p != s) diff = true;
	}
	check("two sources with the same seed are bit-identical", same);
	check("and a different seed gives a different stream", diff);
	a.reset();
	AvalancheSource d(5u);
	d.setRate(fs);
	bool back = true;
	for (int i = 0; i < 5000; i++) if (a.next() != d.next()) back = false;
	check("reset() returns to the start of the stream", back);

	// the Tiny Dazzler original: 2N3904, +13.5 V
	AvalancheNoise dz(AvalancheNoise::DAZZLER);
	dz.start(192000.0);
	check("the Tiny Dazzler variant finds its operating point", dz.ok);
	checkv("it is on 13.5 V", dz.vcc, 13.5, 1e-9);
	AvalancheNoise pv;
	pv.start(192000.0);
	check("and its bias differs from the BC549 board's", std::fabs(dz.ckt.v[AvalancheNoise::C2] - pv.ckt.v[AvalancheNoise::C2]) > 0.1);
	bool bad = false;
	for (int i = 0; i < 192000 * 2; i++) { dz.step(); if (!dz.ok) bad = true; }
	check("it runs two seconds with every Newton step converged", !bad);
	AvalancheSource dsrc(0x5EAF00Du, AvalancheNoise::DAZZLER, true, 1e-9);
	dsrc.setRate(fs);
	std::vector<float> xd = grab(dsrc, 12);
	moments(xd, rms, skew, kurt);
	printf("    Tiny Dazzler (hi-hat C): rms %.4f, skew %.2f, kurtosis %.2f\n", rms, skew, kurt);
	check("the Dazzler source is also non-Gaussian", kurt > 4.0 || std::fabs(skew) > 0.5);
	check("and finite", std::isfinite(rms) && rms > 0.01);
}

int main() {
	testDwell();
	testSpectrumAndAmplitude();
	testCircuit();
	testSource();
	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}

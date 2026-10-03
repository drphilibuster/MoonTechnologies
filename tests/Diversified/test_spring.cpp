// Diversified 101, the spring tank: Kristian Blasol's Day 12 board (2N2222 + speaker, piezo +
// NE5532) and a tank built from Parker & Bilbao's helical-spring dispersion relation.
//
// Every check has an oracle that is not the code under test:
//   - the tank against the closed forms printed in the paper ((2) and (4)), and the DSP's
//     measured chirp against the dispersion relation solved separately (delayLF / delayHF);
//   - the driver against a hybrid-pi small-signal solution done here by hand and the BJT's own
//     collector-current relations;
//   - the pickup against the textbook responses of its networks.
// And each has a negative control: the same measurement made on a deliberately broken object,
// which must FAIL the check (printed as "control").

#include "../../src/Diversified/SpringBoard.hpp"

#include <cmath>
#include <complex>
#include <cstdio>
#include <vector>
#include <random>
#include <chrono>

using namespace springtank;
using namespace springboard;
typedef std::complex<double> cx;

static int checks = 0, failures = 0;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; \
	printf("  FAIL  "); printf(__VA_ARGS__); printf("\n"); } else { printf("  ok    "); printf(__VA_ARGS__); printf("\n"); } } while (0)
// A control passes when the broken thing is caught.
#define CONTROL(caught, ...) do { checks++; if (!(caught)) { failures++; \
	printf("  FAIL  control NOT caught: "); printf(__VA_ARGS__); printf("\n"); } else { printf("  ok    control caught: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static double relerr(double a, double b) { return std::fabs(a - b) / std::fabs(b); }

// ------------------------------------------------------------------------------------
// Measurement helpers
// ------------------------------------------------------------------------------------

/** Time of the envelope peak of y in a Gaussian band around f (centre f Hz, sigma_f Hz), in
    seconds, searching [t0, t1]. */
static double bandPeak(const std::vector<double>& y, double fs, double f, double sigmaF, double t0, double t1,
                       double* peakAmp = nullptr) {
	double sigT = 1.0 / (2.0 * M_PI * sigmaF);
	int half = (int) (4.0 * sigT * fs);
	std::vector<double> re(y.size()), im(y.size());
	for (size_t n = 0; n < y.size(); n++) {
		double ph = 2.0 * M_PI * f * (double) n / fs;
		re[n] = y[n] * std::cos(ph);
		im[n] = -y[n] * std::sin(ph);
	}
	std::vector<double> k(2 * half + 1);
	double ks = 0.0;
	for (int i = -half; i <= half; i++) {
		double t = i / fs;
		k[i + half] = std::exp(-0.5 * t * t / (sigT * sigT));
		ks += k[i + half];
	}
	double best = -1.0, bt = 0.0;
	int n0 = (int) (t0 * fs), n1 = std::min((int) (t1 * fs), (int) y.size() - 1);
	for (int n = n0; n <= n1; n++) {
		double a = 0.0, b = 0.0;
		for (int i = -half; i <= half; i++) {
			int m = n + i;
			if (m < 0 || m >= (int) y.size()) continue;
			a += re[m] * k[i + half];
			b += im[m] * k[i + half];
		}
		double mag = std::sqrt(a * a + b * b) / ks;
		if (mag > best) { best = mag; bt = (double) n / fs; }
	}
	if (peakAmp) *peakAmp = best;
	return bt;
}

/** Impulse response of one branch (host rate fs), gain of the loop set to g. */
static std::vector<double> loopImpulse(Loop& L, double fs, double seconds) {
	std::vector<double> y((size_t) (seconds * fs));
	for (size_t n = 0; n < y.size(); n++) y[n] = L.process(n == 0 ? 1.0 : 0.0);
	return y;
}

/** The time at which a band of the loop's impulse response peaks, less the converters' latency. */
static double arrival(const SpringDesign& d, bool hf, double fs, double f, double sigmaF, double seconds,
                      const Design* override = nullptr) {
	Loop L;
	const Design& dsn = override ? *override : (hf ? d.hf : d.lf);
	L.setup(dsn, fs, hf ? 0.45 * dsn.fsInt : d.lfCutHz, !hf);
	L.g = 0.0;
	std::vector<double> y = loopImpulse(L, fs, seconds);
	double lat = L.rs.latency() / dsn.fsInt;
	return bandPeak(y, fs, f, sigmaF, 0.0, seconds - 0.02) - lat;
}

// ------------------------------------------------------------------------------------
// 1. The physics: dispersion relation against the paper's closed forms
// ------------------------------------------------------------------------------------

static void testPhysics() {
	printf("tank physics: the dispersion relation against Parker & Bilbao's closed forms\n");
	Geometry gs[5] = {leem1210(0), leem1210(1), leem1210(2), olsonX82(0), olsonX82(1)};
	const char* nm[5] = {"Leem 1", "Leem 2", "Leem 3", "Olson 1", "Olson 2"};
	for (int i = 0; i < 5; i++) {
		const Geometry& g = gs[i];
		double fc = transitionHz(g), td = 2.0 * delayLF(g, 20.0);
		double fcPaper = closedFormFc(g), tdPaper = closedFormTD(g);
		CHECK(relerr(fc, fcPaper) < 0.01, "%s: Fc %.0f Hz from the relation, %.0f from eq (4)", nm[i], fc, fcPaper);
		CHECK(relerr(td, tdPaper) < 0.005, "%s: TD %.2f ms from the relation, %.2f from eq (2)", nm[i], td * 1e3, tdPaper * 1e3);
	}
	// The measured spring in the paper: Leem spring 3 has an echo spacing in the 40-55 ms range
	// and Fc near 4.5 kHz (their Fig. 4 puts Fc at about 4 kHz+; the formulas give 4.6 kHz).
	Geometry g3 = leem1210(2);
	CHECK(transitionHz(g3) > 4400 && transitionHz(g3) < 4800, "Leem 3 Fc %.0f Hz", transitionHz(g3));
	// The group delay rises from TD/2 toward Fc, and the HF branch falls.
	CHECK(delayLF(g3, 1000) < delayLF(g3, 3000) && delayLF(g3, 3000) < delayLF(g3, 4200),
	      "LF delay rises with frequency: %.1f, %.1f, %.1f ms at 1, 3, 4.2 kHz", delayLF(g3, 1000) * 1e3,
	      delayLF(g3, 3000) * 1e3, delayLF(g3, 4200) * 1e3);
	CHECK(delayHF(g3, 1000) > delayHF(g3, 6000) && delayHF(g3, 6000) > delayHF(g3, 15000),
	      "HF delay falls with frequency: %.1f, %.1f, %.1f ms at 1, 6, 15 kHz", delayHF(g3, 1000) * 1e3,
	      delayHF(g3, 6000) * 1e3, delayHF(g3, 15000) * 1e3);
	// Control: a spring with its wire radius doubled has a different TD and Fc, so a model that
	// ignored the geometry (one fixed answer) would be caught by the closed-form check.
	Geometry bad = g3;
	bad.wireRadius *= 2.0;
	CONTROL(relerr(transitionHz(bad), closedFormFc(g3)) > 0.5, "doubling r moves Fc to %.0f Hz, away from %.0f", transitionHz(bad), closedFormFc(g3));
}

// ------------------------------------------------------------------------------------
// 2. The DSP against the physics: chirp direction and size, echo spacing
// ------------------------------------------------------------------------------------

static void testChirp() {
	printf("tank DSP: the measured chirp against the dispersion relation\n");
	const double fs = 48000.0;
	SpringDesign d = designSpring(leem1210(2));
	CHECK(d.lf.maxRel < 0.05 && d.hf.maxRel < 0.15, "fit error against L/vg: LF max %.1f %% (rms %.1f), HF max %.1f %% (rms %.1f)",
	      d.lf.maxRel * 100, d.lf.rmsRel * 100, d.hf.maxRel * 100, d.hf.rmsRel * 100);
	const double fl[5] = {600, 1500, 2500, 3200, 3600};
	double prev = 0.0;
	bool rising = true, match = true;
	double worst = 0.0;
	for (int i = 0; i < 5; i++) {
		double t = arrival(d, false, fs, fl[i], 60.0, 0.25);
		double want = delayLF(d.geom, fl[i]);
		double tol = 0.0010 + 0.04 * want;
		worst = std::max(worst, std::fabs(t - want) / tol);
		if (std::fabs(t - want) > tol) match = false;
		if (t <= prev) rising = false;
		prev = t;
		printf("          LF %.0f Hz: arrives %.2f ms, relation says %.2f ms\n", fl[i], t * 1e3, want * 1e3);
	}
	CHECK(rising, "LF echo is a rising chirp: low frequencies arrive first, the later the higher");
	CHECK(match, "LF arrival times follow L/vg within 1 ms + 4 %% (worst %.2f of tolerance)", worst);

	const double fh[4] = {800, 2500, 6000, 9000};
	prev = 1e9;
	bool falling = true, hmatch = true;
	for (int i = 0; i < 4; i++) {
		double t = arrival(d, true, fs, fh[i], 250.0, 0.12);
		double want = delayHF(d.geom, fh[i]);
		if (std::fabs(t - want) > 0.0015 + 0.10 * want) hmatch = false;
		if (t >= prev) falling = false;
		prev = t;
		printf("          HF %.0f Hz: arrives %.2f ms, relation says %.2f ms\n", fh[i], t * 1e3, want * 1e3);
	}
	CHECK(falling, "HF echo is a falling chirp: the higher the earlier");
	CHECK(hmatch, "HF arrival times follow L/vg within 1.5 ms + 10 %%");

	// Controls. (a) No dispersion: the allpass chain removed. The low and high bands then arrive
	// together; the "rising" check must fail.
	Design flat = d.lf;
	flat.a.clear();
	flat.n0 = (int) std::lround(delayLF(d.geom, 20.0) * flat.fsInt);
	double t1 = arrival(d, false, fs, 600, 60.0, 0.25, &flat), t2 = arrival(d, false, fs, 3600, 60.0, 0.25, &flat);
	CONTROL(!(t2 - t1 > 0.004), "chain removed: 600 Hz and 3.6 kHz arrive %.2f ms apart (the real tank: %.1f ms)", (t2 - t1) * 1e3,
	        (delayLF(d.geom, 3600) - delayLF(d.geom, 600)) * 1e3);
	// (b) The chirp the wrong way round: fit the LF branch to the HF branch's falling delay.
	std::vector<double> f, t;
	for (int i = 0; i < 60; i++) { double fi = 100.0 + 38.0 * i; f.push_back(fi); t.push_back(delayHF(d.geom, fi) + 0.02); }
	Design wrong = fitChain(d.lf.fsInt, f, t, 140, 8, true);
	double w1 = arrival(d, false, fs, 600, 60.0, 0.25, &wrong), w2 = arrival(d, false, fs, 2000, 60.0, 0.25, &wrong);
	CONTROL(w2 < w1, "falling-delay design: 2 kHz arrives (%.1f ms) BEFORE 600 Hz (%.1f ms)", w2 * 1e3, w1 * 1e3);
}

static void testEchoSpacing() {
	printf("tank DSP: echo spacing from the spring's length and wave speed\n");
	for (int id = 0; id < 2; id++) {
		const TankPreset& P = tankPreset(id);
		for (int sr = 0; sr < 2; sr++) {
			double fs = sr == 0 ? 44100.0 : 96000.0;
			for (int s = 0; s < P.count; s++) {
				const SpringDesign& d = P.spring[s];
				Loop L;
				L.setup(d.lf, fs, d.lfCutHz, true);
				L.setLoss(6.0, 6000.0);
				std::vector<double> y = loopImpulse(L, fs, 0.6);
				double lat = L.rs.latency() / d.lf.fsInt;
				double f = 250.0;
				double t1 = bandPeak(y, fs, f, 45.0, 0.0, 0.8 * d.td);
				double t2 = bandPeak(y, fs, f, 45.0, t1 + 0.6 * d.td, t1 + 1.4 * d.td);
				double spacing = t2 - t1;
				double want = closedFormTD(d.geom);
				if (sr == 0 && s == 0 && id == 0) (void) lat;
				CHECK(std::fabs(spacing - want) < 0.0008 + 0.01 * want, "%s spring %d at %.1f kHz: echoes %.2f ms apart, 4LR/(rc) = %.2f ms (L = %.2f m)",
				      P.name, s + 1, fs / 1000, spacing * 1e3, want * 1e3, d.geom.wireLength());
			}
		}
	}
	// Control: a tank built for a spring of twice the length. Same measurement, same oracle (the
	// original spring's TD): it must be off by a factor of two.
	Geometry g = leem1210(2), g2 = g;
	g2.turns *= 2.0;
	SpringDesign d2 = designSpring(g2);
	Loop L;
	L.setup(d2.lf, 48000.0, d2.lfCutHz, true);
	L.setLoss(6.0, 6000.0);
	std::vector<double> y = loopImpulse(L, 48000.0, 0.7);
	double t1 = bandPeak(y, 48000.0, 250.0, 45.0, 0.0, 0.8 * d2.td);
	double t2 = bandPeak(y, 48000.0, 250.0, 45.0, t1 + 0.6 * d2.td, t1 + 1.4 * d2.td);
	CONTROL(std::fabs((t2 - t1) - closedFormTD(g)) > 0.02, "twice the turns: spacing %.1f ms against the original spring's %.1f ms", (t2 - t1) * 1e3, closedFormTD(g) * 1e3);
}

static void testResampler() {
	printf("tank DSP: the rate converters are a clean, known delay\n");
	for (int k = 0; k < 2; k++) {
		double fs = k ? 96000.0 : 44100.0, fl = 9496.0;
		Resampler r;
		r.setup(fs, fl);
		std::vector<double> out;
		double dummy;
		int N = 8000;
		out.assign(N, 0.0);
		// the loop is transparent: loop sample in = loop sample out
		std::vector<double> x(N);
		for (int n = 0; n < N; n++) x[n] = std::sin(2 * M_PI * 3000.0 * n / fs) + 0.5 * std::sin(2 * M_PI * 1100.0 * n / fs);
		for (int n = 0; n < N; n++) {
			if (r.push(x[n], dummy)) r.pushLoop(dummy);
			out[n] = r.pull();
		}
		// find the delay by fitting: expected output(n) = x(n - latency*q)
		double lagHost = r.latency() * r.q;
		double err = 0.0, ref = 0.0;
		for (int n = 3000; n < N - 10; n++) {
			double t = n - lagHost;
			double want = std::sin(2 * M_PI * 3000.0 * t / fs) + 0.5 * std::sin(2 * M_PI * 1100.0 * t / fs);
			err += (out[n] - want) * (out[n] - want);
			ref += want * want;
		}
		CHECK(std::sqrt(err / ref) < 0.02, "%.1f kHz host: through the loop rate and back, error %.2f %% of the signal, delay %.2f ms", fs / 1000, 100.0 * std::sqrt(err / ref), lagHost / fs * 1e3);
		if (k == 0) {
			double err2 = 0.0;
			for (int n = 3000; n < N - 10; n++) {
				double t = n - lagHost * 0.8;                       // a wrong delay
				double want = std::sin(2 * M_PI * 3000.0 * t / fs) + 0.5 * std::sin(2 * M_PI * 1100.0 * t / fs);
				err2 += (out[n] - want) * (out[n] - want);
			}
			CONTROL(std::sqrt(err2 / ref) > 0.1, "the same check against a delay 20 %% off reads %.0f %% error", 100.0 * std::sqrt(err2 / ref));
		}
	}
}

// ------------------------------------------------------------------------------------
// 3. The driver
// ------------------------------------------------------------------------------------

/** Complex Gaussian elimination, n <= 4. */
static void csolve(cx A[4][5], int n, cx* x) {
	for (int c = 0; c < n; c++) {
		int p = c;
		for (int r = c + 1; r < n; r++) if (std::abs(A[r][c]) > std::abs(A[p][c])) p = r;
		for (int j = 0; j <= n; j++) std::swap(A[c][j], A[p][j]);
		for (int r = c + 1; r < n; r++) {
			cx f = A[r][c] / A[c][c];
			for (int j = c; j <= n; j++) A[r][j] -= f * A[c][j];
		}
	}
	for (int r = n - 1; r >= 0; r--) {
		cx s = A[r][n];
		for (int j = r + 1; j < n; j++) s -= A[r][j] * x[j];
		x[r] = s / A[r][r];
	}
}

/** Small-signal speaker current per input volt, and the junction voltage per input volt, by
    hand (hybrid-pi), from the collector current and the parts. Nodes: B (C1's far side), Bb
    (inside Rb), C. The Early effect: Ic = Is e^(vbe/Vt) (1 + Vcb/VAF), so gm = Ic/Vt,
    r_pi = BF (1 + Vcb/VAF) / gm and ro = (VAF + Vcb) / Ic. */
static void handGain(double f, double pot, double ic, double vcb, double bf, double vaf, double* iPerV, double* vbePerV) {
	cx s(0.0, 2.0 * M_PI * f);
	double gm = ic / mna::kVt, rpi = bf * (1.0 + vcb / vaf) / gm, ro = (vaf + vcb) / ic;
	// the speaker between VCC (ac ground) and the collector
	double ws = 2.0 * M_PI * kSpkFs;
	double cmes = kSpkQes / (ws * kSpkRe), lmes = 1.0 / (ws * ws * cmes), res = kSpkRe * kSpkQms / kSpkQes;
	cx ymot = 1.0 / res + s * cmes + 1.0 / (s * lmes);
	cx zsp = s * kSpkLe + kSpkRe + 1.0 / ymot;
	// the source: Vin -> (1-x)RP1 -> W -> x RP1 to ground; Thevenin at W, then C1 into B
	double rt = std::max(1.0, (1.0 - pot) * kRP1), rb = std::max(1.0, pot * kRP1);
	cx vth = rb / (rt + rb);
	double rth = rt * rb / (rt + rb);
	cx zc1 = rth + 1.0 / (s * kC1);
	cx A[4][5] = {};
	// unknowns v0 = B, v1 = Bb, v2 = C
	A[0][0] = 1.0 / zc1 + 1.0 / kR2 + 1.0 / kR1 + 1.0 / kQRb;
	A[0][1] = -1.0 / kQRb;
	A[0][2] = -1.0 / kR1;
	A[0][3] = vth / zc1;
	A[1][0] = -1.0 / kQRb;
	A[1][1] = 1.0 / kQRb + 1.0 / rpi + gm * 0.0;
	A[1][3] = 0.0;
	// collector: gm vbe (out of C) + (vc)/ro + vc/zsp + (vc - vb)/R1 = 0
	A[2][0] = -1.0 / kR1;
	A[2][1] = gm;
	A[2][2] = 1.0 / ro + 1.0 / zsp + 1.0 / kR1;
	A[2][3] = 0.0;
	cx x[4];
	csolve(A, 3, x);
	*iPerV = std::abs(-x[2] / zsp);
	*vbePerV = std::abs(x[1]);          // Bb is the junction's base
}

static double firstHarmonicAmp(const std::vector<double>& y, double fs, double f, int from, int to) {
	double a = 0, b = 0;
	for (int n = from; n < to; n++) {
		double ph = 2 * M_PI * f * n / fs;
		a += y[n] * std::cos(ph); b += y[n] * std::sin(ph);
	}
	return 2.0 * std::sqrt(a * a + b * b) / (to - from);
}

static std::vector<double> driverRun(Driver& e, double fs, double f, double amp, double seconds, double* ic0 = nullptr) {
	int N = (int) (seconds * fs);
	std::vector<double> y(N);
	double c0 = e.speakerAmps();
	if (ic0) *ic0 = c0;
	for (int n = 0; n < N; n++) { e.step(amp * std::sin(2 * M_PI * f * n / fs)); y[n] = e.speakerAmps(); }
	return y;
}

static void testDriver() {
	printf("driver: the 2N2222 stage and the speaker, against hybrid-pi by hand\n");
	const double fs = 96000.0;
	Driver d;
	d.setRate(fs);
	d.setPot(0.5);
	double ic = d.speakerAmps(), vc = d.collectorVolts(), vb = d.baseVolts();
	double ib = (vc - vb) / kR1 - vb / kR2;
	double vbb = vb - ib * kQRb, vcb = vc - vbb;
	// Independent of the solver: the transistor's own relations and Kirchhoff.
	double beff = kQBF * (1.0 + vcb / kQVAF);
	CHECK(std::fabs(ic / ib - beff) / beff < 0.03,
	      "bias: Ic %.1f mA, Ib %.3f mA, Ic/Ib = %.0f against BF (1 + Vcb/VAF) = %.0f", ic * 1e3, ib * 1e3, ic / ib, beff);
	CHECK(std::fabs(vbb - mna::kVt * std::log(ic / kQIs)) < 0.012, "bias: Vbe %.3f V against Vt ln(Ic/Is) = %.3f V", vbb, mna::kVt * std::log(ic / kQIs));
	CHECK(std::fabs((kVcc - vc) / kSpkRe - ic) / ic < 0.01, "bias: the speaker's DC drop (%.2f V across %.1f ohm) is the collector current", kVcc - vc, kSpkRe);

	// small-signal current in the speaker, by simulation and by hand
	const double freqs[4] = {100.0, 480.0, 1000.0, 4000.0};
	for (int i = 0; i < 4; i++) {
		Driver e;
		e.setRate(fs);
		e.setPot(0.5);
		double amp = 0.0005, c0;
		std::vector<double> y = driverRun(e, fs, freqs[i], amp, 0.4, &c0);
		int N = (int) y.size();
		double got = firstHarmonicAmp(y, fs, freqs[i], N / 2, N) / amp;
		double want, vbe;
		handGain(freqs[i], 0.5, ic, vcb, kQBF, kQVAF, &want, &vbe);
		CHECK(relerr(got, want) < 0.03, "%.0f Hz: speaker current per input volt %.3f mA/V simulated, %.3f by hand", freqs[i], got * 1e3, want * 1e3);
	}
	// Control: a driver given a different transistor (BF 60) is caught by the same hand model.
	{
		Driver e;
		e.qBF = 60.0;
		e.setRate(fs);
		e.setPot(0.5);
		double amp = 0.0005, c0;
		std::vector<double> y = driverRun(e, fs, 1000.0, amp, 0.4, &c0);
		int N = (int) y.size();
		double got = firstHarmonicAmp(y, fs, 1000.0, N / 2, N) / amp;
		double want, vbe;
		handGain(1000.0, 0.5, ic, vcb, kQBF, kQVAF, &want, &vbe);
		CONTROL(relerr(got, want) > 0.1, "BF = 60: %.3f mA/V against the 2N2222's %.3f", got * 1e3, want * 1e3);
	}

	// The exponential: for a junction driven by x = Vbe_peak / Vt << 1 the second harmonic of the
	// collector current is x / 4 of the fundamental (expand exp(x sin) to second order).
	{
		Driver e;
		e.setRate(fs);
		e.setPot(1.0);
		double amp = 0.002, c0;
		std::vector<double> y = driverRun(e, fs, 1000.0, amp, 0.2, &c0);
		int N = (int) y.size();
		double h1 = firstHarmonicAmp(y, fs, 1000.0, N / 2, N), h2 = firstHarmonicAmp(y, fs, 2000.0, N / 2, N);
		double gi, gv;
		handGain(1000.0, 1.0, ic, vcb, kQBF, kQVAF, &gi, &gv);
		double x = gv * amp / mna::kVt;
		CHECK(std::fabs(h2 / h1 - x / 4.0) / (x / 4.0) < 0.3, "second harmonic %.2f %% of the fundamental, Vbe %.1f mV peak gives x/4 = %.2f %%", 100.0 * h2 / h1, gv * amp * 1e3, 100.0 * x / 4.0);
	}

	// Clipping onset: the speaker current cannot fall below zero, so the swing stops where the
	// small-signal amplitude equals the bias current: V = Ic / (dIsp/dVin).
	const double pots[2] = {0.5, 0.2};
	double onsets[2];
	for (int k = 0; k < 2; k++) {
		double gi, gv;
		handGain(1000.0, pots[k], ic, vcb, kQBF, kQVAF, &gi, &gv);
		double predicted = ic / gi, onset = 0.0;
		for (double amp = predicted * 0.15; amp < predicted * 8.0; amp *= 1.03) {
			Driver e;
			e.setRate(fs);
			e.setPot(pots[k]);
			int N = (int) (0.08 * fs);
			double mn = 1e9;
			for (int n = 0; n < N; n++) { e.step(amp * std::sin(2 * M_PI * 1000.0 * n / fs)); if (n > N / 2) mn = std::min(mn, e.speakerAmps()); }
			if (mn < 0.05 * ic) { onset = amp; break; }
		}
		onsets[k] = onset;
		printf("          RP1 at %.0f %%: current reaches zero at %.2f V; Ic / small-signal gain = %.2f V\n", pots[k] * 100, onset, predicted);
		CHECK(onset > 0.8 * predicted && onset < 1.8 * predicted, "RP1 %.0f %%: the driver clips near the small-signal prediction (%.2f V against %.2f V)", pots[k] * 100, onset, predicted);
	}
	CHECK(onsets[1] > 1.5 * onsets[0], "turning RP1 down moves the onset up: %.2f V at 20 %%, %.2f V at 50 %%", onsets[1], onsets[0]);
	// Control: the onset is a property of the pot setting (V = Ic / gain, and the gain follows RP1),
	// so the prediction for the wrong pot must miss.
	{
		double gi, gv;
		handGain(1000.0, 0.2, ic, vcb, kQBF, kQVAF, &gi, &gv);
		double wrongPrediction = ic / gi;
		CONTROL(!(onsets[0] > 0.8 * wrongPrediction && onsets[0] < 1.8 * wrongPrediction), "the 50 %% onset (%.2f V) against the 20 %% prediction (%.2f V)", onsets[0], wrongPrediction);
	}
	// Motional impedance: the speaker's resonance shows as a peak in the voltage across it
	{
		double best = 0.0, bf = 0.0;
		for (double f = 200; f < 1200; f += 20) {
			Driver g;
			g.setRate(fs);
			g.setPot(0.5);
			int N = (int) (0.3 * fs);
			std::vector<double> y(N);
			for (int n = 0; n < N; n++) { g.step(0.0005 * std::sin(2 * M_PI * f * n / fs)); y[n] = g.motionalVolts(); }
			double a = firstHarmonicAmp(y, fs, f, N / 2, N);
			if (a > best) { best = a; bf = f; }
		}
		CHECK(std::fabs(bf - kSpkFs) < 80.0, "the cone's motional voltage peaks at %.0f Hz; the speaker's Fs is %.0f Hz", bf, kSpkFs);
	}
}

// ------------------------------------------------------------------------------------
// 4. The pickup
// ------------------------------------------------------------------------------------

static double pickupGainAt(Pickup& p, double fs, double f, double emfAmp, double* peak = nullptr) {
	int N = (int) (0.25 * fs);
	std::vector<double> y(N);
	for (int n = 0; n < N; n++) y[n] = p.process(emfAmp * std::sin(2 * M_PI * f * n / fs));
	if (peak) { *peak = 0; for (int n = N / 2; n < N; n++) *peak = std::max(*peak, std::fabs(y[n])); }
	return firstHarmonicAmp(y, fs, f, N / 2, N) / emfAmp;
}

static cx closedLoop(double gbw, double a0, double g, double f) {
	cx sj(0, 2 * M_PI * f);
	double wt = 2 * M_PI * gbw;
	cx A = a0 / (1.0 + sj * a0 / wt);
	return g / (1.0 + g / A);
}

static void testPickup() {
	printf("pickup: piezo into C3 / R3, U1.1 (1 + RP2 / R4), C4, U1.2\n");
	const double fs = 192000.0;
	double cs = kPiezoC * kC3 / (kPiezoC + kC3);
	double fcHpf = 1.0 / (2 * M_PI * kR3 * cs);
	const double fracs[4] = {0.0, 0.1, 0.5, 1.0};
	for (int i = 0; i < 4; i++) {
		Pickup p;
		p.setRate(fs);
		p.setGain(fracs[i]);
		double f = 12000.0;
		double got = pickupGainAt(p, fs, f, 0.001);
		cx hp = cx(0, f / fcHpf) / (1.0 + cx(0, f / fcHpf));
		cx hp2 = cx(0, f / (1.0 / (2 * M_PI * kBiasU12 * kC4))) / (1.0 + cx(0, f / (1.0 / (2 * M_PI * kBiasU12 * kC4))));
		double want = std::abs(hp * closedLoop(12e6, 1e5, 1.0 + fracs[i] * kRP2 / kR4, f) * hp2);
		CHECK(relerr(got, want) < 0.02, "RP2 %.0f %%: gain at 12 kHz %.2f, %.2f from 1 + RP2/R4 = %.0f with the highpass and the NE5532's GBW", fracs[i] * 100, got, want, 1.0 + fracs[i] * kRP2 / kR4);
	}
	// the first stage's corner, with the piezo's own capacitance in series
	{
		Pickup p;
		p.setRate(fs);
		p.setGain(0.0);
		double hi = pickupGainAt(p, fs, 30000.0, 0.001);
		double at = pickupGainAt(p, fs, fcHpf, 0.001);
		// -3 dB relative to the plateau (30 kHz is 1 dB under it for a 9.5 kHz corner): compare against the analytic ratio
		cx ratio = (cx(0, 1) / (1.0 + cx(0, 1))) / (cx(0, 30000.0 / fcHpf) / (1.0 + cx(0, 30000.0 / fcHpf)));
		CHECK(relerr(at / hi, std::abs(ratio)) < 0.03, "first-stage highpass corner %.2f kHz (R3 with Cp = %.0f nF in series with C3), not the %.2f kHz of R3 C3 alone", fcHpf / 1000, kPiezoC * 1e9, 1.0 / (2 * M_PI * kR3 * kC3) / 1000);
		Pickup q;
		q.piezoC = 1.0;                  // control: a piezo with no capacitance of its own
		q.setRate(fs);
		q.setGain(0.0);
		double hi2 = pickupGainAt(q, fs, 30000.0, 0.001), at2 = pickupGainAt(q, fs, fcHpf, 0.001);
		CONTROL(relerr(at2 / hi2, std::abs(ratio)) > 0.3, "with no piezo capacitance the %.1f kHz point is %.2f of the plateau, not %.2f", fcHpf / 1000, at2 / hi2, std::abs(ratio));
	}
	// the clip: U1.2 into R5 = 1k from +-12 V rails
	{
		Pickup p;
		p.setRate(fs);
		p.setGain(1.0);
		double pk = 0.0;
		pickupGainAt(p, fs, 20000.0, 1.0, &pk);
		opamp::Core c;
		c.setSpec(ne5532());
		c.setSupply(kRailPos, kRailNeg);
		c.setLoad(kR5, 0.0);
		CHECK(relerr(pk, c.hi) < 0.01, "driven hard it clips at %.2f V: the NE5532 into R5 (1k) from +-12 V gives %.2f V", pk, c.hi);
	}
	{
		Pickup p;
		p.setRate(fs);
		p.setGain(1.0);
		double y = 0.0;
		for (int n = 0; n < 20000; n++) y = p.process(0.0);
		CHECK(std::fabs(y) < 1e-3, "no signal: output rests at %.2f mV", y * 1e3);
	}
}

// ------------------------------------------------------------------------------------
// 5. The whole board
// ------------------------------------------------------------------------------------

static void testBoard() {
	printf("board: driver -> speaker -> tank -> piezo -> pickup\n");
	const double fs = 48000.0;
	std::mt19937 gen(7);
	std::normal_distribution<double> nd(0.0, 1.0);

	// stability across the corners of every control
	bool fin = true;
	double worst = 0.0;
	for (int tank = 0; tank < 2; tank++)
		for (int c = 0; c < 8; c++) {
			Board b;
			b.setTank(tank);
			b.setSampleRate(fs);
			double pot = (c & 1) ? 1.0 : 0.0, dw = (c & 2) ? 6.0 : 0.4, tn = (c & 4) ? 6000.0 : 1000.0;
			b.setParams(pot, dw, tn, 1.0);
			for (int n = 0; n < (int) (fs * 3); n++) {
				double x = 5.0 * nd(gen);
				if (x > 12) x = 12;
				if (x < -12) x = -12;
				double y = b.process(x, (n % 24000) == 0);
				if (!std::isfinite(y)) fin = false;
				worst = std::max(worst, std::fabs(y));
			}
		}
	CHECK(fin && worst < 11.0, "stable and finite at every corner of RP1, dwell, tone, both tanks (peak %.2f V)", worst);

	// level at the defaults and the full gain
	{
		Board b;
		b.setSampleRate(fs);
		b.setParams(0.35, 0.4 * std::pow(15.0, 0.55), 1000.0 * std::pow(6.0, 0.5), 0.75);
		double si = 0, so = 0;
		int N = (int) (fs * 4);
		for (int n = 0; n < N; n++) { double x = nd(gen); double y = b.process(x, false); if (n > N / 2) { si += x * x; so += y * y; } }
		double r = std::sqrt(so / si);
		printf("          1 V rms noise in, defaults: %.3f V rms out (gain 76x, ASSUMED piezo sensitivity)\n", std::sqrt(so / (N / 2)));
		CHECK(r > 0.3 && r < 3.0, "default level is within a factor of three of unity: %.2f", r);
	}

	// reverberation time follows DWELL (the loop's RT60), measured from the Schroeder integral
	for (int k = 0; k < 2; k++) {
		double rt = k ? 3.0 : 1.2;
		Board b;
		b.setSampleRate(fs);
		b.setParams(0.5, rt, 6000.0, 1.0);
		int N = (int) (fs * 5);
		std::vector<double> y(N);
		for (int n = 0; n < N; n++) y[n] = b.process(n < 48 ? 4.0 : 0.0, false);
		std::vector<double> E(N);
		double acc = 0;
		for (int n = N - 1; n >= 0; n--) { acc += y[n] * y[n]; E[n] = acc; }
		// slope between -5 dB and -25 dB of the integrated decay
		int a = -1, bb = -1;
		for (int n = 0; n < N; n++) {
			double dB = 10 * std::log10(E[n] / E[0]);
			if (a < 0 && dB < -5) a = n;
			if (bb < 0 && dB < -25) { bb = n; break; }
		}
		double t60 = bb > a && a >= 0 ? 60.0 / 20.0 * (bb - a) / fs : 0.0;
		CHECK(t60 > 0.65 * rt && t60 < 1.5 * rt, "DWELL: asked for RT60 %.1f s, the output's Schroeder decay (T20 x 3) is %.2f s", rt, t60);
	}

	// the tanks differ as the springs do
	for (int id = 0; id < 2; id++) {
		Board b;
		b.setTank(id);
		b.setSampleRate(fs);
		b.setParams(0.5, 6.0, 6000.0, 1.0);
		int N = (int) (fs * 0.5);
		std::vector<double> y(N);
		for (int n = 0; n < N; n++) y[n] = b.process(n < 24 ? 4.0 : 0.0, false);
		const TankPreset& P = tankPreset(id);
		// every spring's echoes appear: find band peaks of 250 Hz near each predicted position
		double t1 = bandPeak(y, fs, 250.0, 45.0, 0.0, 0.18);
		(void) t1;
		bool found = true;
		for (int s = 0; s < P.count; s++) {
			double td = closedFormTD(P.spring[s].geom);
			double lat = (springtank::Resampler().latency());
			(void) lat;
			double first = delayLF(P.spring[s].geom, 250.0);
			double a0 = 0.0;
			double t = bandPeak(y, fs, 250.0, 45.0, std::max(0.0, first + 0.5 * td - 0.012), first + 0.5 * td + 0.012, &a0);
			if (a0 < 1e-6) found = false;
			(void) t;
		}
		CHECK(found, "%s: an echo band at every spring's second traversal", P.name);
	}

	// the knock
	{
		Board b;
		b.setSampleRate(fs);
		b.setParams(0.0, 2.0, 3000.0, 1.0);
		double quiet = 0, knocked = 0;
		for (int n = 0; n < (int) fs; n++) { double y = b.process(0.0, n == 100 || (n > 100 && n < 400)); if (n < 100) quiet = std::max(quiet, std::fabs(y)); else knocked = std::max(knocked, std::fabs(y)); }
		CHECK(quiet < 1e-3 && knocked > 0.05, "AUX knock rings the tank with the input at zero: %.3f V peak (%.4f V before it)", knocked, quiet);
	}
}

static void testSampleRates() {
	printf("board: the same tank at every sample rate\n");
	double ref = 0.0;
	const double rates[4] = {44100.0, 48000.0, 96000.0, 192000.0};
	for (int i = 0; i < 4; i++) {
		double fs = rates[i];
		const SpringDesign& d = tankPreset(0).spring[2];
		Loop L;
		L.setup(d.lf, fs, d.lfCutHz, true);
		L.setLoss(6.0, 6000.0);
		std::vector<double> y = loopImpulse(L, fs, 0.5);
		double t1 = bandPeak(y, fs, 250.0, 45.0, 0.0, 0.8 * d.td);
		double t2 = bandPeak(y, fs, 250.0, 45.0, t1 + 0.6 * d.td, t1 + 1.4 * d.td);
		double sp = t2 - t1;
		if (i == 0) ref = sp;
		CHECK(std::fabs(sp - ref) < 0.0006, "%.1f kHz: echo spacing %.2f ms (44.1 kHz: %.2f ms)", fs / 1000, sp * 1e3, ref * 1e3);
	}
}

static void testCost() {
	printf("board: cost\n");
	const double rates[3] = {44100.0, 48000.0, 96000.0};
	for (int i = 0; i < 3; i++) {
		Board b;
		b.setSampleRate(rates[i]);
		b.setParams(0.35, 2.0, 3000.0, 0.75);
		int N = (int) rates[i];
		volatile double sink = 0;
		auto t0 = std::chrono::steady_clock::now();
		for (int n = 0; n < N; n++) sink = sink + b.process(2.0 * std::sin(n * 0.05), false);
		double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
		printf("          %.1f kHz: %.2f us per sample, %.1f %% of one core (%d-spring tank, -O2 scalar)\n", rates[i] / 1000, dt / N * 1e6, 100.0 * dt, tankPreset(0).count);
	}
}

int main() {
	auto t0 = std::chrono::steady_clock::now();
	(void) tankPreset(0);
	printf("tank design (both presets, once per process): %.0f ms\n", 1e3 * std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
	testPhysics();
	testChirp();
	testEchoSpacing();
	testResampler();
	testDriver();
	testPickup();
	testBoard();
	testSampleRates();
	testCost();
	printf("\n%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}

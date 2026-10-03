// The Percussive Noise Voice's avalanche-noise circuit (AvalancheNoise.hpp, solved by Mna.hpp):
// the operating point against KCL written out here, the noise's spectrum against the circuit's own
// small-signal transfer function solved independently, and its level and determinism.

#include "../../src/Kickback/AvalancheNoise.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <vector>

using namespace kickback;
typedef AvalancheNoise N;
typedef std::complex<double> cx;

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

static const double Vt = 0.025852;
struct Bjt { double Ic, Ib, gm, go, gpi, gmu; };
static Bjt bjt(double vbe, double vbc) {
	double Is = N::kIs, BF = N::kBF, BR = N::kBR, VA = N::kVAF;
	auto ic = [&](double a, double c) { return Is * (std::exp(a / Vt) - std::exp(c / Vt)) * (1.0 - c / VA) - Is / BR * (std::exp(c / Vt) - 1.0); };
	auto ib = [&](double a, double c) { return Is / BF * (std::exp(a / Vt) - 1.0) + Is / BR * (std::exp(c / Vt) - 1.0); };
	double h = 1e-6;
	Bjt b;
	b.Ic = ic(vbe, vbc); b.Ib = ib(vbe, vbc);
	b.gm = (ic(vbe + h, vbc) - ic(vbe - h, vbc)) / (2 * h);
	b.go = (ic(vbe, vbc + h) - ic(vbe, vbc - h)) / (2 * h);
	b.gpi = (ib(vbe + h, vbc) - ib(vbe - h, vbc)) / (2 * h);
	b.gmu = (ib(vbe, vbc + h) - ib(vbe, vbc - h)) / (2 * h);
	return b;
}
// the breakdown junction: forward Shockley minus the SPICE breakdown term, anode B2 -> cathode A
static double zener(double vd) {
	return 1e-14 * (std::exp(vd / 0.025852) - 1.0) - N::kIbv * std::exp(-(vd + N::kBv) / N::kNvtBr);
}
static double zenerG(double vd) {
	return 1e-14 / 0.025852 * std::exp(vd / 0.025852) + N::kIbv / N::kNvtBr * std::exp(-(vd + N::kBv) / N::kNvtBr);
}

static void testDc() {
	printf("operating point\n");
	N n;
	n.start(192000.0);
	const double* v = n.ckt.v;
	double Vcc = N::kVcc;
	Bjt t2 = bjt(v[N::B2], v[N::B2] - v[N::C2]);
	double iz = zener(v[N::B2] - v[N::A]);                 // anode -> cathode
	double F[4];
	F[N::C2] = (v[N::C2] - Vcc) / 4.7e3 + (v[N::C2] - v[N::A]) / 47e3 + t2.Ic;
	F[N::A]  = (v[N::A] - v[N::C2]) / 47e3 - iz;           // the zener's anode->cathode current leaves B2 and enters A
	F[N::B2] = t2.Ib + iz;
	bool kcl = std::fabs(F[N::C2]) < 1e-9 && std::fabs(F[N::A]) < 1e-9 && std::fabs(F[N::B2]) < 1e-9;
	printf("    A %.3f V  B2 %.3f V  C2 %.3f V  B1 %.3f V  breakdown current %.2f uA\n", v[N::A], v[N::B2], v[N::C2], v[N::B1], -iz * 1e6);
	check("KCL holds at A, B2 and C2 to 1 nA", kcl);
	checkv("the junction sits where its breakdown law puts the current it carries: BV + nVt * ln(I / IBV)", v[N::A] - v[N::B2], N::kBv + N::kNvtBr * std::log(-iz / N::kIbv), 0.01);
	check("and that is breakdown, within a volt of BV", std::fabs((v[N::A] - v[N::B2]) - N::kBv) < 1.0);
	check("T2 is biased by it: collector current is hFE times the breakdown current", std::fabs(t2.Ic / -iz / N::kBF - 1.0) < 0.15);
	check("T2 conducts but is not saturated", v[N::C2] > 1.0 && v[N::C2] < Vcc);
	checkv("B1 sits at T1's base-emitter drop", v[N::B1], 0.65, 0.05);
	// the breakdown stays on in a step of the supply tolerance
	check("ok flag", n.ok);
}

// NOTE (2026-10-03): the circuit's default noise is now the microplasma model (test_microplasma.cpp).
// Everything below that compares against a linear small-signal solve or a Gaussian runs the
// shot-noise-only model (`microplasma = false`), which is the part of the physics that IS linear.

// the circuit's own small-signal transfer from the junction's noise current to a node, solved here
static void transfer(const N& n, double f, cx& hB1, cx& hC2) {
	const double* v = n.ckt.v;
	double w = 2 * M_PI * f;
	cx j(0, 1);
	cx A[4][5] = {};
	auto G = [&](int a, int b, cx y) {
		if (a >= 0) { A[a][a] += y; if (b >= 0) A[a][b] -= y; }
		if (b >= 0) { A[b][b] += y; if (a >= 0) A[b][a] -= y; }
	};
	G(N::C2, -1, 1.0 / 4.7e3);                      // R4 (VCC is AC ground)
	G(N::C2, N::A, 1.0 / 47e3);                     // R3
	G(N::A, -1, j * w * 0.1e-6);                    // C3
	G(N::C2, N::B1, j * w * 1e-6);                  // C4
	G(N::B1, -1, 1.0 / 1e6);                        // R7
	G(N::B1, -1, j * w * 0.1e-6);                   // C5
	G(N::B1, -1, 7.6e-14 / 520.0 / 0.025852 * std::exp(v[N::B1] / 0.025852));   // T1's base-emitter
	G(N::B2, N::A, zenerG(v[N::B2] - v[N::A]));     // the junction
	Bjt t = bjt(v[N::B2], v[N::B2] - v[N::C2]);
	// T2: collector C2, base B2, emitter ground. vbe = Vb, vbc = Vb - Vc
	A[N::C2][N::B2] += t.gm + t.go;  A[N::C2][N::C2] += -t.go;
	A[N::B2][N::B2] += t.gpi + t.gmu; A[N::B2][N::C2] += -t.gmu;
	// the noise current: leaves A, enters B2
	A[N::A][4] += -1.0;
	A[N::B2][4] += 1.0;
	for (int i = 0; i < 4; i++) A[i][i] += 1e-12;
	for (int c0 = 0; c0 < 4; c0++) {
		int piv = c0;
		for (int r = c0 + 1; r < 4; r++) if (std::abs(A[r][c0]) > std::abs(A[piv][c0])) piv = r;
		std::swap_ranges(A[c0], A[c0] + 5, A[piv]);
		for (int r = c0 + 1; r < 4; r++) {
			cx fct = A[r][c0] / A[c0][c0];
			for (int k = c0; k < 5; k++) A[r][k] -= fct * A[c0][k];
		}
	}
	for (int r = 3; r >= 0; r--) {
		cx s = A[r][4];
		for (int k = r + 1; k < 4; k++) s -= A[r][k] * A[k][4];
		A[r][4] = s / A[r][r];
	}
	hB1 = A[N::B1][4];
	hC2 = A[N::C2][4];
}

static void testSpectrum() {
	printf("spectrum\n");
	N n;
	n.microplasma = false;                     // the linear, shot-noise-only model: the small-signal oracle applies to it
	n.seed(12345);
	n.start(192000.0);
	for (int i = 0; i < 40000; i++) n.step();
	const int L = 4096, SEG = 120;
	std::vector<double> b1(L * SEG), c2(L * SEG);
	for (int i = 0; i < L * SEG; i++) { n.step(); b1[i] = n.ckt.v[N::B1]; c2[i] = n.ckt.v[N::C2]; }
	auto psd = [&](const std::vector<double>& x, double f) {
		double acc = 0;
		for (int s = 0; s < SEG; s++) {
			double mu = 0;
			for (int i = 0; i < L; i++) mu += x[s * L + i];
			mu /= L;
			double re = 0, im = 0;
			for (int i = 0; i < L; i++) {
				double w = 0.5 - 0.5 * std::cos(2 * M_PI * i / L), ph = 2 * M_PI * f * i / 192000.0;
				double v = (x[s * L + i] - mu) * w;
				re += v * std::cos(ph); im += v * std::sin(ph);
			}
			acc += re * re + im * im;
		}
		return acc / SEG;
	};
	double worstB = 0, worstC = 0;
	cx h1k, c1k;
	// the oracle is evaluated at the mean operating point the noisy run sits at: use the quiet circuit's
	N q;
	q.start(192000.0);
	transfer(q, 1000.0, h1k, c1k);
	double refB = psd(b1, 1000.0) / std::norm(h1k), refC = psd(c2, 1000.0) / std::norm(c1k);
	(void)refC;
	for (double f : { 60., 120., 250., 500., 2000., 4000., 8000., 16000. }) {
		cx hb, hc;
		transfer(q, f, hb, hc);
		double dB = 10 * std::log10(psd(b1, f) / (refB * std::norm(hb)));
		double dC = 10 * std::log10(psd(c2, f) / (psd(c2, 1000.0) / std::norm(c1k) * std::norm(hc)));
		worstB = std::fmax(worstB, std::fabs(dB));
		worstC = std::fmax(worstC, std::fabs(dC));
	}
	printf("    worst PSD shape difference from the independent small-signal solve: B1 %.2f dB, C2 %.2f dB\n", worstB, worstC);
	check("the simulated noise at T1's base has the circuit's own spectrum within 1.5 dB, 60 Hz to 16 kHz", worstB < 1.5);
	check("and so does the noise at T2's collector", worstC < 1.5);
	// the shape itself: a hump in the low hundreds of Hz to 1 kHz, falling above it
	cx hb1, hb2, hb3, hc;
	transfer(q, 120.0, hb1, hc); transfer(q, 700.0, hb2, hc); transfer(q, 6000.0, hb3, hc);
	check("the circuit's noise is band-limited: 700 Hz is 10 dB above 120 Hz and above 6 kHz", std::abs(hb2) > 3.0 * std::abs(hb1) && std::abs(hb2) > 3.0 * std::abs(hb3));
}

static void testNoiseCurrent() {
	printf("the noise current\n");
	N n;
	n.microplasma = false;
	n.seed(99);
	n.start(192000.0);
	for (int i = 0; i < 20000; i++) n.step();
	double sum2 = 0, pred = 0;
	int M = 200000;
	for (int i = 0; i < M; i++) {
		double ib = -n.ckt.diodeCurrent(n.junction);
		if (ib < 0) ib = 0;
		pred += 2.0 * N::kQ * ib * N::kM * N::kM * n.rate * 0.5;
		n.step();
		double in = n.ckt.e[n.noiseSrc].p0;
		sum2 += in * in;
	}
	printf("    injected variance / shot-noise-times-M^2 prediction: %.3f\n", sum2 / pred);
	check("the injected current's variance is 2 q I M^2 (rate/2), within 5 %", std::fabs(sum2 / pred - 1.0) < 0.05);
	N q;
	q.start(192000.0);
	double lo = 1e9, hi = -1e9;
	for (int i = 0; i < 20000; i++) { double v = q.step(false); lo = std::fmin(lo, v); hi = std::fmax(hi, v); }
	check("with the noise off the circuit is quiet (B1 steady to 1 uV)", hi - lo < 1e-6);
}

static void testSource() {
	printf("the source\n");
	// level, rate independence, statistics, determinism
	double rms[5];
	double rates[5] = { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 };
	for (int r = 0; r < 5; r++) {
		AvalancheSource s(0x5EAF00Du, N::PERCUSSIVE, false);     // the shot-noise-only source
		s.setRate(rates[r]);
		for (int i = 0; i < (int)rates[r] / 2; i++) s.next();
		double acc = 0, m = 0, m4 = 0;
		int n = (int)(rates[r] * 3);
		for (int i = 0; i < n; i++) { double x = s.next(); acc += x * x; m += x; m4 += x * x * x * x; }
		rms[r] = std::sqrt(acc / n);
		if (r == 1) {
			double var = acc / n - (m / n) * (m / n);
			check("the output has no DC (mean under 2 % of the rms)", std::fabs(m / n) < 0.02 * rms[r]);
			double kurt = (m4 / n) / (var * var);
			printf("    kurtosis %.2f\n", kurt);
			check("and is Gaussian-ish (kurtosis 2.7 to 3.5; the amplifier is still linear here)", kurt > 2.7 && kurt < 3.5);
			check("its level matches the white noise it replaced (rms 0.577 at 48 kHz) within 3 %", std::fabs(rms[r] / 0.57735 - 1.0) < 0.03);
		}
	}
	double lo = *std::min_element(rms, rms + 5), hi = *std::max_element(rms, rms + 5);
	printf("    rms across 44.1 to 192 kHz: %.4f .. %.4f (%.2f dB)\n", lo, hi, 20 * std::log10(hi / lo));
	check("the level is the same at every sample rate to 0.4 dB", 20 * std::log10(hi / lo) < 0.4);
	AvalancheSource a, b, c(77u);
	a.setRate(48000.0); b.setRate(48000.0); c.setRate(48000.0);
	bool same = true, diff = false;
	for (int i = 0; i < 20000; i++) {
		float x = a.next(), y = b.next(), z = c.next();
		if (x != y) same = false;
		if (x != z) diff = true;
	}
	check("two sources with the same seed are bit-identical", same);
	check("and a different seed gives a different stream", diff);
	a.reset();
	AvalancheSource d;
	d.setRate(48000.0);
	bool back = true;
	for (int i = 0; i < 5000; i++) if (a.next() != d.next()) back = false;
	check("reset() returns to the start of the stream", back);
}

int main() {
	testDc();
	testSpectrum();
	testNoiseCurrent();
	testSource();
	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}

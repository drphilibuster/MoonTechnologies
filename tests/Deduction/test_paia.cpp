// The PAiA 2720-3L circuit (PaiaCircuit.hpp, solved by Mna.hpp) against independent oracles:
// KCL residuals computed from the component equations written out again here, and a
// hybrid-pi small-signal AC solve built from the same schematic by hand.

#include "../../src/Deduction/PaiaCircuit.hpp"

#include <algorithm>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

using namespace deduction;
typedef std::complex<double> cx;
typedef PaiaCircuit P;

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

// --- the oracle's own device equations ---------------------------------------------------
static const double Vt = 0.025852;
struct Bjt { double Ic, Ib, gm, gpi, gmu, go; };
static Bjt bjt(double vbe, double vbc) {
	// Ebers-Moll with the Early factor, evaluated from first principles here
	double Is = P::kIs, BF = P::kBF, BR = P::kBR, VA = P::kVAF;
	double ef = std::exp(vbe / Vt), er = std::exp(vbc / Vt);
	double f = 1.0 - vbc / VA;
	Bjt b;
	b.Ic = Is * (ef - er) * f - Is / BR * (er - 1.0);
	b.Ib = Is / BF * (ef - 1.0) + Is / BR * (er - 1.0);
	// small-signal by centred differences, so nothing is shared with the solver's algebra
	double h = 1e-6;
	auto ic = [&](double a, double c) { return Is * (std::exp(a / Vt) - std::exp(c / Vt)) * (1.0 - c / VA) - Is / BR * (std::exp(c / Vt) - 1.0); };
	auto ib = [&](double a, double c) { return Is / BF * (std::exp(a / Vt) - 1.0) + Is / BR * (std::exp(c / Vt) - 1.0); };
	b.gm  = (ic(vbe + h, vbc) - ic(vbe - h, vbc)) / (2 * h);       // dIc/dVbe
	b.go  = (ic(vbe, vbc + h) - ic(vbe, vbc - h)) / (2 * h);       // dIc/dVbc
	b.gpi = (ib(vbe + h, vbc) - ib(vbe - h, vbc)) / (2 * h);       // dIb/dVbe
	b.gmu = (ib(vbe, vbc + h) - ib(vbe, vbc - h)) / (2 * h);       // dIb/dVbc
	return b;
}
static double diodeI(double v) { return P::kDiodeIs * (std::exp(v / P::kDiodeNVt) - 1.0); }

// KCL residual at every node at DC (capacitors open), amps leaving. Written out by hand.
static void residualDc(const P& p, double vin, double vctl, double* F) {
	const double* v = p.ckt.v;
	double Vcc = P::kVcc, R11 = 1.0 / p.ckt.e[p.trimmer].p0;
	Bjt q1 = bjt(v[P::B1] - v[P::E1], v[P::B1] - v[P::C]);
	Bjt q2 = bjt(v[P::C] - v[P::E2], v[P::C] - v[P::VCCF]);
	(void)R11;
	F[P::B1]   = (v[P::B1] - vin) / 680e3 + (v[P::B1] - v[P::VCCF]) / 150e3 + v[P::B1] / 27e3
	             + q1.Ib + (v[P::B1] - v[P::M1]) / 68e3;
	F[P::VCCF] = (v[P::VCCF] - Vcc) / 1e3 + (v[P::VCCF] - v[P::B1]) / 150e3 + (v[P::VCCF] - v[P::C]) / 6.8e3
	             + q2.Ic;
	F[P::C]    = (v[P::C] - v[P::VCCF]) / 6.8e3 + q1.Ic + q2.Ib;
	F[P::E1]   = v[P::E1] / 2.2e3 - (q1.Ic + q1.Ib);
	F[P::F]    = 0.0;                                      // only C5 reaches it at DC
	F[P::E2]   = v[P::E2] / 4.7e3 - (q2.Ic + q2.Ib);
	F[P::OUT]  = v[P::OUT] / P::kLoad;
	F[P::M1]   = (v[P::M1] - v[P::B1]) / 68e3 + (v[P::M1] - v[P::X]) / 68e3;
	F[P::M2]   = (v[P::M2] - vctl) / 220e3 + diodeI(v[P::M2]);
	F[P::X]    = (v[P::X] - v[P::M1]) / 68e3;
}

// Independent small-signal solve: hybrid-pi from the oracle's own derivatives, complex nodal
// analysis by Gaussian elimination, written against the schematic and not the solver.
static cx acOracle(const P& p, double f) {
	const double* v = p.ckt.v;
	double w = 2 * M_PI * f;
	cx j(0, 1);
	const int N = P::kNodes;
	cx A[N][N + 1] = {};
	auto G = [&](int a, int b, cx y) {
		if (a >= 0) { A[a][a] += y; if (b >= 0) A[a][b] -= y; }
		if (b >= 0) { A[b][b] += y; if (a >= 0) A[b][a] -= y; }
	};
	double R11 = 1.0 / p.ckt.e[p.trimmer].p0;
	// Vin is a unit source behind R1: Norton equivalent at B1
	G(P::B1, -1, 1.0 / 680e3);
	A[P::B1][N] += 1.0 / 680e3;
	G(P::VCCF, -1, 1.0 / 1e3);
	G(P::VCCF, -1, j * w * 100e-6);
	G(P::VCCF, P::B1, 1.0 / 150e3);
	G(P::B1, -1, 1.0 / 27e3);
	G(P::VCCF, P::C, 1.0 / 6.8e3);
	G(P::E1, -1, 1.0 / 2.2e3);
	G(P::E1, P::F, j * w * 33e-6);
	G(P::F, -1, 1.0 / R11);
	G(P::E2, -1, 1.0 / 4.7e3);
	G(P::E2, P::OUT, j * w * 0.1e-6);
	G(P::OUT, -1, 1.0 / P::kLoad);
	G(P::B1, P::M1, 1.0 / 68e3);
	G(P::M1, P::X, 1.0 / 68e3);
	G(P::M1, -1, j * w * 0.22e-6);
	G(P::B1, P::M2, j * w * 1e-9);
	G(P::M2, P::X, j * w * 1e-9);
	G(P::M2, -1, 1.0 / 220e3);                                        // R10 to the (AC-dead) control
	double gd = P::kDiodeIs / P::kDiodeNVt * std::exp(v[P::M2] / P::kDiodeNVt);
	G(P::M2, -1, gd);
	G(P::X, P::C, j * w * 0.1e-6);
	// the transistors: current into the collector gm*dVbe + go*dVbc, into the base gpi*dVbe + gmu*dVbc
	struct Q { int c, b, e; };
	Q qs[2] = { { P::C, P::B1, P::E1 }, { P::VCCF, P::C, P::E2 } };
	for (int k = 0; k < 2; k++) {
		int c = qs[k].c, b = qs[k].b, e = qs[k].e;
		Bjt t = bjt(v[b] - v[e], v[b] - v[c]);
		// leaving: collector Ic(vbe,vbc), base Ib(vbe,vbc), emitter -(Ic+Ib); vbe = Vb-Ve, vbc = Vb-Vc
		double dvbe[3] = { 0, 1, -1 }, dvbc[3] = { -1, 1, 0 };     // wrt (Vc, Vb, Ve)
		int nodes[3] = { c, b, e };
		double rowIc[3], rowIb[3];
		for (int x = 0; x < 3; x++) {
			rowIc[x] = t.gm * dvbe[x] + t.go * dvbc[x];
			rowIb[x] = t.gpi * dvbe[x] + t.gmu * dvbc[x];
		}
		for (int x = 0; x < 3; x++) {
			if (nodes[x] < 0) continue;
			A[c][nodes[x]] += rowIc[x];
			A[b][nodes[x]] += rowIb[x];
			A[e][nodes[x]] -= rowIc[x] + rowIb[x];
		}
	}
	// the supply node VCCF has Q2's collector, but VCCF is an unknown here too (R2, C1 filter)
	for (int i = 0; i < N; i++) A[i][i] += 1e-12;
	for (int c0 = 0; c0 < N; c0++) {
		int piv = c0;
		for (int r = c0 + 1; r < N; r++) if (std::abs(A[r][c0]) > std::abs(A[piv][c0])) piv = r;
		std::swap_ranges(A[c0], A[c0] + N + 1, A[piv]);
		for (int r = c0 + 1; r < N; r++) {
			cx fct = A[r][c0] / A[c0][c0];
			for (int jx = c0; jx <= N; jx++) A[r][jx] -= fct * A[c0][jx];
		}
	}
	for (int r = N - 1; r >= 0; r--) {
		cx s = A[r][N];
		for (int jx = r + 1; jx < N; jx++) s -= A[r][jx] * A[jx][N];
		A[r][N] = s / A[r][r];
	}
	return A[P::OUT][N];
}

static double db(cx z) { return 20 * std::log10(std::abs(z)); }

static void testDc() {
	printf("operating point\n");
	bool kcl = true;
	for (double vc : { 0.0, 0.5, 1.0, 3.0, 12.0 }) {
		P p;
		p.start(vc, 1.0 / 96000);
		double F[P::kNodes];
		residualDc(p, 0.0, vc, F);
		for (int i = 0; i < P::kNodes; i++) if (std::fabs(F[i]) > 1e-9) { kcl = false; printf("    vc %.1f node %d residual %.3g A\n", vc, i, F[i]); }
	}
	check("KCL holds at every node of the DC solution to 1 nA (5 control voltages)", kcl);
	P p;
	p.start(0.0, 1.0 / 96000);
	checkv("Q1's base sits near the 150k/27k divider less its base current (1.4-1.5 V)", p.ckt.v[P::B1], 1.475, 0.1);
	check("Q1 conducts but is not saturated (collector above the emitter by > 1 V)", p.ckt.v[P::C] - p.ckt.v[P::E1] > 1.0);
	checkv("the diode's node is the diode's drop at 12 V control", [] { P q; q.start(12.0, 1.0 / 96000); return q.ckt.v[P::M2]; }(), 0.44, 0.03);
	P q;
	q.start(12.0, 1.0 / 96000);
	double id = (12.0 - q.ckt.v[P::M2]) / 220e3;
	checkv("and the control current is (12 - Vd) / 220k", id, 52.5e-6, 2e-6);
}

static void testAc() {
	printf("small-signal response\n");
	double worst = 0;
	for (double vc : { 0.0, 0.6, 1.0, 2.0, 5.0, 12.0 })
		for (double r11 : { 1.0, 100.0, 1000.0 }) {
			P p;
			p.setTrimmer(r11);
			p.start(vc, 1.0 / 96000);
			for (double f : { 30., 100., 300., 700., 1500., 3000., 5000., 9000., 16000. }) {
				double a = db(p.ckt.acGain(f, P::VIN, P::OUT)), b = db(acOracle(p, f));
				worst = std::fmax(worst, std::fabs(a - b));
			}
		}
	printf("    worst solver-vs-oracle AC difference %.4f dB\n", worst);
	check("the solver's linearisation matches an independent hybrid-pi solve to 0.02 dB", worst < 0.02);

	// the control moves the peak, and the peak is a peak
	double lastPeak = 0;
	bool moves = true, isPeak = true;
	for (double vc : { 0.5, 1.0, 2.0, 5.0, 12.0 }) {
		P p;
		p.setTrimmer(100.0);
		p.start(vc, 1.0 / 96000);
		double best = -1e9, bf = 0;
		for (double f = 200; f < 15000; f *= 1.03) {
			double g = db(p.ckt.acGain(f, P::VIN, P::OUT));
			if (g > best) { best = g; bf = f; }
		}
		if (bf <= lastPeak) moves = false;
		lastPeak = bf;
		if (db(p.ckt.acGain(bf * 2, P::VIN, P::OUT)) > best - 3.0) isPeak = false;
	}
	check("the peak frequency rises with the control voltage", moves);
	check("the response is down 3 dB or more an octave above the peak", isPeak);
	// R11 sets the loop gain: less resistance, bigger peak
	P a, b;
	a.setTrimmer(1000.0); a.start(2.0, 1.0 / 96000);
	b.setTrimmer(1.0);    b.start(2.0, 1.0 / 96000);
	double pa = -1e9, pb = -1e9;
	for (double f = 300; f < 8000; f *= 1.05) {
		pa = std::fmax(pa, db(a.ckt.acGain(f, P::VIN, P::OUT)));
		pb = std::fmax(pb, db(b.ckt.acGain(f, P::VIN, P::OUT)));
	}
	check("R11 trades gain: 1 ohm peaks 10 dB or more above 1k", pb - pa > 10.0);
}

static double sineGain(double vc, double r11, double f, double amp, double fs, double* asym = 0) {
	P p;
	p.setTrimmer(r11);
	p.start(vc, 1.0 / fs);
	int N = (int)(fs * 0.3);
	double mx = -1e9, mn = 1e9;
	for (int n = 0; n < N; n++) {
		double y = p.step(amp * std::sin(2 * M_PI * f * n / fs), vc);
		if (n > N / 2) { mx = std::fmax(mx, y); mn = std::fmin(mn, y); }
	}
	if (asym) *asym = (mx + mn) / (mx - mn);
	return 20 * std::log10(0.5 * (mx - mn) / amp);
}

static void testTime() {
	printf("time domain\n");
	// a small sine through the stepping solver reproduces the AC solve (trapezoid warping
	// allowed for: at 192 kHz it is well under 0.1 dB up to 5 kHz)
	double worst = 0;
	for (double vc : { 1.0, 3.0 }) {
		P p;
		p.setTrimmer(100.0);
		p.start(vc, 1.0 / 192000);
		for (double f : { 200., 1000., 2500., 4000. }) {
			double want = db(p.ckt.acGain(f, P::VIN, P::OUT));
			double got = sineGain(vc, 100.0, f, 0.02, 192000);
			worst = std::fmax(worst, std::fabs(got - want));
		}
	}
	printf("    worst time-vs-AC difference %.3f dB\n", worst);
	check("a 20 mV sine through the time-stepper matches the AC response within 0.15 dB", worst < 0.15);

	// the circuit is nonlinear: a hot input compresses and the transistor clips one side first
	double small = sineGain(2.0, 100.0, 1000, 0.02, 192000);
	double asym = 0;
	double big = sineGain(2.0, 100.0, 1000, 5.0, 192000, &asym);
	printf("    small-signal gain %.2f dB, 5 V input %.2f dB\n", small, big);
	check("a 5 V input compresses the gain by more than 2 dB", small - big > 2.0);
	check("and the output is lopsided (the stage clips asymmetrically)", std::fabs(asym) > 0.05);

	// the diode in the twin-T is a signal element too: its own distortion depends on the control
	double a1 = 0, a2 = 0;
	sineGain(0.6, 100.0, 1000, 0.5, 192000, &a1);
	sineGain(12.0, 100.0, 1000, 0.5, 192000, &a2);
	printf("    asymmetry at 0.6 V control %.3f, at 12 V %.3f\n", a1, a2);
	check("the output changes with the control at a fixed hot input (the diode is in the signal)", std::fabs(a1 - a2) > 0.002);
}

static void testTuning() {
	printf("tuning table\n");
	const PaiaTuning& t = PaiaTuning::get();
	bool mono = true;
	for (int i = 1; i < PaiaTuning::N; i++) if (t.hz[i] < t.hz[i - 1] || t.volts[i] <= t.volts[i - 1]) mono = false;
	check("peak frequency is monotone in the control voltage", mono);
	check("it reaches from a few hundred Hz to about 5 kHz", t.hz[0] > 200 && t.hz[0] < 800 && t.hz[PaiaTuning::N - 1] > 4000 && t.hz[PaiaTuning::N - 1] < 7000);
	bool inv = true;
	for (double f : { 600., 1000., 2000., 3000., 4500. }) {
		double vc = t.controlFor(f);
		P p;
		p.setTrimmer(100.0);
		p.start(vc, 1.0 / 96000);
		double best = -1e9, bf = 0;
		for (double ff = 200; ff < 15000; ff *= 1.01) {
			double g = std::abs(p.ckt.acGain(ff, P::VIN, P::OUT));
			if (g > best) { best = g; bf = ff; }
		}
		if (std::fabs(std::log(bf / f)) > 0.06) { inv = false; printf("    asked %g Hz, control %.3f V peaks at %g Hz\n", f, vc, bf); }
	}
	check("controlFor() puts the peak where it was asked, within 6 %", inv);
	check("outside the board's reach it clamps", t.controlFor(20.0) == t.volts[0] && t.controlFor(20000.0) == t.volts[PaiaTuning::N - 1]);
}

static void testRobust() {
	printf("robustness\n");
	bool ok = true, finite = true;
	srand(11);
	for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 }) {
		PaiaVoice v;
		for (int n = 0; n < (int)rate; n++) {
			double x = (rand() % 100 < 3) ? ((rand() % 2) ? 1.0 : -1.0) * 2.0 : std::sin(n * 0.05);
			double fc = 20.0 * std::pow(1000.0, (rand() % 1000) / 1000.0);
			double y = v.process(x, fc, (n / 5000) % 2 ? 1.0 : 0.0, (rand() % 1000) / 1000.0, rate);
			if (!v.ckt.ok) ok = false;
			if (std::isnan(y) || std::fabs(y) > 10.0) finite = false;
		}
	}
	check("noise bursts, jumping cutoff, RES slammed, drive random: the solver never fails to converge", ok);
	check("and the output stays finite and under 50 V", finite);
}

int main() {
	testDc();
	testAc();
	testTime();
	testTuning();
	testRobust();
	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}

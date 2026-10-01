// Depreciation: the analog side (src/Pcm70Analog.hpp): converter filter responses, the host <-> machine rate conversions that carry them, the level detector, the
// mix stage and the DC blocker. No ROM is needed.
#include "../../src/Pcm70Analog.hpp"
#include <cstdio>
#include <random>

using namespace pcm70::analog;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)
static double db(double x) { return 20 * std::log10(x); }

// the analog responses against scipy's (signal.ellip(9, 0.01, 49, 2 pi 14480, analog=True), hold sinc and aperture biquad), complex, 12 digits
static void testResponses() {
	struct Row { double f, re, im; };
	static const Row IN[] = { {100.0, 0.999659523581, -0.0260141959431}, {1000.0, 0.966080883953, -0.257488799121}, {5000.0, 0.222881236721, -0.974063224242}, {9000.0, -0.890018091847, -0.453681304923}, {12000.0, -0.533767455832, 0.845187297052}, {13250.0, 0.407746445887, 0.912531510202}, {14125.0, 0.996577482355, 0.0673414153691}, {14480.0, 0.811500972138, -0.582379803944}, {14875.0, -0.268444596717, -0.71687886643}, {15125.0, -0.333339697458, -0.112787378793}, {15375.0, -0.121779179405, 0.0293512708664}, {16000.0, -0.000888787317916, 0.00135944983325}, {16900.0, -8.95525795849e-07, -0.000595660133336} };
	static const Row OUT[] = { {100.0, 0.999401222242, -0.0347744024997}, {1000.0, 0.940477902112, -0.341618422805}, {5000.0, -0.25181600785, -0.977867351639}, {9000.0, -0.816961733124, 0.476343732122}, {12000.0, 0.591154893931, 0.478658036557}, {13250.0, 0.584506563358, -0.303367642955}, {14125.0, -0.0605821898042, -0.583088826397}, {14480.0, -0.413940021584, -0.373813559136}, {14875.0, -0.329102970849, 0.233854354233}, {15125.0, -0.00696141562492, 0.178729799542}, {15375.0, 0.0320988235518, 0.0522533726358}, {16000.0, 0.000707791855763, 0.000143972811233}, {16900.0, -0.000205337812033, 0.00010263652146} };
	double worst = 0;
	for (const Row& r : IN) worst = std::fmax(worst, std::abs(hIn(r.f) - cd(r.re, r.im)));
	for (const Row& r : OUT) worst = std::fmax(worst, std::abs(hOut(r.f) - cd(r.re, r.im)));
	std::printf("analog responses vs scipy: worst complex difference %.2e\n", worst);
	CHECK(worst < 1e-9);
	CHECK(std::abs(hIn(0.0) - 1.0) < 1e-6);                                // unity DC gain
	CHECK(db(std::abs(hIn(15125.0))) < -9 && db(std::abs(hIn(16000.0))) < -48);   // the cliff, and the 49 dB stopband
}

// least-squares complex gain of a sinusoid of frequency f in y[0..n) sampled at fs, relative to sin(2 pi f t + 0), t = (i + t0) / fs
static cd gainAt(const std::vector<double>& y, double fs, double f, double t0) {
	double a = 0, b = 0, saa = 0, sbb = 0, sab = 0, ya = 0, yb = 0;
	for (size_t i = 0; i < y.size(); i++) {
		const double w = 0.5 - 0.5 * std::cos(2 * PI * (i + 0.5) / y.size()), ph = 2 * PI * f * ((double)i + t0) / fs, s = std::sin(ph), c = std::cos(ph);
		saa += w * s * s; sbb += w * c * c; sab += w * s * c; ya += w * y[i] * s; yb += w * y[i] * c;
	}
	const double det = saa * sbb - sab * sab; a = (ya * sbb - yb * sab) / det; b = (yb * saa - ya * sab) / det;   // y ~ a sin + b cos
	return cd(a, b);                                                       // a sin + b cos = Re[(a - jb) e^{j ph}]... gain G with y = Im[G e^{j ph}] = Re(G) sin + Im(G) cos
}

static const double FREQS[] = { 100, 1000, 3000, 5000, 7000, 9000, 11000, 12000, 13250, 14000 };

static void testInput(double host) {
	InputStage st; st.init(host);
	double worstDb = 0, worstPh = 0;
	for (double f : FREQS) {
		st = InputStage(); st.init(host);
		const int nh = (int)(host * 0.12); std::vector<double> y; double o; long long nOut = 0;
		for (int k = 0; k < nh; k++) { st.push(std::sin(2 * PI * f * k / host)); while (st.pull(o)) { if (nOut++ >= (long long)(0.04 * FS)) y.push_back(o); } }
		const cd G = gainAt(y, FS, f, (double)(long long)(0.04 * FS));
		const cd H = hIn(f);
		const double e = db(std::abs(G) / std::abs(H)), ph = std::arg(G / H);
		worstDb = std::fmax(worstDb, std::fabs(e)); worstPh = std::fmax(worstPh, std::fabs(ph));
	}
	std::printf("input stage, host %6.0f Hz: worst magnitude error %.4f dB, worst phase error %.4f rad (kernel %d taps)\n", host, worstDb, worstPh, st.pp.taps);
	CHECK(worstDb < 0.05); CHECK(worstPh < 0.01);
}

static void testOutput(double host) {
	double worstDb = 0, worstPh = 0; int taps = 0;
	for (double f : FREQS) {
		OutputStage st; st.init(host, 0); taps = st.pp.taps;
		const int nn = (int)(FS * 0.12); std::vector<double> y; double l, r; long long nOut = 0;
		const long long skip = (long long)(0.04 * host);
		for (int n = 0; n < nn; n++) { const double w = std::sin(2 * PI * f * n / FS); st.push(w, -w); while (st.pull(l, r)) { if (nOut++ >= skip) y.push_back(l); } }
		const cd G = gainAt(y, host, f, (double)skip);
		const cd H = hOut(f);
		worstDb = std::fmax(worstDb, std::fabs(db(std::abs(G) / std::abs(H)))); worstPh = std::fmax(worstPh, std::fabs(std::arg(G / H)));
	}
	std::printf("output stage, host %6.0f Hz: worst magnitude error %.4f dB, worst phase error %.4f rad (kernel %d taps)\n", host, worstDb, worstPh, taps);
	CHECK(worstDb < 0.05); CHECK(worstPh < 0.01);
}

// the machine clock and the host clock never drift: after 10 s the stages have made exactly the right number of samples
static void testRatio(double host) {
	InputStage in; in.init(host); OutputStage out; out.init(host, 0);
	const long long nh = (long long)host * 10; long long ni = 0; double o, l, r;
	for (long long k = 0; k < nh; k++) { in.push(0.0); while (in.pull(o)) { ni++; out.push(0.0, 0.0); } }
	const double expect = 10.0 * FS - (double)(in.A + 1) * FS / host;                   // minus the look-ahead still waiting
	std::printf("rate ratio, host %6.0f Hz: %lld machine samples in 10 s (expected %.1f)\n", host, ni, expect);
	CHECK(std::fabs((double)ni - expect) < 2.0);
	long long no = 0; while (out.pull(l, r)) no++;
	CHECK(no > 0);
}

static void testDetector() {
	LevelDetector d;
	CHECK(d.update(0.0) == 0);
	CHECK(d.update(0.5) == (uint8_t)(0.5 * 403.5));                        // 201
	CHECK(d.update(1.0) == 255);                                           // clamps: the ADC saturates above 0.63 of full scale
	CHECK(d.leds() == 5 || d.leds() == 4);
	{ LevelDetector n; n.update(-0.9); CHECK(n.peak == 0.0); }             // positive peaks only
	// droop: an independent integration of dV/dt = -(V+15)/(R22 C42), V = 2.5 x peak
	LevelDetector h; h.update(0.5); double V = 2.5 * 0.5; const double dt = 1e-4; const int steps = 20000;      // 2 s
	for (int i = 0; i < steps; i++) { const double k1 = -(V + 15.0) / 220.0; const double k2 = -(V + dt / 2 * k1 + 15.0) / 220.0; V += dt * k2; }
	for (long long i = 0; i < (long long)(2.0 * FS); i++) h.update(0.0);
	std::printf("detector: after 2 s hold voltage %.5f V (independent integration %.5f V)\n", h.holdVolts(), V);
	CHECK(std::fabs(h.holdVolts() - V) < 2e-3);
	LevelDetector l1, l2, l3, l4, l5; l1.update(1.2); l2.update(0.6); l3.update(0.5); l4.update(0.12); l5.update(0.01);
	CHECK(l1.leds() == 5 && l2.leds() == 4 && l3.leds() == 3 && l4.leds() == 1 && l5.leds() == 0);   // bar thresholds 2.76 V x (1, 1/2, 1/4, 1/8, 1/16); hold = 2.5 x peak
}

static void testMix() {
	MixStage m; m.init(48000.0);
	CHECK(std::fabs(MixStage::gainOf(0) - 1.0) < 1e-12);
	CHECK(std::fabs(db(MixStage::gainOf(255)) + 99.96) < 0.01);            // 255 codes x 0.392 dB
	m.cw = m.cd = 255; m.sw = m.sd = 255; m.setCodes(0, 255);
	double l = 0, r = 0; int n = 0; for (; n < 48000; n++) { m.process(0.0, 1.0, 1.0, l, r); if (m.sw < 255.0 * std::exp(-1.0)) break; }
	std::printf("mix: wet code falls to 1/e after %.2f ms (tau 5.4 ms)\n", n / 48.0);
	CHECK(std::fabs(n / 48.0 - 5.4) < 0.1);
	MixStage e; e.init(48000.0); e.sw = e.cw = 0; e.sd = e.cd = 0;                    // equal codes: dry and wet at equal level (kDry 1.05)
	e.process(0.5, 0.5, 0.5, l, r); CHECK(std::fabs(l - (0.5 + 0.5 * 1.05)) < 1e-12);
}

static void testDc() {
	DcBlock b; b.init(); double y = 0;
	for (int i = 0; i < (int)(FS * 5); i++) y = b.process(1.0);
	std::printf("DC blocker: 1.0 at DC leaves %.4f after 5 s\n", y); CHECK(std::fabs(y) < 0.1);
	DcBlock c; c.init(); double mx = 0; for (int i = 0; i < (int)(FS * 0.5); i++) { const double o = c.process(std::sin(2 * PI * 1000.0 * i / FS)); if (i > (int)FS * 0.25) mx = std::fmax(mx, std::fabs(o)); }
	CHECK(std::fabs(db(mx)) < 0.01);                                       // a 1 kHz tone passes unchanged
}

int main() {
	testResponses();
	const double hosts[] = { 44100.0, 48000.0, 96000.0, 192000.0 };
	for (double h : hosts) { testInput(h); testOutput(h); testRatio(h); }
	testDetector(); testMix(); testDc();
	if (failures) { std::printf("%d FAILED\n", failures); return 1; }
	std::printf("ok\n");
	return 0;
}

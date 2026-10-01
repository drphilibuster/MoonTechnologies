// "I AM O" (IAmO.hpp, solved by Mna.hpp) against independent oracles: the 2N5457 channel
// written out again here, a two-unknown Newton solve of the static circuit, and the complex
// divider for small signals.

#include "../../src/Garnishment/IAmO.hpp"

#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

using namespace garnishment;
typedef IAmO C;

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

// --- the oracle's own 2N5457 -------------------------------------------------------------
static double chan(double vgs, double vds) {        // vds >= 0
	double vov = vgs - C::kVto;
	if (vov <= 0) return 0;
	double cl = 1 + C::kLambda * vds;
	return vds < vov ? C::kBeta * vds * (2 * vov - vds) * cl : C::kBeta * vov * vov * cl;
}
// drain -> source current for any vd, vs (the channel is symmetric)
static double idrain(double vg, double vd, double vs) {
	return vd >= vs ? chan(vg - vs, vd - vs) : -chan(vg - vd, vs - vd);
}
static double dio(double v) { return C::kGateIs * (std::exp(v / C::kNVt) - 1.0); }

// Static circuit: unknowns Vg, Vd, with Vin and Vam fixed and the capacitor open.
static void staticSolve(double vin, double vam, double& vg, double& vd) {
	vg = 0; vd = 0;
	for (int it = 0; it < 500; it++) {
		auto F = [&](double g, double d, double* f) {
			f[0] = (g - vin) / 100e3 + dio(g) + dio(g - d);
			f[1] = (d - vam) / 100.0 + idrain(g, d, 0.0) - dio(g - d);
		};
		double f[2], fg[2], fd[2], h = 1e-7;
		F(vg, vd, f);
		F(vg + h, vd, fg);
		F(vg, vd + h, fd);
		double a = (fg[0] - f[0]) / h, b = (fd[0] - f[0]) / h, c = (fg[1] - f[1]) / h, d = (fd[1] - f[1]) / h;
		double det = a * d - b * c;
		double dg = (-f[0] * d + f[1] * b) / det, dd = (-a * f[1] + c * f[0]) / det;
		double lim = 0.3;
		dg = std::fmax(-lim, std::fmin(lim, dg));
		dd = std::fmax(-lim, std::fmin(lim, dd));
		vg += dg; vd += dd;
		if (std::fabs(dg) < 1e-13 && std::fabs(dd) < 1e-13) break;
	}
}

static void testChannel() {
	printf("the channel\n");
	// the solver's channel against the oracle's, current and both slopes
	bool match = true, cont = true;
	for (double vgs : { -3.0, -2.0, -1.5, -1.0, -0.3, 0.0, 0.4 })
		for (double vds : { 0.0, 0.01, 0.2, 0.9, 1.9, 2.0, 2.4, 6.0, 15.0 }) {
			double gm, gds;
			double i = mna::jfetId(vgs, vds, C::kBeta, C::kVto, C::kLambda, gm, gds);
			if (std::fabs(i - chan(vgs, vds)) > 1e-15) match = false;
			double h = 1e-6, g1, g2;
			double dgm = (mna::jfetId(vgs + h, vds, C::kBeta, C::kVto, C::kLambda, g1, g2) -
			              mna::jfetId(vgs - h, vds, C::kBeta, C::kVto, C::kLambda, g1, g2)) / (2 * h);
			double dds = (mna::jfetId(vgs, vds + h, C::kBeta, C::kVto, C::kLambda, g1, g2) -
			              mna::jfetId(vgs, vds > h ? vds - h : 0.0, C::kBeta, C::kVto, C::kLambda, g1, g2)) / (vds > h ? 2 * h : h);
			if (std::fabs(gm - dgm) > 1e-7 + 1e-4 * std::fabs(dgm)) match = false;
			if (std::fabs(gds - dds) > 1e-7 + 1e-4 * std::fabs(dds)) match = false;
		}
	check("current and slopes match an independent coding, over triode, the knee and saturation", match);
	// triode meets saturation at vds = vgs - Vto
	for (double vgs : { -1.0, 0.0 }) {
		double vk = vgs - C::kVto, g1, g2;
		double a = mna::jfetId(vgs, vk - 1e-9, C::kBeta, C::kVto, C::kLambda, g1, g2);
		double b = mna::jfetId(vgs, vk + 1e-9, C::kBeta, C::kVto, C::kLambda, g1, g2);
		if (std::fabs(a - b) > 1e-9) cont = false;
	}
	check("the current is continuous across the knee", cont);
	// datasheet anchors: IDSS 3 mA at vgs=0, |Yfs| 3000 umho
	double gm, gds;
	double idss = mna::jfetId(0.0, 15.0, C::kBeta, C::kVto, C::kLambda, gm, gds);
	checkv("IDSS at VDS 15 V is 3 mA (datasheet typical, to the lambda term)", idss, 3e-3, 0.2e-3);
	checkv("and |Yfs| there is 3000 umho", gm, 3e-3, 0.3e-3);
	checkv("output admittance at 15 V is the datasheet's 10 umho", gds, 10e-6, 1e-6);
	check("pinched off below VGS(off)", mna::jfetId(-2.1, 5.0, C::kBeta, C::kVto, C::kLambda, gm, gds) == 0.0);
}

static void testStatic() {
	printf("static transfer\n");
	double worst = 0;
	for (double vin : { 0.0, -1.0, -2.5, 1.0 })
		for (double vam = -5.0; vam <= 5.0; vam += 0.5) {
			C c;
			double tgt[C::kFixed] = { vin, vam };
			bool ok = c.ckt.solveDc(tgt, C::kFixed);
			double vg, vd;
			staticSolve(vin, vam, vg, vd);
			worst = std::fmax(worst, std::fabs(c.ckt.v[C::D] - vd));
			worst = std::fmax(worst, std::fabs(c.ckt.v[C::G] - vg));
			if (!ok) worst = 1e9;
		}
	printf("    worst solver-vs-oracle node voltage difference %.3g V\n", worst);
	check("the solver's DC solution matches a two-unknown Newton solve to 10 uV, gate -2.5..+1 V, carrier +-5 V", worst < 1e-5);
	// the negative carrier pinches the channel through the gate-drain junction
	double vg, vd;
	staticSolve(0.0, -5.0, vg, vd);
	printf("    -5 V carrier at gate 0: gate %.3f V, drain %.3f V\n", vg, vd);
	check("a -5 V carrier drags the gate below pinch-off through the gate-drain diode", vg < C::kVto);
	check("and the drain sits within 0.6 V of the carrier (the channel conducts backwards from the drain end)", vd < -4.4 && vd > -5.0);
}

static void testSmallSignal() {
	printf("small signal\n");
	typedef std::complex<double> cx;
	double worst = 0;
	for (double vg : { 0.0, -0.5, -1.0, -1.5, -1.9, -3.0 })
		for (double f : { 100.0, 1000.0, 5000.0 }) {
			double amp = 0.005, fs = 192000;
			C c;
			c.begin(fs);
			int N = (int)(fs * 0.4);
			double mx = -1e9, mn = 1e9;
			for (int n = 0; n < N; n++) {
				double y = c.process(vg, amp * std::sin(2 * M_PI * f * n / fs), fs);
				if (n > N / 2) { mx = std::fmax(mx, y); mn = std::fmin(mn, y); }
			}
			double got = 0.5 * (mx - mn) / amp;
			// the oracle: channel resistance at vds = 0, then the complex divider
			double vov = vg - C::kVto;
			double rds = vov > 0 ? 1.0 / (2 * C::kBeta * vov) : 1e12;
			cx zc = 1.0 / cx(0, 2 * M_PI * f * 100e-9), zl = zc + 100e3;
			cx zd = 1.0 / (1.0 / rds + 1.0 / zl);
			cx want = zd / (zd + 100.0) * 100e3 / zl;
			worst = std::fmax(worst, std::fabs(20 * std::log10(got / std::abs(want))));
		}
	printf("    worst gain difference %.4f dB\n", worst);
	check("5 mV through the stepper matches Rds/(Rds+100) with C1 and the load to 0.05 dB, gate 0 to -3 V", worst < 0.05);
	// the anchors in dB: gate at 0 is about -2.3 dB, pinched is 0
	C a;
	a.begin(48000);
	double mx = -1e9, mn = 1e9;
	for (int n = 0; n < 24000; n++) { double y = a.process(0.0, 0.01 * std::sin(2 * M_PI * 1000 * n / 48000.0), 48000); if (n > 12000) { mx = std::fmax(mx, y); mn = std::fmin(mn, y); } }
	checkv("with the gate at 0 V the stage costs 2.3 dB", 20 * std::log10(0.5 * (mx - mn) / 0.01), -2.29, 0.05);
}

static void testLarge() {
	printf("large signal\n");
	// at gate 0 a 5 V carrier is nearly unattenuated (the channel saturates near 3 mA)
	C c;
	c.begin(192000);
	double mx = -1e9, mn = 1e9, fs = 192000;
	for (int n = 0; n < (int)(fs * 0.3); n++) {
		double y = c.process(0.0, 5.0 * std::sin(2 * M_PI * 1000 * n / fs), fs);
		if (n > fs * 0.15) { mx = std::fmax(mx, y); mn = std::fmin(mn, y); }
	}
	double gain = 20 * std::log10(0.5 * (mx - mn) / 5.0);
	printf("    5 V carrier, gate 0 V: gain %.2f dB, peaks %.3f / %.3f\n", gain, mx, mn);
	check("a 5 V carrier is attenuated less than a small one (the channel runs out of current)", gain > -1.5 && gain < -0.2);
	// the stepper reproduces the static transfer, point by point, at 5 kHz
	C d;
	d.begin(192000);
	double worst = 0, mean = 0;
	int N = (int)(fs * 0.2), cnt = 0;
	static double vdSim[40000], vdOracle[40000];
	for (int n = 0; n < N; n++) {
		double am = 2.0 * std::sin(2 * M_PI * 5000 * n / fs);
		d.process(0.0, am, fs);
		double vg, vdo;
		staticSolve(0.0, am, vg, vdo);
		if (n > N / 2) { vdSim[cnt] = d.ckt.v[C::D]; vdOracle[cnt] = vdo; mean += vdo; cnt++; }
	}
	mean /= cnt;
	double msim = 0;
	for (int i = 0; i < cnt; i++) msim += vdSim[i];
	msim /= cnt;
	for (int i = 0; i < cnt; i++) worst = std::fmax(worst, std::fabs((vdSim[i] - msim) - (vdOracle[i] - mean)));
	printf("    worst drain waveform difference %.4f V (of a 2 V carrier)\n", worst);
	check("the time-stepped drain voltage follows the static transfer curve to 30 mV at 5 kHz (192 kHz solve)", worst < 0.03);
}

static void testRobust() {
	printf("robustness\n");
	bool ok = true, finite = true;
	srand(5);
	for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 }) {
		C c;
		for (int n = 0; n < (int)rate; n++) {
			double g = (rand() % 100 < 2) ? ((rand() % 2) ? 12.0 : -12.0) : -4.0 * (n % 20000) / 20000.0;
			double a = (rand() % 100 < 5) ? ((rand() % 2) ? 12.0 : -12.0) : 5.0 * std::sin(n * 0.07);
			double y = c.process(g, a, rate);
			if (!c.ok) ok = false;
			if (std::isnan(y) || std::fabs(y) > 15.0) finite = false;
		}
	}
	check("gate and carrier slammed to +-12 V at 44.1 to 192 kHz: the solver always converges", ok);
	check("and the output stays finite and within 15 V", finite);
}

int main() {
	testChannel();
	testStatic();
	testSmallSignal();
	testLarge();
	testRobust();
	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}

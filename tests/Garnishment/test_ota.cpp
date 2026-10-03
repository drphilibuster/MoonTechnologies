// The LM13700 (src/Lm13700.hpp) and the Day 2 "Simple 13700 Dual VCA" board built on it
// (src/Garnishment/OtaVca.hpp), against oracles that are NOT the solver's own equations:
//   * the static input network solved a second way (one unknown, a bracketed bisection, the
//     diode drops written with log10 and the datasheet's identity for Va),
//   * the small-signal response solved as a complex 3x3 linear system around that operating
//     point (including the input capacitor), then the buffer's Darlington gain,
//   * the datasheet's own numbers: gm, peak output current, Figure 10, Figure 5, buffer input
//     current, and Eq. 5/7.

#include "../../src/Garnishment/OtaVca.hpp"

#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>

using namespace garnishment;
namespace L = lm13700;
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

// --- the oracle's own constants, restated (not read from the header) ----------------------
static const double VT = 0.025852;
static const double IS = 4e-16;
static const double LN10 = 2.302585092994046;
static double vf(double i) { return VT * LN10 * std::log10(i / IS); }      // log10 form

// Static input network, one unknown. Base currents are left out (the solver includes them:
// a 0.4 uA effect, so the tolerances below are set by it). gs = 1/Rs, 0 for DC.
struct Dc { double v3, v4, va, id2, id3, iabc, iout; bool ok; };
static Dc dcOracle(double trim, double vth, double gs, double iabc,
                   double rtrim = 1000.0, double rbias = 12e3, double vp = 12.0) {
	double rb = trim * rtrim, rt = (1.0 - trim) * rtrim;
	Dc d = { 0, 0, 0, 0, 0, iabc, 0, false };
	auto G = [&](double v3, double* v4o, double* vao, double* i2o, double* i3o, bool* valid) {
		double id3 = v3 / rb + (v3 - vth) * gs;
		*valid = false;
		if (id3 <= 1e-12) return 1e9;
		double va = v3 + vf(id3);
		double id2 = (vp - va) / rbias - id3;
		if (id2 <= 1e-12) return -1e9;
		double v4 = va - vf(id2);
		*v4o = v4; *vao = va; *i2o = id2; *i3o = id3; *valid = true;
		return v4 / rt - id2;
	};
	// scan for a bracket, then bisect
	double lo = -2.0, hi = 6.0, prev = 0, vprev = lo;
	bool have = false;
	for (int k = 0; k <= 4000; k++) {
		double v3 = -2.0 + 8.0 * k / 4000.0, a, b, c, e; bool valid;
		double g = G(v3, &a, &b, &c, &e, &valid);
		if (!valid) { have = false; continue; }
		if (have && ((g < 0) != (prev < 0))) { lo = vprev; hi = v3; break; }
		prev = g; vprev = v3; have = true;
	}
	double glo, ghi, a, b, c, e; bool valid;
	glo = G(lo, &a, &b, &c, &e, &valid);
	ghi = G(hi, &a, &b, &c, &e, &valid);
	if ((glo < 0) == (ghi < 0)) return d;
	for (int k = 0; k < 200; k++) {
		double mid = 0.5 * (lo + hi);
		double g = G(mid, &a, &b, &c, &e, &valid);
		if ((g < 0) == (glo < 0)) { lo = mid; glo = g; } else hi = mid;
	}
	double v3 = 0.5 * (lo + hi);
	G(v3, &d.v4, &d.va, &d.id2, &d.id3, &valid);
	d.v3 = v3; d.ok = valid;
	d.iout = iabc * std::tanh((d.v3 - d.v4) / (2 * VT));
	return d;
}

// Complex 3x3 Gaussian elimination
static void solve3(cx A[3][3], cx b[3], cx x[3]) {
	for (int c = 0; c < 3; c++) {
		int p = c;
		for (int r = c + 1; r < 3; r++) if (std::abs(A[r][c]) > std::abs(A[p][c])) p = r;
		for (int k = 0; k < 3; k++) std::swap(A[c][k], A[p][k]);
		std::swap(b[c], b[p]);
		for (int r = c + 1; r < 3; r++) {
			cx f = A[r][c] / A[c][c];
			for (int k = c; k < 3; k++) A[r][k] -= f * A[c][k];
			b[r] -= f * b[c];
		}
	}
	for (int r = 2; r >= 0; r--) {
		cx s = b[r];
		for (int k = r + 1; k < 3; k++) s -= A[r][k] * x[k];
		x[r] = s / A[r][r];
	}
}

/** Small-signal voltage gain In -> Out of the board at frequency f, around the balanced or
    trimmed rest point, by the linearised node equations (the oracle). */
static cx acGain(double trim, double cv, double f, double* iabcOut = nullptr, double* restV5 = nullptr) {
	// Iabc by its own bisection on the pin-1 law, log10 form
	double lo = 1e-9, hi = 2e-3;
	for (int k = 0; k < 200; k++) {
		double mid = std::sqrt(lo * hi);
		double lhs = (cv + 12.0 - vf(mid) * 2.0) / 22e3;
		if (lhs > mid) lo = mid; else hi = mid;
	}
	double iabc = std::sqrt(lo * hi);
	if (iabcOut) *iabcOut = iabc;
	Dc d = dcOracle(trim, 0.0, 0.0, iabc);
	double rb = trim * 1000.0, rt = (1.0 - trim) * 1000.0;
	double gd3 = d.id3 / VT, gd2 = d.id2 / VT;
	cx zs = cx(27e3, -1.0 / (2 * M_PI * f * 470e-9));
	cx A[3][3] = {
		{ -gd3 - 1.0 / zs - 1.0 / rb, 0, gd3 },
		{ 0, -gd2 - 1.0 / rt, gd2 },
		{ gd3, gd2, -1.0 / 12e3 - gd2 - gd3 } };
	cx b[3] = { -1.0 / zs, 0, 0 };           // the source: vth = 1 into node 3 through zs
	cx x[3];
	solve3(A, b, x);
	// the pair's slope at the rest point: sech^2 of its offset (1 at the balance)
	double th = std::tanh((d.v3 - d.v4) / (2 * VT));
	cx iout = iabc / (2 * VT) * (1.0 - th * th) * (x[0] - x[1]);
	if (restV5) *restV5 = d.iout / (1.0 / 33e3 + 1.0 / (800.0 / iabc));
	double rl = 1.0 / (1.0 / 33e3 + 1.0 / (800.0 / iabc));
	// the buffer: Darlington follower, series 2 * VT / Ie, Ie from the rest level
	double vout0 = -1.4, ie = (vout0 + 12.0) / 4.7e3;
	for (int k = 0; k < 50; k++) {                 // rest level, own fixed point
		double ie12 = ie / 74.0;
		vout0 = -(VT * LN10 * std::log10(ie / IS)) - (VT * LN10 * std::log10(ie12 / IS));
		ie = (vout0 + 12.0) / 4.7e3;
	}
	double gbuf = 1.0 / (1.0 + 2.0 * (VT / ie) / 4.7e3);
	return iout * rl * gbuf;
}

static double restLevel() {                    // the buffer with pin 5 at 0 V, by bisection on Vout
	double lo = -12.0, hi = 0.0;
	for (int k = 0; k < 200; k++) {
		double v = 0.5 * (lo + hi), ie = (v + 12.0) / 4.7e3, ie12 = ie / 74.0;
		double v5 = -(ie / (74.0 * 74.0)) / (1.0 / 33e3 + 1.0 / (800.0 / 0.48e-3));     // the buffer's base current through 33k
		double want = v5 - VT * LN10 * std::log10(ie / IS) - VT * LN10 * std::log10(ie12 / IS);
		if (want > v) lo = v; else hi = v;
	}
	return 0.5 * (lo + hi);
}

static double measureGain(OtaVca& o, double amp, double f, double cv, double fs, double* mid = nullptr) {
	int N = (int)(fs * 0.9);
	double mx = -1e9, mn = 1e9;
	for (int n = 0; n < N; n++) {
		double y = o.process(amp * std::sin(2 * M_PI * f * n / fs), cv, fs);
		if (n > N * 2 / 3) { mx = std::fmax(mx, y); mn = std::fmin(mn, y); }
	}
	if (mid) *mid = 0.5 * (mx + mn);
	return 0.5 * (mx - mn) / amp;
}

// ------------------------------------------------------------------------------------------
static void testControlCurrent() {
	printf("Iabc from the CV through 22k\n");
	// Figure 10 (25 C): about 1.0 V at 0.1 uA, 1.5 V at 1 mA
	checkv("pin 1 sits 1.0 V above V- at 0.1 uA (Figure 10)", L::pin1Volts(0.1e-6), 1.0, 0.05);
	checkv("and about 1.5 V at 1 mA (Figure 10)", L::pin1Volts(1e-3), 1.5, 0.05);
	double worst = 0;
	for (double cv : { -11.0, -10.0, -6.0, -2.0, 0.0, 1.0, 5.0, 10.0, 12.0 }) {
		double i = L::iabcFromCv(cv, -12.0, 22e3);
		double resid = (cv + 12.0 - 2 * VT * LN10 * std::log10(i / IS)) / 22e3 - i;      // KCL, log10 form
		worst = std::fmax(worst, std::fabs(resid) / i);
	}
	printf("    worst KCL residual at pin 1: %.3g of Iabc\n", worst);
	check("Iabc satisfies (CV - V- - Vpin1)/22k = Iabc to 1e-9, -11..+12 V", worst < 1e-9);
	double i0 = L::iabcFromCv(0.0, -12.0, 22e3);
	checkv("0 V CV gives 0.48 mA (the channel is half open at rest)", i0, 0.480e-3, 0.01e-3);
	double i10 = L::iabcFromCv(10.0, -12.0, 22e3);
	checkv("+10 V CV gives 0.93 mA", i10, 0.933e-3, 0.01e-3);
	check("Iabc is zero at -11.6 V and below, and about 0.1 uA near -11 V",
	      L::iabcFromCv(-11.6, -12.0, 22e3) == 0.0 && L::iabcFromCv(-11.0, -12.0, 22e3) > 0.05e-6 &&
	      L::iabcFromCv(-11.0, -12.0, 22e3) < 0.2e-6);
	bool mono = true;
	double prev = -1;
	for (double cv = -11.5; cv <= 12.0; cv += 0.25) {
		double i = L::iabcFromCv(cv, -12.0, 22e3);
		if (i < prev) mono = false;
		prev = i;
	}
	check("monotonic in CV", mono);
	// a different supply moves the zero with it (the V- reference is the supply)
	checkv("on a -15 V supply 0 V CV gives (15 - 1.48)/22k", L::iabcFromCv(0.0, -15.0, 22e3), 0.6145e-3, 0.02e-3);
}

static void testStatic() {
	printf("the input network at rest and under DC drive\n");
	double worstV = 0, worstI = 0;
	for (double trim : { 0.2, 0.35, 0.5, 0.65, 0.8 })
		for (double vth : { 0.0, 0.5, -0.5, 3.0, -3.0, 8.0, -8.0 }) {
			double rs = 27e3;
			Dc o = dcOracle(trim, vth, 1.0 / rs, 480e-6);
			OtaVca b; b.trim = trim;
			L::InputState s;
			L::solveInput(b.inputNet(), vth, rs, 480e-6, s);
			if (!o.ok || !s.ok) { worstV = 1e9; continue; }
			worstV = std::fmax(worstV, std::fmax(std::fabs(s.v3 - o.v3), std::fabs(s.v4 - o.v4)));
			worstI = std::fmax(worstI, std::fabs(s.iout - o.iout) / 480e-6);
		}
	printf("    worst node-voltage difference to the one-unknown oracle %.3g V, Iout %.3g of Iabc\n", worstV, worstI);
	check("(+) and (-) pin voltages match the one-unknown oracle to 1 mV (base currents are the difference)", worstV < 1e-3);
	check("Iout matches the oracle's to 1.5 % of Iabc over trim 0.2..0.8 and +-8 V DC drive", worstI < 0.015);

	// KCL residual of the solver's own answer, computed here
	{
		OtaVca b; b.trim = 0.37;
		L::InputState s;
		double rs = 27e3, vth = 2.0, iabc = 480e-6;
		L::solveInput(b.inputNet(), vth, rs, iabc, s);
		double id3 = IS * std::exp((s.va - s.v3) / VT), id2 = IS * std::exp((s.va - s.v4) / VT);
		double sg = 1 / (1 + std::exp(-(s.v3 - s.v4) / VT));
		double f1 = id3 + (vth - s.v3) / rs - s.v3 / (0.37 * 1000) - iabc * sg / 625;
		double f2 = id2 - s.v4 / (0.63 * 1000) - iabc * (1 - sg) / 625;
		double f3 = (12 - s.va) / 12e3 - id2 - id3;
		printf("    KCL residuals %.3g %.3g %.3g A\n", f1, f2, f3);
		check("the solution satisfies all three node equations to 1 pA", std::fabs(f1) < 1e-12 && std::fabs(f2) < 1e-12 && std::fabs(f3) < 1e-12);
	}

	// the balance the trimmer is for
	{
		OtaVca b; b.trim = 0.5;
		b.begin(48000.0, 0.0);
		check("centred trimmer: the pins sit at the same voltage and Iout is zero at rest (to 1 nA)",
		      std::fabs(b.in.v3 - b.in.v4) < 1e-6 && std::fabs(b.in.iout) < 1e-9);
		checkv("each diode carries half of Id = (12 V - Va)/12k", b.in.id2, (12.0 - b.in.va) / 12e3 / 2, 5e-6);
		checkv("Id is about 0.92 mA", b.in.id2 + b.in.id3, 0.92e-3, 0.03e-3);
		// the rest level of Out is the buffer's two Vbe below pin 5's 0 V, and does not move with CV
		double r = restLevel();
		printf("    rest level of Out: model %.4f V, oracle %.4f V\n", b.out.vout, r);
		checkv("Out rests at the buffer's two Vbe below 0 V (independent bisection), about -1.4 V", b.out.vout, r, 0.01);
		bool flat = true;
		for (double cv : { -9.0, -3.0, 0.0, 6.0, 12.0 }) {
			OtaVca c; c.begin(48000.0, cv);
			if (std::fabs(c.out.vout - r) > 0.01) flat = false;
		}
		check("and it does not move with the CV when the pair is balanced (Iout is zero whatever Iabc is)", flat);
	}
	// off centre: an input offset current that scales with Iabc
	{
		bool sign = true, scales = true;
		for (double trim : { 0.4, 0.6 }) {
			OtaVca a; a.trim = trim; a.begin(48000.0, -6.0);
			OtaVca b; b.trim = trim; b.begin(48000.0, 6.0);
			Dc o = dcOracle(trim, 0.0, 0.0, 1.0);        // tanh(dV)  per unit Iabc
			double unit = o.iout;
			if ((a.in.iout > 0) != (unit > 0)) sign = false;
			double ra = a.in.iout / a.iabc, rb = b.in.iout / b.iabc;
			printf("    trim %.1f: Iout/Iabc %.4f at -6 V, %.4f at +6 V (oracle %.4f)\n", trim, ra, rb, unit);
			if (std::fabs(ra - unit) > 0.012 || std::fabs(rb - unit) > 0.012) scales = false;
		}
		check("an off-centre trimmer's offset has the oracle's sign (6 % of Iabc or so at 0.4 / 0.6)", sign);
		check("and is the oracle's fraction of Iabc, so it grows with the CV", scales);
		OtaVca lo; lo.trim = 0.4; lo.begin(48000.0, 0.0);
		OtaVca hi; hi.trim = 0.6; hi.begin(48000.0, 0.0);
		check("mirror trimmer positions give opposite offsets", lo.in.iout * hi.in.iout < 0);
	}
}

static void testChip() {
	printf("datasheet anchors\n");
	// gm = Iabc / 2Vt: typical 9600 umho at 500 uA, 6700..13000
	{
		L::InputNet n;  n.rBot = n.rTop = 500.0;
		L::InputState s;
		double iabc = 500e-6;
		L::solveInput(n, 0.0, 27e3, iabc, s);
		L::InputState s2;
		L::solveInput(n, 0.0, 1e3, iabc, s2);            // drive, then the differential
		L::InputState s3 = s;
		L::solveInput(n, 0.02, 27e3, iabc, s3);          // 20 mV through 27k: a small signal current
		double gm = (s3.iout - s.iout) / ((s3.v3 - s3.v4) - (s.v3 - s.v4));
		printf("    gm %.0f umho at Iabc 500 uA\n", gm * 1e6);
		check("transconductance at 500 uA is in the datasheet's 6700..13000 umho and near the 9600 typical",
		      gm > 6700e-6 && gm < 13000e-6 && std::fabs(gm - 9600e-6) < 500e-6);
	}
	// peak output current ~ Iabc (350..650 uA at 500 uA)
	{
		OtaVca o; o.begin(48000.0, 0.0);
		L::InputState s;
		L::solveInput(o.inputNet(), 40.0, 27e3, 500e-6, s);
		printf("    peak Iout at 500 uA: %.1f uA\n", s.iout * 1e6);
		check("peak output current at Iabc 500 uA is 350..650 uA (it is Iabc: the pair has steered it all)",
		      s.iout > 350e-6 && s.iout < 650e-6);
		L::solveInput(o.inputNet(), -40.0, 27e3, 500e-6, s);
		check("and the same the other way", s.iout < -350e-6 && s.iout > -650e-6);
	}
	// Figure 5 swing and the buffer's input current (5k to V- at +-15 V)
	{
		L::OutputNet n; n.vPlus = 15; n.vMinus = -15; n.rEmit = 5e3; n.rLoad = 1e12;
		L::OutputState s;
		L::solveOutput(n, 1e-3, 500e-6, s);
		checkv("the output stage tops out at 14.2 V on +-15 V (Figure 5)", s.v5, 14.2, 1e-9);
		check("and flags it", s.clipped);
		L::solveOutput(n, -1e-3, 500e-6, s);
		checkv("and bottoms at -14.4 V", s.v5, -14.4, 1e-9);
		L::solveOutput(n, 0.0, 500e-6, s);
		printf("    buffer input current, +-15 V and 5k: %.3f uA\n", s.ibuf * 1e6);
		check("the buffer's input current at rest is the datasheet's 0.5 uA typical (0.3..1 uA)", s.ibuf > 0.3e-6 && s.ibuf < 1e-6);
		L::solveOutput(n, 400e-6, 500e-6, s);              // pin 5 at ~ +12 V on an open load
		check("the buffer peak output voltage exceeds the datasheet's 10 V minimum", s.vout > 10.0);
	}
	// Eq. 5 exact: Iout = Iabc * tanh(dV / 2Vt) from the pair, any dV the solver reports
	{
		bool ok = true;
		OtaVca o; o.begin(48000.0, 4.0);
		for (double vth : { -20.0, -5.0, -1.0, 0.3, 2.0, 7.0, 15.0 }) {
			L::InputState s;
			L::solveInput(o.inputNet(), vth, 27e3, o.iabc, s);
			double dv = s.v3 - s.v4;
			double i5 = o.iabc / (1 + std::exp(-dv / VT)), i4 = o.iabc - i5;
			if (std::fabs((i5 - i4) - s.iout) > 1e-12) ok = false;
		}
		check("Iout = I5 - I4 with I5/I4 = exp(dV/Vt) and I4 + I5 = Iabc (Eq. 1, 2, 5)", ok);
	}
}

static void testAC() {
	printf("small signal, against the linearised node equations\n");
	double worst = 0;
	for (double trim : { 0.5, 0.3, 0.7 })
		for (double cv : { -6.0, 0.0, 10.0 })
			for (double f : { 5.0, 12.3, 20.0, 100.0, 1000.0 }) {
				double rest5 = 0;
				acGain(trim, cv, f, nullptr, &rest5);
				if (std::fabs(rest5) > 9.0) continue;        // the offset has pinned pin 5 against its rail: not small signal
				OtaVca o; o.trim = trim;
				double fs = 96000.0;
				double got = measureGain(o, 0.02, f, cv, fs);
				double want = std::abs(acGain(trim, cv, f));
				if (std::fabs(got - want) / want > 0.025) printf("      trim %.1f cv %g f %g: got %.4f want %.4f\n", trim, cv, f, got, want);
				worst = std::fmax(worst, std::fabs(got - want) / want);
			}
	printf("    worst gain difference %.3g\n", worst);
	check("gain In -> Out matches the complex linearised network to 2.5 %, trim 0.3/0.5/0.7, CV -6/0/10 V, 5 Hz..1 kHz", worst < 0.025);
	// at 48 kHz (two solves per sample) the same
	{
		OtaVca o;
		double got = measureGain(o, 0.02, 1000.0, 3.0, 48000.0);
		double want = std::abs(acGain(0.5, 3.0, 1000.0));
		checkv("and at 48 kHz (two solves a sample), 1 kHz", got / want, 1.0, 0.025);
	}
	// the 470 nF into the input network: the high-pass corner is where the oracle says
	{
		OtaVca o1, o2;
		double g1 = measureGain(o1, 0.02, 12.3, 0.0, 96000.0);
		double g2 = measureGain(o2, 0.02, 1000.0, 0.0, 96000.0);
		printf("    gain at 12.3 Hz / at 1 kHz: %.3f (a one-pole corner would give 0.707)\n", g1 / g2);
		checkv("the 470 nF / 27k corner is about 12.3 Hz (-3 dB there)", g1 / g2, 0.707, 0.03);
	}
	// the numbers a person would use
	{
		OtaVca o;
		double g = measureGain(o, 0.02, 1000.0, 0.0, 96000.0);
		double gp = measureGain(o, 0.02, 1000.0, 10.0, 96000.0);
		printf("    small-signal gain: %.3f at 0 V CV, %.3f at +10 V CV\n", g, gp);
		check("0 V CV: half open (gain about 0.55); +10 V: about unity", std::fabs(g - 0.55) < 0.03 && std::fabs(gp - 1.05) < 0.04);
		OtaVca z;
		double gz = measureGain(z, 0.02, 1000.0, -11.6, 96000.0);
		check("the VCA closes (gain < 0.001) at -11.6 V", gz < 1e-3);
	}
}

static void testLinearised() {
	printf("what the linearizing diodes buy\n");
	double g[4]; int k = 0;
	for (double amp : { 0.5, 2.0, 5.0, 10.0 }) {
		OtaVca o;
		g[k++] = measureGain(o, amp, 1000.0, 0.0, 96000.0);
	}
	printf("    gain at 0.5 / 2 / 5 / 10 V in: %.4f %.4f %.4f %.4f\n", g[0], g[1], g[2], g[3]);
	check("gain is flat to 1.5 % from 0.5 V to 10 V in (the diodes linearize)", std::fabs(g[3] / g[0] - 1) < 0.015 && std::fabs(g[2] / g[0] - 1) < 0.01);
	// Without the diodes the pins would carry the divided input themselves: 10 V through 27k
	// into the trimmer's 500 ohm (and nothing on the other pin) is 0.18 V across the pair.
	{
		OtaVca o; o.begin(96000.0, 0.0);
		L::InputState s;
		L::solveInput(o.inputNet(), 10.0, 27e3, o.iabc, s);
		double dvBare = 10.0 * 500.0 / (27e3 + 500.0);
		double bareCompression = std::tanh(dvBare / (2 * VT)) / (dvBare / (2 * VT));
		double dv = s.v3 - s.v4;
		printf("    pair differential at 10 V in: %.1f mV with the diodes, %.0f mV without (which would leave %.2f of the small-signal gain)\n",
		       dv * 1e3, dvBare * 1e3, bareCompression);
		check("without the diodes the pair would be compressed by more than 60 % at 10 V in (the contrast the flatness above rests on)", bareCompression < 0.4);
		check("with them the pins move under a fifth of that (the diodes take the signal current, not the pair)", dv < 0.2 * dvBare);
	}
	// beyond Id/2 of signal current the diodes run dry and the stage clips hard
	{
		OtaVca a, b;
		double ga = measureGain(a, 10.0, 1000.0, 0.0, 96000.0);
		double gb = measureGain(b, 40.0, 1000.0, 0.0, 96000.0);
		printf("    gain at 10 V in %.3f, at 40 V in %.3f\n", ga, gb);
		check("far past the diodes' range (40 V in) the gain has collapsed (clips), the limit being Id/2 through 27k", gb < 0.6 * ga);
	}
	// polarity
	{
		OtaVca o; o.begin(48000.0, 0.0);
		double rest = o.out.vout, up = 0, dn = 0;
		for (int n = 0; n < 4800; n++) up = o.process(1.0, 0.0, 48000.0);
		check("a positive step at the input raises Out (non-inverting)", up > rest);
		(void)dn;
	}
}

static void testStart() {
	printf("start-up\n");
	OtaVca o;
	double first = o.process(0.0, 3.0, 48000.0), worst = 0;
	for (int n = 0; n < 4800; n++) worst = std::fmax(worst, std::fabs(o.process(0.0, 3.0, 48000.0) - first));
	printf("    drift of Out over 100 ms of silence from a cold start: %.3g V\n", worst);
	check("a cold start is already at rest: the input capacitor begins charged to the pin's DC level (no thump)", worst < 1e-4);
}

static void testLimits() {
	printf("output compliance\n");
	OtaVca o;
	double mx = -1e9, mn = 1e9;
	for (int n = 0; n < 96000; n++) {
		double y = o.process(30.0 * std::sin(2 * M_PI * 200.0 * n / 96000.0), 12.0, 96000.0);
		if (n > 48000) { mx = std::fmax(mx, y); mn = std::fmin(mn, y); }
	}
	printf("    +-30 V in at +12 V CV: Out %.2f..%.2f V\n", mn, mx);
	check("the output never passes 10 V at the top and sits above V- at the bottom", mx < 10.0 && mx > 8.0 && mn > -12.0 && mn < -9.0);
	// the squaring option
	OtaVca q; q.squareIabc = true; q.begin(48000.0, 5.0);
	checkv("the squared option makes Iabc = raw^2 / 1 mA", q.iabc, q.iabcRaw * q.iabcRaw / 1e-3, 1e-12);
}

static void testRobust() {
	printf("robustness\n");
	bool ok = true, finite = true;
	srand(11);
	for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 }) {
		OtaVca o;
		for (int n = 0; n < (int)rate; n++) {
			double a = (rand() % 100 < 5) ? ((rand() % 2) ? 12.0 : -12.0) : 5.0 * std::sin(n * 0.07);
			double cv = (rand() % 100 < 2) ? ((rand() % 2) ? 12.0 : -12.0) : 12.0 * std::sin(n * 0.0005);
			o.trim = (n % 20000) / 20000.0;
			double y = o.process(a, cv, rate);
			if (!o.in.ok) ok = false;
			if (std::isnan(y) || std::fabs(y) > 15.0) finite = false;
		}
	}
	check("input and CV slammed to +-12 V, the trimmer swept end to end, 44.1..192 kHz: the solve always converges", ok);
	check("and Out stays finite and within 15 V", finite);
	// cost: the Newton iteration count in steady state
	OtaVca o; double fs = 48000.0; long total = 0; int calls = 0;
	for (int n = 0; n < 48000; n++) { o.process(5 * std::sin(n * 0.1), 3.0 * std::sin(n * 0.001), fs); total += o.in.iters; calls++; }
	printf("    mean Newton iterations per solve in use: %.2f\n", (double)total / calls);
	check("a warm solve takes under 8 Newton iterations on average", (double)total / calls < 8.0);
}

int main() {
	testControlCurrent();
	testStatic();
	testChip();
	testAC();
	testLinearised();
	testStart();
	testLimits();
	testRobust();
	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}

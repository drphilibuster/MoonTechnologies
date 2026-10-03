// The behavioural op-amps (src/OpAmp.hpp) against their datasheets.
//
// Three kinds of check:
//   * the datasheet numbers themselves, read back through the model (gain-bandwidth,
//     slew, output swing against supply and load);
//   * the circuit solver against the closed-form frequency response of the same
//     network with the same op-amp (A(s) = A0 / (1 + s A0 / wt)), which is what proves
//     the implicit step is a correct simulation and not merely a plausible one;
//   * the LM358's crossover, which exists because of one mechanism and so goes away
//     when that mechanism is removed.
//
// Every check has a negative control: the same measurement on a model with the
// relevant behaviour removed, which has to FAIL the check. A test that passes on the
// broken model is not testing the thing.

#include "../../src/OpAmp.hpp"

#include <cmath>
#include <complex>
#include <cstdio>
#include <vector>

using namespace opamp;
typedef std::complex<double> cx;

static int checks = 0, failures = 0;

static void check(bool ok, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
#include <cstdarg>
static void check(bool ok, const char* fmt, ...) {
	checks++;
	if (!ok) failures++;
	char buf[300];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	printf("  %s  %s\n", ok ? "ok  " : "FAIL", buf);
}

// --- harness ---------------------------------------------------------------------

/** Settled amplitude and phase of y/x for a sine of frequency f through `stepFn`
    (called once per dt with the input sample). Synchronous detection over whole
    periods after a settling run. */
template <class F>
static cx response(F stepFn, double f, double amp, double dt, int settle = 12, int measure = 8) {
	double T = 1.0 / f;
	long ns = (long)std::llround(T / dt);
	double h = T / (double)ns;
	double sc = 0, ss = 0;
	long n = 0;
	for (int p = 0; p < settle + measure; p++) {
		for (long i = 0; i < ns; i++, n++) {
			double t = (double)n * h;
			double x = amp * std::sin(2.0 * kPi * f * t);
			double y = stepFn(x, h);
			if (p >= settle) {
				sc += y * std::cos(2.0 * kPi * f * t);
				ss += y * std::sin(2.0 * kPi * f * t);
			}
		}
	}
	double norm = 2.0 / ((double)ns * measure);
	// y = |H| amp sin(wt + phi) => ss -> |H|amp cos(phi), sc -> |H|amp sin(phi)
	return cx(ss * norm / amp, sc * norm / amp);
}

static cx openLoop(const Spec& s, double f) {
	cx sj(0, 2.0 * kPi * f);
	double wt = 2.0 * kPi * s.gbw;
	return s.a0 / (1.0 + sj * s.a0 / wt);
}

static cx hInverting(const Spec& s, double rin, double rf, double cf, double f) {
	cx sj(0, 2.0 * kPi * f);
	cx zf = rf / (1.0 + sj * rf * cf);
	cx ng = 1.0 + zf / rin;
	return -(zf / rin) / (1.0 + ng / openLoop(s, f));
}

static cx hNonInverting(const Spec& s, double rg, double cg, double rf, double cf, double f) {
	cx sj(0, 2.0 * kPi * f);
	cx zf = rf / (1.0 + sj * rf * cf);
	cx zg = cg > 0.0 ? cx(rg) + 1.0 / (sj * cg) : cx(rg);
	cx g = 1.0 + zf / zg;
	return g / (1.0 + g / openLoop(s, f));
}

static double rel(cx a, cx b) { return std::abs(a - b) / std::abs(b); }

/** Output of a step input through a follower, sampled every dt, returned as a trace. */
static std::vector<double> followerStep(const Spec& s, double vstep, double dt, int n) {
	NonInvertingStage st;
	st.op.setSpec(s);
	st.op.setSupply(15.0, -15.0);
	st.rf = 0.0;
	st.reset(0.0);
	std::vector<double> y(n);
	for (int i = 0; i < n; i++) y[i] = st.step(vstep, dt);
	return y;
}

static double riseTime1090(const std::vector<double>& y, double final, double dt) {
	double t10 = -1, t90 = -1;
	for (size_t i = 0; i < y.size(); i++) {
		if (t10 < 0 && y[i] >= 0.1 * final) t10 = (double)i * dt;
		if (t90 < 0 && y[i] >= 0.9 * final) { t90 = (double)i * dt; break; }
	}
	return t90 - t10;
}

/** Steepest slope of a trace. */
static double maxSlope(const std::vector<double>& y, double dt) {
	double m = 0;
	for (size_t i = 1; i < y.size(); i++) m = std::fmax(m, (y[i] - y[i - 1]) / dt);
	return m;
}

// --- 1. the datasheet numbers ------------------------------------------------------

static double swing(const Spec& s, double vcc, double rl, bool top = true) {
	Core c;
	c.setSpec(s);
	c.setSupply(vcc, -vcc);
	c.setLoad(rl, 0.0);
	return top ? c.hi : c.lo;
}

static void testDatasheet() {
	printf("Datasheet numbers read back through the model\n");
	Spec tl = tl07x(), l741 = lm741(), l358 = lm358();
	check(tl.gbw == 5.25e6 && tl.slew == 20e6 && tl.a0 == 2e5, "TL07x: 5.25 MHz, 20 V/us, 200 V/mV");
	check(l741.gbw == 1e6 && l741.slew == 0.5e6 && l741.a0 == 2e5 && l741.ri == 2e6, "LM741: 1 MHz, 0.5 V/us, 200 V/mV, 2 Mohm");
	check(l358.gbw == 0.7e6 && l358.slew == 0.3e6 && l358.a0 == 1e5, "LM358: 0.7 MHz, 0.3 V/us, 100 V/mV");

	// TL07x, +-15 V (table 5.8): 13.5 V typ at 10k; Fig. 5-43: ~12.3 V at 2k.
	double a = swing(tl, 15, 10e3), b = swing(tl, 15, 2e3);
	check(std::fabs(a - 13.5) < 0.1, "TL07x +-15 V into 10k reaches %.2f V (datasheet 13.5 typ)", a);
	check(std::fabs(b - 12.3) < 0.15, "TL07x +-15 V into 2k reaches %.2f V (Fig. 5-42/43: ~12.3)", b);
	// Fig. 5-44, RL = 10k: VOM against |Vcc|; read at 6 V (5.0), 10 V (8.7), 14 V (12.5).
	double v6 = swing(tl, 6, 10e3), v10 = swing(tl, 10, 10e3), v14 = swing(tl, 14, 10e3);
	check(std::fabs(v6 - 4.9) < 0.3 && std::fabs(v10 - 8.7) < 0.3 && std::fabs(v14 - 12.5) < 0.3,
	      "TL07x swing vs supply into 10k: %.2f %.2f %.2f V at +-6/10/14 (Fig. 5-44: ~5 / 8.7 / 12.5)", v6, v10, v14);
	double v12 = swing(tl, 12, 10e3);
	check(v12 > 10.3 && v12 < 10.9, "TL07x on +-12 V into 10k reaches %.2f V, not the rail and not the old 11 V", v12);
	check(std::fabs(swing(tl, 12, 10e3, false) + v12) < 1e-9, "TL07x swing is symmetric");
	// LM741, +-15 V (table 6.5): 14 V typ at 10k, 13 V typ at 2k.
	double c = swing(l741, 15, 10e3), d = swing(l741, 15, 2e3);
	check(std::fabs(c - 14.0) < 0.05 && std::fabs(d - 13.0) < 0.05, "LM741 +-15 V: %.2f V into 10k (14), %.2f V into 2k (13)", c, d);
	// LM358 on a 30 V supply (+-15 V), RL >= 10k: 2 V under the rail typ (table 5.7).
	double e = swing(l358, 15, 10e3);
	check(std::fabs((15 - e) - 2.0) < 0.15, "LM358 at 30 V into 10k is %.2f V under the rail (datasheet 2 typ)", 15 - e);
	double f2 = swing(l358, 15, 2e3);
	check(std::fabs((15 - f2) - 3.0) < 0.3, "LM358 into 2k is %.2f V under the rail (10k's 2 V plus the datasheet's 1 V)", 15 - f2);

	// negative control: a model with the headroom removed must fail the same checks
	Spec bad = tl; bad.hSat = 0; bad.hPerVolt = 0; bad.rs = 0;
	double bv = swing(bad, 15, 10e3);
	check(!(std::fabs(bv - 13.5) < 0.1), "negative control: no headroom reaches %.2f V, so the 13.5 V check does fail", bv);
}

// --- 2. gain-bandwidth --------------------------------------------------------------

static void testGbw() {
	printf("Gain-bandwidth\n");
	// A unity follower is a one-pole at GBW: rise time 0.35 / GBW.
	const Spec all[3] = { tl07x(), lm741(), lm358() };
	for (int i = 0; i < 3; i++) {
		double dt = 0.002 / all[i].gbw;
		int n = (int)(6.0 / (2 * kPi * all[i].gbw) / dt) + 400;
		std::vector<double> y = followerStep(all[i], 0.01, dt, n);     // 10 mV: no slewing
		double tr = riseTime1090(y, 0.01, dt);
		double expect = 0.35 / all[i].gbw;
		check(std::fabs(tr / expect - 1.0) < 0.06, "%s follower rise time %.1f ns (0.35/GBW = %.1f ns)",
		      all[i].name, tr * 1e9, expect * 1e9);
	}
	// Negative control: ten times the GBW has a rise time ten times shorter.
	Spec fast = lm741(); fast.gbw *= 10.0;
	double dt = 0.002 / lm741().gbw;
	std::vector<double> y = followerStep(fast, 0.01, dt, 2000);
	double tr = riseTime1090(y, 0.01, dt), expect = 0.35 / lm741().gbw;
	check(!(std::fabs(tr / expect - 1.0) < 0.06), "negative control: 10x GBW rises in %.1f ns, so the check does fail", tr * 1e9);

	// A gain-of-10 stage has a tenth of the bandwidth: closed loop = GBW * beta.
	Spec s = lm741();
	NonInvertingStage st;
	st.op.setSpec(s); st.op.setSupply(15, -15);
	st.rg = 1e3; st.rf = 9e3; st.reset(0.0);
	double h = 0.002 / (s.gbw * 0.1);
	std::vector<double> yy;
	for (int i = 0; i < 4000; i++) yy.push_back(st.step(0.01, h));
	double tr10 = riseTime1090(yy, 0.1, h);
	check(std::fabs(tr10 / (0.35 / (s.gbw * 0.1)) - 1.0) < 0.06,
	      "LM741 at gain 10: rise time %.2f us, i.e. a %.0f kHz closed loop (GBW/10)", tr10 * 1e6, 0.35 / tr10 / 1e3);
}

// --- 3. the circuit solver against closed form -------------------------------------

static void testNetworks() {
	printf("Networks against the closed-form response\n");
	// Consolidation's first stage: 10k in, 10k with 27 pF across it, a TL07x.
	{
		Spec s = tl07x();
		InvertingStage st;
		st.op.setSpec(s); st.op.setSupply(12, -12);
		st.gin = 1.0 / 10e3; st.rf = 10e3; st.cf = 27e-12; st.reset(0);
		double fl[3] = { 1e3, 100e3, 590e3 };
		for (int i = 0; i < 3; i++) {
			st.reset(0);
			cx m = response([&](double x, double h) { return st.step(x * st.gin, h); }, fl[i], 1.0, 1.0 / (fl[i] * 400.0));
			cx a = hInverting(s, 10e3, 10e3, 27e-12, fl[i]);
			check(rel(m, a) < 0.02, "inverting 10k/10k||27p at %.0f Hz: |H| %.4f vs %.4f", fl[i], std::abs(m), std::abs(a));
		}
		// The 27 pF is a real pole at 590 kHz: -3 dB there to within the GBW's own bit.
		cx m = response([&](double x, double h) { return st.step(x * st.gin, h); }, 590e3, 1.0, 1.0 / (590e3 * 400.0));
		check(std::abs(m) < 0.75 && std::abs(m) > 0.6, "27 pF with 10k is a pole near 590 kHz: |H| = %.3f there", std::abs(m));
		// Negative control: leave the cap out and 590 kHz sails through.
		InvertingStage nc = st; nc.cf = 0; nc.reset(0);
		cx m2 = response([&](double x, double h) { return nc.step(x * nc.gin, h); }, 590e3, 1.0, 1.0 / (590e3 * 400.0));
		check(!(std::abs(m2) < 0.75), "negative control: without the 27 pF |H| is %.3f at 590 kHz, so the check does fail", std::abs(m2));
	}
	// The Distortion+ network: LM741, 4k7 + 47 nF to ground, a 100k pot with 1 nF across.
	{
		Spec s = lm741();
		NonInvertingStage st;
		st.op.setSpec(s); st.op.setSupply(4.5, -4.5);
		st.rg = 4.7e3; st.cg = 47e-9; st.rf = 100e3; st.cf = 1e-9; st.reset(0);
		double fl[5] = { 100, 720, 2000, 8000, 30000 };
		for (int i = 0; i < 5; i++) {
			st.reset(0);
			cx m = response([&](double x, double h) { return st.step(x, h); }, fl[i], 0.01, 1.0 / (fl[i] * 800.0), 40, 8);
			cx a = hNonInverting(s, 4.7e3, 47e-9, 100e3, 1e-9, fl[i]);
			check(rel(m, a) < 0.02, "Distortion+ network at %5.0f Hz: |H| %.3f vs %.3f", fl[i], std::abs(m), std::abs(a));
		}
		// At the step the module actually runs it at (16 x 48 kHz): 1.3 us, 770 steps
		// a period at 1 kHz and 96 at 8 kHz.
		{
			double worst = 0;
			for (int i = 0; i < 4; i++) {
				st.reset(0);
				double fm[4] = { 100, 720, 2000, 8000 };
				cx m = response([&](double x, double h) { return st.step(x, h); }, fm[i], 0.01, 1.0 / 768000.0, 40, 8);
				cx a = hNonInverting(s, 4.7e3, 47e-9, 100e3, 1e-9, fm[i]);
				worst = std::fmax(worst, std::fabs(std::abs(m) / std::abs(a) - 1.0));
			}
			check(worst < 0.008, "at the module's 1.3 us step the amplitude is within %.2f%% of the closed form", 100 * worst);
		}
		// And at full gain (1 M), where the 741's own gain-bandwidth has something to do.
		NonInvertingStage hi = st; hi.rf = 1e6; hi.reset(0);
		double f = 3000;
		cx m = response([&](double x, double h) { return hi.step(x, h); }, f, 0.001, 1.0 / (f * 800.0), 60, 8);
		cx a = hNonInverting(s, 4.7e3, 47e-9, 1e6, 1e-9, f);
		cx ideal = hNonInverting([&] { Spec z = s; z.gbw = 1e12; return z; }(), 4.7e3, 47e-9, 1e6, 1e-9, f);
		check(rel(m, a) < 0.02, "full gain, 3 kHz: |H| %.3f vs closed form %.3f (an ideal op-amp would give %.3f)",
		      std::abs(m), std::abs(a), std::abs(ideal));
		check(std::abs(a) < 0.97 * std::abs(ideal) && std::abs(a) > 0.2 * std::abs(ideal) + 0,
		      "the 741's GBW costs the loop gain: %.1f%% of the ideal at 3 kHz", 100 * std::abs(a) / std::abs(ideal));
		// Negative control: a solver that ignored the op-amp's bandwidth (GBW x1000)
		// disagrees with the closed form for the real part.
		NonInvertingStage nb = hi; nb.op.spec.gbw *= 1000.0; nb.reset(0);
		cx m3 = response([&](double x, double h) { return nb.step(x, h); }, f, 0.001, 1.0 / (f * 800.0), 60, 8);
		check(!(rel(m3, a) < 0.02), "negative control: 1000x GBW gives |H| %.3f against %.3f, so the check does fail",
		      std::abs(m3), std::abs(a));
	}
}

// --- 4. slew -------------------------------------------------------------------------

static void testSlew() {
	printf("Slew rate\n");
	const Spec all[3] = { tl07x(), lm741(), lm358() };
	const double step[3] = { 10.0, 6.0, 6.0 };
	for (int i = 0; i < 3; i++) {
		double dt = 0.0005 / all[i].gbw;
		std::vector<double> y = followerStep(all[i], step[i], dt, (int)(step[i] / (all[i].slew * dt) * 1.6) + 100);
		double sl = maxSlope(y, dt);
		check(std::fabs(sl / all[i].slew - 1.0) < 0.02, "%s: a %.0f V step slews at %.3f V/us (datasheet %.1f)",
		      all[i].name, step[i], sl / 1e6, all[i].slew / 1e6);
	}
	// And the small signal is untouched: a 10 mV step is far below the slew limit.
	Spec s = lm741();
	double dt = 0.002 / s.gbw;
	std::vector<double> y = followerStep(s, 0.01, dt, 3000);
	check(maxSlope(y, dt) < 0.5 * s.slew, "a 10 mV step on the 741 is not slew limited (%.3f V/us)", maxSlope(y, dt) / 1e6);
	// Negative control
	Spec nc = lm741(); nc.slew *= 1000.0;
	std::vector<double> y2 = followerStep(nc, 6.0, 0.0005 / nc.gbw, 4000);
	double sl = maxSlope(y2, 0.0005 / nc.gbw);
	check(!(std::fabs(sl / lm741().slew - 1.0) < 0.02), "negative control: unlimited slew gives %.1f V/us, so the check does fail", sl / 1e6);

	// What it does to audio: a 5 V peak sine at 20 kHz needs 0.63 V/us. A TL07x passes it,
	// an LM741 follower cannot, and the 741's output is a triangle of the right slope.
	{
		double f = 20e3, amp = 5.0, dtt = 1.0 / (f * 2000.0);
		for (int k = 0; k < 2; k++) {
			Spec sp = k ? lm741() : tl07x();
			NonInvertingStage st; st.op.setSpec(sp); st.op.setSupply(15, -15); st.rf = 0; st.reset(0);
			cx m = response([&](double x, double h) { return st.step(x, h); }, f, amp, dtt);
			if (k == 0) check(std::abs(m) > 0.999, "TL07x follower passes 5 V at 20 kHz (|H| %.4f)", std::abs(m));
			else        check(std::abs(m) < 0.97, "LM741 follower cannot: |H| %.3f at 5 V, 20 kHz (needs 0.63 V/us, has 0.5)", std::abs(m));
		}
	}
}

// --- 5. rails, recovery ----------------------------------------------------------------

static void testRails() {
	printf("Rails and overload recovery\n");
	Spec s = tl07x();
	NonInvertingStage st;
	st.op.setSpec(s); st.op.setSupply(12, -12); st.op.setLoad(10e3, 0);
	st.rg = 1e3; st.rf = 9e3; st.reset(0.0);
	double dt = 1e-7, y = 0;
	for (int i = 0; i < 2000; i++) y = st.step(2.0, dt);          // wants 20 V
	check(std::fabs(y - st.op.hi) < 1e-9 && y > 10.3 && y < 10.9, "gain-10 stage asked for 20 V stops at %.3f V on +-12 V", y);
	for (int i = 0; i < 2000; i++) y = st.step(-2.0, dt);
	check(std::fabs(y - st.op.lo) < 1e-9, "and at %.3f V the other way", y);
	// recovery: from the rail to a small signal inside a microsecond
	for (int i = 0; i < 2000; i++) y = st.step(2.0, dt);
	int n = 0;
	while (y > 5.0 && n < 100000) { y = st.step(0.1, dt); n++; }
	check(n * dt < 2e-6, "leaves the rail %.2f us after the input goes small (no windup)", n * dt * 1e6);
	// heavier load, lower swing
	st.op.setLoad(2e3, 0);
	for (int i = 0; i < 2000; i++) y = st.step(2.0, dt);
	check(y < 9.75 && y > 9.55, "a 2k load takes it down to %.2f V (Fig. 5-43 scaled to +-12 V)", y);
	// DC gain error: 1/A0
	NonInvertingStage fl; fl.op.setSpec(s); fl.op.setSupply(12, -12); fl.rf = 0; fl.reset(0);
	for (int i = 0; i < 20000; i++) y = fl.step(5.0, 1e-6);
	check(std::fabs(y / 5.0 - 1.0) < 1.5 / s.a0 + 1e-9 && y < 5.0, "follower DC error %.2e (1/A0 = %.2e)", 1.0 - y / 5.0, 1.0 / s.a0);
}

// --- 6. LM358 crossover -----------------------------------------------------------------

/** THD (rms of harmonics 2..10 over the fundamental) of a follower driving a load. */
static double thd(const Spec& s, double rl, double vt, double f, double amp, double dtt) {
	NonInvertingStage st;
	st.op.setSpec(s); st.op.setSupply(12, -12); st.op.setLoad(rl, vt);
	st.rf = 0; st.reset(0.0);
	double T = 1.0 / f;
	long ns = (long)std::llround(T / dtt);
	double h = T / (double)ns;
	const int settle = 4, meas = 10;
	double sc[11] = {}, ss[11] = {};
	long n = 0;
	for (int p = 0; p < settle + meas; p++)
		for (long i = 0; i < ns; i++, n++) {
			double t = (double)n * h;
			double y = st.step(amp * std::sin(2 * kPi * f * t), h);
			if (p >= settle)
				for (int k = 1; k <= 10; k++) {
					sc[k] += y * std::cos(2 * kPi * f * k * t);
					ss[k] += y * std::sin(2 * kPi * f * k * t);
				}
		}
	double mag[11];
	for (int k = 1; k <= 10; k++)
		mag[k] = 2.0 / ((double)ns * meas) * std::sqrt(sc[k] * sc[k] + ss[k] * ss[k]);
	double hr = 0;
	for (int k = 2; k <= 10; k++) hr += mag[k] * mag[k];
	return std::sqrt(hr) / mag[1];
}

static void testCrossover() {
	printf("LM358 crossover\n");
	const double f = 1000, amp = 1.0, dtt = 1.0 / (f * 4000.0);
	double d358 = thd(lm358(), 10e3, 0.0, f, amp, dtt);
	double dpull = thd(lm358(), 3.3e3, -12.0, f, amp, dtt);
	double dtl = thd(tl07x(), 10e3, 0.0, f, amp, dtt);
	printf("        THD at 1 kHz, 1 V peak: LM358 into 10k to ground %.4f%%, LM358 with a 3k3 pull-down %.5f%%, TL07x %.5f%%\n",
	       100 * d358, 100 * dpull, 100 * dtl);
	check(d358 > 3e-4, "LM358 into a ground-referred load has crossover distortion: %.3f%%", 100 * d358);
	check(dpull < 0.1 * d358, "a pull-down to V- (class A) takes it away: %.5f%% (%.0fx lower)", 100 * dpull, d358 / dpull);
	check(dtl < 0.05 * d358, "a TL07x does not have it: %.5f%%", 100 * dtl);
	// It is the dead band: shrink it and the distortion shrinks with it.
	Spec half = lm358(); half.vbe *= 0.5;
	double dh = thd(half, 10e3, 0.0, f, amp, dtt);
	check(dh < 0.8 * d358 && dh > 0.1 * d358, "half the base-emitter drop, distortion %.4f%% (was %.4f%%)", 100 * dh, 100 * d358);
	// More amplitude: relatively less (the dead band is a fixed width).
	double dbig = thd(lm358(), 10e3, 0.0, f, 8.0, dtt);
	check(dbig < d358, "a larger signal is relatively cleaner: %.4f%% at 8 V peak", 100 * dbig);
	// Slower slew widens it (3 Vbe / SR).
	Spec slow = lm358(); slow.slew *= 0.1;
	double dsl = thd(slow, 10e3, 0.0, f, amp, dtt);
	check(dsl >= d358, "the dead band is crossed at the slew rate, so a slower part is no cleaner (%.4f%%)", 100 * dsl);
	// Negative control: with the class-B stage turned into a follower the check fails.
	Spec nc = lm358(); nc.vbe = 0.0;
	double dnc = thd(nc, 10e3, 0.0, f, amp, dtt);
	check(!(dnc > 3e-4), "negative control: no dead band gives %.5f%%, so the crossover check does fail", 100 * dnc);

	// The flat spot itself: TI SLOA277B 4.4 -- a swing across zero current holds the
	// output near RL * Isink for about 3 Vbe / SR.
	{
		Spec s = lm358();
		NonInvertingStage st; st.op.setSpec(s); st.op.setSupply(5, -5); st.op.setLoad(2.7e3, 0.0);
		st.rf = 0; st.reset(0.0);
		double dt = 2e-9, t = 0, tHold = 0;
		double y = 0;
		// a +-1 V, 10 kHz sine with the load to ground (TI's second example); the flat
		// spot sits at RL x Isink, so time how long the output stays within 1 mV of it
		double f2 = 10e3, y0 = st.op.y0, run = 0;
		for (long i = 0; i < (long)(2.2 / f2 / dt); i++, t += dt) {
			y = st.step(std::sin(2 * kPi * f2 * t), dt);
			if (std::fabs(y - y0) < 1e-3) { run += dt; if (run > tHold) tHold = run; }
			else run = 0;
		}
		double expect = 3 * s.vbe / s.slew;
		check(tHold > 0.6 * expect && tHold < 1.2 * expect, "the output stalls for %.1f us at the crossing (3 Vbe / SR = %.1f us)", tHold * 1e6, expect * 1e6);
	}
}

// --- 7. the class-B output map -------------------------------------------------------

static void testStage() {
	printf("Output stage limits\n");
	Core c; c.setSpec(lm358()); c.setSupply(12, -12);
	c.setLoad(10e3, 0.0);
	// With the load to ground the sink side is the PNP follower: 0.62 V above V- plus 47 ohm x I
	check(c.lo > -11.5 && c.lo < -11.2, "LM358 on +-12 V into 10k to ground sinks down to %.2f V (V- + 0.62 + 47 ohm x 1.1 mA)", c.lo);
	// A load that asks for under the constant sink's current gets within 5 mV of V-
	Core d; d.setSpec(lm358()); d.setSupply(12, -12); d.setLoad(1e9, 0.0);
	check(std::fabs(d.lo - (-12 + 0.005)) < 1e-6, "a feather-light load: %.3f V, 5 mV off V-", d.lo);
	check(std::fabs(c.y0 - (-40e-6 * 10e3)) < 1e-9, "the dead band floats at RL x Isink = %.2f V", c.y0);
}

int main() {
	printf("Op-amps (src/OpAmp.hpp)\n");
	testDatasheet();
	testGbw();
	testNetworks();
	testSlew();
	testRails();
	testCrossover();
	testStage();
	printf("\n%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}

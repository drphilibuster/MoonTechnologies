// Diversified 99 (Echomatic) and 100 (Little Angel) run the real PT2399 model.
//
// test_programs.cpp already sweeps every program for NaN and runaway. This is
// what only these two have to answer: that the delay is the chip's clock and
// not a buffer length, that moving the clock bends the pitch, that feedback past
// unity is bounded by the chip rather than by a function added for the purpose,
// and that the Little Angel is the published board: a chip held at its shortest
// delay with the LFO on REF, the drawn LFO and filters, a fixed mix.

#include "../../src/Diversified/MiawFx.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

using divfx::MiawCtx;
using divfx::MiawRack;

static int checks = 0, failures = 0;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; \
	printf("  FAIL  "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static const float SR = 48000.f;

static MiawCtx ctx(float p0, float p1, float p2) {
	MiawCtx c;
	c.sr = SR;
	c.p[0] = p0; c.p[1] = p1; c.p[2] = p2;
	return c;
}

/** Zero crossings per second over [i0, i1) of a signal. */
static float freqOf(const std::vector<float>& x, int i0, int i1) {
	int n = 0;
	for (int i = i0 + 1; i < i1; i++)
		if ((x[i - 1] < 0.f) != (x[i] < 0.f)) n++;
	return 0.5f * n * SR / (float) (i1 - i0);
}

/** Lag, in seconds, at which `out` best matches the `burst` that went into it. */
static float echoLag(const std::vector<float>& burst, const std::vector<float>& out,
                     int lo, int hi) {
	double best = -1e30;
	int bestLag = lo;
	for (int lag = lo; lag < hi; lag++) {
		double s = 0.0;
		for (size_t i = 0; i < burst.size(); i++)
			s += (double) burst[i] * out[i + lag];
		if (s > best) { best = s; bestLag = lag; }
	}
	return (float) bestLag / SR;
}

static void testDelayIsTheClock() {
	printf("Echomatic: delay equals TIME\n");
	const float knobs[] = { 0.f, 0.5f, 1.f };
	for (float k : knobs) {
		MiawRack m; m.init(); m.setSampleRate(SR); m.clearAll();
		MiawCtx c = ctx(k, 0.f, 1.f);       // no feedback, full level
		m.setParams(divfx::MW_ECHOMATIC, c);
		float want = 0.03f * std::pow(1.f / 0.03f, k);

		// Let the Smooth settle on TIME, then fire a 10 ms burst.
		const int pre = (int) (0.6f * SR), blen = (int) (0.010f * SR);
		const int total = pre + blen + (int) ((want + 0.1f) * SR);
		std::vector<float> in(total, 0.f), out(total, 0.f), burst(blen);
		for (int i = 0; i < blen; i++) {
			burst[i] = 2.f * std::sin(2.f * (float) M_PI * 700.f * i / SR);
			in[pre + i] = burst[i];
		}
		for (int i = 0; i < total; i++) {
			float l, r;
			m.process(divfx::MW_ECHOMATIC, c, in[i], in[i], l, r);
			out[i] = l;
		}
		std::vector<float> seg(out.begin() + pre, out.end());
		float lag = echoLag(burst, seg, (int) (0.02f * SR), (int) ((want + 0.05f) * SR));
		CHECK(std::fabs(lag - want) < 0.002f,
		      "knob %.1f: echo at %.1f ms, want %.1f ms", k, lag * 1e3f, want * 1e3f);
	}
}

static void testPitchBendOnTimeChange() {
	printf("Echomatic: moving TIME bends the pitch\n");
	MiawRack m; m.init(); m.setSampleRate(SR); m.clearAll();
	MiawCtx c = ctx(0.5f, 0.f, 1.f);
	m.setParams(divfx::MW_ECHOMATIC, c);
	const int n = (int) (3.f * SR);
	std::vector<float> out(n);
	for (int i = 0; i < n; i++) {
		float t = (float) i / SR;
		// Knob 0.5 -> 0.7 between 1.0 s and 2.0 s: the delay lengthens.
		float k = 0.5f + 0.2f * std::fmin(std::fmax(t - 1.f, 0.f), 1.f);
		c.p[0] = k;
		m.setParams(divfx::MW_ECHOMATIC, c);
		float in = 2.f * std::sin(2.f * (float) M_PI * 500.f * t);
		float l, r;
		m.process(divfx::MW_ECHOMATIC, c, in, in, l, r);
		out[i] = l;
	}
	float steady = freqOf(out, (int) (0.7f * SR), (int) (0.95f * SR));
	float bent = freqOf(out, (int) (1.3f * SR), (int) (1.8f * SR));
	CHECK(std::fabs(steady - 500.f) < 15.f, "steady tone is %.1f Hz, want 500", steady);
	CHECK(bent < steady * 0.98f,
	      "lengthening the delay should drop the pitch: %.1f Hz vs steady %.1f", bent, steady);
}

static void testFeedbackBoundedByChip() {
	printf("Echomatic: feedback past unity is bounded and finite\n");
	MiawRack m; m.init(); m.setSampleRate(SR); m.clearAll();
	MiawCtx c = ctx(0.3f, 1.f, 1.f);        // FEEDBACK at the top: 1.25
	m.setParams(divfx::MW_ECHOMATIC, c);
	float peak = 0.f;
	bool finite = true;
	for (int i = 0; i < (int) (8.f * SR); i++) {
		float in = (i < 480) ? 4.f : 0.f;
		float l, r;
		m.process(divfx::MW_ECHOMATIC, c, in, in, l, r);
		if (!std::isfinite(l)) finite = false;
		if (i > (int) (2.f * SR)) peak = std::fmax(peak, std::fabs(l));
	}
	CHECK(finite, "output went non-finite");
	CHECK(peak < 8.f, "loop reached %.1f V; the chip's rail should hold it", peak);
	CHECK(peak > 0.05f, "loop at 1.25x feedback should still be singing, peak %.3f V", peak);
}

static void testSendReturnInsideLoop() {
	printf("Echomatic: insert sits inside the feedback path\n");
	// RETURN patched to silence breaks the loop: with FEEDBACK up, the repeats
	// that the open loop would sing must be gone.
	auto run = [](bool retConnected) {
		MiawRack m; m.init(); m.setSampleRate(SR); m.clearAll();
		MiawCtx c = ctx(0.f, 0.9f, 1.f);    // 30 ms, FEEDBACK 1.125
		c.retConnected = retConnected;
		c.ret = 0.f;
		m.setParams(divfx::MW_ECHOMATIC, c);
		double e = 0.0;
		for (int i = 0; i < (int) (3.f * SR); i++) {
			float in = (i < 480) ? 4.f : 0.f;
			float l, r;
			m.process(divfx::MW_ECHOMATIC, c, in, in, l, r);
			if (i > (int) (1.5f * SR)) e += (double) l * l;
		}
		return e;
	};
	double open = run(false), broken = run(true);
	CHECK(open > 100.0 * broken + 1e-9,
	      "late energy: loop closed %.4g vs RETURN grounded %.4g", open, broken);
}

// ---------------------------------------------------------------------------
// 100  Little Angel -- Rick Holt's PT2399 mini chorus, Jack Orman's NYE rev 2.
// Source for every number below: http://www.muzique.com/news/images2/Angel_NYE2.gif
// and the PT2399 data sheet (Princeton V1.6, Table 1).

/** Spread of a tone's frequency cycle by cycle (interpolated upward crossings,
    averaged over 8 cycles), which resolves a fraction of a hertz where counting
    zero crossings in a window resolves several. */
static float cycleSpread(const std::vector<float>& x, int from) {
	std::vector<double> t;
	for (size_t i = (size_t) from + 1; i < x.size(); i++)
		if (x[i - 1] < 0.f && x[i] >= 0.f)
			t.push_back((double) (i - 1) + (double) (-x[i - 1]) / (double) (x[i] - x[i - 1]));
	float lo = 1e9f, hi = 0.f;
	for (size_t k = 0; k + 8 < t.size(); k++) {
		float f = (float) (8.0 * SR / (t[k + 8] - t[k]));
		lo = std::fmin(lo, f); hi = std::fmax(hi, f);
	}
	return t.size() > 16 ? hi - lo : 0.f;
}

static void runAngel(float speed, float depth, float mode, float freq, float amp,
                     float secs, std::vector<float>& L, std::vector<float>& R) {
	MiawRack m; m.init(); m.setSampleRate(SR); m.clearAll();
	MiawCtx c = ctx(speed, depth, mode);
	m.setParams(divfx::MW_ANGEL, c);
	const int n = (int) (secs * SR);
	L.resize(n); R.resize(n);
	for (int i = 0; i < n; i++) {
		float in = amp * std::sin(2.f * (float) M_PI * freq * i / SR);
		m.process(divfx::MW_ANGEL, c, in, in, L[i], R[i]);
	}
}

/** The pedal's delay is the chip's: pin 6 down on a J112, 6 V, REF at 3 V -- the
    shortest the part goes, and not a 5-30 ms range with a floor of this module's
    own. With the LFO off the echo of a burst sits where the data sheet's law
    (corrected for REF) says. */
static void testAngelDelayIsTheChipsMinimum() {
	printf("Little Angel: unmodulated delay is the chip's shortest, at 6 V\n");
	MiawRack m; m.init(); m.setSampleRate(SR); m.clearAll();
	MiawCtx c = ctx(0.5f, 0.f, 0.7f);       // DEPTH 0: REF does not move; vibe: wet only
	m.setParams(divfx::MW_ANGEL, c);
	const int pre = (int) (0.3f * SR), blen = (int) (0.010f * SR);
	const int total = pre + blen + (int) (0.1f * SR);
	std::vector<float> in(total, 0.f), out(total, 0.f), burst(blen);
	for (int i = 0; i < blen; i++) {
		burst[i] = 2.f * std::sin(2.f * (float) M_PI * 700.f * i / SR);
		in[pre + i] = burst[i];
	}
	for (int i = 0; i < total; i++) {
		float l, r;
		m.process(divfx::MW_ANGEL, c, in[i], in[i], l, r);
		out[i] = l;
	}
	std::vector<float> seg(out.begin() + pre, out.end());
	float lag = echoLag(burst, seg, (int) (0.010f * SR), (int) (0.060f * SR));
	double law = pt2399::Pt2399::delaySecondsForVref(divfx::la::J112_RDS_ON, 0.5 * divfx::la::VCC);
	printf("    echo at %.2f ms; law (J112 %.0f ohm, REF %.1f V) %.2f ms; at 5 V it would be %.2f ms\n",
	       lag * 1e3f, divfx::la::J112_RDS_ON, 0.5 * divfx::la::VCC, law * 1e3,
	       pt2399::Pt2399::delaySecondsForVref(divfx::la::J112_RDS_ON, 2.5) * 1e3);
	// The echo is the law plus the filters' group delay (LPF1, the integrators,
	// LPF2: well under 1 ms together).
	CHECK(lag > law && lag - law < 0.0015, "echo %.2f ms, law %.2f ms", lag * 1e3f, law * 1e3);
	// Orman: the clock "runs faster at the higher voltage", which is why the rev
	// moved the regulator to 6 V. The same pin-6 path at 5 V is ~31.2 ms here.
	CHECK(lag > 0.026f && lag < 0.0305f,
	      "echo %.2f ms: not the data sheet's shortest (31.3 ms at 5 V), shortened by 6 V", lag * 1e3f);
}

/** The data sheet's Table 1 at pin 6 = 0.5 ohm is 31.3 ms at 5 V; the law the chip
    model uses agrees to better than 2 ms, and REF moves only the ramp's share. */
static void testChipVrefLaw() {
	printf("Pt2399: REF enters through the VCO current, and only there\n");
	using pt2399::Pt2399;
	CHECK(std::fabs(Pt2399::delaySecondsFor(0.5) - 31.3e-3) < 2e-3,
	      "law at 0.5 ohm is %.2f ms, table 31.3", Pt2399::delaySecondsFor(0.5) * 1e3);
	CHECK(std::fabs(Pt2399::delaySecondsFor(27.6e3) - 342e-3) < 6e-3,
	      "law at 27.6k is %.1f ms, table 342", Pt2399::delaySecondsFor(27.6e3) * 1e3);
	bool same = true;
	const double rs[] = { 0.5, 50.0, 1e3, 10e3, 27.6e3 };
	for (double r : rs)
		for (double ri : { 0.0, 500.0, 1000.0, 3000.0 })
			if (std::fabs(Pt2399::delaySecondsForVref(r, pt2399::assumed::LAW_VREF, ri)
			              - Pt2399::delaySecondsFor(r)) > 1e-12) same = false;
	CHECK(same, "at REF = 2.5 V the Vref law must be the existing law, whatever the pin-6 internal resistance");
	// Lower REF, slower clock; higher, faster; monotone.
	double d25 = Pt2399::delaySecondsForVref(50.0, 2.5), d20 = Pt2399::delaySecondsForVref(50.0, 2.0);
	double d30 = Pt2399::delaySecondsForVref(50.0, 3.0);
	CHECK(d20 > d25 && d25 > d30, "delay vs REF: 2.0 V %.2f, 2.5 V %.2f, 3.0 V %.2f ms", d20 * 1e3, d25 * 1e3, d30 * 1e3);
	// The pin-2 hack: 2.5 V -> 2 V, pin 6 grounded, is milliseconds, not microseconds.
	CHECK(d20 - d25 > 1.5e-3 && d20 - d25 < 5e-3, "2.5 -> 2.0 V moves the delay by %.2f ms", (d20 - d25) * 1e3);
	// A resistor that is large swamps the internal one: REF barely matters to the
	// fixed part, and the ramp scales exactly as REF.
	double big = Pt2399::delaySecondsForVref(27.6e3, 2.0) - 29.70e-3 + 11.46e-3 * 1.0;
	double big0 = Pt2399::delaySecondsForVref(27.6e3, 2.5) - 29.70e-3 + 11.46e-3 * 1.0;
	CHECK(std::fabs(big / big0 - 1.25) < 1e-9, "ramp share must scale as 2.5/REF, got %.6f", big / big0);

	// The integrator constant is a per-chip setting whose default is the one the
	// other modules were fitted with: bit-identical output, to the last bit.
	Pt2399 a, b;
	a.setSampleRate(SR); b.setSampleRate(SR);
	b.setIntegrator(4.7e3, 47e-9);
	a.setPin6(10e3); b.setPin6(10e3);
	bool exact = true;
	float prev = 0.f;
	for (int i = 0; i < 20000 && exact; i++) {
		float cur = 2.f * std::sin(i * 0.07f);
		a.begin(); b.begin();
		float ya = a.demodModulate(prev, cur), yb = b.demodModulate(prev, cur);
		prev = cur;
		if (ya != yb) exact = false;
	}
	CHECK(exact, "setIntegrator(4.7k, 47n) must be the default chip, bit for bit");
	Pt2399 c2; c2.setSampleRate(SR); c2.setIntegrator(4.7e3, 100e-9); c2.setPin6(10e3);
	CHECK(std::fabs(c2.alpha - a.alpha * 0.47f) < a.alpha * 0.01f,
	      "0.1 uF should slow the integrator by 2.13x: alpha %.5f vs %.5f", c2.alpha, a.alpha);
}

template <class F>
static double gainAt(F& f, double hz, double) {
	double fs = SR, sum = 0.0;
	int n = 0;
	for (int i = 0; i < (int) (0.6 * fs); i++) {
		double u = std::sin(2.0 * M_PI * hz * i / fs);
		double y = f.step(u);
		if (i > (int) (0.3 * fs)) { sum += y * y; n++; }
	}
	return std::sqrt(2.0 * sum / n);
}

/** LPF1 (1.6 kHz) and LPF2 (531 Hz) are 1/(1 + 3 s R C), inverting, as the pins
    16/15 and 13/14 are wired -- not the 2-pole 5-7 kHz this module used to assume. */
static void testAngelFilters() {
	printf("Little Angel: LPF1 and LPF2 are the circuit's first-order sections\n");
	divfx::la::Lpf1 l1; l1.setSampleRate(SR); l1.clear();
	divfx::la::Lpf2 l2; l2.setSampleRate(SR); l2.clear();
	double fc1 = 1.0 / (2.0 * M_PI * 3.0 * 10e3 * 3.3e-9);
	double fc2 = 1.0 / (2.0 * M_PI * 3.0 * 10e3 * 10e-9);
	double g1 = gainAt(l1, fc1, 0), g2 = gainAt(l2, fc2, 0);
	printf("    LPF1 corner %.0f Hz gain %.3f; LPF2 corner %.0f Hz gain %.3f\n", fc1, g1, fc2, g2);
	CHECK(std::fabs(g1 - 0.7071) < 0.02, "LPF1 at its corner: %.3f", g1);
	CHECK(std::fabs(g2 - 0.7071) < 0.02, "LPF2 at its corner: %.3f", g2);
	l1.clear(); l2.clear();
	double g1b = gainAt(l1, 2.0 * fc1, 0), g2b = gainAt(l2, 2.0 * fc2, 0);
	CHECK(std::fabs(g1b - 0.447) < 0.02, "LPF1 an octave up: %.3f (first order: 0.447)", g1b);
	CHECK(std::fabs(g2b - 0.447) < 0.02, "LPF2 an octave up: %.3f (first order: 0.447)", g2b);
	// LPF1 sits behind a 0.1 uF into 20k: a high-pass near 80 Hz.
	l1.clear();
	double g1lo = gainAt(l1, 20.0, 0);
	CHECK(g1lo < 0.35, "LPF1's coupling cap should block 20 Hz: gain %.3f", g1lo);
	// Both invert at DC-ish frequencies (so the wet path, two inversions, is not).
	l2.clear();
	double y = 0.0;
	for (int i = 0; i < (int) SR; i++) y = l2.step(1.0);
	CHECK(y < -0.99 && y > -1.01, "LPF2 DC gain is %.3f, want -1", y);
}

/** The LFO is the one-op-amp Schmitt/RC triangle, with the Speed pot in its time
    constant: slow at one end (under 1 Hz), fast at the other (~15 Hz), shaped like
    a triangle and not a sine, and what reaches pin 2 grows with DEPTH. */
static void testAngelLfo() {
	printf("Little Angel: LFO is the drawn circuit\n");
	auto measure = [](double speed, double depth, double& hz, double& peak, double& meanAbs) {
		divfx::la::Lfo l; l.setSampleRate(SR); l.reset(); l.setKnobs(speed, depth);
		int up = 0; double prev = 0.0, lastUp = -1.0, per = 0.0; int periods = 0;
		peak = 0.0; double sa = 0.0; long cnt = 0;
		double shapePeak = 0.0, shapeSum = 0.0;
		const double secs = 40.0;
		for (long i = 0; i < (long) (secs * SR); i++) {
			l.step();
			double v = l.dVref, t = (double) i / SR;
			if (t > 6.0) { shapePeak = std::fmax(shapePeak, std::fabs(l.T)); shapeSum += std::fabs(l.T); }
			if (t > 6.0) {
				if (prev < 0.0 && v >= 0.0) {
					if (lastUp >= 0.0) { per += t - lastUp; periods++; }
					lastUp = t; up++;
				}
				peak = std::fmax(peak, std::fabs(v));
				sa += std::fabs(v); cnt++;
			}
			prev = v;
		}
		hz = periods ? periods / per : 0.0;
		(void) sa; (void) up;
		meanAbs = shapePeak > 0 ? shapeSum / cnt / shapePeak : 0.0;   // of the ramp node itself
	};
	double hz, pk, ma;
	// Closed form with the Depth path unloaded: half period = tau ln 2, tau = (4k7 + Rspeed) 10 uF.
	measure(1.0, 0.0, hz, pk, ma);
	double f1 = 1.0 / (2.0 * std::log(2.0) * 4.7e3 * 10e-6);
	CHECK(std::fabs(hz / f1 - 1.0) < 0.1, "SPEED up: %.2f Hz, closed form %.2f", hz, f1);
	measure(0.5, 0.0, hz, pk, ma);
	double rs = 100e3 * divfx::la::audioTaper(0.5);
	double f2 = 1.0 / (2.0 * std::log(2.0) * (4.7e3 + rs) * 10e-6);
	CHECK(std::fabs(hz / f2 - 1.0) < 0.1, "SPEED mid: %.2f Hz, closed form %.2f", hz, f2);
	measure(0.0, 0.0, hz, pk, ma);
	CHECK(hz > 0.3 && hz < 1.0, "SPEED down: %.2f Hz", hz);
	// The ramp node is a triangle, not a sine: mean |x| / peak is 0.5 for a triangle
	// (a little more for the RC's curvature), 0.64 for a sine, 1 for a square.
	measure(0.75, 1.0, hz, pk, ma);
	printf("    SPEED .75 DEPTH 1: %.2f Hz, REF swings %.0f mV peak, mean/peak %.2f\n", hz, pk * 1e3, ma);
	CHECK(ma > 0.45 && ma < 0.58, "LFO shape mean/peak %.2f is not a triangle's", ma);
	// What reaches pin 2: the ramp's +-1/3 of the swing through a 2.8k/12.8k divider at most.
	double bound = (divfx::la::OPAMP_SWING_V / 3.0) * divfx::la::REF_THEVENIN_OHMS
	             / (divfx::la::REF_THEVENIN_OHMS + divfx::la::R_PIN2);
	CHECK(pk > 0.7 * bound && pk <= bound * 1.001, "full DEPTH: %.0f mV peak, bound %.0f", pk * 1e3, bound * 1e3);
	double pkHalf;
	measure(0.75, 0.5, hz, pkHalf, ma);
	CHECK(pkHalf < 0.25 * pk, "DEPTH at half should be far down the rev-log taper: %.0f mV vs %.0f", pkHalf * 1e3, pk * 1e3);
	measure(0.75, 0.0, hz, pkHalf, ma);
	// The 500k pot still divides 2.8k/(2.8k + 10k + 500k) = 0.55 % of the ramp.
	CHECK(pkHalf < 0.04 * pk, "DEPTH at zero leaves REF almost alone: %.2f mV", pkHalf * 1e3);
	// No DC: the 10 uF takes it out, so REF averages to Vcc/2.
	divfx::la::Lfo l; l.setSampleRate(SR); l.reset(); l.setKnobs(0.75, 1.0);
	double mean = 0.0; long cnt = 0;
	for (long i = 0; i < (long) (30.0 * SR); i++) { l.step(); if (i > (long) (10.0 * SR)) { mean += l.dVref; cnt++; } }
	CHECK(std::fabs(mean / cnt) < 0.02 * pk, "REF mean offset %.2f mV", 1e3 * mean / cnt);
}

static void testAngelModulatesREF() {
	printf("Little Angel: the pitch swing is REF moving the clock\n");
	std::vector<float> L, R, L0, R0;
	runAngel(0.75f, 1.f, 0.7f, 300.f, 2.f, 3.f, L, R);          // vibe, DEPTH full
	float spread = cycleSpread(L, (int) (1.0f * SR));
	runAngel(0.75f, 0.f, 0.7f, 300.f, 2.f, 3.f, L0, R0);        // DEPTH 0: the control
	float flat = cycleSpread(L0, (int) (1.0f * SR));
	printf("    300 Hz vibe: spread %.1f Hz at DEPTH 1, %.1f Hz at DEPTH 0\n", spread, flat);
	CHECK(spread > 8.f && spread < 40.f, "pitch spread %.1f Hz at full DEPTH", spread);
	CHECK(flat < 3.f, "with DEPTH down REF is still, and so is the pitch: %.1f Hz", flat);

	double d = 0.0;
	for (size_t i = (size_t) SR; i < L.size(); i++) d += std::fabs(L[i] - R[i]);
	CHECK(d > 1.0, "the two chips should differ (LFO inverted on the second), sum |L-R| = %.3f", d);

	// Space/Warbler adds the LFO to the J112's gate: more clock excursion, but only
	// when there is LFO to add (DEPTH 0 -> nothing).
	std::vector<float> Lw, Rw, Lw0, Rw0;
	runAngel(0.75f, 1.f, 0.95f, 300.f, 2.f, 3.f, Lw, Rw);
	float spreadW = cycleSpread(Lw, (int) (1.0f * SR));
	runAngel(0.75f, 0.f, 0.95f, 300.f, 2.f, 3.f, Lw0, Rw0);
	float flatW = cycleSpread(Lw0, (int) (1.0f * SR));
	printf("    warbler: spread %.1f Hz (normal %.1f); at DEPTH 0 %.1f Hz\n", spreadW, spread, flatW);
	CHECK(spreadW > spread * 1.05f, "warbler should widen the swing: %.1f vs %.1f Hz", spreadW, spread);
	CHECK(flatW < 3.f, "warbler with no LFO is still: %.1f Hz", flatW);
}

static void testAngelMix() {
	printf("Little Angel: fixed dry/wet, and VIBE opens the dry path\n");
	// A short burst, then the energy in the 15 ms after it, before the wet copy (28 ms) arrives.
	auto early = [](float mode, double& latePart) {
		MiawRack m; m.init(); m.setSampleRate(SR); m.clearAll();
		MiawCtx c = ctx(0.5f, 0.f, mode);
		m.setParams(divfx::MW_ANGEL, c);
		const int pre = (int) (0.3f * SR), blen = (int) (0.005f * SR);
		double e = 0.0, e2 = 0.0;
		for (int i = 0; i < pre + (int) (0.1f * SR); i++) {
			float in = (i >= pre && i < pre + blen) ? 2.f * std::sin(2.f * (float) M_PI * 700.f * (i - pre) / SR) : 0.f;
			float l, r;
			m.process(divfx::MW_ANGEL, c, in, in, l, r);
			if (i >= pre && i < pre + (int) (0.015f * SR)) e += (double) l * l;
			if (i >= pre + (int) (0.025f * SR)) e2 += (double) l * l;
		}
		latePart = e2;
		return e;
	};
	double lateChorus, lateVibe;
	double eChorus = early(0.1f, lateChorus), eVibe = early(0.7f, lateVibe);
	CHECK(eChorus > 100.0 * eVibe + 1e-9, "dry energy: chorus %.4g vs vibe %.4g", eChorus, eVibe);
	// With the dry branch in, its 10k loads the node: the wet amplitude is
	// (0.1 mS)/(0.1+0.1+0.01 mS) of the input to what vibe leaves, (0.1)/(0.1+0.01),
	// so the wet's energy is that ratio squared: 0.274, as the circuit gives it.
	double want = std::pow((0.1 / 0.21) / (0.1 / 0.11), 2.0);
	CHECK(std::fabs(lateChorus / lateVibe / want - 1.0) < 0.1,
	      "wet energy chorus/vibe %.3f, circuit %.3f", lateChorus / lateVibe, want);
	// A circuit with two 10k into a 100k load: coherent dry+wet at 100 Hz, where both
	// filters are flat, is the sum of two unity paths. The 100 Hz tone passes about
	// as loud as it went in, within a few dB, not 0.7x of it as the old mix had.
	std::vector<float> L, R;
	runAngel(0.5f, 0.f, 0.1f, 100.f, 2.f, 1.5f, L, R);
	float pk = 0.f;
	for (size_t i = (size_t) SR; i < L.size(); i++) pk = std::fmax(pk, std::fabs(L[i]));
	CHECK(pk > 2.f && pk < 5.f, "chorus output peak %.2f V for a 2 V, 100 Hz input", pk);
	// The input stage inverts, and so does the whole board (dry and wet both).
	double dot = 0.0;
	for (size_t i = (size_t) SR; i < L.size(); i++)
		dot += L[i] * 2.f * std::sin(2.f * (float) M_PI * 100.f * i / SR);
	CHECK(dot < 0.0, "output should be in antiphase with the input, dot %.3g", dot);
}

/** REF is also the wet node's DC level, so the LFO leaks to the output through the
    0.1 uF -- a little, at a few Hz, and only when DEPTH puts something on pin 2. */
static void testAngelLfoBleed() {
	printf("Little Angel: REF moving shows at the output, through the coupling cap\n");
	auto rms = [](float depth) {
		MiawRack m; m.init(); m.setSampleRate(SR); m.clearAll();
		MiawCtx c = ctx(0.75f, depth, 0.7f);
		m.setParams(divfx::MW_ANGEL, c);
		double e = 0.0; long n = 0;
		for (int i = 0; i < (int) (4.f * SR); i++) {
			float l, r;
			m.process(divfx::MW_ANGEL, c, 0.f, 0.f, l, r);
			if (i > (int) (2.f * SR)) { e += (double) l * l; n++; }
		}
		return std::sqrt(e / n);
	};
	double full = rms(1.f), none = rms(0.f);
	printf("    silence in: %.1f mV rms at DEPTH 1, %.2f mV at DEPTH 0\n", full * 1e3, none * 1e3);
	CHECK(full > 0.02 && full < 0.5, "LFO bleed %.1f mV rms", full * 1e3);
	CHECK(none < 0.05 * full, "DEPTH 0 should leave none: %.2f mV", none * 1e3);
}

static bool sameChip(const pt2399::Pt2399& a, const pt2399::Pt2399& b) {
	return a.w == b.w && a.ram == b.ram && a.lastOut == b.lastOut
	    && a.dem.v == b.dem.v && a.dem.u == b.dem.u && a.dem.hist == b.dem.hist
	    && a.mod.v == b.mod.v && a.mod.u == b.mod.u && a.mod.hist == b.mod.hist;
}

/** The fused passes Little Angel uses are an optimisation of a shared model other
    modules depend on, so they must change nothing: same output sample for sample,
    same RAM, same integrator state, to the last bit -- including slow clocks where
    some audio samples contain no bits at all. */
static void testFusedPassesAreExact() {
	printf("Pt2399: fused passes are bit-identical to demod() then modulate()\n");
	using pt2399::Pt2399;
	Pt2399 refA, refB, fusA, fusB, one;
	for (Pt2399* p : { &refA, &refB, &fusA, &fusB, &one }) p->setSampleRate(SR);
	uint32_t seed = 12345;
	auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return (seed >> 8) * (1.f / 8388608.f) - 1.f; };
	float prev = 0.f;
	bool exact = true;
	for (int i = 0; i < 200000 && exact; i++) {
		float tsec = (i / 40000) % 2 ? 0.9f : 0.003f + 0.02f * (0.5f + 0.5f * std::sin(i * 0.001f));
		double rA = (double) Pt2399::kBits / tsec, rB = (double) Pt2399::kBits / (tsec * 1.3);
		refA.setBitRate(rA); fusA.setBitRate(rA); one.setBitRate(rA);
		refB.setBitRate(rB); fusB.setBitRate(rB);
		float cur = 2.4f * (0.7f * std::sin(i * 0.05f) + 0.2f * rnd());
		refA.begin(); refB.begin(); fusA.begin(); fusB.begin(); one.begin();
		float ra = refA.demod(); refA.modulate(prev, cur);
		float rb = refB.demod(); refB.modulate(prev, cur);
		float fa, fb;
		Pt2399::demodModulate2(fusA, fusB, prev, cur, fa, fb);
		float o = one.demodModulate(prev, cur);
		prev = cur;
		if (ra != fa || rb != fb || ra != o || !sameChip(refA, fusA) || !sameChip(refB, fusB)
		    || !sameChip(refA, one)) {
			printf("    diverged at sample %d\n", i);
			exact = false;
		}
	}
	CHECK(exact, "fused pass differs from the reference");
}

static void benchmark() {
	printf("CPU (informational): one program, 48 kHz, worst-case macros\n");
	struct Case { int id; const char* name; float p0, p1, p2; } cases[] = {
		{ divfx::MW_ECHOMATIC, "99 Echomatic, 30 ms",    0.f,  0.5f, 0.5f },
		{ divfx::MW_ANGEL,     "100 Little Angel, deep", 0.9f, 1.0f, 0.95f },
	};
	for (auto& k : cases) {
		MiawRack m; m.init(); m.setSampleRate(SR); m.clearAll();
		MiawCtx c = ctx(k.p0, k.p1, k.p2);
		m.setParams(k.id, c);
		const int n = (int) (5.f * SR);
		float sink = 0.f;
		auto t0 = std::chrono::steady_clock::now();
		for (int i = 0; i < n; i++) {
			float in = 3.f * std::sin(2.f * (float) M_PI * 220.f * i / SR);
			float l, r;
			m.process(k.id, c, in, in, l, r);
			sink += l + r;
		}
		double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
		printf("    %-24s %5.1f%% of one core (real-time %.0fx)%s\n", k.name,
		       100.0 * s / 5.0, 5.0 / s, sink == 12345.f ? "!" : "");
		CHECK(s / 5.0 < 0.35, "%s uses %.0f%% of a core", k.name, 100.0 * s / 5.0);
	}
}

int main() {
	testDelayIsTheClock();
	testPitchBendOnTimeChange();
	testFeedbackBoundedByChip();
	testSendReturnInsideLoop();
	testChipVrefLaw();
	testAngelFilters();
	testAngelLfo();
	testAngelDelayIsTheChipsMinimum();
	testAngelModulatesREF();
	testAngelMix();
	testAngelLfoBleed();
	testFusedPassesAreExact();
	benchmark();
	printf("\n%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}

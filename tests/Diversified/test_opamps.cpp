// The op-amps in Diversified: 102's LM741 (MXR Distortion+) and 104's two LM358s
// (the bitcrusher's reconstruction filter), against src/OpAmp.hpp's datasheet
// numbers. tests/OpAmp checks the models themselves; this checks they are wired
// into the circuits with the values the schematics give, and that the wiring does
// what the part does. Each has a negative control.

#include "../../src/Diversified/MiawFx.hpp"

#include <cmath>
#include <complex>
#include <cstdio>
#include <vector>

using divfx::MiawCtx;
using divfx::BitCrush;
using divfx::DistPlus;
typedef std::complex<double> cx;

static int checks = 0, failures = 0;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; \
	printf("  FAIL  "); printf(__VA_ARGS__); printf("\n"); } else { printf("  ok    "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static const float SR = 48000.f;

static cx hClosed(const opamp::Spec& s, double rg, double cg, double rf, double cf, double f) {
	cx sj(0, 2 * M_PI * f);
	cx zf = rf / (1.0 + sj * rf * cf);
	cx zg = cx(rg) + 1.0 / (sj * cg);
	cx g = 1.0 + zf / zg;
	double wt = 2 * M_PI * s.gbw;
	cx A = s.a0 / (1.0 + sj * s.a0 / wt);
	return g / (1.0 + g / A);
}

// --- 102 ---------------------------------------------------------------------------

static void testDistPlus() {
	printf("102 Distortion+: the LM741 in the network as drawn\n");
	MiawCtx c; c.sr = SR; c.p[0] = 0.5f; c.p[1] = 1.f; c.p[2] = 1.f;
	DistPlus d; d.setSampleRate(SR); d.clear(); d.setParams(c);

	CHECK(d.amp.op.spec.gbw == 1.0e6 && d.amp.op.spec.slew == 0.5e6, "the op-amp is the 741: %.2f MHz, %.2f V/us",
	      d.amp.op.spec.gbw / 1e6, d.amp.op.spec.slew / 1e6);
	CHECK(d.amp.rg == 4700.0 && d.amp.cg == 47e-9 && d.amp.cf == 1e-9, "4k7 + 47 nF to ground, 1 nF across the pot");
	double rf = 1000.0 + 999000.0 * std::pow(0.5, 2.2);
	CHECK(std::fabs(d.amp.rf - rf) < 1.0, "Rf follows the pot: %.0f ohm", d.amp.rf);
	double rail = (4.5 - 0.73) * 10e3 / (10e3 + 196.0);
	CHECK(std::fabs(d.amp.op.hi - rail) < 1e-6 && std::fabs(d.amp.op.lo + rail) < 1e-6,
	      "swing +-%.2f V: 4.5 V less the 741's 0.73 V, into the clamp's 10k", d.amp.op.hi);

	// Gain at small signal, from the op-amp node, against the closed form. 20 mV so
	// even at its peak gain the stage stays under the rails.
	double worst = 0;
	const double fl[4] = { 300, 1000, 3000, 8000 };
	for (int i = 0; i < 4; i++) {
		DistPlus e; e.setSampleRate(SR); e.clear(); e.setParams(c);
		const int N = 96000;
		double sc = 0, ss = 0; int n = 0;
		for (int k = 0; k < N; k++) {
			float x = 0.02f * std::sin(2.f * (float) M_PI * (float) fl[i] * k / SR);
			// bypass the input highpass and the upsampler's filter: feed the stage
			// the 2x samples directly, so it is the stage alone that is measured
			for (int h = 0; h < 2; h++) {
				double t = (2.0 * k + h) / (2.0 * SR);
				float v = 0.02f * std::sin(2.f * (float) M_PI * (float) fl[i] * (float) t);
				(void) x;
				e.stage(v);
				if (k >= N / 2) {
					ss += e.amp.op.y * std::sin(2 * M_PI * fl[i] * t);
					sc += e.amp.op.y * std::cos(2 * M_PI * fl[i] * t);
					n++;
				}
			}
		}
		double m = 2.0 / n * std::sqrt(ss * ss + sc * sc) / 0.02;
		double a = std::abs(hClosed(opamp::lm741(), 4700, 47e-9, rf, 1e-9, fl[i]));
		worst = std::fmax(worst, std::fabs(m / a - 1.0));
	}
	CHECK(worst < 0.02, "small-signal gain within %.2f%% of the network's closed form at 0.3 - 8 kHz", 100 * worst);

	// The 741 slews at 0.5 V/us: a hard-driven stage's op-amp output never moves
	// faster, and does reach it.
	{
		DistPlus e; e.setSampleRate(SR); e.clear(); e.setParams(c);
		double prevY = 0, maxSlope = 0;
		double dt = 1.0 / (2.0 * SR);
		for (int k = 0; k < 4000; k++) {
			float v = ((k / 6) % 2) ? 4.f : -4.f;        // a 8 kHz square, 4 V
			e.stage(v);
			double sl = std::fabs(e.amp.op.y - prevY) / dt;
			if (k > 200 && sl > maxSlope) maxSlope = sl;
			prevY = e.amp.op.y;
		}
		CHECK(maxSlope <= 0.5e6 * 1.02 && maxSlope > 0.4e6, "a hard-driven 8 kHz square slews the output at %.3f V/us (the 741's 0.5)", maxSlope / 1e6);
		// negative control: the same with the slew limit removed
		DistPlus f; f.setSampleRate(SR); f.clear(); f.setParams(c);
		f.amp.op.spec.slew *= 1000.0;
		prevY = 0; double ms2 = 0;
		for (int k = 0; k < 4000; k++) {
			f.stage(((k / 6) % 2) ? 4.f : -4.f);
			double sl = std::fabs(f.amp.op.y - prevY) / dt;
			if (k > 200 && sl > ms2) ms2 = sl;
			prevY = f.amp.op.y;
		}
		CHECK(!(ms2 <= 0.5e6 * 1.02), "negative control: without the limit it moves at %.1f V/us, so the check does fail", ms2 / 1e6);
	}

	// The bandwidth is real: at full gain the boost at 3 kHz is the closed form's, not an ideal op-amp's.
	{
		MiawCtx m = c; m.p[0] = 1.f;
		DistPlus e; e.setSampleRate(SR); e.clear(); e.setParams(m);
		double sc = 0, ss = 0; int n = 0;
		for (int k = 0; k < 96000; k++)
			for (int h = 0; h < 2; h++) {
				double t = (2.0 * k + h) / (2.0 * SR);
				e.stage((float) (0.002 * std::sin(2 * M_PI * 3000 * t)));
				if (k >= 48000) { ss += e.amp.op.y * std::sin(2 * M_PI * 3000 * t); sc += e.amp.op.y * std::cos(2 * M_PI * 3000 * t); n++; }
			}
		double g = 2.0 / n * std::sqrt(ss * ss + sc * sc) / 0.002;
		opamp::Spec ideal = opamp::lm741(); ideal.gbw = 1e12;
		double gc = std::abs(hClosed(opamp::lm741(), 4700, 47e-9, 1e6, 1e-9, 3000));
		double gi = std::abs(hClosed(ideal, 4700, 47e-9, 1e6, 1e-9, 3000));
		CHECK(std::fabs(g / gc - 1.0) < 0.02 && g < gi * 0.99, "full gain at 3 kHz: %.2f (741 closed form %.2f, an ideal op-amp %.2f)", g, gc, gi);
	}
}

// --- 104 ---------------------------------------------------------------------------

static std::vector<float> runCrush(BitCrush& b, MiawCtx& c, int n, float amp, float hz) {
	std::vector<float> out(n);
	for (int i = 0; i < n; i++) {
		float x = amp * std::sin(2.f * (float) M_PI * hz * i / SR);
		b.process(c, x, out[i]);
	}
	return out;
}

static void testBitCrush() {
	printf("104 BitCrusher: the two LM358s\n");
	MiawCtx c; c.sr = SR; c.p[0] = 1.f; c.p[1] = 1.f; c.p[2] = 1.f;
	c.crushDiv = 0; c.crushLpf = false; c.crushGain = 1;
	BitCrush b; b.setSampleRate(SR); b.clear(); b.setParams(c);

	CHECK(b.u71.op.spec.classB() && b.u72.op.spec.classB() && b.u71.op.spec.gbw == 0.7e6 && b.u71.op.spec.slew == 0.3e6,
	      "both are LM358s: class-B output, %.1f MHz, %.1f V/us", b.u71.op.spec.gbw / 1e6, b.u71.op.spec.slew / 1e6);
	CHECK(std::fabs(b.u71.rf - 1000.0) < 1e-9, "U7.1's Rf is the schematic's 1k");
	CHECK(std::fabs(b.u71.gin - 1.0 / 1000.0) < 1e-9, "the full ladder is 1k to U7.1's node: noise gain %.2f", 1.0 + b.u71.rf * b.u71.gin);
	// open legs raise the ladder's resistance: only the MSB driven, the string behind it
	// is 2k + 1k per node out to the termination
	{
		MiawCtx d = c; d.p[1] = 0.f;                    // one bit
		BitCrush e; e.setSampleRate(SR); e.clear(); e.setParams(d);
		double want = 1.0 / 2000.0 + 1.0 / (1000.0 + 8000.0);
		CHECK(std::fabs(e.u71.gin - want) / want < 1e-9, "only the MSB patched: %.4f mS (hand-solved %.4f)", e.u71.gin * 1e3, want * 1e3);
	}
	CHECK(std::fabs(b.u72.rf - 2200.0) < 1e-9 && std::fabs(b.u72.rg - 2200.0) < 1e-9, "U7.2 at 2x: RP3 = R28 = 2k2");

	// Slew rounds the converter's steps. How much depends on the clock: at the top of
	// RATE the codes arrive every couple of microseconds and differ by an LSB, nothing
	// to slew; slower, the steps grow with the interval until a conversion lands every
	// 100 us with volts between codes, and the LM358 needs 17 us per 5 V. Compare the
	// board with the real op-amps against the same board with infinitely fast ones.
	double worst = 0, atTop = 0;
	const float rates[5] = { 1.0f, 0.8f, 0.6f, 0.4f, 0.2f };
	for (int pass = 0; pass < 5; pass++) {
		MiawCtx m = c; m.p[2] = rates[pass];
		BitCrush real; real.setSampleRate(SR); real.clear(); real.setParams(m);
		BitCrush fast; fast.setSampleRate(SR); fast.clear(); fast.setParams(m);
		fast.u71.op.spec.slew *= 1000.0;
		fast.u72.op.spec.slew *= 1000.0;
		std::vector<float> a = runCrush(real, m, 24000, 5.f, 1500.f), q = runCrush(fast, m, 24000, 5.f, 1500.f);
		double d2 = 0, r2 = 0;
		for (int i = 6000; i < 24000; i++) { d2 += (double) (a[i] - q[i]) * (a[i] - q[i]); r2 += (double) q[i] * q[i]; }
		double ratio = std::sqrt(d2 / r2);
		printf("        RATE %.1f: %.0f conversions/s, slew changes the output by %.2f%% rms\n",
		       rates[pass], 1e6 / (72.0 / (adc0809::Ltc1799::hz(3000.0 * std::pow(1003000.0 / 3000.0, 1.0 - rates[pass]), 1))) * 1.0 / 1e6 * 1.0, 100 * ratio);
		if (pass == 0) atTop = ratio;
		if (ratio > worst) worst = ratio;
	}
	CHECK(worst > 0.03, "somewhere in RATE the LM358's slew changes the output by %.1f%% rms", 100 * worst);
	CHECK(atTop < 0.01, "and not at the top, where codes differ by an LSB (%.2f%%)", 100 * atTop);
	{
		// negative control: two identical models differ by nothing
		MiawCtx m = c; m.p[2] = 0.4f;
		BitCrush x1; x1.setSampleRate(SR); x1.clear(); x1.setParams(m);
		BitCrush x2; x2.setSampleRate(SR); x2.clear(); x2.setParams(m);
		std::vector<float> a = runCrush(x1, m, 24000, 5.f, 1500.f), t = runCrush(x2, m, 24000, 5.f, 1500.f);
		double e2 = 0, r2 = 0;
		for (int i = 6000; i < 24000; i++) { e2 += (double) (a[i] - t[i]) * (a[i] - t[i]); r2 += (double) a[i] * a[i]; }
		CHECK(!(std::sqrt(e2 / r2) > 0.03), "negative control: an identical model differs by %.3f%%, so the slew check does fail", 100 * std::sqrt(e2 / r2));
	}

	// The output stage's rails on a load: U7.1 into SW1 + Rf cannot reach 12 V.
	BitCrush e; e.setSampleRate(SR); e.clear(); e.setParams(c);
	CHECK(e.u71.op.hi < 9.0 && e.u71.op.hi > 6.5, "U7.1 tops out at %.2f V into its load, not at the 10.5 V the old model allowed", e.u71.op.hi);
	CHECK(e.u71.op.lo > -11.0 && e.u71.op.lo < -10.7, "and sinks to %.2f V (V- + 0.62 V + 47 ohm x I)", e.u71.op.lo);
}

int main() {
	testDistPlus();
	testBitCrush();
	printf("\n%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}

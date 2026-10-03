// Consolidation and Bailout's op-amp stages (src/MixerStages.hpp): what a TL07x mixer
// does that an ideal one does not, and that it does not do anything else.
//
// The docs said for years that the compensation caps and the output RC "have no audible
// effect at audio rates". Here that is a measurement: the pass band is flat to 20 kHz at
// 48 kHz to within a tenth of a dB, and the only audible things the op-amps do are the
// rail they stop at, the load that sets it, and (with the coupling on) the cap that
// blocks DC. Each of those has a negative control.

#include "../../src/MixerStages.hpp"

#include <cmath>
#include <cstdarg>
#include <cstdio>

using namespace mixstage;

static int checks = 0, failures = 0;

static void check(bool ok, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
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

static const double SR = 48000.0, DT = 1.0 / 48000.0;

/** Settled amplitude of the bank's OUT for a sine sum of amplitude `amp`. */
static double gainAt(Bank& b, double f, double amp, bool inverted = false) {
	double sc = 0, ss = 0;
	int n = 0;
	const int N = 48000;
	for (int i = 0; i < N; i++) {
		double x = amp * std::sin(2 * opamp::kPi * f * i * DT), inv;
		double y = b.process(x, DT, inv);
		if (inverted) y = inv;
		if (i >= N / 2) {
			sc += y * std::cos(2 * opamp::kPi * f * i * DT);
			ss += y * std::sin(2 * opamp::kPi * f * i * DT);
			n++;
		}
	}
	return 2.0 / n * std::sqrt(sc * sc + ss * ss) / amp;
}

int main() {
	printf("Consolidation / Bailout op-amp stages\n");

	printf("Transparent in the audio band\n");
	{
		Bank b; b.init(4);
		double g1 = gainAt(b, 1000, 1.0), g20 = gainAt(b, 20000, 1.0);
		check(std::fabs(g1 - 1.0) < 1e-3, "OUT is unity at 1 kHz (%.5f)", g1);
		check(std::fabs(g20 - 1.0) < 0.012, "and at 20 kHz (%.5f): the 27 pF / 47 pF and the GBW do nothing here", g20);
		b.init(4);
		double gi = gainAt(b, 1000, 1.0, true);
		check(std::fabs(gi - 1.0) < 1e-3, "INV OUT is its mirror (%.5f)", gi);
		double inv, out = 0;
		b.init(4);
		for (int i = 0; i < 2000; i++) out = b.process(3.0, DT, inv);
		check(std::fabs(out - 3.0) < 1e-3 && std::fabs(inv + 3.0) < 1e-3, "3 V in: OUT %.4f, INV %.4f (both polarities of the sum)", out, inv);
	}

	printf("Rails\n");
	{
		Bank b; b.init(4);
		double inv, out = 0;
		for (int i = 0; i < 2000; i++) out = b.process(14.0, DT, inv);
		// stage 1 drives 5k; its EMF 10.86 V behind 250 ohm gives 10.34 V, and stage 2
		// can only re-invert what it is given.
		check(out > 10.25 && out < 10.45, "a 14 V sum comes out at %.2f V (stage 1's rail into 5k)", out);
		check(inv < -10.25 && inv > -10.45, "INV at %.2f V", inv);
		for (int i = 0; i < 2000; i++) out = b.process(-14.0, DT, inv);
		check(out < -10.25 && out > -10.45, "and %.2f V the other way", out);
		// Not the rail and not the old 11 V of Consolidation's tanh.
		check(std::fabs(out) < 10.9, "below the 11 V the soft clip assumed");

		// negative control: an ideal op-amp (no headroom at all) lets 14 V through
		Bank nc; nc.init(4);
		nc.s1.op.spec.hSat = 0; nc.s1.op.spec.hPerVolt = 0; nc.s1.op.spec.rs = 0; nc.s1.op.update();
		nc.s2.op.spec = nc.s1.op.spec; nc.s2.op.update();
		double o2 = 0;
		for (int i = 0; i < 2000; i++) o2 = nc.process(11.5, DT, inv);
		check(!(o2 < 10.45), "negative control: with no headroom 11.5 V comes out as %.2f V, so the rail check does fail", o2);
	}

	printf("Recovery\n");
	{
		Bank b; b.init(4);
		double inv, out = 0;
		for (int i = 0; i < 400; i++) b.process(14.0, DT, inv);
		out = b.process(1.0, DT, inv);
		check(std::fabs(out - 1.0) < 0.05, "the sample after a long overload is back on the signal (%.3f V)", out);
	}

	printf("Output coupling (100 ohm, 10 uF, 100k)\n");
	{
		Bank b; b.init(4); b.coupled = true;
		double inv, pin, jack = 0;
		int n = (int)(1.0 * SR);
		for (int i = 0; i < n; i++) { pin = b.process(5.0, DT, inv); jack = b.jack(pin, DT); }
		double expect = 5.0 * std::exp(-1.0 / ((kRout + kLoad) * kCout)) * kLoad / (kLoad + kRout);
		check(std::fabs(jack - expect) < 0.03, "5 V of DC decays with tau = 1.001 s: %.3f V after 1 s (e^-1 gives %.3f)", jack, expect);
		double g = 0;
		{
			Bank c; c.init(4); c.coupled = true;
			double sc = 0, ss = 0; int m = 0;
			for (int i = 0; i < 96000; i++) {
				double x = std::sin(2 * opamp::kPi * 1000 * i * DT);
				double p = c.process(x, DT, inv), j = c.jack(p, DT);
				if (i >= 48000) { sc += j * std::cos(2 * opamp::kPi * 1000 * i * DT); ss += j * std::sin(2 * opamp::kPi * 1000 * i * DT); m++; }
			}
			g = 2.0 / m * std::sqrt(sc * sc + ss * ss);
		}
		check(std::fabs(g - kLoad / (kLoad + kRout)) < 2e-3, "audio passes with the 100 ohm / 100k divider: %.4f", g);
		// negative control
		Bank u; u.init(4); u.coupled = false;
		double j2 = 0;
		for (int i = 0; i < n; i++) { pin = u.process(5.0, DT, inv); j2 = u.jack(pin, DT); }
		check(!(std::fabs(j2 - expect) < 0.03), "negative control: uncoupled the DC stays at %.3f V, so the decay check does fail", j2);
	}

	printf("Multiple\n");
	{
		Leg l; l.init();
		double y = 0;
		for (int i = 0; i < 400; i++) y = l.process(4.0, DT);
		check(std::fabs(y - 4.0) < 1e-3, "a follower passes 4 V (%.4f)", y);
		l.withR = true;
		for (int i = 0; i < 400; i++) y = l.process(4.0, DT);
		check(std::fabs(y - 4.0 * kLoad / (kLoad + kRout)) < 1e-3, "with the 100 ohm into 100k it passes %.4f (0.999)", y);
		l.withR = false;
		for (int i = 0; i < 400; i++) y = l.process(12.0, DT);
		check(y > 10.7 && y < 10.95, "12 V in comes out at %.2f V: a TL07x on +-12 V, unloaded EMF", y);
		for (int i = 0; i < 400; i++) y = l.process(-12.0, DT);
		check(y < -10.7 && y > -10.95, "and %.2f V down", y);
		// negative control: no headroom
		Leg n; n.init();
		n.f.op.spec.hSat = 0; n.f.op.spec.hPerVolt = 0; n.f.op.spec.rs = 0; n.f.op.update();
		double y2 = 0;
		for (int i = 0; i < 400; i++) y2 = n.process(11.5, DT);
		check(!(y2 < 10.95), "negative control: with no headroom a follower passes %.2f V, so the rail check does fail", y2);
	}

	printf("\n%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}

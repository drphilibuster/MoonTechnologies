#include "../../src/Obfuscation/Matrix.hpp"
#include <cstdio>
#include <vector>
using namespace obf;

static int checks = 0, failures = 0;
#define CHECK(c, ...) do { checks++; if (!(c)) { failures++; printf("FAIL %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static const float SR = 48000.f;

static float rmsSine(float f, int n, void (*run)(float, float&)) {
	double acc = 0; int cnt = 0;
	for (int i = 0; i < n; i++) {
		float x = std::sin(2.f * PI_F * f * i / SR), y;
		run(x, y);
		if (i > n / 2) { acc += (double) y * y; cnt++; }
	}
	return (float) std::sqrt(acc / cnt) / 0.70710678f;
}

static LR4 g1, g2, gc;
static void xoSum(float x, float& y) {
	float lo1, hi1, mid, hi;
	g1.run(x, lo1, hi1); g2.run(hi1, mid, hi);
	y = gc.allpass(lo1) + mid + hi;
}

int main() {
	// 1. crossover flat
	g1.set(250.f, SR); g2.set(2500.f, SR); gc.set(2500.f, SR);
	for (float f : {50.f, 150.f, 250.f, 700.f, 2500.f, 6000.f, 15000.f}) {
		g1.reset(); g2.reset(); gc.reset();
		float m = rmsSine(f, 48000, xoSum);
		CHECK(std::fabs(20.f * std::log10(m)) < 0.1f, "xover not flat at %g Hz: %g", f, m);
	}

	// 2. allpass chain magnitude is flat for any Q, N = 96
	for (float q : {0.5f, 4.f, 30.f}) {
		Chain c; c.nSm = c.nTarget = 96.f;
		for (int i = 0; i < 96; i++) c.setStage(i, 1000.f, q, SR);
		double acc = 0, acx = 0;
		for (int i = 0; i < 192000; i++) {
			float x = std::sin(2.f * PI_F * 3000.f * i / SR);
			float y = c.process(x);
			if (i > 96000) { acc += (double) y * y; acx += (double) x * x; }
		}
		CHECK(std::fabs(std::sqrt(acc / acx) - 1.0) < 0.02, "chain not unity at Q %g: %g", q, std::sqrt(acc / acx));
	}

	// 2b. pinch concentrates the delay: an impulse through 32 stages at 1 kHz
	// is spread over more samples at high Q than at low Q.
	{
		auto spreadSamples = [&](float q) {
			Chain c; c.nSm = c.nTarget = 32.f;
			for (int i = 0; i < 32; i++) c.setStage(i, 1000.f, q, SR);
			double e = 0, mean = 0, var = 0; std::vector<float> h;
			for (int i = 0; i < 48000; i++) { float y = c.process(i == 0 ? 1.f : 0.f); h.push_back(y); e += (double) y * y; }
			for (int i = 0; i < (int) h.size(); i++) mean += i * (double) h[i] * h[i];
			mean /= e;
			for (int i = 0; i < (int) h.size(); i++) var += (i - mean) * (i - mean) * (double) h[i] * h[i];
			return std::sqrt(var / e);
		};
		CHECK(spreadSamples(20.f) > 3.f * spreadSamples(0.7f), "pinch did not widen the dispersion");
	}

	// 3. stability: noise, max everything, all random, stage sweep
	{
		Matrix m; Rng r; r.seed(99);
		Params p; p.pinch = 1.f; p.spread = 1.f; p.drive = 1.f; p.boost = 1.f; p.stages = 96.f; p.freq = 0.9f;
		float peak = 0.f; bool ok = true;
		for (int i = 0; i < 480000; i++) {
			p.stages = 1.f + 95.f * (0.5f + 0.5f * std::sin(i * 0.0003f));
			p.freq = 0.5f + 0.5f * std::sin(i * 0.0001f);
			float y = m.process(r.bi(), p, (i % 4800) == 0, false, true, false);
			if (!std::isfinite(y)) ok = false;
			peak = std::max(peak, std::fabs(y));
		}
		CHECK(ok, "non-finite output");
		CHECK(peak <= 2.4001f, "unbounded peak %g", peak);
	}

	// 4. determinism
	{
		std::vector<float> a, b;
		for (int run = 0; run < 2; run++) {
			Matrix m; Rng r; r.seed(5); Params p; p.spread = 0.8f; p.pinch = 0.6f; p.stages = 24.f;
			for (int i = 0; i < 20000; i++) {
				float y = m.process(r.bi() * 0.5f, p, (i % 2000) == 0, false, true, false);
				(run ? b : a).push_back(y);
			}
		}
		CHECK(a == b, "not deterministic");
	}

	// 5. stepping STAGES is click-free: the worst sample-to-sample jump while
	// the count steps up and down must stay close to the steady-state jump.
	{
		Matrix m; Params p; p.stages = 4.f; p.pinch = 0.5f; p.spread = 1.f; p.drive = 0.f;
		float base = 0.f, stepMax = 0.f, prev = 0.f;
		for (int i = 0; i < 240000; i++) {
			if (i >= 48000 && i % 2000 == 0) p.stages = 4.f + (float)((i / 2000) % 17);
			float y = m.process(0.5f * std::sin(2.f * PI_F * 220.f * i / SR), p, false, false, false, false);
			float d = std::fabs(y - prev); prev = y;
			if (i < 48000 && i > 20000) base = std::max(base, d);
			if (i >= 48000) stepMax = std::max(stepMax, d);
		}
		CHECK(stepMax < 2.f * base + 0.02f, "STAGES step click: %g vs base %g", stepMax, base);
	}

	// 6. freeze: while the gate is held, clock edges do not re-roll; released, they do
	{
		auto rolls = [&](bool gate) {
			Matrix m; Params p; p.spread = 1.f; p.stages = 8.f;
			m.process(0.f, p, false, gate, true, false);   // gate rising edge (if any) rolls once
			float before = m.band[1].tgt[0];
			for (int i = 0; i < 100; i++) m.process(0.f, p, i == 50, gate, true, false);
			return m.band[1].tgt[0] != before;
		};
		CHECK(rolls(false), "clock did not roll with gate low");
		CHECK(!rolls(true), "clock rolled while frozen");
	}

	// 7. hard sweep at max pinch: bounded, and silent a moment after the input stops
	{
		Matrix m; Rng r; r.seed(7);
		Params p; p.pinch = 1.f; p.stages = 96.f; p.drive = 0.f;
		float peak = 0.f; bool ok = true;
		for (int i = 0; i < 96000; i++) {
			p.freq = 0.5f + 0.5f * std::sin(i * 0.002f);   // ~top-to-bottom in tens of ms
			float y = m.process(0.5f * r.bi(), p, false, false, false, false);
			ok = ok && std::isfinite(y); peak = std::max(peak, std::fabs(y));
		}
		CHECK(ok && peak <= 2.4001f, "sweep unstable, peak %g", peak);
		// The ring is the dispersion itself (96 stages at max pinch hold hundreds of
		// ms of group delay), so it is not silent -- but it must decay, not grow.
		double early = 0, tail = 0;
		for (int i = 0; i < 192000; i++) {
			float y = m.process(0.f, p, false, false, false, false);
			if (i < 1000) early += (double) y * y;
			if (i > 144000) tail += (double) y * y;
		}
		CHECK(early > 0.0 && tail < 0.1 * early, "tail not decaying: early %g tail %g", early, tail);
	}

	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}

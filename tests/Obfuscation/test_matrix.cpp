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

	// 2. allpass chain magnitude, fb = 0, N = 96
	{
		Chain c; c.nSm = 96.f;
		for (int i = 0; i < 96; i++) { float t = std::tan(PI_F * 1000.f / SR); c.a[i] = (t - 1.f) / (t + 1.f); }
		double acc = 0, acx = 0;
		for (int i = 0; i < 96000; i++) {
			float x = std::sin(2.f * PI_F * 3000.f * i / SR);
			float y = c.process(x, 0.f, 1.f);
			if (i > 48000) { acc += (double) y * y; acx += (double) x * x; }
		}
		CHECK(std::fabs(std::sqrt(acc / acx) - 1.0) < 0.01, "chain not unity: %g", std::sqrt(acc / acx));
	}

	// 3. stability: noise, max everything, all random, stage sweep
	{
		Matrix m; Rng r; r.seed(99);
		Params p; p.res = 1.f; p.spread = 1.f; p.drive = 1.f; p.boost = 1.f; p.stages = 96.f; p.freq = 0.9f;
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
			Matrix m; Rng r; r.seed(5); Params p; p.spread = 0.8f; p.res = 0.6f; p.stages = 24.f;
			for (int i = 0; i < 20000; i++) {
				float y = m.process(r.bi() * 0.5f, p, (i % 2000) == 0, false, true, false);
				(run ? b : a).push_back(y);
			}
		}
		CHECK(a == b, "not deterministic");
	}

	// 5. stage-count change is click-free (sine in, step N)
	{
		Matrix m; Params p; p.stages = 4.f; p.res = 0.4f;
		float maxStep = 0.f, prev = 0.f;
		for (int i = 0; i < 96000; i++) {
			if (i == 48000) p.stages = 90.f;
			float y = m.process(0.5f * std::sin(2.f * PI_F * 220.f * i / SR), p, false, false, false, false);
			if (i > 100) maxStep = std::max(maxStep, std::fabs(y - prev));
			prev = y;
		}
		CHECK(maxStep < 0.5f, "stage step click %g", maxStep);
	}

	// 6. freeze: input stops, output holds well beyond the normal tail
	{
		auto tailRms = [&](bool gate) {
			Matrix m; Params p; p.res = 0.5f; p.stages = 12.f; p.drive = 0.f;
			for (int i = 0; i < 24000; i++)
				m.process(0.5f * std::sin(2.f * PI_F * 300.f * i / SR), p, false, false, false, false);
			double acc = 0; int n = 0;
			for (int i = 0; i < 48000; i++) {
				float y = m.process(0.f, p, false, gate, false, false);
				if (i > 40000) { acc += (double) y * y; n++; }
			}
			return std::sqrt(acc / n);
		};
		double free = tailRms(false), held = tailRms(true);
		CHECK(held > 10.0 * free && held > 1e-3, "freeze did not hold: free=%g held=%g", free, held);
	}

	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}

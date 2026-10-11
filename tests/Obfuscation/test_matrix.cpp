#include "../../src/Obfuscation/Matrix.hpp"
#include <chrono>
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

	// 8. running only the stages that reach the output sounds the same as running all 96: the same noise-and-sine
	// through both while STAGES sweeps up and down, the error against the signal, and what it saves.
	{
		for (int margin : { 0, 8 }) for (float pinch : { 0.3f, 0.6f, 1.f }) {
		Matrix fast, full; Rng r; r.seed(99);
		for (int b = 0; b < 3; b++) { full.band[b].prune = false; fast.band[b].margin = margin; }
		Params p; p.pinch = pinch; p.spread = 0.5f; p.drive = 0.f; p.stages = 8.f;
		double err = 0, sig = 0; float worst = 0.f;
		for (int i = 0; i < 480000; i++) {
			p.stages = 4.f + 40.f * (0.5f + 0.5f * std::sin(i * 0.00004f));   // 4..44 and back, over ~3 s
			float x = 0.3f * r.bi() + 0.3f * std::sin(2.f * PI_F * 220.f * i / SR);
			float a = fast.process(x, p, false, false, false, false), b = full.process(x, p, false, false, false, false);
			if (i > 48000) { err += (double)(a - b) * (a - b); sig += (double) b * b; worst = std::max(worst, std::fabs(a - b)); }
		}
		const double rel = 10.0 * std::log10((err + 1e-30) / (sig + 1e-30));
		printf("   margin %2d pinch %.1f: pruned vs full while STAGES sweeps 4..44: error %.1f dB below the signal, worst sample %.4f\n", margin, pinch, -rel, worst);
		if (margin == 8) CHECK(rel < (pinch > 0.9f ? -55.0 : -80.0), "pruning changes the sound at pinch %g: error only %.1f dB down", pinch, -rel);
		}
		{
		auto cost = [&](bool prune, float stages) {
			Matrix m; for (int b = 0; b < 3; b++) m.band[b].prune = prune;
			Params q; q.stages = stages; Rng g; g.seed(3);
			volatile float sink = 0.f;
			const auto t0 = std::chrono::steady_clock::now();
			for (int i = 0; i < 480000; i++) sink = sink + m.process(0.3f * g.bi(), q, false, false, false, false);
			return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() * 100.0 / 10.0;   // percent of one core at 48 kHz
		};
		for (float st : { 8.f, 32.f, 96.f })
			printf("   %3.0f stages: %.1f%% of a core all stages, %.1f%% pruned\n", st, cost(false, st), cost(true, st));
		}
	}

	// 9. the stage's single multiply-add form against the textbook arithmetic it replaced (Zavalishin's TPT allpass: v3 = v - z2, v1 = a1 z1 + a2 v3,
	// v2 = z2 + a2 z1 + a3 v3, out = v - 2 kd v1), written out here as the reference: 96 stages at cutoffs and Qs across the range, noise in.
	{
		for (float qlo : { 0.5f, 6.f, 30.f }) {
			Chain c; c.nSm = c.nTarget = 96.f; c.prune = false;
			float rz1[MAX_STAGES] = {}, rz2[MAX_STAGES] = {};
			Rng r; r.seed(5);
			for (int i = 0; i < 96; i++) c.setStage(i, 40.f * std::exp2(0.5f * (float) i / 6.f * 1.2f) , qlo * (1.f + 0.02f * (float) i), SR);
			double err = 0, sig = 0;
			for (int n = 0; n < 192000; n++) {
				const float x = 0.4f * r.bi();
				float v = x;
				for (int i = 0; i < 96; i++) {
					const float v3 = v - rz2[i], v1 = c.a1[i] * rz1[i] + c.a2[i] * v3, v2 = rz2[i] + c.a2[i] * rz1[i] + c.a3[i] * v3;
					rz1[i] = 2.f * v1 - rz1[i]; rz2[i] = 2.f * v2 - rz2[i]; v = v - 2.f * c.kd[i] * v1;
				}
				const float y = c.process(x);
				if (n > 48000) { err += (double)(y - v) * (y - v); sig += (double) v * v; }
			}
			const double rel = 10.0 * std::log10((err + 1e-30) / (sig + 1e-30));
			printf("   Q %.1f: multiply-add form vs reference over 96 stages: error %.1f dB below the signal\n", qlo, -rel);
			CHECK(rel < -95.0, "stage form drifts from the reference: only %.1f dB down at Q %g", -rel, qlo);
		}
	}

	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}

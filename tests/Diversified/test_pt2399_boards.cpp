// Diversified 99 (Echomatic) and 100 (Little Angel) run the real PT2399 model.
//
// test_programs.cpp already sweeps every program for NaN and runaway. This is
// what only these two have to answer: that the delay is the chip's clock and
// not a buffer length, that moving the clock bends the pitch, that feedback past
// unity is bounded by the chip rather than by a function added for the purpose,
// and that the chorus's modulation really is the clock's.

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

static void testAngelModulatesTheClock() {
	printf("Little Angel: vibe is the clock's own pitch modulation\n");
	auto run = [](float mode, std::vector<float>& L, std::vector<float>& R) {
		MiawRack m; m.init(); m.setSampleRate(SR); m.clearAll();
		MiawCtx c = ctx(0.3f, 1.f, mode);   // SPEED, DEPTH full
		m.setParams(divfx::MW_ANGEL, c);
		const int n = (int) (2.f * SR);
		L.resize(n); R.resize(n);
		for (int i = 0; i < n; i++) {
			float in = 2.f * std::sin(2.f * (float) M_PI * 400.f * i / SR);
			m.process(divfx::MW_ANGEL, c, in, in, L[i], R[i]);
		}
	};
	std::vector<float> L, R;
	run(0.7f, L, R);                         // vibe / normal: no dry path
	// Pitch swings with the LFO; measure the spread of local frequency.
	float lo = 1e9f, hi = 0.f;
	for (int w = (int) (0.5f * SR); w + 2400 < (int) (2.f * SR); w += 1200) {
		float f = freqOf(L, w, w + 2400);
		lo = std::fmin(lo, f); hi = std::fmax(hi, f);
	}
	CHECK(hi - lo > 12.f, "vibe pitch spread only %.1f Hz (%.1f..%.1f)", hi - lo, lo, hi);

	double d = 0.0;
	for (size_t i = (size_t) SR; i < L.size(); i++) d += std::fabs(L[i] - R[i]);
	CHECK(d > 1.0, "the two chips should differ (quarter-cycle LFO), sum |L-R| = %.3f", d);

	// Chorus keeps the dry path: output carries the input's own level.
	std::vector<float> L2, R2;
	run(0.1f, L2, R2);
	float pk = 0.f;
	for (size_t i = (size_t) SR; i < L2.size(); i++) pk = std::fmax(pk, std::fabs(L2[i]));
	CHECK(pk > 2.5f, "chorus output peak %.2f V; dry (2 V) plus wet expected", pk);
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
		{ divfx::MW_ANGEL,     "100 Little Angel, deep", 0.9f, 1.0f, 0.6f },
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
	testAngelModulatesTheClock();
	testFusedPassesAreExact();
	benchmark();
	printf("\n%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}

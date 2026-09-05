// Retroactive — offline test harness. No Rack headers, no gtest.
// Every CHECK runs; failures are counted and reported at the end.
//
//   make && ./test_permuter            # unit tests + sanitizers
//   ./test_permuter --render out/      # render WAVs to listen to

#include "../../src/Retroactive/dsp/WindowPermuter.hpp"
#include "wav.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

using retroactive::WindowPermuter;
using retroactive::ClockSync;

static int g_fail = 0;
static int g_checks = 0;
static const char* g_test = "";

#define CHECK(cond, msg) do { \
	g_checks++; \
	if (!(cond)) { \
		g_fail++; \
		std::printf("  FAIL [%s] %s   (%s:%d)\n", g_test, (msg), __FILE__, __LINE__); \
	} \
} while (0)

static void startTest(const char* name) { g_test = name; std::printf("%s\n", name); }

static const float kSentinel = -12345.f;
static bool isSentinel(float v) { return v == kSentinel; }

// ---------------------------------------------------------------------------
// helpers

struct Run {
	std::vector<float> outL, outR;
};

/** Drives the permuter with `in` on both lanes. */
static Run drive(WindowPermuter& p, const std::vector<float>& in) {
	Run r;
	r.outL.resize(in.size());
	r.outR.resize(in.size());
	for (size_t t = 0; t < in.size(); t++)
		p.process(in[t], in[t], r.outL[t], r.outR[t]);
	return r;
}

static WindowPermuter makeCore(float sr, int64_t n, int s, int mode, int64_t fade, float mix = 1.f) {
	WindowPermuter p;
	p.setSampleRate(sr);
	p.setWindowSamples(n);
	p.setSubdivisions(s);
	p.setMode(mode);
	p.setFadeSamples(fade);
	p.setMix(mix);
	return p;
}

static const char* kModeName[] = {
	"identity", "reverse", "blockreverse", "blockinternal",
	"shuffle", "pairwise", "stutter", "scatter"
};

static bool isBijective(int mode) {
	return mode != WindowPermuter::MODE_STUTTER && mode != WindowPermuter::MODE_SCATTER;
}

// ---------------------------------------------------------------------------
// T1 — exact reverse

static void t1() {
	startTest("T1  exact reverse, bit-exact");
	const int64_t N = 64;
	WindowPermuter p = makeCore(48000.f, N, 1, WindowPermuter::MODE_REVERSE, 0);
	std::vector<float> in(4 * N);
	for (size_t t = 0; t < in.size(); t++) in[t] = (float)t;
	Run r = drive(p, in);

	CHECK(p.getWindowSamples() == N, "N latched to 64");
	CHECK(p.getFadeSamples() == 0, "fade 0 is a hard cut");
	CHECK(p.getLatencySamples() == N, "latency == N when L == 0");
	for (int64_t i = 0; i < N; i++)
		CHECK(r.outL[N + i] == in[N - 1 - i], "out[N+i] == x[N-1-i]");
	// f(i) = N-1-i holds for any S, since REVERSE is block-reverse composed with
	// internal-reverse.
	for (int S = 2; S <= 16; S *= 2) {
		WindowPermuter q = makeCore(48000.f, N, S, WindowPermuter::MODE_REVERSE, 0);
		Run rq = drive(q, in);
		bool same = true;
		for (int64_t i = 0; i < N; i++) if (rq.outL[N + i] != in[N - 1 - i]) same = false;
		CHECK(same, "REVERSE == whole-window reverse for every S");
	}
}

// ---------------------------------------------------------------------------
// T2 — identity is a pure delay; mode equivalences at S=1

static void t2() {
	startTest("T2  identity == pure delay; S=1 equivalences");
	const int64_t N = 512;
	std::vector<float> in(4 * N);
	for (size_t t = 0; t < in.size(); t++) in[t] = std::sin(0.01f * (float)t) * 0.7f;

	WindowPermuter p = makeCore(48000.f, N, 1, WindowPermuter::MODE_IDENTITY, 0);
	Run r = drive(p, in);
	CHECK(p.getLatencySamples() == N, "identity latency is N, not 0");
	bool ok = true;
	for (size_t t = (size_t)N; t < in.size(); t++) if (r.outL[t] != in[t - N]) ok = false;
	CHECK(ok, "out[t] == x[t-N] bit-exact");

	WindowPermuter a = makeCore(48000.f, N, 1, WindowPermuter::MODE_BLOCK_REVERSE, 0);
	Run ra = drive(a, in);
	bool eqIdent = true;
	for (size_t t = 0; t < in.size(); t++) if (ra.outL[t] != r.outL[t]) eqIdent = false;
	CHECK(eqIdent, "BLOCK_REVERSE@S=1 == IDENTITY");

	WindowPermuter b = makeCore(48000.f, N, 1, WindowPermuter::MODE_BLOCK_INTERNAL, 0);
	WindowPermuter c = makeCore(48000.f, N, 1, WindowPermuter::MODE_REVERSE, 0);
	Run rb = drive(b, in);
	Run rc = drive(c, in);
	bool eqRev = true;
	for (size_t t = 0; t < in.size(); t++) if (rb.outL[t] != rc.outL[t]) eqRev = false;
	CHECK(eqRev, "BLOCK_INTERNAL@S=1 == REVERSE");
}

// ---------------------------------------------------------------------------
// T3 — warm-up emits silence, never the sentinel

static void t3() {
	startTest("T3  warm-up silence, no sentinel leakage");
	const int64_t N = 4096;
	const int64_t F = 144;
	for (int mode = 0; mode < WindowPermuter::NUM_MODES; mode++) {
		for (int ch = 0; ch < 2; ch++) {
			for (int S = 1; S <= 16; S *= 4) {
				WindowPermuter p = makeCore(48000.f, N, S, mode, F);
				p.setCharacter(ch);
				std::vector<float> in(3 * N, 0.f);
				for (size_t t = 0; t < in.size(); t++) in[t] = std::sin(0.05f * (float)t);
				Run r = drive(p, in);
				bool zeros = true, clean = true;
				for (int64_t t = 0; t < p.getWindowSamples(); t++)
					if (r.outL[t] != 0.f || r.outR[t] != 0.f) zeros = false;
				for (size_t t = 0; t < in.size(); t++)
					if (isSentinel(r.outL[t]) || isSentinel(r.outR[t])) clean = false;
				CHECK(zeros, "first window is exactly silent");
				CHECK(clean, "sentinel never reaches the output");
			}
		}
	}
	// IDENTITY additionally has a flat N+L delay, so the whole warm-up is silent.
	WindowPermuter p = makeCore(48000.f, N, 1, WindowPermuter::MODE_IDENTITY, F);
	std::vector<float> in(3 * N);
	for (size_t t = 0; t < in.size(); t++) in[t] = 1.f;
	Run r = drive(p, in);
	int64_t lat = p.getLatencySamples();
	CHECK(lat == N + p.getFadeSamples(), "latency == N + L");
	bool silent = true;
	for (int64_t t = 0; t < lat; t++) if (r.outL[t] != 0.f) silent = false;
	CHECK(silent, "IDENTITY silent for t < N+L");
}

// ---------------------------------------------------------------------------
// T4 — bijectivity

static void t4() {
	startTest("T4  bijective modes permute the source window");
	const int64_t N = 1024;
	for (int mode = 0; mode < WindowPermuter::NUM_MODES; mode++) {
		if (!isBijective(mode)) continue;
		for (int S = 1; S <= 32; S *= 2) {
			WindowPermuter p = makeCore(48000.f, N, S, mode, 0);
			std::vector<float> in(6 * N);
			for (size_t t = 0; t < in.size(); t++) in[t] = (float)(t + 1);
			Run r = drive(p, in);
			// Window 3 of output (well past warm-up) reads source window 2.
			std::vector<float> got(r.outL.begin() + 3 * N, r.outL.begin() + 4 * N);
			std::vector<float> want(in.begin() + 2 * N, in.begin() + 3 * N);
			std::sort(got.begin(), got.end());
			std::sort(want.begin(), want.end());
			CHECK(got == want, "output window is a permutation of the source window");
		}
	}
}

// ---------------------------------------------------------------------------
// T5 — N changes mid-stream

static void t5() {
	startTest("T5  N change mid-stream, 200k samples");
	// Pass A: fade=0 with a distinct-value ramp. Every output must be a value that
	// was genuinely written, so a stale or unwritten read is visible directly.
	{
		WindowPermuter p = makeCore(48000.f, 4800, 1, WindowPermuter::MODE_REVERSE, 0);
		unsigned st = 12345u;
		bool badVal = false, sentinel = false, nonFinite = false;
		int mode = 0;
		for (int64_t t = 0; t < 200000; t++) {
			if (t % 1000 == 0) {
				st = st * 1103515245u + 12345u;
				int64_t n = 64 + (int64_t)((st >> 8) % 19937u);
				p.setWindowSamples(n);
				p.setSubdivisions(1 << (int)((st >> 3) % 5u));
				p.setMode(mode);
				mode = (mode + 1) % WindowPermuter::NUM_MODES;
			}
			float in = (float)(t + 1);
			float oL, oR;
			p.process(in, in, oL, oR);
			if (!std::isfinite(oL) || !std::isfinite(oR)) nonFinite = true;
			if (isSentinel(oL) || isSentinel(oR)) sentinel = true;
			if (!(oL == 0.f || (oL >= 1.f && oL <= (float)(t + 1)))) badVal = true;
		}
		CHECK(!nonFinite, "all outputs finite");
		CHECK(!sentinel, "no sentinel ever reaches the output");
		CHECK(!badVal, "every output is a sample that was actually written");
	}
	// Pass B: same schedule with a real fade and a bounded signal.
	{
		WindowPermuter p = makeCore(48000.f, 4800, 1, WindowPermuter::MODE_REVERSE, 144);
		unsigned st = 777u;
		bool nonFinite = false, tooLoud = false, sentinel = false;
		int mode = 0;
		for (int64_t t = 0; t < 200000; t++) {
			if (t % 1000 == 0) {
				st = st * 1103515245u + 12345u;
				p.setWindowSamples(64 + (int64_t)((st >> 8) % 19937u));
				p.setSubdivisions(1 << (int)((st >> 3) % 6u));
				p.setMode(mode);
				p.setCharacter((int)((st >> 20) % 2u));
				mode = (mode + 1) % WindowPermuter::NUM_MODES;
			}
			float in = 0.9f * std::sin(0.013f * (float)t);
			float oL, oR;
			p.process(in, in, oL, oR);
			if (!std::isfinite(oL) || !std::isfinite(oR)) nonFinite = true;
			if (isSentinel(oL) || isSentinel(oR)) sentinel = true;
			if (std::fabs(oL) > 1.5f * 0.9f) tooLoud = true;
		}
		CHECK(!nonFinite, "all outputs finite with fade + overlap");
		CHECK(!sentinel, "no sentinel with fade + overlap");
		CHECK(!tooLoud, "|out| <= 1.5 * max|in|");
	}
}

// ---------------------------------------------------------------------------
// T6 — seam click, quantified, with a negative control

static double maxStep(const std::vector<float>& v, size_t from, size_t to) {
	double m = 0.0;
	for (size_t t = from + 1; t < to; t++) {
		double d = std::fabs((double)v[t] - (double)v[t - 1]);
		if (d > m) m = d;
	}
	return m;
}

static void t6() {
	startTest("T6  seam click bounded by the fade (with negative control)");
	const float sr = 48000.f;
	const int64_t N = 4800;
	// Chosen so consecutive windows land ~pi apart: the seam is near-maximal.
	const double f = 48000.0 * 87.5 / (2.0 * (double)N - 1.0);
	const double natural = 2.0 * 3.14159265358979 * f / (double)sr;   // max per-sample slope
	const double bound = 4.0 * natural;

	std::vector<float> in(20 * N);
	for (size_t t = 0; t < in.size(); t++)
		in[t] = (float)std::sin(2.0 * 3.14159265358979 * f * (double)t / (double)sr);

	WindowPermuter faded = makeCore(sr, N, 1, WindowPermuter::MODE_REVERSE, (int64_t)(0.003f * sr));
	Run rf = drive(faded, in);
	double mf = maxStep(rf.outL, (size_t)(2 * N), in.size());

	WindowPermuter hard = makeCore(sr, N, 1, WindowPermuter::MODE_REVERSE, 0);
	Run rh = drive(hard, in);
	double mh = maxStep(rh.outL, (size_t)(2 * N), in.size());

	std::printf("  natural slope %.5f, bound %.5f, fade=3ms %.5f, fade=0 %.5f\n",
	            natural, bound, mf, mh);
	CHECK(mf < bound, "3 ms fade keeps the seam under 4x the natural slope");
	CHECK(mh > bound, "negative control: fade=0 breaks the same bound");
}

// ---------------------------------------------------------------------------
// T7 — RMS conservation

static double rms(const std::vector<float>& v, size_t from, size_t to) {
	double a = 0.0;
	for (size_t t = from; t < to; t++) a += (double)v[t] * (double)v[t];
	return std::sqrt(a / (double)(to - from));
}

static void t7() {
	startTest("T7  RMS conservation on the bijective modes");
	const int64_t N = 4800;
	std::vector<float> in(24 * N);
	unsigned st = 4242u;
	for (size_t t = 0; t < in.size(); t++) {
		st = st * 1103515245u + 12345u;
		in[t] = ((float)((st >> 9) & 0xffffu) / 32768.f - 1.f) * 0.5f;
	}
	double ref = rms(in, 2 * N, in.size() - N);
	for (int mode = 0; mode < WindowPermuter::NUM_MODES; mode++) {
		if (!isBijective(mode)) continue;      // STUTTER/SCATTER do not conserve energy
		WindowPermuter p = makeCore(48000.f, N, 8, mode, (int64_t)(0.003f * 48000.f));
		Run r = drive(p, in);
		double got = rms(r.outL, 3 * N, in.size());
		double db = 20.0 * std::log10(got / ref);
		CHECK(std::fabs(db) < 1.0, "RMS within +/-1 dB of the input");
		if (std::fabs(db) >= 1.0)
			std::printf("    mode %s: %.3f dB\n", kModeName[mode], db);
	}
}

// ---------------------------------------------------------------------------
// T8 — stereo phase lock

static void t8() {
	startTest("T8  stereo lanes are phase-locked");
	std::vector<float> in(50000);
	for (size_t t = 0; t < in.size(); t++) in[t] = (float)(t % 997) / 997.f - 0.5f;
	for (int mode = 0; mode < WindowPermuter::NUM_MODES; mode++) {
		for (int ch = 0; ch < 2; ch++) {
			WindowPermuter p = makeCore(48000.f, 3001, 8, mode, 144);
			p.setCharacter(ch);
			Run r = drive(p, in);
			bool same = true;
			for (size_t t = 0; t < in.size(); t++) if (r.outL[t] != r.outR[t]) same = false;
			CHECK(same, "outL == outR bit-exact for identical inputs");
		}
	}
}

// ---------------------------------------------------------------------------
// T9 — determinism

static void t9() {
	startTest("T9  seeded randomness is reproducible");
	std::vector<float> in(60000);
	for (size_t t = 0; t < in.size(); t++) in[t] = std::sin(0.007f * (float)t);

	// PAIRWISE_SWAP is deterministic by construction, so it is not in this list.
	static const int randomModes[] = {
		WindowPermuter::MODE_BLOCK_SHUFFLE,
		WindowPermuter::MODE_STUTTER,
		WindowPermuter::MODE_SCATTER
	};
	for (int mi = 0; mi < 3; mi++) {
		const int mode = randomModes[mi];
		WindowPermuter a = makeCore(48000.f, 2048, 16, mode, 96);
		a.setSeed(0xC0FFEEu);
		a.reset();
		Run ra = drive(a, in);

		WindowPermuter b = makeCore(48000.f, 2048, 16, mode, 96);
		b.setSeed(0xC0FFEEu);
		b.reset();
		Run rb = drive(b, in);

		bool same = true;
		for (size_t t = 0; t < in.size(); t++) if (ra.outL[t] != rb.outL[t]) same = false;
		CHECK(same, "same seed gives bit-identical output");

		// reset() restores it on the same instance.
		a.reset();
		Run rc = drive(a, in);
		bool restored = true;
		for (size_t t = 0; t < in.size(); t++) if (ra.outL[t] != rc.outL[t]) restored = false;
		CHECK(restored, "reset() restores the pattern");

		WindowPermuter c = makeCore(48000.f, 2048, 16, mode, 96);
		c.setSeed(0xBADF00Du);
		c.reset();
		Run rd = drive(c, in);
		bool differs = false;
		for (size_t t = 0; t < in.size(); t++) if (ra.outL[t] != rd.outL[t]) differs = true;
		CHECK(differs, "a different seed gives a different pattern");
	}
}

// ---------------------------------------------------------------------------
// T10 — sample-rate change

static void t10() {
	startTest("T10 sample-rate change mid-stream");
	WindowPermuter p = makeCore(48000.f, 12000, 4, WindowPermuter::MODE_SCATTER, 144);
	float oL, oR;
	for (int t = 0; t < 60000; t++) p.process(std::sin(0.01f * t), std::sin(0.01f * t), oL, oR);
	p.setSampleRate(96000.f);        // fires on module-add too: must zero the ring
	bool clean = true;
	for (int t = 0; t < 300000; t++) {
		p.process(std::sin(0.005f * t), std::sin(0.005f * t), oL, oR);
		if (!std::isfinite(oL) || !std::isfinite(oR) || isSentinel(oL)) clean = false;
	}
	CHECK(clean, "no NaN, no sentinel across a rate change");

	p.setSubdivisions(1);
	p.setWindowSeconds(0.25f);
	for (int t = 0; t < 60000; t++) p.process(0.f, 0.f, oL, oR);
	CHECK(p.getSampleRate() == 96000.f, "sample rate updated");
	CHECK(p.getWindowSamples() == 24000, "setWindowSeconds(0.25) == 24000 @ 96k");

	// setSampleRate is idempotent (Rack calls it on module add).
	WindowPermuter q;
	q.setSampleRate(48000.f);
	q.setSampleRate(48000.f);
	q.setWindowSamples(1000);
	q.setSubdivisions(1);
	q.setMode(WindowPermuter::MODE_IDENTITY);
	q.setFadeSamples(0);
	std::vector<float> in(5000, 1.f);
	Run r = drive(q, in);
	bool silentWarm = true;
	for (int t = 0; t < 1000; t++) if (r.outL[t] != 0.f) silentWarm = false;
	CHECK(silentWarm, "repeat setSampleRate still zeroes the ring");
}

// ---------------------------------------------------------------------------
// T11 — impulse train

static void t11() {
	startTest("T11 impulse train");
	const int64_t N = 2000;
	const int64_t period = 137;
	std::vector<float> in(10 * N, 0.f);
	for (size_t t = 0; t < in.size(); t += (size_t)period) in[t] = 1.f;

	WindowPermuter p = makeCore(48000.f, N, 1, WindowPermuter::MODE_IDENTITY, 0);
	Run r = drive(p, in);
	bool exact = true;
	for (size_t t = (size_t)N; t < in.size(); t++)
		if (r.outL[t] != in[t - N]) exact = false;
	CHECK(exact, "IDENTITY reproduces impulses exactly N+L later at full amplitude");

	for (int mode = 0; mode < WindowPermuter::NUM_MODES; mode++) {
		if (!isBijective(mode)) continue;
		WindowPermuter q = makeCore(48000.f, N, 8, mode, 0);
		Run rq = drive(q, in);
		int srcCount = 0, outCount = 0;
		for (int64_t i = 0; i < N; i++) {
			if (in[(size_t)(2 * N + i)] == 1.f) srcCount++;
			if (rq.outL[(size_t)(3 * N + i)] == 1.f) outCount++;
		}
		CHECK(srcCount == outCount, "bijective modes preserve the impulse count per window");
	}
}

// ---------------------------------------------------------------------------
// T12 — null test

static void t12() {
	startTest("T12 mix=0 is bit-identical to the input");
	std::vector<float> in(30000);
	for (size_t t = 0; t < in.size(); t++) in[t] = std::sin(0.021f * (float)t) * 0.83f;
	for (int mode = 0; mode < WindowPermuter::NUM_MODES; mode++) {
		WindowPermuter p = makeCore(48000.f, 3333, 4, mode, 96, 0.f);
		Run r = drive(p, in);
		bool same = true;
		for (size_t t = 0; t < in.size(); t++)
			if (r.outL[t] != in[t] || r.outR[t] != in[t]) same = false;
		CHECK(same, "output == input bit-exact at mix=0");
	}
}

// ---------------------------------------------------------------------------
// extras: fade-collapse flag, freeze, and the clock tracker

static void tExtra() {
	startTest("EX  overdraft, freeze, clock sync");

	// Fade collapse: requested fade >= B/2 means every block is 100% crossfade.
	WindowPermuter p = makeCore(48000.f, 4800, 1, WindowPermuter::MODE_REVERSE, 144);
	float oL, oR;
	for (int t = 0; t < 20000; t++) p.process(0.f, 0.f, oL, oR);
	CHECK(!p.isOverdraft(), "S=1, N=100ms, 3ms fade is not overdrawn");
	p.setSubdivisions(64);
	for (int t = 0; t < 20000; t++) p.process(0.f, 0.f, oL, oR);
	CHECK(p.isOverdraft(), "S=64 at the same N overdraws the fade");

	// Freeze holds the source region: the output becomes periodic at N.
	WindowPermuter q = makeCore(48000.f, 1024, 1, WindowPermuter::MODE_IDENTITY, 0);
	std::vector<float> in(8192);
	for (size_t t = 0; t < in.size(); t++) in[t] = std::sin(0.03f * (float)t);
	for (size_t t = 0; t < 4096; t++) q.process(in[t], in[t], oL, oR);
	q.setFreeze(true);
	std::vector<float> a(1024), b(1024);
	for (int k = 0; k < 1024; k++) q.process(0.f, 0.f, a[k], oR);
	for (int k = 0; k < 1024; k++) q.process(0.f, 0.f, b[k], oR);
	CHECK(a == b, "freeze loops the same window bit-exactly");

	// Clock: one edge cannot give a period; the second adopts it.
	ClockSync cs;
	cs.setSampleRate(48000.f);
	cs.process(true);
	CHECK(!cs.isLocked(), "one edge does not lock");
	for (int t = 0; t < 23999; t++) cs.process(false);
	cs.process(true);
	CHECK(cs.isLocked(), "second edge locks");
	CHECK(cs.period() == 24000, "period == 24000 (0.5 s @ 48k)");

	// Small jitter is smoothed by 0.25, not tracked one-for-one.
	for (int t = 0; t < 24399; t++) cs.process(false);
	cs.process(true);
	CHECK(cs.period() == 24100, "12.5%-window jitter is smoothed by 0.25");

	// A big change snaps.
	for (int t = 0; t < 11999; t++) cs.process(false);
	cs.process(true);
	CHECK(cs.period() == 12000, "out-of-tolerance change snaps");

	// Stopped edges keep the last period and go stale rather than snapping back.
	for (int t = 0; t < 24001; t++) cs.process(false);
	CHECK(cs.isStale(), "stopped clock goes stale");
	CHECK(cs.period() == 12000, "stale clock keeps its last period");
}

// ---------------------------------------------------------------------------
// rendering

static void makeSource(std::vector<float>& l, std::vector<float>& r, int sr, double seconds) {
	size_t n = (size_t)(seconds * sr);
	l.assign(n, 0.f);
	r.assign(n, 0.f);
	const double bpm = 120.0;
	const double beat = 60.0 / bpm;              // 0.5 s
	const double pi = 3.14159265358979;
	// Arpeggio over Am7: A2 D3 E3 G3 A3 C4 E4 A4-ish, one note per eighth.
	static const double arp[] = {110.0, 146.83, 164.81, 196.0, 220.0, 261.63, 329.63, 440.0};
	int nEighths = (int)(seconds / (beat * 0.5));
	for (int k = 0; k < nEighths; k++) {
		double f = arp[k % 8];
		size_t t0 = (size_t)(k * beat * 0.5 * sr);
		double dur = beat * 0.5 * 0.9;
		size_t len = (size_t)(dur * sr);
		double pan = 0.5 + 0.35 * std::sin(k * 0.7);
		for (size_t j = 0; j < len && t0 + j < n; j++) {
			double env = std::exp(-4.0 * (double)j / (dur * sr));
			double s = 0.28 * env * (std::sin(2 * pi * f * j / sr)
			                         + 0.35 * std::sin(4 * pi * f * j / sr));
			l[t0 + j] += (float)(s * (1.0 - pan));
			r[t0 + j] += (float)(s * pan);
		}
	}
	// Decaying sine pings on every beat, an octave down, for the low end.
	int nBeats = (int)(seconds / beat);
	for (int k = 0; k < nBeats; k++) {
		size_t t0 = (size_t)(k * beat * sr);
		double f = (k % 4 == 0) ? 55.0 : 82.41;
		size_t len = (size_t)(beat * 0.8 * sr);
		for (size_t j = 0; j < len && t0 + j < n; j++) {
			double env = std::exp(-6.0 * (double)j / (beat * 0.8 * sr));
			double s = 0.35 * env * std::sin(2 * pi * f * j / sr);
			l[t0 + j] += (float)s;
			r[t0 + j] += (float)s;
		}
	}
}

static int render(const std::string& dir) {
	const int sr = 48000;
	std::vector<float> inL, inR;
	makeSource(inL, inR, sr, 8.0);
	std::string d = dir;
	if (!d.empty() && d[d.size() - 1] != '/') d += "/";
	std::string mk = "mkdir -p \"" + d + "\"";
	if (std::system(mk.c_str()) != 0) { std::printf("cannot create %s\n", d.c_str()); return 1; }

	wav::writeStereo16(d + "source.wav", inL, inR, sr);
	int written = 1;

	static const int subdivs[] = {1, 4, 16};
	for (int mode = 0; mode < WindowPermuter::NUM_MODES; mode++) {
		for (int si = 0; si < 3; si++) {
			WindowPermuter p;
			p.setSampleRate((float)sr);
			p.setWindowSeconds(0.25f);
			p.setSubdivisions(subdivs[si]);
			p.setMode(mode);
			p.setFadeSeconds(0.003f);
			p.setMix(1.f);
			p.setSeed(0x51EEDu);
			p.reset();
			std::vector<float> oL(inL.size()), oR(inL.size());
			for (size_t t = 0; t < inL.size(); t++) p.process(inL[t], inR[t], oL[t], oR[t]);
			char name[256];
			std::snprintf(name, sizeof(name), "out_%s_%d.wav", kModeName[mode], subdivs[si]);
			wav::writeStereo16(d + name, oL, oR, sr);
			written++;
		}
	}

	// Overlap character, on the modes where it reads differently.
	for (int mode = 0; mode < WindowPermuter::NUM_MODES; mode++) {
		WindowPermuter p;
		p.setSampleRate((float)sr);
		p.setWindowSeconds(0.25f);
		p.setSubdivisions(4);
		p.setMode(mode);
		p.setCharacter(WindowPermuter::CHAR_OVERLAP);
		p.setFadeSeconds(0.003f);
		p.setSeed(0x51EEDu);
		p.reset();
		std::vector<float> oL(inL.size()), oR(inL.size());
		for (size_t t = 0; t < inL.size(); t++) p.process(inL[t], inR[t], oL[t], oR[t]);
		char name[256];
		std::snprintf(name, sizeof(name), "overlap_%s_4.wav", kModeName[mode]);
		wav::writeStereo16(d + name, oL, oR, sr);
		written++;
	}

	// N swept 20 ms -> 2 s over 30 s, to audition the window-length-change behaviour.
	{
		std::vector<float> sL, sR;
		makeSource(sL, sR, sr, 30.0);
		WindowPermuter p;
		p.setSampleRate((float)sr);
		p.setSubdivisions(1);
		p.setMode(WindowPermuter::MODE_REVERSE);
		p.setFadeSeconds(0.003f);
		p.reset();
		std::vector<float> oL(sL.size()), oR(sL.size());
		for (size_t t = 0; t < sL.size(); t++) {
			double u = (double)t / (double)sL.size();
			double sec = 0.02 * std::pow(100.0, u);      // 20 ms -> 2 s
			p.setWindowSeconds((float)sec);
			p.process(sL[t], sR[t], oL[t], oR[t]);
		}
		wav::writeStereo16(d + "sweep.wav", oL, oR, sr);
		written++;
	}

	std::printf("rendered %d files to %s\n", written, d.c_str());
	return 0;
}

// ---------------------------------------------------------------------------

int main(int argc, char** argv) {
	if (argc >= 2 && std::strcmp(argv[1], "--render") == 0)
		return render(argc >= 3 ? argv[2] : "out/");

	t1(); t2(); t3(); t4(); t5(); t6(); t7(); t8(); t9(); t10(); t11(); t12();
	tExtra();

	std::printf("\n%d checks, %d failures\n", g_checks, g_fail);
	return g_fail ? 1 : 0;
}

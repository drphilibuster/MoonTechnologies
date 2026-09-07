// Does the exciter actually put the harmonics where the chord says?
//
// Measured with a DFT at each harmonic rather than argued from the identity:
// the identity is only true at unit amplitude, and the whole module is an
// argument about what happens when that does or does not hold.
#include "../../src/Dependents/Chebyshev.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace dependents;

static int checks = 0;
static int failures = 0;

static void fail(const char* what, const char* detail) {
	failures++;
	printf("  FAIL  %s: %s\n", what, detail);
}

static const float kFs = 48000.f;
static const float kF0 = 100.f;
static const int kN = 4800;          // exactly 10 cycles of f0: no leakage

/** Amplitude at the h'th harmonic of kF0. */
static float harmonic(const std::vector<float>& y, int h) {
	double re = 0.0, im = 0.0;
	for (int i = 0; i < (int)y.size(); i++) {
		double a = 2.0 * M_PI * (double)h * kF0 * (double)i / kFs;
		re += y[i] * std::cos(a);
		im -= y[i] * std::sin(a);
	}
	return (float)(2.0 * std::sqrt(re * re + im * im) / (double)y.size());
}

int main() {
	// --- T_n on a unit sine is the nth harmonic and nothing else -------------
	printf("  the identity...\n");
	{
		for (int n = 1; n <= 8; n++) {
			std::vector<float> y;
			for (int i = 0; i < kN; i++) {
				float t[kMaxHarmonic + 1];
				chebyshev(std::cos(2.f * (float)M_PI * kF0 * (float)i / kFs), n, t);
				y.push_back(t[n]);
			}
			float want = harmonic(y, n);
			float spill = 0.f;
			for (int h = 1; h <= 20; h++)
				if (h != n) spill = std::fmax(spill, harmonic(y, h));
			checks++;
			if (std::fabs(want - 1.f) > 0.02f || spill > 0.02f) {
				char d[160];
				snprintf(d, sizeof d, "T_%d gave %.3f at harmonic %d and %.3f"
				         " somewhere else; wanted 1.000 and nothing",
				         n, (double)want, n, (double)spill);
				fail("identity", d);
			}
		}
	}

	// --- every chord in the table sounds its own harmonics -------------------
	// This is the one that catches a typo in kChords: a wrong number is still a
	// chord, just not the one it is labelled.
	printf("  the chord table...\n");
	{
		for (int c = 0; c < kChordCount; c++) {
			const Chord& ch = kChords[c];
			// CUSTOM has no members of its own -- its weights come from the
			// panel. Left in, it would pass this check by producing silence,
			// which is the sort of pass that hides a real one going missing.
			if (ch.count == 0) continue;
			float w[kMaxHarmonic + 1];
			voicing(ch, 0.f, w);
			std::vector<float> y;
			for (int i = 0; i < kN; i++)
				y.push_back(excite(std::cos(2.f * (float)M_PI * kF0 * (float)i / kFs), w));
			// every member present, and nothing else above the floor
			float weakest = 1e9f, spill = 0.f;
			for (int h = 1; h <= 20; h++) {
				bool member = false;
				for (int k = 0; k < ch.count; k++) if (ch.n[k] == h) member = true;
				float a = harmonic(y, h);
				if (member) weakest = std::fmin(weakest, a);
				else spill = std::fmax(spill, a);
			}
			checks++;
			if (weakest < 0.05f || spill > 0.02f) {
				char d[200];
				snprintf(d, sizeof d, "%s: weakest member %.3f, loudest stray %.3f",
				         ch.name, (double)weakest, (double)spill);
				fail("chord table", d);
			}
		}
	}

	// --- the catch, stated as a measurement ----------------------------------
	// Half amplitude in, and the major triad is no longer a major triad. This
	// is why the module normalises; the test is here so that reason cannot
	// quietly stop being true.
	printf("  amplitude collapses the chord...\n");
	{
		float w[kMaxHarmonic + 1];
		voicing(kChords[3], 0.f, w);                       // MAJOR, 4:5:6
		std::vector<float> y;
		for (int i = 0; i < kN; i++)
			y.push_back(excite(0.5f * std::cos(2.f * (float)M_PI * kF0 * (float)i / kFs), w));
		float stray = 0.f;
		for (int h = 1; h <= 3; h++) stray = std::fmax(stray, harmonic(y, h));
		checks++;
		if (!(stray > 0.05f)) {
			char d[160];
			snprintf(d, sizeof d, "at half amplitude the strongest non-member was"
			         " %.3f -- the chord did not collapse, so the identity is"
			         " being satisfied somewhere it should not be", (double)stray);
			fail("amplitude", d);
		}
	}

	// --- and that normalising fixes it ---------------------------------------
	printf("  normalising holds the chord together...\n");
	{
		float w[kMaxHarmonic + 1];
		voicing(kChords[3], 0.f, w);
		Unity u;
		u.setRate(kFs);
		std::vector<float> y;
		// a full second first, so the follower has settled before measuring
		for (int i = 0; i < (int)kFs; i++) {
			float s = 0.12f * std::cos(2.f * (float)M_PI * kF0 * (float)i / kFs);
			u.gain(s);
		}
		for (int i = 0; i < kN; i++) {
			float s = 0.12f * std::cos(2.f * (float)M_PI * kF0 * (float)i / kFs);
			y.push_back(excite(s * u.gain(s), w));
		}
		float weakest = 1e9f, spill = 0.f;
		for (int h = 1; h <= 20; h++) {
			bool member = (h == 4 || h == 5 || h == 6);
			float a = harmonic(y, h);
			if (member) weakest = std::fmin(weakest, a);
			else spill = std::fmax(spill, a);
		}
		checks++;
		if (weakest < 0.05f || spill > 0.02f) {
			char d[200];
			snprintf(d, sizeof d, "a signal at 12%% normalised to unity gave"
			         " weakest member %.3f and loudest stray %.3f",
			         (double)weakest, (double)spill);
			fail("normalise", d);
		}
	}

	// --- CUSTOM is the last entry, and the only empty one ---------------------
	// The module tells a preset from a drawn spectrum by count == 0, so exactly
	// one entry may have it and it has to be the last, or adding a preset would
	// silently renumber every saved patch.
	printf("  the custom slot...\n");
	{
		int empty = 0, lastEmpty = -1;
		for (int c = 0; c < kChordCount; c++)
			if (kChords[c].count == 0) { empty++; lastEmpty = c; }
		checks++;
		if (empty != 1 || lastEmpty != kChordCount - 1) {
			char d[160];
			snprintf(d, sizeof d, "%d entries have no members and the last of them"
			         " is at %d of %d; wanted exactly one, at the end",
			         empty, lastEmpty, kChordCount - 1);
			fail("custom slot", d);
		}
	}

	// --- morphing is a spectrum, not two chords at once -----------------------
	// Halfway from major to minor, 5 should be half down and 12 and 15 half up,
	// and the level must not step as the count of sounding partials changes.
	printf("  morphing major to minor...\n");
	{
		float a[kMaxHarmonic + 1], b[kMaxHarmonic + 1], m[kMaxHarmonic + 1];
		voicing(kChords[3], 0.f, a);            // MAJOR   4:5:6
		voicing(kChords[4], 0.f, b);            // MINOR  10:12:15
		float peak[3] = {0.f, 0.f, 0.f};
		for (int k = 0; k < 3; k++) {
			morph(a, b, k * 0.5f, m);
			std::vector<float> y;
			for (int i = 0; i < kN; i++)
				y.push_back(excite(std::cos(2.f * (float)M_PI * kF0 * (float)i / kFs), m));
			for (size_t i = 0; i < y.size(); i++)
				peak[k] = std::fmax(peak[k], std::fabs(y[i]));
		}
		checks++;
		float lo = std::fmin(peak[0], std::fmin(peak[1], peak[2]));
		float hi = std::fmax(peak[0], std::fmax(peak[1], peak[2]));
		if (hi > lo * 2.f) {
			char d[160];
			snprintf(d, sizeof d, "peaks across the morph were %.3f / %.3f / %.3f"
			         " -- the level steps as the partial count changes",
			         (double)peak[0], (double)peak[1], (double)peak[2]);
			fail("morph", d);
		}
		// and the midpoint really does carry both chords' members
		morph(a, b, 0.5f, m);
		checks++;
		if (!(m[4] > 0.f && m[5] > 0.f && m[12] > 0.f && m[15] > 0.f))
			fail("morph", "the midpoint is missing members of one chord or the other");
	}

	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}

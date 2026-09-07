// Amortization's tanks and the collision between them.
//
// The claim under test is the one the hardware makes and this module did not:
// changing mode while the loop is ringing puts the energy from one structure
// into the other and is *loud*, rather than fading through a hole into a cold
// tank. Every check below is a number off that behaviour, not an opinion about
// it, and each was watched failing with the old crossfade in place.
#include "../../src/Amortization/Tanks.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace amortization;

static int checks = 0;
static int failures = 0;

static void fail(const char* what, const char* detail) {
	failures++;
	printf("  FAIL  %s: %s\n", what, detail);
}

/** The module's signal path from the tanks onward, so the test drives what the
    module drives rather than an idealisation of it. */
struct Rig {
	VerbTank verb;
	TronicTank tronic;
	ModeCollider collider;
	float lastVL = 0.f, lastVR = 0.f, lastTL = 0.f, lastTR = 0.f;
	float fade = 0.11f, kCollide = 0.95f, limit = 1.f;

	void setRate(float sr) { verb.setSampleRate(sr); tronic.setSampleRate(sr); }

	/** FEEDBACK as the module maps it: the knob does not reach the two tanks
	    as the same number, and testing them at equal raw parameters says
	    nothing about what the module does. */
	static float verbDecay(float fb) { return 0.97f * fb; }
	static float tronicGain(float fb) { float s = 1.f - fb; return 1.10f * (1.f - s * s); }

	/** One sample at a FEEDBACK setting. Returns the mono wet output. */
	float stepFb(float in, bool tronicMode, float dt, float fb) {
		return step(in, tronicMode, dt, verbDecay(fb), tronicGain(fb));
	}

	/** One sample. Returns the mono wet output. */
	float step(float in, bool tronicMode, float dt, float decay, float gain) {
		collider.step(tronicMode, dt, fade);
		float hot = decay > gain ? decay : gain;
		float k = collider.collide() * kCollide * hot;
		const float noMod[4] = {0.f, 0.f, 0.f, 0.f};
		float vL, vR, tL, tR;
		verb.process(in + k * lastTL, decay, 0.5f, 0.f, 0.f,
		             1.f, 1.f, INTERP_LINEAR, vL, vR);
		tronic.process(in + k * lastVL, in + k * lastVR, gain, 0.5f, noMod,
		               1.f, 1.f, limit, INTERP_LINEAR, tL, tR);
		lastVL = vL; lastVR = vR; lastTL = tL; lastTR = tR;
		float gv = collider.gVerb(), gt = collider.gTronic();
		return 0.5f * ((vL + vR) * gv + (tL + tR) * gt);
	}
};

static float rms(const std::vector<float>& v, size_t a, size_t b) {
	double s = 0.0;
	for (size_t i = a; i < b && i < v.size(); i++) s += (double)v[i] * v[i];
	size_t n = (b > a) ? (b - a) : 1;
	return (float)std::sqrt(s / (double)n);
}

int main() {
	const float fs = 48000.f;
	const float dt = 1.f / fs;

	// --- the fade is equal power ---------------------------------------------
	// The old code scaled the tank inputs by the fade and crossfaded the
	// outputs as well, so the middle of a change was a quarter-gain hole. The
	// gains must sum in power to one everywhere, or the change dips.
	printf("  equal power...\n");
	{
		ModeCollider c;
		float worst = 0.f;
		for (int i = 0; i <= 100; i++) {
			c.x = i / 100.f;
			float p = c.gVerb() * c.gVerb() + c.gTronic() * c.gTronic();
			worst = std::fmax(worst, std::fabs(p - 1.f));
		}
		checks++;
		if (worst > 1e-4f) {
			char d[96];
			snprintf(d, sizeof d, "power deviates by %.5f across the change", (double)worst);
			fail("equal power", d);
		}
	}

	// --- the collision is zero at rest and greatest halfway across ------------
	printf("  collision shape...\n");
	{
		ModeCollider c;
		c.x = 0.f;   checks++; if (c.collide() > 1e-6f) fail("collision", "not zero in Verb");
		c.x = 1.f;   checks++; if (c.collide() > 1e-6f) fail("collision", "not zero in Tronic");
		c.x = 0.5f;  checks++;
		if (std::fabs(c.collide() - 1.f) > 1e-5f)
			fail("collision", "not at maximum halfway across the change");
	}

	// --- a mode change while the loop is hot must not go quiet -----------------
	// Run the loop up on a burst, let it ring, then change mode. The energy in
	// the second after the change is measured against the second before it. The
	// old path dipped to about a quarter through the middle of the change; the
	// hardware gets *louder*, because what was in one structure arrives in the
	// other.
	printf("  a mode change is not a dropout...\n");
	{
		Rig r;
		r.setRate(fs);
		std::vector<float> y;
		// The condition described: FEEDBACK run up until the loop is ringing on
		// its own, then the mode changed under it.
		const float fb = 0.95f;
		// half a second of noise to charge the tanks, then silence
		unsigned seed = 22u;
		for (int i = 0; i < (int)(fs * 0.5f); i++) {
			seed = seed * 1664525u + 1013904223u;
			float n = ((float)(seed >> 9) / 4194304.f - 1.f) * 0.5f;
			r.stepFb(n, false, dt, fb);
		}
		for (int i = 0; i < (int)(fs * 1.0f); i++) y.push_back(r.stepFb(0.f, false, dt, fb));
		size_t mark = y.size();
		for (int i = 0; i < (int)(fs * 1.0f); i++) y.push_back(r.stepFb(0.f, true, dt, fb));

		float before = rms(y, mark - (size_t)(fs * 0.2f), mark);
		float during = rms(y, mark, mark + (size_t)(fs * 0.2f));
		checks++;
		if (!(during > before * 0.9f)) {
			char d[160];
			snprintf(d, sizeof d, "the 200 ms after a mode change measured %.4f"
			         " against %.4f before it -- the change is a dropout",
			         (double)during, (double)before);
			fail("mode change", d);
		}
		// and it has to leave something behind: a tail that is still ringing
		// well after the change, not a click.
		float after = rms(y, mark + (size_t)(fs * 0.5f), mark + (size_t)(fs * 0.7f));
		checks++;
		if (!(after > before * 0.15f)) {
			char d[160];
			snprintf(d, sizeof d, "half a second after the change the tail measured"
			         " %.4f against %.4f before it -- nothing rang on",
			         (double)after, (double)before);
			fail("mode change", d);
		}
		printf("      before %.4f   during %.4f (%.2fx)   +0.5 s %.4f (%.2fx)\n",
		       (double)before, (double)during, (double)(during / before),
		       (double)after, (double)(after / before));
	}

	// --- how loud is the wet, for a unit input? -------------------------------
	// Reported rather than asserted at a value: it is the number the module's
	// output gain has to be chosen against, and printing it is what stops that
	// gain being picked by ear and then drifting.
	printf("  wet level...\n");
	{
		const float decays[] = {0.3f, 0.6f, 0.85f, 0.97f};   // FEEDBACK settings
		float dry = 0.5f / std::sqrt(2.f);                 // rms of the input
		for (size_t c = 0; c < sizeof decays / sizeof decays[0]; c++) {
			Rig r;
			r.setRate(fs);
			std::vector<float> y;
			for (int i = 0; i < (int)(fs * 2.f); i++) {
				float n = std::sin(2.f * 3.14159265f * 220.f * (float)i / fs) * 0.5f;
				y.push_back(r.stepFb(n, false, dt, decays[c]));
			}
			float wet = rms(y, (size_t)(fs * 1.f), y.size());
			printf("      FEEDBACK %.2f   wet/dry %.3f\n", (double)decays[c], (double)(wet / dry));
			checks++;
			if (!(wet > 1e-5f)) fail("wet level", "the tanks produced nothing");
		}
	}

	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}

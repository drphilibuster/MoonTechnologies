// Deduction's six filter models, swept for NaN and self-destruction.
//
// Same reason as the sweeps next door: a filter whose coefficients go bad
// poisons its own state history and stays bad, and nothing in the build
// notices. Deduction is a nastier case than most because every one of these
// models is a *nonlinear* resonant filter -- each has a saturator inside the
// feedback path, which is the whole point of modelling them -- so a bad
// coefficient does not merely ring, it can run away.
//
// The corners that matter here:
//
//   res = 1        every model self-oscillates at full resonance by design.
//                  It must oscillate, not explode.
//   fc -> Nyquist  g = tan(pi fc / fs) goes to infinity as fc approaches fs/2;
//                  the module clamps fc, and this checks the clamp is enough.
//   fc -> DC       g -> 0, so G -> 0, and any 1/G would be a division by zero.
//   drive = 1      the most gain the saturators ever see.
//   model changes  select() crossfades from the old model to the new while both
//                  run, so a bad state in either shows up in the sum.

#include <cmath>
#include <cstdio>

// Filters.hpp uses `clamp` unqualified. In the plugin it comes from <rack.hpp>
// via plugin.hpp; the header itself pulls in nothing but <cmath>, so one
// definition here is the entire Rack surface these models touch.
inline float clamp(float x, float a, float b) {
	return x < a ? a : (x > b ? b : x);
}

#include "../../src/Deduction/Filters.hpp"

static int checks = 0;
static int failures = 0;

static const float SANE = 500.f;   // filter cores work in +-1, not volts

static const char* MODELS[deduction::NUM_MODELS] = {
	"PAiA 2720-3L", "Escobedo Q&D", "Korg35", "MS-20 OTA", "EFM high-pass",
	"Synthrotek DIRT"
};

/** Runs one model at one corner. `fcHz` is asked for directly, including values
    the module itself would clamp -- the point is to prove the core survives
    whatever the clamp lets through, and a little beyond. */
static bool run(int model, float fcHz, float res, float drive, float fs,
                const char*& why, int& badSample, float& badValue) {
	deduction::Voice v;
	v.setRate(fs);
	v.reset();
	v.model = model;
	v.prevModel = model;

	deduction::Coeffs c;
	const float nyq = fs * 0.5f;
	const float fc = fcHz > nyq * 0.98f ? nyq * 0.98f : fcHz;
	c.g = std::tan((float) M_PI * fc / fs);
	c.G = c.g / (1.f + c.g);
	c.res = res;
	c.drive = drive;

	const int n = (int) (fs * 1.5f);
	const float fadeInc = 1.f / (0.005f * fs);

	for (int i = 0; i < n; i++) {
		const float t = (float) i / fs;

		// Silence first: at res = 1 these models ring on their own, and that is
		// where a runaway shows itself with nothing to blame the input for.
		// Then impulses, then a loud low tone that walks the saturators.
		float in;
		if (t < 0.4f)
			in = 0.f;
		else if (t < 0.7f)
			in = (i % 4096 == 0) ? 1.f : 0.f;
		else
			in = std::sin(2.f * (float) M_PI * 55.f * t);

		const float y = v.process(in, in, true, c, fadeInc);

		if (!std::isfinite(y) || std::fabs(y) > SANE) {
			why = std::isnan(y) ? "NaN" : (std::isinf(y) ? "infinity" : "runaway");
			badSample = i;
			badValue = y;
			return false;
		}
	}
	return true;
}

int main() {
	const float rates[] = { 44100.f, 96000.f };
	// Deliberately past the audible band at both ends: 1 Hz drives G toward 0,
	// and 30 kHz is above Nyquist at 44.1k, so the clamp is under test too.
	const float cutoffs[] = { 1.f, 20.f, 200.f, 2000.f, 12000.f, 30000.f };
	const float resos[] = { 0.f, 0.5f, 0.95f, 1.f };
	const float drives[] = { 0.f, 0.5f, 1.f };

	const int NR = (int) (sizeof(rates) / sizeof(rates[0]));
	const int NC = (int) (sizeof(cutoffs) / sizeof(cutoffs[0]));
	const int NQ = (int) (sizeof(resos) / sizeof(resos[0]));
	const int ND = (int) (sizeof(drives) / sizeof(drives[0]));

	printf("Deduction: %d models x %d cutoffs x %d resonances x %d drives x %d rates\n",
	       deduction::NUM_MODELS, NC, NQ, ND, NR);

	// One check for the corner sweep, not one per corner. Every combination
	// still runs; the suite reports the property -- "every model survives its
	// corners" -- rather than eight hundred restatements of it. The first ten
	// failures print in full, which is what a diagnosis needs.
	{
		int bad = 0, total = 0;
		for (int m = 0; m < deduction::NUM_MODELS; m++)
			for (int r = 0; r < NR; r++)
				for (int ci = 0; ci < NC; ci++)
					for (int qi = 0; qi < NQ; qi++)
						for (int di = 0; di < ND; di++) {
							const char* why = "";
							int bs = -1;
							float bv = 0.f;
							total++;
							if (!run(m, cutoffs[ci], resos[qi], drives[di], rates[r],
							         why, bs, bv)) {
								if (bad < 10)
									printf("    %-16s fc=%-7.0f res=%.2f drive=%.1f "
									       "sr=%.0f  %s at sample %d (%g)\n",
									       MODELS[m], cutoffs[ci], resos[qi],
									       drives[di], rates[r], why, bs, bv);
								bad++;
							}
						}
		checks++;
		if (bad) {
			failures++;
			printf("  FAIL  every model survives its corners: %d of %d combinations"
			       " broke\n", bad, total);
		}
	}
	printf("T1  every model survives its corners\n");

	// --- T2: walking the MODEL knob under load -------------------------------
	// select() leaves the outgoing model running under a crossfade, so a switch
	// sums two nonlinear resonant filters. Sweeping the knob while the input is
	// loud and resonance is high is the worst case a patch can ask for, and the
	// one a CV into MODEL produces on purpose.
	for (int r = 0; r < NR; r++) {
		const float fs = rates[r];
		deduction::Voice v;
		v.setRate(fs);
		v.reset();

		deduction::Coeffs c;
		c.g = std::tan((float) M_PI * 800.f / fs);
		c.G = c.g / (1.f + c.g);
		c.res = 0.98f;
		c.drive = 0.9f;

		const int n = (int) (fs * 4.f);
		const float fadeInc = 1.f / (0.005f * fs);
		bool ok = true;
		checks++;

		for (int i = 0; i < n && ok; i++) {
			// A new model every 20 ms: faster than the 5 ms crossfade can be
			// left alone, so switches land on top of unfinished switches.
			if (i % (int) (fs * 0.02f) == 0)
				v.select((i / (int) (fs * 0.02f)) % deduction::NUM_MODELS);

			const float in = std::sin(2.f * (float) M_PI * 110.f * (float) i / fs);
			const float y = v.process(in, in, true, c, fadeInc);
			if (!std::isfinite(y) || std::fabs(y) > SANE) {
				failures++;
				printf("  FAIL  model sweep at sr=%.0f: %g at sample %d\n",
				       fs, y, i);
				ok = false;
			}
		}
	}
	printf("T2  switching models under load stays finite\n");

	printf("\n%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}

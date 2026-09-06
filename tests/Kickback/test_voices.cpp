// Kickback's eight voices, swept for NaN and for stale cached coefficients.
//
// Two things are under test.
//
// 1. No voice, at any knob setting or sample rate, may produce a non-finite or
//    runaway sample. This is the same sweep Diversified gets next door, for the
//    same reason: a coefficient computed from an uninitialised or degenerate
//    value poisons a filter's state history permanently, and nothing in the
//    build notices.
//
// 2. The mt::Cache coefficients hold their value until their *key* changes --
//    but several of them close over `fs`, which the key cannot see. Those
//    caches are cleared by hand in setRate(), and if that is ever missed the
//    voice keeps running yesterday's sample rate's coefficients: it still
//    sounds plausible, so a listening test will not catch it. The rate-change
//    case below pins it down by construction: a voice taken to a new rate must
//    match one that was born at that rate, sample for sample.

#include "../../src/Kickback/Voices.hpp"

#include <cmath>
#include <cstdio>

static int checks = 0;
static int failures = 0;

static const float SANE = 200.f;   // volts; a drum voice never approaches this

static void fail(const char* what, const char* detail) {
	failures++;
	printf("  FAIL  %s: %s\n", what, detail);
}

/** Renders one voice into `out`, striking on the first sample. `v` selects the
    voice, `a`/`b`/`c` are its three knobs. Every voice is driven through the
    same shape so the sweep below can treat them uniformly. */
static void render(int v, float a, float b, float c, float fs, float* out, int n) {
	kickback::Kick kick;
	kickback::Snare snare;
	kickback::Hat hat;
	kickback::Smurf smurf;
	kickback::Tom tom;
	kickback::Bell bell;
	kickback::NoiseVoice nv;
	kickback::Dazzler daz;

	kick.setRate(fs); snare.setRate(fs); hat.setRate(fs); smurf.setRate(fs);
	tom.setRate(fs); bell.setRate(fs); nv.setRate(fs); daz.setRate(fs);

	for (int i = 0; i < n; i++) {
		const bool s = (i == 0);
		switch (v) {
			case 0: out[i] = kick.process(s, 1.f, a, b, c, fs); break;
			case 1: out[i] = snare.process(s, 1.f, a, b, c, fs); break;
			case 2: out[i] = hat.process(s, 1.f, a, b); break;
			case 3: out[i] = smurf.process(s, 1.f, a, b, c); break;
			case 4: out[i] = tom.process(s, 1.f, a, b, (int) (c * 2.f), fs); break;
			case 5: out[i] = bell.process(s, 1.f, a, b, c); break;
			case 6: out[i] = nv.process(s, 1.f, a, b, 0.f); break;
			default: out[i] = daz.process(s, 1.f, (int) c, b, a); break;
		}
	}
}

/** The same, but the voice is built at `fsFrom`, run until its caches are warm,
    then moved to `fsTo` with setRate() **alone** -- no reset().

    That is deliberate. Kickback itself always calls reset() before setRate()
    (Kickback.cpp:120-129), and reset() clears the caches too, so going through
    the module would prove nothing: the bug would be masked and this test could
    never fail. setRate() is a public entry point that has to leave the voice
    correct on its own, and the mt::Cache members that close over `fs` are
    exactly what makes that non-trivial -- their key is a knob, which cannot see
    the rate move. Verified to fail by deleting the clear() calls from
    Smurf::setRate(). */
static void renderAfterRateChange(int v, float a, float b, float c,
                                  float fsFrom, float fsTo, float* out, int n) {
	kickback::Kick kick;
	kickback::Snare snare;
	kickback::Hat hat;
	kickback::Smurf smurf;
	kickback::Tom tom;
	kickback::Bell bell;
	kickback::NoiseVoice nv;
	kickback::Dazzler daz;

	// Born at the old rate, and actually run there, so every cache is warm with
	// the old rate's coefficients.
	kick.setRate(fsFrom); snare.setRate(fsFrom); hat.setRate(fsFrom);
	smurf.setRate(fsFrom); tom.setRate(fsFrom); bell.setRate(fsFrom);
	nv.setRate(fsFrom); daz.setRate(fsFrom);
	for (int i = 0; i < 64; i++) {
		kick.process(i == 0, 1.f, a, b, c, fsFrom);
		snare.process(i == 0, 1.f, a, b, c, fsFrom);
		hat.process(i == 0, 1.f, a, b);
		smurf.process(i == 0, 1.f, a, b, c);
		tom.process(i == 0, 1.f, a, b, (int) (c * 2.f), fsFrom);
		bell.process(i == 0, 1.f, a, b, c);
		nv.process(i == 0, 1.f, a, b, 0.f);
		daz.process(i == 0, 1.f, (int) c, b, a);
	}

	// Moved to the new rate by setRate() alone. reset() would clear the caches
	// as a side effect and hide the very thing under test, so it is not called;
	// the ringing state left over from the warm-up is cleared by hand instead,
	// leaving the cached coefficients as the only difference from a fresh voice.
	kick.setRate(fsTo); snare.setRate(fsTo); hat.setRate(fsTo);
	smurf.setRate(fsTo); tom.setRate(fsTo); bell.setRate(fsTo);
	nv.setRate(fsTo); daz.setRate(fsTo);
	// (DcBlock::reset() and the raw field pokes below touch no mt::Cache.)
	kick.body.y1 = kick.body.y2 = 0.f;                          kick.dc.reset();
	smurf.osc.phase = 0.f; smurf.lp.s = 0.f; smurf.env = 0.f;   smurf.dc.reset();
	tom.body.y1 = tom.body.y2 = 0.f;                            tom.dc.reset();
	bell.osc1.phase = bell.osc2.phase = bell.osc3.phase = 0.f;
	bell.env = 0.f;                                             bell.dc.reset();

	for (int i = 0; i < n; i++) {
		const bool s = (i == 0);
		switch (v) {
			case 0: out[i] = kick.process(s, 1.f, a, b, c, fsTo); break;
			case 1: out[i] = snare.process(s, 1.f, a, b, c, fsTo); break;
			case 2: out[i] = hat.process(s, 1.f, a, b); break;
			case 3: out[i] = smurf.process(s, 1.f, a, b, c); break;
			case 4: out[i] = tom.process(s, 1.f, a, b, (int) (c * 2.f), fsTo); break;
			case 5: out[i] = bell.process(s, 1.f, a, b, c); break;
			case 6: out[i] = nv.process(s, 1.f, a, b, 0.f); break;
			default: out[i] = daz.process(s, 1.f, (int) c, b, a); break;
		}
	}
}

static const char* NAMES[8] = { "KICK", "SNARE", "HAT", "SMURF",
                                "TOM", "BELL", "NOISE", "DAZZLER" };

int main() {
	static float buf[96000];
	static float ref[96000];
	const int n = 4096;

	const float rates[] = { 44100.f, 48000.f, 96000.f };
	const float knobs[] = { 0.f, 0.25f, 0.5f, 0.75f, 1.f };
	const int NK = (int) (sizeof(knobs) / sizeof(knobs[0]));
	const int NR = (int) (sizeof(rates) / sizeof(rates[0]));

	printf("Kickback: 8 voices x %d^3 knob settings x %d sample rates\n",
	       NK, NR);

	// --- T1: nothing goes non-finite, anywhere in the knob space -------------
	for (int v = 0; v < 8; v++) {
		for (int r = 0; r < NR; r++) {
			for (int i = 0; i < NK; i++) {
				for (int j = 0; j < NK; j++) {
					for (int k = 0; k < NK; k++) {
						checks++;
						render(v, knobs[i], knobs[j], knobs[k], rates[r], buf, n);
						for (int s = 0; s < n; s++) {
							if (!std::isfinite(buf[s]) || std::fabs(buf[s]) > SANE) {
								char d[192];
								snprintf(d, sizeof d,
								         "%s at %.0f Hz knobs %.2f/%.2f/%.2f -> %g at sample %d",
								         NAMES[v], rates[r], knobs[i], knobs[j],
								         knobs[k], buf[s], s);
								fail("non-finite or runaway", d);
								s = n;   // one report per configuration
							}
						}
					}
				}
			}
		}
	}
	printf("T1  every voice stays finite across the knob space\n");

	// --- T2: a rate change must not leave a cached coefficient behind --------
	// mt::Cache keys on a knob; several of Kickback's close over fs as well, so
	// setRate() has to clear them by hand. If it stops doing that, a voice
	// carried across a rate change diverges from one created at the new rate --
	// silently, and only by a few percent, which is exactly why it needs a test
	// rather than an ear.
	for (int v = 0; v < 8; v++) {
		for (int i = 0; i < NK; i++) {
			for (int j = 0; j < NK; j++) {
				checks++;
				const float a = knobs[i], b = knobs[j], c = 0.5f;
				render(v, a, b, c, 96000.f, ref, n);
				renderAfterRateChange(v, a, b, c, 44100.f, 96000.f, buf, n);

				// The noise-driven voices reseed per instance, so compare only
				// the voices whose output is deterministic from the knobs.
				const bool deterministic = (v == 0 || v == 3 || v == 4 || v == 5);
				if (!deterministic)
					continue;

				for (int s = 0; s < n; s++) {
					if (std::fabs(buf[s] - ref[s]) > 1e-4f) {
						char d[192];
						snprintf(d, sizeof d,
						         "%s knobs %.2f/%.2f: 44.1k->96k gives %g, "
						         "born at 96k gives %g, at sample %d",
						         NAMES[v], a, b, buf[s], ref[s], s);
						fail("stale coefficient after rate change", d);
						s = n;
					}
				}
			}
		}
	}
	printf("T2  a voice carried across a sample-rate change matches one born there\n");

	printf("\n%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}

// Kickback's six voices and its pattern engine, tested without Rack.
//
// Five things are under test.
//
// 1. No voice, at any knob setting, velocity or sample rate, may produce a
//    non-finite or runaway sample. A coefficient computed from a degenerate
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
//
// 3. Every voice must actually have a transient. This is the fault the rebuild
//    was for -- a mode bank is a set of narrow resonators well under a
//    kilohertz, so a strike that only reaches the output through it arrives as
//    a bass note with a fast envelope. Measured through a 24 dB/oct band at
//    2 kHz, in two parts: every voice at every setting has to put real energy
//    up there at the strike and have it scale with how loud the drum is; and
//    the three voices the complaint was actually about -- both KICK models and
//    the TOMs -- have to show an order of magnitude more of it at the strike
//    than a fifth of a second later, at their shipped defaults.
//
// 4. Velocity has to reach the output. A voice whose saturation stage is driven
//    past its knee turns every strike into the same clipped peak, which is a
//    compressor rather than a drum; the pattern engine's per-step velocities
//    then do nothing at all.
//
// 5. A voice has to sound the same at every sample rate. This one bit three
//    times while the voices were being written, and never audibly at the rate
//    the author happened to be running: a pole radius written as a constant is
//    a decay per *sample*, so the snare's rattle ran four times shorter at
//    192 kHz; a resonator struck by an impulse rings at 1/sin(w), so the same
//    snare came out seventeen decibels louder there; and a PRNG's spectral
//    density halves each time the rate doubles, so the noise voice lost five
//    decibels. None of that is visible in a build or in a listening test at one
//    rate. The check below renders every voice at five rates and bounds the
//    spread.
//
// 6. Every strike has to end. A resonator whose frequency is modulated by its
//    own amplitude is a parametric amplifier, and with BEND wide open the toms
//    pumped themselves faster than their decay could empty them: a single
//    strike was still ringing at three and a half volts six seconds later, at
//    any DECAY past about 0.9. Nothing in a build or a short listening test
//    catches that -- it only shows up if you hold a note and wait.
//
// 7. euclid() has to produce the Euclidean rhythms, not merely k onsets in n
//    steps: E(3,8) is the tresillo and E(5,8) the cinquillo, and if those two
//    are right the recursion is right.

#include "../../src/Kickback/Payroll.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

using namespace kickback;

static int checks = 0;
static int failures = 0;

static const float SANE = 60.f;    // volts; a drum voice never approaches this

static void fail(const char* what, const char* detail) {
	failures++;
	printf("  FAIL  %s: %s\n", what, detail);
}

/** Every voice behind one signature, so the sweeps below can treat them
    uniformly. `v` selects the voice; a/b/c/d are TUNE/DECAY/BEND/COLOUR. */
struct Bank {
	Kick kick;
	Snare snare;
	Hat hat;
	Tom tomLo, tomMid, tomHi;

	Bank() : tomLo(0x70Au, 0), tomMid(0x70Bu, 1), tomHi(0x70Cu, 2) {}

	void setRate(float fs) {
		kick.setRate(fs); snare.setRate(fs); hat.setRate(fs);
		tomLo.setRate(fs); tomMid.setRate(fs); tomHi.setRate(fs);
	}
	void reset() {
		kick.reset(); snare.reset(); hat.reset();
		tomLo.reset(); tomMid.reset(); tomHi.reset();
	}

	/** Every distinct sound the module can make, behind one signature -- each
	    model of the two voices that switch between models counts separately,
	    because a mode that is never rendered is a mode that is never tested. */
	float process(int v, bool hit, float vel, float a, float b, float c, float d,
	              float volts) {
		switch (v) {
			case 0: return kick.process(hit, vel, 0, a, volts, b, c, d);
			case 1: return kick.process(hit, vel, 1, a, volts, b, c, d);
			case 2: return snare.process(hit, vel, 0, a, volts, b, c);
			case 3: return snare.process(hit, vel, 1, a, volts, b, c);
			case 4: return snare.process(hit, vel, 2, a, volts, b, c);
			case 5: return hat.process(hit, vel, a, volts, b, c, d);
			case 6: return tomLo.process(hit, vel, a, volts, b, c, d);
			case 7: return tomMid.process(hit, vel, a, volts, b, c, d);
			default: return tomHi.process(hit, vel, a, volts, b, c, d);
		}
	}
};

static const int kVoices = 9;
static const char* kName[kVoices] = {
	"KICK bridge", "KICK smurf", "SNARE xor", "SNARE vactrol", "SNARE dazzle",
	"HAT", "TOM I", "TOM II", "TOM III"
};

/** One strike, rendered. Strikes on sample 0 and runs `n` samples. */
static void render(Bank& bank, int v, float vel, float a, float b, float c, float d,
                   float volts, float* out, int n) {
	bank.reset();
	for (int i = 0; i < n; i++) out[i] = bank.process(v, i == 0, vel, a, b, c, d, volts);
}


// ---------------------------------------------------------------------------
// 1 + 4: the sweep
// ---------------------------------------------------------------------------

static void sweep(float fs) {
	static float buf[192000];
	int n = (int)(fs * 0.5f);
	Bank bank;
	bank.setRate(fs);

	for (int v = 0; v < kVoices; v++) {
		float worst = 0.f;
		for (int ia = 0; ia <= 4; ia++)
		for (int ib = 0; ib <= 2; ib++)
		for (int ic = 0; ic <= 2; ic++)
		for (int id = 0; id <= 4; id++)
		for (int iv = 0; iv < 3; iv++) {
			// V/OCT is swept too: it multiplies a frequency, so it is the one
			// input that can push a resonator at Nyquist or a delay line to
			// zero length from outside the module.
			static const float volts[3] = { -5.f, 0.f, 5.f };
			render(bank, v, 1.f, ia / 4.f, ib / 2.f, ic / 2.f, id / 4.f,
			       volts[iv], buf, n);
			for (int i = 0; i < n; i++) {
				if (!std::isfinite(buf[i])) {
					char d[128];
					snprintf(d, sizeof d, "%s non-finite at %.0f Hz, knobs %d%d%d%d, %+.0f V",
					         kName[v], fs, ia, ib, ic, id, volts[iv]);
					fail("finite", d);
					return;
				}
				float m = std::fabs(buf[i]);
				if (m > worst) worst = m;
			}
		}
		checks++;
		if (worst > SANE) {
			char d[128];
			snprintf(d, sizeof d, "%s peaked at %.1f V at %.0f Hz", kName[v], worst, fs);
			fail("sane level", d);
		}
	}
}

/** Velocity has to move the output monotonically, and by a real margin. */
static void velocity(float fs) {
	static float buf[192000];
	int n = (int)(fs * 0.5f);
	Bank bank;
	bank.setRate(fs);

	for (int v = 0; v < kVoices; v++) {
		double prev = -1.0;
		bool rising = true;
		double lo = 0.0, hi = 0.0;
		static const float vels[4] = { 0.2f, 0.45f, 0.7f, 1.0f };
		for (int k = 0; k < 4; k++) {
			render(bank, v, vels[k], 0.4f, 0.5f, 0.45f, 0.45f, 0.f, buf, n);
			double e = 0.0;
			for (int i = 0; i < n; i++) e += (double)buf[i] * buf[i];
			double r = std::sqrt(e / n);
			if (k == 0) lo = r;
			if (k == 3) hi = r;
			if (r <= prev) rising = false;
			prev = r;
		}
		checks++;
		if (!rising) fail("velocity monotone", kName[v]);
		checks++;
		// A drum voice driven into a limiter shows a ratio near 1; anything
		// with real dynamics is several times louder at full velocity.
		if (!(hi > lo * 2.5)) {
			char d[128];
			snprintf(d, sizeof d, "%s only %.2fx louder from v0.2 to v1.0", kName[v], lo > 0 ? hi / lo : 0.0);
			fail("velocity range", d);
		}
	}
}


// ---------------------------------------------------------------------------
// 2: rate changes cannot leave a coefficient stale
// ---------------------------------------------------------------------------

static void rateChange() {
	static float a[96000], b[96000];
	const float fs1 = 44100.f, fs2 = 96000.f;
	int n = (int)(fs2 * 0.25f);

	for (int v = 0; v < kVoices; v++) {
		// One bank warmed up at the old rate and moved, one born at the new.
		Bank moved;
		moved.setRate(fs1);
		render(moved, v, 1.f, 0.7f, 0.3f, 0.6f, 0.6f, 0.f, a, (int)(fs1 * 0.1f));
		moved.reset();
		moved.setRate(fs2);
		render(moved, v, 1.f, 0.35f, 0.55f, 0.4f, 0.5f, 0.f, a, n);

		Bank fresh;
		fresh.setRate(fs2);
		render(fresh, v, 1.f, 0.35f, 0.55f, 0.4f, 0.5f, 0.f, b, n);

		checks++;
		for (int i = 0; i < n; i++) {
			if (std::fabs(a[i] - b[i]) > 1e-4f) {
				char d[160];
				snprintf(d, sizeof d,
				         "%s differs at sample %d after a rate change (%.6f vs %.6f)"
				         " -- a cache that closes over fs was not cleared in setRate()",
				         kName[v], i, a[i], b[i]);
				fail("rate change", d);
				break;
			}
		}
	}
}


// ---------------------------------------------------------------------------
// 3: every voice has a transient
// ---------------------------------------------------------------------------

/** Peak of a 24 dB/oct highpass at 2 kHz, early and late.

    A one-pole is nowhere near steep enough here: a kick's fundamental is
    thirty decibels louder two octaves down, and what leaks through a single
    pole swamps the thing being measured. Four poles put the leakage six orders
    of magnitude below the band. */
static void hfPeaks(const float* y, int n, float fs, float* early, float* late) {
	float s[4] = { 0.f, 0.f, 0.f, 0.f };
	float g = std::tan(kPi * 2000.f / fs); g = g / (1.f + g);
	*early = 0.f; *late = 0.f;
	int cut = (int)(fs * 0.015f);
	for (int i = 0; i < n; i++) {
		float v = y[i];
		for (int k = 0; k < 4; k++) {
			float lo = s[k] + (v - s[k]) * g;
			s[k] = lo + (lo - s[k]);
			v = v - lo;
		}
		float m = std::fabs(v);
		if (i < cut) { if (m > *early) *early = m; }
		else         { if (m > *late)  *late  = m; }
	}
}

static void transient(float fs) {
	static float buf[192000];
	int n = (int)(fs * 0.5f);
	Bank bank;
	bank.setRate(fs);

	// Part one, over the whole knob grid: there has to be real energy above
	// 2 kHz at the strike, and it has to be a real fraction of how loud the
	// voice is -- a fixed tick bolted onto a quiet drum is not a transient.
	//
	// Note what is deliberately *not* asserted here: that the high band is
	// louder early than late. It is not, at every setting, and it should not
	// be. A hat with DECAY wide open, a kick driven hard enough to clip, and
	// the noise voice's vactrol -- which by design takes tens of milliseconds
	// to open its filter -- all legitimately carry more high end after the
	// first fifteen milliseconds than during them. Asserting otherwise would
	// be a test demanding the circuits misbehave.
	for (int v = 0; v < kVoices; v++) {
		float worstEarly = 1e9f;
		float worstCrest = 1e9f;
		for (int ia = 0; ia <= 2; ia++)
		for (int ib = 0; ib <= 2; ib++)
		for (int ic = 0; ic <= 2; ic++)
		for (int id = 0; id <= 2; id++) {
			// A tom with STRIKE at zero is the one setting on the panel whose
			// whole purpose is to have no strike: dead centre, softest mallet,
			// no beater at all. Asserting a transient there would be asserting
			// that a control does not do its job. Every other setting of every
			// other voice still has to have one, and the toms are checked from
			// the first notch of the knob upward.
			bool tomAtZero = (v >= 6 && v <= 8) && id == 0;
			if (tomAtZero) continue;
			render(bank, v, 1.f, ia / 2.f, ib / 2.f, ic / 2.f, id / 2.f, 0.f, buf, n);
			float e, l;
			hfPeaks(buf, n, fs, &e, &l);
			float pk = 0.f;
			for (int i = 0; i < n; i++) { float m = std::fabs(buf[i]); if (m > pk) pk = m; }
			if (e < worstEarly) worstEarly = e;
			float crest = pk > 1e-6f ? e / pk : 0.f;
			if (crest < worstCrest) worstCrest = crest;
		}

		// And the other half of that bargain: STRIKE at zero must actually be
		// silent up there, not merely quieter. This is the check that would
		// have caught the beater still being audible at the bottom of the knob.
		if (v >= 6 && v <= 8) {
			render(bank, v, 1.f, 0.4f, 0.5f, 0.5f, 0.f, 0.f, buf, n);
			float e0, l0;
			hfPeaks(buf, n, fs, &e0, &l0);
			checks++;
			if (!(e0 < 0.05f)) {
				char d[192];
				snprintf(d, sizeof d,
				         "%s at STRIKE 0 still puts %.3f V above 2 kHz into the"
				         " first 15 ms -- the knob's bottom is supposed to have"
				         " no beater at all", kName[v], e0);
				fail("STRIKE 0 is silent", d);
			}
		}
		checks++;
		if (!(worstEarly > 0.15f)) {
			char d[192];
			snprintf(d, sizeof d,
			         "%s: only %.4f V above 2 kHz in the first 15 ms at its worst setting"
			         " -- the strike is not reaching the output", kName[v], worstEarly);
			fail("has a transient", d);
		}
		checks++;
		if (!(worstCrest > 0.04f)) {
			char d[192];
			snprintf(d, sizeof d,
			         "%s: the strike's high band is only %.3f of the voice's own peak"
			         " -- an attack that does not scale with the drum", kName[v], worstCrest);
			fail("transient scales", d);
		}
	}

	// Part two, and the one this rebuild exists for. KICK and the TOMs were
	// the voices that "sounded like bass oscillators": a mode bank is a set of
	// narrow resonators well under a kilohertz, so a strike that reaches the
	// output only through it arrives as a tone with a fast envelope and no
	// beater on the front of it. At their shipped defaults these three have to
	// show an unmistakable onset -- an order of magnitude more high band at the
	// strike than a fifth of a second later.
	{
		struct Def { int v; float tune, decay, bend, colour; const char* what; };
		static const Def defs[] = {
			{ 0, 0.40f, 0.50f, 0.60f, 0.55f, "KICK bridge at defaults" },
			{ 1, 0.40f, 0.50f, 0.60f, 0.55f, "KICK smurf at defaults" },
			{ 6, 0.42f, 0.55f, 0.55f, 0.30f, "TOM I at defaults" },
			{ 7, 0.40f, 0.50f, 0.55f, 0.42f, "TOM II at defaults" },
			{ 8, 0.38f, 0.45f, 0.55f, 0.55f, "TOM III at defaults" },
		};
		for (size_t k = 0; k < sizeof defs / sizeof defs[0]; k++) {
			const Def& d = defs[k];
			render(bank, d.v, 1.f, d.tune, d.decay, d.bend, d.colour, 0.f, buf, n);
			float e, l;
			hfPeaks(buf, n, fs, &e, &l);
			checks++;
			float ratio = l > 1e-6f ? e / l : 1e9f;
			if (!(ratio > 10.f)) {
				char msg[192];
				snprintf(msg, sizeof msg,
				         "%s: high band only %.1fx louder at the strike than after it"
				         " -- that is a bass note with an envelope, not a struck drum",
				         d.what, ratio);
				fail("beater on the front", msg);
			}
		}
	}
}


// ---------------------------------------------------------------------------
// 6: every strike ends
// ---------------------------------------------------------------------------

static void decays() {
	const float fs = 48000.f;
	static float buf[480000];
	int n = (int)(fs * 8.f);
	int late = (int)(fs * 6.f);
	Bank bank;
	bank.setRate(fs);

	for (int v = 0; v < kVoices; v++) {
		float worst = 0.f;
		int wa = 0, wb = 0, wc = 0, wd = 0;
		for (int ia = 0; ia <= 2; ia++)
		for (int ib = 0; ib <= 2; ib++)
		for (int ic = 0; ic <= 2; ic++)
		for (int id = 0; id <= 2; id++) {
			render(bank, v, 1.f, ia / 2.f, ib / 2.f, ic / 2.f, id / 2.f, 0.f, buf, n);
			float pk = 0.f, tail = 0.f;
			for (int i = 0; i < n; i++) {
				float m = std::fabs(buf[i]);
				if (m > pk) pk = m;
				if (i >= late && m > tail) tail = m;
			}
			// The longest ring on the panel is three seconds, so six seconds
			// after the strike every voice is at least two t60s down -- a
			// thousandth of its peak, with room to spare. Anything still
			// audible there is being driven, not ringing.
			float rel = pk > 1e-6f ? tail / pk : 0.f;
			if (rel > worst) { worst = rel; wa = ia; wb = ib; wc = ic; wd = id; }
		}
		checks++;
		if (!(worst < 0.01f)) {
			char d[192];
			snprintf(d, sizeof d,
			         "%s is still at %.1f%% of its peak six seconds after one"
			         " strike, at knobs %d%d%d%d -- it is self-oscillating, not"
			         " decaying", kName[v], worst * 100.f, wa, wb, wc, wd);
			fail("strikes end", d);
		}
	}
}


// ---------------------------------------------------------------------------
// 5: the same drum at every sample rate
// ---------------------------------------------------------------------------

/** RMS of `y`, optionally only above 1.5 kHz and only after the first 20 ms. */
static double bandRms(const float* y, int n, float fs, bool tailOnly) {
	double e = 0.0;
	int used = 0;
	int skip = tailOnly ? (int)(fs * 0.02f) : 0;
	float st[4] = { 0.f, 0.f, 0.f, 0.f };
	float g = std::tan(kPi * 1500.f / fs); g = g / (1.f + g);
	for (int i = 0; i < n; i++) {
		float u = y[i];
		if (tailOnly) {
			for (int k = 0; k < 4; k++) {
				float lo = st[k] + (u - st[k]) * g;
				st[k] = lo + (lo - st[k]);
				u = u - lo;
			}
		}
		if (i >= skip) { e += (double)u * u; used++; }
	}
	return used ? std::sqrt(e / used) : 0.0;
}

static void sampleRates() {
	static const float rates[5] = { 44100.f, 48000.f, 88200.f, 96000.f, 192000.f };
	static float buf[192000];

	// Two measures, because one is not enough. Full-band RMS is what a mixer
	// sees, but it is dominated by whichever component is loudest -- the
	// snare's wires ran seven times hot and four times short at 192 kHz and
	// moved the snare's total by less than a decibel, because the shell buries
	// them. The second measure looks where the quiet, distinctive components
	// actually live: above 1.5 kHz, after the strike has gone.
	for (int pass = 0; pass < 2; pass++) {
		bool tail = (pass == 1);
		for (int v = 0; v < kVoices; v++) {
			// SNARE's dazzle mode is Karplus-Strong, whose delay line is an
			// integer number of samples: which realisation of an aperiodic
			// sequence you get depends on the rate even though its character
			// does not, and the paper is explicit that at b = 1/2 the table
			// length sets decay rather than pitch. Its tail band wanders a
			// couple of decibels between rates with no coefficient wrong. The
			// full-band pass still covers it.
			if (tail && v == 4) continue;

			double loudest = 0.0, quietest = 1e30;
			for (int r = 0; r < 5; r++) {
				float fs = rates[r];
				Bank bank;
				bank.setRate(fs);
				int n = (int)(fs * 0.6f);
				// One strike is enough: reset() reseeds every noise source, so
				// a second strike is the first one again.
				render(bank, v, 1.f, 0.4f, 0.5f, 0.45f, 0.7f, 0.f, buf, n);
				double rms = bandRms(buf, n, fs, tail);
				if (rms > loudest) loudest = rms;
				if (rms < quietest) quietest = rms;
			}
			// A ratio between two near-silent readings is not a measurement.
			// BELL's modes all sit under a kilohertz at some tunings, so its
			// high band after the strike can be numerical residue, and
			// comparing residue to residue reports whatever the noise floor
			// happened to be.
			if (tail && loudest < 0.02) continue;

			checks++;
			double ratio = quietest > 1e-9 ? loudest / quietest : 1.0;
			// What these bounds are for: a coefficient written per sample
			// rather than per second, which produces a large, monotone drift.
			// The negative controls measure 2.3x full-band and 5.6x on the
			// tail, so both catch them with room.
			//
			// They are not tight, and the tail one especially is not, because
			// BELL's wire bed is a bank of resonators driven by a train of
			// contact bursts. That is neither an impulse nor a steady tone, so
			// no closed-form drive normalisation flattens it: the exponent it
			// uses was swept and measured, holds the bed's full-band level to
			// 1.15x, and still leaves about 1.4x of non-monotonic spread in the
			// high band. A bound tighter than that would fail on a machine's
			// sample rate rather than on a defect.
			double bound = tail ? 2.0 : 1.5;
			if (!(ratio < bound)) {
				char d[192];
				snprintf(d, sizeof d,
				         "%s: %s is %.2fx louder at one sample rate than another"
				         " -- a coefficient somewhere is written per-sample"
				         " rather than per-second",
				         kName[v], tail ? "the high band after the strike" : "the output",
				         ratio);
				fail("rate independent", d);
			}
		}
	}
}


// ---------------------------------------------------------------------------
// 6: the Euclidean rhythms
// ---------------------------------------------------------------------------

static void patternTests() {
	struct Case { int k, n; const char* want; };
	// The named ones. If the tresillo and the cinquillo come out, Bjorklund's
	// recursion is right; the rest follow.
	static const Case cases[] = {
		{ 3,  8, "x..x..x." },                  // tresillo
		{ 5,  8, "x.xx.xx." },                  // cinquillo
		{ 2,  5, "x.x.." },                     // khafif-e-ramal
		{ 4, 16, "x...x...x...x..." },          // four on the floor
		{ 5, 16, "x..x..x..x..x..." },
		{ 0, 16, "................" },
		{ 16, 16, "xxxxxxxxxxxxxxxx" },
	};
	for (size_t c = 0; c < sizeof cases / sizeof cases[0]; c++) {
		bool out[32];
		euclid(cases[c].k, cases[c].n, out);
		char got[33];
		for (int i = 0; i < cases[c].n; i++) got[i] = out[i] ? 'x' : '.';
		got[cases[c].n] = 0;
		checks++;
		if (std::strcmp(got, cases[c].want) != 0) {
			char d[160];
			snprintf(d, sizeof d, "E(%d,%d) = %s, wanted %s",
			         cases[c].k, cases[c].n, got, cases[c].want);
			fail("euclid", d);
		}
	}

	// FILL has to be monotone: turning it up may only ever add onsets, never
	// take one away, or the knob is a randomiser with a density side-effect.
	Payroll p;
	p.reset();
	int prev[V_COUNT];
	for (int v = 0; v < V_COUNT; v++) prev[v] = -1;
	// One check for the property, not one per (fill, voice) pair: "FILL is
	// monotone" is a single claim about the knob, and stating it six hundred
	// times said nothing the first did not. Every step is still walked, and the
	// first ten regressions print with their voice and fill.
	{
		int bad = 0;
		for (int i = 0; i <= 100; i++) {
			p.build(i / 100.f, 0, 0.f);
			for (int v = 0; v < V_COUNT; v++) {
				if (p.onsets[v] < prev[v]) {
					if (bad < 10)
						printf("    voice %d lost an onset (%d -> %d) at fill %.2f\n",
						       v, prev[v], p.onsets[v], i / 100.f);
					bad++;
				}
				prev[v] = p.onsets[v];
			}
		}
		checks++;
		if (bad) {
			char d[128];
			snprintf(d, sizeof d, "%d places where turning FILL up removed an onset", bad);
			fail("fill monotone", d);
		}
	}

	// Grid mode: every voice fires at its own ratio against the step grid, and
	// the table is symmetric about x1 so every division has a multiplication to
	// mirror it. This is the mode where a runaway is easiest to write -- x256
	// on a sixteenth grid is two kilohertz -- so the top of the range is
	// checked for a sane count, not merely for not hanging.
	{
		const float fs = 48000.f;
		checks++;
		if (kClockRatio[kUnity] != 1.f) fail("ratio table", "index kUnity is not x1");
		checks++;
		bool mirrored = true;
		for (int i = 0; i < kUnity; i++)
			if (std::fabs(kClockRatio[i] * kClockRatio[kRatioCount - 1 - i] - 1.f) > 1e-4f)
				mirrored = false;
		if (!mirrored) fail("ratio table", "the divisions do not mirror the multiplications");

		// 120 BPM at 1/16 is eight steps a second, so four seconds is 32 steps
		// and each voice should land its ratio's share of them.
		// The expectation comes from the table itself rather than being typed
		// beside it -- a hand-written count is one more thing that can
		// disagree with the code it is checking, and did.
		static const int idxs[] = {
			kUnity, kUnity - 1, kUnity - 2, kUnity - 4, kUnity + 1, kUnity + 3
		};
		for (size_t c = 0; c < sizeof idxs / sizeof idxs[0]; c++) {
			float want = 32.f * kClockRatio[idxs[c]];
			Payroll g;
			g.reset(); g.running = true; g.gridMode = true; g.build(0.f, 0, 0.f);
			bool none[V_COUNT] = {};
			for (int v = 0; v < V_COUNT; v++) g.ratioIndex[v] = idxs[c];
			int fires = 0;
			for (int i = 0; i < (int)(fs * 4.f); i++) {
				g.process(1.f / fs, false, false, false, 120.f, 3, 0.f, none);
				if (g.fired[0]) fires++;
			}
			checks++;
			if (std::fabs((float)fires - want) > 1.5f) {
				char d[160];
				snprintf(d, sizeof d, "ratio %g gave %d hits in four seconds,"
				         " wanted about %.1f", (double)kClockRatio[idxs[c]], fires, want);
				fail("grid ratios", d);
			}
		}

		// And the far end. Eight steps a second times 256 is 2048; anything
		// wildly under means the phase loop is dropping hits, anything over
		// means the clamp that keeps it out of audio rate is not holding.
		Payroll g;
		g.reset(); g.running = true; g.gridMode = true; g.build(0.f, 0, 0.f);
		bool none[V_COUNT] = {};
		for (int v = 0; v < V_COUNT; v++) g.ratioIndex[v] = kRatioCount - 1;
		int fires = 0;
		for (int i = 0; i < (int)fs; i++) {
			g.process(1.f / fs, false, false, false, 120.f, 3, 0.f, none);
			if (g.fired[0]) fires++;
		}
		checks++;
		if (fires < 1900 || fires > 2100) {
			char d[128];
			snprintf(d, sizeof d, "x256 gave %d hits in a second, wanted about 2048", fires);
			fail("grid ratios", d);
		}
	}

	// --- BURST: the ratios drive the pattern's own steps ---------------------
	// The contract has three parts, and each is a separate way to get it wrong:
	// at unity a burst is one hit per lit step (so BURST changes nothing you did
	// not ask it to); multiplying puts that many hits inside each lit step; and
	// nothing at all comes out of a step the pattern left dark.
	{
		const float fs = 48000.f;
		bool none[V_COUNT] = {};

		// how many steps of sixteen this voice's pattern actually lights
		Payroll ref;
		ref.reset(); ref.running = true; ref.build(0.6f, 0, 0.f);
		int lit = 0;
		for (int i = 0; i < kSteps; i++) if (ref.on[0][i]) lit++;

		// The multipliers are read out of kClockRatio, not assumed from the
		// index: the table interleaves twos, threes, fives and sevens, so
		// kUnity+2 is three and not two. Writing the expectation by hand got
		// that wrong and reported a fault in code that was behaving correctly.
		int mul[8], nmul = 0;
		for (int i = kUnity; i < kRatioCount && nmul < 4; i++) {
			float r = kClockRatio[i];
			if (r >= 1.f && r <= 8.f && std::fabs(r - std::floor(r + 0.5f)) < 1e-4f)
				mul[nmul++] = i;
		}
		for (int c = 0; c < nmul; c++) {
			Payroll b;
			b.reset(); b.running = true; b.burstMode = true;
			b.build(0.6f, 0, 0.f);
			for (int v = 0; v < V_COUNT; v++) b.ratioIndex[v] = mul[c];
			// 120 BPM, 1/16: eight steps a second, so two seconds is one pass
			// of the sixteen-step pattern.
			int fires = 0;
			for (int i = 0; i < (int)(fs * 2.f); i++) {
				b.process(1.f / fs, false, false, false, 120.f, 3, 0.f, none);
				if (b.fired[0]) fires++;
			}
			float want = (float)lit * kClockRatio[mul[c]];
			checks++;
			if (std::fabs((float)fires - want) > 1.5f) {
				char d[160];
				snprintf(d, sizeof d, "x%g over a 16-step pass gave %d hits,"
				         " wanted about %.0f (%d lit steps)",
				         (double)kClockRatio[mul[c]], fires, (double)want, lit);
				fail("burst", d);
			}
		}

		// A ratchet has to start *on* the beat. Counting off a phase of its own
		// puts the hits at 1/R, 2/R ... 1 of the step, so the first is late by a
		// sub-division and the last lands on the next step -- audibly a
		// different figure from the same number of hits placed from zero.
		{
			Payroll b;
			b.reset(); b.running = true; b.burstMode = true;
			b.build(0.6f, 0, 0.f);
			int idx = mul[nmul - 1];
			for (int v = 0; v < V_COUNT; v++) b.ratioIndex[v] = idx;
			int firstStep = -1, sinceStep = -1, lastStep = -1;
			for (int i = 0; i < (int)(fs * 2.f); i++) {
				b.process(1.f / fs, false, false, false, 120.f, 3, 0.f, none);
				if (b.step != lastStep) { lastStep = b.step; sinceStep = 0; }
				else if (sinceStep >= 0) sinceStep++;
				if (b.fired[0] && firstStep < 0 && sinceStep >= 0) {
					firstStep = sinceStep;
					break;
				}
			}
			// eight steps a second at 48 kHz is 6000 samples a step; the first
			// hit of a burst belongs in the first handful of them.
			checks++;
			if (firstStep < 0 || firstStep > 4) {
				char d[128];
				snprintf(d, sizeof d, "the first hit of a burst landed %d samples"
				         " into the step, not on it", firstStep);
				fail("burst", d);
			}
		}

		// A voice whose pattern is empty stays silent however fast its ratio is.
		// This is the one that fails if the gate is dropped and burst mode turns
		// into grid mode wearing a different name.
		{
			Payroll b;
			b.reset(); b.running = true; b.burstMode = true;
			b.build(0.f, 0, 0.f);            // fill 0 -> every pattern empty
			for (int v = 0; v < V_COUNT; v++) b.ratioIndex[v] = kUnity + 6;
			int fires = 0;
			for (int i = 0; i < (int)(fs * 2.f); i++) {
				b.process(1.f / fs, false, false, false, 120.f, 3, 0.f, none);
				if (b.fired[0]) fires++;
			}
			checks++;
			if (fires != 0) {
				char d[128];
				snprintf(d, sizeof d, "an empty pattern still fired %d times", fires);
				fail("burst", d);
			}
		}

		// Dividing thins the pattern rather than thickening it: /4 must speak
		// strictly less often than the same pattern at unity.
		{
			int at[2] = {0, 0};
			const int idx[2] = { kUnity, kUnity - 4 };
			for (int k = 0; k < 2; k++) {
				Payroll b;
				b.reset(); b.running = true; b.burstMode = true;
				b.build(0.6f, 0, 0.f);
				for (int v = 0; v < V_COUNT; v++) b.ratioIndex[v] = idx[k];
				for (int i = 0; i < (int)(fs * 8.f); i++) {
					b.process(1.f / fs, false, false, false, 120.f, 3, 0.f, none);
					if (b.fired[0]) at[k]++;
				}
			}
			checks++;
			if (at[1] >= at[0] || at[1] == 0) {
				char d[160];
				snprintf(d, sizeof d, "/%g gave %d hits against unity's %d --"
				         " dividing must thin the pattern, not silence or thicken it",
				         1.0 / (double)kClockRatio[kUnity - 4], at[1], at[0]);
				fail("burst", d);
			}
		}
	}

	// --- SEED lands on the bar line, not under your hand ---------------------
	{
		const float fs = 48000.f;
		bool none[V_COUNT] = {};
		Payroll p;
		p.reset(); p.running = true; p.build(0.6f, 0, 0.f);

		bool before[V_COUNT][kSteps];
		for (int v = 0; v < V_COUNT; v++)
			for (int i = 0; i < kSteps; i++) before[v][i] = p.on[v][i];

		// run to somewhere in the middle of the bar, then turn SEED
		while (p.step < 5)
			p.process(1.f / fs, false, false, false, 120.f, 3, 0.f, none);
		p.build(0.6f, 3, 0.f);

		bool same = true;
		for (int v = 0; v < V_COUNT; v++)
			for (int i = 0; i < kSteps; i++) same &= (p.on[v][i] == before[v][i]);
		checks++;
		if (!same) fail("seed", "a new seed took effect in the middle of the bar");

		// and it must still be the old pattern right up to the bar line
		while (p.step != kSteps - 1)
			p.process(1.f / fs, false, false, false, 120.f, 3, 0.f, none);
		same = true;
		for (int v = 0; v < V_COUNT; v++)
			for (int i = 0; i < kSteps; i++) same &= (p.on[v][i] == before[v][i]);
		checks++;
		if (!same) fail("seed", "the pattern changed before the last step of the bar");

		// over the line, and it is the new one
		while (p.step != 0)
			p.process(1.f / fs, false, false, false, 120.f, 3, 0.f, none);
		bool changed = false;
		for (int v = 0; v < V_COUNT; v++)
			for (int i = 0; i < kSteps; i++) changed |= (p.on[v][i] != before[v][i]);
		checks++;
		if (!changed) fail("seed", "the new seed never arrived at the bar line");

		// A stopped clock has no bar to wait for: the seed has to take at once,
		// or the knob does nothing at all until you press RUN.
		Payroll q;
		q.reset(); q.build(0.6f, 0, 0.f);
		bool stopped[kSteps];
		for (int i = 0; i < kSteps; i++) stopped[i] = q.on[0][i];
		q.build(0.6f, 5, 0.f);
		changed = false;
		for (int i = 0; i < kSteps; i++) changed |= (q.on[0][i] != stopped[i]);
		checks++;
		if (!changed) fail("seed", "a stopped module ignored the seed knob");
	}

	// The internal clock has to land the right number of steps in a second.
	// 120 BPM at four steps to the beat is eight steps a second.
	{
		Payroll q;
		q.reset();
		q.running = true;
		q.build(1.f, 0, 0.f);          // every voice at its fullest
		bool none[V_COUNT] = {};
		const float fs = 48000.f;
		int pulses = 0;
		for (int i = 0; i < (int)fs; i++) {
			q.process(1.f / fs, false, false, false, 120.f, 3, 0.f, none);
			if (q.clockPulse) pulses++;
		}
		checks++;
		if (pulses < 7 || pulses > 9) {
			char d[128];
			snprintf(d, sizeof d, "120 BPM at 1/16 gave %d steps in a second, wanted 8", pulses);
			fail("clock rate", d);
		}
	}

	// A voice with its TRIG patched is the patch's, not the engine's.
	{
		Payroll q;
		q.reset();
		q.running = true;
		q.build(1.f, 0, 0.f);
		bool gate[V_COUNT] = {};
		gate[V_KICK] = true;
		const float fs = 48000.f;
		int kicks = 0, snares = 0;
		for (int i = 0; i < (int)(fs * 4.f); i++) {
			q.process(1.f / fs, false, false, false, 160.f, 3, 0.f, gate);
			if (q.fired[V_KICK]) kicks++;
			if (q.fired[V_SNARE]) snares++;
		}
		checks++;
		if (kicks != 0) fail("normalling", "the engine played a voice whose TRIG is patched");
		checks++;
		if (snares == 0) fail("normalling", "the engine played nothing at all");
	}
}


int main() {
	printf("Kickback voices\n");

	printf("  sweeping 44100 Hz...\n");  sweep(44100.f);
	printf("  sweeping 96000 Hz...\n");  sweep(96000.f);
	printf("  velocity...\n");           velocity(48000.f);
	printf("  transients...\n");         transient(48000.f);
	printf("  rate change...\n");        rateChange();
	printf("  sample rates...\n");       sampleRates();
	printf("  strikes end...\n");        decays();
	printf("  patterns...\n");           patternTests();

	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}

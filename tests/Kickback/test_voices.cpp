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
//
// 8. The patterns are *ranked*, not Euclid's raw answer for each k. The kick keeps
//    its downbeat at any FILL above the bottom stop (the old E(k,16) kick at 40%
//    was x....x....x.....), the pinned anchors survive every knob, the hits at
//    one FILL are all there at any higher one, a voice's cycle can be any length
//    from three to sixteen, EVOLVE moves the loop from pass to pass without
//    touching the anchors, and the same (seed, pass) always gives the same table.

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
			case 8: return tomHi.process(hit, vel, a, volts, b, c, d);
			default: return kick.process(hit, vel, 2, a, volts, b, c, d);
		}
	}
};

static const int kVoices = 10;
static const char* kName[kVoices] = {
	"KICK bridge", "KICK smurf", "SNARE xor", "SNARE vactrol", "SNARE dazzle",
	"HAT", "TOM I", "TOM II", "TOM III", "KICK sweep"
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
			// KICK smurf is not in this table on purpose. This test exists because the mode-bank voices "sounded
			// like bass oscillators"; SMURF is a relaxation-oscillator pulse train, every pulse of which has
			// the same sharp edges, so it has no onset to find an order of magnitude over its tail -- the
			// sheet itself says "neither sounds like a drum really". What does define its hit (the start-up
			// firing at the full supply, a train whose heights follow the supply down) is asserted in
			// smurfKick() below.
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

	// A voice with its TRIG patched is the engine's *and* the patch's: the two add,
	// the gate is the louder, and a stopped transport mutes the gate input too.
	{
		bool hit; float vel;
		// unpatched: the engine alone, accent on top
		strike(true, false, true, 1.f, true, 0.5f, 1.5f, hit, vel);
		checks++;
		if (!hit || std::fabs(vel - 0.75f) > 1e-5f) fail("strike", "an unpatched voice was not the engine's alone");
		strike(true, false, true, 1.f, false, 0.5f, 1.f, hit, vel);
		checks++;
		if (hit) fail("strike", "an unpatched voice played its (unread) gate");
		// patched, running: a gate alone, an engine hit alone, both at once
		strike(true, true, true, 1.2f, false, 0.9f, 1.f, hit, vel);
		checks++;
		if (!hit || vel != 1.2f) fail("strike", "a gate alone did not play at its own level");
		strike(true, true, false, 1.2f, true, 0.9f, 1.f, hit, vel);
		checks++;
		if (!hit || std::fabs(vel - 0.9f * kEngineUnderGate) > 1e-5f)
			fail("strike", "the engine's pattern was overridden by a patched gate");
		strike(true, true, true, 1.f, true, 1.f, 1.f, hit, vel);
		checks++;
		if (!hit || vel != 1.f) fail("strike", "on a coincident hit the gate was not the louder");
		// the gate is always at least the engine, so the engine is the lower
		checks++;
		if (kEngineUnderGate >= 1.f) fail("strike", "the engine is not quieter than the gate");
		// stopped: a patched voice is silent, gate or engine
		strike(false, true, true, 1.f, true, 1.f, 1.f, hit, vel);
		checks++;
		if (hit) fail("strike", "RUN off did not mute a patched gate input");
	}

	// SEED detent 0 is the engine OFF; the others are the patterns, shifted down by one.
	{
		checks++;
		if (engineOn(0)) fail("engine off", "SEED 0 left the pattern engine on");
		bool allOn = true, shifted = engineSeed(1) == 0 && engineSeed(2) == 1 && engineSeed(15) == 14;
		for (int k = 1; k <= 15; k++) allOn &= engineOn(k);
		checks++;
		if (!allOn) fail("engine off", "a non-zero SEED turned the engine off");
		checks++;
		if (!shifted) fail("engine off", "SEED knob 1 is not the engine's seed 0");
		bool hit; float vel;
		// off: an unpatched voice is silent, a patched one is just its gate
		strike(true, false, false, 1.f, false, 0.8f, 1.f, hit, vel);
		checks++;
		if (hit) fail("engine off", "an unpatched voice played with the engine off");
		strike(true, true, true, 1.f, false, 0.8f, 1.f, hit, vel);
		checks++;
		if (!hit || vel != 1.f) fail("engine off", "a patched gate did not play at full level with the engine off");
	}

	// Through the engine itself: with nothing marked as patched, every voice plays.
	{
		Payroll q;
		q.reset();
		q.running = true;
		q.build(1.f, 0, 0.f);
		bool none[V_COUNT] = {};
		const float fs = 48000.f;
		int kicks = 0, snares = 0;
		for (int i = 0; i < (int)(fs * 4.f); i++) {
			q.process(1.f / fs, false, false, false, 160.f, 3, 0.f, none);
			if (q.fired[V_KICK]) kicks++;
			if (q.fired[V_SNARE]) snares++;
		}
		checks++;
		if (kicks == 0 || snares == 0) fail("engine", "the engine did not play every voice");
	}
}


// ---------------------------------------------------------------------------
// 8: the ranked patterns -- anchors, nesting, polymeter, evolution
// ---------------------------------------------------------------------------

static int litCount(const Payroll& p, int v) {
	int n = 0;
	for (int s = 0; s < p.len[v]; s++) if (p.on[v][s]) n++;
	return n;
}

static void rankedTests() {
	const float shapes[] = { 0.f, 0.5f, 1.f };
	const float evolves[] = { 0.f, 0.7f };

	// The one anchor never vanishes: the kick keeps its downbeat at any FILL above
	// the bottom stop, at every SEED, SHAPE, EVOLVE and pass. A pinned step
	// outranks every knob. Nothing else is promised -- no half bar, no backbeat --
	// because a kit that promises those is a polka.
	{
		int bad = 0;
		for (int sd = 0; sd < 16; sd++)
		for (size_t sh = 0; sh < 3; sh++)
		for (size_t ev = 0; ev < 2; ev++)
		for (int cy = 0; cy < 8; cy++)
		for (int i = 1; i <= 100; i++) {
			float fill = i / 100.f;
			Payroll p;
			p.reset();
			p.build(fill, sd, 0.f, shapes[sh], evolves[ev]);
			for (int v = 0; v < V_COUNT; v++) { p.cycle[v] = (uint32_t)cy; p.buildVoice(v); }
			if (!p.on[V_KICK][0]) bad++;
		}
		checks++;
		if (bad) {
			char d[128];
			snprintf(d, sizeof d, "%d (seed, shape, evolve, pass, fill) settings lost a pinned step", bad);
			fail("anchors", d);
		}
	}

	// At a fifth of the knob the kick is two hits, one of them the downbeat -- and
	// the other is not always the half bar. Across the sixteen seeds the kick has
	// to land on at least eight different patterns, and some of them on steps that
	// are not on a quarter of the bar: a kick that can only halve is a polka.
	{
		bool offQuarter = false;
		int distinct = 0;
		bool seen[16][kSteps] = {};
		for (int sd = 0; sd < 16; sd++) {
			Payroll p; p.reset(); p.build(0.45f, sd, 0.f, 0.5f);
			bool dup = false;
			for (int e = 0; e < sd; e++) {
				bool same = true;
				for (int s = 0; s < kSteps; s++) same &= (seen[e][s] == p.on[V_KICK][s]);
				if (same) dup = true;
			}
			for (int s = 0; s < kSteps; s++) {
				seen[sd][s] = p.on[V_KICK][s];
				if (p.on[V_KICK][s] && s % 4 != 0) offQuarter = true;
			}
			if (!dup) distinct++;
			Payroll q; q.reset(); q.build(0.2f, sd, 0.f, 0.5f);
			checks++;
			if (litCount(q, V_KICK) != 2 || !q.on[V_KICK][0])
				fail("anchors", "the kick at 20% FILL was not two hits including the downbeat");
		}
		checks++;
		if (distinct < 8) {
			char d[96];
			snprintf(d, sizeof d, "sixteen seeds gave only %d different kick patterns", distinct);
			fail("variety", d);
		}
		checks++;
		if (!offQuarter) fail("variety", "no seed ever put a kick off the quarter-note grid");
		Payroll z; z.reset(); z.build(0.45f, 0, 0.f, 0.5f);
		bool floor4 = true;
		for (int s = 0; s < kSteps; s++) floor4 &= (z.on[V_KICK][s] == (s % 4 == 0));
		checks++;
		if (floor4) fail("variety", "the default kick is four on the floor");
	}

	// Below half the knob the kick is never outnumbered by the snare.
	{
		int bad = 0;
		for (int i = 1; i < 50; i++) {
			Payroll p; p.reset(); p.build(i / 100.f, 0, 0.f);
			if (p.onsets[V_KICK] < p.onsets[V_SNARE]) bad++;
		}
		checks++;
		if (bad) {
			char d[96];
			snprintf(d, sizeof d, "the snare had more hits than the kick at %d FILL settings below 50%%", bad);
			fail("kit balance", d);
		}
	}

	// Nesting: the hits at one FILL are all still there at any higher FILL, for
	// every voice, seed and shape. This is what Euclid by itself cannot do --
	// E(3,16) is not E(2,16) plus a hit -- and what lets one knob walk from a
	// sparse backbone to a busy pattern by adding the next most important hit.
	{
		int bad = 0;
		for (int sd = 0; sd < 16; sd++)
		for (size_t sh = 0; sh < 3; sh++) {
			bool prev[V_COUNT][kSteps] = {};
			for (int i = 0; i <= 100; i++) {
				Payroll p; p.reset(); p.build(i / 100.f, sd, 0.f, shapes[sh]);
				for (int v = 0; v < V_COUNT; v++)
					for (int s = 0; s < kSteps; s++) {
						if (prev[v][s] && !p.on[v][s]) bad++;
						prev[v][s] = p.on[v][s];
					}
			}
		}
		checks++;
		if (bad) {
			char d[96];
			snprintf(d, sizeof d, "%d hits were dropped by turning FILL up", bad);
			fail("nesting", d);
		}
	}

	// SHAPE has to do something, and SEED has to do something.
	{
		bool shapeMoves = false, seedMoves = false;
		Payroll a, b, c;
		a.reset(); b.reset(); c.reset();
		a.build(0.6f, 3, 0.f, 0.f);
		b.build(0.6f, 3, 0.f, 1.f);
		c.build(0.6f, 9, 0.f, 0.5f);
		Payroll m; m.reset(); m.build(0.6f, 3, 0.f, 0.5f);
		for (int v = 0; v < V_COUNT; v++)
			for (int s = 0; s < kSteps; s++) {
				shapeMoves |= (a.on[v][s] != b.on[v][s]);
				seedMoves |= (m.on[v][s] != c.on[v][s]);
			}
		checks++;
		if (!shapeMoves) fail("shape", "SHAPE 0 and SHAPE 1 gave the same patterns");
		checks++;
		if (!seedMoves) fail("seed", "two seeds gave the same patterns");
	}

	// Lengths: a shorter cycle only ever lights steps inside itself, keeps its
	// anchors, and its table is a function of the length alone.
	{
		bool ok = true, anchors = true;
		for (int L = 3; L <= kSteps; L++) {
			int lens[V_COUNT];
			for (int v = 0; v < V_COUNT; v++) lens[v] = L;
			for (int i = 1; i <= 100; i += 3) {
				Payroll p; p.reset(); p.build(i / 100.f, 0, 0.f, 0.5f, 0.f, lens);
				for (int v = 0; v < V_COUNT; v++) {
					for (int s = L; s < kSteps; s++) ok &= !p.on[v][s];
					ok &= (p.len[v] == L);
				}
				anchors &= p.on[V_KICK][0];
			}
		}
		checks++;
		if (!ok) fail("length", "a pattern lit a step beyond its cycle length");
		checks++;
		if (!anchors) fail("length", "the kick lost its downbeat at a shorter cycle length");
	}

	// Polymeter: a 12-step hat against a 16-step kick comes back into step after
	// 48 grid steps, and plays the same number of hits per pass of its own cycle.
	{
		const float fs = 48000.f;
		Payroll p;
		p.reset(); p.running = true;
		int lens[V_COUNT];
		for (int v = 0; v < V_COUNT; v++) lens[v] = 16;
		lens[V_HAT] = 12;
		p.build(0.6f, 0, 0.f, 0.5f, 0.f, lens);
		bool none[V_COUNT] = {};
		int hatHits = 0, lastStep = p.step, steps = 0, realign = -1;
		int hatOn = litCount(p, V_HAT);
		while (steps < 48) {
			p.process(1.f / fs, false, false, false, 120.f, 3, 0.f, none);
			if (p.fired[V_HAT]) hatHits++;
			if (p.step != lastStep) {
				lastStep = p.step; steps++;
				if (p.vstep[V_KICK] == 0 && p.vstep[V_HAT] == 0 && realign < 0) realign = steps;
			}
		}
		checks++;
		if (realign != 48) {
			char d[96];
			snprintf(d, sizeof d, "16- and 12-step cycles realigned after %d steps, wanted 48", realign);
			fail("polymeter", d);
		}
		// four hat passes of 12 steps fit in 48; the last boundary's hits are
		// armed on the step edge, so allow one pass of slack at the end.
		checks++;
		if (hatHits < hatOn * 3 || hatHits > hatOn * 4) {
			char d[96];
			snprintf(d, sizeof d, "a %d-hit, 12-step hat played %d hits in 48 steps", hatOn, hatHits);
			fail("polymeter", d);
		}
	}

	// Evolution. With EVOLVE at zero every pass is the same pass; raised, the
	// passes differ, but the pinned steps never do, the same (seed, pass) always
	// gives the same table, and every fourth pass is a fill (busier non-kick).
	{
		bool still = true;
		Payroll a; a.reset(); a.build(0.6f, 5, 0.f, 0.5f, 0.f);
		bool first[V_COUNT][kSteps];
		for (int v = 0; v < V_COUNT; v++) for (int s = 0; s < kSteps; s++) first[v][s] = a.on[v][s];
		for (int cy = 1; cy < 8; cy++)
			for (int v = 0; v < V_COUNT; v++) {
				a.cycle[v] = (uint32_t)cy; a.buildVoice(v);
				for (int s = 0; s < kSteps; s++) still &= (a.on[v][s] == first[v][s]);
			}
		checks++;
		if (!still) fail("evolve", "with EVOLVE at zero a later pass differed from the first");

		Payroll e, f;
		e.reset(); e.build(0.6f, 5, 0.f, 0.5f, 0.8f);
		f.reset(); f.build(0.6f, 5, 0.f, 0.5f, 0.8f);
		bool differs = false, repeatable = true, fillBusier = false;
		int counts[4] = {};
		for (int cy = 0; cy < 8; cy++) {
			for (int v = 0; v < V_COUNT; v++) {
				e.cycle[v] = (uint32_t)cy; e.buildVoice(v);
				f.cycle[v] = (uint32_t)cy; f.buildVoice(v);
				for (int s = 0; s < kSteps; s++) {
					if (e.on[v][s] != f.on[v][s]) repeatable = false;
					if (e.on[v][s] != a.on[v][s] && cy > 0) differs = true;
				}
			}
			counts[cy & 3] += litCount(e, V_HAT);
		}
		fillBusier = counts[3] > counts[0] && counts[3] > counts[1] && counts[3] > counts[2];
		checks++;
		if (!differs) fail("evolve", "EVOLVE up, and no pass ever differed from the first");
		checks++;
		if (!repeatable) fail("evolve", "the same seed and pass gave two different tables");
		checks++;
		if (!fillBusier) fail("evolve", "the fourth pass of four was not a fill");
	}

	// Micro-timing. Steps are shifted off the grid by a half, a third or a quarter
	// of a step, the anchor never is, every offset sits on the 1/384 tick grid, and
	// at HUMAN 0 the only offsets are those deliberate shifts.
	{
		Payroll p; p.reset(); p.build(1.f, 3, 0.f);
		bool grid = true, anyShift = false, anchorMoved = false, early = false;
		for (int v = 0; v < V_COUNT; v++)
			for (int s = 0; s < p.len[v]; s++) {
				float o = p.offs[v][s] * 384.f;
				if (std::fabs(o - std::floor(o + 0.5f)) > 1e-3f || std::fabs(p.offs[v][s]) > 0.52f)
					grid = false;
				if (p.offs[v][s] != 0.f) anyShift = true;
				if (p.offs[v][s] < 0.f) early = true;
			}
		for (int sd = 0; sd < 16; sd++) {
			Payroll q; q.reset(); q.build(0.5f, sd, 0.f, 0.5f, 0.9f);
			if (q.offs[V_KICK][0] != 0.f) anchorMoved = true;
		}
		checks++;
		if (!grid) fail("feel", "an offset was off the 1/384 grid or past half a step");
		checks++;
		if (!anyShift) fail("feel", "no step was shifted off the grid");
		checks++;
		if (!early) fail("feel", "no step was ever pulled early");
		checks++;
		if (anchorMoved) fail("feel", "the pinned anchor was shifted off the beat");
	}

	// Every hit the table lights is played exactly once, with micro-timing, early
	// hits armed a step ahead and swing all on: the look-ahead must neither lose a
	// hit nor play one twice. Counted over five bars of a 16-step cycle.
	{
		const float fs = 48000.f;
		bool none[V_COUNT] = {};
		bool ok = true;
		char why[96] = "";
		for (int sd = 0; sd < 6 && ok; sd++) {
			Payroll p;
			p.reset(); p.running = true; p.build(0.7f, sd, 0.f, 0.6f);
			int lit[V_COUNT], fires[V_COUNT] = {};
			for (int v = 0; v < V_COUNT; v++) lit[v] = litCount(p, v);
			for (long i = 0; i < (long)(fs * 2.f * 6); i++) {
				p.process(1.f / fs, false, false, false, 120.f, 3, 0.5f, none);
				for (int v = 0; v < V_COUNT; v++) if (p.fired[v]) fires[v]++;
			}
			for (int v = 0; v < V_COUNT; v++)
				if (fires[v] < lit[v] * 5 || fires[v] > lit[v] * 6) {
					ok = false;
					snprintf(why, sizeof why, "seed %d voice %d: %d hits in 6 bars from %d lit steps",
					         sd, v, fires[v], lit[v]);
				}
		}
		checks++;
		if (!ok) fail("look-ahead", why);
	}

	// Ratchets exist once EVOLVE is up, never on the anchor, never more than four.
	{
		bool any = false, bad = false;
		Payroll p; p.reset(); p.build(1.f, 2, 0.f, 0.5f, 0.9f);
		for (int cy = 0; cy < 8; cy++)
			for (int v = 0; v < V_COUNT; v++) {
				p.cycle[v] = (uint32_t)cy; p.buildVoice(v);
				for (int s = 0; s < p.len[v]; s++) {
					if (p.ratch[v][s] > 1) any = true;
					if (p.ratch[v][s] < 1 || p.ratch[v][s] > 4) bad = true;
				}
				if (p.ratch[v][0] > 1 && v == V_KICK) bad = true;
			}
		Payroll q; q.reset(); q.build(1.f, 2, 0.f, 0.5f, 0.f);
		for (int v = 0; v < V_COUNT; v++)
			for (int s = 0; s < q.len[v]; s++) if (q.ratch[v][s] != 1) bad = true;
		checks++;
		if (!any) fail("ratchet", "EVOLVE up and no step was ever ratcheted");
		checks++;
		if (bad) fail("ratchet", "a ratchet count was out of range, on the anchor, or present at EVOLVE 0");
	}
}


// ---------------------------------------------------------------------------
// 9: SWEEP's two defining properties
//
// A swept-oscillator kick is a pitch envelope in two stages, and it is the same
// kick every time. So: (a) the first few milliseconds sit well above the tuned
// pitch and the tail settles onto it, and (b) a second strike, arriving while the
// first is still ringing, begins on the same phase -- the opening of the two hits
// agree, because the oscillator restarts rather than carrying on from wherever
// it happened to be.
// ---------------------------------------------------------------------------
static void sweepKick() {
	const float fs = 48000.f;
	const float tune = 0.40f, decay = 0.5f, bend = 0.6f;
	const float f0 = expMap(tune, 32.f, 190.f);
	float colour = 0.25f + 0.5f * bend;

	// Mean frequency over a window, from upward zero crossings of a lowpassed copy.
	auto meanHz = [&](const std::vector<float>& y, float t0ms, float t1ms) {
		OnePole p1, p2;
		float G = poleG(900.f, fs);
		int a = (int)(t0ms * 0.001f * fs), b = (int)(t1ms * 0.001f * fs);
		int up = 0; float prev = 0.f; float first = -1.f, last = -1.f;
		for (int i = 0; i < b; i++) {
			float l = p2.lp(p1.lp(y[i], G), G);
			if (i >= a && prev < 0.f && l >= 0.f) {
				float pos = (i - 1) + (-prev) / (l - prev);
				if (first < 0.f) first = pos;
				last = pos; up++;
			}
			prev = l;
		}
		return up >= 2 ? (float)(up - 1) * fs / (last - first) : 0.f;
	};

	std::vector<float> y((size_t)(0.5f * fs));
	Kick k; k.setRate(fs); k.reset();
	for (size_t i = 0; i < y.size(); i++) y[i] = k.process(i == 0, 1.f, 2, tune, 0.f, decay, bend, colour);
	float early = meanHz(y, 0.f, 9.f), late = meanHz(y, 150.f, 250.f);
	char d[200];
	if (early < 4.f * f0) {
		snprintf(d, sizeof d, "first 9 ms averaged %.0f Hz against a tuned %.0f Hz -- no spike", early, f0);
		fail("sweep spike", d);
	}
	if (late < 0.85f * f0 || late > 1.25f * f0) {
		snprintf(d, sizeof d, "the tail sat at %.0f Hz against a tuned %.0f Hz", late, f0);
		fail("sweep settle", d);
	}
	checks += 2;

	// Phase lock: strike, let it ring 137 ms (an arbitrary, non-multiple delay),
	// strike again, and compare the opening 3 ms of the two hits. The attack
	// layer's noise differs from hit to hit by design, so compare with the noise
	// out of the picture by correlating the lowpassed signals.
	std::vector<float> z((size_t)(0.5f * fs));
	size_t again = (size_t)(0.137f * fs);
	Kick k2; k2.setRate(fs); k2.reset();
	for (size_t i = 0; i < z.size(); i++)
		z[i] = k2.process(i == 0 || i == again, 1.f, 2, tune, 0.f, decay, bend, colour);
	OnePole a1, a2, b1, b2; float G = poleG(500.f, fs);
	double sab = 0, saa = 0, sbb = 0;
	int n3 = (int)(0.003f * fs);
	std::vector<float> la(n3), lb(n3);
	for (int i = 0; i < n3; i++) {
		la[i] = a2.lp(a1.lp(y[i], G), G);
		lb[i] = b2.lp(b1.lp(z[again + i], G), G);
	}
	for (int i = 0; i < n3; i++) { sab += la[i] * lb[i]; saa += la[i] * la[i]; sbb += lb[i] * lb[i]; }
	double corr = sab / (std::sqrt(saa * sbb) + 1e-30);
	if (corr < 0.95) {
		snprintf(d, sizeof d, "the opening of a retrigger correlated %.3f with the opening of the first hit", corr);
		fail("sweep phase lock", d);
	}
	checks++;
}


// ---------------------------------------------------------------------------
// 10: BRIDGE is the 808, and the 808 has three properties a ringing filter does not
//
// (a) For the first ~6 ms the envelope generator holds Q43 on and the bridged-T's centre
//     frequency is thrown up by more than an octave, so the first completed half-cycle is
//     much shorter than half a period of the tuned pitch.
// (b) The ring settles on the tuned pitch: the capacitors are scaled with TUNE and
//     nothing else moves it.
// (c) Nothing is zeroed on a strike. A hit that arrives while the last still rings is not
//     the same waveform as a hit on silence -- the paper's "no machine-gun effect".
// (d) DECAY lands near the t60 it names, by inverting the measured VR6 table.
// ---------------------------------------------------------------------------
static void bridgeKick() {
	const float fs = 48000.f;
	char d[240];

	const float tunes[] = { 0.1f, 0.4f, 0.9f };
	for (int ti = 0; ti < 3; ti++) {
		float tune = tunes[ti], f0 = expMap(tune, 32.f, 190.f), bend = 0.6f;
		std::vector<float> y((size_t)(0.3f * fs));
		Kick k; k.setRate(fs); k.reset();
		for (size_t i = 0; i < y.size(); i++)
			y[i] = k.process(i == 0, 1.f, 0, tune, 0.f, 0.5f, bend, 0.25f + 0.5f * bend);

		// Sign changes more than 0.3 ms apart (the edge's own ringing is not a half-cycle).
		float cross[4]; int nc = 0; float lastT = -1.f;
		for (size_t i = 1; i < y.size() && nc < 2; i++)
			if ((y[i - 1] < 0.f) != (y[i] < 0.f)) {
				float t = ((float)(i - 1) + y[i - 1] / (y[i - 1] - y[i])) / fs;
				if (t - lastT > 0.0003f) { cross[nc++] = t; lastT = t; }
			}
		checks++;
		if (nc < 2) { fail("808 attack", "fewer than two zero crossings in 300 ms"); continue; }
		float half = cross[1] - cross[0];                  // the first completed half-cycle
		float hz = 0.5f / half;
		if (hz < 1.5f * f0) {
			snprintf(d, sizeof d, "tune %.1f Hz: the first half-cycle ran at %.0f Hz (%.2fx) -- no Fc jump",
			         f0, hz, hz / f0);
			fail("808 attack", d);
		}

		// Mean upward-crossing frequency over 150-250 ms.
		OnePole p1, p2; float G = poleG(900.f, fs);
		int a = (int)(0.15f * fs), b = (int)(0.25f * fs), up = 0; float prev = 0.f, first = -1.f, last = -1.f;
		std::vector<float> yy((size_t)(0.26f * fs));
		Kick k2; k2.setRate(fs); k2.reset();
		for (size_t i = 0; i < yy.size(); i++)
			yy[i] = k2.process(i == 0, 1.f, 0, tune, 0.f, 0.5f, bend, 0.25f + 0.5f * bend);
		for (int i = 0; i < b; i++) {
			float l = p2.lp(p1.lp(yy[i], G), G);
			if (i >= a && prev < 0.f && l >= 0.f) {
				float pos = (float)(i - 1) + (-prev) / (l - prev);
				if (first < 0.f) first = pos;
				last = pos; up++;
			}
			prev = l;
		}
		float late = up >= 2 ? (float)(up - 1) * fs / (last - first) : 0.f;
		checks++;
		if (late < 0.9f * f0 || late > 1.1f * f0) {
			snprintf(d, sizeof d, "tuned %.1f Hz but the ring settled at %.1f Hz", f0, late);
			fail("808 pitch", d);
		}
	}

	// (c) a retrigger over a ringing tail against a hit on silence
	{
		const float tune = 0.4f, bend = 0.6f;
		int n = (int)(0.5f * fs), again = (int)(0.137f * fs);
		std::vector<float> a(n), b(n);
		Kick k; k.setRate(fs); k.reset();
		for (int i = 0; i < n; i++) a[i] = k.process(i == 0, 1.f, 0, tune, 0.f, 0.5f, bend, 0.55f);
		Kick k2; k2.setRate(fs); k2.reset();
		for (int i = 0; i < n; i++) b[i] = k2.process(i == 0 || i == again, 1.f, 0, tune, 0.f, 0.5f, bend, 0.55f);
		double dd = 0, e = 0; int m = (int)(0.05f * fs);
		for (int i = 0; i < m; i++) { double x = a[i], y2 = b[again + i]; dd += (x - y2) * (x - y2); e += x * x; }
		double rel = std::sqrt(dd / (e + 1e-30));
		checks++;
		if (rel < 0.005) {
			snprintf(d, sizeof d, "a retrigger over a tail matched a fresh hit to %.4f rms -- the state was reset", rel);
			fail("808 retrigger", d);
		}
	}

	// (d) DECAY. Time for the 20 ms peak envelope to fall 40 dB from its fourth window, x1.5.
	{
		const float decays[] = { 0.5f, 0.8f };
		for (int di = 0; di < 2; di++) {
			float dec = decays[di], want = expMap(dec, 0.05f, 1.8f);
			Kick k; k.setRate(fs); k.reset();
			int n = (int)(6.f * fs), w = (int)(0.02f * fs);
			std::vector<float> pk;
			float cur = 0.f; int c = 0;
			for (int i = 0; i < n; i++) {
				cur = std::fmax(cur, std::fabs(k.process(i == 0, 1.f, 0, 0.4f, 0.f, dec, 0.6f, 0.55f)));
				if (++c == w) { pk.push_back(cur); cur = 0.f; c = 0; }
			}
			size_t i0 = 3, i1 = i0;
			while (i1 < pk.size() && pk[i1] > pk[i0] * 0.01f) i1++;
			float t60 = (float)(i1 - i0) * 0.02f * 1.5f;
			checks++;
			if (t60 < 0.7f * want || t60 > 1.3f * want) {
				snprintf(d, sizeof d, "DECAY %.1f asked for t60 %.2f s and rang for %.2f s", dec, want, t60);
				fail("808 decay", d);
			}
		}
	}
}


// ---------------------------------------------------------------------------
// 11: SMURF is the Smurf Drum -- a supply-starved relaxation oscillator
//
// (a) The pair fires once at the full supply a moment after the trigger (the hit), then settles to a train
//     of pulses at about the tuned rate whose heights follow the supply down.
// (b) It starts from the same state every time: a strike on silence is the same strike, whatever the last
//     one left behind. (The earlier SMURF free-ran its oscillator, so the first millisecond differed.)
// (c) It ends: the pair stops firing when the supply falls below its cutoff.
// ---------------------------------------------------------------------------
struct PulseRec { float t, h; };
static std::vector<PulseRec> findPulses(const std::vector<float>& y, float fs, float floorV) {
	std::vector<PulseRec> out;
	bool in = false; size_t st = 0; float pk = 0.f;
	for (size_t i = 1; i < y.size(); i++) {
		if (!in && y[i] > floorV) { in = true; st = i; pk = y[i]; }
		else if (in) {
			if (y[i] > pk) pk = y[i];
			if (y[i] < 0.4f * floorV) { in = false; out.push_back({ (float)st / fs * 1000.f, pk }); }
		}
	}
	return out;
}

static void smurfKick() {
	const float fs = 48000.f;
	char d[240];

	// (a) the hit, the rate, and the heights. DECAY 0.148 is a 10 uF hold cap, the sheet's own.
	const float tunes[] = { 0.1f, 0.4f, 0.9f };
	for (int ti = 0; ti < 3; ti++) {
		float tune = tunes[ti], f0 = expMap(tune, 32.f, 190.f);
		std::vector<float> y((size_t)(1.5f * fs));
		Kick k; k.setRate(fs); k.reset();
		for (size_t i = 0; i < y.size(); i++) y[i] = k.process(i == 0, 1.f, 1, tune, 0.f, 0.148f, 0.6f, 0.55f);
		float pkAll = 0.f; for (float v : y) pkAll = std::fmax(pkAll, std::fabs(v));
		std::vector<PulseRec> p = findPulses(y, fs, 0.15f * pkAll);
		checks++;
		if (p.size() < 6) { snprintf(d, sizeof d, "tune %.0f Hz: only %zu pulses", f0, p.size()); fail("smurf train", d); continue; }
		// the hit: first pulse inside 3 ms, taller than the one after it
		checks++;
		if (p[0].t > 3.5f || p[0].h < 1.1f * p[1].h) {
			snprintf(d, sizeof d, "tune %.0f Hz: first pulse at %.2f ms, %.2f V against %.2f V next -- no start-up firing", f0, p[0].t, p[0].h, p[1].h);
			fail("smurf hit", d);
		}
		// the rate: mean pulse spacing over pulses 2..6, within 25% of the tuned rate
		float span = (p[5].t - p[1].t) / 4.f;
		float hz = 1000.f / span;
		checks++;
		if (hz < 0.75f * f0 || hz > 1.25f * f0) {
			snprintf(d, sizeof d, "tuned %.1f Hz but the pulses came at %.1f Hz", f0, hz);
			fail("smurf rate", d);
		}
		// the heights follow the supply: never rising once the train is running
		checks++;
		bool falling = true;
		for (size_t i = 2; i < 6 && i < p.size(); i++) if (p[i].h > 1.05f * p[i - 1].h) falling = false;
		if (!falling) fail("smurf envelope", "pulse heights rose within the first train");
	}

	// (b) the same strike every time
	{
		const float tune = 0.4f;
		std::vector<float> y((size_t)(3.0f * fs));
		Kick k; k.setRate(fs); k.reset();
		size_t again = (size_t)(1.6f * fs);               // long after the first has finished
		for (size_t i = 0; i < y.size(); i++) y[i] = k.process(i == 0 || i == again, 1.f, 1, tune, 0.f, 0.148f, 0.6f, 0.55f);
		int n = (int)(0.04f * fs); float worst = 0.f, pk = 0.f;
		for (int i = 0; i < n; i++) { worst = std::fmax(worst, std::fabs(y[i] - y[again + i])); pk = std::fmax(pk, std::fabs(y[i])); }
		checks++;
		if (worst > 0.01f * pk) {
			snprintf(d, sizeof d, "the opening 40 ms of a second strike differed from the first by %.3f V (peak %.2f V)", worst, pk);
			fail("smurf same strike", d);
		}
	}

	// (c) it ends, at every DECAY
	{
		const float decays[] = { 0.0f, 0.5f, 1.0f };
		const float limitS[] = { 0.4f, 1.5f, 6.0f };
		for (int di = 0; di < 3; di++) {
			int n = (int)(8.f * fs);
			Kick k; k.setRate(fs); k.reset();
			int last = 0;
			for (int i = 0; i < n; i++) { float v = k.process(i == 0, 1.f, 1, 0.4f, 0.f, decays[di], 0.6f, 0.55f); if (std::fabs(v) > 0.02f) last = i; }
			checks++;
			if (last > (int)(limitS[di] * fs)) {
				snprintf(d, sizeof d, "DECAY %.1f was still sounding at %.2f s (limit %.1f s)", decays[di], (float)last / fs, limitS[di]);
				fail("smurf ends", d);
			}
		}
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
	printf("  ranked patterns...\n");    rankedTests();
	printf("  sweep kick...\n");         sweepKick();
	printf("  808 kick...\n");           bridgeKick();
	printf("  smurf kick...\n");         smurfKick();

	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}

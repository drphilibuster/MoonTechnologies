#pragma once
// ---------------------------------------------------------------------------
// The payroll: Kickback's own clock and pattern engine, so the module is a
// drum machine on its own and a bank of drum voices when it isn't.
//
// The normalling rule is the whole design. A voice whose TRIG jack is empty is
// played by the engine; a voice with something patched into its TRIG is played
// by that and nothing else. So an unpatched Kickback runs a kit, patching one
// TRIG takes that one voice over, and patching all nine leaves the engine doing
// nothing but driving CLK OUT. Nothing has to be switched on or off to move
// between those.
//
// What the engine emits per step is a trigger *and a velocity*, because a grid
// of identical hits is not a performance. That framing is straight out of the
// symbolic-drum-generation literature -- Soiledis et al., "Drum Synthesis from
// Expressive Drum Grids via Neural Audio Codecs" (arXiv:2605.10281), take an
// "expressive drum grid" to be a time-aligned grid *with microtiming and
// velocity information*, and train on E-GMD because that is what human playing
// has in it. HUMAN below is those two axes on one knob. Kirby & Sandler make
// the same point from the synthesis side: subtle per-strike variation is what
// avoids "the machine gun effect."
//
// Pure DSP, like the voices: no Rack types, no allocation, no I/O.
// ---------------------------------------------------------------------------

#include <cmath>
#include <cstdint>

#include "Voices.hpp"

namespace kickback {

//: One bar of sixteenths. Long enough to hold a pattern, short enough that
//: every step is reachable from one FILL knob.
static const int kSteps = 16;

/** Bjorklund's algorithm: place `k` onsets as evenly as possible in `n` steps.

    The Euclidean rhythm. Toussaint's observation is that E(k,n) for small
    integers is most of the world's ostinati -- E(3,8) is the tresillo, E(5,8)
    the cinquillo, E(2,5) the khafif-e-ramal, E(7,16) a Brazilian necklace --
    and that they are the same sequence Euclid's algorithm produces for
    gcd(k,n). The recursion below is the standard one: repeatedly distribute
    the remainder into the head of the sequence until the remainder is 1 or 0,
    which is exactly the subtraction in Euclid.

    Bjorklund's own bookkeeping is a pair of lists of bit-strings that get
    concatenated head-to-head until one side is exhausted. At n <= 16 every
    string fits in a machine word, so the two lists collapse to (pattern,
    length, count) pairs and the whole thing allocates nothing: `a` groups of
    `apat`, then `b` groups of `bpat`, concatenated, is the answer.

    E(3,8) comes out 10010010, the tresillo; E(5,8) 10110110, the cinquillo;
    E(5,16) 1001001001001000. Those are the sequences the name promises. */
inline void euclid(int k, int n, bool* out) {
	for (int i = 0; i < n; i++) out[i] = false;
	if (n <= 0 || k <= 0) return;
	if (k >= n) { for (int i = 0; i < n; i++) out[i] = true; return; }

	uint32_t apat = 1, bpat = 0;      // a "1" group and a "0" group
	int alen = 1, blen = 1;
	int na = k, nb = n - k;

	while (nb > 1) {
		int m = na < nb ? na : nb;
		uint32_t np = (apat << blen) | bpat;   // a ++ b
		int nl = alen + blen;
		if (na > m) { bpat = apat; blen = alen; nb = na - m; }
		else        { nb = nb - m; }
		apat = np; alen = nl; na = m;
	}

	int at = 0;
	for (int g = 0; g < na && at < n; g++)
		for (int j = alen - 1; j >= 0 && at < n; j--) out[at++] = (apat >> j) & 1u;
	for (int g = 0; g < nb && at < n; g++)
		for (int j = blen - 1; j >= 0 && at < n; j--) out[at++] = (bpat >> j) & 1u;
}

/** Each voice's place in the queue, as (start, span, base rotation).

    Euclid says how to spread k onsets over sixteen steps; it does not say what
    k should be, and a kit where every voice has the same k is a polyrhythm
    demo rather than a beat. `start` is the FILL at which the voice first
    speaks, `span` how much of the remaining knob it takes to reach its
    fullest, `most` the largest k it ever asks for, and `rot` the rotation that
    puts its default pattern where a player would: E(2,16) rotated by four is
    the backbeat, E(4,16) unrotated is four on the floor.

    The starts matter more than they look. They were first set so the toms
    arrived late, as fills -- which put all three of them at k = 0 for the whole
    lower half of the knob, including at the FILL the module ships with, so an
    unpatched Kickback simply never played a tom. A voice that is silent at the
    default is a voice the module does not appear to have. */
struct Role { float start, span; int most, rot; };
static const Role kRole[V_COUNT] = {
	{ 0.00f, 0.80f,  7,  0 },   // KICK    -- in from the first turn of the knob
	{ 0.06f, 0.74f,  6,  4 },   // SNARE   -- the backbeat
	{ 0.03f, 0.94f, 16,  0 },   // HAT     -- fills all the way to sixteenths
	{ 0.20f, 0.75f,  5, 10 },   // TOM I   -- the toms are part of the kit, not
	{ 0.28f, 0.70f,  5, 13 },   // TOM II     a garnish on top of it: at the
	{ 0.36f, 0.62f,  4, 11 },   // TOM III    default FILL all three speak
};

/** Every ratio a voice can run at against the grid, as multipliers of the step
    rate. Symmetric about x1: index kUnity is 1, and the two halves mirror.

    Grid mode turns the pattern engine off and lets every voice hit on every
    step; these ratios are then what makes a beat out of that. Multiplying is
    the obvious half -- x2, x4 and so on subdivide -- but the divisions are the
    interesting ones, because a voice on /5 against a voice on /7 takes
    thirty-five steps to come back round, and one on /256 is a hit every
    sixteen bars.

    The steps are 2s, 3s, 5s and 7s rather than powers of two alone. Powers of
    two stay on the grid and never produce anything the pattern engine could
    not; the odd factors are where the polyrhythms live. Out to 256 in both
    directions: at 120 BPM on a sixteenth-note grid, x256 is two kilohertz --
    the top of the range is audio rate, which is the point at which dividing a
    clock stops being rhythm, and there is no reason to go further. */
static const int kRatioCount = 39;
static const int kUnity = 19;               // kClockRatio[19] == 1
static const float kClockRatio[kRatioCount] = {
	1.f/256, 1.f/192, 1.f/128, 1.f/96, 1.f/64, 1.f/48, 1.f/32, 1.f/24,
	1.f/16,  1.f/14,  1.f/12,  1.f/10, 1.f/8,  1.f/7,  1.f/6,  1.f/5,
	1.f/4,   1.f/3,   1.f/2,
	1.f,
	2.f,     3.f,     4.f,     5.f,    6.f,    7.f,    8.f,    10.f,
	12.f,    14.f,    16.f,    24.f,   32.f,   48.f,   64.f,   96.f,
	128.f,   192.f,   256.f,
};

//: How many sixteenths of the grid one clock step is worth, per DIV position.
//: Written as steps-per-beat so the names on the panel mean what they say.
static const int kDivCount = 6;
static const float kDivPerBeat[kDivCount] = { 1.f, 2.f, 3.f, 4.f, 6.f, 8.f };

/** One deterministic 32-bit hash. The pattern has to be the same every time a
    patch is opened and the same on every machine, so it is derived from
    (seed, voice, step) rather than drawn from a running generator. */
inline uint32_t hash3(uint32_t a, uint32_t b, uint32_t c) {
	uint32_t x = a * 0x9E3779B1u ^ b * 0x85EBCA77u ^ c * 0xC2B2AE3Du;
	x ^= x >> 15; x *= 0x2C1B3C6Du;
	x ^= x >> 12; x *= 0x297A2D39u;
	x ^= x >> 15;
	return x;
}

inline float hash3f(uint32_t a, uint32_t b, uint32_t c) {
	return (float)(hash3(a, b, c) >> 8) * (1.f / 16777216.f);   // 0..1
}


/** The engine. One instance per module; `process` is called once a sample and
    reports, through `fired`/`vel`, which voices the engine wants struck now. */
struct Payroll {
	// --- transport -----------------------------------------------------------
	bool running = false;
	int step = 0;                   // 0..kSteps-1
	double phase = 0.0;             // 0..1 through the current step
	double stepHz = 8.0;            // steps per second

	// External clock: measured period, so a quarter-note clock can still drive
	// a sixteenth-note grid. Free-runs between edges and resyncs on each one.
	float extPeriod = 0.f;          // seconds between the last two edges
	float sinceEdge = 0.f;
	bool extSeen = false;

	// --- the pattern, rebuilt only when SEED or FILL moves --------------------
	bool on[V_COUNT][kSteps] = {};
	float amp[V_COUNT][kSteps] = {};
	float offs[V_COUNT][kSteps] = {};   // microtiming, in fractions of a step
	int onsets[V_COUNT] = {};           // the k of each voice's E(k,16)
	int rotation[V_COUNT] = {};         // and how far it is turned
	int builtSeed = -1;
	int builtFill = -1;
	int builtHuman = -1;

	// --- what this sample should do -----------------------------------------
	bool fired[V_COUNT] = {};
	float vel[V_COUNT] = {};
	bool clockPulse = false;        // a step boundary happened this sample
	float pulseLeft = 0.f;          // seconds of CLK OUT high remaining

	// Steps that are waiting on their microtiming offset before they fire.
	bool pending[V_COUNT] = {};
	float pendingIn[V_COUNT] = {};  // seconds until it lands

	//: Grid mode's per-voice phase, one turn per hit. Free-running against the
	//: step grid at that voice's own ratio, so two voices on coprime divisions
	//: drift through a long pattern rather than repeating every bar.
	double gridPhase[V_COUNT] = {};

	//: Grid mode, and what each voice runs at in it. Set by the caller before
	//: process(), the way `running` is.
	bool gridMode = false;
	int ratioIndex[V_COUNT] = {};
	uint32_t gridCount[V_COUNT] = {};
	float humanHeld = 0.f;

	//: Which voices are under external control this sample. Held for the
	//: duration of one process() call so advanceInto() can honour the
	//: normalling rule without threading the array through.
	const bool* gateNow = nullptr;

	void reset() {
		running = false; step = 0; phase = 0.0;
		extPeriod = 0.f; sinceEdge = 0.f; extSeen = false;
		pulseLeft = 0.f;
		builtSeed = builtFill = builtHuman = -1;
		gridMode = false; humanHeld = 0.f;
		for (int v = 0; v < V_COUNT; v++) { ratioIndex[v] = kUnity; gridCount[v] = 0; }
		for (int v = 0; v < V_COUNT; v++) {
			fired[v] = false; vel[v] = 0.f;
			pending[v] = false; pendingIn[v] = 0.f;
			gridPhase[v] = 0.0;
		}
	}

	/** Rebuild the nine Euclidean patterns. Only runs when one of the three
	    knobs that shape them has moved to a new quantised value, so it costs
	    nothing while the module is playing.

	    FILL sets every voice's k at once, through its role: a voice's k is
	    monotone in FILL, so turning the knob up never takes an onset away, and
	    each voice enters the kit at its own point on the sweep. SEED turns each
	    voice's necklace by a different amount -- rotation is the other half of
	    what makes E(k,n) a family rather than a single pattern, and rotating
	    the voices against each other is what stops nine Euclidean rhythms from
	    landing on the same grid points and sounding like one. */
	void build(float fill, int seed, float human) {
		int qf = (int)(fill * 256.f);
		int qh = (int)(human * 64.f);
		if (qf == builtFill && seed == builtSeed && qh == builtHuman) return;
		builtFill = qf; builtSeed = seed; builtHuman = qh;
		humanHeld = human;

		for (int v = 0; v < V_COUNT; v++) {
			const Role& role = kRole[v];
			float t = (fill - role.start) / role.span;
			t = clampf(t, 0.f, 1.f);
			// Round rather than truncate, so the first onset arrives as soon
			// as the voice's share of the knob is half a hit wide.
			int k = (int)(t * role.most + 0.5f);
			onsets[v] = k;

			bool raw[kSteps];
			euclid(k, kSteps, raw);

			// The rotation: the role's own, plus the seed's, per voice.
			int rot = role.rot;
			if (seed > 0) rot += (int)(hash3(seed, v, 7u) % (uint32_t)kSteps);
			rot %= kSteps;
			rotation[v] = rot;

			for (int s = 0; s < kSteps; s++) {
				int src = (s - rot) % kSteps;
				if (src < 0) src += kSteps;
				on[v][s] = raw[src];

				// Velocity. A Euclidean onset that lands on a beat is an
				// accent and one that lands between beats is a ghost -- that
				// alone is most of what makes a generated pattern sit down.
				float weight = (s % 4 == 0) ? 1.f : ((s % 2 == 0) ? 0.78f : 0.62f);
				float spread = (hash3f(seed + 101u, v, s) - 0.5f) * human * 0.8f;
				amp[v][s] = clampf(weight + spread, 0.14f, 1.f);

				// Microtiming: up to a sixth of a step either way, and the
				// downbeats move least, as a player's do.
				float pull = (hash3f(seed + 211u, v, s) - 0.5f) * 2.f;
				float anchor = (s % 4 == 0) ? 0.25f : 1.f;
				offs[v][s] = pull * human * 0.16f * anchor;
			}
		}
	}

	/** Advance one sample.

	    `extEdge` is a rising edge on CLK IN this sample; `extConnected` says
	    whether anything is patched there at all. `swing` delays the odd
	    sixteenths. `gate[v]` is true where a voice's own TRIG is patched, and
	    the engine leaves those voices alone. */
	void process(float dt, bool extConnected, bool extEdge, bool resetEdge,
	             float bpm, int div, float swing, const bool gate[V_COUNT]) {
		gateNow = gate;
		for (int v = 0; v < V_COUNT; v++) fired[v] = false;
		clockPulse = false;

		if (resetEdge) {
			step = 0; phase = 0.0;
			for (int v = 0; v < V_COUNT; v++) { pending[v] = false; gridPhase[v] = 0.0; }
		}

		if (pulseLeft > 0.f) pulseLeft -= dt;

		// --- work out how fast the grid is running ---------------------------
		float perBeat = kDivPerBeat[div < 0 ? 0 : (div >= kDivCount ? kDivCount - 1 : div)];
		if (extConnected) {
			sinceEdge += dt;
			if (extEdge) {
				// Ignore absurd periods: a first edge, or a clock that stopped
				// and started, must not set the grid to a thousandth of a Hz.
				if (extSeen && sinceEdge > 0.004f && sinceEdge < 6.f) extPeriod = sinceEdge;
				sinceEdge = 0.f;
				extSeen = true;
				// Resync. An external edge is always a beat boundary, so the
				// phase lands on it however far the free-run had drifted.
				//
				// The step index only snaps when the subdivision actually
				// divides the bar: at 1/8 triplets a sixteen-step pattern is
				// five and a third beats long, and rounding the step to a
				// multiple of three there would jump the pattern by one or two
				// steps on every beat rather than locking it. Where it does not
				// divide, the phase lock alone keeps the grid on the clock and
				// RST is what aligns the bar.
				int pb = (int)perBeat;
				if (pb > 0 && kSteps % pb == 0) step = (step / pb) * pb;
				phase = 0.0;
				// Before advanceInto, not after: it scales swing and
				// microtiming by the step length, and the step length comes
				// from the period this edge just measured. Arming the step
				// first would time the first hit after any tempo change
				// against the *old* tempo.
				stepHz = extPeriod > 1e-4f ? perBeat / extPeriod : 0.0;
				advanceInto(step, swing);
			}
			stepHz = extPeriod > 1e-4f ? perBeat / extPeriod : 0.0;
			// The engine runs whenever a clock is patched; RUN is the switch
			// for the internal one only.
			if (!extSeen) return;
		}
		else {
			stepHz = (double)bpm / 60.0 * perBeat;
			if (!running) {
				// Held at the top of the bar, so pressing RUN starts on 1.
				step = 0; phase = 0.0;
				for (int v = 0; v < V_COUNT; v++) pending[v] = false;
				return;
			}
		}

		// --- grid mode -------------------------------------------------------
		// FILL at its bottom stop switches the pattern engine off: every voice
		// hits on every step, and each voice's RATIO knob multiplies or divides
		// that for itself. It lives at the bottom of FILL because that is where
		// the Euclidean patterns are all empty anyway -- the knob used to sit
		// on a setting where the module was simply silent, which is a setting
		// nobody wants and the right place for the mode that replaces it.
		//
		// Each voice keeps its own free-running phase rather than counting
		// steps, so a voice on /5 and a voice on /7 drift through thirty-five
		// steps before they agree again. That is where the long patterns come
		// from, and it is why division is worth as much as multiplication here.
		if (gridMode) {
			for (int v = 0; v < V_COUNT; v++) {
				pending[v] = false;
				int idx = ratioIndex[v];
				idx = idx < 0 ? 0 : (idx >= kRatioCount ? kRatioCount - 1 : idx);
				double hz = stepHz * (double)kClockRatio[idx];
				// Past a quarter of the sample rate a trigger is not a rhythm
				// any more, and the loop below would have to fire more times
				// than there are samples. The top of the range is clamped
				// rather than left to run away.
				double cap = 0.25 / (double)dt;
				if (hz > cap) hz = cap;
				if (hz <= 0.0) continue;
				gridPhase[v] += (double)dt * hz;
				int guard = 0;
				while (gridPhase[v] >= 1.0 && guard++ < 4) {
					gridPhase[v] -= 1.0;
					// HUMAN still spreads the velocity, so a grid of identical
					// hits is not what comes out unless it is asked for.
					float spread = (hash3f((uint32_t)(builtSeed + 101), (uint32_t)v,
					                       gridCount[v] & 15u) - 0.5f) * humanHeld * 0.8f;
					vel[v] = clampf(1.f + spread, 0.14f, 1.f);
					gridCount[v]++;
					if (!gate[v]) fired[v] = true;
				}
			}
			// and fall through: the step counter still runs, so CLK OUT keeps
			// ticking and RATIO has a grid to be a ratio *of*.
		}
		else {
			// --- release anything whose microtiming offset has come due ------
			for (int v = 0; v < V_COUNT; v++) {
				if (!pending[v]) continue;
				pendingIn[v] -= dt;
				if (pendingIn[v] <= 0.f) {
					pending[v] = false;
					if (!gate[v]) fired[v] = true;
				}
			}
		}

		// --- advance the grid ------------------------------------------------
		if (stepHz <= 0.0) return;
		phase += (double)dt * stepHz;
		while (phase >= 1.0) {
			phase -= 1.0;
			step = (step + 1) % kSteps;
			advanceInto(step, swing);
		}
	}

	/** Arm whatever the new step wants, offset by swing and microtiming. */
	void advanceInto(int s, float swing) {
		const bool* gate = gateNow;
		clockPulse = true;
		pulseLeft = 0.001f;
		// In grid mode a step boundary is still a clock tick -- CLK OUT and the
		// ratios both need it -- but the patterns are not what is playing, so
		// nothing is armed from them.
		if (gridMode) return;
		float stepSec = stepHz > 1e-6 ? (float)(1.0 / stepHz) : 0.f;
		// Swing pushes the odd sixteenths later; the even ones never move, so
		// the pulse on CLK OUT stays where a downbeat should be.
		float sw = (s & 1) ? swing * 0.62f : 0.f;
		for (int v = 0; v < V_COUNT; v++) {
			if (!on[v][s]) continue;
			vel[v] = amp[v][s];
			float delay = (sw + offs[v][s]) * stepSec;
			if (delay <= 0.f) {
				pending[v] = false;
				if (!gate[v]) fired[v] = true;
			}
			else {
				pending[v] = true;
				pendingIn[v] = delay;
			}
		}
	}

	/** CLK OUT: a 1 ms pulse on every grid step, so Kickback can be the clock
	    for whatever else is in the rack rather than only its own. */
	inline bool clockHigh() const { return pulseLeft > 0.f; }
};

}  // namespace kickback

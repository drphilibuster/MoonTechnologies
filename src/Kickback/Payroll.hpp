#pragma once
// ---------------------------------------------------------------------------
// The payroll: Kickback's own clock and pattern engine, so the module is a
// drum machine on its own and a bank of drum voices when it isn't.
//
// The normalling rule is the whole design. A voice whose TRIG jack is empty is
// played by the engine; a voice with something patched into its TRIG is played
// by that and nothing else. So an unpatched Kickback runs a kit, patching one
// TRIG takes that one voice over, and patching all six leaves the engine doing
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

#include <algorithm>
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


/** Each voice's place in the queue and its Euclidean character.

    FILL decides how many hits a voice has, `k`; *which* steps they are is
    decided by ranking (see Payroll::buildVoice), never by Bjorklund alone. The
    old engine asked E(k,16) for the pattern at every k, and that is a density
    control with no memory: E(3,16) is not E(2,16) plus one hit, it is a new
    shape, so the kick at 40% FILL was `x....x....x.....` -- on no beat after the
    first -- while the snare and hat were busier than it was.

    `start` is the FILL at which the voice first speaks, `span` how much of the
    knob it takes to reach `most` hits in a sixteen-step bar. `kLo..kHi` is the
    range of E(kE,L) the SEED picks this voice's *character necklace* from, and
    `rot` that necklace's base rotation. The necklace does not set the count;
    it is a bonus on the ranking, so it decides which of the in-between steps a
    voice reaches for once its anchors are down. `bias` is the voice's
    lay-back, in steps. */
struct Role { float start, span; int most, rot, kLo, kHi; float bias; };
static const Role kRole[V_COUNT] = {
	{ 0.00f, 0.80f,  7,  0, 3, 5, 0.00f },   // KICK    -- in from the first turn of the knob
	{ 0.06f, 0.74f,  6,  2, 3, 5, 0.04f },   // SNARE   -- the backbeat; necklace on the 8ths
	{ 0.03f, 0.94f, 16,  0, 5, 9, 0.02f },   // HAT     -- fills all the way to sixteenths
	{ 0.20f, 0.75f,  5, 10, 3, 5, 0.02f },   // TOM I   -- the toms are part of the kit, not
	{ 0.28f, 0.70f,  5, 13, 3, 5, 0.02f },   // TOM II     a garnish on top of it: at the
	{ 0.36f, 0.62f,  4, 11, 3, 5, 0.02f },   // TOM III    default FILL all three speak
};

//: How many onsets of a voice are pinned: its one strongest step. A voice with any
//: hits at all always has that one, so the kick is always the kick; nothing else is
//: promised, because anything else promised is a rhythm everyone has heard.
static const int kPins[V_COUNT] = { 1, 1, 1, 1, 1, 1 };

//: Each voice's grouping of sixteen at SEED 0, as the lengths of consecutive
//: groups -- an additive meter, the way Aksak and the Balkan and Arab rhythms count
//: Toussaint lists (3+3+2, 2+2+3) rather than the way a drummer in four counts.
//: Every voice counts differently, so the kit is a polyrhythm sitting in one bar:
//: the kick in 3+3+3+3+4, the snare in 3+3+2+3+3+2 turned to start on its own
//: step, the hat in 2+2+3+2+2+3+2.
static const int kPreset[V_COUNT][10] = {
	{ 3, 3, 3, 3, 4, 0 },
	{ 3, 3, 2, 3, 3, 2, 0 },
	{ 2, 2, 3, 2, 2, 3, 2, 0 },
	{ 5, 5, 6, 0 },
	{ 3, 5, 3, 5, 0 },
	{ 4, 3, 4, 5, 0 },
};

/** A grouping of `L` steps: consecutive group lengths summing to L, 2 to 6 long.
    At SEED 0 and sixteen steps it is the voice's preset; otherwise it is composed
    from `key`, mostly threes and twos with the odd four and five, never leaving a
    group of one. Returns the number of groups. */
inline int makeGrouping(int v, int L, uint32_t key, bool useSeed, int* g) {
	int n = 0;
	if (!useSeed && L == kSteps) {
		for (; kPreset[v][n]; n++) g[n] = kPreset[v][n];
		return n;
	}
	static const int parts[8] = { 3, 3, 2, 3, 4, 2, 5, 3 };
	int rem = L;
	uint32_t h = key * 2654435761u + 12345u;
	while (rem > 0 && n < 15) {
		h ^= h << 13; h ^= h >> 17; h ^= h << 5;
		int p = parts[h & 7u];
		if (p >= rem) p = rem;
		else if (rem - p == 1) p = rem;      // never strand a group of one
		g[n++] = p;
		rem -= p;
	}
	return n;
}

/** One voice's spine: what each step of its L-step cycle is worth to it, 0..1.

    The first step of a group is strong, the middle of a long group middling, the
    rest weak, and the groups lose weight as the cycle goes on, with a seeded wobble
    so no two groups are quite equal. Then the whole thing is turned by the voice's
    own rotation, so each voice's strongest step falls somewhere different. The kick
    is not turned; the snare is turned to start on the "and" of two; the others by
    the seed. Hat is lifted so its weak steps still outrank the other voices' nothing
    -- a hat is allowed to be dense -- and toms lean toward the end of the cycle. */
inline void spineFor(int v, int L, uint32_t seed, float* out) {
	int g[16];
	const bool useSeed = seed > 0;
	uint32_t key = hash3(seed * 7u + (uint32_t)v * 101u + (uint32_t)L, (uint32_t)v, 0x51u);
	int n = makeGrouping(v, L, key, useSeed, g);
	float w[kSteps];
	int s = 0;
	for (int j = 0; j < n; j++) {
		int len = g[j];
		float jitter = 0.14f * (hash3f(key + 1u, (uint32_t)j, 0u) - 0.5f);
		float start = j == 0 ? 1.f : 0.88f - 0.05f * (float)(j < 6 ? j : 6) + jitter;
		for (int o = 0; o < len && s < L; o++, s++) {
			float x;
			if (o == 0) x = start;
			else if (len >= 4 && o == len / 2) x = 0.52f + jitter;
			else x = 0.30f - 0.025f * (float)(o < 4 ? o : 4) + 0.5f * jitter;
			w[s] = x < 0.05f ? 0.05f : x;
		}
	}
	int rot = 0;
	if (v == V_SNARE) rot = (3 * L) / 8;
	else if (v >= V_TOM1) rot = (v * 5) % L;
	if (useSeed && v != V_KICK) rot = (int)(hash3(seed, (uint32_t)v, 3u) % (uint32_t)L);
	for (int i = 0; i < L; i++) {
		float x = w[((i - rot) % L + L) % L];
		if (v == V_HAT) x = 0.35f + 0.65f * x;
		else if (v >= V_TOM1) x *= 0.6f + 0.4f * (float)(i + 1) / (float)L;
		out[i] = x;
	}
}

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
	int ratch[V_COUNT][kSteps] = {};     // hits in this step: 1 plain, 2-4 a ratchet
	float offs[V_COUNT][kSteps] = {};   // microtiming, in fractions of a step
	int onsets[V_COUNT] = {};           // the k each voice was asked for, before conditions
	int rotation[V_COUNT] = {};         // how far its Euclidean necklace is turned
	int builtSeed = -1;
	int builtFill = -1;
	int builtHuman = -1;
	int builtShape = -1;
	int builtEvolve = -1;
	int builtLen[V_COUNT] = {};

	// What the tables were last built from, kept so a voice can rebuild itself
	// on its own cycle boundary (EVOLVE) without the module calling build again.
	float curFill = 0.f, curHuman = 0.f, curShape = 0.5f, curEvolve = 0.f;

	// --- polymeter ---------------------------------------------------------------
	// Every voice has its own length and its own position in it. `step` is the
	// grid (CLK OUT, swing, the bar line); `vstep[v]` is where voice v is in its
	// own cycle, and `cycle[v]` how many times it has come round -- the bar
	// counter EVOLVE's conditions and fills are counted in.
	int len[V_COUNT] = { 16, 16, 16, 16, 16, 16 };
	int vstep[V_COUNT] = {};
	uint32_t cycle[V_COUNT] = {};

	// --- what this sample should do -----------------------------------------
	bool fired[V_COUNT] = {};
	float vel[V_COUNT] = {};
	bool clockPulse = false;        // a step boundary happened this sample
	float pulseLeft = 0.f;          // seconds of CLK OUT high remaining

	// Steps that are waiting on their microtiming offset before they fire.
	//: Hits waiting for their moment, per voice: how long until each lands and
	//: how hard. A step's micro-timing, a ratchet's later hits and a hit pulled
	//: *early* from the step after (see advanceInto) all wait here, so a voice can
	//: have several in flight at once.
	static const int kQ = 8;
	float qT[V_COUNT][kQ] = {};
	float qVel[V_COUNT][kQ] = {};
	int qn[V_COUNT] = {};
	//: This voice's next step was armed ahead of time, on the previous boundary,
	//: because its micro-timing puts it before the beat.
	bool early[V_COUNT] = {};
	//: The voice's next pass was already built, for the same reason.
	bool preWrapped[V_COUNT] = {};

	//: Grid mode's per-voice phase, one turn per hit. Free-running against the
	//: step grid at that voice's own ratio, so two voices on coprime divisions
	//: drift through a long pattern rather than repeating every bar.
	double gridPhase[V_COUNT] = {};

	//: Grid mode, and what each voice runs at in it. Set by the caller before
	//: process(), the way `running` is.
	bool gridMode = false;
	//: BURST: the pattern says *when* a voice is live, the ratio says how fast
	//: it repeats while it is. Without it the ratio knobs only mean anything at
	//: FILL 0, where the top of their range is six drones and little else; with
	//: it a fast ratio is a ratchet inside one step and a slow one thins the
	//: pattern across many, which is where the complicated figures come from.
	bool burstMode = false;
	int ratioIndex[V_COUNT] = {};
	//: Which sub-division of the current step a bursting voice last spoke on.
	//: Multiplication is counted off the step's own phase rather than a phase
	//: of its own: a free-running phase fires on its wraps, which fall at 1/R,
	//: 2/R ... 1 of the step, so the last of them lands on the next boundary
	//: and every step comes out one hit long. Reading floor(phase * R) instead
	//: puts them at 0, 1/R ... (R-1)/R, which is where a ratchet belongs, and
	//: cannot drift or double-count however the samples fall.
	int burstSub[V_COUNT] = {};
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
		builtSeed = builtFill = builtHuman = builtShape = builtEvolve = -1;
		for (int v = 0; v < V_COUNT; v++) { len[v] = kSteps; vstep[v] = 0; cycle[v] = 0; builtLen[v] = -1; }
		liveSeed = wantSeed = 0;
		gridMode = false; burstMode = false; humanHeld = 0.f;
		for (int v = 0; v < V_COUNT; v++) {
			ratioIndex[v] = kUnity; gridCount[v] = 0; burstSub[v] = -1;
		}
		for (int v = 0; v < V_COUNT; v++) {
			fired[v] = false; vel[v] = 0.f;
			qn[v] = 0; early[v] = false; preWrapped[v] = false;
			gridPhase[v] = 0.0;
		}
	}

	/** Rebuild the six patterns. Only runs when one of the knobs that shape
	    them has moved to a new quantised value, so it costs nothing while the
	    module is playing.

	    FILL sets every voice's hit count k at once, through its role; k is
	    monotone in FILL. What changed from the first engine is *which* k steps:
	    the voice takes the k best-ranked steps of its spine (see buildVoice), so
	    the set at k is always inside the set at k + 1 -- turning FILL up only
	    adds hits, and always the next most important one -- where the E(k,16)
	    patterns it used to ask for were unrelated to each other from one k to
	    the next. SHAPE brings the Euclidean necklace into the ranking, SEED
	    picks which necklace and where it is turned, LEN sets each cycle's
	    length, EVOLVE sets how much the pattern moves from one pass to the next. */
	//: The seed the pattern playing right now was built from, and the one the
	//: knob is asking for. They differ only between a turn of SEED and the bar
	//: line that acts on it.
	int liveSeed = 0;
	int wantSeed = 0;

	/** What the module calls every sample. SEED is held back to the top of the
	    bar; FILL, SHAPE, EVOLVE and HUMAN are not.

	    A new seed turns every voice's necklace at once, so taking it mid-bar cuts
	    the figure off wherever the knob happened to move and starts another one
	    out of phase with the bar -- which sounds like a mistake rather than a
	    change. FILL only ever adds or removes onsets from the pattern already
	    playing, SHAPE only re-ranks them, and HUMAN is a spread applied at the
	    moment a voice speaks, so those stay live: they are the ones you want to
	    hear yourself moving. */
	void build(float fill, int seed, float human, float shape = 0.5f,
	           float evolve = 0.f, const int* lens = nullptr) {
		wantSeed = seed;
		// Immediately if there is no bar to wait for: nothing has been built
		// yet, or the clock is not running, in which case the next thing the
		// listener hears is step 0 anyway.
		if (!running || builtSeed < 0) liveSeed = seed;
		rebuild(fill, liveSeed, human, shape, evolve, lens);
	}

	/** Apply a seed the knob asked for while the bar was still running. */
	void takeSeed() {
		if (wantSeed == liveSeed) return;
		liveSeed = wantSeed;
		builtSeed = -1;                 // force the rebuild below
		int l[V_COUNT];
		for (int v = 0; v < V_COUNT; v++) l[v] = len[v];
		rebuild(curFill, liveSeed, curHuman, curShape, curEvolve, l);
	}

	void rebuild(float fill, int seed, float human, float shape, float evolve,
	             const int* lens) {
		int qf = (int)(fill * 256.f);
		int qh = (int)(human * 64.f);
		int qs = (int)(shape * 64.f);
		int qe = (int)(evolve * 64.f);
		int ql[V_COUNT];
		bool same = true;
		for (int v = 0; v < V_COUNT; v++) {
			int l = lens ? lens[v] : len[v];
			ql[v] = l < 3 ? 3 : (l > kSteps ? kSteps : l);
			if (ql[v] != builtLen[v]) same = false;
		}
		if (same && qf == builtFill && seed == builtSeed && qh == builtHuman
		    && qs == builtShape && qe == builtEvolve) return;
		builtFill = qf; builtSeed = seed; builtHuman = qh; builtShape = qs;
		builtEvolve = qe;
		curFill = fill; curHuman = human; curShape = shape; curEvolve = evolve;
		for (int v = 0; v < V_COUNT; v++) {
			builtLen[v] = len[v] = ql[v];
			if (vstep[v] >= len[v]) vstep[v] %= len[v];
		}
		for (int v = 0; v < V_COUNT; v++) buildVoice(v);
	}

	/** One voice's table for its current pass through its cycle.

	    Every step of the cycle is scored:

	        spine            what the step is worth to this voice: the kick's
	                         downbeat, the snare's backbeat. Fixed.
	      + SHAPE * 0.6      if the step is an onset of the voice's Euclidean
	                         necklace E(kE, L), turned by its rotation.
	      + a hair           of hash, so equal steps do not always fall to the
	                         lowest index.
	      + EVOLVE noise     seeded per pass, so each time round the cycle
	                         reaches for slightly different in-between steps.

	    and the voice plays the k best. The pinned steps get a bonus no setting
	    can outweigh. Because the necklace is chosen by SEED and does not depend
	    on k, the ranking does not depend on k either, which is what makes the
	    patterns nested.

	    Ghosts are the pattern's tail: a step whose spine is below a third of the
	    downbeat's plays quietly and a few ticks late, and with EVOLVE up some of
	    them are made conditional the way an Elektron trig is -- on every other
	    pass, three in four, one in four, or half the time -- so the loop breathes
	    rather than repeating. Every fourth pass is a fill when EVOLVE is up. */
	void buildVoice(int v) {
		const Role& role = kRole[v];
		const int L = len[v];
		const uint32_t seed = (uint32_t)(builtSeed < 0 ? 0 : builtSeed);
		const uint32_t cy = cycle[v];
		const float ev = curEvolve;

		float spine[kSteps];
		spineFor(v, L, seed, spine);

		// --- how many ---------------------------------------------------------
		float fill = curFill;
		const bool fillPass = v != V_KICK && ev > 0.05f && (cy & 3u) == 3u;
		if (fillPass) fill = std::min(1.f, fill + 0.2f + 0.2f * ev);
		float t = clampf((fill - role.start) / role.span, 0.f, 1.f);
		int k = (int)(t * (float)role.most * (float)L / (float)kSteps + 0.5f);
		if (v == V_KICK && fill > 0.0005f && k < 1) k = 1;
		if (k > L) k = L;
		onsets[v] = k;

		// --- two necklaces, laid over each other ------------------------------
		// Bonus from two Euclidean rhythms of different k at once: where they
		// agree a step is strong, where one of them has it a step is lifted. The
		// interference is what makes the in-between hits feel like a polyrhythm
		// rather than one pattern. With EVOLVE past a half the necklaces change
		// every four passes, and past three quarters they turn as well.
		uint32_t era = ev > 0.5f ? (cy >> 2) : 0u;
		int kE = role.kLo + (int)(hash3(seed + era * 977u, (uint32_t)v, 17u)
		                          % (uint32_t)(role.kHi - role.kLo + 1));
		int kF = 2 + (int)(hash3(seed + era * 977u, (uint32_t)v, 19u) % 6u);
		kE = (int)((float)kE * (float)L / (float)kSteps + 0.5f);
		kF = (int)((float)kF * (float)L / (float)kSteps + 0.5f);
		if (kE < 1) kE = 1;
		if (kF < 1) kF = 1;
		if (kE > L) kE = L;
		if (kF > L) kF = L;
		int rot = role.rot + (int)(hash3(seed, (uint32_t)v, 7u) % (uint32_t)L);
		int rot2 = (int)(hash3(seed, (uint32_t)v, 11u) % (uint32_t)L);
		if (ev > 0.75f) { rot += (int)(cy >> 2); rot2 += (int)(cy >> 3); }
		rot %= L; rot2 %= L;
		rotation[v] = rot;
		bool e1[kSteps], e2[kSteps];
		euclid(kE, L, e1);
		euclid(kF, L, e2);

		// --- pins: the voice's strongest step -------------------------------------
		bool pinned[kSteps] = {};
		float pinBonus[kSteps] = {};
		for (int n = 0; n < kPins[v] && n < L; n++) {
			int best = -1;
			for (int s = 0; s < L; s++)
				if (!pinned[s] && (best < 0 || spine[s] > spine[best])) best = s;
			pinned[best] = true;
			pinBonus[best] = 10.f * (float)(kPins[v] - n);
		}

		// --- rank -------------------------------------------------------------
		float score[kSteps];
		int order[kSteps];
		for (int s = 0; s < L; s++) {
			float neck = 0.6f * (e1[((s - rot) % L + L) % L] ? 1.f : 0.f)
			           + 0.4f * (e2[((s - rot2) % L + L) % L] ? 1.f : 0.f);
			float sc = spine[s] + curShape * 0.8f * neck
			         + 0.03f * hash3f(seed + 13u, (uint32_t)v, (uint32_t)s);
			if (ev > 0.f && !pinned[s])
				sc += ev * 1.4f
				    * (hash3f(seed + 977u + cy * 40503u, (uint32_t)v, (uint32_t)s) - 0.5f);
			sc += pinBonus[s];
			score[s] = sc;
			int i = s;
			while (i > 0 && score[order[i - 1]] < sc) { order[i] = order[i - 1]; i--; }
			order[i] = s;
		}

		// --- conditions on everything that is not an anchor -----------------------
		// Which passes a step plays on, Elektron-style: every other pass, two in
		// three, one in four, half the time. A step whose condition is off this
		// pass hands its place to the next best step rather than leaving a hole, so
		// the density stays where FILL put it and what moves is *which* steps -- the
		// pattern mutates instead of thinning. The more EVOLVE, the more of the
		// pattern is conditional, and none of it is on the fill pass.
		bool blocked[kSteps] = {};
		if (ev > 0.001f && !fillPass) {
			float share = clampf(ev, 0.f, 1.f);
			for (int s = 0; s < L; s++) {
				if (pinned[s]) continue;
				if (hash3f(seed + 51u, (uint32_t)v, (uint32_t)s) >= share) continue;
				bool play;
				switch (hash3(seed + 77u, (uint32_t)v, (uint32_t)s) % 9u) {
					case 0:  play = (cy & 1u) == 0u; break;   // 1:2
					case 1:  play = (cy & 1u) == 1u; break;   // 2:2
					case 2:  play = (cy & 3u) != 0u; break;   // not 1:4
					case 3:  play = (cy & 3u) != 1u; break;   // not 2:4
					case 4:  play = cy % 3u != 0u; break;     // not 1:3
					case 5:  play = cy % 3u != 2u; break;     // not 3:3
					case 6:  play = (cy & 3u) == 2u; break;   // 3:4
					case 7:  play = hash3f(seed + 91u + cy * 131u, (uint32_t)v,
					                       (uint32_t)s) < 0.5f; break;   // 50%
					default: play = true; break;
				}
				if (!play) blocked[s] = true;
			}
		}

		for (int s = 0; s < kSteps; s++) { on[v][s] = false; ratch[v][s] = 1; }
		for (int i = 0, took = 0; i < L && took < k; i++)
			if (!blocked[order[i]]) { on[v][order[i]] = true; took++; }

		// --- ratchets ----------------------------------------------------------
		// A step may be struck two to four times inside itself. Chance rises with
		// EVOLVE and doubles on the fill pass; the kick and snare only double.
		if (ev > 0.05f) {
			float chance = ev * 0.45f * (fillPass ? 2.f : 1.f);
			for (int s = 0; s < L; s++) {
				if (!on[v][s] || pinned[s]) continue;
				if (hash3f(seed + 601u + cy * 17u, (uint32_t)v, (uint32_t)s) >= chance) continue;
				int n = 2 + (int)(hash3(seed + 603u + cy * 19u, (uint32_t)v, (uint32_t)s) % 3u);
				if (v <= V_SNARE && n > 2) n = 2;
				ratch[v][s] = n;
			}
		}

		// --- velocity and feel --------------------------------------------------
		const float human = curHuman;
		// Micro-timing is a rhythmic device, not only a human one: a step may be
		// shifted off the grid by a half, a third or a quarter of a step -- the
		// positions where a triplet, a sextuplet or a thirty-second would fall --
		// so a hit lands where no sixteenth-note grid has one. Elektron's
		// micro-timing does the same, in 1/384ths. A share of the steps is shifted
		// by SEED and does not change; EVOLVE adds more, and moves them each pass.
		static const float kShift[8] = { -0.5f, -0.333f, -0.25f, -0.167f,
		                                 0.167f, 0.25f, 0.333f, 0.5f };
		float shiftChance = 0.22f + 0.45f * ev;
		for (int s = 0; s < kSteps; s++) {
			if (s >= L) { amp[v][s] = 0.f; offs[v][s] = 0.f; continue; }
			float w = spine[s];
			float spread = (hash3f(seed + 101u, (uint32_t)v, (uint32_t)s) - 0.5f) * human * 0.8f;
			amp[v][s] = clampf(0.25f + 0.75f * std::pow(w, 0.9f) + spread, 0.14f, 1.f);

			float o = 0.f;
			if (!pinned[s]) {
				uint32_t salt = ev > 0.3f ? cy : 0u;
				if (hash3f(seed + 301u + salt * 53u, (uint32_t)v, (uint32_t)s) < shiftChance)
					o = kShift[hash3(seed + 311u + salt * 59u, (uint32_t)v, (uint32_t)s) & 7u];
			}
			// HUMAN is the small, unintended kind: a spread of up to a sixth of a
			// step either way, zero at zero.
			float pull = (hash3f(seed + 211u, (uint32_t)v, (uint32_t)s) - 0.5f) * 2.f;
			o += pull * human * 0.16f;
			o += role.bias * std::min(1.f, human * 2.f);
			offs[v][s] = std::floor(o * 384.f + 0.5f) / 384.f;
		}
	}

	/** Put every voice back at the top of its cycle, on its first pass. */
	void restartVoices() {
		for (int v = 0; v < V_COUNT; v++) {
			bool moved = cycle[v] != 0;
			vstep[v] = 0; cycle[v] = 0;
			early[v] = false; preWrapped[v] = false; qn[v] = 0;
			if (moved && curEvolve > 0.001f && builtSeed >= 0) buildVoice(v);
		}
	}

	/** Move the grid to step `s`, taking every voice with it. */
	void setStep(int s) {
		int d = s - step;
		step = s;
		for (int v = 0; v < V_COUNT; v++) early[v] = false;
		for (int v = 0; v < V_COUNT; v++)
			vstep[v] = (((vstep[v] + d) % len[v]) + len[v]) % len[v];
	}

	/** Is a patched clock actually ticking? True from the first edge until
	    the edges stop arriving for a period and a half, so a cable into CLK IN
	    with nothing running down it does not read as a running transport. */
	bool extClockLive() const {
		if (!extSeen) return false;
		float timeout = extPeriod > 1e-4f ? std::max(1.5f * extPeriod, 0.03f) : 1.f;
		return sinceEdge < timeout;
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
			restartVoices();
			for (int v = 0; v < V_COUNT; v++) { qn[v] = 0; gridPhase[v] = 0.0; }
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
				if (pb > 0 && kSteps % pb == 0) setStep((step / pb) * pb);
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
			// The engine runs while a patched clock is ticking; RUN is the
			// switch for the internal one only. When the edges stop the grid
			// holds where it is (resuming on the next edge) and any hit still
			// waiting on its microtiming is dropped rather than fired late.
			if (!extClockLive()) {
				for (int v = 0; v < V_COUNT; v++) qn[v] = 0;
				return;
			}
		}
		else {
			stepHz = (double)bpm / 60.0 * perBeat;
			if (!running) {
				// Held at the top of the bar, so pressing RUN starts on 1.
				step = 0; phase = 0.0;
				restartVoices();
				for (int v = 0; v < V_COUNT; v++) qn[v] = 0;
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
				qn[v] = 0;
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
		else if (burstMode) {
			// --- burst mode ---------------------------------------------------
			// Grid mode gated by the pattern: every voice's ratio clock runs,
			// but a voice only sounds while its own Euclidean step is live.
			//
			// That reading makes both halves of the ratio range worth having.
			// Multiplying, the phase is reset to the beat as the step arms (see
			// advanceInto) so x4 lays four even hits inside that step -- a
			// ratchet. Dividing, the phase is deliberately *not* reset, so /5
			// ticks once every five steps and speaks only when that tick lands
			// on a step the pattern has lit; two voices on coprime divisions
			// then drift through a figure far longer than sixteen steps.
			// At x1 the phase turns once per step, which is a single hit on the
			// beat -- exactly what the module does with BURST off.
			for (int v = 0; v < V_COUNT; v++) {
				qn[v] = 0;
				int idx = ratioIndex[v];
				idx = idx < 0 ? 0 : (idx >= kRatioCount ? kRatioCount - 1 : idx);
				float ratio = kClockRatio[idx];
				bool speak = false;

				if (ratio >= 1.f) {
					// Ratcheting inside the step, counted off the step's phase.
					if (!on[v][vstep[v]]) { burstSub[v] = -1; continue; }
					// Past a quarter of the sample rate a trigger is not a
					// rhythm any more; the same clamp grid mode uses.
					double per = (double)ratio;
					double cap = 0.25 / ((double)dt * (stepHz > 0.0 ? stepHz : 1.0));
					if (per > cap) per = cap;
					int sub = (int)(phase * per);
					if (sub != burstSub[v]) { burstSub[v] = sub; speak = true; }
				}
				else {
					// Dividing spans steps, so this one keeps a phase of its
					// own and the pattern is the gate on it: a tick landing on
					// a step the voice does not play is spent, not saved, which
					// is what keeps a divided voice in step with the bar rather
					// than sliding out of it.
					burstSub[v] = -1;
					double hz = stepHz * (double)ratio;
					if (hz <= 0.0) continue;
					gridPhase[v] += (double)dt * hz;
					int guard = 0;
					while (gridPhase[v] >= 1.0 && guard++ < 4) {
						gridPhase[v] -= 1.0;
						if (on[v][vstep[v]]) speak = true;
					}
				}

				if (!speak) continue;
				float spread = (hash3f((uint32_t)(builtSeed + 101), (uint32_t)v,
				                       gridCount[v] & 15u) - 0.5f) * humanHeld * 0.8f;
				vel[v] = clampf(amp[v][vstep[v]] + spread, 0.14f, 1.f);
				gridCount[v]++;
				if (!gate[v]) fired[v] = true;
			}
		}
		else {
			// --- release anything whose microtiming offset has come due ------
			for (int v = 0; v < V_COUNT; v++) {
				bool spoke = false;
				for (int i = 0; i < qn[v]; ) {
					qT[v][i] -= dt;
					if (qT[v][i] <= 0.f && !spoke) {
						spoke = true;
						if (!gate[v]) { fired[v] = true; vel[v] = qVel[v][i]; }
						qn[v]--;
						qT[v][i] = qT[v][qn[v]]; qVel[v][i] = qVel[v][qn[v]];
						continue;
					}
					i++;
				}
			}
		}

		// --- advance the grid ------------------------------------------------
		if (stepHz <= 0.0) return;
		phase += (double)dt * stepHz;
		while (phase >= 1.0) {
			phase -= 1.0;
			step = (step + 1) % kSteps;
			// Each voice walks its own cycle. One that has come round starts a
			// new pass, and with EVOLVE up a new pass is a new table.
			for (int v = 0; v < V_COUNT; v++) {
				if (++vstep[v] >= len[v]) {
					vstep[v] = 0;
					// advanceInto may have built this pass already, to look
					// ahead into it; if not, build it now.
					if (!preWrapped[v]) { cycle[v]++; buildVoice(v); }
					preWrapped[v] = false;
				}
			}
			// The bar line, and the only place a new seed is allowed in. It
			// runs before the step is armed, so step 0 of the new figure is
			// the first thing the new pattern plays.
			if (step == 0) takeSeed();
			advanceInto(step, swing);
		}
	}

	/** Arm whatever the new step wants, offset by swing and microtiming. */
	void advanceInto(int s, float swing) {
		swingNow = swing;
		clockPulse = true;
		pulseLeft = 0.001f;
		// In grid mode a step boundary is still a clock tick -- CLK OUT and the
		// ratios both need it -- but the patterns are not what is playing, so
		// nothing is armed from them.
		if (gridMode) return;
		// In burst mode the ratio clock does the firing, so nothing is armed
		// with a microtiming offset. A voice multiplying starts its burst on
		// the beat -- the phase is pulled back to zero and the first hit is
		// this one; a voice dividing keeps the phase it had, because the whole
		// point of dividing is to span steps rather than restart on each.
		if (burstMode) {
			// Nothing is armed here: a multiplying voice counts its hits off
			// the step's phase in process(), and a dividing one is running a
			// phase that deliberately spans steps. All this boundary does is
			// clear the sub-division counter so the new step starts on a hit.
			for (int v = 0; v < V_COUNT; v++) { qn[v] = 0; burstSub[v] = -1; }
			return;
		}
		float stepSec = stepHz > 1e-6 ? (float)(1.0 / stepHz) : 0.f;
		for (int v = 0; v < V_COUNT; v++) {
			// This step: unless it was already armed from the step before.
			int vs = vstep[v];
			if (early[v]) early[v] = false;
			else if (on[v][vs]) arm(v, vs, s, stepSec, 0.f, true);

			// The next step, if its micro-timing puts it before its own beat: it
			// has to be armed now, a step ahead, to land early. If the next step
			// begins a new pass, build that pass first so the hit comes from it.
			int nv = vs + 1;
			if (nv >= len[v]) {
				nv = 0;
				if (!preWrapped[v]) {
					cycle[v]++;
					buildVoice(v);
					preWrapped[v] = true;
				}
			}
			if (on[v][nv] && total(v, nv, (s + 1) % kSteps, swing) < 0.f) {
				arm(v, nv, (s + 1) % kSteps, stepSec, stepSec, false);
				early[v] = true;
			}
		}
	}

	float swingNow = 0.f;
	/** Where a step's hit lands against its beat, in steps: swing plus the
	    step's own micro-timing. Negative is early. */
	float total(int v, int vs, int gs, float swing) const {
		return ((gs & 1) ? swing * 0.62f : 0.f) + offs[v][vs];
	}

	/** Put a step's hit, and its ratchet, in the queue. `base` is how long from
	    now the step's beat is: zero for the current step, one step for one armed
	    ahead. `now` says this is the step's own boundary, where an early hit that
	    could not be armed ahead (the first step after a reset) just plays at once. */
	void arm(int v, int vs, int gs, float stepSec, float base, bool now) {
		float t = base + total(v, vs, gs, swingNow) * stepSec;
		if (t < 0.f) t = 0.f;
		float a = amp[v][vs];
		int n = ratch[v][vs];
		if (n < 1) n = 1;
		float gap = stepSec / (float)n;
		for (int i = 0; i < n && qn[v] < kQ; i++) {
			// Each hit of a ratchet is a little softer than the one before.
			float x = a * (i == 0 ? 1.f : std::pow(0.82f, (float)i));
			if (t + gap * (float)i <= 0.f && i == 0) {
				if (!gateNow[v]) { fired[v] = true; vel[v] = x; }
				continue;
			}
			qT[v][qn[v]] = t + gap * (float)i;
			qVel[v][qn[v]] = x;
			qn[v]++;
		}
		(void)now;
	}

	/** CLK OUT: a 1 ms pulse on every grid step, so Kickback can be the clock
	    for whatever else is in the rack rather than only its own. */
	inline bool clockHigh() const { return pulseLeft > 0.f; }
};

}  // namespace kickback

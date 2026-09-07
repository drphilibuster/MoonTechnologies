#pragma once
// Collusion's engine: six phase oscillators that listen to each other, and the
// shift register that turns what they agree on into a melody.
//
// The model is Kuramoto's -- a population of oscillators, each with its own
// natural frequency, each pulled toward the others by a sinusoidal coupling:
//
//     dtheta_i/dt  =  omega_i  +  K * omega_0 * (1/N) sum_j sin(theta_j - theta_i - alpha)
//
// Kuramoto's 1975 result is that this has a phase transition. Below a critical
// coupling Kc the population stays incoherent and every oscillator runs at its
// own rate; above it a macroscopic fraction locks to one collective frequency,
// and the fraction grows continuously from zero as K passes Kc. Kc depends only
// on how widely the natural frequencies are spread, which is why this module
// has exactly two knobs that matter -- COUPLING and SPREAD -- and why the
// transition is audible rather than theoretical.
//
//   Y. Kuramoto, "Self-entrainment of a population of coupled non-linear
//   oscillators", Int. Symposium on Mathematical Problems in Theoretical
//   Physics, Lecture Notes in Physics 39 (Springer, 1975), 420-422.
//   S. H. Strogatz, "From Kuramoto to Crawford: exploring the onset of
//   synchronization in populations of coupled oscillators", Physica D 143
//   (2000), 1-20.
//
// alpha is Sakaguchi's phase lag (H. Sakaguchi and Y. Kuramoto, Prog. Theor.
// Phys. 76 (1986), 576-581): the coupling aims not at the others' phase but a
// little behind it, so the population can never quite settle. It is what buys
// partial order -- clusters, travelling waves, and coexisting locked and
// incoherent groups -- instead of all-or-nothing. The FIREFLY scheme swaps the
// continuous coupling for the pulse coupling of Mirollo and Strogatz,
// "Synchronization of pulse-coupled biological oscillators", SIAM J. Appl.
// Math. 50 (1990), 1645-1662, where nothing happens between fires and a fire
// jumps everyone else's phase at once.
//
// Rack-free on purpose: nothing here includes <rack.hpp>, so tests/Collusion
// compiles the shipping signal path with a host compiler and no SDK. The
// precedent is src/Deduction/Filters.hpp.

#include <cmath>
#include <cstdint>

namespace collusion {

/** Six. Not a parameter: the panel has six lamps and six jacks, and the whole
    point of the module is that you can see each member of the population. */
static const int N_FILERS = 6;

inline float clampf(float x, float lo, float hi) {
	return x < lo ? lo : (x > hi ? hi : x);
}

/** Replaces a non-finite sample with zero. The coupled system is a feedback
    loop with a user-controlled gain; one inf that escapes poisons every phase
    on the next sample and the module goes silent for the rest of the session. */
inline float sane(float x) {
	return std::isfinite(x) ? x : 0.f;
}

/** Forces a phase into [0, 1), after the caller has already added or subtracted
    the one turn it could possibly be out by.

    The second clause is not belt and braces. A phase that steps a hair below
    zero -- which is what repulsive coupling does routinely -- comes back from
    `p += 1.f` as exactly 1.0f, because 1.0f is the nearest float to it. Every
    branch below reads phase as a half-open range, so one sample of exactly 1
    is a filer reported as high and low at once, and a fire that never fired. */
inline float inTurn(float p) {
	return (p >= 0.f && p < 1.f) ? p : 0.f;
}

// --- fast trigonometry -------------------------------------------------------
// The coupling costs four sines per filer per sample, plus two for the mean
// field and two for the waveform -- around forty a sample for the population.
// std::sin at that rate is the module's whole CPU budget, and none of it buys
// anything: the coupling term is a force, not a signal, and a thousandth of a
// radian of error in it is indistinguishable from a thousandth of a turn of the
// EVASION knob.
//
// The parabola 16t(1/2 - |t|) approximates sin(2*pi*t) on [-1/2, 1/2] to about
// 1%; the standard cubic refinement below takes that to ~7e-4, which is a
// hundred times finer than the panel can be set.

/** sin(2*pi*x), for any real x. */
inline float sin2pi(float x) {
	x -= std::floor(x);                       // [0, 1)
	const float t = (x < 0.5f) ? x : x - 1.f; // [-0.5, 0.5)
	const float a = (t < 0.f) ? -t : t;
	const float y = 16.f * t * (0.5f - a);
	const float b = (y < 0.f) ? -y : y;
	return 0.225f * (y * b - y) + y;
}

/** cos(2*pi*x), for any real x. */
inline float cos2pi(float x) {
	return sin2pi(x + 0.25f);
}

// --- who hears whom ----------------------------------------------------------

enum Scheme {
	//! All-to-all: every filer is pulled toward the population's mean phase.
	//! Kuramoto's own wiring, and the only one with a clean phase transition.
	SCHEME_CARTEL  = 0,
	//! A ring: each filer hears only its two neighbours. Local coupling cannot
	//! impose one phase on everybody, so what forms instead is a travelling
	//! wave -- the six lamps chase each other round rather than flashing together.
	SCHEME_RING    = 1,
	//! A one-way cascade: 1 hears nobody, 2 hears 1, 3 hears 2. The top of the
	//! structure is unaffected by anything below it, so filer 1 is a clean LFO
	//! and the other five are a chain of increasingly late, increasingly
	//! distorted copies of it.
	SCHEME_PYRAMID = 2,
	//! Pulse coupling: nothing at all happens between fires, and a fire jumps
	//! every other phase toward (or, for negative coupling, away from) the
	//! firing one. Locks harder and faster than the continuous schemes, and
	//! stays locked -- the rhythmic setting.
	SCHEME_FIREFLY = 3,
	NUM_SCHEMES    = 4,
};

/** How much of the SHAPE knob is usable at this frequency.

    The waveform is a phase-warped cosine, and the warp is a phase-modulation
    index: the sharper the snap, the more harmonics it has. At LFO rates that
    is free. In the audio band it is not, so the ceiling falls with frequency
    and the spike softens back into a sine as the swarm is swept up past a few
    kHz, rather than folding a stack of aliases down over the fundamental.

    Deliberately not a hard band-limit. This is a modulation source pushed into
    the audio range on purpose, and some grit up there is the reason to do it;
    what the ceiling prevents is the shape control becoming a noise control. */
inline float shapeCeiling(float freq, float sampleRate) {
	const float f = (freq < 0.f) ? -freq : freq;
	if (!(f > 1e-6f) || !(sampleRate > 1.f))
		return 0.98f;
	const float harmonics = 0.45f * sampleRate / f;   // how many fit under Nyquist
	return clampf((harmonics - 1.f) / 8.f, 0.f, 0.98f);
}

/** One cycle of the waveform, from a phase in [0, 1).

    `m` in [0, 1) warps the phase so the cycle dwells near its minimum and then
    snaps through the rest -- the shape of a relaxation oscillation, which is
    what van der Pol's equation produces as its own nonlinearity grows, and what
    every RC-and-a-comparator LFO in a DIY case actually puts out. m = 0 is a
    plain cosine.

    This is a warp, not an integration of van der Pol's ODE: the population
    model needs a phase for each member, and an ODE oscillator does not have
    one to give. The family resemblance is in the waveform, and the claim here
    is no stronger than that. */
inline float shapedWave(float phase, float m) {
	static const float INV_TAU = 0.15915494309189535f;   // 1 / (2*pi)
	const float u = phase - m * sin2pi(phase) * INV_TAU;
	return -cos2pi(u);
}

// --- the population ----------------------------------------------------------

/** Everything one process() call needs to be told. Frequencies are in Hz,
    `spread` and `leverage` in octaves, `evasion` in [0, 1]. */
struct Controls {
	float baseFreq   = 1.f;    //!< the swarm's centre rate
	float spread     = 0.3f;   //!< octaves the natural rates fan across, each way
	float shape      = 0.f;    //!< 0 = sine, -> 1 = relaxation spike
	float coupling   = 0.f;    //!< K, bipolar: negative repels, positive locks
	float evasion    = 0.f;    //!< 0..1, scaled to Sakaguchi's alpha
	int   scheme     = SCHEME_CARTEL;
	float leverage   = 0.f;    //!< octaves the ledger takes off the rates
	float ledger     = 0.f;    //!< the ledger's own voltage, normalised to [-1, 1]
	float sampleTime = 1.f / 44100.f;
	float sampleRate = 44100.f;
};

/** The largest phase advance allowed in one sample. Half a cycle is the point
    at which "which way is it going" stops having an answer, so this is well
    under it -- and it is also what keeps a runaway coupling term from throwing
    a phase to infinity instead of merely making it sound bad. */
static const float MAX_STEP = 0.45f;

/** Sakaguchi's alpha at full EVASION, in turns. Just past a quarter turn: far
    enough for the coupling to be mostly reactive (which is where clusters and
    travelling waves live) without reaching the half turn at which attraction
    has simply become repulsion and the knob duplicates COUPLING's left half. */
static const float MAX_ALPHA = 0.4f;

/** The pulse-coupling kick at unit coupling, in turns. */
static const float FIREFLY_KICK = 0.12f;

/** How long PULSE holds a gate, as a fraction of the swarm's own cycle.
    Long enough to merge the fires of a locked population, short enough to
    leave six separate gates when they are spread around the cycle. */
static const float PULSE_TURNS = 0.05f;

struct Swarm {
	float phase[N_FILERS];
	float detune[N_FILERS];      //!< [-1, 1]: this filer's place in the fan
	float wave[N_FILERS];        //!< last shaped output, [-1, 1]
	float freq[N_FILERS];        //!< last natural rate actually used, Hz
	bool  fired[N_FILERS];       //!< wrapped past 1 this sample

	float order     = 0.f;       //!< Kuramoto's r: 0 incoherent, 1 unison
	float consensus = 0.f;       //!< mean of wave[], [-1, 1]
	//! The gate output: a pulse every time any filer fires.
	//!
	//! Held for a fixed fraction of the swarm's own cycle, which is what makes
	//! it report the phase transition instead of merely counting: six fires
	//! spread around an incoherent cycle are six separate gates, and the same
	//! six fires drawn together by the coupling merge into one. Patch it to a
	//! drum and COUPLING is a control that collapses a six-against-one
	//! polyrhythm into a unison hit.
	//!
	//! A fraction of a cycle rather than a fixed millisecond count because what
	//! it has to swallow is a *phase* spread, which scales with the period --
	//! and because the swarm is meant to be swept from a cycle a minute up into
	//! the audio band, where a fixed hold would be longer than the cycle.
	bool  pulse     = false;
	float pulseHold = 0.f;       //!< seconds left on the current gate

	Swarm() {
		fan();
		scatter();
	}

	/** The default fan: the six natural rates evenly spaced across the SPREAD.
	    A uniform frequency distribution is the case Kuramoto's critical
	    coupling is quoted for, so this is the setting in which the panel's
	    "Kc is about 0.9 x SPREAD" rule of thumb is actually true. */
	void fan() {
		for (int i = 0; i < N_FILERS; i++)
			detune[i] = -1.f + 2.f * (float) i / (float) (N_FILERS - 1);
	}

	/** Re-roll the fan. `u` must return uniform values in [0, 1); the module
	    hands it Rack's RNG, the tests a fixed sequence. */
	template <typename Uniform>
	void deal(Uniform u) {
		for (int i = 0; i < N_FILERS; i++)
			detune[i] = clampf(2.f * u() - 1.f, -1.f, 1.f);
	}

	/** Every filer to phase zero -- what the SYNC jack does. The population
	    starts in perfect agreement and you watch it come apart, which is the
	    same transition as COUPLING run backwards and is much easier to hear. */
	void align() {
		for (int i = 0; i < N_FILERS; i++) {
			phase[i] = 0.f;
			wave[i] = -1.f;
			freq[i] = 0.f;
			fired[i] = false;
		}
		order = 1.f;
		consensus = -1.f;
		pulse = false;
		pulseHold = 0.f;
	}

	/** Phases spread evenly round the circle: maximally incoherent, r = 0.
	    The state a fresh module comes up in, so that COUPLING has somewhere to
	    take it. */
	void scatter() {
		align();
		for (int i = 0; i < N_FILERS; i++) {
			phase[i] = (float) i / (float) N_FILERS;
			wave[i] = shapedWave(phase[i], 0.f);
		}
		order = 0.f;
		consensus = 0.f;
	}

	/** One sample. */
	void process(const Controls& c) {
		const float alpha = clampf(c.evasion, 0.f, 1.f) * MAX_ALPHA;
		const float k = sane(c.coupling);
		const float f0 = (c.baseFreq > 0.f && std::isfinite(c.baseFreq)) ? c.baseFreq : 0.f;
		const int scheme = (c.scheme < 0 || c.scheme >= NUM_SCHEMES) ? SCHEME_CARTEL : c.scheme;

		// The mean field, in one pass: sum the unit phasors. r falls out of its
		// length, and -- because sin(A-B) expands -- so does every filer's
		// coupling term, without an atan2 to find the mean phase first.
		float sx = 0.f, cx = 0.f;
		for (int i = 0; i < N_FILERS; i++) {
			sx += sin2pi(phase[i]);
			cx += cos2pi(phase[i]);
		}
		order = clampf(std::sqrt(sx * sx + cx * cx) / (float) N_FILERS, 0.f, 1.f);

		const float nyquist = 0.45f * c.sampleRate;

		for (int i = 0; i < N_FILERS; i++) {
			float term = 0.f;
			switch (scheme) {
				case SCHEME_CARTEL: {
					// (1/N) sum_j sin(2pi(theta_j - theta_i) - 2pi alpha)
					//   = (Sx cos(2pi(theta_i + alpha)) - Cx sin(2pi(theta_i + alpha))) / N
					const float b = phase[i] + alpha;
					term = (sx * cos2pi(b) - cx * sin2pi(b)) / (float) N_FILERS;
					break;
				}
				case SCHEME_RING: {
					const int lo = (i + N_FILERS - 1) % N_FILERS;
					const int hi = (i + 1) % N_FILERS;
					term = 0.5f * (sin2pi(phase[lo] - phase[i] - alpha)
					             + sin2pi(phase[hi] - phase[i] - alpha));
					break;
				}
				case SCHEME_PYRAMID:
					term = (i == 0) ? 0.f : sin2pi(phase[i - 1] - phase[i] - alpha);
					break;
				case SCHEME_FIREFLY:
				default:
					term = 0.f;      // applied at fire events, below
					break;
			}

			// The ledger's feedback is deliberately not common mode: it takes a
			// different amount off each filer, weighted by where that filer sits
			// in the fan, so a step of the register moves the population through
			// the phase transition rather than just changing its tempo. This is
			// the Benjolin's per-oscillator rungler attenuators, ganged to one
			// knob -- and it is what stops a locked swarm from staying locked.
			const float oct = c.spread * detune[i]
			                + c.leverage * c.ledger * (0.5f + detune[i]);
			float f = f0 * std::exp2(clampf(oct, -12.f, 12.f));
			f = clampf(sane(f), 0.f, nyquist);
			freq[i] = f;

			// Kuramoto's equation, with K measured in units of the swarm's own
			// centre rate. That is what makes the coupling mean the same thing
			// at 0.05 Hz and at 4 kHz: RANGE moves the whole phase diagram
			// without moving where on it you are standing.
			float dphi = (f + k * f0 * term) * c.sampleTime;
			dphi = clampf(sane(dphi), -MAX_STEP, MAX_STEP);

			float p = phase[i] + dphi;
			bool wrapped = false;
			if (p >= 1.f) { p -= 1.f; wrapped = true; }
			else if (p < 0.f) { p += 1.f; }   // running backwards is not a fire
			phase[i] = inTurn(p);
			fired[i] = wrapped;
		}

		// Pulse coupling: a fire jumps every other phase. -sin(2pi theta) is the
		// phase response curve that pulls toward the firing oscillator -- it
		// retards anyone in the first half of their cycle and advances anyone in
		// the second -- so positive coupling gathers the population at one phase
		// and negative coupling drives it apart.
		if (scheme == SCHEME_FIREFLY && k != 0.f) {
			for (int j = 0; j < N_FILERS; j++) {
				if (!fired[j])
					continue;
				for (int i = 0; i < N_FILERS; i++) {
					if (i == j)
						continue;
					float d = -k * FIREFLY_KICK * sin2pi(phase[i]);
					d = clampf(sane(d), -0.25f, 0.25f);
					float p = phase[i] + d;
					// A kick over the top is a fire in its own right -- absorption,
					// in Mirollo and Strogatz's terms. It does not kick back: one
					// round per sample, so a cascade cannot run away.
					if (p >= 1.f) { p -= 1.f; fired[i] = true; }
					else if (p < 0.f) { p += 1.f; }
					phase[i] = inTurn(p);
				}
			}
		}

		// Waveforms, the mean wave, and the gate.
		float sum = 0.f;
		bool anyFired = false;
		for (int i = 0; i < N_FILERS; i++) {
			const float m = clampf(c.shape, 0.f, 1.f) * shapeCeiling(freq[i], c.sampleRate);
			wave[i] = clampf(sane(shapedWave(phase[i], m)), -1.f, 1.f);
			sum += wave[i];
			anyFired = anyFired || fired[i];
		}
		consensus = sum / (float) N_FILERS;

		if (anyFired)
			pulseHold = (f0 > 1e-4f) ? (PULSE_TURNS / f0) : PULSE_TURNS;
		else
			pulseHold -= c.sampleTime;
		pulse = pulseHold > 0.f;
	}
};

// --- the ledger --------------------------------------------------------------

/** The rungler: a short shift register whose output bits are read as a number.

    Rob Hordijk's Benjolin clocks an eight-bit register from one oscillator and
    feeds it from the comparator of another, then reads three of its bits
    through a resistor ladder -- a "stepped havoc wave" that is fed back into
    both oscillators' frequencies. Music Thing Modular's Turing Machine adds the
    control that makes it playable: how likely a bit is to be rewritten rather
    than recirculated, so the same pattern can be sealed into a loop, left to
    mutate slowly, or thrown away every step.

    This is both of those, with one difference. The bit that gets written is not
    a comparator on one oscillator but on the whole population's mean field, so
    what the register fills with depends on how much the swarm agrees: incoherent
    below the critical coupling, and a short repeating figure above it. The
    melody is the phase transition, read out one bit at a time.

    Deterministic on purpose -- the caller decides whether a step mutates, so
    the same sequence of decisions always gives the same sequence of bits, and
    the tests do not need a random number generator. */
struct Ledger {
	//! The eight register lengths TERM steps through.
	static const int NUM_TERMS = 8;
	static int termLength(int i) {
		static const int TERMS[NUM_TERMS] = {2, 3, 4, 5, 6, 8, 12, 16};
		return TERMS[(i < 0) ? 0 : ((i >= NUM_TERMS) ? NUM_TERMS - 1 : i)];
	}

	//! Seeded with a pattern that is neither all ones nor all zeros, so a
	//! sealed loop (AUDIT at zero) has something to say before it is ever fed.
	uint32_t bits = 0xB5u;
	int length = 8;
	//! How many of the newest bits the ladder reads. Three is the Benjolin's
	//! eight levels; more is smoother and less like a melody.
	int width = 3;

	void setLength(int n) {
		length = (n < 2) ? 2 : ((n > 16) ? 16 : n);
	}

	void setWidth(int n) {
		width = (n < 1) ? 1 : ((n > 8) ? 8 : n);
	}

	uint32_t mask() const {
		return (length >= 32) ? 0xFFFFFFFFu : ((1u << length) - 1u);
	}

	/** One step. `mutate` says this bit is written from the swarm rather than
	    recirculated; `dataBit` is what the swarm currently says. */
	void clock(bool dataBit, bool mutate) {
		const uint32_t last = (bits >> (length - 1)) & 1u;
		const uint32_t in = mutate ? (dataBit ? 1u : 0u) : last;
		bits = ((bits << 1) | in) & mask();
	}

	/** The ladder's reading, in [-1, 1]. */
	float value() const {
		const int w = (width > length) ? length : width;
		const uint32_t top = (1u << w) - 1u;
		const uint32_t v = bits & top;
		return (float) v / (float) top * 2.f - 1.f;
	}

	void reset() {
		bits = 0xB5u;
	}
};

} // namespace collusion

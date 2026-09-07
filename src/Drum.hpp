#pragma once
// ---------------------------------------------------------------------------
// The physical vocabulary the struck-percussion voices are built from --
// Kickback's drum bank and Toll's bell, which are the same physics at different
// scales. It lives at src/ rather than inside either of them because a struck
// membrane and a struck bar want the same contact pulse, the same click, the
// same attack layer and the same tension term; only the mode ratios differ.
//
// Voices.hpp used to strike a single two-pole resonator with a one-sample
// impulse and call that a drum. It is not one, and the reason is in the
// literature: a struck membrane is a *bank* of inharmonic modes whose relative
// levels depend on where the stick lands, excited by a contact force of finite
// duration, whose pitch falls as the strike's energy leaves it, with a dense
// high-frequency attack layer riding on top that is separately audible. A
// single mode with no attack layer and no glide is a plucked bass note, which
// is exactly what it sounded like.
//
// Sources, in the order they bear on the code below:
//
//   Kirby & Sandler, "Advanced Fourier Decomposition for Realistic Drum
//   Synthesis", DAFx-20 -- membrane modal frequencies from Bessel zeros (their
//   eq. 1); mode amplitudes set by strike position; tension-modulation pitch
//   glide; and the attack component as a dense set of fast-decaying partials
//   between roughly 1 and 8 kHz. Their listening test found the modelled
//   fundamental indistinguishable from a real tom (49.6% in ABX), while
//   removing everything above 2 kHz was caught 98.4% of the time -- so the
//   attack layer is not a garnish, it is most of what a listener identifies.
//
//   Bilbao, "Time domain simulation and sound synthesis for the snare drum",
//   JASA 131(1), 2012 -- the raised-cosine contact force (his eq. 20), whose
//   duration shortens and amplitude grows with strike velocity; the Berger
//   averaged-nonlinearity term behind the pitch glide, "quite important
//   perceptually in the case of certain drums, such as toms"; and the one-sided
//   power-law snare/head collision (his eq. 23).
//
//   Karplus & Strong, "Digital Synthesis of Plucked-String and Drum Timbres",
//   CMJ 7(2), 1983 -- the probabilistic drum recurrence, its blend factor b and
//   its stretch factor S.
//
//   Shier et al., "Differentiable Modelling of Percussive Audio with Transient
//   and Spectral Synthesis", Forum Acusticum 2023 -- transients modelled as a
//   third signal component alongside sines and noise, rather than left to fall
//   out of them. Their onset error on kick drums drops by a fifth the moment a
//   transient path exists at all.
//
// Everything here is pure DSP: no Rack types, no allocation, no I/O.
// ---------------------------------------------------------------------------

#include <cmath>
#include <cstdint>

#include "DspCache.hpp"

namespace kickback {

static const float kPi = 3.14159265358979f;
static const float kTwoPi = 6.28318530717959f;

/** Exponential knob mapping: 0..1 -> [lo, hi], log-spaced. */
inline float expMap(float v, float lo, float hi) {
	return lo * std::pow(hi / lo, v);
}

/** Cheap tanh, exact at +-1 past |x| = 3 (the (27+x^2)/(27+9x^2) Pade form),
    so a hard clamp past there is continuous with what it clamps. */
inline float ftanh(float x) {
	if (x >  3.f) return  1.f;
	if (x < -3.f) return -1.f;
	float x2 = x * x;
	return x * (27.f + x2) / (27.f + 9.f * x2);
}

/** A single-ended BC547/BC549 common-emitter stage: soft into the rail one
    way, harder and lower the other. Every one of the donor circuits has at
    least one such stage between its RC network and the output jack. */
inline float transistorClip(float x) {
	return x >= 0.f ? ftanh(x * 0.8f) * 1.25f : ftanh(x * 1.6f) * 0.625f;
}

/** TPT one-pole; `lp`/`hp` advance the state, so call exactly one per sample. */
struct OnePole {
	float s = 0.f;
	void reset() { s = 0.f; }
	inline float lp(float x, float G) {
		float v = (x - s) * G;
		float y = v + s;
		s = y + v;
		return y;
	}
	inline float hp(float x, float G) { return x - lp(x, G); }
};

/** The TPT one-pole's coefficient for a corner at `hz`. */
inline float poleG(float hz, float fs) {
	float t = std::tan(kPi * std::fmin(std::fmax(hz, 1.f), fs * 0.48f) / fs);
	return t / (1.f + t);
}

/** Output DC blocker, a one-pole high-pass around 8 Hz. Every voice ends in
    one: several of the source circuits couple through an electrolytic and a
    biased transistor, and a strike-driven envelope multiplying a signal is a
    reliable way to leave a little DC on the table. */
struct DcBlock {
	OnePole p;
	float G = 0.f;
	void setRate(float fs) { G = poleG(8.f, fs); }
	void reset() { p.reset(); }
	inline float process(float x) { return p.hp(x, G); }
};

/** xorshift32. Deterministic per-voice seed so nine voices strike from
    independent streams rather than one shared generator's phase. */
struct Noise {
	uint32_t x;
	uint32_t seed0;
	explicit Noise(uint32_t seed) : x(seed ? seed : 0x9e3779b9u), seed0(x) {}
	/** Back to where this stream started. A module reset has to be a reset:
	    two Kickbacks reset and struck identically must produce identical
	    audio, or nothing downstream of a noise source can be compared. */
	void reset() { x = seed0; }
	inline float next() {
		x ^= x << 13; x ^= x >> 17; x ^= x << 5;
		return (float)(int32_t)x * (1.f / 2147483648.f);   // -1 .. 1
	}
	/** A raw word, for the single random bit the Karplus-Strong drum wants. */
	inline uint32_t bits() {
		x ^= x << 13; x ^= x >> 17; x ^= x << 5;
		return x;
	}
};

/** The gain that keeps a noise source's power spectral *density* constant as
    the sample rate moves, rather than its per-sample variance.

    A PRNG gives every sample the same variance, so it spreads a fixed amount
    of power over the whole band and its density per hertz halves each time the
    rate doubles. Whether that matters depends entirely on what the noise meets
    next. A filter with a corner written in hertz -- a lowpass, a bandpass --
    keeps a fixed slice of the band, so it passes half the power and the voice
    comes out five decibels quieter at 192 kHz than at 44.1. A highpass, or one
    clamped by Nyquist, keeps a slice that grows with the rate, and there the
    plain constant-variance stream is already right and this correction makes it
    worse. Measured both ways on every voice: applied only where the noise ends
    in a fixed-frequency lowpass. */
inline float noisePsdGain(float fs) { return std::sqrt(fs / 48000.f); }

/** A decaying exponential struck to `vel`, reaching 1/1000 in t60 seconds. */
struct Decay {
	float env = 0.f;
	mt::Cache rC;
	void reset() { env = 0.f; rC.clear(); }
	inline void strike(float vel) { env = vel; }
	/** Key on t60*fs so the sample rate cannot go stale behind the cache. */
	inline float process(float t60, float fs) {
		float r = rC.get(std::fmax(t60, 0.002f) * fs,
			[](float k) { return std::exp(-6.9077553f / k); });   // ln(1/1000)
		env *= r;
		return env;
	}
};


// ---------------------------------------------------------------------------
// The strike
// ---------------------------------------------------------------------------

/** The contact force of a stick on a head: Bilbao's eq. 20, a raised cosine
    of duration T0, where "F0 increases, and T0 decreases as strike velocity
    increases."

    This is the single most important thing missing from the old voices, which
    struck their resonator with one sample. A one-sample impulse is spectrally
    flat, which sounds like the right answer and is not: it means the strike
    has no *shape*, so nothing distinguishes a felt beater from a wooden stick,
    and nothing gets brighter when you hit harder. A finite contact time is a
    lowpass on the excitation -- a long, soft contact simply cannot put energy
    into a mode whose period is shorter than the contact -- so `hard` and `vel`
    between them set how far up the mode bank the strike reaches.

    The pulse is normalised to unit area, so hardness moves the spectral tilt
    without moving how much low-frequency drive the mode bank receives. Without
    that, winding hardness up would just turn the drum down. */
struct StrikePulse {
	float phase = 1.f;      // >= 1 means idle
	float inc = 0.f;
	float amp = 0.f;
	float vel = 0.f;
	float t0 = 0.f;         // this strike's contact time, seconds
	float edgeOut = 0.f;    // the click, from the last next()

	void reset() { phase = 1.f; inc = 0.f; amp = 0.f; vel = 0.f; t0 = 0.f; edgeOut = 0.f; }

	/** `hard` 0..1: 3 ms of felt at 0, a third of a millisecond of wood at 1. */
	inline void trigger(float v, float hard, float fs) {
		vel = v;
		t0 = (0.0032f - 0.00285f * hard) / (0.6f + 0.4f * std::fmin(v, 2.f));
		float n = std::fmax(t0 * fs, 2.f);
		inc = 1.f / n;
		// Unit area: the raised cosine averages 0.5 over its support, so a
		// pulse n samples long integrates to n/2 before scaling.
		amp = v * 2.f / n;
		phase = 0.f;
	}

	inline bool active() const { return phase < 1.f; }

	/** The contact force, and alongside it the *click*.

	    A mode bank is a set of narrow resonators well below a kilohertz, so
	    almost none of the contact force's own bandwidth reaches the output
	    through it -- the bank filters the strike out and leaves a tone that
	    starts abruptly, which is what a bass oscillator with a fast envelope
	    sounds like. The stick is heard directly as well as through the head,
	    and that direct path has to be summed in as its own signal rather than
	    hoped for as a by-product. This is the transient component Shier et al.
	    argue must be modelled in parallel with the sines, not fallen out of
	    them.

	    d/dt of a raised cosine of width T0 is one cycle of a sine at 1/T0, so
	    the click's pitch tracks contact time for free: a 3 ms felt beater
	    thumps at 330 Hz, a third-millisecond stick cracks at 2.9 kHz. Scaled
	    to peak at the strike velocity, so it is a click and not a rounding
	    error. */
	inline float next() {
		if (phase >= 1.f) { edgeOut = 0.f; return 0.f; }
		float y = 0.5f * (1.f - std::cos(kTwoPi * phase)) * amp;
		edgeOut = std::sin(kTwoPi * phase) * vel;
		phase += inc;
		return y;
	}

	inline float click() const { return edgeOut; }
};


// ---------------------------------------------------------------------------
// The membrane
// ---------------------------------------------------------------------------

//: Modal frequencies of an ideal circular membrane are f(m,n) = x(m,n)/(2*pi*r)
//: * sqrt(T/sigma) (Kirby & Sandler eq. 1), so their *ratios* are the ratios of
//: the Bessel zeros and depend on nothing else. These are the first eight,
//: (m,n) = (0,1) (1,1) (2,1) (0,2) (3,1) (1,2) (4,1) (2,2).
static const int kModes = 8;
static const float kZero[kModes] = {
	2.404826f, 3.831706f, 5.135622f, 5.520078f,
	6.380162f, 7.015587f, 7.588342f, 8.417244f,
};
static const int kOrder[kModes] = { 0, 1, 2, 0, 3, 1, 4, 2 };
//: kZero[i] / kZero[0] -- what the bank actually tunes to.
static const float kRatio[kModes] = {
	1.000000f, 1.593368f, 2.135545f, 2.295417f,
	2.652855f, 2.917298f, 3.155466f, 3.500156f,
};

/** J_m(z) by its ascending series, in double, for m <= 4 and z <= ~9.
    Only ever called when the strike-position knob moves, so its cost is
    irrelevant; what matters is that it is right, since it decides which modes
    a strike wakes up. */
inline float besselJ(int m, double z) {
	double half = 0.5 * z;
	// term_0 = (z/2)^m / m!
	double term = 1.0;
	for (int i = 1; i <= m; i++) term *= half / i;
	double sum = term;
	double h2 = half * half;
	for (int k = 1; k < 40; k++) {
		term *= -h2 / ((double)k * (double)(k + m));
		sum += term;
		if (std::fabs(term) < 1e-12 * (std::fabs(sum) + 1e-12)) break;
	}
	return (float)sum;
}

/** A bank of `kModes` two-pole resonators tuned to a circular membrane's
    inharmonic mode ratios, struck by a shared contact force.

    Three things the single resonator it replaces could not do:

    * **Strike position.** The displacement of mode (m,n) at radius rho is
      J_m(x(m,n) * rho), so a dead-centre strike (rho = 0) wakes only the
      circular m = 0 modes -- one boomy partial and its octave-ish neighbour --
      while a strike toward the rim brings in the radial modes and the sound
      goes hollow and complex. This is Kirby & Sandler's framework step 2, and
      it is most of the difference between "a bass note" and "a drum".

    * **Per-mode decay.** High modes shed energy faster (radiation and internal
      loss both rise with frequency), so each mode's t60 is scaled down by its
      own ratio. Without this a mode bank turns into a chord that hangs.

    * **Tension modulation.** Berger's averaged nonlinear term makes the modal
      frequencies rise with the membrane's mean-square displacement, which is
      where every real drum's downward pitch glide comes from. Driving it from
      the bank's *own* envelope rather than from a fixed sweep is what makes a
      hard hit glide further than a soft one, for free.

    The bend factor is quantised to 1/1024 before it reaches the coefficients,
    so a glide recomputes eight cosines a few hundred times over its length
    rather than at every sample; 1/1024 in frequency is under two cents. */
struct ModalBank {
	float y1[kModes] = {}, y2[kModes] = {};
	float gain[kModes] = {};
	float a1[kModes] = {}, a2[kModes] = {};
	//: sin(w) per mode. A two-pole resonator struck by a unit impulse rings at
	//: 1/sin(w), so without this the bank is eighteen decibels louder at the
	//: bottom of a tuning sweep than at the top -- TUNE would be a volume
	//: control with a pitch side-effect. Folding sin(w) into the drive makes
	//: the ring amplitude depend on the strike and the mode's own Bessel
	//: weight, and on nothing else.
	float drive[kModes] = {};

	mt::Cache posC;         // strike position -> the gain table

	// The coefficient tables depend on four things at once. mt::Cache holds one
	// float key, and folding four values into one float is where that idiom
	// stops being safe: the bend term moves in 1/1024 steps while f0 moves in
	// whole hertz, so any sum of them can collide -- two different tunings
	// hashing to the same key, and eight resonators quietly running the wrong
	// coefficients. Four exact compares cost about as much as one and cannot.
	float lastF0 = -1.f, lastT60 = -1.f, lastDamp = -1.f, lastBend = -1.f;
	float fs = 44100.f;

	void setRate(float fs_) { fs = fs_; clearCoef(); }
	void clearCoef() { lastF0 = lastT60 = lastDamp = lastBend = -1.f; }

	void reset() {
		for (int i = 0; i < kModes; i++) { y1[i] = y2[i] = 0.f; }
		posC.clear(); clearCoef();
	}

	/** `pos` 0..1: dead centre to hard against the rim. */
	inline void setPosition(float pos) {
		posC.get(pos, [this](float p) {
			// Never exactly 0: a strike of literally zero extent is a limit,
			// not a sound, and it would leave seven of eight modes silent.
			double rho = 0.06 + 0.82 * (double)p;
			float norm = 0.f;
			for (int i = 0; i < kModes; i++) {
				float g = besselJ(kOrder[i], kZero[i] * rho);
				// The sign is a phase, and a mode bank struck all at once is
				// audibly different depending on it; keep it.
				gain[i] = g;
				norm += std::fabs(g);
			}
			// Hold the total drive steady across the sweep, so moving the knob
			// changes colour rather than level.
			float k = norm > 1e-6f ? 3.4f / norm : 0.f;
			for (int i = 0; i < kModes; i++) gain[i] *= k;
			return p;
		});
	}

	/** `damp` 0..1: how much faster the upper modes die than the fundamental.
	    `bend` is the instantaneous tension-modulation factor, >= 1. */
	inline void setTuning(float f0, float t60, float damp, float bend) {
		// The bend term is quantised to 1/1024 -- under two cents -- so a glide
		// recomputes eight cosines a few hundred times over its length rather
		// than at every sample. Everything else here moves at knob rate, so the
		// common case is four compares and nothing else.
		float bq = std::floor(bend * 1024.f) * (1.f / 1024.f);
		if (f0 == lastF0 && t60 == lastT60 && damp == lastDamp && bq == lastBend) return;
		lastF0 = f0; lastT60 = t60; lastDamp = damp; lastBend = bq;
		{
			float b = bq;
			for (int i = 0; i < kModes; i++) {
				float f = f0 * kRatio[i] * b;
				if (f > fs * 0.47f) { a1[i] = a2[i] = drive[i] = 0.f; continue; }
				// t60 shortens with mode ratio; damp says by how much.
				float t = t60 / std::pow(kRatio[i], 0.35f + 1.15f * damp);
				float r = std::exp(-6.9077553f / (std::fmax(t, 0.002f) * fs));
				float w = kTwoPi * f / fs;
				a1[i] = 2.f * r * std::cos(w);
				a2[i] = -r * r;
				drive[i] = std::sin(w);
			}
		}
	}

	/** One sample. `x` is the contact force; every mode is struck by all of it,
	    scaled by how much that mode is displaced where the stick landed. */
	inline float process(float x) {
		float out = 0.f;
		for (int i = 0; i < kModes; i++) {
			float y = a1[i] * y1[i] + a2[i] * y2[i] + x * gain[i] * drive[i];
			y2[i] = y1[i]; y1[i] = y;
			out += y;
		}
		return out;
	}

	/** The fundamental's state, which is what the tension term reads. */
	inline float fundamental() const { return y1[0]; }
};

/** The membrane's own envelope, feeding the tension-modulation term.

    Berger's nonlinearity makes the modal frequencies rise with mean-square
    displacement, so the glide has to be driven by how loud the drum currently
    is, not by a separate envelope: that is why a rimshot dives and a ghost note
    does not. A fast-attack / decay-following envelope on the bank's own output
    is the cheap form of the same feedback. */
struct Tension {
	float ms1 = 0.f, ms2 = 0.f;     // two poles of mean-square smoothing
	mt::Cache gC;

	void reset() { ms1 = ms2 = 0.f; gC.clear(); }

	/** Returns the frequency multiplier, >= 1. `depth` 0..1 is the BEND knob.

	    Berger's term is the *mean square* displacement, which is an average by
	    definition -- and following the instantaneous peak instead is not just
	    a simplification, it is unstable. With an instant attack the envelope
	    re-peaks on every cycle of the waveform, so the resonator's frequency
	    ends up modulated at the resonator's own frequency; that is a parametric
	    amplifier, and at full BEND a tom pumped itself faster than its decay
	    could empty it. It rang at three and a half volts six seconds after a
	    single strike, with DECAY anywhere past about 0.9.

	    Two poles at 8 Hz put the ripple 46 dB under the average, which is well
	    below anything that can pump a mode, and 120 ms is longer than the
	    period of any mode this bank can hold. */
	inline float process(float x, float depth, float fs) {
		float g = gC.get(fs, [](float k) { return 1.f - std::exp(-kTwoPi * 8.f / k); });
		float sq = x * x;
		ms1 += (sq - ms1) * g;
		ms2 += (ms1 - ms2) * g;
		// A sine of amplitude A has mean square A^2/2, so the factor of two
		// keeps the same depth per knob turn the peak envelope used to give.
		float e = std::fmin(ms2 * 2.f, 1.6f);
		return 1.f + depth * depth * 0.9f * e;
	}
};


// ---------------------------------------------------------------------------
// The attack layer
// ---------------------------------------------------------------------------

/** The stochastic attack component: everything the mode bank cannot say.

    Kirby & Sandler decompose a tom into a deterministic sustain (the modes)
    and a stochastic attack that "dominates the higher frequencies", roughly 1
    to 8 kHz, made of many rapidly-decaying partials whose density and height
    both fall with frequency. Removing that band from a real sample was caught
    by listeners 98.4% of the time -- it is the part that says *struck*.

    Rather than sum a hundred short partials, this runs one noise burst through
    a resonant bandpass whose centre and Q track strike hardness, plus two fast
    high resonators for the stick's own ring. The envelope is deliberately
    shorter than any mode's: 4 ms soft, under 2 ms hard. */
struct Attack {
	Noise noise;
	float b1 = 0.f, b2 = 0.f;                      // bandpass state
	float r1a = 0.f, r1b = 0.f, r2a = 0.f, r2b = 0.f;
	float env = 0.f;
	float fs = 44100.f;

	mt::Cache bpC, rC, decC;

	explicit Attack(uint32_t seed) : noise(seed) {}

	void setRate(float fs_) { fs = fs_; bpC.clear(); rC.clear(); decC.clear(); }
	void reset() {
		noise.reset();
		b1 = b2 = r1a = r1b = r2a = r2b = 0.f; env = 0.f;
		bpC.clear(); rC.clear(); decC.clear();
	}

	inline void strike(float vel) { env = vel; }

	float ba1 = 0.f, ba2 = 0.f, bg = 0.f;
	float c1a1 = 0.f, c1a2 = 0.f, c2a1 = 0.f, c2a2 = 0.f;
	float c1g = 0.f, c2g = 0.f;

	/** `hard` 0..1 moves the band up and tightens the ring; `tilt` 0..1 scales
	    the whole layer, for voices that want less of it. */
	inline float process(float hard, float tilt) {
		if (env <= 1e-6f) { env = 0.f; return 0.f; }
		float dec = decC.get(hard, [this](float h) {
			return std::exp(-6.9077553f / (std::fmax((0.0040f - 0.0022f * h), 0.0008f) * fs));
		});
		env *= dec;

		// The band: 1.4 kHz of thud at hard = 0, 5.5 kHz of crack at hard = 1.
		bpC.get(hard, [this](float h) {
			float f = std::fmin(1400.f * std::pow(3.9f, h), fs * 0.44f);
			float q = 0.8f + 1.6f * h;
			float w = kTwoPi * f / fs;
			float r = std::exp(-w / (2.f * q));
			ba1 = 2.f * r * std::cos(w);
			ba2 = -r * r;
			bg = (1.f - r) * 0.9f;
			return h;
		});
		rC.get(hard, [this](float h) {
			// Two short rings standing in for the dense upper peaks; their
			// centres spread apart as the stick gets harder. Radius from a ring
			// time in seconds and drive through sin(w), for the same reason the
			// snare's wires are: written as constants, both the length and the
			// loudness of the stick would follow the engine's sample rate.
			float f1 = std::fmin(3200.f * (1.f + 0.55f * h), fs * 0.44f);
			float f2 = std::fmin(6100.f * (1.f + 0.35f * h), fs * 0.45f);
			float rr = std::exp(-6.9077553f / (0.0011f * fs));
			float w1c = kTwoPi * f1 / fs, w2c = kTwoPi * f2 / fs;
			c1a1 = 2.f * rr * std::cos(w1c); c1a2 = -rr * rr;
			c2a1 = 2.f * rr * std::cos(w2c); c2a2 = -rr * rr;
			c1g = std::sin(w1c) * 0.85f;
			c2g = std::sin(w2c) * 0.65f;
			return h;
		});

		float n = noise.next() * env;
		float b = ba1 * b1 + ba2 * b2 + n * bg;
		b2 = b1; b1 = b;
		float k1 = c1a1 * r1a + c1a2 * r1b + n * c1g;
		r1b = r1a; r1a = k1;
		float k2 = c2a1 * r2a + c2a2 * r2b + n * c2g;
		r2b = r2a; r2a = k2;

		return (b + (k1 + k2) * (0.25f + 0.75f * hard)) * tilt;
	}
};


// ---------------------------------------------------------------------------
// The snares
// ---------------------------------------------------------------------------

/** Snare wires against the underside of the head.

    Bilbao's eq. 23 gives the collision force on the ith wire as
    K * [m - w]+ ^ alpha -- one-sided, so a wire pushes back only while it is
    actually in contact, and superlinear, so the force grows faster than the
    intrusion. Two consequences matter musically and neither survives the
    "crossfade some noise in" approach the old snare took: the buzz only
    appears once the head swings past the wires, so ghost notes stay clean and
    hard hits sizzle; and once excited the wires ring on their own, so the
    rattle outlasts the shell.

    The spatial sum over wires is collapsed to one contact term, which is the
    same simplification Bilbao himself offers as the "piston model": the
    rectified, expanded excess drives a small bank of lightly-damped high
    resonators standing in for the wire set, plus a noise term for the
    randomisation of motion that contact produces. */
struct SnareBed {
	static const int kWires = 4;
	float w1[kWires] = {}, w2[kWires] = {};
	float wa1[kWires] = {}, wa2[kWires] = {};
	float wdrive[kWires] = {};
	Noise noise;
	OnePole hp, fSmooth;
	float fs = 44100.f;
	mt::Cache coefC, hpG, fG;

	SnareBed() : noise(0x51A7E5u) {}

	void setRate(float fs_) { fs = fs_; coefC.clear(); }
	void reset() {
		noise.reset();
		for (int i = 0; i < kWires; i++) { w1[i] = w2[i] = 0.f; }
		hp.reset(); fSmooth.reset(); coefC.clear(); hpG.clear(); fG.clear();
	}

	/** `tension` 0..1: slack wires lie close and buzz on everything, tight
	    wires sit off the head and answer only a real hit. */
	inline float process(float head, float tension) {
		// setRate() clears this, so `fs` cannot go stale behind the key.
		coefC.get(tension, [this](float t) {
			// Four wire resonances, spread and brightened as they tighten.
			//
			// Both the pole radius and the drive are computed from the rate,
			// not written as constants. A fixed radius is a fixed decay *per
			// sample*, so the rattle would run four times shorter at 192 kHz
			// than at 44.1.
			//
			// The drive needs a different normalisation from the mode banks',
			// and this is the trap: sin(w) makes a resonator *struck by an
			// impulse* ring to a rate-independent height, which is what a mode
			// bank gets from the strike pulse. These wires are not struck, they
			// are leaned on -- the collision force pushes for as long as the
			// head is past them -- and a continuously driven resonator's gain
			// at resonance is g / ((1-r) * 2 sin w), so sin(w) alone leaves the
			// bed proportional to the sample rate (2.7x over 44.1 to 192 kHz)
			// while folding in the whole of (1-r) overshoots the other way
			// (1.8x down).
			//
			// Neither closed form is right because the drive is neither: the
			// collision is a train of bursts of *fixed duration in seconds*,
			// so it sits between an impulse and a steady tone. The exponent
			// below was found by sweeping it and measuring -- the drift is flat
			// to 1.2x at 0.65 and rises on both sides -- and that is the honest
			// description of it: an empirical constant for a case where the
			// analysis gives two answers and the truth is in between.
			static const float base[kWires] = { 1900.f, 3100.f, 4700.f, 6900.f };
			static const float ring[kWires] = { 0.105f, 0.088f, 0.074f, 0.062f };
			for (int i = 0; i < kWires; i++) {
				float f = std::fmin(base[i] * (0.85f + 0.4f * t), fs * 0.45f);
				float w = kTwoPi * f / fs;
				float r = std::exp(-6.9077553f / (ring[i] * fs));
				wa1[i] = 2.f * r * std::cos(w); wa2[i] = -r * r;
				wdrive[i] = std::pow(1.f - r, 0.65f) * std::sin(w) * (0.5f + 0.5f / (i + 1));
			}
			return t;
		});

		// The one-sided, superlinear contact. thresh falls as the wires
		// tighten toward the head, so tension raises both how easily contact
		// happens and how hard it is when it does.
		float thresh = 0.42f * (1.f - 0.92f * tension);
		float d = std::fabs(head) - thresh;
		float f = 0.f;
		if (d > 0.f) {
			// alpha = 2: superlinear without a pow() in the audio path. The
			// stiffness is deliberately modest -- Bilbao wants K large so the
			// collision approaches a rigid one, but at audio rates a large K
			// makes the wires switch on like a gate the moment the threshold
			// is crossed, and the velocity response goes from nothing to
			// everything over a third of the knob.
			f = d * d * (1.1f + 3.2f * tension);
			// Contact randomises the wires' motion, which is where the hiss
			// in a real snare comes from -- it is not an added noise source.
			f *= 0.55f + 0.45f * noise.next();
		}

		// Band-limit the contact force before it drives anything. The
		// threshold crossing is a hard edge in the discrete signal, and where
		// that edge falls between two samples changes with the rate -- which
		// showed up as the bed's level wandering non-monotonically from rate to
		// rate rather than following any scaling law. A real contact is not
		// instantaneous either, so a pole at 7 kHz is the honest fix as well as
		// the one that works.
		f = fSmooth.lp(f, fG.get(fs, [](float k) { return poleG(7000.f, k); }));

		float out = 0.f;
		for (int i = 0; i < kWires; i++) {
			float y = wa1[i] * w1[i] + wa2[i] * w2[i] + f * wdrive[i];
			w2[i] = w1[i]; w1[i] = y;
			out += y;
		}
		// Scaled to sit *under* the body that drives it. The sin(w) drive above
		// makes each wire's ring rate-independent but leaves it small in
		// absolute terms, and the compensation for that was first set against
		// a voice that has since moved: left at 62 the bed came out four
		// hundred times louder than the ring it is supposed to garnish, which
		// is not a bell with something loose against it, it is the loose thing
		// on its own.
		float g = hpG.get(fs, [](float k) { return poleG(900.f, k); });
		return hp.hp(out * 9.6f, g);
	}
};


// ---------------------------------------------------------------------------
// Karplus-Strong
// ---------------------------------------------------------------------------

/** The 1983 drum recurrence, verbatim, plus its two published parameters.

        y[t] = +1/2 (y[t-p] + y[t-p-1])   with probability b
             = -1/2 (y[t-p] + y[t-p-1])   with probability 1 - b

    b is the blend factor. b = 1 is the plucked string; b = 1/2 is "drumlike";
    b = 0 negates the whole signal every p + 1/2 samples, dropping the pitch an
    octave and leaving only odd harmonics -- what the paper calls a "plucked
    bottle" up high and "harplike" down low. At b = 1/2 the wavetable length
    stops setting pitch and sets decay time instead: the paper's own reading is
    that large p is "the effect of a snare drum" and small p "a brushed
    tom-tom", "allowing smooth transition from one drum sound to another."

    The stretch factor S is the second recurrence: with probability 1/S do the
    average, otherwise pass the delayed sample through unchanged. It multiplies
    every overtone's decay by roughly S, and the paper notes that for drums
    "increasing S increases the snare sound". */
struct KsDrum {
	static const int kMax = 4096;
	float buf[kMax] = {};
	int idx = 0;
	int p = 400;
	Noise noise;

	KsDrum() : noise(0xD00Du) {}

	void reset() {
		noise.reset();
		for (int i = 0; i < kMax; i++) buf[i] = 0.f;
		idx = 0;
	}

	/** Fill the table. The paper: a constant load "gives some buildup before
	    the decay", a random load "gives maximum amplitude initially". The
	    excitation here is the donor circuit's own noise, scaled by velocity. */
	inline void pluck(float vel, int period, float colour) {
		p = period < 2 ? 2 : (period > kMax - 2 ? kMax - 2 : period);
		float s = 0.f;
		for (int i = 0; i < p + 1; i++) {
			// `colour` leans the load from pure noise toward a constant, which
			// is the paper's own knob on how much buildup the strike has.
			float n = noise.next();
			s += (n - s) * (1.f - colour * 0.92f);
			buf[i] = (n * (1.f - colour) + s * colour) * vel;
		}
		// Nothing past p is ever read -- idx wraps within [0, p] -- so clearing
		// the rest is four thousand dead writes inside one audio sample.
		idx = 0;
	}

	/** `blend` 0..1 is b; `stretch` >= 1 is S. */
	inline float process(float blend, float stretch) {
		int i0 = idx;
		int i1 = idx + 1; if (i1 > p) i1 -= (p + 1);
		float a = buf[i0], b = buf[i1];

		uint32_t r = noise.bits();
		float u = (float)(r >> 8) * (1.f / 16777216.f);      // 0..1
		float v = (float)((r << 8) >> 8) * (1.f / 16777216.f);

		float y;
		if (stretch > 1.f && v >= 1.f / stretch) {
			y = a;                                   // decay stretching: hold
		}
		else {
			y = 0.5f * (a + b);
			if (u >= blend) y = -y;                  // the blend factor
		}
		buf[i0] = y;
		idx = i1;
		return y;
	}
};


// ---------------------------------------------------------------------------
// Oscillators
// ---------------------------------------------------------------------------

/** Band-limited square, bipolar (+-1), via polyBLEP. Used both as a plain
    oscillator (SMURF's starved astable) and as one factor of an XOR product
    (BELL, HAT): XOR of two square *bits* is exactly the product of their
    bipolar encodings, since {0,1}-XOR(a,b) = 1 iff the signs disagree. */
struct SquareOsc {
	float phase = 0.f;
	void reset() { phase = 0.f; }
	static inline float blep(float t, float dt) {
		if (t < dt)  { float x = t / dt; return x + x - x * x - 1.f; }
		if (t > 1.f - dt) { float x = (t - 1.f) / dt; return x * x + x + x + 1.f; }
		return 0.f;
	}
	inline float process(float freq, float fs) {
		float dt = std::fmin(freq / fs, 0.45f);
		phase += dt;
		if (phase >= 1.f) phase -= 1.f;
		float v = phase < 0.5f ? 1.f : -1.f;
		v += blep(phase, dt);
		float t2 = phase + 0.5f; if (t2 >= 1.f) t2 -= 1.f;
		v -= blep(t2, dt);
		return v;
	}
};

/** A slow one-pole follower standing in for a vactrol's LDR: photoresistors
    lag the LED that drives them by tens of milliseconds, which is what makes
    a vactrol-filtered noise decay sound smoothed rather than gated. */
struct Vactrol {
	float s = 0.f;
	mt::Cache gC;
	void reset() { s = 0.f; gC.clear(); }
	inline float process(float x, float riseHz, float fs) {
		float g = gC.get(riseHz / fs,
			[](float k) { return 1.f - std::exp(-kTwoPi * k); });
		s += (x - s) * g;
		return s;
	}
};

}  // namespace kickback

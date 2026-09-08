#pragma once
// ---------------------------------------------------------------------------
// Toll -- the struck-metal voice that came out of Kickback.
//
// BaSnaHi's snare stage is a resonant pair shocked by a trigger edge, with a
// second layer buzzing against it. Modelled as a membrane it read as a struck
// metal pipe rather than as a snare, which is a fine thing to be and a bad
// snare, so it left the drum bank and got the room to be what it was: a modal
// percussion voice with the parameters that decide what kind of object is being
// hit brought out onto the panel.
//
// Everything structural is shared with the drums -- src/Drum.hpp has the
// contact pulse, the click, the attack layer, the tension term and the wire bed,
// and the papers behind them. What is different here is the mode *bank*: where
// a drum is always a circular membrane, this one chooses what it is made of.
//
//   MEMBRANE   the Bessel-zero ratios of an ideal circular head -- the drum
//   BAR        a free-free bar: 1, 2.756, 5.404, 8.933 ... the squares of the
//              odd-ish roots of the beam equation, which is a glockenspiel or
//              a marimba bar before its underside is carved
//   BELL       a tuned church bell's own partials -- hum, prime, tierce,
//              quint, nominal and the rest. The tierce at 1.2 is the minor
//              third that makes a bell sound like a bell and not like a pipe
//   HARMONIC   1, 2, 3, 4 ... not an object at all, but the thing a resonator
//              bank does that no real struck object does, and the reason to
//              have a mode bank on a panel rather than buried in a drum
//
// SPREAD then bends whichever set is chosen away from itself: partial n moves
// to n^(1+s), the standard inharmonicity stretch, so a harmonic bank slides
// into bell-like territory and a bell stretches into something that was never
// cast.
// ---------------------------------------------------------------------------

#include "../Drum.hpp"

namespace toll {

using namespace kickback;   // the shared vocabulary: StrikePulse, Attack, ...

inline float clampf(float x, float lo, float hi) {
	return x < lo ? lo : (x > hi ? hi : x);
}

inline float transpose(float hz, float volts) {
	return hz * std::exp2f(clampf(volts, -6.f, 6.f));
}

//: Sixteen partials, twice a drum's eight. A bell's character is in how far up
//: its series you can still hear something, and the cost is a handful of
//: multiply-adds.
static const int kParts = 16;
static const int kSets = 4;

//: The four objects. Ratios to the fundamental; the first is always 1.
static const float kSet[kSets][kParts] = {
	// MEMBRANE -- Bessel zeros x(m,n)/x(0,1), the circular head.
	{ 1.000f, 1.593f, 2.136f, 2.295f, 2.653f, 2.917f, 3.155f, 3.500f,
	  3.598f, 3.652f, 4.060f, 4.132f, 4.230f, 4.601f, 4.832f, 5.000f },
	// BAR -- free-free flexural modes, proportional to the squares of
	// 3.011, 5, 7, 9 ... over the first. Metallophone territory.
	{ 1.000f, 2.756f, 5.404f, 8.933f, 13.34f, 18.64f, 24.82f, 31.87f,
	  39.81f, 48.63f, 58.33f, 68.91f, 80.37f, 92.72f, 105.9f, 120.0f },
	// BELL -- a tuned bell's named partials: hum, prime, tierce, quint,
	// nominal, deciem, undeciem, duodeciem, and the upper work above them.
	{ 1.000f, 2.000f, 2.400f, 3.000f, 4.000f, 5.000f, 5.333f, 6.000f,
	  6.667f, 8.000f, 8.945f, 10.00f, 10.67f, 12.00f, 13.33f, 16.00f },
	// HARMONIC -- not an object, a resonator bank.
	{ 1.000f, 2.000f, 3.000f, 4.000f, 5.000f, 6.000f, 7.000f, 8.000f,
	  9.000f, 10.00f, 11.00f, 12.00f, 13.00f, 14.00f, 15.00f, 16.00f },
};

//: Which Bessel order each MEMBRANE partial belongs to, for the strike-position
//: weighting. The other sets are one-dimensional objects -- a bar and a bell
//: have no radial modes -- so they use the simple positional weighting below.
static const int kOrder16[kParts] = { 0, 1, 2, 0, 3, 1, 4, 2, 0, 5, 3, 1, 6, 4, 2, 0 };
static const float kZero16[kParts] = {
	2.405f, 3.832f, 5.136f, 5.520f, 6.380f, 7.016f, 7.588f, 8.417f,
	8.654f, 8.771f, 9.761f, 9.936f, 10.17f, 11.06f, 11.62f, 11.79f,
};


/** The mode bank. Sixteen two-pole resonators over a chosen ratio set, struck
    by a contact pulse, with a strike-position weighting and a tension term. */
struct Bank {
	float y1[kParts] = {}, y2[kParts] = {};
	float gain[kParts] = {};
	float a1[kParts] = {}, a2[kParts] = {}, drive[kParts] = {};
	float fs = 44100.f;

	mt::Cache posC;
	// Four exact compares rather than one summed cache key: the bend term moves
	// in 1/1024 steps while f0 moves in whole hertz, and any sum of them can
	// collide -- two tunings hashing to one key, sixteen resonators quietly
	// running the wrong coefficients. See src/Drum.hpp, which learned this.
	float lastF0 = -1.f, lastT60 = -1.f, lastDamp = -1.f;
	float lastBend = -1.f, lastSpread = -1.f;
	int lastSet = -1;

	void setRate(float fs_) { fs = fs_; clearCoef(); }
	void clearCoef() { lastF0 = lastT60 = lastDamp = lastBend = lastSpread = -1.f; lastSet = -1; }
	void reset() {
		for (int i = 0; i < kParts; i++) { y1[i] = y2[i] = 0.f; }
		posC.clear(); clearCoef();
	}

	/** `pos` 0..1: dead centre to hard against the edge.

	    On a membrane this is physics -- mode (m,n)'s displacement at radius rho
	    is J_m(x(m,n) rho), so a central strike wakes only the circular modes.
	    A bar or a bell has no radial modes to wake, so there the weighting is
	    the simpler one every struck object shares: hit it at a node and that
	    partial does not sound. Partial n has nodes at multiples of 1/n, so
	    |sin(n pi rho)| is the weight, which is the same rule a plucked string
	    obeys and audibly the right one here. */
	inline void setPosition(float pos, int set) {
		posC.get(pos + (float)set * 8.f, [this, set](float k) {
			float p = k - (float)set * 8.f;
			double rho = 0.05 + 0.85 * (double)p;
			float norm = 0.f;
			for (int i = 0; i < kParts; i++) {
				float g;
				if (set == 0) g = besselJ(kOrder16[i], kZero16[i] * rho);
				else          g = std::sin((float)(i + 1) * kPi * (float)rho);
				// Upper partials are quieter on every real object; without
				// this a sixteen-mode bank is a burst of white noise.
				g /= (1.f + 0.22f * i);
				gain[i] = g;
				norm += std::fabs(g);
			}
			float s = norm > 1e-6f ? 3.2f / norm : 0.f;
			for (int i = 0; i < kParts; i++) gain[i] *= s;
			return k;
		});
	}

	/** `spread` 0..1 stretches the set: partial n -> n^(1+s). */
	inline void setTuning(float f0, float t60, float damp, float bend,
	                      float spread, int set) {
		float bq = std::floor(bend * 1024.f) * (1.f / 1024.f);
		float sq = std::floor(spread * 512.f) * (1.f / 512.f);
		if (f0 == lastF0 && t60 == lastT60 && damp == lastDamp
		    && bq == lastBend && sq == lastSpread && set == lastSet) return;
		lastF0 = f0; lastT60 = t60; lastDamp = damp;
		lastBend = bq; lastSpread = sq; lastSet = set;

		const float* ratios = kSet[set < 0 ? 0 : (set >= kSets ? kSets - 1 : set)];
		for (int i = 0; i < kParts; i++) {
			float r = ratios[i];
			// n^(1+s), written as an exponential so a stretch of zero is
			// exactly the printed ratio rather than nearly it.
			if (sq > 0.f && r > 1.f) r = std::exp(std::log(r) * (1.f + sq * 1.4f));
			float f = f0 * r * bq;
			if (f > fs * 0.47f || f < 1.f) { a1[i] = a2[i] = drive[i] = 0.f; continue; }
			// Upper partials die faster; DAMP says how much faster. At 0 a
			// bell rings out whole, at 1 only the fundamental survives -- which
			// is the difference between bronze and lead.
			float t = t60 / std::pow(r, 0.25f + 1.35f * damp);
			float rr = std::exp(-6.9077553f / (std::fmax(t, 0.002f) * fs));
			float w = kTwoPi * f / fs;
			a1[i] = 2.f * rr * std::cos(w);
			a2[i] = -rr * rr;
			// sin(w), so a struck resonator rings to the same height whatever
			// it is tuned to -- otherwise TUNE is a volume control.
			drive[i] = std::sin(w);
		}
	}

	inline float process(float x) {
		float out = 0.f;
		for (int i = 0; i < kParts; i++) {
			float y = a1[i] * y1[i] + a2[i] * y2[i] + x * gain[i] * drive[i];
			y2[i] = y1[i]; y1[i] = y;
			out += y;
		}
		return out;
	}

	inline float fundamental() const { return y1[0]; }
};


/** The voice. One strike in, one signal out. */
/** The softest a strike can be and still be a strike. */
static const float kVelFloor = 0.05f;

struct Toll {
	Bank bank;
	Tension tension;
	SnareBed bed;
	StrikePulse strike;
	Attack attack;
	DcBlock dc;
	float fs = 44100.f;

	mt::Cache freqC, t60C;

	Toll() : attack(0x70112u) {}

	void setRate(float fs_) {
		fs = fs_; dc.setRate(fs_); bank.setRate(fs_); bed.setRate(fs_); attack.setRate(fs_);
	}
	void reset() {
		bank.reset(); tension.reset(); bed.reset(); strike.reset();
		attack.reset(); dc.reset(); freqC.clear(); t60C.clear();
	}

	/** How hard it is hit, from a control voltage.

	    Velocity was always in the voice -- a harder strike is a *shorter*
	    contact, which reaches further up the partial bank and drives the loose
	    layer past its collision, so it changes the timbre and not just the
	    level -- but nothing on the panel could set it, and it ran pinned at
	    full force.

	    0 V does not mean silence. A velocity of exactly zero is a strike that
	    never lands, so a trigger arriving while some modulation happens to be
	    resting at zero would do nothing at all and read as a broken patch;
	    the floor makes that a ghost note instead, which is also what it
	    sounds like to barely catch a bell. */
	static float velocityFrom(float volts) {
		float v = volts * 0.1f;
		if (!(v > kVelFloor))            // and this catches a NaN on the jack
			v = kVelFloor;
		if (v > 1.f)
			v = 1.f;
		return v;
	}

	/** All controls 0..1 except `set` (which ratio table) and `volts` (V/oct).

	    `hard` is the mallet: three milliseconds of felt at 0, a third of a
	    millisecond of wood at 1. It decides how far up the bank the strike
	    reaches, which on sixteen partials is most of the timbre.
	    `buzz` is the loose layer -- Bilbao's one-sided collision -- which only
	    speaks once the body swings past it, so a soft strike is a clean ring
	    and a hard one clatters. `choke` damps whatever is ringing. */
	inline float process(bool hit, float vel, int set, float tune, float volts,
	                     float decay, float damp, float pos, float hard,
	                     float spread, float bend, float buzz, bool choke) {
		float f0 = transpose(freqC.get(tune,
			[](float k) { return expMap(k, 27.5f, 1760.f); }), volts);
		float t60 = t60C.get(decay, [](float k) { return expMap(k, 0.03f, 12.f); });
		// The hand on the bell. Not a mute -- a damped object still rings, just
		// briefly -- so CHOKE shortens the ring rather than gating the output,
		// which is what a palm on bronze actually does and what a gated one
		// conspicuously does not.
		if (choke) t60 = std::fmin(t60, 0.035f);

		if (hit) {
			strike.trigger(vel, hard, fs);
			attack.strike(vel);
		}
		float x = strike.next();

		int s = set < 0 ? 0 : (set >= kSets ? kSets - 1 : set);
		bank.setPosition(pos, s);
		float b = tension.process(bank.fundamental(), bend, fs);
		bank.setTuning(f0, t60, damp, b, spread, s);
		float body = bank.process(x);

		float y = body;
		if (buzz > 0.001f) y += bed.process(body, buzz) * 0.9f;
		y += strike.click() * (0.4f + 1.2f * hard);
		y += attack.process(0.25f + 0.7f * hard, 1.2f);
		return dc.process(transistorClip(y * 0.16f) * 2.70f) * 5.f;
	}
};

}  // namespace toll

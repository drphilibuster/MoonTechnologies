#pragma once
// The eight percussion circuits behind Kickback, one struct each, plus the
// shared DSP vocabulary they are built from. Pure DSP: no Rack types, no
// allocation, no I/O -- Kickback.cpp owns params/ports and calls in here once
// a sample. Every voice reads a 0..1 "velocity" at strike time (ACCENT folded
// in by the caller) and writes an audio-rate output normalised so 1.0 is 1 V,
// i.e. the module's own ±5 V convention is ±5.0 in these units.
//
// Sources, all from Modular in a Week's Day 9 folder -- see docs/Kickback.md
// for the full attribution and per-voice notes on what was kept and what was
// approximated:
//   BaSnaHi.pdf                 kristian.borgstedt      -> KICK, SNARE, HAT
//   SmurfDrum_BassDrumish.jpg   Tiny Dazzler Electronics -> SMURF
//   TomTomTom.pdf               Kristian Blasol          -> TOM
//   XORbell.pdf                 Kristian Blasol / Elliot Williams -> BELL
//   Percussive Noise Voice.pdf  Kristian Blasol / A_Magic_Pulsewave -> NOISE
//   Tiny Dazzler Schematic.png  Tiny Dazzler Electronics -> DAZZLER

#include <cmath>
#include <cstdint>

#include "../DspCache.hpp"

namespace kickback {

static const float kPi = 3.14159265358979f;

/** Exponential knob mapping: 0..1 -> [lo, hi], log-spaced. */
inline float expMap(float v, float lo, float hi) {
	return lo * std::pow(hi / lo, v);
}

// ---------------------------------------------------------------------------
// Shared vocabulary
// ---------------------------------------------------------------------------

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

/** Output DC blocker, a one-pole high-pass around 8 Hz. Every voice ends in
    one: several of the source circuits couple through an electrolytic and a
    biased transistor, and a strike-driven envelope multiplying a signal is a
    reliable way to leave a little DC on the table. */
struct DcBlock {
	OnePole p;
	float G = 0.f;
	void setRate(float fs) {
		float g = std::tan(kPi * 8.f / fs);
		G = g / (1.f + g);
	}
	void reset() { p.reset(); }
	inline float process(float x) { return p.hp(x, G); }
};

/** Cheap tanh, exact at +-1 past |x| = 3 (the (27+x^2)/(27+9x^2) Pade form),
    so a hard clamp past there is continuous with what it clamps. */
inline float ftanh(float x) {
	if (x >  3.f) return  1.f;
	if (x < -3.f) return -1.f;
	float x2 = x * x;
	return x * (27.f + x2) / (27.f + 9.f * x2);
}

/** A single-ended BC547/BC549 common-emitter stage: soft into the rail one
    way, harder and lower the other. Every one of these circuits has at least
    one such stage between the RC network and the output jack. */
inline float transistorClip(float x) {
	return x >= 0.f ? ftanh(x * 0.8f) * 1.25f : ftanh(x * 1.6f) * 0.625f;
}

/** xorshift32. Deterministic per-voice seed so eight voices strike from
    independent streams rather than one shared generator's phase. */
struct Noise {
	uint32_t x;
	explicit Noise(uint32_t seed) : x(seed ? seed : 0x9e3779b9u) {}
	inline float next() {
		x ^= x << 13; x ^= x >> 17; x ^= x << 5;
		return (float)(int32_t)x * (1.f / 2147483648.f);   // -1 .. 1
	}
};

/** A two-pole resonant "ping": y[n] = a1 y[n-1] + a2 y[n-2] + x[n], pole
    radius r and angle w. Struck with an impulse this rings at `freq` and
    decays to 1/1000 of its start in `t60` seconds -- exactly the twin-T /
    bridged-T behaviour of BaSnaHi's bassdrum stage and the TomTomTom filters:
    a passive RC ring shocked by an edge, with no forcing after the shock. */
struct Modal {
	float y1 = 0.f, y2 = 0.f;
	// Both coefficients are functions of knobs and the sample rate only, so
	// they hold still for millions of samples at a time; see DspCache.hpp.
	mt::Cache rC, cosC;
	void reset() { y1 = y2 = 0.f; rC.clear(); cosC.clear(); }
	inline float process(float x, float freq, float t60, float fs) {
		freq = std::fmin(freq, fs * 0.45f);
		float r = rC.get(std::fmax(t60, 0.005f) * fs,
			[](float k) { return std::exp(-6.9077553f / k); });   // ln(1/1000)
		float cosw = cosC.get(freq / fs,
			[](float k) { return std::cos(2.f * kPi * k); });
		float a1 = 2.f * r * cosw;
		float a2 = -r * r;
		float y = a1 * y1 + a2 * y2 + x;
		y2 = y1; y1 = y;
		return y;
	}
};

/** Band-limited square, bipolar (+-1), via polyBLEP. Used both as a plain
    oscillator (SMURF's starved astable) and as one factor of an XOR product
    (BELL): XOR of two square *bits* is exactly the product of their bipolar
    encodings, since {0,1}-XOR(a,b) = 1 iff the signs disagree. */
struct SquareOsc {
	float phase = 0.f;
	void reset() { phase = 0.f; }
	static inline float blep(float t, float dt) {
		if (t < dt)  { float x = t / dt; return x + x - x * x - 1.f; }
		if (t > 1.f - dt) { float x = (t - 1.f) / dt; return x * x + x + x + 1.f; }
		return 0.f;
	}
	inline float process(float freq, float fs) {
		float dt = freq / fs;
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
	inline float process(float x, float riseHz, float fs) {
		float g = gC.get(riseHz / fs,
			[](float k) { return 1.f - std::exp(-2.f * kPi * k); });
		s += (x - s) * g;
		return s;
	}
};


// ---------------------------------------------------------------------------
// KICK -- BaSnaHi's bassdrum stage (Q1, R1-R8, C1-C5). A diode-coupled trig
// charges the base network and shocks the transistor's RC feedback pair into
// ringing; the pair is a single resonant partial, R2 with the pitch-setting
// arm and C1/C3 across it. DRIVE stands in for the transistor's own gain
// saturating harder as the strike gets louder, which the original gets for
// free from running one active device flat out.
// ---------------------------------------------------------------------------
struct Kick {
	Modal body;
	DcBlock dc;

	mt::Cache freqC, t60C;

	void setRate(float fs) { dc.setRate(fs); }
	void reset() { body.reset(); dc.reset(); freqC.clear(); t60C.clear(); }

	/** `pitch`/`decay`/`drive` 0..1 knobs. */
	inline float process(bool strike, float vel, float pitch, float decay, float drive, float fs) {
		float freq = freqC.get(pitch, [](float k) { return expMap(k, 35.f, 220.f); });
		float t60 = t60C.get(decay, [](float k) { return expMap(k, 0.06f, 1.6f); });
		float y = body.process(strike ? vel * freq * 0.25f : 0.f, freq, t60, fs);
		y = transistorClip(y * (0.6f + drive * 2.4f)) * (1.f + drive);
		return dc.process(y) * 5.f;
	}
};

// ---------------------------------------------------------------------------
// SNARE -- BaSnaHi's snare stage (Q2's resonant pair, same topology as KICK
// at a higher pitch) summed with the avalanche-noise cascade (Q3-Q6, D3/D4):
// two forward-biased base-emitter junctions run in positive feedback make a
// chaotic, hissy "Snare noise?" tap. SNAP crossfades tone into noise, since
// that is what the original's second output pair (P4 vs P6) lets you pick.
// ---------------------------------------------------------------------------
struct Snare {
	Modal body;
	Noise noise;
	OnePole noiseHp;
	DcBlock dc;

	Snare() : noise(0xC0FFEEu) {}
	void setRate(float fs) { dc.setRate(fs); noiseG = std::tan(kPi * 1200.f / fs) / (1.f + std::tan(kPi * 1200.f / fs)); }
	void reset() { body.reset(); noiseHp.reset(); dc.reset(); }

	float noiseG = 0.f;

	mt::Cache freqC, toneC, nrC;

	inline float process(bool strike, float vel, float pitch, float decay, float snap, float fs) {
		float freq = freqC.get(pitch, [](float k) { return expMap(k, 150.f, 420.f); });
		float t60Tone = toneC.get(decay, [](float k) { return expMap(k, 0.03f, 0.5f); });
		float t60Noise = t60Tone * 0.7f;
		float tone = body.process(strike ? vel * freq * 0.22f : 0.f, freq, t60Tone, fs);

		if (strike) noiseEnvState = vel;
		float nr = nrC.get(std::fmax(t60Noise, 0.005f) * fs,
			[](float k) { return std::exp(-1.f / k); });
		noiseEnvState *= nr;

		float raw = noise.next();
		float hp = raw - noiseHp.lp(raw, noiseG);
		float hiss = hp * noiseEnvState;

		float mix = tone * (1.f - snap) * 1.6f + hiss * (0.4f + snap * 1.6f);
		return dc.process(transistorClip(mix)) * 5.f;
	}

	float noiseEnvState = 0.f;
};

// ---------------------------------------------------------------------------
// HAT -- the same avalanche-noise tap as SNARE, but read from BaSnaHi's
// "Output HH?" node: a tighter highpass (R21/C14/R22) leaves only the upper
// end. TONE sweeps that corner; there is no separate DECAY knob for the
// noise's own colour in the original, only the shared strike envelope.
// ---------------------------------------------------------------------------
struct Hat {
	Noise noise;
	OnePole hp1, hp2;
	DcBlock dc;
	float env = 0.f;

	Hat() : noise(0xBADC0DEu) {}
	// rC/gC key on a knob but close over `fs`, so the rate changing has to
	// invalidate them by hand -- the key alone cannot see it move.
	void setRate(float fs) { dc.setRate(fs); this->fs = fs; rC.clear(); gC.clear(); }
	void reset() { hp1.reset(); hp2.reset(); dc.reset(); env = 0.f; rC.clear(); gC.clear(); }
	float fs = 44100.f;

	mt::Cache rC, gC;

	inline float process(bool strike, float vel, float tone, float decay) {
		if (strike) env = vel;
		float r = rC.get(decay, [this](float k) {
			return std::exp(-1.f / (std::fmax(expMap(k, 0.02f, 0.5f), 0.005f) * fs));
		});
		env *= r;

		float g = gC.get(tone, [this](float k) {
			float t = std::tan(kPi * expMap(k, 2000.f, 11000.f) / fs);
			return t / (1.f + t);
		});
		float raw = noise.next();
		float h1 = raw - hp1.lp(raw, g);
		float h2 = h1 - hp2.lp(h1, g);
		return dc.process(h2 * env) * 5.f;
	}
};

// ---------------------------------------------------------------------------
// SMURF -- the "Smurf Drum" half of SmurfDrum_BassDrumish.jpg: a two-transistor
// astable (the 1M PITCH pot, 10k/22k cross-feedback, .01uF cap) whose supply
// is the trigger's own decaying envelope rather than a rail, so both the
// amplitude *and* the pitch sag together as the strike dies out -- the
// "zippy splat" the schematic's notes describe. SWEEP is how much of that
// pitch sag is let through; at 0 it is a plain decaying tone.
// ---------------------------------------------------------------------------
struct Smurf {
	SquareOsc osc;
	OnePole lp;
	DcBlock dc;
	float env = 0.f;
	float fs = 44100.f;

	mt::Cache rC, baseC, lpgC;

	void setRate(float fs_) {
		dc.setRate(fs_); fs = fs_;
		rC.clear(); lpgC.clear();  // both close over fs
	}
	void reset() {
		osc.reset(); lp.reset(); dc.reset(); env = 0.f;
		rC.clear(); baseC.clear(); lpgC.clear();
	}

	inline float process(bool strike, float vel, float pitch, float decay, float sweep) {
		if (strike) env = vel;
		float r = rC.get(decay, [this](float k) {
			return std::exp(-1.f / (std::fmax(expMap(k, 0.04f, 1.2f), 0.005f) * fs));
		});
		env *= r;

		float base = baseC.get(pitch, [](float k) { return expMap(k, 70.f, 700.f); });
		// Sags down as env dies, so this one really does move every sample --
		// but 0.2^x is exp2(x * log2(0.2)), and exp2f is far cheaper than powf.
		float freq = base * std::exp2f(sweep * (1.f - env) * -2.3219281f);
		float sq = osc.process(freq, fs);
		float lpg = lpgC.get(fs, [](float k) {
			float t = std::tan(kPi * 2200.f / k); return t / (1.f + t);
		});
		float y = lp.lp(sq, lpg);
		y = transistorClip(y * (1.f + 2.f * env)) * env;
		return dc.process(y) * 5.f;
	}
};

// ---------------------------------------------------------------------------
// TOM -- TomTomTom's three twin-T rings (the three CD4069 filter branches),
// each a passive ring shocked by the Ken Stone gate-to-trigger pulse. RANGE
// picks which of the three tuned branches is standing in (their fixed
// resistors are literally captioned "change these as you'd like your
// sound"); PITCH trims within the branch, DECAY sets how long it rings.
// ---------------------------------------------------------------------------
struct Tom {
	Modal body;
	DcBlock dc;

	mt::Cache trimC, t60C;

	void setRate(float fs) { dc.setRate(fs); }
	void reset() { body.reset(); dc.reset(); trimC.clear(); t60C.clear(); }

	/** `range` is 0/1/2 for LO/MID/HI. */
	inline float process(bool strike, float vel, float pitch, float decay, int range, float fs) {
		static const float bandHz[3] = { 90.f, 160.f, 280.f };
		float base = bandHz[range >= 0 && range <= 2 ? range : 1];
		float freq = base * trimC.get(pitch, [](float k) { return expMap(k, 0.6f, 1.6f); });
		float t60 = t60C.get(decay, [](float k) { return expMap(k, 0.06f, 1.4f); });
		float y = body.process(strike ? vel * freq * 0.3f : 0.f, freq, t60, fs);
		return dc.process(ftanh(y * 0.8f)) * 5.f;
	}
};

// ---------------------------------------------------------------------------
// BELL -- XORbell's six 40106 relaxation oscillators through three 4070 XOR
// stages, gated by a decay envelope. Three factors rather than six: XOR of
// N bipolar squares is their product, and a third partial already gives the
// inharmonic inside-out-metallic character the original chases; TIMBRE
// spreads the two upper partials away from PITCH the way independent RC
// pairs (rather than a shared one) would.
// ---------------------------------------------------------------------------
struct Bell {
	SquareOsc osc1, osc2, osc3;
	DcBlock dc;
	float env = 0.f;
	float fs = 44100.f;

	mt::Cache rC, f0C;

	void setRate(float fs_) { dc.setRate(fs_); fs = fs_; rC.clear(); }  // rC closes over fs
	void reset() {
		osc1.reset(); osc2.reset(); osc3.reset(); dc.reset(); env = 0.f;
		rC.clear(); f0C.clear();
	}

	inline float process(bool strike, float vel, float pitch, float decay, float timbre) {
		if (strike) env = vel;
		float r = rC.get(decay, [this](float k) {
			return std::exp(-1.f / (std::fmax(expMap(k, 0.03f, 1.5f), 0.005f) * fs));
		});
		env *= r;

		float f0 = f0C.get(pitch, [](float k) { return expMap(k, 180.f, 1800.f); });
		float f1 = f0 * (1.f + timbre * 0.41f);
		float f2 = f0 * (1.f + timbre * 0.98f);
		float x = osc1.process(f0, fs) * osc2.process(f1, fs) * osc3.process(f2, fs);
		return dc.process(x * env) * 5.f;
	}
};

// ---------------------------------------------------------------------------
// NOISE -- the Percussive Noise Voice: the trig's own decay charges/discharges
// through a vactrol (U1), whose LDR sets a lowpass corner over the T1/T2/T3
// avalanche-noise tap. TONE stands in for the C5/C6 pair the original swaps
// by hand between "Snare" and "HiHat" values -- here a continuous sweep of
// the corner the vactrol closes down to. The DEC CV jack is the schematic's
// own "Decay CV in" (P2), landing straight on the envelope's time constant.
// ---------------------------------------------------------------------------
struct NoiseVoice {
	Noise noise;
	OnePole lp;
	Vactrol vac;
	DcBlock dc;
	float env = 0.f;
	float fs = 44100.f;

	mt::Cache t60C, rC, cornerC;

	NoiseVoice() : noise(0x5EAF00Du) {}
	void setRate(float fs_) { dc.setRate(fs_); fs = fs_; rC.clear(); }
	void reset() {
		lp.reset(); vac.s = 0.f; dc.reset(); env = 0.f;
		t60C.clear(); rC.clear(); cornerC.clear();
	}

	inline float process(bool strike, float vel, float tone, float decay, float decayCv) {
		if (strike) env = vel;
		// decayCv is audio-rate, so key the envelope coefficient on the summed
		// t60 rather than on the knob -- it still holds still whenever nothing
		// is patched into NOISE CV, which is the common case.
		float t60 = std::fmax(t60C.get(decay,
			[](float k) { return expMap(k, 0.02f, 1.0f); }) + decayCv, 0.005f);
		float r = rC.get(t60, [this](float k) { return std::exp(-1.f / (k * fs)); });
		env *= r;

		float smoothed = vac.process(env, 35.f, fs);   // the LDR's lag
		float hiCorner = cornerC.get(tone, [](float k) { return expMap(k, 300.f, 9000.f); });
		float cutoff = 150.f + smoothed * hiCorner;
		float g = std::tan(kPi * std::fmin(cutoff, fs * 0.45f) / fs); g = g / (1.f + g);
		float raw = noise.next();
		float y = lp.lp(raw, g);
		return dc.process(y * env) * 5.f;
	}
};

// ---------------------------------------------------------------------------
// DAZZLER -- the Tiny Dazzler noise voice: a backwards-wired transistor (EBC,
// avalanche noise off its reverse-biased junction) into a decay envelope and
// a switched two-pole lowpass, C1/C2 picking "Snare" or "HiHat" values on the
// silkscreen table. KIT is that switch; CRACKLE is the schematic's own
// margin note -- "higher values for C1 create more crackle in the decay" --
// as a continuous blend rather than a third fixed cap.
// ---------------------------------------------------------------------------
struct Dazzler {
	Noise noise;
	OnePole lp1, lp2;
	DcBlock dc;
	float env = 0.f;
	float fs = 44100.f;

	mt::Cache rC, gC, g2C;

	Dazzler() : noise(0xFACADE1u) {}
	void setRate(float fs_) { dc.setRate(fs_); fs = fs_; rC.clear(); gC.clear(); g2C.clear(); }
	void reset() {
		lp1.reset(); lp2.reset(); dc.reset(); env = 0.f;
		rC.clear(); gC.clear(); g2C.clear();
	}

	/** `kit` 0 = snare-ish (open, low corner), 1 = hihat-ish (tight, bright). */
	inline float process(bool strike, float vel, int kit, float decay, float crackle) {
		if (strike) env = vel;
		float r = rC.get(decay, [this](float k) {
			return std::exp(-1.f / (std::fmax(expMap(k, 0.02f, 1.2f), 0.005f) * fs));
		});
		env *= r;

		float corner = kit ? 6500.f : 900.f;
		corner *= (0.5f + crackle);            // more crackle: brighter, grittier
		float g = gC.get(corner, [this](float k) {
			float t = std::tan(kPi * std::fmin(k, fs * 0.45f) / fs); return t / (1.f + t);
		});
		float raw = noise.next();
		float y = lp1.lp(raw, g);
		// A second, lightly-detuned pole left in the loop is the "crackle": a
		// slightly resonant beat between two close corners rather than a flat roll-off.
		float g2 = g2C.get(corner * (1.f + crackle * 0.35f), [this](float k) {
			float t = std::tan(kPi * std::fmin(k, fs * 0.45f) / fs); return t / (1.f + t);
		});
		float y2 = lp2.lp(y, g2);
		float mix = y + (y - y2) * crackle * 1.5f;
		return dc.process(mix * env) * 5.f;
	}
};

} // namespace kickback

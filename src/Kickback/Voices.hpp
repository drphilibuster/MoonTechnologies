#pragma once
// Kickback's six percussion voices, built from the physical vocabulary in
// Drum.hpp. Pure DSP: no Rack types, no allocation, no I/O -- Kickback.cpp owns
// params/ports and calls in here once a sample.
//
// Every voice takes the same five controls, in the same order, so the panel is
// one grid rather than six special cases:
//
//   tune    0..1  the voice's fundamental, transposed by its own V/OCT jack
//   decay   0..1  how long it rings
//   bend    0..1  how far the pitch falls as the strike's energy leaves it
//   colour  0..1  the one thing that circuit is *for* -- drive, texture,
//                 strike position, wires -- or, on the voices that switch
//                 between whole models, which model is running
//
// plus a 0..1 velocity at strike time (ACCENT and the pattern engine folded in
// by the caller). Output is normalised so 1.0 is 1 V; the module's +-5 V
// convention is +-5.0.
//
// Two voices spend their colour control on a model switch instead of a knob,
// because that is the control those circuits actually have. Where that happens
// the voice's own character rides on BEND, which is documented per voice: a
// column with a switch in the colour row is a column whose BEND does double
// duty.
//
// Sources. The donor circuits are all from Modular in a Week's Day 9 folder;
// the synthesis methods behind them are from the drum-synthesis literature, and
// Drum.hpp carries the full citations. docs/Kickback.md has the per-voice notes
// on what was kept and what was approximated.
//
//   BaSnaHi.pdf                 kristian.borgstedt       -> KICK, HAT
//   SmurfDrum_BassDrumish.jpg   Tiny Dazzler Electronics -> KICK's SMURF model
//   TomTomTom.pdf               Kristian Blasol          -> TOM I/II/III
//   XORbell.pdf                 Kristian Blasol / Elliot Williams -> SNARE's XOR mode
//   Percussive Noise Voice.pdf  Kristian Blasol / A_Magic_Pulsewave -> SNARE's VACTROL mode
//   Tiny Dazzler Schematic.png  Tiny Dazzler Electronics -> SNARE's DAZZLE mode, HAT's top end

#include "../Drum.hpp"

namespace kickback {

/** How many voices the module has, and their order everywhere -- params,
    ports, lights, the pattern engine's tables. */
enum VoiceId {
	V_KICK, V_SNARE, V_HAT, V_TOM1, V_TOM2, V_TOM3,
	V_COUNT
};

inline float clampf(float x, float lo, float hi) {
	return x < lo ? lo : (x > hi ? hi : x);
}

/** 1 V/oct on a 0..1 knob's mapped frequency. Clamped wide but finite, so a
    stray LFO on a V/OCT jack cannot push a resonator past Nyquist or down to
    DC and leave its state history poisoned. */
inline float transpose(float hz, float volts) {
	return hz * std::exp2f(clampf(volts, -5.f, 5.f));
}


// ---------------------------------------------------------------------------
// KICK -- two models, one set of controls.
//
// BRIDGE is BaSnaHi's bassdrum stage (Q1, R1-R8, C1-C5): a diode-coupled trig
// charges the base network and shocks the transistor's RC feedback pair into
// ringing. SMURF is the "Smurf Drum" half of SmurfDrum_BassDrumish.jpg: a
// two-transistor astable (the 1M PITCH pot, 10k/22k cross-feedback, .01uF cap)
// running off the trigger's own decaying envelope rather than a rail, so
// loudness and pitch sag together -- the "zippy splat" its notes describe.
//
// They are the same controls because they answer the same question in two
// ways: a ringing filter and a starved oscillator both make a bass drum, and
// which one a patch wants is a switch, not two columns of panel.
// ---------------------------------------------------------------------------
struct Kick {
	// BRIDGE
	ModalBank body;
	Tension tension;
	// SMURF
	SquareOsc osc;
	OnePole lp, lp2;
	Decay smurfEnv;
	// shared
	StrikePulse strike;
	Attack attack;
	DcBlock dc;
	float fs = 44100.f;

	mt::Cache freqC, t60C;

	Kick() : attack(0x1CE7A11u) {}

	void setRate(float fs_) {
		fs = fs_; dc.setRate(fs_); body.setRate(fs_); attack.setRate(fs_);
	}
	void reset() {
		body.reset(); tension.reset(); osc.reset(); lp.reset(); lp2.reset();
		smurfEnv.reset(); strike.reset(); attack.reset(); dc.reset();
		freqC.clear(); t60C.clear();
	}

	/** `mode` 0 = BRIDGE, 1 = SMURF. `colour` is DRIVE for both. */
	inline float process(bool hit, float vel, int mode, float tune, float volts,
	                     float decay, float bend, float colour) {
		float f0 = transpose(freqC.get(tune, [](float k) { return expMap(k, 32.f, 190.f); }), volts);
		float t60 = t60C.get(decay, [](float k) { return expMap(k, 0.05f, 1.8f); });

		if (hit) {
			// A kick beater is soft; DRIVE hardens it, which is what the
			// original's overdriven transistor does to the leading edge.
			strike.trigger(vel, 0.18f + 0.42f * colour, fs);
			attack.strike(vel * (0.5f + 0.4f * colour));
			smurfEnv.strike(vel);
		}
		float x = strike.next();

		float y;
		if (mode == 0) {
			// A bass drum is struck dead centre; the rim modes belong to toms.
			body.setPosition(0.10f + 0.16f * colour);
			float b = tension.process(body.fundamental(), bend, fs);
			body.setTuning(f0, t60, 0.62f, b);
			y = body.process(x) * 0.9f;
		}
		else {
			float env = smurfEnv.process(t60, fs);
			// The astable's supply *is* the envelope, so the pitch sags with
			// it -- BEND is how much of that sag is let through, which is the
			// sheet's own switched cap on the upper half of the circuit.
			float f = f0 * 1.8f * std::exp2f(bend * (1.f - env) * -1.9f);
			float sq = osc.process(f, fs);
			// Rounded hard, and twice, tracking the oscillator's own pitch. A
			// two-transistor astable is slew-limited by the very RC pair that
			// sets its period and is nowhere near a hard square -- and leaving
			// it square costs the voice its beater, because a square's
			// harmonics run past 9 kHz for the whole note and mask any click
			// sitting on top of them.
			float r1 = lp.lp(sq, poleG(std::fmin(f * 2.2f, 1500.f), fs));
			y = lp2.lp(r1, poleG(std::fmin(f * 3.2f, 2200.f), fs)) * env * 2.4f;
			// The strike pulse still lands, so SMURF has an edge too.
			y += x * 0.5f;
		}

		y = transistorClip(y * (0.7f + colour * 2.6f)) * (0.9f + colour * 0.7f);
		// The beater, heard directly. A kick's click is the one everybody
		// reaches for the moment it is missing -- it is what says the head was
		// hit rather than that an oscillator was switched on.
		y += strike.click() * (1.4f + 2.0f * colour);
		y += attack.process(0.15f + 0.5f * colour, 1.8f + 1.8f * colour);
		// Trimmed per model: a starved astable runs a good deal hotter than a
		// ringing filter, and one number for both would make the switch a
		// volume control.
		return dc.process(y * (mode ? 0.310f : 0.484f)) * 5.f;
	}
};


// ---------------------------------------------------------------------------
// The three engines behind SNARE, and the one HAT borrows from.
//
// A snare is the one drum on this panel with no single right answer -- what
// people want from it runs from a tuned crack to a wash of noise to a rattle
// -- so it is three circuits under one switch rather than one circuit with a
// knob pretending to reach all three.
//
// BaSnaHi's own snare stage is not among them. It reads as a struck metal pipe
// rather than a snare, which is a fine thing to be but not this one, so it left
// for a module of its own: see src/Toll/.
// ---------------------------------------------------------------------------

/** XORbell's six 40106 relaxation oscillators through three 4070 XOR stages.

    XOR of two square waves in their bipolar (+-1) encoding is exactly their
    product, so three band-limited squares multiplied give the inharmonic,
    clangy body; the timbre term spreads the upper two away from the
    fundamental. A short modal ring sits underneath, because pure square
    products have no body under the clang.

    Three oscillators rather than six -- a third partial already supplies the
    character the extra three mostly reinforce. */
struct XorEngine {
	SquareOsc o1, o2, o3;
	ModalBank body;
	Decay env;
	float fs = 44100.f;

	void setRate(float fs_) { fs = fs_; body.setRate(fs_); }
	void reset() { o1.reset(); o2.reset(); o3.reset(); body.reset(); env.reset(); }
	inline void strike(float vel) { env.strike(vel); }

	inline float process(float f0, float t60, float bend, float timbre, float x) {
		float e = env.process(t60, fs);
		// The 40106's own supply sagging as the gate envelope empties.
		float sag = std::exp2f(bend * (1.f - e) * -0.9f);
		float f = f0 * sag;
		float sq = o1.process(f, fs)
		         * o2.process(f * (1.f + timbre * 0.41f), fs)
		         * o3.process(f * (1.f + timbre * 0.98f), fs);
		body.setPosition(0.78f);
		body.setTuning(f0 * 2.1f, t60 * 0.5f, 0.5f, sag);
		return sq * e * 0.8f + body.process(x) * 0.35f;
	}
};

/** The Percussive Noise Voice: the trig's own decay drives a vactrol, whose
    slow-following LDR sets a lowpass corner over the T1/T2/T3 avalanche-noise
    tap. The grain term is that lag, from fast (nearly a gate) to slow (a
    smeared, breathing decay) -- the lag is what makes this sound like a
    photoresistor rather than a VCA, and it is the character of the circuit. */
struct VactrolEngine {
	Noise noise;
	OnePole lp, lp2;
	Vactrol vac;
	Decay env;
	float fs = 44100.f;
	//: The vactrol's filter is a lowpass at a corner in hertz, so this engine
	//: wants noise at constant spectral density -- see noisePsdGain().
	float psd = 1.f;

	VactrolEngine() : noise(0x5EAF00Du) {}
	void setRate(float fs_) { fs = fs_; psd = noisePsdGain(fs_); }
	void reset() { noise.reset(); lp.reset(); lp2.reset(); vac.reset(); env.reset(); }
	inline void strike(float vel) { env.strike(vel); }

	inline float process(float top, float t60, float bend, float grain) {
		float e = env.process(t60, fs);
		// 90 Hz down to 6 Hz of rise time is the range real vactrols cover.
		float smoothed = vac.process(e, 90.f * std::pow(0.07f, grain), fs);
		float span = top * (0.25f + 0.75f * (1.f - bend));
		float g = poleG(140.f + smoothed * span * (1.f + bend * 2.f), fs);
		return lp2.lp(lp.lp(noise.next() * psd, g), g) * e * (1.5f + 0.4f * grain);
	}
};

/** Karplus & Strong's 1983 drum recurrence, verbatim.

        y[t] = +1/2 (y[t-p] + y[t-p-1])   with probability b
             = -1/2 (y[t-p] + y[t-p-1])   with probability 1 - b

    At b = 1/2 the table length stops setting pitch and sets decay instead, and
    the paper's own reading is that large p is "the effect of a snare drum" and
    small p "a brushed tom-tom", "allowing smooth transition from one drum
    sound to another." Both ends are used: the snare's DAZZLE mode takes the
    long one, the hat's top end the short one. */
struct KsEngine {
	KsDrum ks;
	OnePole lp, hp;
	Decay env;
	float fs = 44100.f;
	float psd = 1.f;
	mt::Cache capC;
	int lastMode = -1;

	void setRate(float fs_) { fs = fs_; psd = noisePsdGain(fs_); capC.clear(); }
	void reset() { ks.reset(); lp.reset(); hp.reset(); env.reset(); capC.clear(); lastMode = -1; }

	/** `bright` 0 = the snare end (long table, open and low), 1 = the hat end
	    (short table, tight and bright). `blend` is the paper's b. */
	inline void strike(float vel, float hz, float blend, int bright) {
		// A blend near 1/2 makes its own randomness, so the paper allows a
		// constant load; nearer the string ends it needs a noisy one.
		float bias = 1.f - std::fmin(std::fabs(blend - 0.5f) * 2.f, 1.f);
		// The snare end finishes in a lowpass at a corner in hertz and wants
		// noise at constant spectral density; the hat end finishes in a
		// highpass, whose band grows with the rate, and is right without it.
		ks.pluck(vel * (bright ? 1.f : psd), (int)(fs / std::fmax(hz, 8.f)), bias * 0.8f);
		env.strike(vel);
	}

	/** `stretch` is the paper's S, which it notes "increases the snare sound". */
	inline float process(float hz, float t60, float blend, float stretch, int bright) {
		if (bright != lastMode) { lastMode = bright; capC.clear(); }
		float e = env.process(t60, fs);
		float y = ks.process(blend, stretch);
		float g = capC.get(hz, [this, bright](float h) {
			return poleG(std::fmin(h * (bright ? 1.6f : 9.f), fs * 0.45f), fs);
		});
		y = bright ? hp.hp(y, g) : lp.lp(y, g);
		// The recurrence decays on its own; the envelope only ever shortens it.
		return y * std::fmin(e * 3.f, 1.f);
	}
};


// ---------------------------------------------------------------------------
// SNARE -- three circuits under one switch.
//
// The modal shell with wires that used to sit here read as a struck metal pipe
// rather than as a snare, so it moved to BELL, where that is exactly what is
// wanted, and the three circuits that *do* sound like snares took its column:
// the XOR bell's clangy crack, the vactrol noise voice's wash, and
// Karplus-Strong's rattle at the long end of its delay -- the end the paper
// itself calls a snare drum.
//
// MODE has taken the colour knob, so each mode's own character rides on BEND:
// the timbre spread on XOR, the vactrol's grain on VACTROL, and the blend
// factor b together with the stretch factor S on DAZZLE.
// ---------------------------------------------------------------------------
struct Snare {
	XorEngine xorEng;
	VactrolEngine vacEng;
	KsEngine ksEng;
	StrikePulse strike;
	Attack attack;
	DcBlock dc;
	float fs = 44100.f;

	mt::Cache freqC, t60C;

	Snare() : attack(0xC0FFEEu) {}

	void setRate(float fs_) {
		fs = fs_; dc.setRate(fs_); attack.setRate(fs_);
		xorEng.setRate(fs_); vacEng.setRate(fs_); ksEng.setRate(fs_);
	}
	void reset() {
		xorEng.reset(); vacEng.reset(); ksEng.reset();
		strike.reset(); attack.reset(); dc.reset();
		freqC.clear(); t60C.clear();
	}

	/** `mode` 0 = XOR, 1 = VACTROL, 2 = DAZZLE. */
	inline float process(bool hit, float vel, int mode, float tune, float volts,
	                     float decay, float bend) {
		float f0 = transpose(freqC.get(tune, [mode](float k) {
			// Each mode reads TUNE in its own units: a pitch for the XOR
			// oscillators, a filter corner for the vactrol, a delay length for
			// the recurrence. The ranges are chosen so the middle of the knob
			// lands on a usable snare in all three.
			if (mode == 0) return expMap(k, 110.f, 900.f);
			if (mode == 1) return expMap(k, 500.f, 9000.f);
			return expMap(k, 70.f, 460.f);
		}), volts);
		float t60 = t60C.get(decay, [](float k) { return expMap(k, 0.03f, 1.1f); });
		float blend = 0.5f - 0.42f * bend;

		if (hit) {
			strike.trigger(vel, 0.72f, fs);          // a stick, not a beater
			attack.strike(vel);
			if (mode == 0) xorEng.strike(vel);
			else if (mode == 1) vacEng.strike(vel);
			else ksEng.strike(vel, f0, blend, 0);
		}
		float x = strike.next();

		float y;
		// Trimmed per mode, then the column as a whole. Three unrelated
		// circuits do not arrive at the same loudness on their own, and a mode
		// selector that is also a volume control is unusable.
		if (mode == 0)      y = xorEng.process(f0, t60, bend, bend, x) * 1.256f;
		else if (mode == 1) y = vacEng.process(f0, t60, bend, bend) * 4.394f;
		else                y = ksEng.process(f0, t60, blend,
		                                      1.f + bend * bend * 11.f, 0) * 6.148f;

		y += strike.click() * 1.1f;
		y += attack.process(0.55f, 1.6f);
		// Into the stage well under its knee, so a ghost note stays linear and
		// only a hard strike finds the transistor's rail.
		return dc.process(transistorClip(y * 0.20f) * 2.85f) * 5.f;
	}
};


// ---------------------------------------------------------------------------
// HAT -- BaSnaHi's "Output HH?" node, read the way an 808 reads it.
//
// The original is the same noise cascade through a tighter highpass. A pure
// filtered-noise hat is the one everybody writes and nobody keeps: what makes
// a hi-hat a hi-hat is that two lumps of metal are ringing, so three square
// oscillators multiplied together supply the metal.
//
// RATTLE runs through all three of the hat's textures rather than switching
// between them: metal at the bottom, noise in the middle, and the Tiny
// Dazzler's Karplus-Strong rattle at the short end of its delay line -- what
// the 1983 paper calls a brushed tom-tom -- at the top. The hat had no control
// to spare for a mode switch and RATTLE was already the texture control, so
// the third texture went on the end of it, which also makes both boundaries
// sweepable rather than stepped.
//
// ACCENT opens the hat. There is only one hi-hat here, so how hard it is
// struck is what decides whether it is closed or open, exactly as a pedal
// would: velocity multiplies the ring time, better than three to one from a
// ghost tick to a full accent.
// ---------------------------------------------------------------------------
struct Hat {
	Noise noise;
	SquareOsc o1, o2, o3;
	OnePole hp1, hp2, lp1;
	KsEngine ksEng;
	Decay fast, slow;
	Attack attack;
	DcBlock dc;
	float fs = 44100.f;

	mt::Cache freqC, t60C, topC;

	Hat() : noise(0xBADC0DEu), attack(0x4A77E5u) {}

	// topC closes over fs, so the rate moving has to invalidate it by hand --
	// its key is a knob and cannot see the rate change behind it.
	void setRate(float fs_) {
		fs = fs_; dc.setRate(fs_); attack.setRate(fs_); ksEng.setRate(fs_); topC.clear();
	}
	void reset() {
		noise.reset();
		o1.reset(); o2.reset(); o3.reset();
		hp1.reset(); hp2.reset(); lp1.reset();
		ksEng.reset(); fast.reset(); slow.reset(); attack.reset(); dc.reset();
		freqC.clear(); t60C.clear(); topC.clear();
	}

	/** `tune` sets the highpass corner and the metal's pitch together;
	    `colour` is RATTLE: metal -> noise -> Dazzler. */
	inline float process(bool hit, float vel, float tune, float volts,
	                     float decay, float bend, float colour) {
		float corner = transpose(freqC.get(tune, [](float k) { return expMap(k, 1800.f, 11000.f); }), volts);
		float t60 = t60C.get(decay, [](float k) { return expMap(k, 0.018f, 0.9f); });
		// The pedal. A ghost tick closes to a third of the knob's time, a full
		// accent opens to better than twice it.
		float open = 0.34f + 0.78f * clampf(vel, 0.f, 2.f);
		float t = t60 * open;
		float ksHz = corner * 0.25f;

		if (hit) {
			fast.strike(vel); slow.strike(vel); attack.strike(vel * 0.8f);
			// The Dazzler's delay tracks the same knob, at its short end.
			ksEng.strike(vel, ksHz, 0.5f, 1);
		}
		float ef = fast.process(t * 0.18f, fs);
		float es = slow.process(t, fs);
		float env = ef * 0.55f + es * 0.75f;

		// BEND sweeps the corner down as the hat dies -- the same "pitch high
		// then down" the envelope does on a pitched voice, applied to colour.
		float sweep = corner * (1.f + bend * bend * 2.2f * es);
		float g = poleG(std::fmin(sweep, fs * 0.45f), fs);

		// Three squares at inharmonic ratios, multiplied. Their fundamentals
		// sit under the corner so the highpass is what shapes them.
		float m = o1.process(corner * 0.42f, fs)
		        * o2.process(corner * 0.63f, fs)
		        * o3.process(corner * 0.87f, fs);
		float n = noise.next();

		// RATTLE crossfades metal -> noise over the first half of its travel
		// and noise -> Dazzler over the second, so each boundary is a sweep.
		float metal = clampf(1.f - colour * 2.f, 0.f, 1.f);
		float dazz  = clampf(colour * 2.f - 1.f, 0.f, 1.f);
		float hiss  = 1.f - metal - dazz;
		float src = m * metal + n * (hiss * 1.25f);

		float h1 = src - hp1.lp(src, g);
		float h2 = h1 - hp2.lp(h1, g);
		// Capped at 18 kHz, not at Nyquist. Clamping the top of the band to
		// the sample rate makes the band itself rate-dependent -- the hat would
		// occupy 19.8 kHz of spectrum at 44.1 and 28.6 kHz at 96.
		float topG = topC.get(corner, [this](float c) {
			return poleG(std::fmin(std::fmin(c * 2.6f, 18000.f), fs * 0.45f), fs);
		});
		float y = lp1.lp(h2, topG) * env;

		// The Dazzler's path keeps its own envelope, so its rattle sits beside
		// the metal rather than being gated by it.
		if (dazz > 0.f)
			y += ksEng.process(ksHz, t, 0.5f, 1.f + bend * bend * 6.f, 1) * dazz * 1.5f;

		y += attack.process(0.9f, 0.5f);
		return dc.process(y * 1.445f) * 5.f;
	}
};


// ---------------------------------------------------------------------------
// TOM -- TomTomTom's twin-T rings, three of them, all at once.
//
// The schematic is three physically separate CD4069-buffered twin-T branches,
// each shocked by its own Ken Stone gate-to-trigger pulse (CGS24), captioned
// "change these 3 resistors as you'd like your sound." That caption is the
// point: the three branches are the *same circuit built at three sizes*, so
// the three voices here are one struct given a size, and the size has to move
// more than the pitch or they are one drum transposed. A floor tom's knob does
// not reach a rack tom's range, its upper modes hang on longer, it rings
// longer, and it takes a softer beater. The ranges overlap by about a fourth
// at each join, so a kit can be tuned across them without a gap and without
// all three landing on the same note.
//
// This is the voice the mode bank was built for. STRIKE moves the stick from
// dead centre -- where only the circular modes are displaced and the drum is
// one boomy partial -- out toward the rim, where the radial modes come in and
// it goes hollow and complex, and hardens the stick as it goes.
// ---------------------------------------------------------------------------
struct Tom {
	ModalBank body;
	Tension tension;
	StrikePulse strike;
	Attack attack;
	DcBlock dc;
	float fs = 44100.f;
	int size = 1;               // 0 low, 1 mid, 2 high

	mt::Cache freqC, t60C;

	Tom(uint32_t seed, int size_)
		: attack(seed), size(size_ < 0 ? 0 : (size_ > 2 ? 2 : size_)) {}

	void setRate(float fs_) {
		fs = fs_; dc.setRate(fs_); body.setRate(fs_); attack.setRate(fs_);
	}
	void reset() {
		body.reset(); tension.reset(); strike.reset(); attack.reset();
		dc.reset(); freqC.clear(); t60C.clear();
	}

	/** `colour` is STRIKE: centre to rim, soft to hard, together. */
	inline float process(bool hit, float vel, float tune, float volts,
	                     float decay, float bend, float colour) {
		//: Tune, ring and damping per size. The overlaps -- 80..150 Hz between
		//: low and mid, 150..290 between mid and high -- are deliberate: a kit
		//: has to be tunable across the joins.
		static const float lo[3]   = {  42.f,  80.f, 150.f };
		static const float hi[3]   = { 150.f, 290.f, 520.f };
		static const float ring[3] = {  3.0f,  2.2f,  1.4f };
		//: Damping is how much faster the upper modes die than the fundamental.
		//: A big shell holds its overtones; a small one is all fundamental and
		//: gone. This is most of why the three do not read as one drum moved.
		static const float damp[3] = { 0.34f, 0.55f, 0.76f };
		//: And a floor tom takes a softer beater than a rack tom.
		static const float soft[3] = { 0.12f, 0.25f, 0.40f };
		//: A hard beater on a short contact makes a *big* click -- the pulse is
		//: normalised to unit area, so halving its width doubles its peak --
		//: and left alone the small tom would out-crack the big one. Small
		//: drums click brighter, not louder.
		static const float clickG[3] = { 1.00f, 0.85f, 0.62f };
		//: The three sizes do not land on one loudness by themselves: the low
		//: tom has more modes in band and the high one a bigger transient.
		static const float trim[3] = { 0.620f, 0.700f, 0.780f };

		int z = size;
		float f0 = transpose(freqC.get(tune,
			[z](float k) { return expMap(k, lo[z], hi[z]); }), volts);
		float t60 = t60C.get(decay, [z](float k) { return expMap(k, 0.08f, ring[z]); });

		// STRIKE at zero means no strike. The knob's job at that end is a
		// drum hit dead centre with a soft mallet, and a beater still audible
		// there is the knob failing to do the one thing its bottom is for --
		// but the click is wanted back the instant the knob moves, not faded
		// in over the first third of its travel. So `bite` is zero at zero and
		// full again by about a tenth of a turn: the contact goes to its
		// softest three milliseconds, which is a low enough lowpass that the
		// mode bank hears no upper modes either, and both direct paths -- the
		// click and the stochastic attack layer -- are muted outright.
		float bite = clampf(colour * 9.f, 0.f, 1.f);

		if (hit) {
			strike.trigger(vel, (soft[z] + 0.55f * colour) * bite, fs);
			attack.strike(vel);
		}
		float x = strike.next();

		body.setPosition(0.05f + 0.9f * colour);
		float b = tension.process(body.fundamental(), bend, fs);
		body.setTuning(f0, t60, damp[z], b);
		float y = body.process(x);

		y = ftanh(y * 0.9f);
		// The stick, direct. STRIKE hardens it as it moves toward the rim, so
		// a centre hit is a soft thump with a low beater note in it and a rim
		// hit is a crack -- which is the difference a listener actually uses
		// to tell a tom from a filtered saw.
		y += strike.click() * (0.65f + 1.35f * colour) * clickG[z] * bite;
		y += attack.process(soft[z] + 0.6f * colour, (1.3f + 0.9f * colour) * bite);
		return dc.process(y * trim[z]) * 5.f;
	}
};


}  // namespace kickback

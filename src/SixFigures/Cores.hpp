#pragma once
// Six Figures' DSP: the four MiaW oscillator cores, band-limited, plus the
// small amount of shared machinery (a leaky integrator, a per-voice state
// block) every core borrows from.
//
// None of this is panel code -- see Retroactive/dsp/WindowPermuter.hpp for the
// precedent of keeping a module's engine in its own header, separate from
// SixFigures.cpp, which only wires it to params/ins/outs and the silkscreen.

#include <rack.hpp>
#include <cmath>

namespace sixfigures {

using namespace rack;

enum Core {
	CORE_SCHMITT   = 0,   // 40106 hex Schmitt astable: square, RC cap ramp on the side
	CORE_AAC       = 1,   // 4069 integrator + Schmitt (All About Circuits VCO): square + triangle
	CORE_PLL       = 2,   // 4046 PLL VCO: square, locks to SIGNAL via an XOR phase detector
	CORE_AVALANCHE = 3,   // Kassutronics reverse-avalanche: RC-charge saw, plus slow drift
	NUM_CORES      = 4,
};

// --- band-limiting -----------------------------------------------------------
// Two-sample polynomial BLEP, the standard trivial-oscillator correction: it
// replaces the naive discontinuity at a wrap with a curve that has the same
// band-limited spectrum a real RC relaxation oscillator's rounded corner would.
inline float polyblep(float t, float dt) {
	if (t < dt) {
		float x = t / dt;
		return x + x - x * x - 1.f;
	}
	if (t > 1.f - dt) {
		float x = (t - 1.f) / dt;
		return x * x + x + x + 1.f;
	}
	return 0.f;
}

/** Bipolar band-limited square from a phase in [0, 1) advancing by `dt` a sample. */
inline float blepSquare(float phase, float dt) {
	float v = phase < 0.5f ? 1.f : -1.f;
	v += polyblep(phase, dt);
	float t2 = phase + 0.5f;
	if (t2 >= 1.f)
		t2 -= 1.f;
	v -= polyblep(t2, dt);
	return v;
}

/** Bipolar band-limited ramp (rising sawtooth) from the same phase convention. */
inline float blepSaw(float phase, float dt) {
	float v = 2.f * phase - 1.f;
	v -= polyblep(phase, dt);
	return v;
}

/** Bends a linear ramp in [-1, 1] toward the bulge of an RC charge/discharge
    curve -- the character both the 40106's cap voltage and the avalanche
    core's saw actually have, rather than the straight line a bare BLEP ramp
    draws. `amount` in [0, 1); 0 leaves the ramp alone. */
inline float rcBend(float x, float amount) {
	float s = x < 0.f ? -1.f : 1.f;
	float m = std::fabs(x);
	return s * std::pow(m, 1.f - 0.6f * amount);
}

/** Leaky integrator: turns a band-limited square into a band-limited triangle
    of roughly constant amplitude regardless of frequency (the rising edge
    must cross from -1 to +1 in half a period, so the per-sample step is
    4*freq/fs), with a slow leak back to zero so quantization and BLEP
    corrections cannot walk the DC offset away over time. */
struct Integrator {
	float state = 0.f;
	inline float process(float square, float freq, float sampleTime) {
		state += square * 4.f * freq * sampleTime;
		state -= state * 0.0008f;
		return state;
	}
	void reset() { state = 0.f; }
};

/** Per-voice state that outlives one process() call: the phase all four cores
    share, the triangle integrator (AAC), the slow drift wander (avalanche),
    the PLL loop filter and its lock heuristic, and the one-cycle pulse the
    avalanche core hands its AUX output. Every voice keeps all of this, not
    just the state its current CORE setting needs, so switching CORE mid-patch
    never reads from a stale or uninitialized field. */
struct Voice {
	float phase = 0.f;
	Integrator tri;
	float drift = 0.f;
	float loopFilter = 0.f;      // 4046-style: low-passed XOR bit, centred on 0
	float loopFilterSlow = 0.f;  // slower average of the same, for the lock heuristic
	float lockEnv = 0.f;
	bool locked = false;
	dsp::PulseGenerator avalanchePulse;

	void reset() {
		phase = 0.f;
		tri.reset();
		drift = 0.f;
		loopFilter = 0.f;
		loopFilterSlow = 0.f;
		lockEnv = 0.f;
		locked = false;
	}
};

// --- frequency mapping -------------------------------------------------------
// The panel's two ranges. Not calibrated to anything in particular -- these
// are RC relaxation oscillators, not tracking VCOs -- just an audio range and
// an LFO range wide enough to be useful.
inline void rangeHz(bool lfo, float& lo, float& hi) {
	if (lfo) {
		lo = 0.05f;
		hi = 8.f;
	}
	else {
		lo = 20.f;
		hi = 4000.f;
	}
}

/** The default, "crude" response: CV is summed directly into the knob's own
    0-1 pot position -- exactly what the AAC sketch's "CV summing mixer"
    feeding the freq pot's wiper shows -- and *then* the knob's exponential
    taper is applied. That taper makes the dial usable, but the CV response
    it gives an RC oscillator is nothing like 1 V/oct: doubling the CV does
    not double the octave shift near the ends of the pot's travel the way it
    would in the middle. */
inline float potFreq(float knob, float cvVolts, float cvAmount, float lo, float hi) {
	float pot = clamp(knob + cvVolts * 0.1f * cvAmount, 0.f, 1.f);
	return lo * std::pow(hi / lo, pot);
}

/** The menu's "1 V/oct" mode: a real exponential converter, so CV tracks
    pitch properly. The knob becomes a coarse offset from the range's
    geometric centre, spanning the same total range in octaves as the pot
    mode's full sweep. */
inline float voltOctFreq(float knob, float cvVolts, float cvAmount, float lo, float hi) {
	float centre = std::sqrt(lo * hi);
	float span = std::log2(hi / lo);
	float oct = (knob - 0.5f) * span + cvVolts * cvAmount;
	return centre * std::pow(2.f, oct);
}

} // namespace sixfigures

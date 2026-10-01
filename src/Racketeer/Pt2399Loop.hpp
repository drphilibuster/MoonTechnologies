#pragma once
// The PT2399 abused: a delay chip run as a self-sustaining noise voice.
//
// This is the loop of Wolfgang Spahn's PB701 Electric Intonarumori (after Urs
// Gaudenz's Chaos Looper). The chip in it is the same one the Verbtronic is
// built from, and it is modelled the same way: not as a buffer of samples but
// as the datasheet draws it -- a 1-bit adaptive delta modulator writing 44 kbit
// of RAM, a demodulator reading it back, and a clock set by the resistance on
// pin 6 (src/Pt2399.hpp, shared with Amortization).
//
// What that gives, without being asked for:
//
//   * TIME is the clock. Delay = 44 kbit / bit rate, so 30 ms is a 1.47 Mbit/s
//     clock and 1.2 s is 37 kbit/s. There is no read pointer to interpolate: a
//     moving TIME changes the rate the stored bits are replayed at, which is
//     what bends the pitch on the real chip.
//   * The converter is 1 bit, not a word length. Its noise is the adaptive
//     modulator's: slope overload on loud fast signals, granular hiss on quiet
//     ones, and a bandwidth that falls with the clock. The old model's "bits
//     left" figure and its clock-tracking reconstruction filter were not the
//     chip; the filters around the chip are fixed RC networks.
//   * ECHO past unity does not saturate softly. The chip's op-amps run from 5 V
//     and clip, so the loop is bounded by the clip at the modulator's input.
//
// The input stage is the PB701's: a 2k and 4n7 low-pass ahead of pin 15 (read
// off Taymur Streng's hand-drawn reverse-engineered schematic -- ASSUMED, the
// drawing is not unambiguous). The CUTOFF low-pass is the one pot on the board;
// RES, the chopper, DRIVE and SEED are additions.
//
// Units inside are +-1 = the chip's full scale, 2.4 V. The module scales at its
// edges.

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "../Pt2399.hpp"

namespace racketeer {

/** xorshift32: a white noise source with no state worth saving. */
struct Noise {
	uint32_t s = 0x9E3779B9u;
	inline float white() {
		s ^= s << 13;
		s ^= s >> 17;
		s ^= s << 5;
		return (float)(int32_t)s * (1.f / 2147483648.f);
	}
};

/** One-pole low-pass, the reconstruction filter's building block. */
struct OnePole {
	float z = 0.f;
	float a = 1.f;
	void setCutoff(float fc, float fs) {
		float x = fc / fs;
		if (x > 0.45f) x = 0.45f;
		if (x < 1e-5f) x = 1e-5f;
		a = 1.f - std::exp(-6.2831853f * x);
	}
	inline float process(float x) {
		z += a * (x - z);
		return z;
	}
	void reset() { z = 0.f; }
};

/** DC blocker at ~10 Hz. The chip is AC coupled; the loop must not wind up. */
struct DcBlock {
	float x1 = 0.f, y1 = 0.f, r = 0.999f;
	void setSampleRate(float fs) { r = 1.f - 6.2831853f * 10.f / fs; }
	inline float process(float x) {
		float y = x - x1 + r * y1;
		x1 = x;
		y1 = y;
		return y;
	}
	void reset() { x1 = y1 = 0.f; }
};

/** TPT state-variable low-pass (Simper). Stable at any cutoff below Nyquist
    and any resonance below self-oscillation, which the setter clamps to. */
struct Svf {
	float ic1 = 0.f, ic2 = 0.f;
	float a1 = 1.f, a2 = 0.f, a3 = 0.f;
	void set(float fc, float fs, float res) {
		float x = fc / fs;
		if (x > 0.45f) x = 0.45f;
		if (x < 1e-5f) x = 1e-5f;
		float g = std::tan(3.14159265f * x);
		if (res < 0.f) res = 0.f;
		if (res > 1.f) res = 1.f;
		float k = 2.f - 1.94f * res;
		a1 = 1.f / (1.f + g * (g + k));
		a2 = g * a1;
		a3 = g * a2;
	}
	inline float low(float x) {
		float v3 = x - ic2;
		float v1 = a1 * ic1 + a2 * v3;
		float v2 = ic2 + a2 * ic1 + a3 * v3;
		ic1 = 2.f * v1 - ic1;
		ic2 = 2.f * v2 - ic2;
		return v2;
	}
	void reset() { ic1 = ic2 = 0.f; }
};


/** The loop. Call setSampleRate() once, then set*() every sample and process(). */
struct Pt2399Loop {
	static constexpr float kMinDelay = 0.030f;
	static constexpr float kMaxDelay = 1.2f;
	// Volts at the chip per loop unit: its op-amps clip at +-CHIP_CLIP, so that
	// is full scale.
	static double volts() { return pt2399::assumed::CHIP_CLIP; }
	// The input stage's 2k against 4n7, twice (two poles, not resonant).
	static constexpr float kInputFc = 17000.f;
	// The datasheet's output noise floor, -90 dBV, at the comparator.
	static constexpr double kNoiseV = 40e-6;

	pt2399::Pt2399 chip;
	float fs = 44100.f;

	// Per-sample settings.
	float delaySec = 0.3f;
	float echo = 0.7f;           // feedback gain; > 1 self-oscillates
	float polarity = 1.f;        // +1 / -1
	float loopGain = 1.f;        // BOOST steps this
	float chop = 1.f;            // 0..1, the chopper / mute in the loop
	bool filterInLoop = true;
	bool kill = false;           // NOISE button in "kill" mode: feedback off
	float chipNoise = 0.5f;      // 0..1, how noisy the comparator is

	// State.
	OnePole in1, in2;
	float prevIn = 0.f;
	DcBlock dc;
	Svf svf;
	Noise noise;

	// Read-out.
	float fsInternal = 1.f;      // the chip's bit clock, Hz

	/** `base` is the engine rate, `os` the oversampling factor the loop runs at. */
	void setSampleRate(float base, int os) {
		fs = base * (float)os;
		chip.setSampleRate((double)fs);
		dc.setSampleRate(fs);
		in1.setCutoff(kInputFc, fs);
		in2.a = in1.a;
		reset();
	}

	void reset() {
		chip.reset();
		chip.setBitRate((double)pt2399::Pt2399::kBits / (double)delaySec);
		in1.reset();
		in2.reset();
		prevIn = 0.f;
		dc.reset();
		svf.reset();
	}

	void setFilter(float cutoffHz, float res) { svf.set(cutoffHz, fs, res); }

	/** One sample. `x` is what goes into the loop (input, seed, injected noise),
	    in +-1 units. Returns the filtered output; `dirty` gets the pre-filter tap. */
	inline float process(float x, float& dirty) {
		float d = delaySec;
		if (d < kMinDelay) d = kMinDelay;
		if (d > kMaxDelay) d = kMaxDelay;

		// --- the clock: TIME sets the bit rate ---
		chip.setBitRate((double)pt2399::Pt2399::kBits / (double)d);
		fsInternal = (float)chip.bitRate;
		chip.begin();

		// --- the demodulator reads what was written a delay ago ---
		float y = (float)(chip.demod() / volts());
		if (!std::isfinite(y)) {
			reset();
			y = 0.f;
		}
		y = dc.process(y);
		dirty = y;

		// --- the low-pass, in the loop or after it ---
		float yF = svf.low(y);
		float loopSig = filterInLoop ? yF : y;

		// --- feedback into the input stage; its op-amp clips at the chip's rail ---
		float fb = kill ? 0.f : polarity * echo * loopGain * chop * loopSig;
		float v = in2.process(in1.process(x + fb));
		if (v > 1.f) v = 1.f;
		else if (v < -1.f) v = -1.f;

		// --- the comparator: noise floor, then the modulator writes the RAM ---
		float cur = (float)volts() * v;
		if (chipNoise > 0.f)
			cur += (float)kNoiseV * std::pow(20.f, 2.f * chipNoise - 1.f) * 1.7320508f * noise.white();
		chip.modulate(prevIn, cur);
		prevIn = cur;

		return yF;
	}
};

} // namespace racketeer

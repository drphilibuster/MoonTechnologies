#pragma once
// The PT2399 abused: a delay chip run as a self-sustaining noise voice.
//
// This is the loop of Wolfgang Spahn's PB701 Electric Intonarumori (after Urs
// Gaudenz's Chaos Looper), modelled by what the chip actually does rather than
// by what a delay pedal is supposed to do:
//
//   * The delay is a fixed-length memory clocked at a variable rate. Longer
//     delay means a slower clock, so the sample rate falls with delay time --
//     about 44 kHz at 30 ms, 4 kHz at 340 ms (the top of the datasheet), just
//     over 1 kHz at the 1.2 s the pot can be pushed to. That is modelled as a
//     sample-and-hold at the chip's own rate ahead of the memory, and a
//     reconstruction low-pass that tracks it, so a long delay is dark and
//     stair-stepped the way the real thing is.
//   * The converters are noisy and the noise grows with delay time; a
//     bit-reduction plus a noise floor, both scaled by delay and by a "chip
//     noise" amount, stand in for that.
//   * ECHO is feedback that runs past unity. The chip's own op-amps clip, so
//     the loop saturates rather than blowing up: a tanh at the memory input
//     bounds everything, and the seed noise is what it grows from.
//
// Units inside are +-1, not volts. The module scales at its edges.

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

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
	// The chip's memory in samples: delay * sampleRate is a constant. 44.1 kHz
	// at the datasheet's 31 ms minimum gives this.
	static constexpr float kMemory = 1365.f;
	static constexpr float kMinDelay = 0.030f;
	static constexpr float kMaxDelay = 1.2f;
	// The ring is sized for this so a sample-rate change is the only allocation.
	static constexpr float kRingSeconds = 1.3f;

	std::vector<float> ring;
	uint32_t mask = 0;
	uint32_t writeIdx = 0;
	float fs = 44100.f;

	// Per-sample settings.
	float delaySec = 0.3f;
	float echo = 0.7f;           // feedback gain; > 1 self-oscillates
	float polarity = 1.f;        // +1 / -1
	float loopGain = 1.f;        // BOOST steps this
	float chop = 1.f;            // 0..1, the chopper / mute in the loop
	bool filterInLoop = true;
	bool kill = false;           // NOISE button in "kill" mode: feedback off
	float chipNoise = 0.5f;      // 0..1, how bad the converters are

	// State.
	float phase = 0.f;           // the chip's sample clock
	float held = 0.f;            // what the S&H is holding
	OnePole rec1, rec2;
	DcBlock dc;
	Svf svf;
	Noise noise;

	// Read-outs.
	float fsInternal = 44100.f;
	float bits = 16.f;

	/** `base` is the engine rate, `os` the oversampling factor the loop runs
	    at. The ring is sized for 2x at `base` whatever `os` is, so switching
	    oversampling on the audio thread never allocates; only a change of
	    engine rate does, and that arrives through onSampleRateChange. */
	void setSampleRate(float base, int os) {
		fs = base * (float)os;
		size_t need = (size_t)(kRingSeconds * base * 2.f) + 8;
		size_t n = 1024;
		while (n < need) n <<= 1;
		if (ring.size() != n) {
			ring.assign(n, 0.f);
			mask = (uint32_t)(n - 1);
			writeIdx = 0;
		}
		dc.setSampleRate(fs);
		reset();
	}

	void reset() {
		if (!ring.empty())
			std::memset(ring.data(), 0, ring.size() * sizeof(float));
		writeIdx = 0;
		phase = 0.f;
		held = 0.f;
		rec1.reset();
		rec2.reset();
		dc.reset();
		svf.reset();
	}

	void setFilter(float cutoffHz, float res) { svf.set(cutoffHz, fs, res); }

	/** 0 at the shortest delay, 1 at the longest -- how far the chip is being pushed. */
	inline float stress() const {
		float t = std::log(delaySec / kMinDelay) / std::log(kMaxDelay / kMinDelay);
		return t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
	}

	/** One sample. `x` is what goes into the loop (input, seed, injected noise),
	    in +-1 units. Returns the filtered output; `dirty` gets the pre-filter tap. */
	inline float process(float x, float& dirty) {
		float d = delaySec;
		if (d < kMinDelay) d = kMinDelay;
		if (d > kMaxDelay) d = kMaxDelay;

		// --- read the memory, interpolated, so a moving TIME bends pitch ---
		float D = d * fs;
		float maxD = (float)(ring.size() - 4);
		if (D > maxD) D = maxD;
		float rp = (float)writeIdx - D;
		if (rp < 0.f) rp += (float)ring.size();
		uint32_t i0 = (uint32_t)rp;
		float frac = rp - (float)i0;
		float y0 = ring[i0 & mask];
		float y1 = ring[(i0 + 1) & mask];
		float y = y0 + (y1 - y0) * frac;
		if (!std::isfinite(y)) {
			reset();
			y = 0.f;
		}

		// --- the chip's clock, and the reconstruction filter that tracks it ---
		fsInternal = kMemory / d;
		float fcRec = 0.42f * fsInternal;
		if (fcRec > 16000.f) fcRec = 16000.f;
		rec1.setCutoff(fcRec, fs);
		rec2.a = rec1.a;
		y = rec2.process(rec1.process(y));
		y = dc.process(y);
		dirty = y;

		// --- the low-pass, in the loop or after it ---
		float yF = svf.low(y);
		float loopSig = filterInLoop ? yF : y;

		// --- feedback into the input, then the op-amps clip ---
		float fb = kill ? 0.f : polarity * echo * loopGain * chop * loopSig;
		float v = std::tanh(x + fb);

		// --- the converters: sample-and-hold at the chip's rate, then quantise ---
		float s = stress();
		bits = 16.f - chipNoise * (3.f + 8.f * s);
		phase += fsInternal / fs;
		if (phase >= 1.f) {
			phase -= std::floor(phase);
			float floor_ = chipNoise * (0.0005f + 0.004f * s) * noise.white();
			float levels = std::exp2(bits - 1.f);
			held = std::round((v + floor_) * levels) / levels;
		}
		ring[writeIdx & mask] = held;
		writeIdx = (writeIdx + 1) & mask;

		return yF;
	}
};

} // namespace racketeer

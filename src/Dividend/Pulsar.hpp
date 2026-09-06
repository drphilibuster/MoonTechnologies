// The pulsar-synthesis core: a train of pulsarets, each a few cycles of a
// waveform under an envelope, issued at the fundamental rate and withheld by
// burst, stochastic and channel masking. After Curtis Roads, "Microsound"
// (MIT Press, 2001), chapter 4.
//
// Unit-agnostic and Rack-free apart from <cmath>: it produces samples in
// [-1, 1] and an envelope in [0, 1] at whatever rate it is ticked at. The
// module oversamples it and decimates the result.
#pragma once
#include <cmath>
#include <cstdint>

namespace dividend {

enum Wave {
	WAVE_SINE, WAVE_SINC, WAVE_SAW, WAVE_SQUARE, WAVE_TRI, WAVE_BURST, NUM_WAVES
};
enum Window {
	WIN_RECT, WIN_GAUSS, WIN_HANN, WIN_EXP, WIN_REXP, WIN_LINEAR, NUM_WINDOWS
};

static const float kTwoPi = 6.28318530717958647692f;

/** One pulsaret waveform sample. `theta` runs 0..1 over the whole pulsaret,
    which holds `cycles` cycles of the waveform. */
inline float pulsaretWave(int wave, float theta, float cycles) {
	float x = theta * cycles;
	float u = x - std::floor(x);           // phase within the current cycle
	switch (wave) {
		case WAVE_SINC: {
			// `cycles` lobes either side of the centre: sin(pi t) / (pi t).
			float t = (theta - 0.5f) * 2.f * cycles;
			float a = 3.14159265358979f * t;
			if (std::fabs(a) < 1e-4f)
				return 1.f;
			return std::sin(a) / a;
		}
		case WAVE_SAW:
			return 2.f * u - 1.f;
		case WAVE_SQUARE:
			return u < 0.5f ? 1.f : -1.f;
		case WAVE_TRI:
			// Starts at zero, so the pulsaret's onset is not itself a step.
			if (u < 0.25f) return 4.f * u;
			if (u < 0.75f) return 2.f - 4.f * u;
			return 4.f * u - 4.f;
		case WAVE_BURST:
			return std::cos(kTwoPi * x);
		default:
			return std::sin(kTwoPi * x);
	}
}

/** The envelope that windows one pulsaret, 0..1 over `theta` in 0..1. */
inline float pulsaretWindow(int window, float theta) {
	switch (window) {
		case WIN_GAUSS: {
			// sigma = 0.15: the edges sit at exp(-5.6), inaudible but continuous.
			float z = (theta - 0.5f) / 0.15f;
			return std::exp(-0.5f * z * z);
		}
		case WIN_HANN:
			return 0.5f * (1.f - std::cos(kTwoPi * theta));
		case WIN_EXP:
			return std::exp(-6.f * theta);
		case WIN_REXP:
			return std::exp(-6.f * (1.f - theta));
		case WIN_LINEAR:
			return 1.f - theta;
		default:
			return 1.f;
	}
}


struct Voice {
	bool active = false;
	float phase = 0.f;        // 0..1 over the pulsaret
	float inc = 0.f;          // per tick
	int wave = WAVE_SINE;
	int window = WIN_GAUSS;
	float cycles = 1.f;
	int channel = 2;          // 0 = L, 1 = R, 2 = both
	uint32_t serial = 0;      // issue order, for stealing the oldest
};


struct PulsarCore {
	enum { POOL = 8 };

	struct Frame {
		float l = 0.f;
		float r = 0.f;
		float env = 0.f;      // the newest pulsaret's window, 0..1
		bool gate = false;    // any pulsaret sounding
		bool onset = false;   // a pulsar period began this tick
		bool masked = false;  // ... and its pulsaret was withheld
	};

	// --- settings, written by the module each sample ------------------------
	float freq = 261.6256f;       // fundamental, Hz; may be negative (TZ FM)
	float formant = 1046.5f;      // Hz; pulsaret duration is cycles / formant
	float cycles = 1.f;
	int wave = WAVE_SINE;
	int window = WIN_GAUSS;
	int burstOn = 1;              // pulsars issued ...
	int burstOff = 0;             // ... then withheld, per burst cycle
	float prob = 1.f;             // stochastic masking: chance a pulsar is paid
	bool stereo = false;          // channel masking: alternate L and R
	bool overlap = false;         // false: truncate at the period; true: pool

	// --- state -----------------------------------------------------------------
	Voice voices[POOL];
	float phase = 0.f;            // fundamental, 0..1
	int burstIndex = 0;
	int channelToggle = 0;
	uint32_t serial = 0;
	uint32_t rng = 0x9E3779B9u;

	void seed(uint32_t s) { rng = s ? s : 0x9E3779B9u; }

	float uniform() {
		// xorshift32: no allocation, no locking, good enough for a coin toss.
		rng ^= rng << 13;
		rng ^= rng >> 17;
		rng ^= rng << 5;
		return (rng >> 8) * (1.f / 16777216.f);
	}

	void reset() {
		for (int i = 0; i < POOL; i++)
			voices[i].active = false;
		phase = 0.f;
		burstIndex = 0;
		channelToggle = 0;
	}

	/** Hard sync: the train restarts here, and a pulsar begins now. */
	void sync() {
		for (int i = 0; i < POOL; i++)
			voices[i].active = false;
		phase = 0.f;
		burstIndex = 0;
		channelToggle = 0;
		pendingOnset = true;
		pendingSince = 0.f;
	}

	/** Advance one tick of `dt` seconds. */
	void tick(float dt, Frame& out) {
		out = Frame();

		// --- the pulsar clock ----------------------------------------------
		bool onset = false;
		float since = 0.f;            // seconds since the period boundary
		if (pendingOnset) {
			onset = true;
			since = pendingSince;
			pendingOnset = false;
		}
		else {
			phase += freq * dt;
			if (phase >= 1.f) {
				phase -= 1.f;
				if (phase >= 1.f) phase = 0.f;
				onset = true;
				since = freq > 0.f ? phase / freq : 0.f;
			}
			else if (phase < 0.f) {
				phase += 1.f;
				if (phase < 0.f) phase = 0.f;
				onset = true;
				since = freq < 0.f ? (1.f - phase) / (-freq) : 0.f;
			}
		}

		if (onset) {
			out.onset = true;
			// Truncation: whatever is still sounding is cut at the period,
			// whether or not this pulsar is paid.
			if (!overlap)
				for (int i = 0; i < POOL; i++)
					voices[i].active = false;

			bool pay = true;
			int total = burstOn + burstOff;
			if (total < 1) total = 1;
			if (burstIndex >= total) burstIndex = 0;
			pay = burstIndex < burstOn;
			burstIndex = (burstIndex + 1) % total;

			if (pay && prob < 1.f)
				pay = uniform() < prob;

			if (pay) {
				Voice* v = allocate();
				float d = cycles / formant;          // pulsaret duration, s
				v->active = true;
				v->inc = dt / d;
				v->phase = since / d;                // sub-tick accurate onset
				if (v->phase >= 1.f) v->phase = 0.f;
				v->wave = wave;
				v->window = window;
				v->cycles = cycles;
				v->serial = ++serial;
				if (stereo) {
					v->channel = channelToggle;
					channelToggle ^= 1;
				}
				else {
					v->channel = 2;
				}
			}
			else {
				out.masked = true;
			}
		}

		// --- the pulsarets --------------------------------------------------
		uint32_t newest = 0;
		for (int i = 0; i < POOL; i++) {
			Voice& v = voices[i];
			if (!v.active)
				continue;
			float w = pulsaretWindow(v.window, v.phase);
			float s = pulsaretWave(v.wave, v.phase, v.cycles) * w;
			if (v.channel != 1) out.l += s;
			if (v.channel != 0) out.r += s;
			if (v.serial > newest) {
				newest = v.serial;
				out.env = w;
			}
			out.gate = true;
			v.phase += v.inc;
			if (v.phase >= 1.f)
				v.active = false;
		}
	}

private:
	bool pendingOnset = false;
	float pendingSince = 0.f;

	Voice* allocate() {
		Voice* oldest = &voices[0];
		for (int i = 0; i < POOL; i++) {
			if (!voices[i].active)
				return &voices[i];
			if (voices[i].serial < oldest->serial)
				oldest = &voices[i];
		}
		return oldest;
	}
};

} // namespace dividend

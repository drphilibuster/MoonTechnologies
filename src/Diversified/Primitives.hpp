#pragma once
/** Shared audio primitives for Diversified.
 *
 * Nothing in here knows what a program is. It is the delay lines, filters,
 * oscillators and non-linearities that both banks -- the DSP99 algorithm family
 * in Dsp99.hpp and the dedicated MiaW circuits in MiawFx.hpp -- are built from.
 *
 * Two rules the whole file obeys, because the module is one long feedback graph:
 *   - every recursive path is clamped or saturated, so nothing can run away;
 *   - every buffer is sized once, at construction, for 192 kHz. Nothing here
 *     allocates after init(), so process() never touches the heap.
 */
#include <rack.hpp>

#include <cmath>
#include <cstdint>
#include <algorithm>
#include <vector>

namespace divfx {

using namespace rack;

/** The rate every buffer is sized for. Rack's highest engine rate is 768 kHz on
    paper but 192 kHz in practice; a delay asked for more time than the buffer
    holds is clamped rather than wrapped, which reads as a shorter delay instead
    of as a crash. */
static const float MAX_SR = 192000.f;

/** Flushes NaNs and denormals. Cheap enough to sit on every feedback path, which
    is the only place a 106-program box can produce either. */
inline float sanitize(float x) {
	if (!std::isfinite(x))
		return 0.f;
	if (std::fabs(x) < 1e-25f)
		return 0.f;
	return x;
}

/** Bounded, monotonic tanh (Pade 7/6), clamped where the rational form would
    start to misbehave. Used wherever a feedback loop needs a soft ceiling. */
inline float tanhApprox(float x) {
	float x2 = x * x;
	if (x2 > 20.f)
		return (x > 0.f) ? 1.f : -1.f;
	float a = x * (135135.f + x2 * (17325.f + x2 * (378.f + x2)));
	float b = 135135.f + x2 * (62370.f + x2 * (3150.f + x2 * 28.f));
	return a / b;
}

/** Cubic soft clip normalised to unity gain at the origin and +-1 at the rails.
    Harder-cornered than tanh: this is the one the diode clippers want. */
inline float softClip(float x) {
	if (x <= -1.f)
		return -1.f;
	if (x >= 1.f)
		return 1.f;
	return 1.5f * (x - x * x * x / 3.f);
}

inline float lerp(float a, float b, float t) { return a + (b - a) * t; }

/** A ring buffer with a fractional read.
 *
 * Not power-of-two: a 1 s line at 192 kHz would round up to 262144 and the
 * module holds fourteen of these across two crossfade voices, so the wrap is a
 * compare rather than a mask and the memory is the memory actually asked for. */
struct DelayLine {
	std::vector<float> buf;
	int n;
	int w;

	DelayLine() : n(0), w(0) {}

	void init(int samples) {
		n = std::max(samples, 8);
		buf.assign((size_t) n, 0.f);
		w = 0;
	}
	void clear() {
		std::fill(buf.begin(), buf.end(), 0.f);
		w = 0;
	}
	int size() const { return n; }
	float maxDelay() const { return (float) (n - 2); }

	inline void write(float x) {
		buf[(size_t) w] = x;
		if (++w >= n)
			w = 0;
	}
	/** `d` samples back from the newest sample. Linear interpolation: at the
	    modulation depths used here the error sits far below the noise the
	    algorithms deliberately add. */
	inline float read(float d) const {
		if (d < 0.f)
			d = 0.f;
		float lim = maxDelay();
		if (d > lim)
			d = lim;
		int i = (int) d;
		float f = d - (float) i;
		int a = w - 1 - i;
		if (a < 0)
			a += n;
		int b = a - 1;
		if (b < 0)
			b += n;
		return buf[(size_t) a] + f * (buf[(size_t) b] - buf[(size_t) a]);
	}
	inline float readInt(int d) const {
		if (d < 0)
			d = 0;
		if (d > n - 1)
			d = n - 1;
		int a = w - 1 - d;
		if (a < 0)
			a += n;
		return buf[(size_t) a];
	}
	/** Absolute write-relative tap, for the reverse buffer. */
	inline float at(int i) const {
		i %= n;
		if (i < 0)
			i += n;
		return buf[(size_t) i];
	}
	inline void put(int i, float x) {
		i %= n;
		if (i < 0)
			i += n;
		buf[(size_t) i] = x;
	}
};

/** One-pole lowpass. The damping element in every comb, every feedback path and
    every tone control that does not need resonance. */
struct OnePole {
	float a, z;
	OnePole() : a(1.f), z(0.f) {}

	void setCutoff(float fc, float sr) {
		fc = clamp(fc, 1.f, sr * 0.45f);
		a = 1.f - std::exp(-2.f * (float) M_PI * fc / sr);
	}
	inline float lp(float x) { z += a * (x - z); return z; }
	inline float hp(float x) { return x - lp(x); }
	void clear() { z = 0.f; }
};

/** 12 Hz highpass. Anything with a diode, a rectifier or an asymmetric clipper
    in it leaves DC behind; this is what stops that reaching the output or, far
    worse, a feedback loop. */
struct DCBlocker {
	float x1, y1, r;
	DCBlocker() : x1(0.f), y1(0.f), r(0.999f) {}

	void setSampleRate(float sr) {
		r = 1.f - 2.f * (float) M_PI * 12.f / sr;
		r = clamp(r, 0.9f, 0.99999f);
	}
	inline float process(float x) {
		float y = x - x1 + r * y1;
		x1 = x;
		y1 = sanitize(y);
		return y1;
	}
	void clear() { x1 = y1 = 0.f; }
};

/** Topology-preserving state-variable filter (Zavalishin). Unconditionally
    stable at every cutoff and Q the panel can ask for, which a naive digital SVF
    is not -- and this module lets CV sweep both. */
struct Svf {
	float g, k, a1, a2, a3;
	float ic1, ic2;
	float lp, bp, hp;

	Svf() : g(0.f), k(1.f), a1(1.f), a2(0.f), a3(0.f),
	        ic1(0.f), ic2(0.f), lp(0.f), bp(0.f), hp(0.f) {}

	void set(float fc, float q, float sr) {
		fc = clamp(fc, 5.f, sr * 0.45f);
		q = clamp(q, 0.4f, 24.f);
		g = std::tan((float) M_PI * fc / sr);
		k = 1.f / q;
		a1 = 1.f / (1.f + g * (g + k));
		a2 = g * a1;
		a3 = g * a2;
	}
	inline void process(float v0) {
		float v3 = v0 - ic2;
		float v1 = a1 * ic1 + a2 * v3;
		float v2 = ic2 + a2 * ic1 + a3 * v3;
		ic1 = sanitize(2.f * v1 - ic1);
		ic2 = sanitize(2.f * v2 - ic2);
		lp = v2;
		bp = v1;
		hp = v0 - k * v1 - v2;
	}
	void clear() { ic1 = ic2 = 0.f; lp = bp = hp = 0.f; }
};

/** First-order allpass. Phaser stages, and the dispersion chain that makes a
    spring tank chirp rather than simply repeat. */
struct Ap1 {
	float a, z;
	Ap1() : a(0.f), z(0.f) {}

	void setFreq(float fc, float sr) {
		float t = std::tan((float) M_PI * clamp(fc, 5.f, sr * 0.45f) / sr);
		a = (t - 1.f) / (t + 1.f);
	}
	inline float process(float x) {
		float y = a * x + z;
		z = sanitize(x - a * y);
		return y;
	}
	void clear() { z = 0.f; }
};

/** Schroeder allpass: a delay wrapped in a lattice, the diffusion element of
    every reverb here. `dl` is in samples and may be modulated. */
struct Allpass {
	DelayLine d;
	float g, dl;
	Allpass() : g(0.5f), dl(1.f) {}

	void init(int samples) { d.init(samples); }
	void clear() { d.clear(); }
	inline float process(float x) {
		float y = d.read(dl);
		float v = x + g * y;
		d.write(sanitize(v));
		return y - g * v;
	}
};

/** Damped feedback comb, Freeverb's tank element. */
struct Comb {
	DelayLine d;
	OnePole damp;
	float fb, dl;
	Comb() : fb(0.5f), dl(100.f) {}

	void init(int samples) { d.init(samples); }
	void clear() { d.clear(); damp.clear(); }
	inline float process(float x) {
		float y = d.read(dl);
		d.write(sanitize(x + damp.lp(y) * fb));
		return y;
	}
};

/** Phase accumulator. Everything modulated in the module runs off one of these,
    so nothing has to own its own idea of what a cycle is. */
struct Lfo {
	float phase, inc;
	Lfo() : phase(0.f), inc(0.f) {}

	void setFreq(float hz, float sr) { inc = clamp(hz, 0.f, sr * 0.25f) / sr; }
	inline void step() {
		phase += inc;
		if (phase >= 1.f)
			phase -= 1.f;
	}
	inline float sine(float off = 0.f) const {
		float p = phase + off;
		p -= std::floor(p);
		return std::sin(2.f * (float) M_PI * p);
	}
	inline float tri(float off = 0.f) const {
		float p = phase + off;
		p -= std::floor(p);
		return 4.f * std::fabs(p - 0.5f) - 1.f;
	}
	void reset() { phase = 0.f; }
};

/** xorshift32. Deterministic per voice, so a program sounds the same twice. */
struct Rng {
	uint32_t s;
	Rng() : s(0x9e3779b9u) {}
	inline uint32_t u32() {
		s ^= s << 13;
		s ^= s >> 17;
		s ^= s << 5;
		return s;
	}
	inline float uni() { return (float) (u32() >> 8) * (1.f / 16777216.f); }
	inline float bi() { return uni() * 2.f - 1.f; }
};

/** One-pole parameter smoother. Delay times in particular must not step: a
    jumped read pointer is a click, and under CV it is a stream of them. */
struct Smooth {
	float v, a;
	Smooth() : v(0.f), a(1.f) {}

	void setTime(float ms, float sr) {
		a = 1.f - std::exp(-1.f / std::max(0.001f * ms * sr, 1.f));
	}
	inline float process(float target) { v += a * (target - v); return v; }
	void snap(float target) { v = target; }
};

/** Envelope follower with separate attack and release. */
struct Follower {
	float v, atk, rel;
	Follower() : v(0.f), atk(0.01f), rel(0.001f) {}

	void set(float atkMs, float relMs, float sr) {
		atk = 1.f - std::exp(-1.f / std::max(0.001f * atkMs * sr, 1.f));
		rel = 1.f - std::exp(-1.f / std::max(0.001f * relMs * sr, 1.f));
	}
	inline float process(float x) {
		float m = std::fabs(x);
		v += (m > v ? atk : rel) * (m - v);
		return v;
	}
	void clear() { v = 0.f; }
};

/** Schmitt comparator around an arbitrary threshold. The 4011 ring modulator's
    input stage, and the TAP input's edge detector. */
struct Comparator {
	bool state;
	Comparator() : state(false) {}

	inline bool process(float x, float lo, float hi) {
		if (state) {
			if (x < lo)
				state = false;
		}
		else {
			if (x > hi)
				state = true;
		}
		return state;
	}
	void clear() { state = false; }
};

} // namespace divfx

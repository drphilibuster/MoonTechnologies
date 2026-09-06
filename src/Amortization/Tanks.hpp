// The two reverb tanks behind Amortization, and the small parts they share.
//
// Everything in here works in normalised units (+-1.0 is +-5 V) at whatever
// sample rate it is told, and allocates only in setSampleRate(), which the
// module calls from its constructor and from onSampleRateChange() -- both run
// while the engine is not calling process() on this module (Engine.cpp holds
// its write lock around every onSampleRateChange dispatch).
#pragma once
#include <rack.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace amortization {

using namespace rack;

/** Interpolation used for every fractional delay read. */
enum Interp { INTERP_LINEAR = 0, INTERP_CUBIC = 1 };

/** A power-of-two ring buffer read at a fractional delay.
    Read before write: read(d) is the sample written d samples ago. */
struct DelayLine {
	std::vector<float> buf;
	uint32_t mask = 0;
	uint32_t w = 0;

	void resize(size_t maxSamples) {
		size_t n = 64;
		while (n < maxSamples + 8)
			n <<= 1;
		buf.assign(n, 0.f);
		mask = (uint32_t)(n - 1);
		w = 0;
	}

	void clear() {
		std::fill(buf.begin(), buf.end(), 0.f);
		w = 0;
	}

	float maxDelay() const { return (float)(mask - 3); }

	void write(float x) {
		buf[w] = x;
		w = (w + 1) & mask;
	}

	float at(uint32_t back) const { return buf[(w - back) & mask]; }

	float read(float d, Interp interp) const {
		d = clamp(d, 2.f, maxDelay());
		uint32_t i = (uint32_t)d;
		float f = d - (float)i;
		float y1 = at(i);
		float y2 = at(i + 1);
		if (interp == INTERP_LINEAR)
			return y1 + f * (y2 - y1);
		// 4-point Hermite on the samples either side of the read point.
		float y0 = at(i - 1);
		float y3 = at(i + 2);
		float c1 = 0.5f * (y2 - y0);
		float c2 = y0 - 2.5f * y1 + 2.f * y2 - 0.5f * y3;
		float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
		return ((c3 * f + c2) * f + c1) * f + y1;
	}
};

/** Schroeder allpass around a delay line. Positive g is the "input diffusion"
    orientation of Dattorro's paper; the tank's first pair takes a negative g. */
inline float allpass(DelayLine& dl, float x, float d, float g, Interp interp) {
	float v = dl.read(d, interp);
	float node = x - g * v;
	dl.write(node);
	return g * node + v;
}

/** One-pole tilt: splits at the pivot, weights the halves, sums them back.
    With gLo == gHi == 1 it is transparent. */
struct Tilt {
	float lp = 0.f;
	float process(float x, float k, float gLo, float gHi) {
		lp += k * (x - lp);
		return gLo * lp + gHi * (x - lp);
	}
	void reset() { lp = 0.f; }
};

/** DC blocker for a feedback loop. */
struct DcBlock {
	float x1 = 0.f, y1 = 0.f;
	float process(float x, float r) {
		float y = x - x1 + r * y1;
		x1 = x;
		y1 = y;
		return y;
	}
	void reset() { x1 = y1 = 0.f; }
};

/** Odd, monotonic, unit slope at zero, saturates at +-1. Cheap tanh stand-in. */
inline float softClip(float x) {
	x = clamp(x, -3.f, 3.f);
	return x * (27.f + x * x) / (27.f + 9.f * x * x);
}

/** softClip scaled so the knee sits at +-t rather than +-1. */
inline float softLimit(float x, float t) {
	return t * softClip(x / t);
}

/** Cutoff to one-pole coefficient. */
inline float onePoleK(float hz, float sr) {
	return 1.f - std::exp(-2.f * (float)M_PI * hz / sr);
}

/** Dattorro's four input-diffusion allpasses. The tank gets a smeared version
    of the input rather than the input itself, so the first reflections do not
    read as discrete echoes. Lengths are the paper's at 29761 Hz; `stretch`
    lets a second instance sit at slightly different lengths so a stereo pair
    does not diffuse identically. */
struct Diffuser {
	static constexpr float REF_RATE = 29761.f;
	DelayLine ap[4];
	float scale = 1.f;
	float stretch = 1.f;

	static const float* lengths() {
		static const float L[4] = {142.f, 107.f, 379.f, 277.f};
		return L;
	}

	void setSampleRate(float sr, float stretchIn) {
		scale = sr / REF_RATE;
		stretch = stretchIn;
		const float* L = lengths();
		for (int i = 0; i < 4; i++)
			ap[i].resize((size_t)(L[i] * scale * stretch) + 16);
		reset();
	}

	void reset() {
		for (int i = 0; i < 4; i++)
			ap[i].clear();
	}

	float process(float x, Interp interp) {
		const float* L = lengths();
		const float s = scale * stretch;
		x = allpass(ap[0], x, L[0] * s, 0.75f, interp);
		x = allpass(ap[1], x, L[1] * s, 0.75f, interp);
		x = allpass(ap[2], x, L[2] * s, 0.625f, interp);
		x = allpass(ap[3], x, L[3] * s, 0.625f, interp);
		return x;
	}
};

/** A sine LFO for the modulated allpasses. Phase in turns. */
struct Lfo {
	float phase = 0.f;
	float step(float hz, float sampleTime) {
		phase += hz * sampleTime;
		if (phase >= 1.f)
			phase -= 1.f;
		return std::sin(2.f * (float)M_PI * phase);
	}
	void reset() { phase = 0.f; }
};


// ---------------------------------------------------------------------------
/** VERB: Dattorro's plate ("Effect Design Part 1", JAES 1997), the Griesinger
    figure-eight tank. Four input diffusers, then two halves that feed each
    other: a modulated allpass, a long delay, damping and decay, a second
    allpass, another delay, and across. Stereo comes from tapping the tank at
    the paper's fourteen points. Lengths are the paper's, at its 29761 Hz, and
    are scaled to the running rate and to SIZE. */
struct VerbTank {
	static constexpr float REF_RATE = 29761.f;
	static constexpr float SIZE_CEIL = 2.f;

	DelayLine ap1L, d1L, ap2L, d2L;
	DelayLine ap1R, d1R, ap2R, d2R;
	Tilt tiltL, tiltR;
	DcBlock dcL, dcR;

	float scale = 1.f;        // samples per paper sample
	float dcR_ = 0.999f;
	float tiltK = 0.1f;
	float lastPeak = 0.f;     // largest |loop signal| before the limiter, last call

	void setSampleRate(float sr) {
		scale = sr / REF_RATE;
		float m = scale * SIZE_CEIL;
		ap1L.resize((size_t)(672.f * m + 64.f * scale));
		d1L.resize((size_t)(4453.f * m));
		ap2L.resize((size_t)(1800.f * m));
		d2L.resize((size_t)(3720.f * m));
		ap1R.resize((size_t)(908.f * m + 64.f * scale));
		d1R.resize((size_t)(4217.f * m));
		ap2R.resize((size_t)(2656.f * m));
		d2R.resize((size_t)(3163.f * m));
		dcR_ = 1.f - 2.f * (float)M_PI * 10.f / sr;
		tiltK = onePoleK(1000.f, sr);
		reset();
	}

	void reset() {
		ap1L.clear(); d1L.clear(); ap2L.clear(); d2L.clear();
		ap1R.clear(); d1R.clear(); ap2R.clear(); d2R.clear();
		tiltL.reset(); tiltR.reset();
		dcL.reset(); dcR.reset();
		lastPeak = 0.f;
	}

	/** Largest |loop signal| on the last call, for the LIMIT light. */
	float peak() const { return lastPeak; }

	/** Mean loop length in seconds, for the decay-time read-out. */
	float loopSeconds(float size) const {
		return (4453.f + 3720.f + 4217.f + 3163.f) / 2.f / REF_RATE * size;
	}

	/** x: diffused mono input. decay: per-half loop gain, <= 1. mod: the two
	    modulated allpasses' excursion in paper samples. gLo/gHi: loop tilt. */
	void process(float x, float decay, float size, float modL, float modR,
	             float gLo, float gHi, Interp interp, float& outL, float& outR) {
		const float s = scale * size;

		// Each half is fed by the far end of the other.
		float fromR = d2R.read(3163.f * s, interp);
		float fromL = d2L.read(3720.f * s, interp);

		// Left half.
		float l = allpass(ap1L, x + fromR, 672.f * s + modL * scale, -0.7f, interp);
		float lv = d1L.read(4453.f * s, interp);
		d1L.write(l);
		lv = tiltL.process(lv, tiltK, gLo, gHi);
		lv = dcL.process(lv, dcR_);
		lv = softLimit(lv * decay, 3.f);
		float l2 = allpass(ap2L, lv, 1800.f * s, 0.5f, interp);
		d2L.write(l2);

		// Right half.
		float r = allpass(ap1R, x + fromL, 908.f * s + modR * scale, -0.7f, interp);
		float rv = d1R.read(4217.f * s, interp);
		d1R.write(r);
		rv = tiltR.process(rv, tiltK, gLo, gHi);
		rv = dcR.process(rv, dcR_);
		lastPeak = std::fmax(std::fabs(lv * decay), std::fabs(rv * decay));
		rv = softLimit(rv * decay, 3.f);
		float r2 = allpass(ap2R, rv, 2656.f * s, 0.5f, interp);
		d2R.write(r2);

		// The paper's output taps.
		outL = 0.6f * (d1R.read(266.f * s, interp) + d1R.read(2974.f * s, interp)
		               - ap2R.read(1913.f * s, interp) + d2R.read(1996.f * s, interp)
		               - d1L.read(1990.f * s, interp) - ap2L.read(187.f * s, interp)
		               - d2L.read(1066.f * s, interp));
		outR = 0.6f * (d1L.read(353.f * s, interp) + d1L.read(3627.f * s, interp)
		               - ap2L.read(1228.f * s, interp) + d2L.read(2673.f * s, interp)
		               - d1R.read(2111.f * s, interp) - ap2R.read(335.f * s, interp)
		               - d2R.read(121.f * s, interp));
	}
};


// ---------------------------------------------------------------------------
/** TRONIC: an eight-line feedback delay network with a Householder reflection
    for its mixing matrix. The reflection is orthogonal, so the only loss in the
    loop is the feedback gain and the tilt -- and the feedback gain is allowed
    past unity, with a soft limiter on every line to keep "relentless" from
    becoming "exploded". Line lengths are primes at 48 kHz, 35-114 ms, so no two
    lines share a period and the tail smears rather than rings. */
struct TronicTank {
	static const int N = 8;
	static constexpr float REF_RATE = 48000.f;
	static constexpr float SIZE_CEIL = 2.f;

	DelayLine line[N];
	Tilt tilt[N];
	DcBlock dc[N];
	float lastOut[N];

	float scale = 1.f;
	float dcR_ = 0.999f;
	float tiltK = 0.1f;

	static const float* lengths() {
		static const float L[N] = {1699.f, 2003.f, 2371.f, 2803.f, 3313.f, 3917.f, 4637.f, 5471.f};
		return L;
	}

	TronicTank() {
		for (int i = 0; i < N; i++)
			lastOut[i] = 0.f;
	}

	void setSampleRate(float sr) {
		scale = sr / REF_RATE;
		const float* L = lengths();
		for (int i = 0; i < N; i++)
			line[i].resize((size_t)(L[i] * scale * SIZE_CEIL + 64.f * scale));
		dcR_ = 1.f - 2.f * (float)M_PI * 10.f / sr;
		tiltK = onePoleK(1000.f, sr);
		reset();
	}

	void reset() {
		for (int i = 0; i < N; i++) {
			line[i].clear();
			tilt[i].reset();
			dc[i].reset();
			lastOut[i] = 0.f;
		}
	}

	/** Mean line length in seconds, for the decay-time read-out. */
	float loopSeconds(float size) const {
		const float* L = lengths();
		float sum = 0.f;
		for (int i = 0; i < N; i++)
			sum += L[i];
		return sum / N / REF_RATE * size;
	}

	/** Largest |line output| on the last call, for the LIMIT light. */
	float peak() const {
		float p = 0.f;
		for (int i = 0; i < N; i++)
			p = std::fmax(p, std::fabs(lastOut[i]));
		return p;
	}

	/** xL/xR: diffused input. g: feedback gain, may exceed 1. mod[4]: excursion
	    in reference samples for the odd lines. limit: the limiter knee. */
	void process(float xL, float xR, float g, float size, const float* mod,
	             float gLo, float gHi, float limit, Interp interp,
	             float& outL, float& outR) {
		const float s = scale * size;
		const float* L = lengths();

		float v[N];
		float sum = 0.f;
		for (int i = 0; i < N; i++) {
			float d = L[i] * s;
			if (i & 1)
				d += mod[i >> 1] * scale;
			float y = line[i].read(d, interp);
			lastOut[i] = y;
			y = tilt[i].process(y, tiltK, gLo, gHi);
			y = dc[i].process(y, dcR_);
			y = softLimit(y * g, limit);
			v[i] = y;
			sum += y;
		}

		// Householder: H = I - (2/N) 1 1^T. One subtraction per line.
		const float f = 2.f / N * sum;
		static const float inSign[N] = {1.f, 1.f, -1.f, -1.f, 1.f, 1.f, -1.f, -1.f};
		for (int i = 0; i < N; i++) {
			float in = (i & 1) ? xR : xL;
			line[i].write(v[i] - f + inSign[i] * in * 0.5f);
		}

		// Even lines left, odd lines right, alternating polarity so the sum does
		// not pile up at DC.
		outL = 0.4f * (lastOut[0] - lastOut[2] + lastOut[4] - lastOut[6]);
		outR = 0.4f * (lastOut[1] - lastOut[3] + lastOut[5] - lastOut[7]);
	}
};

} // namespace amortization

#pragma once
/** Obfuscation's DSP: a three-band allpass matrix, tested without Rack.
 *
 *   in -> 3-band LR4 split -> per band: up to 96 first-order allpass stages
 *   inside one feedback loop -> sum -> saturator (2x oversampled) -> 2-band
 *   split for BRIGHT -> hard clip -> boost -> out
 *
 * No Rack types, no allocation, no I/O. Audio is +-1 inside (Rack's +-5 V is
 * scaled by the module). Every recursive path is bounded by a tanh or by
 * |a| < 1, and sanitize() sits on the loop memory, so nothing can run away.
 */
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace obf {

static const int MAX_STAGES = 96;
static const int CTRL_DIV = 32;     // coefficient update interval, samples
static const float PI_F = 3.14159265358979f;

inline float sanitize(float x) {
	if (!std::isfinite(x)) return 0.f;
	if (std::fabs(x) < 1e-25f) return 0.f;
	return x;
}
inline float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }

struct Rng {
	uint32_t s;
	Rng() : s(0x9E3779B9u) {}
	void seed(uint32_t v) { s = v ? v : 0x9E3779B9u; }
	uint32_t u32() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
	float bi() { return (float)(u32() >> 8) * (2.f / 16777216.f) - 1.f; }
};

/** Zavalishin TPT state-variable filter, Butterworth (k = sqrt 2). */
struct Svf {
	float g = 0.f, a1 = 1.f, a2 = 0.f, a3 = 0.f, ic1 = 0.f, ic2 = 0.f;
	static constexpr float K = 1.41421356f;
	void set(float fc, float sr) {
		fc = clampf(fc, 5.f, 0.45f * sr);
		g = std::tan(PI_F * fc / sr);
		a1 = 1.f / (1.f + g * (g + K));
		a2 = g * a1;
		a3 = g * a2;
	}
	void reset() { ic1 = ic2 = 0.f; }
	/** lp and hp of one 2nd-order Butterworth section. */
	void run(float x, float& lp, float& hp) {
		float v3 = x - ic2;
		float v1 = a1 * ic1 + a2 * v3;
		float v2 = ic2 + a2 * ic1 + a3 * v3;
		ic1 = 2.f * v1 - ic1;
		ic2 = 2.f * v2 - ic2;
		lp = v2;
		hp = x - K * v1 - v2;
	}
};

/** Linkwitz-Riley 4: two Butterworth sections per leg. lp + hp is an allpass. */
struct LR4 {
	Svf lp1, lp2, hp1, hp2;
	void set(float fc, float sr) { lp1.set(fc, sr); lp2.set(fc, sr); hp1.set(fc, sr); hp2.set(fc, sr); }
	void reset() { lp1.reset(); lp2.reset(); hp1.reset(); hp2.reset(); }
	void run(float x, float& lo, float& hi) {
		float l, h, d;
		lp1.run(x, l, d); lp2.run(l, lo, d);
		hp1.run(x, d, h); hp2.run(h, d, hi);
	}
	/** The allpass this split makes of its input, for phase-matching a band. */
	float allpass(float x) { float l, h; run(x, l, h); return l + h; }
};

/** N first-order allpasses inside one feedback loop. */
struct Chain {
	float a[MAX_STAGES], z[MAX_STAGES];
	float off[MAX_STAGES], tgt[MAX_STAGES];   // per-stage cutoff offset, -1..1
	float fbOff = 0.f, fbTgt = 0.f;           // per-band feedback offset
	float nSm = 8.f, yPrev = 0.f;
	Chain() { reset(); }
	void reset() {
		for (int i = 0; i < MAX_STAGES; i++) a[i] = z[i] = off[i] = tgt[i] = 0.f;
		fbOff = fbTgt = 0.f; yPrev = 0.f;
	}
	int active() const { return (int)clampf(std::ceil(nSm), 1.f, (float)MAX_STAGES); }
	float process(float x, float fb, float inGain) {
		int k = active();
		float frac = nSm - (float)(k - 1);
		frac = clampf(frac, 0.f, 1.f);
		float v = x * inGain + fb * std::tanh(yPrev);
		float before = v;
		for (int i = 0; i < k; i++) {
			if (i == k - 1) before = v;
			float y = a[i] * v + z[i];
			z[i] = v - a[i] * y;
			v = y;
		}
		float out = sanitize(before + frac * (v - before));
		yPrev = out;
		return out * (1.f - 0.5f * fb);
	}
};

struct Params {
	float freq = 0.5f;      // 0..1, 40 Hz .. 8 kHz exponential
	float res = 0.3f;       // 0..1 loop feedback
	float stages = 8.f;     // 1..96
	float spread = 0.f;     // 0..1 per-stage cutoff spread / random depth
	float drive = 0.2f;     // 0..1
	float bright = 0.5f;    // 0..1, 0.5 neutral
	float clip = 1.f;       // 0..1, 1 = no clip
	float boost = 0.f;      // 0..1 -> 0..+24 dB
};

struct Matrix {
	float sr = 48000.f;
	LR4 xo1, xo2, xoComp, bright;
	Chain band[3];
	Svf osUp, osUp2, osDn, osDn2;
	float dcX = 0.f, dcY = 0.f;
	float hold = 0.f, env = 0.f, envArm = 1.f, refractory = 0.f;
	float xoLoF = 250.f, xoHiF = 2500.f;
	int ctr = 0;
	bool randWas = false, gateWas = false;
	Rng rng;
	float fired = 0.f;       // decaying flag for the module's LED

	Matrix() { setRate(48000.f); reset(); }
	void setRate(float s) { sr = s; xo1.set(xoLoF, sr); xo2.set(xoHiF, sr); xoComp.set(xoHiF, sr); bright.set(3000.f, sr);
		osUp.set(0.4f * sr, 2.f * sr); osUp2.set(0.4f * sr, 2.f * sr); osDn.set(0.4f * sr, 2.f * sr); osDn2.set(0.4f * sr, 2.f * sr); }
	void reset() {
		xo1.reset(); xo2.reset(); xoComp.reset(); bright.reset();
		for (int b = 0; b < 3; b++) band[b].reset();
		osUp.reset(); osUp2.reset(); osDn.reset(); osDn2.reset();
		dcX = dcY = 0.f; hold = env = 0.f; envArm = 1.f; refractory = 0.f; ctr = 0; fired = 0.f;
		randWas = gateWas = false;
		rng.seed(0x1234567u);
		for (int b = 0; b < 3; b++) ladder(b);
		for (int b = 0; b < 3; b++) for (int i = 0; i < MAX_STAGES; i++) band[b].off[i] = band[b].tgt[i];
	}
	void seed(uint32_t v) { rng.seed(v); }

	/** The fixed offsets used with randomisation off: a golden-ratio ladder, so
	    SPREAD alone gives a repeatable, even comb of stage cutoffs. */
	void ladder(int b) {
		for (int i = 0; i < MAX_STAGES; i++) {
			float p = std::fmod((float)(i + 1 + 17 * b) * 0.6180339887f, 1.f);
			band[b].tgt[i] = 2.f * p - 1.f;
		}
		band[b].fbTgt = 0.f;
	}
	void roll() {
		for (int b = 0; b < 3; b++) {
			for (int i = 0; i < MAX_STAGES; i++) band[b].tgt[i] = rng.bi();
			band[b].fbTgt = 0.5f * rng.bi();
		}
		fired = 1.f;
	}

	static float freqHz(float k) { return 40.f * std::exp2(clampf(k, 0.f, 1.f) * 7.64f); }

	void updateCoeffs(const Params& p) {
		static const float bandMul[3] = {0.25f, 1.f, 4.f};
		float f0 = freqHz(p.freq);
		float target = clampf(p.stages, 1.f, (float)MAX_STAGES);
		for (int b = 0; b < 3; b++) {
			Chain& c = band[b];
			float fb = f0 * bandMul[b];
			for (int i = 0; i < MAX_STAGES; i++) {
				c.off[i] += (c.tgt[i] - c.off[i]) * 0.05f;
				float f = fb * std::exp2(p.spread * 2.f * c.off[i]);
				f = clampf(f, 10.f, 0.45f * sr);
				float t = std::tan(PI_F * f / sr);
				c.a[i] = (t - 1.f) / (t + 1.f);
			}
			c.fbOff += (c.fbTgt - c.fbOff) * 0.05f;
			c.nSm += (target - c.nSm) * 0.1f;
		}
	}

	/** x in +-1. clockEdge: rising edge of the clock jack. gate: RANDOM gate level.
	    randSw: RANDOM switch. envMode: peak follower drives the roll instead of the clock. */
	float process(float x, const Params& p, bool clockEdge, bool gate, bool randSw, bool envMode) {
		x = sanitize(x);
		bool randOn = randSw || gate;

		// peak-sensing trigger
		float ax = std::fabs(x);
		env = ax > env ? ax : env + (ax - env) * (1.f / (0.05f * sr));
		if (refractory > 0.f) refractory -= 1.f;
		bool envEdge = false;
		if (envArm > 0.5f && env > 0.2f && refractory <= 0.f) { envEdge = true; envArm = 0.f; refractory = 0.02f * sr; }
		if (env < 0.1f) envArm = 1.f;

		bool gateEdge = gate && !gateWas;
		bool trig = randOn && ((envMode ? envEdge : clockEdge) || gateEdge);
		if (trig) roll();
		if (!randOn && randWas) for (int b = 0; b < 3; b++) ladder(b);
		randWas = randOn;
		gateWas = gate;

		if (ctr == 0) updateCoeffs(p);
		ctr = (ctr + 1) % CTRL_DIV;
		if (fired > 0.f) fired -= 1.f / (0.1f * sr);

		float holdT = gate ? 1.f : 0.f;
		hold += (holdT - hold) * (1.f / (0.005f * sr));

		// split
		float lo1, hi1, mid, hi;
		xo1.run(x, lo1, hi1);
		xo2.run(hi1, mid, hi);
		float lo = xoComp.allpass(lo1);

		float bands[3] = {lo, mid, hi};
		float sum = 0.f;
		for (int b = 0; b < 3; b++) {
			float fb = clampf(p.res * 0.97f + band[b].fbOff * p.spread, 0.f, 0.99f);
			fb = fb + (0.995f - fb) * hold;
			sum += band[b].process(bands[b], fb, 1.f - hold);
		}

		// saturator, 2x oversampled
		float d = 1.f + 19.f * p.drive;
		float td = std::tanh(d);
		float y = 0.f;
		for (int s = 0; s < 2; s++) {
			float u = (s == 0 ? 2.f * sum : 0.f), l1, l2, h;
			osUp.run(u, l1, h); osUp2.run(l1, l2, h);
			float sat = std::tanh(d * l2) / td;
			osDn.run(sat, l1, h); osDn2.run(l1, l2, h);
			if (s == 0) y = l2;
		}

		// brightness: +-12 dB shelf-ish via LR4 split
		float bl, bh;
		bright.run(y, bl, bh);
		y = bl + bh * std::pow(10.f, (p.bright - 0.5f) * 24.f / 20.f);

		// dc block
		float dc = y - dcX + 0.9995f * dcY; dcX = y; dcY = sanitize(dc); y = dcY;

		// clip then boost
		float th = 0.1f + 0.9f * p.clip;
		y = clampf(y, -th, th);
		y *= std::pow(10.f, p.boost * 24.f / 20.f);
		return clampf(sanitize(y), -2.4f, 2.4f);
	}
};

} // namespace obf

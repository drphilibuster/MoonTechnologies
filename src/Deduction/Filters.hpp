#pragma once
// The six filter models behind Deduction, one voice each. Pure DSP: no Rack
// types, no allocation, no I/O. Signals are normalised so that 1.0 is 5 V,
// which puts every tanh knee where the original's diodes and transistors have
// theirs. Every model is topology-preserving (TPT / zero-delay), so the only
// per-sample trig is the one tan() that turns a cutoff into a gain, and every
// nonlinearity is bounded, so nothing here can leave the state non-finite.
//
// Coefficient vocabulary, shared by every model:
//   g  = tan(pi * fc / fs)      the pre-warped integrator gain
//   G  = g / (1 + g)            a one-pole's instantaneous gain
//   H  = 1 - G                  its high-pass complement
// A one-pole with state s then reads  lp = G*x + H*s,  hp = H*(x - s),  which is
// what lets a feedback loop be solved in closed form before the states move.

#include <cmath>

namespace deduction {

static const float kPi = 3.14159265358979f;
static const int NUM_MODELS = 6;
enum Model { PAIA = 0, QD, KORG35, MS20, EFM, DIRT };

/** Cheap, exact-at-the-ends tanh: the (27 + x^2) / (27 + 9 x^2) Pade form,
    which reaches exactly 1 at |x| = 3, so the clamp is continuous. */
inline float ftanh(float x) {
	if (x >  3.f) return  1.f;
	if (x < -3.f) return -1.f;
	float x2 = x * x;
	return x * (27.f + x2) / (27.f + 9.f * x2);
}

/** A single-ended transistor stage: soft into cutoff on one side, harder and
    lower against the rail on the other. The even harmonics are the point. */
inline float transistorClip(float x) {
	return x >= 0.f ? ftanh(x * 0.8f) * 1.25f : ftanh(x * 1.6f) * 0.625f;
}

/** A diode pair with a hard knee at +-t: near-linear inside, then flat. */
inline float diodeClip(float x, float t) {
	float u = x / t;
	float u2 = u * u;
	// Fourth-order rational: |u| / (1 + u^4)^(1/4), a much squarer knee than tanh.
	return t * u / std::sqrt(std::sqrt(1.f + u2 * u2));
}

/** The per-sample control set every model reads. */
struct Coeffs {
	float g = 0.f;        // tan(pi fc / fs)
	float G = 0.f;        // g / (1 + g)
	float res = 0.f;      // 0 .. 1
	float drive = 0.5f;   // 0 .. 1, meaning per model
};

/** TPT one-pole. `lp` advances the state; the static forms only read it. */
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

/** Output DC block, a one-pole high-pass around 5 Hz. */
struct DcBlock {
	OnePole p;
	float G = 0.f;
	void setRate(float fs) {
		float g = std::tan(kPi * 5.f / fs);
		G = g / (1.f + g);
	}
	void reset() { p.reset(); }
	inline float process(float x) { return p.hp(x, G); }
};


// ---------------------------------------------------------------------------
// PAiA 2720-3L. One BC549 common-emitter stage with a twin-T in its feedback
// (the R-C-R arm fixed, the C-R-C arm's shunt a 1N4148 whose dynamic
// resistance the control current sets) and an emitter follower out through a
// coupling cap. Detuned as the L board is, the network makes the stage a
// 2-pole low-pass with a modest peak and no resonance control; the transistor
// clips single-endedly, and cutoff follows diode current, i.e. control voltage,
// linearly. DRIVE is the input level into the stage. RES here widens the peak
// the original has fixed; it never reaches self-oscillation.
struct Paia {
	float ic1 = 0.f, ic2 = 0.f;
	DcBlock dc;
	float hot = 0.f;

	void reset() { ic1 = ic2 = 0.f; dc.reset(); }
	void setRate(float fs) { dc.setRate(fs); }

	inline float process(float x, const Coeffs& c) {
		float gain = std::exp2((c.drive - 0.5f) * 6.f);          // 1/8 .. 8
		float u = x * gain;
		hot = std::fabs(u);
		u = transistorClip(u);

		// Damping from 1.4 (the board's own gentle peak) down to 0.3: Q up to ~3.
		float k = 1.4f - 1.1f * c.res;
		float g = c.g;
		float a1 = 1.f / (1.f + g * (g + k));
		float a2 = g * a1;
		float a3 = g * a2;
		float v3 = u - ic2;
		float v1 = a1 * ic1 + a2 * v3;
		float v2 = ic2 + a2 * ic1 + a3 * v3;
		ic1 = 2.f * v1 - ic1;
		ic2 = 2.f * v2 - ic2;
		return dc.process(v2);
	}
};


// ---------------------------------------------------------------------------
// Escobedo Q&D VCF. A 2-pole state-variable low-pass whose resonance path runs
// through an asymmetric soft clipper, so the peak folds over rather than
// screaming, and a dirt stage in front. Solved with full damping and the
// resonance re-injected as a bounded positive term, so it is stable at every
// setting; the clipper's argument is the linear prediction for this sample.
struct Qd {
	float ic1 = 0.f, ic2 = 0.f;
	DcBlock dc;
	float hot = 0.f;

	void reset() { ic1 = ic2 = 0.f; dc.reset(); }
	void setRate(float fs) { dc.setRate(fs); }

	static inline float sat(float x) {
		return x >= 0.f ? ftanh(x) : 1.3f * ftanh(x / 1.3f);
	}

	inline float process(float x, const Coeffs& c) {
		float gain = std::exp2((c.drive - 0.5f) * 6.f);
		float u = x * gain;
		hot = std::fabs(u);
		u = sat(u);

		float g = c.g;
		float R = 2.f * c.res;                    // positive feedback, self-osc at 2
		// Linear prediction of the band-pass term at the reduced damping.
		float kl = std::fmax(2.f - R, 0.02f);
		float b1 = 1.f / (1.f + g * (g + kl));
		float b2 = g * b1;
		float v3 = u - ic2;
		float bpPred = b1 * ic1 + b2 * v3;
		// Then the real step, fully damped, with the clipped resonance added in.
		float fb = R * sat(bpPred);
		float xin = u + fb;
		float a1 = 1.f / (1.f + g * (g + 2.f));
		float a2 = g * a1;
		float a3 = g * a2;
		v3 = xin - ic2;
		float v1 = a1 * ic1 + a2 * v3;
		float v2 = ic2 + a2 * ic1 + a3 * v3;
		ic1 = 2.f * v1 - ic1;
		ic2 = 2.f * v2 - ic2;
		return dc.process(v2);
	}
};


// ---------------------------------------------------------------------------
// Korg35 / MS-20. Two one-poles with a saturating positive-feedback amplifier
// round the second: LP1 -> sum -> LP2 -> out, with out -> HP -> K -> sum. The
// loop is a band-pass with peak gain K/2, so K = 2 is the edge of
// self-oscillation and the amplifier's clip is what holds it there. The HP IN
// path is the mirror image: HP1 -> sum -> HP2 -> out, with out -> LP -> K -> sum.
// The Korg35 amplifier is a soft tanh; the MS-20 OTA version clips against a
// diode pair whose threshold DRIVE sets. Both paths share the loop solver.
struct Korg {
	bool ms20 = false;
	OnePole l1, l2, l3;     // LP path: LP1, LP2, feedback HP
	OnePole h1, h2, h3;     // HP path: HP1, HP2, feedback LP
	float hot = 0.f;

	void reset() { l1.reset(); l2.reset(); l3.reset(); h1.reset(); h2.reset(); h3.reset(); }
	void setRate(float) {}

	inline float clip(float x, float thr) const {
		return ms20 ? diodeClip(x, thr) : ftanh(x);
	}

	inline float process(float lpIn, float hpIn, bool useHp, const Coeffs& c) {
		float G = c.G, H = 1.f - G;
		float K = c.res * (ms20 ? 2.3f : 2.05f);
		float den = 1.f / (1.f - K * G * H);         // K*G*H <= 0.575: never singular
		float thr = 1.f, gain = 1.f;
		if (ms20) thr = std::exp2((0.5f - c.drive) * 4.f);   // 4 .. 1/4
		else      gain = std::exp2((c.drive - 0.5f) * 6.f);

		float out = 0.f;

		// --- low-pass path ---------------------------------------------------
		{
			float u = lpIn * gain;
			hot = std::fabs(u);
			u = ftanh(u);
			float y1 = l1.lp(u, G);
			// Closed-form loop, then the clip on the predicted feedback.
			float v = (y1 + K * H * (H * l2.s - l3.s)) * den;
			float y2p = G * v + H * l2.s;
			float yhp = H * (y2p - l3.s);
			float fb = clip(K * yhp, thr);
			if (ms20) hot = std::fmax(hot, std::fabs(K * yhp / thr));
			v = y1 + fb;
			float y2 = l2.lp(v, G);
			l3.lp(y2, G);
			out = y2;
		}

		// --- high-pass path --------------------------------------------------
		if (useHp) {
			float u = ftanh(hpIn * gain);
			float x1 = h1.hp(u, G);
			float v = (x1 + K * H * (h3.s - G * h2.s)) * den;
			float yp = H * (v - h2.s);
			float ylp = G * yp + H * h3.s;
			float fb = clip(K * ylp, thr);
			v = x1 + fb;
			float y = h2.hp(v, G);
			h3.lp(y, G);
			out += y;
		}
		return out;
	}
};


// ---------------------------------------------------------------------------
// EFM Moog-type high-pass. A four-pole ladder of one-pole high-passes with
// tanh feedback: each stage leads 45 degrees at the corner, so the loop is in
// phase there with gain 1/4, and K = 4 is self-oscillation. The passband loses
// 1/(1+K) as resonance rises, as a ladder does. DRIVE is the level into the
// ladder's input pair.
struct Efm {
	OnePole s[4];
	float hot = 0.f;

	void reset() { for (int i = 0; i < 4; i++) s[i].reset(); }
	void setRate(float) {}

	inline float process(float x, const Coeffs& c) {
		float G = c.G, H = 1.f - G;
		float K = c.res * 4.1f;
		float gain = std::exp2((c.drive - 0.5f) * 6.f);
		float xg = x * gain;
		hot = std::fabs(xg);

		float H2 = H * H, H3 = H2 * H, H4 = H3 * H;
		float sum = H4 * s[0].s + H3 * s[1].s + H2 * s[2].s + H * s[3].s;
		// Linear prediction of this sample's output, then the real feedback.
		float yPred = (H4 * xg - sum) / (1.f + K * H4);
		float u = ftanh(xg - K * ftanh(yPred));
		float y = u;
		for (int i = 0; i < 4; i++)
			y = s[i].hp(y, G);
		return y;
	}
};


// ---------------------------------------------------------------------------
// Synthrotek DIRT. Two CMOS inverter stages, each with an RC in its feedback
// so it is a one-pole low-pass with a steep, bounded transfer, and RES feeding
// the pair's output back to the input. BIAS moves both inverters' operating
// point up or down the transfer: at the centre the gain is highest, towards
// either rail the slope goes to nothing and the signal is choked to silence.
// Two inverting stages make the feedback positive; a cap in that path keeps it
// from latching, so at the top of RES it squeals rather than sticks.
struct Dirt {
	OnePole p1, p2, fbHp;
	float fbG = 0.f;
	float y2 = 0.f;
	DcBlock dc;
	float hot = 0.f;

	void reset() { p1.reset(); p2.reset(); fbHp.reset(); y2 = 0.f; dc.reset(); }
	void setRate(float fs) {
		dc.setRate(fs);
		float g = std::tan(kPi * 30.f / fs);
		fbG = g / (1.f + g);
	}

	/** One inverter about operating point B: the slope at the point is what the
	    signal gets; the DC it sits on is removed so the stages can chain. */
	static inline float inverter(float u, float B) {
		return -(ftanh(3.f * u + B) - ftanh(B));
	}

	inline float process(float x, const Coeffs& c) {
		float B = (c.drive - 0.5f) * 5.f;
		float fb = c.res * 1.4f * fbHp.hp(y2, fbG);
		float u1 = p1.lp(x + fb, c.G);
		hot = std::fabs(3.f * u1 + B);
		float y1 = inverter(u1, B);
		float u2 = p2.lp(y1, c.G);
		y2 = inverter(u2, B);
		return dc.process(y2);
	}
};


// ---------------------------------------------------------------------------
/** One voice: every model's state, and the crossfade between two of them. */
struct Voice {
	Paia paia;
	Qd qd;
	Korg korg35, ms20;
	Efm efm;
	Dirt dirt;

	int model = KORG35;
	int prevModel = KORG35;
	float fade = 1.f;         // 0 .. 1, progress from prevModel to model
	float hot = 0.f;

	Voice() { ms20.ms20 = true; }

	void setRate(float fs) {
		paia.setRate(fs); qd.setRate(fs); korg35.setRate(fs);
		ms20.setRate(fs); efm.setRate(fs); dirt.setRate(fs);
	}

	void resetModel(int m) {
		switch (m) {
			case PAIA:   paia.reset(); break;
			case QD:     qd.reset(); break;
			case KORG35: korg35.reset(); break;
			case MS20:   ms20.reset(); break;
			case EFM:    efm.reset(); break;
			default:     dirt.reset(); break;
		}
	}

	void reset() {
		for (int m = 0; m < NUM_MODELS; m++) resetModel(m);
		fade = 1.f;
		prevModel = model;
	}

	/** Point the voice at `m`; the old model keeps running under a fade. */
	void select(int m) {
		if (m == model) return;
		prevModel = model;
		model = m;
		resetModel(m);
		fade = 0.f;
	}

	inline float run(int m, float lpIn, float hpIn, bool useHp, const Coeffs& c, float& h) {
		switch (m) {
			case PAIA:   { float y = paia.process(lpIn + hpIn, c);   h = paia.hot;   return y; }
			case QD:     { float y = qd.process(lpIn + hpIn, c);     h = qd.hot;     return y; }
			case KORG35: { float y = korg35.process(lpIn, hpIn, useHp, c); h = korg35.hot; return y; }
			case MS20:   { float y = ms20.process(lpIn, hpIn, useHp, c);   h = ms20.hot;   return y; }
			case EFM:    { float y = efm.process(lpIn + hpIn, c);    h = efm.hot;    return y; }
			default:     { float y = dirt.process(lpIn + hpIn, c);   h = dirt.hot;   return y; }
		}
	}

	/** `fadeInc` is 1 / (fade seconds * sample rate) at the rate this is called. */
	inline float process(float lpIn, float hpIn, bool useHp, const Coeffs& c, float fadeInc) {
		float y = run(model, lpIn, hpIn, useHp, c, hot);
		if (fade < 1.f) {
			float hp = 0.f;
			float yp = run(prevModel, lpIn, hpIn, useHp, c, hp);
			y = yp + (y - yp) * fade;
			hot = std::fmax(hot, hp);
			fade += fadeInc;
			if (fade >= 1.f) fade = 1.f;
		}
		return y;
	}
};

} // namespace deduction

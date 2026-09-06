// The nonlinear block's mapping function, in one place so the audio thread and
// the panel read-out draw the same curve. Nothing here touches Rack.
#pragma once
#include <cmath>

namespace gross {

/** Which mapping the CURVE knob has selected. TANH is Eichas & Zölzer's
    three-piece hyperbolic tangent (DAFx-16, eq. 6); the rest are the usual
    static waveshapers, all given the same four knee/shape numbers so the trims
    never go dead. */
enum Curve {
	CURVE_TANH,
	CURVE_RATIONAL,
	CURVE_HARD,
	CURVE_DIODE,
	CURVE_CUBIC,
	NUM_CURVES
};

/** The mapping's own parameters: the positive and negative knees (where the
    curve stops being tanh, or for the other curves the level it clips at) and
    the slope factor beyond each knee. tkp / tkn cache tanh(kp) / tanh(kn),
    which eq. 6 needs per sample and which only change when a trim moves. */
struct Shape {
	int curve = CURVE_TANH;
	float kp = 1.f, kn = 1.f;
	float gp = 1.f, gn = 1.f;
	float tkp = 0.f, tkn = 0.f;

	void set(int c, float kp_, float kn_, float gp_, float gn_) {
		curve = c;
		kp = kp_ < 0.02f ? 0.02f : kp_;
		kn = kn_ < 0.02f ? 0.02f : kn_;
		gp = gp_ < 0.05f ? 0.05f : gp_;
		gn = gn_ < 0.05f ? 0.05f : gn_;
		tkp = std::tanh(kp);
		tkn = std::tanh(kn);
	}
};

/** A saturating half-curve on [0, inf): f(0) = 0, f'(0) = 1, f -> 1. `g` is the
    hardness where the family has one; HARD and CUBIC ignore it. */
inline float soft(int curve, float t, float g) {
	if (t > 1000.f)
		t = 1000.f;
	switch (curve) {
		case CURVE_RATIONAL:
			// x / (1 + |x|^g)^(1/g): g = 1 is x / (1 + |x|), g -> inf is a hard clip.
			return t / std::pow(1.f + std::pow(t, g), 1.f / g);
		case CURVE_HARD:
			return t < 1.f ? t : 1.f;
		case CURVE_DIODE:
			// (1 - e^(-t^g))^(1/g): the exponential clipper 1 - e^-t at g = 1,
			// squarer corners as g rises.
			return std::pow(1.f - std::exp(-std::pow(t, g)), 1.f / g);
		case CURVE_CUBIC:
			return t < 1.f ? t - t * t * t / 3.f : 2.f / 3.f;
		default:
			return std::tanh(t);
	}
}

/** m(x). For TANH this is eq. 6 of Eichas & Zölzer with the argument of the
    outer tanh read as gp (x - kp): the typeset form, gp x - kp, is only
    continuous at the knee when gp = 1, and the paper's stated property -- a
    continuous derivative at the joins -- holds for gp (x - kp) and not for the
    other. For the remaining curves the knees scale the half-curve so it
    saturates at +kp / -kn, and the shapes set its hardness. */
inline float map(const Shape& s, float x) {
	if (s.curve == CURVE_TANH) {
		if (x > s.kp)
			return s.tkp + (1.f - s.tkp * s.tkp) / s.gp * std::tanh(s.gp * (x - s.kp));
		if (x < -s.kn)
			return -s.tkn - (1.f - s.tkn * s.tkn) / s.gn * std::tanh(s.gn * (-x - s.kn));
		return std::tanh(x);
	}
	if (x >= 0.f)
		return s.kp * soft(s.curve, x / s.kp, s.gp);
	return -s.kn * soft(s.curve, -x / s.kn, s.gn);
}

} // namespace gross

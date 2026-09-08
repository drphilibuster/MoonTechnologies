#pragma once
#include <cmath>

// ---------------------------------------------------------------------------
// CURVE -- the one thing the Modular in a Week function generators never had.
//
// Every core in here draws its shape from a fixed circuit: the Simple LFO's
// OTA integrator ramps *linearly*, and the diode-steered RC pair rises and
// falls on the *one* exponential its time constant fixes. A hardware function
// generator with any pretension bends that shape -- a Maths channel's response
// knob, a 281's shape control -- because a linear ramp and a 5-tau RC are two
// points on a continuum, not the whole of it.
//
// So CURVE is a per-channel bend of the finished unipolar output, applied
// after the core has run, and it is deliberately a *shaper*, not a change to
// the integrator: the cycle keeps exactly the ATTACK and RELEASE seconds the
// knobs set, and every zero crossing, wrap and end-of-cycle lands where it did
// before. Only the path between the endpoints moves.

namespace installment {

/** CURVE at full deflection is a cube or a cube root -- three octaves of
    exponent either side of linear, which is about as far as a bend stays
    musical before it collapses onto the axis. */
static const float kCurveExpLog2 = 1.5849625f;   // log2(3)

/** Bend a unipolar 0..10 V function output. `knob` is -1..1:

      -1  logarithmic, jumping off the floor and easing into the ceiling
       0  linear -- the core's own shape, passed through untouched
      +1  exponential, hanging near the floor and rushing the ceiling

    Both endpoints are fixed points (0 V stays 0 V, 10 V stays 10 V), so the
    bend cannot change a cycle's length, its peak, or where it ends. */
inline float curveShape(float v, float knob) {
	// The range guards come first, before the detent's bypass, so the shaper
	// answers the same way at every knob position -- a detent that passed a
	// negative through while every other setting clamped it would put a step
	// in the middle of the knob's travel for the one input that must not
	// reach std::pow, whose negative base is a NaN.
	float u = v * 0.1f;
	if (!(u > 0.f))                  // and this catches a NaN coming in
		return 0.f;
	if (u >= 1.f)
		return v;
	if (knob == 0.f)
		return v;                    // linear is a bypass, not pow(u, 1)
	return std::pow(u, std::exp2(knob * kCurveExpLog2)) * 10.f;
}

} // namespace installment

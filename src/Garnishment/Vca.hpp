// The three VCA circuits behind Garnishment, and the state one channel keeps.
//
// Split out of the module so it can be tested: none of this touches Rack -- no
// engine, no Module, no simd -- and `make test` is deliberately SDK-free, which
// is what lets CI run the suite on a machine with neither Rack nor the SDK.
//
// Everything works in Rack's conventions: audio at +-5 V, control at 0-10 V.
#pragma once
#include <algorithm>   // std::max, which this used to get via rack.hpp
#include <cmath>

enum GarnishmentMode { MODE_OTA = 0, MODE_VACTROL = 1, MODE_JFET_AM = 2 };

static const int MAX_POLY = 16;

/** Asymmetric exponential slew -- separate time constants rising and falling.
    This is the vactrol's own fast-attack/slow-decay behaviour (~2 ms up, 50-200
    ms down), reused as the shared LAG control for the other two circuits. */
static inline float slewTo(float prev, float target, float attackTau, float decayTau, float dt) {
	float tau = (target > prev) ? attackTau : decayTau;
	float coef = 1.f - std::exp(-dt / std::max(tau, 1e-6f));
	return prev + (target - prev) * coef;
}

/** One-pole TPT lowpass. Used only in the vactrol's LPG mode, where the filter
    closes along with the gain -- a low-pass gate rolls off treble as it darkens,
    which a plain multiply-by-gain VCA does not. */
struct OnePoleLP {
	float state = 0.f;
	float process(float x, float cutoffHz, float sampleTime) {
		float g = std::tan((float) M_PI * cutoffHz * sampleTime);
		float G = g / (1.f + g);
		float v = (x - state) * G;
		float y = v + state;
		state = y + v;
		return y;
	}
	void reset() { state = 0.f; }
};

/** DC blocker for the JFET path's output cap (C1 in the schematic): the drain
    divider sits on a DC operating point that must not reach the output. */
struct OnePoleHP {
	float x1 = 0.f, y1 = 0.f;
	float process(float x, float r) {
		float y = x - x1 + r * y1;
		x1 = x;
		y1 = y;
		return y;
	}
	void reset() { x1 = y1 = 0.f; }
};

/** Everything one VCA channel needs to remember between samples, per
    polyphonic voice. Six of these live in the module, one per channel. */
struct VcaBus {
	float ctrl[MAX_POLY] = {};   // slewed control voltage, 0..1 -- all three modes
	OnePoleLP lpg[MAX_POLY];
	OnePoleHP dcBlock[MAX_POLY];

	void reset() {
		for (int i = 0; i < MAX_POLY; i++) {
			ctrl[i] = 0.f;
			lpg[i].reset();
			dcBlock[i].reset();
		}
	}
};

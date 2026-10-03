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

#include "IAmO.hpp"
#include "../Vactrol.hpp"
#include "OtaVca.hpp"

enum GarnishmentMode { MODE_OTA = 0, MODE_VACTROL = 1, MODE_JFET_AM = 2 };

static const int MAX_POLY = 16;

/** VACTROL mode: the Day 2 schematic's CV In -> 330 ohm -> LED, and the cell it lights in series with
    the audio, against an assumed 100 kohm downstream input (R12 has no partner on the sheet).
    `volts` is the LED's drive (BIAS + CV AMOUNT * CV, volts); returns the signal gain and leaves
    the cell's state advanced by one sample. Clamped to the LED's 40 mA absolute maximum. */
static inline float vactrolGain(vactrol::Vactrol& cell, float volts, float sampleTime) {
	const double RSERIES = 330.0, RLOAD = 100e3;
	cell.setSampleTime(sampleTime);
	double i = vactrol::ledCurrent((double) volts, RSERIES, *cell.p);
	if (i > cell.p->maxLedCurrent) i = cell.p->maxLedCurrent;
	double g = cell.step(i);          // siemens
	return (float) (1.0 / (1.0 + 1.0 / (g * RLOAD)));    // RLOAD / (RLOAD + 1/g)
}

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

/** Everything one VCA channel needs to remember between samples, per
    polyphonic voice. Six of these live in the module, one per channel. */
struct VcaBus {
	float ctrl[MAX_POLY] = {};   // slewed control voltage, 0..1 -- all three modes
	OnePoleLP lpg[MAX_POLY];
	vactrol::Vactrol ldr[MAX_POLY];            // VACTROL mode: the cell (VTL5C3 unless the menu says otherwise), with its memory
	garnishment::IAmO jfet[MAX_POLY];          // the I AM O circuit, solved; C1 is its own DC blocker
	garnishment::OtaVca ota[MAX_POLY];         // the Day 2 13700 board, solved
	float cvv[MAX_POLY] = {};                  // OTA: the CV at the 22k, volts, slewed by LAG
	float open[MAX_POLY] = {};                 // how open the channel is, 0..1, for the read-out (every mode)
	float dcLp[MAX_POLY] = {};                 // OTA: the DC the optional blocker takes off Out

	void reset() {
		for (int i = 0; i < MAX_POLY; i++) {
			ctrl[i] = 0.f;
			cvv[i] = 0.f;
			open[i] = 0.f;
			dcLp[i] = 0.f;
			ota[i].reset();
			ldr[i].reset();
			lpg[i].reset();
			jfet[i].reset();
		}
	}
};

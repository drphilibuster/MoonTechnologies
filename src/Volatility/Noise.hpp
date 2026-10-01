#pragma once
#include <cmath>
#include <cstdint>

#include "../Cd4006.hpp"

// ---------------------------------------------------------------------------
// The two things Volatility's sections needed to be finishable, kept out of the
// .cpp so they can be tested: the noise colours, and the random gate's length.
//
// COLOUR. The module's continuous white noise is the one signal on the panel
// that comes from no source circuit at all -- the 4006 is a digital noise
// generator, and the white source was invented so SAMPLE & HOLD's SRC and RND
// GATE's SRC IN would have something to normal to. Having invented it, leaving
// it as white only was the gap: white is the one colour every other module in
// a rack already has, and the useful ones for modulation -- pink for something
// that reads as musical across the spectrum, red for slow drift -- are two
// filters away.
//
// GATE LENGTH. RND GATE held its output high for the whole clock period on a
// successful draw, which means two successes in a row made one continuous high
// rather than two gates. Nothing downstream could count them: an envelope saw
// one trigger, a sequencer advanced once. A random gate that cannot produce a
// trigger is a random gate that cannot clock anything.

namespace volatility {

/** Pink is -3 dB/octave: three one-pole lowpasses in parallel, summed with the
    raw white, which is Paul Kellet's economy filter. His published coefficients
    are for 44.1 kHz, and a pole written as a bare per-sample coefficient moves
    its corner when the sample rate changes -- the slope would tilt at 96 kHz
    and tilt the other way at 22.05 kHz. So the corners are stored as the
    frequencies those coefficients actually describe, and the coefficients are
    rebuilt from them for whatever rate the engine is running. */
static const float kPinkHz[3] = { 16.53f, 261.2f, 3950.f };
/** Kellet writes each pole as a bare input coefficient, which is its DC gain
    times (1 - pole). Store the DC gains, so rebuilding a pole at another rate
    keeps the gain the stack was tuned for instead of scaling it by the rate. */
static const float kPinkGain[3] = { 42.147f, 8.0140f, 2.4481f };
static const float kPinkDirect = 0.1848f;

/** Red (Brownian) is -6 dB/octave: white through a single integrator, leaky
    enough that it cannot wander off as a DC offset. 20 Hz is low enough to
    sound like drift and high enough that the leak actually catches it. */
static const float kRedHz = 20.f;

/** Both colours are filters fed by a per-sample white source, whose spectral
    density halves when the sample rate doubles for the same RMS -- so a filter
    integrating a fixed band gets quieter as the rate rises, by 1/sqrt(fs).
    These are the gains that put each colour at white's own RMS at 44.1 kHz;
    build() scales them by sqrt(fs/44100) so COLOUR is a spectrum control at
    every rate rather than also a volume control. */
static const float kNormAtRate = 44100.f;
static const float kPinkNorm = 0.3351f;
static const float kRedNorm = 26.34f;

struct NoiseColours {
	float pinkState[3];
	float redState;
	float pinkCoef[3];
	float redCoef;
	float pinkNorm;
	float redNorm;
	float builtFor;

	NoiseColours() { reset(); build(44100.f); }

	void reset() {
		for (int i = 0; i < 3; i++)
			pinkState[i] = 0.f;
		redState = 0.f;
	}

	/** One-pole coefficient for a corner at `hz`, at sample rate `fs`. */
	static float pole(float hz, float fs) {
		return std::exp(-2.f * 3.14159265358979f * hz / fs);
	}

	void build(float fs) {
		// A nonsense rate would divide by zero in pole() and leave every
		// coefficient a NaN, which the states would then hold forever -- one
		// bad build() and the module never produces a number again.
		if (!(fs > 0.f))
			return;
		for (int i = 0; i < 3; i++)
			pinkCoef[i] = pole(kPinkHz[i], fs);
		redCoef = pole(kRedHz, fs);
		float rate = std::sqrt(fs / kNormAtRate);
		pinkNorm = kPinkNorm * rate;
		redNorm = kRedNorm * rate;
		builtFor = fs;
	}

	/** `white` is the module's own +/-5 V source. Returns the three colours at
	    matched level, so turning COLOUR changes the spectrum and not the gain. */
	void step(float white, float& pink, float& red) {
		float acc = white * kPinkDirect;
		for (int i = 0; i < 3; i++) {
			pinkState[i] = pinkCoef[i] * pinkState[i]
			             + white * kPinkGain[i] * (1.f - pinkCoef[i]);
			acc += pinkState[i];
		}
		pink = acc * pinkNorm;

		redState = redCoef * redState + white * (1.f - redCoef);
		red = redState * redNorm;
	}
};

// ---------------------------------------------------------------------------
// THE REGISTER. One CD4006B wired as an 18-stage shift register, with two of its
// pins XORed back into its input.
//
// The textbook two-tap solution for 18 stages is stages 18 and 11, and it cannot
// be built on one chip: a 4006 brings out only the end of each section, so the
// pins it has are, at most, stages 4, 5, 8, 9, 12, 13, 14, 17 and 18 once the
// sections are chained, in an order that picks six of those. There is no pin on
// stage 11, or on stage 7, the reciprocal. No 18-stage trinomial is reachable at
// all, and the nearest a single 4006 comes is 17: stages 17 and 12, a maximal
// sequence of 2^17 - 1 = 131071 clocks. That is what this is. (The TR-909's noise
// source reaches 2^31 - 1 with two 4006s and taps 31 and 13: 18 + 13, and 13.)
//
// The chain, section by section, is D1 -> D3 -> D2 -> D4, which numbers the
// stages
//
//     1..4   section 1   (pin 13 is stage 4)
//     5..8   section 3   (pin 10 is stage 8)
//     9..13  section 2   (pin 11 is stage 12, pin 12 is stage 13)
//     14..18 section 4   (pin 8 is stage 17, pin 9 is stage 18)
//
// and the XOR takes pins 11 and 8. Stage 18 comes out as a delayed copy of 17,
// as the TR-909's 36th does.
//
// An XOR register locks up on all zeros. The real circuit has a start-up network
// that holds the input high for 20-30 ms; here an all-zero register is fed a one,
// which is what that network is for.

static const int kRegisterStages = 18;
static const int kFeedbackStageA = 12;     // D2+4, pin 11
static const int kFeedbackStageB = 17;     // D4+4, pin 8
static const uint32_t kRegisterMask = 0x0003FFFFu;
static const uint32_t kRegisterSeed = 0x00015555u;   // any nonzero 18-stage fill

struct Register4006 {
	cd4006::Cd4006 chip;

	Register4006() { setStages(kRegisterSeed); }

	void reset() { setStages(kRegisterSeed); }

	/** The register as one number: bit k-1 is stage k, counting from the input. */
	uint32_t stages() const {
		uint32_t v = 0;
		for (int i = 0; i < 4; i++) v |= (uint32_t) chip.s1[i] << i;          // 1-4
		for (int i = 0; i < 4; i++) v |= (uint32_t) chip.s3[i] << (4 + i);    // 5-8
		for (int i = 0; i < 5; i++) v |= (uint32_t) chip.s2[i] << (8 + i);    // 9-13
		for (int i = 0; i < 5; i++) v |= (uint32_t) chip.s4[i] << (13 + i);   // 14-18
		return v;
	}

	void setStages(uint32_t v) {
		for (int i = 0; i < 4; i++) chip.s1[i] = (v >> i) & 1u;
		for (int i = 0; i < 4; i++) chip.s3[i] = (v >> (4 + i)) & 1u;
		for (int i = 0; i < 5; i++) chip.s2[i] = (v >> (8 + i)) & 1u;
		for (int i = 0; i < 5; i++) chip.s4[i] = (v >> (13 + i)) & 1u;
	}

	/** What the XOR gate is putting on pin 1 now. */
	bool feedback() const {
		if (stages() == 0u)
			return true;                           // the start-up network
		return chip.pin11() != chip.pin8();        // stage 12 XOR stage 17
	}

	/** One negative-going clock edge. Returns the bit that went in. */
	bool clockFalling() {
		bool fb = feedback();
		// Section 1 takes the feedback; each later section takes the pin of the one
		// before it in the chain, read before the edge.
		chip.clockFalling(fb, chip.pin10(), chip.pin13(), chip.pin12());
		return fb;
	}
};

/** The clock's level, to find its negative-going edges. The external clock uses
    a Schmitt trigger on its own; the internal one is a 50 % square, high for the
    first half of its cycle. */
struct FallingEdge {
	bool wasHigh = false;
	bool process(bool high) {
		bool fell = wasHigh && !high;
		wasHigh = high;
		return fell;
	}
	void reset() { wasHigh = false; }
};

/** COLOUR is one knob across three colours: white at full CCW, pink at centre,
    red at full CW, crossfading between the neighbouring pair in each half. */
inline float colourMix(float white, float pink, float red, float knob) {
	if (knob <= 0.f)
		return white;
	if (knob >= 1.f)
		return red;
	if (knob < 0.5f) {
		float t = knob * 2.f;
		return white * (1.f - t) + pink * t;
	}
	float t = (knob - 0.5f) * 2.f;
	return pink * (1.f - t) + red * t;
}

// --- the random gate's length ----------------------------------------------

static const float kPeriodMin = 1e-4f;      // 10 kHz, past the clock's ceiling
static const float kPeriodMax = 30.f;       // slower than the knob's floor
static const float kPeriodDefault = 0.1f;
static const float kMinGateSec = 1e-3f;     // a trigger anything can see
static const float kFullGate = 0.99f;       // above this, latch to the edge

/** Holds the random gate high for a fraction of the clock's own period. The
    period is measured from the clock's edges rather than read off RATE,
    because the clock may be an external one and RATE says nothing then.

    At full length this latches instead of running the timer: two successes in
    a row have to make one unbroken high, and a timer set to exactly one period
    would drop a sample between them on any rounding error. */
struct GateStretcher {
	float sinceEdge;
	float periodSec;
	float remaining;
	bool held;

	GateStretcher() { reset(); }

	void reset() {
		sinceEdge = 0.f;
		periodSec = kPeriodDefault;
		remaining = 0.f;
		held = false;
	}

	/** Every sample, before any fire(). An edge both measures the period and
	    clears the previous edge's decision -- a draw only speaks for its own
	    clock period. */
	void tick(float dt, bool edge) {
		sinceEdge += dt;
		if (edge) {
			if (sinceEdge > kPeriodMin && sinceEdge < kPeriodMax)
				periodSec = sinceEdge;
			sinceEdge = 0.f;
			// A draw speaks for its own clock period and no longer. Clearing
			// the timer here as well as the latch is what makes that exact:
			// the period is measured from the last interval, so on a clock
			// that is speeding up a timer set from it would otherwise spill
			// past the next edge and swallow the following draw's gap.
			held = false;
			remaining = 0.f;
		}
		if (remaining > 0.f)
			remaining -= dt;
	}

	/** A successful draw. `length` is 0..1 of the measured period. */
	void fire(float length) {
		if (length >= kFullGate) {
			held = true;
			remaining = 0.f;
			return;
		}
		// Never shorter than something downstream can see. There is no
		// ceiling to apply: the next edge ends the gate whatever the timer
		// still holds.
		float floorSec = (periodSec * 0.5f < kMinGateSec) ? periodSec * 0.5f
		                                                  : kMinGateSec;
		float want = length * periodSec;
		if (want < floorSec)
			want = floorSec;
		remaining = want;
	}

	bool high() const { return held || remaining > 0.f; }
};

} // namespace volatility

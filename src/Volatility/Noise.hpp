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
// THE REGISTER. One CD4006B wired as an 18-stage shift register, closed into a
// ring through a 4070 XOR pair and one transistor inverter: the noise source of
// Yves Usson's "Random Eight Pole Gate Switch" (Yusynth, Sept. 2005,
// https://yusynth.net/Modular/Commun/RANDOM/RANDOMGATE-sch.pdf). Only the
// register is taken from that board; its 4051 router and gate outputs are not.
//
// THE WIRING, read off the schematic (U1 = 4006, U3a/U3b = 4070, Q2 = BC547):
//
//     D4 (pin 6)  <- U3a pin 3          (the only input fed by logic)
//     D3 (pin 5)  <- Q18 (pin 9)        D4+5
//     D2 (pin 4)  <- Q13 (pin 10)       D3+4
//     D1 (pin 1)  <- Q9  (pin 12)       D2+5
//     U3b  = pin 9 XOR pin 13           (pin 13 = D1+4, the last stage)
//     Q2   = inverter on U3b's output   (R7 22k base, R6 4.7k collector pull-up)
//     U3a  = Q2 XOR pin 10
//
// So the chain, in the order the bit travels, is D4 -> D3 -> D2 -> D1, and
// numbering the stages from the input
//
//      1.. 5   section 4   (pin 8 is stage 4, pin 9 is stage 5)
//      6.. 9   section 3   (pin 10 is stage 9)
//     10..14   section 2   (pin 11 is stage 13, pin 12 is stage 14)
//     15..18   section 1   (pin 13 is stage 18)
//
// the new bit entering stage 1 is
//
//     NOT( s18 XOR s5 ) XOR s9
//
// -- taps at stages 5, 9 and 18 and an inversion. Three taps and an inverter
// is what makes all 18 stages usable on one chip: the two-tap textbook pair
// (18, 11) needs a pin on stage 11, which the 4006 does not have, but this
// ring does not need a trinomial. Its feedback is invertible (the oldest bit
// enters linearly), so every state lies on a cycle: one of 2^18 - 4 = 262140
// clocks, and one of 4 clocks (001100110011001100 and its shifts) that is
// not reached from the main one and in practice is never entered (the
// sequence only lands there from those four states themselves). All zeros is
// NOT a lock-up here: the inverter makes the XOR of nothing a one. That is
// why the Yusynth board has no start-up network, and why this has no rule.
// Shifting is on the clock's falling edge, as the datasheet has it.
//
// Pin-backed stages are 4, 5, 9, 13, 14 and 18; the rest of the register, and
// so most of the DAC window, is inside the chip with no pin.

static const int kRegisterStages = 18;
static const int kTapStageA = 5;        // D4+5, pin 9
static const int kTapStageB = 9;        // D3+4, pin 10
static const int kTapStageC = 18;       // D1+4, pin 13
static const uint32_t kRegisterMask = 0x0003FFFFu;
static const uint32_t kRegisterSeed = 0x00015555u;   // any state on the long cycle

struct Register4006 {
	cd4006::Cd4006 chip;

	Register4006() { setStages(kRegisterSeed); }

	void reset() { setStages(kRegisterSeed); }

	/** The register as one number: bit k-1 is stage k, counting from the input. */
	uint32_t stages() const {
		uint32_t v = 0;
		for (int i = 0; i < 5; i++) v |= (uint32_t) chip.s4[i] << i;          // 1-5
		for (int i = 0; i < 4; i++) v |= (uint32_t) chip.s3[i] << (5 + i);    // 6-9
		for (int i = 0; i < 5; i++) v |= (uint32_t) chip.s2[i] << (9 + i);    // 10-14
		for (int i = 0; i < 4; i++) v |= (uint32_t) chip.s1[i] << (14 + i);   // 15-18
		return v;
	}

	void setStages(uint32_t v) {
		for (int i = 0; i < 5; i++) chip.s4[i] = (v >> i) & 1u;
		for (int i = 0; i < 4; i++) chip.s3[i] = (v >> (5 + i)) & 1u;
		for (int i = 0; i < 5; i++) chip.s2[i] = (v >> (9 + i)) & 1u;
		for (int i = 0; i < 4; i++) chip.s1[i] = (v >> (14 + i)) & 1u;
	}

	/** What U3a is putting on pin 6 now: NOT(pin 9 XOR pin 13) XOR pin 10. */
	bool feedback() const {
		bool u3b = chip.pin9() != chip.pin13();    // U3b
		bool q2 = !u3b;                            // the inverter
		return q2 != chip.pin10();                 // U3a
	}

	/** One negative-going clock edge. Returns the bit that went in. */
	bool clockFalling() {
		bool fb = feedback();
		// D4 takes the feedback; D3, D2 and D1 each take the pin of the section
		// before them in the chain, read before the edge.
		chip.clockFalling(chip.pin12(), chip.pin10(), chip.pin9(), fb);
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

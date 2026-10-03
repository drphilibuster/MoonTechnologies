#pragma once
// The op-amp stages of Consolidation and Bailout, from "A Simple Mixer Right?" v1.4 and
// "Buffured Multiple 3x1:2" (Kristian Blasol), as src/OpAmp.hpp's TL07x.
//
// MIXER. A strip is a 10k pot into a 10k input resistor; the strips meet at the
// virtual ground of a unity-gain inverting summer (10k feedback), and a second unity
// inverting stage follows it. The compensation caps across the feedback resistors are
// 27 pF (first stage) and 47 pF (second), and the output is 100 ohm into 10 uF
// (Consolidation.md has had these values from the start; the schematic itself is not in
// the course folder this tree reads from). So:
//
//     stage 1   y1 = -(sum of the strips)     the INV OUT jack
//     stage 2   y2 = +(sum of the strips)     through 100 ohm and 10 uF to OUT
//
// What the op-amps cost, which is what this file is for: the summer's noise gain is
// 1 + 10k * (the strips' conductance), the output stops short of the rails by an amount
// that depends on what loads it, each stage is a pole at GBW over its noise gain with
// a 590 kHz (27 pF) or 339 kHz (47 pF) pole in front of that, and a slew limit.
//
// Loads, since an op-amp's swing depends on them: stage 1 drives its own feedback
// resistor and stage 2's input resistor, both to a virtual ground (10k || 10k);
// stage 2 drives its feedback resistor and, through the 100 ohm / 10 uF, whatever the
// jack feeds (ASSUMED 100 k, the Eurorack norm: 10k || 100.1k).
//
// MULTIPLE. A TL07x voltage follower with 100 ohm in series with the jack. The
// follower's feedback is taken before the resistor, so a load drops 100 / (100 + RL)
// of the signal. The legs of one multiple are identical op-amps with an identical
// input and (assumed) identical loads, so one follower is solved and fanned out.
//
// Each is an object per polyphony channel. No Rack dependency.

#include "OpAmp.hpp"

#include <cmath>

namespace mixstage {

static const int kMaxPoly = 16;

static const double kRin = 10e3;          // the strips' input resistors, and both Rf
static const double kCf1 = 27e-12;
static const double kCf2 = 47e-12;
static const double kRout = 100.0;        // the output resistor, mixer and multiple
static const double kCout = 10e-6;        // the mixer's coupling cap
static const double kLoad = 100e3;        // ASSUMED: what the jack feeds

inline double finite(double v) { return (v == v && std::fabs(v) < 1e6) ? v : 0.0; }

/** A summer and an inverter: `nIn` strips in. */
struct Bank {
	opamp::InvertingStage s1, s2;
	double vcap = 0.0;
	double prev = 0.0;
	bool coupled = false;

	/** The feedback caps are 270 and 470 ns time constants against a 21 us sample, so
	    a solve per sample would leave their charge to decay over several of them; four
	    solves per sample (5 us, ten times the time constants) let it decay inside one,
	    and make the poles' effect at 20 kHz (0.03 of a radian) the size it should be. */
	static const int kSub = 4;

	void init(int nIn, double vpos = 12.0, double vneg = -12.0) {
		s1 = opamp::InvertingStage();
		s2 = opamp::InvertingStage();
		s1.op.setSpec(opamp::tl07x());
		s2.op.setSpec(opamp::tl07x());
		s1.op.setSupply(vpos, vneg);
		s2.op.setSupply(vpos, vneg);
		s1.op.setLoad(kRin * kRin / (kRin + kRin), 0.0);
		s2.op.setLoad(kRin * (kRout + kLoad) / (kRin + kRout + kLoad), 0.0);
		s1.gin = (double) nIn / kRin;  s1.rf = kRin;  s1.cf = kCf1;
		s2.gin = 1.0 / kRin;           s2.rf = kRin;  s2.cf = kCf2;
		s1.reset(0.0);
		s2.reset(0.0);
		vcap = 0.0;
		prev = 0.0;
	}

	/** `sum` is the strips' total, volts (the unipolar sum the module computes).
	    Returns what stage 2 puts on the pin; `inv` is stage 1's. */
	double process(double sum, double dt, double& inv) {
		sum = finite(sum);
		double h = dt / (double) kSub, y = 0.0;
		for (int i = 1; i <= kSub; i++) {
			double x = prev + (sum - prev) * ((double) i / (double) kSub);
			inv = s1.step(x / kRin, h);                 // sum of v_i / Rin into the node
			y = s2.step(inv / kRin, h);
		}
		prev = sum;
		return y;
	}

	/** The jack: stage 2 through 100 ohm and the 10 uF into the load. With `coupled`
	    false it is the pin itself (what the module did before). */
	double jack(double pin, double dt) {
		if (!coupled) return pin;
		double r = kRout + kLoad;
		double i = (pin - vcap) / r;
		vcap += dt * i / kCout;
		return i * kLoad;
	}
};

/** One multiple: a follower and its 100 ohm. */
struct Leg {
	opamp::NonInvertingStage f;
	bool withR = false;

	void init(double vpos = 12.0, double vneg = -12.0) {
		f = opamp::NonInvertingStage();
		f.op.setSpec(opamp::tl07x());
		f.op.setSupply(vpos, vneg);
		f.op.setLoad(kRout + kLoad, 0.0);
		f.rf = 0.0;
		f.reset(0.0);
	}

	double process(double in, double dt) {
		double y = f.step(finite(in), dt);
		return withR ? y * kLoad / (kLoad + kRout) : y;
	}
};

} // namespace mixstage

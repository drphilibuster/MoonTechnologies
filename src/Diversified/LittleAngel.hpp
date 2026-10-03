#pragma once
/** Diversified program 100 -- Little Angel, Rick Holt's PT2399 mini chorus.
 *
 * The board is Jack Orman's "New Year's Edition" rev 2 (AMZ-FX, Dec 2015),
 *   http://www.muzique.com/news/images2/Angel_NYE2.gif
 * of Rick Holt's (Frequency Central) Little Angel. Everything below that is a
 * number is read off that drawing unless it is under `assumed`, and each of those
 * says why.
 *
 * What makes it a chorus and not a delay is that the chip's clock is NOT swept.
 * Pin 6 is held to ground by a J112 (the shortest delay the part has) and the
 * LFO is fed to pin 2 instead -- the chip's REF, 1/2 Vcc -- through the Depth pot
 * and 10k. REF is every analog stage's ground in the datasheet's block diagram, so
 * signals, which are relative to it, do not care; the one thing that does is the
 * VCO, whose control current is REF over the pin-6 path (Pt2399::
 * delaySecondsForVref). That is the "pin 2 hack".
 *
 * The rest is the circuit as drawn: the inverting input stage (-1M/470k, biased
 * Vcc/2), LPF1 and LPF2 as the three-10k-one-capacitor sections they are on pins
 * 16/15 and 13/14, the integrators' 0.1 uF, a fixed dry/wet sum through 10k each
 * into the 100k load (there is no mix pot), the single-op-amp triangle LFO, and
 * the 78L06 that makes the supply 6 V. REF moving is also *heard*, a little: it
 * is the wet node's DC level, and it goes through the output coupling cap.
 */
#include "Primitives.hpp"
#include "../Pt2399.hpp"
#include <algorithm>
#include <cmath>

namespace divfx {

namespace la {
	// --- read off the schematic --------------------------------------------
	static const double VCC = 6.0;                // 78L06 (rev 2 changed the 5 V part to 6 V)
	static const double R_IN = 470e3, R_FB = 1e6; // input stage: gain -R_FB/R_IN = -2.13
	static const double C_IN = 0.1e-6;            // input coupling
	static const double R_FILT = 10e3;            // every resistor of LPF1 and LPF2
	static const double C_9 = 0.1e-6;             // op-amp out -> LPF1 coupling
	static const double C_LPF1 = 3.3e-9;          // pin 16 - pin 15
	static const double C_LPF2 = 10e-9;           // pin 13 - pin 14
	static const double C_INT = 0.1e-6;           // across pins 9-10 and 11-12
	static const double R_OUT = 10e3;             // dry and wet, each, into the node
	static const double C_OUT = 0.1e-6;           // dry and wet, each
	static const double R_LOAD = 100e3;           // the output node's resistor to ground
	// LFO: 220k/220k bias, 220k positive feedback, 4k7 + Speed (100k rev-log) into
	// a 10 uF node; Depth (500k rev-log) in series with 10 uF and 10k to pin 2.
	static const double R_LFO_FIXED = 4.7e3, R_SPEED = 100e3, C_LFO = 10e-6;
	static const double R_DEPTH = 500e3, C_DEPTH = 10e-6, R_PIN2 = 10e3;
	static const double C_WARBLE = 10e-6;         // the Space/Warbler switch's cap (v4)
	static const double R_GATE = 1e6;             // J112 gate resistor
	// The Speed and Depth pots are "rev log". A reverse-log pot is the mirror of an
	// audio taper: 15 % of the resistance is reached at 50 % of the rotation (the
	// usual audio-taper figure), measured from the other end.
	static const double TAPER_MID = 0.15;

	// --- not on the drawing --------------------------------------------------
	// Volts at the board per Rack volt. A Rack signal is +-5 V; the board is for a
	// guitar, about 1 V at its loudest. 5 Rack volts as 1 V puts the input stage
	// (gain 2.13) just under its own swing and the chip just under its rail.
	static const double PEDAL_V_PER_RACK_V = 0.2;
	// The signal op-amp's and the LFO op-amp's output swing about Vcc/2 on 9 V.
	// The layouts for this board use an NE5532 ("any dual op-amp"), which on 9 V
	// single supply gets to within about 1.5 V of either rail. ASSUMED.
	static const double OPAMP_SWING_V = 3.0;
	// The chip's internal REF divider is 5.6k + 5.6k (Valve Wizard measured the
	// resistors at 5.6k, not the 4.7k of the datasheet), so REF is a 2.8k source.
	static const double REF_THEVENIN_OHMS = 2800.0;
	// The J112. Its rDS(on) is at most 50 ohms (data sheet); its cutoff voltage is
	// anywhere from -1 to -5 V, and -3 is only the middle of that. ASSUMED.
	static const double J112_RDS_ON = 50.0;
	static const double J112_VGS_OFF = -3.0;
	static const double GATE_FORWARD_V = 0.6;     // the gate junction conducts
	// The datasheet's output noise, -90 dBV, at the comparator.
	static const double CHIP_NOISE_V = 40e-6;
	// Pin 3/4 on 6 V: the chip's op-amps swing to about 0.1 V of the rails.
	static inline double chipClipV() { return 0.5 * VCC - 0.1; }

	/** A reverse-log pot's taper: 0..1 rotation -> fraction of resistance, 15 % at
	    the middle, linear either side. */
	static inline double audioTaper(double x) {
		x = x < 0.0 ? 0.0 : (x > 1.0 ? 1.0 : x);
		return x < 0.5 ? TAPER_MID * (x / 0.5) : TAPER_MID + (1.0 - TAPER_MID) * ((x - 0.5) / 0.5);
	}

	/** The triangle LFO, as drawn: a Schmitt trigger (220k from the output to the
	    + input, which sits on the 220k/220k bias, so it flips when the ramp node
	    reaches one third of the output swing) driving a 10 uF node through 4k7
	    plus the Speed pot. The 220k/10 nF around the inverting input only shapes
	    the few milliseconds of each edge and is not modelled. The ramp node is
	    loaded by the Depth path (10 uF, the pot, 10k, and pin 2's own 2.8k), which
	    is solved with it. Everything is a deviation from the 4.5 V bias. */
	struct Lfo {
		double T = 0.0, vc = 0.0, sgn = 1.0;     // ramp node, Depth cap, output state
		double dVref = 0.0, xNode = 0.0;          // out: pin 2's deviation, the node after the pot
		double a = 0.0, b = 0.0, k2 = 0.0, den = 0.0, cn = 0.0;
		double rl = 1.0;
		double dt = 1.0 / 48000.0;

		void setSampleRate(double sr) { dt = 1.0 / sr; }
		void reset() { T = vc = 0.0; sgn = 1.0; dVref = xNode = 0.0; }

		/** speed, depth: knob positions 0..1, clockwise = faster / deeper. */
		void setKnobs(double speed, double depth) {
			double rs = R_SPEED * audioTaper(1.0 - speed);     // wiper to the ramp node
			double rt = R_LFO_FIXED + rs;
			double rp = R_DEPTH * audioTaper(1.0 - depth);     // wiper to the 10k
			rl = rp + R_PIN2 + REF_THEVENIN_OHMS;
			a = dt / (C_LFO * rt);
			b = dt / (C_LFO * rl);
			k2 = dt / (C_DEPTH * rl);
			den = 1.0 + a + b - b * k2 / (1.0 + k2);
			cn = b / (1.0 + k2);
		}

		void step() {
			const double swing = OPAMP_SWING_V;
			double vo = sgn * swing;
			double t1 = (T + a * vo + cn * vc) / den;
			vc = (vc + k2 * t1) / (1.0 + k2);
			T = t1;
			double th = swing / 3.0;
			if (sgn > 0.0 && T > th) sgn = -1.0;
			else if (sgn < 0.0 && T < -th) sgn = 1.0;
			double il = (T - vc) / rl;                // current into the Depth path
			dVref = REF_THEVENIN_OHMS * il;
			xNode = (REF_THEVENIN_OHMS + R_PIN2) * il;
		}
	};

	/** The Space/Warbler switch, which is on the v4 board and not on this one: it
	    takes the LFO, through a 10 uF, to the control terminal of the device that
	    holds pin 6 down -- there the anti-lock transistor's base, here the J112's
	    gate. The gate's own junction clamps its positive swing at about 0.6 V, so
	    the 10 uF charges and the swing hangs below that, into the J112's
	    resistance rising toward cutoff. ASSUMED to be the same mechanism on this
	    board; the NYE drawing does not show the switch. */
	struct Gate {
		double q = 0.0;
		void reset() { q = 0.0; }
		double rds(double x, double dt) {
			double g = x - q;
			if (g > GATE_FORWARD_V) { q = x - GATE_FORWARD_V; g = GATE_FORWARD_V; }
			q += dt * (x - q) * (1.0 / (R_GATE * C_WARBLE));   // the 1M leaks it back
			if (g >= 0.0) return J112_RDS_ON;
			double f = 1.0 + g / (-J112_VGS_OFF);                // 1 - Vgs/Vp
			if (f < 0.02) f = 0.02;                              // at cutoff: 50 x
			return J112_RDS_ON / f;
		}
	};

	/** LPF1 behind its coupling cap, as the chip's pins 16/15 make it: x1 is C9's
	    voltage, x2 the 3.3 nF's. With all four resistors 10k the node between them
	    sits at (u - x1 - x2)/3, which gives a -1 low-pass with its corner at
	    1/(2 pi 3 R C) = 1.6 kHz under a high-pass at 1/(2 pi 20k C9). Trapezoidal. */
	struct Lpf1 {
		double p11 = 0, p12 = 0, p21 = 0, p22 = 0, q1 = 0, q2 = 0;
		double x1 = 0, x2 = 0, u0 = 0;
		void setSampleRate(double sr) {
			double h = 1.0 / sr;
			double a11 = -(2.0 / 3.0) / (R_FILT * C_9), a12 = (1.0 / 3.0) / (R_FILT * C_9);
			double a21 = -1.0 / (3.0 * R_FILT * C_LPF1), a22 = a21;
			double b1 = (2.0 / 3.0) / (R_FILT * C_9), b2 = 1.0 / (3.0 * R_FILT * C_LPF1);
			// (I - hA/2)^-1
			double m11 = 1.0 - 0.5 * h * a11, m12 = -0.5 * h * a12;
			double m21 = -0.5 * h * a21, m22 = 1.0 - 0.5 * h * a22;
			double det = m11 * m22 - m12 * m21;
			double i11 = m22 / det, i12 = -m12 / det, i21 = -m21 / det, i22 = m11 / det;
			double n11 = 1.0 + 0.5 * h * a11, n12 = 0.5 * h * a12;
			double n21 = 0.5 * h * a21, n22 = 1.0 + 0.5 * h * a22;
			p11 = i11 * n11 + i12 * n21; p12 = i11 * n12 + i12 * n22;
			p21 = i21 * n11 + i22 * n21; p22 = i21 * n12 + i22 * n22;
			q1 = 0.5 * h * (i11 * b1 + i12 * b2);
			q2 = 0.5 * h * (i21 * b1 + i22 * b2);
		}
		void clear() { x1 = x2 = u0 = 0.0; }
		/** u in volts relative to REF; returns pin 15, relative to REF. */
		double step(double u) {
			double n1 = p11 * x1 + p12 * x2 + q1 * (u0 + u);
			double n2 = p21 * x1 + p22 * x2 + q2 * (u0 + u);
			x1 = n1; x2 = n2; u0 = u;
			return -x2;
		}
	};

	/** LPF2, pins 13/14: the same section without the coupling cap, 10 nF, so its
	    corner is 1/(2 pi 3 R C) = 531 Hz. */
	struct Lpf2 {
		double k = 0.0, x = 0.0, u0 = 0.0;
		void setSampleRate(double sr) { k = (1.0 / sr) / (3.0 * R_FILT * C_LPF2); }
		void clear() { x = u0 = 0.0; }
		double step(double u) {
			x = ((1.0 - 0.5 * k) * x + 0.5 * k * (u0 + u)) / (1.0 + 0.5 * k);
			u0 = u;
			return -x;
		}
	};
}

struct LittleAngel {
	pt2399::Pt2399 chip[2];
	la::Lfo lfo;
	la::Gate gate[2];
	la::Lpf1 lpf1[2];
	la::Lpf2 lpf2[2];
	OnePole inHp;
	Rng noise;

	double sr, dt;
	double vcd[2], vcw[2];          // the output coupling caps
	float prev[2];                  // last sample into each chip
	double gDry, gWet;              // conductances into the output node
	bool vibe, warble;
	int mode;

	LittleAngel() : sr(48000.0), dt(1.0 / 48000.0), gDry(1.0 / la::R_OUT),
	                gWet(1.0 / la::R_OUT), vibe(false), warble(false), mode(0) {
		vcd[0] = vcd[1] = vcw[0] = vcw[1] = 0.0;
		prev[0] = prev[1] = 0.f;
		for (int i = 0; i < 2; i++) configureChip(chip[i]);
	}

	/** The chip as this board wires it: 6 V rails, 0.1 uF across both integrators. */
	static void configureChip(pt2399::Pt2399& c) {
		c.clipV = (float) la::chipClipV();
		c.setIntegrator(4.7e3, la::C_INT);   // the datasheet's 4.7k
	}

	void init() {}

	void resetChips() {
		for (int i = 0; i < 2; i++) {
			chip[i].reset();
			configureChip(chip[i]);
			chip[i].setSampleRate(sr);
			chip[i].setBitRate((double) pt2399::Pt2399::kBits
			    / pt2399::Pt2399::delaySecondsForVref(la::J112_RDS_ON, 0.5 * la::VCC));
			prev[i] = 0.f;
		}
	}

	void clear() {
		lfo.reset();
		for (int i = 0; i < 2; i++) {
			gate[i].reset();
			lpf1[i].clear(); lpf2[i].clear();
			vcd[i] = vcw[i] = 0.0;
		}
		inHp.clear();
		resetChips();
	}

	void setSampleRate(float s) {
		sr = s; dt = 1.0 / sr;
		lfo.setSampleRate(sr);
		for (int i = 0; i < 2; i++) {
			chip[i].setSampleRate(sr);
			lpf1[i].setSampleRate(sr);
			lpf2[i].setSampleRate(sr);
		}
		inHp.setCutoff((float) (1.0 / (2.0 * M_PI * la::R_IN * la::C_IN)), s);
	}

	/** SPEED, DEPTH and MODE, each 0..1. */
	void setParams(float speed, float depth, float modeKnob) {
		lfo.setKnobs(clamp(speed, 0.f, 1.f), clamp(depth, 0.f, 1.f));
		// MODE carries the board's two switches: chorus/normal, chorus/warbler,
		// vibe/normal, vibe/warbler. Vibe opens the dry path's switch; nothing else
		// about the mix changes, because there is no mix control.
		mode = (int) clamp(std::floor(clamp(modeKnob, 0.f, 0.999f) * 4.f), 0.f, 3.f);
		vibe = (mode >= 2);
		warble = (mode == 1 || mode == 3);
		gDry = vibe ? 0.0 : 1.0 / la::R_OUT;
	}

	void process(float in, float& outL, float& outR) {
		lfo.step();

		// Input stage: 0.1 uF into 470k, inverting, 1M back, within its rails.
		double vin = clamp(in, -12.f, 12.f) * la::PEDAL_V_PER_RACK_V;
		double vd = -(la::R_FB / la::R_IN) * inHp.hp((float) vin);
		vd = std::fmax(-la::OPAMP_SWING_V, std::fmin(la::OPAMP_SWING_V, vd));

		float outs[2];
		for (int ch = 0; ch < 2; ch++) {
			// The second chip is this module's own addition: the same board with the
			// LFO inverted, so the pair swing against each other.
			double s = ch ? -1.0 : 1.0;
			double dV = s * lfo.dVref;
			double rds = warble ? gate[ch].rds(s * lfo.xNode, dt) : la::J112_RDS_ON;
			pt2399::Pt2399& p = chip[ch];
			p.setPin6Vref(rds, 0.5 * la::VCC + dV);
			p.begin();

			// LPF1 sees the coupling cap's far side move with REF.
			double x1 = lpf1[ch].step(vd - dV);
			float clipV = (float) la::chipClipV();
			float cur = clamp((float) x1, -clipV, clipV)
			          + (float) (la::CHIP_NOISE_V * 1.7320508) * noise.bi();
			float y = p.demodModulate(prev[ch], cur);
			if (!std::isfinite(y)) {
				resetChips();
				lpf1[ch].clear(); lpf2[ch].clear();
				y = 0.f; cur = 0.f;
			}
			prev[ch] = cur;

			// OP2 -> LPF2 -> pin 14, whose DC level is REF, which is moving.
			double wet = lpf2[ch].step(y) + dV;

			// Dry and wet into the 100k, each through 10k and 0.1 uF.
			double gw = gWet, gd = gDry, gl = 1.0 / la::R_LOAD;
			double v = (gd * (vd - vcd[ch]) + gw * (wet - vcw[ch])) / (gd + gw + gl);
			vcd[ch] += dt * gd * (vd - vcd[ch] - v) / la::C_OUT;
			vcw[ch] += dt * gw * (wet - vcw[ch] - v) / la::C_OUT;
			outs[ch] = (float) (v / la::PEDAL_V_PER_RACK_V);
		}
		outL = outs[0];
		outR = outs[1];
	}
};

} // namespace divfx

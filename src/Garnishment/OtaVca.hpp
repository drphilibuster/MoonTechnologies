#pragma once
// "Simple 13700 Dual VCA" (Kristian Blasol; Modular in a Week Day 2, `Schematic_13700-Dual-VCA`,
// EasyEDA 2018-09-14) as a circuit: one channel, around src/Lm13700.hpp.
//
//   In --C 470n--R 27k--+--(+) pin 3
//                        |
//                      1k BIAS trimmer, WIPER TO GROUND: the (-) pin 4 is the trimmer's top end,
//                        |                 the (+) node its bottom end
//   V+ --R 12k-- diode bias pin 2
//   CV --R 22k-- amp bias pin 1 (Iabc)
//   pin 5 --R 33k-- GND, and into the buffer pin 7;  pin 8 = Out, with R 4k7 to V-.   +-12 V.
//
// What the trimmer is. Its wiper is grounded, so it is not a gain control: it splits the 1k into a
// top part to the (-) pin and a bottom part to the (+) pin, and both pins have the diodes' DC
// current (about half of Id = (12 V - Va)/12k, ~0.45 mA) running through them. At the centre
// the two currents are equal, the pair is balanced and Iout is zero at rest; off centre, the
// difference of the two I*R drops is an input offset that Iabc multiplies (so the offset
// current -- and a DC offset on Out -- moves with the CV). It also shunts the signal: the 27k
// feeds a node held down by at most 1k, so the pins see a few percent of the input, which is
// why the diodes' linear range (the signal current under Id/2) reaches +-12 V of input.
//
// Out is DC coupled on the board: the buffer drops about two Vbe from pin 5, and pin 5 sits at
// 0 V when Iout is zero, so the rest level is about -1.4 V (it does not move with the CV when the
// pair is balanced -- Iout is then zero whatever Iabc is).
//
// ASSUMED (the schematic does not say): +-12 V rails (the course's supply, Day 0); a nominal
// part (no offset voltage); trimmer centred unless told otherwise; the chip's numbers are in
// Lm13700.hpp. Not modelled: the 470 nF input cap's leakage, the chip's 2 MHz and 5 pF.
//
// No Rack dependency, so tests/ drives it bare.

#include "../Lm13700.hpp"

#include <cmath>

namespace garnishment {

struct OtaVca {
	// the board
	double rIn = 27e3, cIn = 470e-9, rCv = 22e3, rBias = 12e3, rTrim = 1000.0;
	double rLoad = 33e3, rEmit = 4.7e3;
	double vPlus = 12.0, vMinus = -12.0;
	/** Trimmer wiper position: the fraction of rTrim between the wiper and the (+) pin end
	    (0 grounds the (+) pin, 1 leaves it with the whole 1k and the (-) pin with none). 0.5 balances. */
	double trim = 0.5;
	/** The module's liberty (its "exponential CV" menu item): Iabc squared against 1 mA. */
	bool squareIabc = false;

	// state and last results (for tests and the read-out)
	lm13700::InputState in;
	lm13700::OutputState out;
	double iabc = 0.0, iabcRaw = 0.0, vc = 0.0, ic = 0.0, callRate = 0.0, vinPrev = 0.0;
	bool started = false;
	int sub = 2;

	void reset() { started = false; }

	lm13700::InputNet inputNet() const {
		lm13700::InputNet n;
		n.rBot = std::fmax(1e-3, trim * rTrim);
		n.rTop = std::fmax(1e-3, (1.0 - trim) * rTrim);
		n.rBias = rBias;
		n.vPlus = vPlus;
		return n;
	}
	lm13700::OutputNet outputNet() const {
		lm13700::OutputNet n;
		n.rLoad = rLoad; n.rEmit = rEmit; n.vPlus = vPlus; n.vMinus = vMinus;
		return n;
	}

	/** The control current for a CV, with the squaring option applied. */
	double controlCurrent(double vcv) {
		iabcRaw = lm13700::iabcFromCv(vcv, vMinus, rCv, iabcRaw > 0.0 ? iabcRaw : 5e-4);
		iabc = squareIabc ? iabcRaw * iabcRaw / 1e-3 : iabcRaw;
		return iabc;
	}

	void begin(double rate, double vcv) {
		callRate = rate;
		sub = (int)std::ceil(96000.0 / rate);
		if (sub < 1) sub = 1;
		iabcRaw = 0.0;
		controlCurrent(vcv);
		in = lm13700::InputState();
		lm13700::solveInput(inputNet(), 0.0, 1e12, iabc, in);        // DC: the capacitor passes nothing
		ic = 0.0;
		vc = -in.v3;                         // the capacitor blocks DC: it holds the node's rest level
		out = lm13700::OutputState();
		lm13700::solveOutput(outputNet(), in.iout, iabc, out);
		vinPrev = 0.0;
		started = true;
	}

	/** `audio` volts on In, `vcv` volts on CV In; returns the volts on Out. Trapezoidal input
	    capacitor; `sub` solves per call with the audio interpolated between calls. */
	double process(double audio, double vcv, double rate) {
		if (!started || rate != callRate) begin(rate, vcv);
		controlCurrent(vcv);
		const double h = 1.0 / (rate * sub);
		const double rc = h / (2.0 * cIn);
		lm13700::InputNet net = inputNet();
		lm13700::OutputNet onet = outputNet();
		for (int k = 1; k <= sub; k++) {
			double vin = vinPrev + (audio - vinPrev) * (k / (double)sub);
			double vth = vin - vc - rc * ic;
			double rs = rIn + rc;
			lm13700::solveInput(net, vth, rs, iabc, in);
			double inew = (vth - in.v3) / rs;
			vc += rc * (ic + inew);
			ic = inew;
			lm13700::solveOutput(onet, in.iout, iabc, out);
		}
		vinPrev = audio;
		if (!in.ok || !std::isfinite(out.vout)) { begin(rate, vcv); return out.vout; }
		return out.vout;
	}
};

} // namespace garnishment

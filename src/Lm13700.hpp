#pragma once
// The LM13700 dual operational transconductance amplifier (National / Texas Instruments
// SNOSBW2F, 2015; datasheet in claude_music_tools/MiawResearch/datasheets/lm13700.pdf), one
// half of it, as the circuit the datasheet draws in its Figure 16 -- not as a gain block.
//
//   Amp bias (pin 1)     Q2's base and the D1/Q1 mirror: the pin sits two junction drops above V-
//                        and whatever current is pushed in is Iabc (Figure 10: 1.0 V at 0.1 uA,
//                        1.5 V at 1 mA, 25 C).
//   Diode bias (pin 2)   the common anode of D2 and D3; D2's cathode is the (-) input pin and the
//                        base of Q4, D3's is the (+) input pin and the base of Q5.
//   Q4 / Q5              the pair that Iabc is shared across: I5 / I4 = exp((V+ - V-) / Vt),
//                        I4 + I5 = Iabc (datasheet Eq. 1, 2), so
//                           Iout = I5 - I4 = Iabc * tanh((V+ - V-) / 2Vt)          (Eq. 5, exact)
//   Output (pin 5)       a current source (the mirrors Q6..Q11), output resistance Figure 12
//                        (about 1.6 Mohm at 500 uA), swinging to V+ - 0.8 V and V- + 0.6 V
//                        (Figure 5: +14.2 / -14.4 V on +-15 V).
//   Buffer (7 -> 8)      the Darlington Q12/Q13: Vout = V7 - 2 Vbe, loaded by whatever the user
//                        ties from pin 8 to V-; its input current (0.5 uA typical, Electrical
//                        Characteristics) is the Darlington's base current.
//
// The linearizing diodes are the reason this is not a bare tanh in a real circuit. D2 and D3
// are fed their bias from one node (pin 2, through a resistor to V+) and their cathodes are
// the input pins, so the pair divides that bias between itself by the same exponential law
// that Q4/Q5 divide Iabc by, and the external network decides how much current each cathode
// can sink. The datasheet's Eq. 7 follows from the identity (no approximation):
//                        Iout = Iabc * (Id2 - Id3) / (Id2 + Id3)
// where Id2 and Id3 are the diode currents -- linear in the signal current while
// |Is| < Id / 2, with the gain Iabc / (Id / 2). Here the diodes are not an equation but part of
// the node equations below, so the external resistors that carry their DC current, the
// balance the board trims, and the 12k that sets Id all take part.
//
// Numbers that are the datasheet's: Iabc <-> pin 1 (Figure 10), input bias current 0.4 uA at
// 500 uA (so beta of Q4/Q5 is 625), output resistance (Figure 12), output swing (Figure 5),
// buffer input current 0.5 uA with a 5k load at +-15 V (a 2.7 mA emitter current: beta of
// each Darlington stage about 73). The junction saturation current 4e-16 A is *fitted* to
// Figure 10 (two junctions at ideal slope: 1.00 V at 0.1 uA and 1.48 V at 1 mA), and D2/D3
// and the buffer's Vbe use the same number because the datasheet says "all transistors and
// diodes are identical in size" (excepting Q12 and Q13). ASSUMED: Vt for 300 K, a nominal part
// (zero offset voltage; the typical is 0.4 mV), and no temperature or frequency behaviour
// (the 2 MHz bandwidth, 5 pF input and output capacitance do nothing at audio).
//
// No Rack dependency, so tests/ drives it bare.

#include <algorithm>
#include <cmath>

namespace lm13700 {

static const double kVt = 0.025852;          // kT/q at 300 K (the datasheet says ~26 mV at 25 C)
static const double kIs = 4e-16;             // junction saturation current, fitted to Figure 10
static const double kBetaIn = 625.0;         // Q4/Q5: 0.4 uA bias at 500 uA Iabc
static const double kBetaBuf = 73.0;         // each Darlington stage (0.5 uA at 2.7 mA)
static const double kRoutK = 800.0;          // Rout = kRoutK / Iabc: 1.6 Mohm at 500 uA (Figure 12)
static const double kSwingTop = 0.8;         // V+ - 0.8 V (Figure 5)
static const double kSwingBot = 0.6;         // V- + 0.6 V (Figure 5)

/** exp that continues linearly past 40, so a bad Newton step cannot overflow. */
inline double expLim(double x) {
	if (x > 40.0) return std::exp(40.0) * (1.0 + x - 40.0);
	return std::exp(x);
}

/** Voltage of pin 1 above V- for a bias current (Figure 10, 25 C). */
inline double pin1Volts(double iabc) { return 2.0 * kVt * std::log(iabc / kIs); }

/** Iabc for a control voltage `vcv` through `rcv` into pin 1, supply `vminus`:
        Iabc = (vcv - vminus - pin1Volts(Iabc)) / rcv
    The left side falls and the right rises with Iabc, so there is one answer; zero when the
    network cannot even reach the junction's knee (under 1 pA). `guess` warm-starts it. */
inline double iabcFromCv(double vcv, double vminus, double rcv, double guess = 5e-4) {
	double drive = vcv - vminus;
	if (drive <= pin1Volts(1e-12)) return 0.0;
	double u = std::log(std::max(guess, 1e-12));              // Newton on u = ln(Iabc)
	for (int it = 0; it < 60; it++) {
		double i = std::exp(u);
		double f = (drive - 2.0 * kVt * (u - std::log(kIs))) / rcv - i;
		double df = -2.0 * kVt / rcv - i;
		double du = -f / df;
		du = std::max(-3.0, std::min(3.0, du));
		u += du;
		if (std::fabs(du) < 1e-13) break;
	}
	return std::exp(u);
}

/** The input half: what the pins and the diode-bias node settle to. */
struct InputNet {
	double rBot = 1000.0;       // (+) pin to ground, beyond the signal source's own impedance
	double rTop = 1000.0;       // (-) pin to ground
	double rBias = 12e3;        // diode-bias pin to V+
	double vPlus = 12.0;
};

struct InputState {
	double v3 = 0.2, v4 = 0.2, va = 0.9;      // (+) pin, (-) pin, diode-bias pin
	double id2 = 0.0, id3 = 0.0;              // the two diodes' currents
	double iout = 0.0;                        // Iabc * tanh((v3 - v4) / 2Vt)
	int iters = 0;
	bool ok = true;
};

/** Solve the three node equations for a Thevenin signal (vth behind rs) on the (+) pin:
        (+) pin   Id3 + (vth - V3)/rs - V3/rBot - Ib5 = 0
        (-) pin   Id2 - V4/rTop - Ib4 = 0
        pin 2     (V+ - Va)/rBias - Id2 - Id3 = 0
    with Id = Is exp((Va - V)/Vt) and Ib5, Ib4 the bases' share of Iabc over beta. Newton on
    (V3, V4, Va), each step limited to 0.1 V; the answer carries over as the next start. */
inline void solveInput(const InputNet& n, double vth, double rs, double iabc, InputState& s) {
	const double gb = iabc / kBetaIn;
	double v3 = s.v3, v4 = s.v4, va = s.va;
	s.ok = false;
	for (int it = 1; it <= 80; it++) {
		double e3 = expLim((va - v3) / kVt), e2 = expLim((va - v4) / kVt);
		double id3 = kIs * e3, id2 = kIs * e2;
		double g3 = id3 / kVt, g2 = id2 / kVt;
		double x = (v3 - v4) / kVt;
		double sg = 1.0 / (1.0 + std::exp(-std::max(-60.0, std::min(60.0, x))));
		double sp = sg * (1.0 - sg) / kVt;                  // d sigma / d(V3 - V4)
		double ib5 = gb * sg, ib4 = gb * (1.0 - sg);

		double f1 = id3 + (vth - v3) / rs - v3 / n.rBot - ib5;
		double f2 = id2 - v4 / n.rTop - ib4;
		double f3 = (n.vPlus - va) / n.rBias - id2 - id3;

		double a11 = -g3 - 1.0 / rs - 1.0 / n.rBot - gb * sp, a12 = gb * sp, a13 = g3;
		double a21 = gb * sp, a22 = -g2 - 1.0 / n.rTop - gb * sp, a23 = g2;
		double a31 = g3, a32 = g2, a33 = -1.0 / n.rBias - g2 - g3;

		double b1 = -f1, b2 = -f2, b3 = -f3;
		double det = a11 * (a22 * a33 - a23 * a32) - a12 * (a21 * a33 - a23 * a31) + a13 * (a21 * a32 - a22 * a31);
		if (!(std::fabs(det) > 1e-300)) break;
		// Cramer's rule: each unknown's column replaced by the right-hand side
		double d3 = (b1 * (a22 * a33 - a23 * a32) - a12 * (b2 * a33 - a23 * b3) + a13 * (b2 * a32 - a22 * b3)) / det;
		double d4 = (a11 * (b2 * a33 - a23 * b3) - b1 * (a21 * a33 - a23 * a31) + a13 * (a21 * b3 - b2 * a31)) / det;
		double da = (a11 * (a22 * b3 - b2 * a32) - a12 * (a21 * b3 - b2 * a31) + b1 * (a21 * a32 - a22 * a31)) / det;
		double m = std::max(std::fabs(d3), std::max(std::fabs(d4), std::fabs(da)));
		double k = m > 0.1 ? 0.1 / m : 1.0;
		v3 += k * d3; v4 += k * d4; va += k * da;
		s.iters = it;
		if (m < 1e-10) { s.ok = true; break; }
	}
	s.v3 = v3; s.v4 = v4; s.va = va;
	s.id3 = kIs * expLim((va - v3) / kVt);
	s.id2 = kIs * expLim((va - v4) / kVt);
	s.iout = iabc * std::tanh((v3 - v4) / (2.0 * kVt));
	if (!std::isfinite(s.iout) || !std::isfinite(v3) || !std::isfinite(v4) || !std::isfinite(va)) {
		s.v3 = 0.2; s.v4 = 0.2; s.va = 0.9; s.iout = 0.0; s.ok = false;
	}
}

/** The output half: Iout into `rLoad` to ground at pin 5, the buffer on it with `rEmit` to V-. */
struct OutputNet {
	double rLoad = 33e3;
	double rEmit = 4.7e3;
	double vPlus = 12.0, vMinus = -12.0;
};

struct OutputState {
	double v5 = 0.0;        // pin 5, volts
	double vout = 0.0;      // pin 8
	double ibuf = 0.0;      // the buffer's input current
	bool clipped = false;   // the output stage ran out of room
};

/** Vbe of a junction carrying `i` (clamped to a floor: an off junction is a 0 V drop). */
inline double vbe(double i) { return i <= 1e-15 ? 0.0 : kVt * std::log(i / kIs); }

inline void solveOutput(const OutputNet& n, double iout, double iabc, OutputState& s) {
	const double rout = iabc > 1e-12 ? kRoutK / iabc : 1e15;
	const double rl = 1.0 / (1.0 / n.rLoad + 1.0 / rout);
	const double lo = n.vMinus + kSwingBot, hi = n.vPlus - kSwingTop;
	double ibuf = s.ibuf, v5 = 0.0, vout = 0.0;
	for (int it = 0; it < 6; it++) {
		v5 = (iout - ibuf) * rl;
		s.clipped = false;
		if (v5 > hi) { v5 = hi; s.clipped = true; }
		if (v5 < lo) { v5 = lo; s.clipped = true; }
		// Darlington: Q13 carries Ie, Q12 carries Ie / (beta + 1), the base carries that over (beta + 1).
		double ie = std::max(0.0, vout - n.vMinus) / n.rEmit;
		for (int k = 0; k < 4; k++) {
			double ie12 = ie / (kBetaBuf + 1.0);
			vout = v5 - vbe(ie) - vbe(ie12);
			ie = std::max(0.0, vout - n.vMinus) / n.rEmit;
		}
		ibuf = ie / ((kBetaBuf + 1.0) * (kBetaBuf + 1.0));
	}
	s.v5 = v5; s.vout = vout; s.ibuf = ibuf;
}

} // namespace lm13700

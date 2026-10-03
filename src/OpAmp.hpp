#pragma once
// Behavioural op-amps from their datasheets: TL071/TL072/TL074, LM741, LM358.
//
// "Behavioural" means one integrator, not a transistor netlist, but every number in
// it is a datasheet number and the ones the datasheet does not give are marked
// ASSUMED. The model is the classic macromodel: a differential input drives an
// integrator whose output node u is
//
//     du/dt = clamp( wt * (V+ - V-)  -  (wt / A0) * u,  +-SR )
//
// wt = 2 pi GBW, A0 the DC open-loop gain, SR the slew rate. That single line is the
// gain-bandwidth product (a closed loop of feedback fraction beta has a pole at
// GBW * beta), the finite DC gain, and slew limiting, with the right interaction
// between them: a loop that is slew limited is no longer a pole at all, it is a ramp.
// The output pin is then u run through an output stage:
//
//   * TL07x, LM741: a follower that stops at a rail. The rail is not the supply: it
//     is the supply less a headroom that grows with the supply and with the current
//     the load draws (see Spec::hSat / hPerVolt / rs).
//   * LM358: a class-B stage with no quiescent bias. A Darlington NPN sources, a PNP
//     emitter follower sinks, and between them lies a dead band 3 Vbe wide in u in
//     which only the weak ~50 uA constant sink acts. The loop has to slew u across
//     that band every time the load current changes sign: crossover distortion,
//     from the same mechanism TI's SLOA277 gives for it (a delay of 3 Vbe / SR).
//
// The circuit around the op-amp enters as V- = a * y + b (y the output pin), which
// is what every feedback network is, and the whole thing is solved
// one step at a time (the integrator exactly, the capacitors by backward Euler), so a
// loop gain of thousands does not need a tiny step. Helpers below build (a, b) for the three stages the plugin uses:
// InvertingStage (a summer with Rf and Cf), NonInvertingStage (Rg + Cg to ground,
// Rf || Cf) and Follower.
//
// Sources (all read for this file; the figures are in docs/OpAmps.md):
//   TL07x   TI SLOS080W (rev. July 2025), tables 5.8 and 5.9 and figures 5-42..5-44
//   LM741   TI SNOSC25D (rev. Oct 2015), table 6.5
//   LM358   TI SLOS068AB (rev. Oct 2024), table 5.7; TI SLOA277B (Jul 2023), 3 and 4.3
//
// No Rack dependency, so tests/ drives it bare.

#include <cmath>

namespace opamp {

static const double kPi = 3.14159265358979323846;

struct Spec {
	const char* name;

	// --- small signal and slewing -------------------------------------------------
	double gbw;        // Hz; the unity-gain crossing of the open-loop gain
	double slew;       // V/s
	double a0;         // DC open-loop gain, V/V

	// --- input (informational: every circuit here drives it from far below Ri) ----
	double ri;         // differential input resistance, ohms (0 = not in the datasheet)

	// --- output swing -------------------------------------------------------------
	// A saturated output is a Thevenin source: an EMF of (rail - h0) behind rs ohms,
	// with h0 = hSat + hPerVolt * (total supply). So into a load RL (to Vt)
	//     V = (rail - h0 + rs Vt / RL) / (1 + rs / RL)
	double hSat, hPerVolt, rs;

	// --- class-B output stage (LM358). vbe = 0 turns it into a plain follower ------
	double vbe;        // volts, one base-emitter drop
	double iSink;      // A, the always-on constant sink
	double volA;       // V above V- the output reaches on the constant sink alone
	double volB, rolB; // V above V-, and ohms, once the PNP follower is sinking

	bool classB() const { return vbe > 0.0; }
};

/** TL071/72/74, the TL07xC column, +-15 V, 25 C.
      GBW 5.25 MHz, SR 20 V/us, AOL 200 V/mV typ.        (older revisions: 3 MHz, 13 V/us)
      VOM +-13.5 V typ at 10k, +-12.3 V at 2k, ~+-10.6 V at 10k on +-12 V (Fig. 5-44).
    The swing fit: Fig. 5-44 is a straight line, VOM(10k) = 0.952 |Vcc| - 0.83 (3.5 V ->
    2.5 V, 14 V -> 12.5 V, and it meets the table's 13.5 V at 15 V). Fig. 5-43 and
    Fig. 5-42 give 2k at 12.3 V against 13.5 V at 10k, which a 250 ohm source resistance
    reproduces (and the 1k, 500, 200 ohm points on the same curve to a few tenths). That
    makes h0 = 0.851 + 0.012 S for a total supply S. */
inline Spec tl07x() {
	Spec s = {};
	s.name = "TL07x";
	s.gbw = 5.25e6;
	s.slew = 20e6;
	s.a0 = 2.0e5;
	s.ri = 1e12;
	s.hSat = 0.851; s.hPerVolt = 0.012; s.rs = 250.0;
	return s;
}

/** LM741 (TI SNOSC25D table 6.5, +-15 V, 25 C): SR 0.5 V/us, AOL 200 V/mV, Ri 2 Mohm,
    output +-14 V at 10k and +-13 V at 2k. The two points give rs = 196 ohm and an EMF
    0.73 V under the rail. GBW is 1.0 MHz (the TI table has rise time 0.3 us, i.e.
    0.35/0.3 = 1.17 MHz; the 1 MHz is the usual printed gain-bandwidth figure and is not
    in the TI document). The datasheet does not give swing against supply for the 741;
    the 0.73 V is carried to any supply (ASSUMED, and the 741 is only specified down to
    +-5 V; the MXR runs it on +-4.5 V). */
inline Spec lm741() {
	Spec s = {};
	s.name = "LM741";
	s.gbw = 1.0e6;
	s.slew = 0.5e6;
	s.a0 = 2.0e5;
	s.ri = 2e6;
	s.hSat = 0.73; s.hPerVolt = 0.0; s.rs = 196.0;
	return s;
}

/** LM358 (TI SLOS068AB, the non-B part, table 5.7): GBW 0.7 MHz, SR 0.3 V/us, AOL
    100 V/mV. Positive swing: 2 V under the rail typ for RL >= 10k at 30 V, 1.5 V max at
    5 V (RL 2k), and 1 V more at 2k than at 10k (4 V against 3 V max). That is read as
    h(10k) = 1.4 + 0.02 S and a 200 ohm source resistance; ASSUMED (interpolating a
    max against a typ).
    Negative swing and the crossover are SLOA277B: the constant sink holds the output
    within ~0.1 V of V- up to ~40 uA typ (12 uA min); past that the PNP follower takes
    over at ~0.62 V above V-, rising ~47 ohm x I (Figure 3-2, 25 C curve read by eye); and
    the switch from Darlington to PNP costs 3 Vbe of slewing (Vbe 0.65 V, ASSUMED 25 C). */
inline Spec lm358() {
	Spec s = {};
	s.name = "LM358";
	s.gbw = 0.7e6;
	s.slew = 0.3e6;
	s.a0 = 1.0e5;
	s.ri = 0.0;
	s.hSat = 1.14; s.hPerVolt = 0.02; s.rs = 200.0;
	s.vbe = 0.65;
	s.iSink = 40e-6;
	s.volA = 0.005;
	s.volB = 0.62; s.rolB = 47.0;
	return s;
}

/** The op-amp proper: integrator state u, output pin y. */
struct Core {
	Spec spec = tl07x();
	double vpos = 12.0, vneg = -12.0;     // the supply pins
	double rl = 1e12, vt = 0.0;           // the load: RL to a termination Vt
	double hi = 0.0, lo = 0.0, y0 = 0.0;  // output limits and the class-B dead-band level
	double u = 0.0, y = 0.0;

	Core() { update(); }

	void setSpec(const Spec& s) { spec = s; update(); }
	void setSupply(double pos, double neg) { vpos = pos; vneg = neg; update(); }
	void setLoad(double ohms, double term) { rl = ohms; vt = term; update(); }

	/** Output limits for the present supply and load. Call after changing any of them. */
	void update() {
		const Spec& s = spec;
		double S = vpos - vneg;
		double h0 = s.hSat + s.hPerVolt * S;
		double g = s.rs / rl;                    // rs / RL; 0 for an unloaded output
		hi = (vpos - h0 + g * vt) / (1.0 + g);
		if (s.classB()) {
			double sinkNeeded = (vt - (vneg + s.volB)) / rl;
			if (sinkNeeded < s.iSink) {
				lo = vneg + s.volA;
			}
			else {
				double r = s.rolB / rl;
				lo = (vneg + s.volB + r * vt) / (1.0 + r);
			}
			y0 = vt - s.iSink * rl;
			if (y0 < vneg + s.volA) y0 = vneg + s.volA;
			if (y0 > hi) y0 = hi;
		}
		else {
			lo = (vneg + h0 + g * vt) / (1.0 + g);
			y0 = 0.0;
		}
	}

	/** Start at rest with the output at `v` (clamped to what it can reach). */
	void reset(double v) {
		if (v > hi) v = hi;
		if (v < lo) v = lo;
		y = v;
		u = toU(v);
	}

	/** The output pin for an integrator value. */
	double outputOf(double uu) const {
		if (!spec.classB()) return uu;
		if (uu - 2.0 * spec.vbe > y0) return uu - 2.0 * spec.vbe;     // Darlington sourcing
		if (uu + spec.vbe < y0) return uu + spec.vbe;                 // PNP sinking
		return y0;                                                    // neither: the dead band
	}

	/** An integrator value that gives output v (the dead band's centre if v is y0). */
	double toU(double v) const {
		if (!spec.classB()) return v;
		if (v > y0) return v + 2.0 * spec.vbe;
		if (v < y0) return v - spec.vbe;
		return y0 + 0.5 * spec.vbe;
	}

	/** u' = rate - lam u over dt: exactly, or by backward Euler. */
	double advance(double lam, double rate, double dt, bool exact) const {
		double x = lam * dt;
		if (!exact) return (u + rate * dt) / (1.0 + x);
		double e = std::exp(-x);
		double g = x < 1e-9 ? dt : -std::expm1(-x) / lam;
		return u * e + rate * g;
	}

	/** One step of dt seconds. The circuit is V- = a y + b, V+ = vp, with b held for
	    the step (the capacitor states that make it up are updated by the caller
	    afterwards).

	    The integrator is advanced one of two ways. Where nothing in the feedback has
	    memory (`noCaps`), or the loop's pole is far above the step (x = (wp + wt a) dt
	    over 20), it is advanced exactly for the held b: backward Euler there would
	    leave a pole at 1 / (1 + x) where the circuit has e^-x, a few percent of the last
	    sample bleeding into this one, which is a lossy 20 kHz on a mixer whose real pole
	    is megahertz away. Where a capacitor is in the loop and the pole is within reach of
	    the step (the Distortion+, at 2.6 us), it is backward Euler, because that is
	    consistent with how the capacitors are solved and measures 0.2 % against the
	    closed form where the exponential measures 3 % (tests/OpAmp). Returns the
	    output pin. */
	double step(double vp, double a, double b, double dt, bool noCaps = true) {
		const Spec& s = spec;
		double wt = 2.0 * kPi * s.gbw;
		double wp = wt / s.a0;
		bool exact = noCaps || (wp + wt * a) * dt > 20.0;
		double un;
		if (!s.classB()) {
			un = advance(wp + wt * a, wt * (vp - b), dt, exact);
		}
		else {
			// The output map is flat in the dead band and slope 1 outside it, so the
			// equation is piecewise linear: take the dead band first and see whether
			// it is consistent, else the side it overshot.
			un = advance(wp, wt * (vp - b - a * y0), dt, exact);
			double yUp = un - 2.0 * s.vbe, yDn = un + s.vbe;
			if (yUp > y0)
				un = advance(wp + wt * a, wt * (vp - b + a * 2.0 * s.vbe), dt, exact);
			else if (yDn < y0)
				un = advance(wp + wt * a, wt * (vp - b - a * s.vbe), dt, exact);
		}
		// The slew limit acts on the integrator's rate, not on the pin.
		double du = un - u, lim = s.slew * dt;
		if (du > lim) du = lim;
		if (du < -lim) du = -lim;
		un = u + du;
		double yn = outputOf(un);
		// The rails: the output stage saturates and so does what drives it (no
		// windup, which is also why overload recovery is fast).
		if (yn > hi) { yn = hi; un = toU(hi); }
		if (yn < lo) { yn = lo; un = toU(lo); }
		u = un;
		y = yn;
		return yn;
	}
};

// ------------------------------------------------------------------------------
// The three stages. Each owns a Core and the capacitor states of its network.
// ------------------------------------------------------------------------------

/** The conductance a capacitor C across a resistor R presents to a step of h seconds,
    in backward-Euler form (i = Gc (v_new - v_old)), fitted so that the step is exact for
    the pair when the drive is held: Gc = 1 / (R expm1(h / RC)). For h much less than RC
    it is C / h less 1 / 2R, which is the trapezoid rule's accuracy without its ringing;
    for h much more than RC it is 0, the capacitor having settled inside the step, which
    plain backward Euler (C / h, a few percent of R here) does not get right. */
inline double fittedCap(double r, double c, double h) {
	return 1.0 / (r * std::expm1(h / (r * c)));
}

/** Inverting summer: input currents (sum of v_i / Rin_i, which the caller supplies) into
    the node, Rf and Cf from the output back to it. `gin` is the total input
    conductance, which sets the noise gain 1 + Rf gin. */
struct InvertingStage {
	Core op;
	double gin = 1e-4, rf = 1e4, cf = 0.0;
	double vf = 0.0;                // voltage across Cf (y - V-)
	double vm = 0.0;                // the inverting node
	double cDt = -1.0, cRf = -1.0, cCf = -1.0, cGc = 0.0;   // fittedCap, remembered

	void reset(double out) {
		op.reset(out);
		vm = 0.0;
		vf = op.y;
	}

	/** `iin` is the current the sources push into the summing node, amps. */
	double step(double iin, double dt) {
		if (dt != cDt || rf != cRf || cf != cCf) {
			cDt = dt; cRf = rf; cCf = cf;
			cGc = cf > 0.0 ? fittedCap(rf, cf, dt) : 0.0;
		}
		double gc = cGc;
		double yf = 1.0 / rf + gc;
		double G = gin + yf;
		double a = yf / G;
		double b = (iin - gc * vf) / G;
		double y = op.step(0.0, a, b, dt, !(cf > 0.0));
		vm = a * y + b;
		vf = y - vm;
		return y;
	}
};

/** Non-inverting: V+ is the input; the inverting node sees Rg in series with Cg to
    ground (cg = 0: just Rg) and Rf in parallel with Cf back to the output. rf = 0 is a
    follower. */
struct NonInvertingStage {
	Core op;
	double rg = 1e3, cg = 0.0, rf = 1e3, cf = 0.0;
	double vc = 0.0;                // charge on Cg
	double vf = 0.0;                // voltage across Cf
	double vm = 0.0;
	double cDt = -1.0, cRf = -1.0, cCf = -1.0, cRg = -1.0, cCg = -1.0;   // the fits, remembered
	double cGcf = 0.0, cYg = 0.0;

	void reset(double out) {
		op.reset(out);
		vm = out;
		vc = 0.0; vf = 0.0;
		if (rf > 0.0 && !(cg > 0.0)) {
			// DC solution with no series cap: V- is the divider of the output.
			vm = out * rg / (rg + rf);
			vf = out - vm;
		}
		else if (rf > 0.0) {
			// Cg blocks DC, so no current flows in Rf either: V- sits at the output
			// and Cg is charged to it.
			vc = out;
		}
	}

	double step(double vin, double dt) {
		if (!(rf > 0.0)) {
			double y = op.step(vin, 1.0, 0.0, dt, true);
			vm = y;
			return y;
		}
		if (dt != cDt || rf != cRf || cf != cCf || rg != cRg || cg != cCg) {
			cDt = dt; cRf = rf; cCf = cf; cRg = rg; cCg = cg;
			cGcf = cf > 0.0 ? fittedCap(rf, cf, dt) : 0.0;
			// Rg with Cg to ground: the same fit, for the capacitor settling toward V-.
			cYg = cg > 0.0 ? cg * (-std::expm1(-dt / (rg * cg))) / dt : 1.0 / rg;
		}
		double gcf = cGcf;
		double ye = 1.0 / rf + gcf;
		double yg = cYg;
		double D = ye + yg;
		double a = ye / D;
		double b = (yg * (cg > 0.0 ? vc : 0.0) - gcf * vf) / D;
		double y = op.step(vin, a, b, dt, !(cf > 0.0) && !(cg > 0.0));
		vm = a * y + b;
		vf = y - vm;
		if (cg > 0.0)
			vc += dt * yg * (vm - vc) / cg;
		return y;
	}

	/** n equal substeps across a linear ramp of the input from v0 to v1 over dt. */
	double stepRamp(double v0, double v1, double dt, int n) {
		double h = dt / (double)n, y = op.y;
		for (int i = 1; i <= n; i++)
			y = step(v0 + (v1 - v0) * ((double)i / (double)n), h);
		return y;
	}
};

} // namespace opamp

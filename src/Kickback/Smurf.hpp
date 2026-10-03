#pragma once
// The Smurf Drum (Tiny Dazzler Electronics, "Smurf Percussion", 01/06/2011), KICK's SMURF model.
//
// It is a complementary-pair relaxation oscillator -- a 2N3906 whose collector load is 380 ohm, a
// 2N3904 whose collector drives the 2N3906's base, a 0.01 uF coupling cap between the load and a node
// that recharges through the 1 M PITCH pot and 22 k -- with its supply taken from an envelope instead of
// a rail. The sheet's own notes: "power source replaced by envelope generator output to provide pitch and
// amplitude envelopes for oscillators by starving". The earlier SMURF here was a symmetric square wave
// under a fixed sweep, which is not what this circuit does.
//
// What it does, from a transistor-level simulation of the whole sheet (the nodal solver in Mna.hpp with a
// PNP added; the transistors from their datasheets -- 2N3904 hFE ~220 typical, 2N3906 hFE 100-300 -- and
// a 1N4148-class diode, Vf 0.69 V at 10 mA):
//
//  * The trigger charges the 10 uF hold cap through a coupling cap and two diodes, so the supply peaks at
//    about (Vtrig - 0.9) * C1 / (C1 + Chold): 6.3 V for a 13.5 V trigger. That supply is the oscillator's
//    only power; it drains through the 1 k and LED, the 1 M bleed and the oscillator's own pulses.
//  * The supply stepping up finds the timing node at 0 V, which charges to the threshold through the
//    pitch resistor: the pair fires once, at the full supply, ~0.75 ms later at 1 M. That is the hit.
//  * It then fires every ~0.9 * (R + 22 k) * 10 nF, each pulse the height of the supply at that moment
//    and ~370 us wide. The amplitude therefore decays with the envelope (3.3 V, 2.2, 1.7, 1.45 ... at 1 M).
//  * Near 0.7 V the pair can barely fire: the pulses narrow to ~60 us and come faster (the "zip"), and stop.
//
// This is fitted to that simulation rather than being it: the circuit sits on the edge between latching
// and oscillating and its tail depends on part gain (3 to 28 pulses by 90 ms across the datasheet's
// hFE spread), so this follows the part that does not move -- the first pulses, which agree to ~7% at
// the nominal setting -- and documents the rest. At a very slow pitch the first interval runs ~25% short.
// Pure DSP, no Rack types.

#include <cmath>

namespace smurf {

struct Smurf {
	double fs = 48000.0;
	double Vs = 0.0;                 // the hold capacitor, which is the oscillator's supply
	double phase = 0.0;              // relaxation timer, 0..1
	double pT = -1.0;                // seconds since the present pulse began (< 0: none)
	double pW = 0.0, pA = 0.0;       // its width and height
	double startCd = -1.0;           // seconds to the start-up firing (< 0: none pending)
	int sub = 5;
	double dt = 1.0 / (48000.0 * 5);

	// LED branch current against supply, Vs = I*Rled + Vd(I); rebuilt when Rled moves.
	static const int kLedN = 501;
	float ledI[kLedN];
	double ledR = -1.0;

	// Constants of the sheet (and the fit)
	static constexpr double kCoupling = 10e-6;   // the series 10 uF
	static constexpr double kTimingC = 10e-9;    // the 0.01 uF
	static constexpr double kLoad = 380.0;
	static constexpr double kRq = 20e3;          // the oscillator's own quiescent drain on the supply
	static constexpr double kCut = 0.66;         // below this the pair no longer fires

	void setRate(double f) {
		fs = f;
		sub = (int)std::ceil(1.0 / (fs * 5e-6));
		if (sub < 1) sub = 1;
		dt = 1.0 / (fs * sub);
	}
	void reset() { Vs = 0.0; phase = 0.0; pT = -1.0; pW = pA = 0.0; startCd = -1.0; ledR = -1.0; }

	static double lin(const double* x, const double* y, int n, double v) {
		if (v <= x[0]) return y[0];
		if (v >= x[n - 1]) return y[n - 1];
		int i = 1;
		while (x[i] < v) i++;
		double u = (v - x[i - 1]) / (x[i] - x[i - 1]);
		return y[i - 1] + u * (y[i] - y[i - 1]);
	}
	/** Period over (R + 22 k) * 10 nF, against the supply. */
	static double g(double v) {
		static const double x[] = { 0.62, 0.66, 0.69, 0.71, 0.74, 0.78, 0.90, 1.0, 1.2, 1.7, 2.2, 3.2, 6.5 };
		static const double y[] = { 0.30, 0.38, 0.40, 0.52, 0.90, 0.95, 0.92, 0.87, 0.81, 0.79, 0.83, 0.93, 0.95 };
		return lin(x, y, 13, v);
	}
	/** Pulse width in seconds, against the supply. */
	static double width(double v) {
		static const double x[] = { 0.62, 0.66, 0.70, 0.74, 0.78, 0.82, 0.90, 1.0, 1.2, 8.0 };
		static const double y[] = { 55e-6, 60e-6, 75e-6, 150e-6, 250e-6, 300e-6, 340e-6, 355e-6, 365e-6, 370e-6 };
		return lin(x, y, 10, v);
	}

	void buildLed(double rled) {
		ledR = rled;
		for (int k = 0; k < kLedN; k++) {
			double vs = k * 0.05, lo = 0.0, hi = vs < 3.0 ? vs : 3.0;
			for (int it = 0; it < 28; it++) {
				double mid = 0.5 * (lo + hi);
				double id = 1.1e-18 * (std::exp(mid / 0.0517) - 1.0);
				if (id * rled + mid > vs) hi = mid; else lo = mid;
			}
			ledI[k] = (float)(1.1e-18 * (std::exp(lo / 0.0517) - 1.0));
		}
	}
	double ledCurrent(double vs) const {
		double p = vs * 20.0;
		if (p >= kLedN - 1) p = kLedN - 1.001;
		if (p < 0.0) p = 0.0;
		int i = (int)p;
		double u = p - i;
		return ledI[i] + u * (ledI[i + 1] - ledI[i]);
	}

	/** A strike. `vtrig` is the trigger voltage (the sheet was tested with 13.5 V), `chold` the hold capacitor
	    (10 uF on the sheet, 20 uF with its switch closed), `rpitch` the pitch resistor and `ccoupling` the series
	    capacitor in front of it (10 uF on the sheet). */
	void trigger(double vtrig, double chold, double rpitch, double ccoupling = kCoupling) {
		double pk = (vtrig - 0.9) * ccoupling / (ccoupling + chold);
		bool running = Vs > kCut + 0.05;
		if (pk > Vs) Vs = pk;
		if (!running) {
			startCd = 0.78e-3 * std::pow(rpitch / 1e6, 0.9);   // the supply stepped up: Y charges to the threshold
			phase = 0.0;
			pT = -1.0;
		}
	}

	/** One sample: the voltage across the 380 ohm load, which is the sheet's audio out before its 0.47 uF. */
	double process(double rpitch, double chold, double rled) {
		if (Vs <= 0.0 && pT < 0.0) return 0.0;
		if (ledR < 0.0 || std::fabs(rled / ledR - 1.0) > 0.02) buildLed(rled);
		// Edges are ~40 us, not the circuit's microseconds: the sample rate cannot carry those.
		const double edge = 40e-6 > 2.0 / fs ? 40e-6 : 2.0 / fs;
		double out = 0.0;
		for (int s = 0; s < sub; s++) {
			double co = 0.0;
			if (pT >= 0.0) {
				double t = pT;
				if (t >= pW + edge) pT = -1.0;
				else {
					double a = t < edge ? 0.5 * (1.0 - std::cos(3.14159265358979 * t / edge)) : 1.0;
					double b = t > pW ? 0.5 * (1.0 + std::cos(3.14159265358979 * (t - pW) / edge)) : 1.0;
					co = pA * (a < b ? a : b);
					pT += dt;
				}
			}
			double drain = ledCurrent(Vs) + Vs / 1e6 + Vs / kRq + co / kLoad;
			Vs -= drain * dt / chold;
			if (Vs < 0.0) Vs = 0.0;
			if (startCd >= 0.0) {
				startCd -= dt;
				if (startCd < 0.0 && pT < 0.0 && Vs > kCut) { pT = 0.0; pW = 350e-6; pA = Vs * 0.99; phase = 0.0; }
			}
			else if (Vs > kCut && pT < 0.0) {
				phase += dt / ((rpitch + 22e3) * kTimingC * g(Vs));
				if (phase >= 1.0) { phase -= 1.0; pT = 0.0; pW = width(Vs); pA = Vs * 0.99; }
			}
			out = co;
		}
		if (Vs < 0.2 && pT < 0.0) Vs = 0.0;
		return out;
	}
};

}  // namespace smurf

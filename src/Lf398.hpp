#pragma once
// The LF398 monolithic sample-and-hold, as its datasheet specifies it, with an external hold
// capacitor.
//
// Source: TI LF198-N / LF298 / LF398-N / LF198A-N / LF398A-N (SNOSBI3C, Oct 2018)
//   https://www.ti.com/lit/ds/symlink/lf398-n.pdf   (copy: MiawResearch/research17/)
//   Section 6.6 "Electrical Characteristics, LF398-N" (the 25 C column, +-15 V, Ch = 0.01 uF) and
//   the "Typical Characteristics" figures. Everything below is a number from there, or is marked
//   FITTED or ASSUMED.
//
// What is modelled
//   * Acquisition. The datasheet gives three facts: 4 us to 0.1% for a 10 V step on 1000 pF, 20 us
//     on 0.01 uF, and a hold-capacitor charging current of 5 mA with 2 V across the input stage.
//     They are reproduced by a front-end lag (FITTED: tau = 0.6 us) followed by an output stage
//     that charges Ch with a current limited to 5 mA (gm 100 mS, so it is current-limited from
//     50 mV of error up): 4.15 us at 1000 pF, 20.06 us at 0.01 uF, and 0.68% error at 3 us on a
//     10 V step on 1 nF (Schmitz's "3 us gives about 1%"). The fit is the model's one free
//     parameter; the two time points and the current are the datasheet's.
//     The input is a straight line across each audio step; the front-end lag is integrated
//     exactly for it and the current-limited stage in 10 ns sub-steps.
//   * Aperture time (Figure 1, 25 C): hold starts 148 ns (positive input step) or 205 ns (negative)
//     after the logic input drops, linear in junction temperature between the figure's -50, 25 and
//     150 C points. The input keeps being tracked for that long.
//   * Hold step (table: 1 mV typ, 2.5 max at Ch = 0.01 uF, VOUT = 0), inversely proportional to Ch
//     as the footnote says, scaled with the output voltage by Figure 15's 25 C line (read:
//     1.0 at 0 V falling by 2.2% per volt). Its sign is read from Figure 17 (output goes down).
//   * Droop: a constant leakage current into Ch, 30 pA typ / 200 pA max at TJ = 25 C, doubling
//     for every 11 C (footnote 3); dV/dt = I / Ch, linear. The junction is above ambient by the
//     supply current times the supply times the 48.9 C/W PDIP RthJA of section 6.3 (4.5 mA typ,
//     6.5 mA max, 30 V): +6.6 C typ. Figure 4 at 1000 pF, 25 C reads about 0.035 V/s; I / Ch for
//     30 pA is 0.030.
//   * Input offset: 2 mV typ / 7 max, added to what is tracked. ASSUMED positive (the datasheet
//     gives a magnitude).
//   * Gain error 0.004% typ / 0.01% max. Feedthrough in hold: 90 dB typ / 80 dB min attenuation
//     at 1 kHz with Ch = 0.01 uF; Figure 14 shows 10 dB less per decade of Ch less (-80 dB at
//     1000 pF) and flat to about 10 kHz, so a flat gain 10^(-dB/20) is used.
//
// Not modelled: the sample-start output transient (Figure 16: +0.6 V / -0.2 V in the first
// ~1.5 us at 1000 pF, settled inside the sample window), the hold-start transient (Figure 17),
// input bias current (10 nA typ times whatever the source resistance is: nothing, for a Rack
// source), output impedance in hold (0.5 ohm), supply rejection, noise, dielectric absorption of
// the hold capacitor (Figure 2: it is a styrene cap, which the figure says is negligible).
//
// No Rack dependency, so tests/ drives it bare.

#include <cmath>

namespace lf398 {

static const double kIcharge  = 5.0e-3;     // A, hold capacitor charging current (table)
static const double kGm       = 0.1;        // S, output stage (sets where the current limit starts)
static const double kTauFront = 0.6e-6;     // s, FITTED front-end lag (see above)
static const double kSubStep  = 10e-9;      // s

struct Spec {
	double ch         = 1e-9;     // F
	double offset     = 2e-3;     // V
	double gainError  = 4e-5;     // fraction
	double holdStep10n = 1e-3;    // V at Ch = 10 nF
	double leakage25  = 30e-12;   // A at TJ = 25 C, signed: + charges Ch
	double feedDb10n  = 90.0;     // dB at Ch = 10 nF
	double tjC        = 25.0;     // junction temperature
	double holdStepSign = -1.0;
};

/** The datasheet's LF398-N column. Typical or worst case; `ambientC` is the air around the part
    and, with `selfHeat`, the junction sits above it by the dissipation times RthJA. */
inline Spec spec(bool worst, double ch, bool leakage, double leakSign = 1.0,
                 double ambientC = 25.0, bool selfHeat = true) {
	Spec s;
	s.ch = ch;
	s.offset = worst ? 7e-3 : 2e-3;
	s.gainError = worst ? 1e-4 : 4e-5;
	s.holdStep10n = worst ? 2.5e-3 : 1e-3;
	s.feedDb10n = worst ? 80.0 : 90.0;
	s.leakage25 = leakage ? (worst ? 200e-12 : 30e-12) * (leakSign < 0.0 ? -1.0 : 1.0) : 0.0;
	double isup = worst ? 6.5e-3 : 4.5e-3;
	s.tjC = ambientC + (selfHeat ? isup * 30.0 * 48.9 : 0.0);
	return s;
}

inline double leakageAmps(const Spec& s) { return s.leakage25 * std::pow(2.0, (s.tjC - 25.0) / 11.0); }
inline double droopRate(const Spec& s) { return leakageAmps(s) / s.ch; }   // V/s
inline double holdStep(const Spec& s, double vout) {
	double m = 1.0 - 0.022 * vout;
	if (m < 0.2) m = 0.2;
	return s.holdStepSign * s.holdStep10n * (10e-9 / s.ch) * m;
}
inline double feedthrough(const Spec& s) {
	return std::pow(10.0, -(s.feedDb10n - 10.0 * std::log10(10e-9 / s.ch)) / 20.0);
}
/** Figure 1, read at -50 / 25 / 150 C. */
inline double aperture(const Spec& s, bool positiveStep) {
	static const double posT[3] = { 62e-9, 148e-9, 252e-9 };
	static const double negT[3] = { 88e-9, 205e-9, 352e-9 };
	const double* y = positiveStep ? posT : negT;
	double t = s.tjC;
	if (t < -50.0) t = -50.0;
	if (t > 150.0) t = 150.0;
	if (t <= 25.0) return y[0] + (y[1] - y[0]) * (t + 50.0) / 75.0;
	return y[1] + (y[2] - y[1]) * (t - 25.0) / 125.0;
}

/** A stretch of the step during which the logic input is high: [a, b] seconds from the step's
    start; b == dt means it is still high at the end. */
struct Window { double a, b; };

struct Channel {
	double v = 0.0;          // volts on the hold capacitor
	double vc = 0.0;         // the front-end's lagged copy of the input
	double apLeft = 0.0;     // seconds of aperture still to run into the next step
	bool primed = false;
	bool tracking = false;   // output stage connected to the capacitor at the end of the step
	double lastOut = 0.0;

	void reset() { *this = Channel(); }

	/** The front-end lag over `h` seconds for an input running linearly from `a` to `a + b h`. */
	static double lag(double vc0, double a, double b, double h) {
		const double tau = kTauFront;
		return (a + b * h) - b * tau + (vc0 - a + b * tau) * std::exp(-h / tau);
	}
	static double lagFull(double vc0, double a, double b, double e) {   // e = exp(-h / tau)
		const double tau = kTauFront;
		return (a + b * kSubStep) - b * tau + (vc0 - a + b * tau) * e;
	}

	/** One audio step of `dt` seconds. The input runs linearly from `vin0` to `vin1`. `win` are
	    the stretches the logic input is high, in order, `n` of them. Returns the output voltage at
	    the end of the step. */
	double step(const Spec& s, double dt, double vin0, double vin1, const Window* win, int n) {
		if (!primed) { primed = true; vc = vin0 + s.offset; }
		const double slope = (vin1 - vin0) / dt;
		const bool rising = vin1 >= vin0;
		const double gain = 1.0 - s.gainError;
		// target(t) = gain * vin(t) + offset, a line a0 + b t
		const double a0 = gain * vin0 + s.offset;
		const double b = gain * slope;
		const double leak = droopRate(s);
		static const double eFront = std::exp(-kSubStep / kTauFront);
		double kCh = -1.0;     // exp(-sub-step / (Ch / gm)), only worked out if something is tracked

		// the tracking stretches: carried aperture, then each window plus its aperture
		Window iv[10];
		bool open[10];
		int m = 0;
		if (apLeft > 0.0) {
			iv[0].a = 0.0; iv[0].b = apLeft < dt ? apLeft : dt; open[0] = false; m = 1;
		}
		for (int k = 0; k < n && m < 9; k++) {
			double a = win[k].a, e;
			bool op = win[k].b >= dt;
			if (op) e = dt;
			else e = win[k].b + aperture(s, rising);
			if (m > 0 && a <= iv[m - 1].b) {
				if (e > iv[m - 1].b) { iv[m - 1].b = e; open[m - 1] = op; }
			}
			else { iv[m].a = a; iv[m].b = e; open[m] = op; m++; }
		}
		apLeft = 0.0;
		double t = 0.0;
		bool trackingNow = false;
		double holdOutVin = vin1;
		for (int k = 0; k < m; k++) {
			double a = iv[k].a, e = iv[k].b;
			bool spill = e > dt;
			if (spill) { apLeft = e - dt; e = dt; }
			if (a > t) {   // hold until the window starts
				v += leak * (a - t);
				vc = lag(vc, a0 + b * t, b, a - t);
				t = a;
			}
			// track from a to e
			double tt = a;
			if (kCh < 0.0) kCh = 1.0 - std::exp(-kSubStep * kGm / s.ch);
			while (tt < e - 1e-15) {
				double h = e - tt < kSubStep ? e - tt : kSubStep;
				const bool full = h >= kSubStep;
				double vc1 = full ? lagFull(vc, a0 + b * tt, b, eFront) : lag(vc, a0 + b * tt, b, h);
				// the lagged target is taken as the mean over the sub-step
				double tgt = 0.5 * (vc + vc1);
				double err = tgt - v;
				const double sat = kIcharge / kGm;      // error below which the stage is linear
				const double sgn = err >= 0.0 ? 1.0 : -1.0;
				double hl = h;                           // time left for the linear part
				if (std::fabs(err) > sat) {
					double over = std::fabs(err) - sat;
					double ts = over * s.ch / kIcharge;  // time to slew down to the knee
					if (ts >= h) {
						v += sgn * kIcharge * h / s.ch;
						hl = 0.0;
					}
					else {
						v += sgn * over;
						hl = h - ts;
					}
					err = tgt - v;
				}
				if (hl > 0.0)
					v += err * (hl >= kSubStep ? kCh : 1.0 - std::exp(-hl * kGm / s.ch));
				vc = vc1;
				tt += h;
			}
			t = e;
			if (open[k] || spill) trackingNow = true;
			else {
				// hold begins: the step
				v += holdStep(s, v);
				trackingNow = false;
			}
		}
		if (t < dt) {
			v += leak * (dt - t);
			vc = lag(vc, a0 + b * t, b, dt - t);
		}
		tracking = trackingNow;
		double out = v;
		if (!trackingNow) out += feedthrough(s) * holdOutVin;
		lastOut = out;
		return out;
	}
};

} // namespace lf398

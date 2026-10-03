#pragma once
// The Roland TR-808 bass drum, after Werner, Abel & Smith, "A Physically-Informed,
// Circuit-Bendable, Digital Model of the Roland TR-808 Bass Drum Circuit" (DAFx-14,
// Erlangen, 2014). KICK's BRIDGE model.
//
// An earlier BRIDGE struck an eight-mode Bessel membrane. That is a tom, and the 808 is not
// a drum head: it is a bridged-T band-pass, in the feedback of an op-amp, rung by a 1 ms
// pulse. What the paper finds, and what this reproduces, is that the 808's character is
// three interacting things and not a pitch sweep:
//
//   1. The trigger passes a shelf-and-diode PULSE SHAPER, so *both* edges of the 1 ms pulse
//      kick the resonator, the falling one clipped to about -0.7 V by the diode.
//   2. For ~6 ms after the trigger the ENVELOPE GENERATOR turns Q43 on, which takes the
//      bridged-T's shunt leg from R165+R166 (53.8 k) down to R166 alone (6.8 k). Reffective
//      falls from 45 k to 6.6 k and the centre frequency rises by more than an octave
//      (49.8 Hz -> ~130 Hz) with a higher Q. Too brief to be heard as a pitch; it is what
//      makes the attack punchy and crisp.
//   3. A RETRIGGERING PULSE (C39, R161, D52) and Q43's own nonlinearity act on the same node
//      afterwards, and give the "sigh": the pitch settling down by ~17% over ~100 ms.
//
// Decay is the feedback buffer: a high shelf, VR6 setting how much of the resonance is fed
// back round the bridged-T. Tone is a one-pole low-pass. Retriggering does not reset anything,
// so there is no machine-gun effect; nothing here is zeroed on a strike.
//
// The bridged-T is not three filters but one two-state system with three inputs. With C41 =
// C42 = C, the op-amp's virtual short (V- = V+), and the junction of R161, R170, C41, C42 and
// R166 called Vcomm:
//
//     a = V(C41) = Vcomm - V+          b = V(C42) = Vbt - Vcomm
//     C a' = -(a + b) / R167
//     C b' = C a' + G (a + V+) - Iin         Iin = Vfb/R170 + Vrp/R161
//     G    = 1/R170 + 1/R161 + Gshunt
//     Vbt  = a + b + V+
//
// which reproduces the paper's centre frequency, 1/(2 pi sqrt(Reff R167 C41 C42)) = 49.8 Hz
// at rest. The shunt conductance carries both Q43 effects, so it is the only thing that is
// time-varying. Everything runs in double: the states are millivolt-scale, the time constants
// run from 0.1 ms to seconds, and a float state is not worth the argument.
//
// Component values are the paper's Fig. 1. Assumed, because the paper does not give them:
// the envelope generator's output swing (15 V, the supply), its duration (6 ms, here set by
// BEND), and the tone control's position (fixed; this module has no jack or knob for it).
// Pure DSP, no Rack types.

#include <cmath>
#include <cstdint>

namespace bd808 {

static const double kR161 = 1e6, kR165 = 47e3, kR166 = 6.8e3, kR167 = 1e6, kR170 = 470e3;
static const double kR162 = 4.7e3, kR163 = 100e3, kC40 = 0.015e-6;      // pulse shaper
static const double kC39 = 0.033e-6;                                    // retriggering pulse
static const double kC0 = 0.015e-6;                                     // C41 = C42, stock
static const double kR164 = 47e3, kR169 = 47e3, kVR6 = 500e3, kC43 = 33e-6;   // feedback buffer
static const double kR171 = 220.0, kR172 = 10e3, kVR5 = 10e3, kC45 = 0.1e-6;  // tone
static const double kStockHz = 49.8;                                   // stock Fc, Reff 45.4 k
static const double kVenv = 12.3;       // the envelope generator's plateau (Werner, thesis Fig. 4.10)
// Q43's collector current against Vcomm (the paper's eq. 8, fit to SPICE).
static const double kAlpha = 14.3150, kV0 = -0.5560, kM = 1.4765e-5;

/** First-order section (beta1 s + beta0) / (alpha1 s + alpha0) by the bilinear transform,
    transposed direct form II. */
struct Bilin1 {
	double b0 = 1, b1 = 0, a1 = 0, z = 0;
	void set(double be1, double be0, double al1, double al0, double fs) {
		double c = 2.0 * fs;
		double a0 = al1 * c + al0;
		b0 = (be1 * c + be0) / a0;
		b1 = (be0 - be1 * c) / a0;
		a1 = (al0 - al1 * c) / a0;
	}
	void reset() { z = 0; }
	inline double process(double x) {
		double y = b0 * x + z;
		z = b1 * x - a1 * y;
		return y;
	}
};

/** VR6's position that gives a ringing of `tNeed` seconds (to -60 dB) at stock tuning.
    Measured on this model (amplitude-independent, and proportional to the capacitors): the
    table is that measurement. The shortest the circuit rings is ~0.18 s, at VR6 near 5%;
    `floorT` returns that, and the caller gates the rest. */
inline double kForT60(double tNeed) {
	static const double kk[] = { 0.05, 0.10, 0.20, 0.30, 0.40, 0.50, 0.60, 0.70, 0.80, 0.90, 1.00 };
	static const double tt[] = { 0.177, 0.224, 0.370, 0.539, 0.739, 0.995, 1.292, 1.665, 2.150, 2.818, 3.763 };
	const int n = 11;
	if (tNeed <= tt[0]) return kk[0];
	if (tNeed >= tt[n - 1]) return kk[n - 1];
	int i = 1;
	while (tt[i] < tNeed) i++;
	double u = (tNeed - tt[i - 1]) / (tt[i] - tt[i - 1]);
	return kk[i - 1] + u * (kk[i] - kk[i - 1]);
}
static const double kFloorT60 = 0.177;

/** The diode that will not let the node swing below about a diode drop (eqs. 4): positive
    voltages pass, negative ones are clipped. */
inline double diodeClip(double v) { return v >= 0.0 ? v : 0.71 * (std::exp(v) - 1.0); }

inline double softplus(double x) { return x > 30.0 ? x : std::log1p(std::exp(x)); }

struct Bridge808 {
	double fs = 48000.0;

	// Signal path state
	Bilin1 shaper, retrig, feedback, tone;
	double a = 0, b = 0;            // the bridged-T's two capacitor voltages
	double vfb = 0;                 // the feedback buffer's last output (one sample of delay)
	double vcomm = 0;               // last Vcomm, for Q43's nonlinearity
	// Strike state
	double gateLeft = 0;            // seconds of trigger pulse left
	double vtrig = 0;               // the trigger voltage
	double envLeft = 0;             // seconds of envelope-generator high left
	double envTarget = 0, env = 0;  // the envelope's output, smoothed (0..1)
	double envLen = 0.006;
	double envGr = 0, envGf = 0;    // rise and fall smoothing: ~0.5 ms up, ~0.08 ms down
	// Cached
	double lastLam = -1, lastK = -1;

	void setRate(double fs_) {
		fs = fs_;
		// Fig. 4.10 of the thesis: the envelope is ~85% of the way up after the 1 ms trigger,
		// plateaus at 12.3 V, and drops in a few hundred microseconds at ~5.4 ms.
		envGr = 1.0 - std::exp(-1.0 / (0.0005 * fs));
		envGf = 1.0 - std::exp(-1.0 / (0.00008 * fs));
		retrig.set(kR161 * kC39, 0.0, kR161 * kC39, 1.0, fs);
		// Tone: fixed. Req = R171 + R172 VR5 l / (R172 + VR5 l), a low-pass R C45.
		const double l = 0.05;
		double req = kR171 + (kR172 * kVR5 * l) / (kR172 + kVR5 * l);
		tone.set(0.0, 1.0, req * kC45, 1.0, fs);
		lastLam = lastK = -1;
	}

	void reset() {
		shaper.reset(); retrig.reset(); feedback.reset(); tone.reset();
		a = b = vfb = vcomm = 0;
		gateLeft = vtrig = envLeft = envTarget = env = 0;
		lastLam = lastK = -1;
	}

	/** A strike. `volts` is the trigger voltage (the accent: 4-14 V on the machine), `envSec`
	    how long the envelope generator holds Q43 on. Nothing is reset. */
	void trigger(double volts, double envSec) {
		vtrig = volts;
		gateLeft = 0.001;            // the CPU's 1 ms pulse
		envLen = envSec;
		envLeft = envSec;
	}

	/** One sample. `lam` scales C41 and C42 (so Fc = 49.8 Hz / lam), `k` is VR6's position
	    0..1, the DECAY. Returns the tone-filtered output, in volts. */
	inline double process(double lam, double k) {
		// Trigger pulse and its shaper
		double vt = gateLeft > 0 ? vtrig : 0.0;
		if (gateLeft > 0) gateLeft -= 1.0 / fs;
		if (lam != lastLam || k != lastK) {
			lastLam = lam; lastK = k;
			shaper.set(kR162 * kR163 * kC40, kR162, kR162 * kR163 * kC40, kR162 + kR163, fs);
			double vr = kVR6 * k;
			feedback.set(-kR169 * vr * kC43, -kR169, kR164 * (kR169 + vr) * kC43, kR164, fs);
		}
		double vp = diodeClip(shaper.process(vt));

		// Envelope generator: high for envLen, with a short RC edge either way
		if (envLeft > 0) { envLeft -= 1.0 / fs; envTarget = 1.0; } else envTarget = 0.0;
		env += (envTarget - env) * (envTarget > env ? envGr : envGf);
		double vrp = -diodeClip(-retrig.process(kVenv * env));   // D52 clips the *positive* side

		// Q43 and the shunt leg. Gshunt is the conductance Vcomm sees through R166 + R165
		// with Q43's collector current taking its share; with the envelope high Q43
		// saturates and the leg is R166 alone.
		double vc = vcomm;
		double vg = std::fabs(vc) < 1e-3 ? (vc < 0 ? -1e-3 : 1e-3) : vc;
		double ic = -kM / kAlpha * softplus(-kAlpha * (vc - kV0));
		double gnl = (1.0 + kR165 * ic / vg) / (kR165 + kR166);
		if (gnl < 0.2 / (kR165 + kR166)) gnl = 0.2 / (kR165 + kR166);
		double gsh = gnl + (1.0 / kR166 - gnl) * env;
		double G = 1.0 / kR170 + 1.0 / kR161 + gsh;

		// The bridged-T, trapezoidal. x' = A x + B u, C = lam C0.
		double C = kC0 * lam;
		double k1 = 1.0 / (kR167 * C), g = G / C;
		double iin = vfb / kR170 + vrp / kR161;
		// A = [[-k1, -k1], [g - k1, -k1]],  forcing f = [0, g vp - iin / C]
		double h2 = 0.5 / fs;
		double f = g * vp - iin / C;
		// (I - h2 A) x1 = (I + h2 A) x0 + 2 h2 f   (the forcing taken at one value per sample)
		double m11 = 1 + h2 * k1, m12 = h2 * k1, m21 = -h2 * (g - k1), m22 = 1 + h2 * k1;
		double r1 = (1 - h2 * k1) * a - h2 * k1 * b;
		double r2 = h2 * (g - k1) * a + (1 - h2 * k1) * b + 2 * h2 * f;
		double det = m11 * m22 - m12 * m21;
		a = (r1 * m22 - m12 * r2) / det;
		b = (m11 * r2 - m21 * r1) / det;
		vcomm = a + vp;
		double vbt = a + b + vp;
		if (vbt > 13.0) vbt = 13.0; else if (vbt < -13.0) vbt = -13.0;

		// The feedback buffer, for the next sample
		vfb = feedback.process(vbt);
		if (vfb > 13.0) vfb = 13.0; else if (vfb < -13.0) vfb = -13.0;

		return tone.process(vbt);
	}
};

}  // namespace bd808

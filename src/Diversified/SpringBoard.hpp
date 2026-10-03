#pragma once
/** Diversified program 101: Kristian Blasol's Day 12 "Spring reverb with speaker and piezo"
 * (EasyEDA, rev 1.0, 2020-08-24; MiawResearch/day12/spring.pdf), solved.
 *
 * The drawing, read directly:
 *
 *   input stage   input jack -> RP1 (10k pot) wiper -> C1 10u (+ to the base) -> Q1 2N2222 base.
 *                 R2 1k base to ground. R1 10k collector to base (the bias: collector feedback).
 *                 Emitter to ground. Collector -> speaker (J2) -> speaker (J3) -> VCC. C2 1u
 *                 across VCC and ground.
 *   output stage  piezo -> C3 100n -> U1.1 (NE5532) pin 3 (+), R3 1k from pin 3 to ground.
 *                 Pin 2 (-) to ground through R4 1k and to RP2 (100k), whose wiper goes to the
 *                 output pin 1: gain 1 + RP2/R4 = 1 .. 101. Pin 1 -> C4 100n -> U1.2 pin 5 (+).
 *                 U1.2 is a follower (pin 6 tied to pin 7) with R5 1k from that node to ground,
 *                 so R5 is the follower's LOAD. Pin 7 is the output jack. V+ / V- on the op-amps.
 *   the spring    "Put the spring here, attach to speaker and piezo": the drawing has no tank.
 *
 * Differences from the brief's description that the drawing forced:
 *   - R5 is not a bias resistor for U1.2's (+) input; it hangs on the output/(-) node. So U1.2's
 *     (+) pin, behind C4, has NO DC path to ground at all. A real NE5532 draws ~200 nA of bias
 *     current (TI SLOS075K, IIB), which a 100 nF with nothing to leak it would integrate to a
 *     rail in seconds: the drawing cannot work as drawn and this is a drawing error. We add a
 *     100 k from pin 5 to ground (ASSUMED, the smallest repair: C4 / 100k = 16 Hz corner, 20 mV
 *     of offset at the nominal bias current, which is not modelled). Change kBiasU12 to move it.
 *   - The first stage's "1.6 kHz" corner is R3 * C3 alone. A piezo is a source in series with
 *     its own capacitance (7BB-27-4: 20 nF +-30 %, Murata), which is in series with C3, so the
 *     corner is 1 / (2 pi R3 (Cp C3 / (Cp + C3))) = 9.5 kHz for the default Cp.
 *
 * What is solved and what is assumed is listed beside each constant. Nothing here is a tuned
 * "sound": every number is a part value, a datasheet figure, or marked ASSUMED.
 */
#include <cmath>
#include <algorithm>
#include "../Mna.hpp"
#include "../OpAmp.hpp"
#include "../SpringTank.hpp"

namespace springboard {

// --- schematic parts -----------------------------------------------------------------
static const double kRP1 = 10e3;          // input pot, linear (no taper is marked)
static const double kC1 = 10e-6;
static const double kR2 = 1e3;            // base to ground
static const double kR1 = 10e3;           // collector to base
static const double kC3 = 100e-9;
static const double kR3 = 1e3;
static const double kR4 = 1e3;
static const double kRP2 = 100e3;
static const double kC4 = 100e-9;
static const double kR5 = 1e3;            // U1.2's load
// --- not on the drawing --------------------------------------------------------------
static const double kVcc = 12.0;          // ASSUMED: Eurorack / Rack +12 V rail, ideal. With an ideal rail C2 (1 uF) does nothing.
static const double kRailPos = 12.0, kRailNeg = -12.0;   // ASSUMED: op-amp supply
static const double kBiasU12 = 100e3;     // ASSUMED, see above
static const double kPiezoC = 20e-9;      // 7BB-27-4, 20 nF +-30 % at 1 kHz (Murata)
static const double kVoltsPerRackVolt = 1.0;   // ASSUMED: a Rack +-5 V signal is +-5 V at the input jack
// 2N2222A, the SPICE model that circulates for it (Is 14.34 fA, BF 255.9, BR 6.092, VAF 74.03,
// Rb 10 ohm). Not modelled from that set: NE = 1.307 base leakage, IKF = 0.2847 A high-current
// roll-off, RC = 1 ohm, junction capacitances (CJE 22 pF, CJC 7.3 pF, TF 0.41 ns).
static const double kQIs = 14.34e-15, kQBF = 255.9, kQBR = 6.092, kQVAF = 74.03, kQRb = 10.0;
// The speaker: Visaton K 50 SQ (8 ohm, 51 mm, Fs 480 Hz, Qes 7.32, Qms 6.42, Re 7.3 ohm,
// Mms 0.4 g, Bl 1.12 T m; the rs-online / Visaton data). The datasheet gives no voice-coil
// inductance: Le = 0.1 mH is ASSUMED, typical of a 2" driver.
static const double kSpkRe = 7.3, kSpkLe = 0.1e-3, kSpkFs = 480.0, kSpkQes = 7.32, kSpkQms = 6.42, kSpkBl = 1.12;
// How far the pickup end of the spring moves per volt the piezo makes: the one number between
// a spring's motion and a voltage that nothing in the drawing or the datasheets pins down.
// ASSUMED, set so the default patch is about unity gain (see the test that measures it).
static const double kPiezoVoltsPerMetre = 2.7e4;
// The spring's pickup end senses displacement (a piezo's charge follows strain); the tank's
// waveguide carries velocity, so it is integrated, leakily (ASSUMED 5 Hz).
static const double kSenseLeakHz = 5.0;
// A knock on the box (the AUX gate): a 1 ms half-sine velocity pulse into the driven end.
static const double kKnockVelocity = 0.5;     // m/s peak, ASSUMED
static const double kKnockSeconds = 1e-3;

/** NE5532 (TI SLOS075K, +-15 V, 25 C): unity-gain bandwidth 12 MHz typ, slew 5 V/us typ (the
    same document's text still says 9 V/us, the table and its change log say 5; older issues
    10 MHz / 9 V/us), AVD 100 V/mV typ at RL >= 2k, Ri 300 k typ. The output swing is not in
    this revision; from the older issues' VOM (+-13 V typ at 600 ohm, +-13.5 at 2k on +-15 V),
    recalled and NOT re-checked, ASSUMED: 33.5 ohm behind an EMF 1.27 V under the rail. */
inline opamp::Spec ne5532() {
	opamp::Spec s = {};
	s.name = "NE5532";
	s.gbw = 12e6;
	s.slew = 5e6;
	s.a0 = 1e5;
	s.ri = 3e5;
	s.hSat = 1.27; s.hPerVolt = 0.0; s.rs = 33.5;
	return s;
}

/** The input stage: pot, C1, Q1 with its bias network, and the speaker, solved together. */
struct Driver {
	enum { W, B, Bb, C, NA, NB, NODES };
	mna::Circuit ck;
	double h = 1.0 / 48000.0;
	int rTop = 0, rBot = 0, rLe = 0, rLes = 0, iLe = 0, iLes = 0, rRe = 0;
	double gLe = 0, gLes = 0, ihLe = 0, ihLes = 0;     // inductor companions
	double lMot = 0, cMot = 0, rMot = 0;
	double pot = 0.5;
	bool ok = true;
	// the transistor's set, members so a test can break it
	double qIs = kQIs, qBF = kQBF, qBR = kQBR, qVAF = kQVAF;

	void setRate(double sr) {
		using namespace mna;
		h = 1.0 / sr;
		ck = Circuit();
		ck.n = NODES;
		ck.h = h;
		double ws = 2.0 * 3.14159265358979323846 * kSpkFs;
		cMot = kSpkQes / (ws * kSpkRe);                 // Mms / Bl^2
		lMot = 1.0 / (ws * ws * cMot);                  // Cms Bl^2
		rMot = kSpkRe * kSpkQms / kSpkQes;              // Bl^2 / Rms
		rTop = ck.addResistor(fixed(0), W, 1.0);
		rBot = ck.addResistor(W, GND, 1.0);
		setPot(pot);
		ck.addCapacitor(W, B, kC1);
		ck.addResistor(B, GND, kR2);
		ck.addResistor(C, B, kR1);
		ck.addResistor(B, Bb, kQRb);
		ck.addNpn(C, Bb, GND, qIs, qBF, qBR, qVAF);
		rLe = ck.addResistor(fixed(1), NA, 1e-3);
		iLe = ck.addCurrent(fixed(1), NA);
		rRe = ck.addResistor(NA, NB, kSpkRe);
		ck.addResistor(NB, C, rMot);
		ck.addCapacitor(NB, C, cMot);
		rLes = ck.addResistor(NB, C, 1e-3);
		iLes = ck.addCurrent(NB, C);
		restart();
	}

	void setPot(double x) {
		pot = std::min(1.0, std::max(0.0, x));
		ck.setResistor(rTop, std::max(1.0, (1.0 - pot) * kRP1));
		ck.setResistor(rBot, std::max(1.0, pot * kRP1));
	}

	/** Operating point with the signal at zero, inductors as shorts, then switch to
	    time-stepping with the inductors as trapezoid companions. */
	void restart() {
		ck.setResistor(rLe, 1e-3); ck.setResistor(rLes, 1e-3);
		ck.setCurrent(iLe, 0.0); ck.setCurrent(iLes, 0.0);
		double target[2] = {0.0, kVcc};
		ok = ck.solveDc(target, 2);
		double i0 = (ck.v[NA] - ck.v[NB]) / kSpkRe;      // the series current: through Le, Re and Les
		gLe = h / (2.0 * kSpkLe);
		gLes = h / (2.0 * lMot);
		ck.setResistor(rLe, 1.0 / gLe);
		ck.setResistor(rLes, 1.0 / gLes);
		ihLe = i0;                                        // v across a DC inductor is 0
		ihLes = i0;
		ck.setCurrent(iLe, ihLe);
		ck.setCurrent(iLes, ihLes);
		ck.fixedV[0] = 0.0; ck.fixedV[1] = kVcc;
	}

	/** One sample. `vin` is the voltage at the input jack. */
	void step(double vin) {
		ck.fixedV[0] = vin;
		ck.fixedV[1] = kVcc;
		int it = ck.step(30);
		ok = it > 0;
		double vLe = kVcc - ck.v[NA];
		double iL = gLe * vLe + ihLe;
		ihLe = iL + gLe * vLe;
		ck.setCurrent(iLe, ihLe);
		double vLes = ck.v[NB] - ck.v[C];
		double iM = gLes * vLes + ihLes;
		ihLes = iM + gLes * vLes;
		ck.setCurrent(iLes, ihLes);
	}

	double motionalVolts() const { return ck.v[NB] - ck.v[C]; }       // = Bl v
	double speakerAmps() const { return (ck.v[NA] - ck.v[NB]) / kSpkRe; }
	double collectorVolts() const { return ck.v[C]; }
	double baseVolts() const { return ck.v[B]; }
};

/** The pickup: piezo, C3 / R3, U1.1 with RP2 and R4, C4 / bias, U1.2 follower into R5. */
struct Pickup {
	opamp::NonInvertingStage s1, s2;
	double piezoC = kPiezoC;          // a member so a test can break it
	double dt = 1.0 / 48000.0;
	double vc1 = 0.0, vc2 = 0.0;      // the two coupling capacitors' charge
	double emfPrev = 0.0, y1Prev = 0.0;
	double rp2 = 0.0;

	void setRate(double sr) {
		dt = 1.0 / sr;
		s1.op.setSpec(ne5532()); s2.op.setSpec(ne5532());
		s1.op.setSupply(kRailPos, kRailNeg); s2.op.setSupply(kRailPos, kRailNeg);
		s1.op.setLoad(kBiasU12, 0.0);          // C4's far side, through the bias resistor
		s2.op.setLoad(kR5, 0.0);
		s1.rg = kR4; s1.cg = 0.0; s1.cf = 0.0;
		s2.rf = 0.0;
		setGain(rp2 / kRP2);
		clear();
	}
	/** RP2's wiper as a fraction of its travel: the feedback resistance is that fraction of 100k. */
	void setGain(double frac) {
		frac = std::min(1.0, std::max(0.0, frac));
		rp2 = frac * kRP2;
		s1.rf = rp2;
	}
	static double gain(double rp2Ohms) { return 1.0 + rp2Ohms / kR4; }
	void clear() { s1.reset(0.0); s2.reset(0.0); vc1 = vc2 = 0.0; emfPrev = y1Prev = 0.0; }

	/** `emf` is the piezo's open-circuit voltage. Returns the output jack. */
	double process(double emf) {
		// piezo source -> Cp -> C3 -> pin 3 with R3 to ground: a first-order highpass of the
		// series capacitance. The capacitor charges toward emf at tau = R3 * Cs; trapezoid rule
		// with the step prewarped so the corner is exactly 1 / (2 pi tau) at any sample rate.
		double cs = piezoC * kC3 / (piezoC + kC3);
		double a1 = std::tan(0.5 * dt / (kR3 * cs));
		vc1 = (vc1 * (1.0 - a1) + a1 * (emfPrev + emf)) / (1.0 + a1);
		emfPrev = emf;
		double vp = emf - vc1;
		double y1 = s1.step(vp, dt);
		double a2 = std::tan(0.5 * dt / (kBiasU12 * kC4));
		vc2 = (vc2 * (1.0 - a2) + a2 * (y1Prev + y1)) / (1.0 + a2);
		y1Prev = y1;
		double vp2 = y1 - vc2;
		return s2.step(vp2, dt);
	}
};

/** The whole board with its tank. Rack volts in, Rack volts out. */
struct Board {
	Driver drv;
	Pickup pick;
	springtank::Tank tank;
	springtank::TankPreset const* preset = nullptr;
	int tankId = 0;
	double sr = 48000.0;
	double disp = 0.0;                // spring end displacement (leaky integral of velocity)
	double leak = 0.0;
	double knock = 0.0;               // samples left in the knock pulse
	bool prevGate = false;
	double rt60 = 2.0, toneHz = 3000.0;

	void setSampleRate(double rate) {
		sr = rate;
		drv.setRate(rate);
		pick.setRate(rate);
		leak = 2.0 * 3.14159265358979323846 * kSenseLeakHz / rate;
		buildTank();
	}
	void buildTank() {
		preset = &springtank::tankPreset(tankId);
		tank.setup(preset->spring, preset->count, sr);
		tank.setLoss(rt60, toneHz);
	}
	void setTank(int id) {
		id = id == 1 ? 1 : 0;
		if (id == tankId && preset) return;
		tankId = id;
		buildTank();
		disp = 0.0;
	}
	void clear() {
		tank.clear();
		pick.clear();
		drv.restart();
		disp = 0.0; knock = 0.0; prevGate = false;
	}
	/** pot: RP1 0..1. dwell: seconds of RT60. toneHz: the loop's damping. rp2: 0..1 of its travel. */
	void setParams(double pot, double rt60s, double tone, double rp2Frac) {
		drv.setPot(pot);
		if (std::fabs(rt60s - rt60) > 1e-6 || std::fabs(tone - toneHz) > 1e-6) {
			rt60 = rt60s; toneHz = tone;
			tank.setLoss(rt60, toneHz);
		}
		pick.setGain(rp2Frac);
	}

	double process(double rackIn, bool gate) {
		drv.step(rackIn * kVoltsPerRackVolt);
		double vel = drv.motionalVolts() / kSpkBl;
		if (gate && !prevGate) knock = kKnockSeconds * sr;
		prevGate = gate;
		if (knock > 0.0) {
			double ph = 1.0 - knock / (kKnockSeconds * sr);
			vel += kKnockVelocity * std::sin(3.14159265358979323846 * ph);
			knock -= 1.0;
		}
		double v = tank.process(vel);
		disp += v / sr - leak * disp;
		double out = pick.process(kPiezoVoltsPerMetre * disp);
		return out;
	}
};

} // namespace springboard

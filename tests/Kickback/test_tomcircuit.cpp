// CD4069UB inverter (src/Cd4069.hpp) and the TomTomTom voice (src/Kickback/TomCircuit.hpp):
// the solver against an independent bisection of the same device equations, the datasheet's
// own numbers, KCL at the rest point, step-size convergence, the comparator, and the
// oscillation threshold, each with a negative control (the model broken, the check must fail).
#include "../../src/Kickback/TomCircuit.hpp"
#include <cmath>
#include <cstdio>
using namespace kickback;

static int checks = 0, failures = 0;
static void check(const char* w, bool ok) { checks++; if (!ok) { failures++; printf("  FAIL  %s\n", w); } }
static void checkv(const char* w, double got, double lo, double hi) {
	checks++; if (!(got >= lo && got <= hi)) { failures++; printf("  FAIL  %s: %.5g not in [%.5g, %.5g]\n", w, got, lo, hi); }
}

// Independent level-1 drain current (written again here, not mna::mosId).
static double id1(double vgs, double vds, const cd4069::Device& d) {
	double z = (vgs - d.vt) / 0.12, vov = z > 30 ? vgs - d.vt : 0.12 * std::log(1 + std::exp(z));
	double f = vds < vov ? vds * (2 * vov - vds) : vov * vov;
	return d.beta * f * (1 + d.lambda * vds) / (1 + d.theta * vov);
}
static double oracleVtc(double vin, double vdd, const cd4069::Params& q) {
	double lo = 0, hi = vdd;
	for (int i = 0; i < 70; i++) {
		double m = 0.5 * (lo + hi);
		double ip = id1(vdd - vin, vdd - m, q.p), in = id1(vin, m, q.n);
		if (ip > in) lo = m; else hi = m;
	}
	return 0.5 * (lo + hi);
}

static double tailAmp(const TomParams& p, double fs, int which, double secs = 1.3) {
	TomCircuit t(p); t.setResistors(kTomBoards[which].r, kTomBoards[which].r10); t.prepare(1.0 / fs);
	double tail = 0;
	for (int i = 0; i < (int)(fs * secs); i++) {
		double tt = i / fs; t.step((tt > 0.01 && tt < 0.03) ? 10.0 : 0.0);
		if (tt > secs - 0.2) tail = std::max(tail, std::fabs(t.out() - t.restOut()));
	}
	return tail;
}
static TomParams withGainScale(double f) { TomParams p; p.chip.n.lambda *= f; p.chip.p.lambda *= f; return p; }

int main() {
	cd4069::Params q = cd4069::typical();
	// 1. solver vs oracle
	double worst = 0;
	for (double vdd : {5.0, 10.0, 12.0, 15.0}) for (int k = 1; k < 40; k++) {
		double vin = vdd * k / 40.0;
		worst = std::max(worst, std::fabs(cd4069::transfer(vin, vdd) - oracleVtc(vin, vdd, q)));
	}
	checkv("VTC solver vs bisection oracle (V)", worst, 0, 2e-3);
	// control: oracle with the P device's lambda broken must disagree
	{ cd4069::Params b = q; b.p.vt += 0.5; double w = 0; for (int k = 1; k < 40; k++) w = std::max(w, std::fabs(cd4069::transfer(0.25 * k, 10) - oracleVtc(0.25 * k, 10, b)));
	  check("control: a different PMOS threshold is caught", w > 0.2); }
	// 2. datasheet numbers (SCHS054E)
	checkv("VOH at Vin=0, VDD=10 (>=9.95)", cd4069::transfer(0, 10), 9.95, 10.0);
	checkv("VOL at Vin=VDD (<=0.05)", cd4069::transfer(10, 10), 0.0, 0.05);
	checkv("VIL: Vin=2 V gives VO>=9 V at VDD=10", cd4069::transfer(2, 10), 9.0, 10.0);
	checkv("VIH: Vin=8 V gives VO<=1 V at VDD=10", cd4069::transfer(8, 10), 0.0, 1.0);
	checkv("VM at 10 V (Fig 3: ~5.2)", cd4069::selfBias(10), 4.9, 5.5);
	checkv("VM at 15 V (Fig 3: ~7.55)", cd4069::selfBias(15), 7.2, 7.9);
	checkv("peak supply current at 10 V (Fig 3: 4.7 mA)", cd4069::supplyCurrent(5.2, 10) * 1e3, 3.5, 5.5);
	{ double gm, gds; double i = mna::mosId(10, 0.5, q.n.beta, q.n.vt, q.n.lambda, q.n.theta, gm, gds) * 1e3;
	  checkv("IOL at VGS=10, VDS=0.5 (Fig 4: 2.2 mA, spec typ 2.6)", i, 1.6, 3.2); }
	checkv("self-biased gain at 12 V (fit target ~-23)", cd4069::selfBiasedGain(12), -30, -17);
	// 3. KCL at the rest point, written out
	{ TomCircuit t; t.setResistors(47e3, 3.9e3); t.prepare(1.0 / 96000);
	  double in1 = t.rest[TomCircuit::IN1], out1 = t.rest[TomCircuit::OUT1];
	  check("rest: feedback carries no current, IN1 = OUT1", std::fabs(in1 - out1) < 1e-6);
	  checkv("rest: OUT1 is the inverter's self-bias", out1, cd4069::selfBias(12) - 1e-3, cd4069::selfBias(12) + 1e-3);
	  check("rest: stage 2 at the same bias", std::fabs(t.rest[TomCircuit::OUT2] - out1) < 1e-4); }
	// 4. comparator: pulse as long as the gate
	for (double gl : {0.005, 0.02, 0.1}) {
		TomCircuit t; t.prepare(1.0 / 96000); double on = 0;
		for (int i = 0; i < 96000 * 0.4; i++) { double tt = i / 96000.0; t.step((tt > 0.01 && tt < 0.01 + gl) ? 10.0 : 0.0); if (t.trig > 5.0) on += 1.0 / 96000; }
		checkv("trigger pulse length ~ gate length", on, gl - 0.001, gl + 0.001);
	}
	{ TomCircuit t; t.prepare(1.0 / 96000); double pk = 0; for (int i = 0; i < 9600; i++) { t.step(i > 960 ? 10.0 : 0.0); pk = std::max(pk, t.trig); }
	  checkv("trigger high level = VCC - 1.1 V", pk, 10.85, 10.95); }
	{ TomCircuit t; t.prepare(1.0 / 96000); double pk = 0; for (int i = 0; i < 9600; i++) { t.step(i > 960 ? 0.05 : 0.0); pk = std::max(pk, t.trig); }
	  check("a 50 mV gate does not trip it (needs ~0.175 V at the junction)", pk < 1.0); }
	{ TomCircuit t; t.prepare(1.0 / 96000); double xmax = -9;
	  for (int i = 0; i < 96000 * 0.2; i++) { double tt = i / 96000.0; t.step((tt > 0.01 && tt < 0.11) ? 10.0 : 0.0);
	    if (tt > 0.1101) xmax = std::max(xmax, t.c.v[TomCircuit::X]); }
	  check("D1 holds X below zero after a long gate falls (no trapezoidal ring-back)", xmax < 0.05); }
	// 5. step-size convergence of the sustained amplitude
	{ double a1 = tailAmp(TomParams(), 48000, 0), a2 = tailAmp(TomParams(), 192000, 0);
	  check("amplitude agrees 48 kHz vs 192 kHz (2 %)", std::fabs(a1 - a2) < 0.02 * a2 && a2 > 0.5); }
	// 6. oscillation threshold: gain 12.6 rings down, 23 sustains, a stiff T never sustains
	checkv("gain ~-12.6: dies", tailAmp(withGainScale(2.0), 48000, 0), 0, 0.01);
	checkv("gain ~-23: sustains", tailAmp(withGainScale(1.0), 48000, 0), 1.0, 3.0);
	{ TomParams p; p.rLow = 50; checkv("stiff T (50 ohm sink): dies", tailAmp(p, 48000, 0), 0, 0.01); }
	// negative control: the same sustain check with the gain broken to -9 must fail
	check("control: gain ~-9 does not sustain", tailAmp(withGainScale(3.0), 48000, 0) < 0.01);
	// R10 matters only through the sink: a different R10 changes the threshold behaviour
	{ TomParams p = withGainScale(1.5); TomCircuit a(p); a.setResistors(47e3, 3.9e3); TomCircuit b(p); b.setResistors(47e3, 30e3);
	  double fs = 48000, ta = 0, tb = 0; a.prepare(1 / fs); b.prepare(1 / fs);
	  for (int i = 0; i < fs * 1.3; i++) { double tt = i / fs, g = (tt > .01 && tt < .03) ? 10 : 0; a.step(g); b.step(g);
	    if (tt > 1.1) { ta = std::max(ta, std::fabs(a.out() - a.restOut())); tb = std::max(tb, std::fabs(b.out() - b.restOut())); } }
	  check("R10 changes the ring (gain -16: 3.9k vs 30k differ)", std::fabs(ta - tb) > 0.05); }
	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}

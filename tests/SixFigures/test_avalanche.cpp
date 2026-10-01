// The reverse-avalanche oscillator's relaxation (Avalanche.hpp): the closed forms against a
// stepped simulation of the capacitor with the junction as a two-voltage switch, and against the
// published measurements of the same circuit (on a 2N2222) the numbers come from.

#include "../../src/SixFigures/Avalanche.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace sixfigures::avalanche;

static int checks = 0, failures = 0;
static void check(const char* what, bool ok) {
	checks++;
	if (!ok) { failures++; printf("  FAIL  %s\n", what); }
}
static void checkv(const char* what, double got, double want, double tol) {
	checks++;
	if (!(std::fabs(got - want) <= tol)) {
		failures++;
		printf("  FAIL  %s: got %.6g, want %.6g (tol %.3g)\n", what, got, want, tol);
	}
}

// An independent simulation: dV/dt = (Vs - V) / (R C), integrated in small steps (midpoint), the
// junction a switch that resets the capacitor to the release voltage when it strikes.
// Returns the period and fills `shape` with V sampled at `n` phases of one settled cycle.
static double simulate(double r, double c, double vcc, double vled, int n, std::vector<double>& shape) {
	double vs = vcc - vled, tau = r * c, h = tau * 1e-5;
	double v = kVRelease, t = 0;
	std::vector<double> tr, vv;
	double lastStrike = -1, period = 0;
	std::vector<double> cycle;
	for (long i = 0; i < 4000000 && period == 0; i++) {
		double k = (vs - v) / tau;
		double mid = v + 0.5 * h * k;
		double nv = v + h * (vs - mid) / tau;
		t += h;
		if (lastStrike >= 0) { cycle.push_back(v); }
		if (nv >= kVStrike) {
			if (lastStrike < 0) { lastStrike = t; cycle.clear(); }
			else period = t - lastStrike;
			nv = kVRelease;
		}
		v = nv;
	}
	shape.resize(n);
	for (int i = 0; i < n; i++) {
		size_t idx = (size_t)((double)i / n * cycle.size());
		shape[i] = (cycle[idx] - kVRelease) / (kVStrike - kVRelease);
	}
	return period;
}

int main() {
	printf("reverse avalanche oscillator\n");

	// the constants
	checkv("the charge's shape factor q = (Vs - strike) / (Vs - release)", q(), (12.0 - 1.8 - 8.2) / (12.0 - 1.8 - 7.3), 1e-12);
	checkv("the swing is 0.9 V", kVStrike - kVRelease, 0.9, 1e-12);

	// closed forms vs the stepped circuit
	bool freqOk = true, shapeOk = true;
	double worstShape = 0;
	for (double vcc : { 11.0, 12.0, 14.0 })
		for (double r : { 1e3, 4.7e3, 11e3 })
			for (double c : { 1e-6, 10e-6 }) {
				std::vector<double> shape;
				double period = simulate(r, c, vcc, kVLed, 64, shape);
				double f = frequency(r, c, vcc, kVLed);
				if (std::fabs(period * f - 1.0) > 1e-3) { freqOk = false; printf("    vcc %.0f R %.0f C %g: period %g vs 1/f %g\n", vcc, r, c, period, 1.0 / f); }
				for (int i = 0; i < 64; i++)
					worstShape = std::fmax(worstShape, std::fabs(shape[i] - charge((double)i / 64, vcc, kVLed)));
			}
	shapeOk = worstShape < 2e-3;
	printf("    worst waveform difference from the stepped circuit %.5f of the swing\n", worstShape);
	check("the closed-form frequency matches the stepped capacitor to 0.1 % (3 supplies, 3 resistances, 2 capacitors)", freqOk);
	check("the closed-form waveform matches the stepped capacitor to 0.2 % of the swing", shapeOk);

	// the waveform's own properties
	checkv("the charge starts at the release voltage", charge(0.0), 0.0, 1e-12);
	checkv("and ends at the strike voltage", charge(1.0), 1.0, 1e-12);
	bool concave = true;
	for (int i = 1; i < 100; i++)
		if (charge(i / 100.0) < i / 100.0) concave = false;      // an RC charge bows above the straight line
	check("an RC charge bows above the straight line between its ends", concave);
	checkv("but only mildly: the mid-point is 0.5 plus under 0.1", charge(0.5), 0.5, 0.1);

	// resistance for frequency, and back
	for (double hz : { 0.05, 5.0, 100.0, 4000.0 }) {
		double c = hz < 10 ? 10e-6 : 1e-6;
		checkv("frequency(resistanceFor(f)) is f", frequency(resistanceFor(hz, c), c) / hz, 1.0, 1e-9);
	}

	// the published measurements of the same circuit
	{
		// 14 V, 1k, 1 mF, a red LED: measured 5.8 Hz
		double f = frequency(1e3, 1e-3, 14.0, 1.8);
		printf("    lcamtuf's circuit (14 V, 1k, 1 mF): model %.2f Hz, measured 5.8 Hz\n", f);
		check("the model is within 20 % of that measurement with the capacitor exactly 1 mF", std::fabs(f / 5.8 - 1.0) < 0.2);
		double cFit = 1e-3 * f / 5.8;
		check("and a capacitor 20 % under its marking (an electrolytic) closes it", std::fabs(frequency(1e3, cFit, 14.0, 1.8) / 5.8 - 1.0) < 1e-9 && cFit > 0.75e-3);
		// that circuit oscillates at 1k, so 1k must be above the latch limit at 14 V
		check("1k is above the latch limit at 14 V (the measured circuit oscillates)", 1e3 > minimumResistance(14.0, 1.8));
		// the capacitor swings 10 V to 9.1 V at the node with the LED on it
		checkv("strike plus the LED is the 10 V the node was read at", kVStrike + 1.8, 10.0, 1e-9);
		checkv("release plus the LED is the 9.1 V it was read at", kVRelease + 1.8, 9.1, 1e-9);
	}

	// the module's range on the board's parts: audio at 1 uF, up to 4 kHz
	{
		double rMin = minimumResistance();
		printf("    latch limit at 12 V: %.0f ohm; 4 kHz at 1 uF needs %.0f ohm\n", rMin, resistanceFor(4000.0, 1e-6));
		check("4 kHz at 1 uF needs more resistance than the latch limit (the module's audio range is reachable)", resistanceFor(4000.0, 1e-6) > rMin);
		check("the board's 1k floor (R1 alone) gives 2.69 kHz at 1 uF (1 / (R C ln(1/q)))", std::fabs(frequency(1e3, 1e-6) / 2691.0 - 1.0) < 0.005);
	}

	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}

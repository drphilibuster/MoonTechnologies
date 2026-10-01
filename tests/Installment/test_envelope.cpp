// INSTALLMENT's AD (TLC555) and AR (TL072) circuits, against independent oracles:
// the diode by bisection, the capacitor by a fine-step RK4, the times by measurement.

#include "../../src/Installment/Envelope.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

using namespace installment::circuit;

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

// Independent diode+R: bisection on the diode's own voltage.
static double oracleI(double delta, double R) {
	if (delta <= 0) return 0;
	double lo = 0, hi = delta;
	for (int k = 0; k < 200; k++) {
		double x = 0.5 * (lo + hi);
		double f = x + R * kDiodeIs * (std::exp(x / kDiodeNVt) - 1.0) - delta;
		if (f > 0) hi = x; else lo = x;
	}
	double x = 0.5 * (lo + hi);
	return kDiodeIs * (std::exp(x / kDiodeNVt) - 1.0);
}

static void testDiode() {
	printf("diode + R\n");
	double Rs[] = { 20, 1e3, 1e5, 1e7 };
	double Ds[] = { 1e-6, 0.1, 0.5, 3, 12, 21 };
	bool match = true, mono = true;
	for (double R : Rs) {
		double prev = -1;
		for (double d : Ds) {
			double g;
			double i = diodeSeries(d, R, g);
			double want = oracleI(d, R);
			if (std::fabs(i - want) > 1e-9 * want + 1e-18) match = false;
			if (i < prev) mono = false;
			prev = i;
			// slope against a centred difference of the oracle
			double h = d * 1e-4;
			double sl = (oracleI(d + h, R) - oracleI(d - h, R)) / (2 * h);
			if (std::fabs(g - sl) > 1e-3 * sl + 1e-15) match = false;
		}
	}
	check("current and slope agree with bisection over R and delta", match);
	check("current rises with the voltage across it", mono);
	double g;
	check("no current when reverse biased", diodeSeries(-3.0, 1e3, g) == 0.0 && g == 0.0);
	check("a NaN gives no current", diodeSeries(std::nan(""), 1e3, g) == 0.0);
	checkv("drop at 1 mA is the 1N4148's ~0.6 V", diodeDrop(1e-3), 0.6, 0.05);
}

// Fine RK4 of the same capacitor.
static double rk4Rc(double v, double src, double rUp, double rDown, double C, double T, double h) {
	auto f = [&](double x) {
		double d = src - x;
		return (d > 0 ? oracleI(d, rUp) : -oracleI(-d, rDown)) / C;
	};
	for (double t = 0; t < T; t += h) {
		double k1 = f(v), k2 = f(v + 0.5 * h * k1), k3 = f(v + 0.5 * h * k2), k4 = f(v + h * k3);
		v += h / 6 * (k1 + 2 * k2 + 2 * k3 + k4);
	}
	return v;
}

static void testRc() {
	printf("diode-steered RC\n");
	// charge from 0 to a 12 V source through 20k, then discharge through 5k, at 48 kHz
	SteeredRc rc;
	double dt = 1.0 / 48000, C = 10e-6;
	for (int n = 0; n < (int)(0.4 / dt); n++) rc.step(12.0, 20e3, 5e3, C, dt);
	double want = rk4Rc(0, 12.0, 20e3, 5e3, C, 0.4, 2e-6);
	checkv("charging: 48 kHz steps vs fine RK4 at 0.4 s", rc.v, want, 0.02 * want);
	double v0 = rc.v;
	for (int n = 0; n < (int)(0.2 / dt); n++) rc.step(0.0, 20e3, 5e3, C, dt);
	want = rk4Rc(v0, 0.0, 20e3, 5e3, C, 0.2, 2e-6);
	checkv("discharging: vs fine RK4 at 0.2 s", rc.v, want, 0.02 * want + 0.01);
	// fast circuit, big step: stays between the rails, never overshoots
	SteeredRc q;
	bool bounded = true;
	for (int n = 0; n < 2000; n++) {
		q.step(n % 100 < 50 ? 10.5 : -10.5, 20.0, 20.0, 1e-6, 1.0 / 48000);
		if (q.v > 10.5 || q.v < -10.5 || std::isnan(q.v)) bounded = false;
	}
	check("a 20-ohm path at 48 kHz stays inside the source's rails", bounded);
}

static double run(Ad555& ad, double gate, double seconds, double att, double rel, bool loop,
                  double sr, double* peak = 0, int* eocs = 0) {
	double dt = 1.0 / sr, out = 0;
	bool eoc;
	for (int n = 0; n < (int)(seconds * sr); n++) {
		ad.process(gate, loop, att, rel, dt, out, eoc);
		if (peak && out > *peak) *peak = out;
		if (eocs && eoc) (*eocs)++;
	}
	return out;
}

// Gate steps up from 0 to `hi` over `rampSec`, then holds.
static double stepGate(Ad555& ad, double hi, double rampSec, double att, double rel, double sr) {
	double dt = 1.0 / sr, out = 0;
	bool eoc;
	int n = std::max(1, (int)(rampSec * sr));
	for (int k = 1; k <= n; k++) ad.process(hi * k / n, false, att, rel, dt, out, eoc);
	return out;
}

static void testAd() {
	printf("AD: TLC555 + 10 uF\n");
	checkv("threshold with the 51k on CV (5k divider)", ad::threshold(), 8.245, 0.005);
	double sr = 48000, dt = 1.0 / sr;

	// attack time: gate edge to the capacitor reaching the threshold
	double times[] = { 0.001, 0.01, 0.1, 1.0, 5.0 };
	bool timing = true;
	for (double T : times) {
		Ad555 ad;
		double out = 0;
		bool eoc;
		double tPeak = -1;
		for (int n = 0; n < (int)(T * 2.0 * sr) + 200; n++) {
			ad.process(n < 2 ? 0.0 : 10.0, false, T, T, dt, out, eoc);
			if (tPeak < 0 && !ad.latch && n > 3) tPeak = n * dt;     // latch let go
		}
		double err = (tPeak - 2 * dt) / T - 1.0;
		if (std::fabs(err) > 0.10) { timing = false; printf("    attack %g s measured %g s\n", T, tPeak); }
	}
	check("rise to the threshold takes ATTACK seconds within 10 % (1 ms to 5 s)", timing);

	{
		Ad555 ad;
		double peak = 0;
		run(ad, 0.0, 0.01, 0.01, 0.05, false, sr);
		run(ad, 10.0, 0.05, 0.01, 0.05, false, sr, &peak);
		checkv("the peak is 10 V on the output (threshold-normalised)", peak, 10.0, 0.1);
	}
	{   // TRIG is a capacitor-coupled step: a slow ramp does not fire, a fast one does
		Ad555 a, b;
		run(a, 0.0, 0.01, 0.01, 0.05, false, sr);
		stepGate(a, 10.0, 0.05, 0.01, 0.05, sr);                 // 50 ms ramp
		check("a 50 ms ramp of the gate does not trigger", !a.active);
		run(b, 0.0, 0.01, 0.01, 0.05, false, sr);
		stepGate(b, 10.0, 0.0001, 0.01, 0.05, sr);               // 0.1 ms
		check("a fast 10 V step triggers", b.active);
		Ad555 c;
		run(c, 0.0, 0.01, 0.01, 0.05, false, sr);
		stepGate(c, 0.5, 0.0, 0.01, 0.05, sr);
		check("a 0.5 V step is below the BC547B's base and does not trigger", !c.active);
	}
	{   // a retrigger in the fall resumes from where the capacitor is
		Ad555 ad;
		double out = 0;
		bool eoc;
		for (int n = 0; n < 4; n++) ad.process(0.0, false, 0.05, 0.5, dt, out, eoc);
		for (int n = 0; n < (int)(0.1 * sr); n++) ad.process(n < 3 ? 0.0 : 10.0, false, 0.05, 0.5, dt, out, eoc);
		double mid = 0;                                           // let it fall for a while
		for (int n = 0; n < (int)(0.1 * sr); n++) ad.process(0.0, false, 0.05, 0.5, dt, out, eoc);
		mid = out;
		check("fell part of the way", mid > 1.0 && mid < 9.0);
		double first = -1;
		for (int n = 0; n < 4; n++) { ad.process(n < 1 ? 0.0 : 10.0, false, 0.05, 0.5, dt, out, eoc); }
		first = out;
		checkv("a retrigger in the fall starts from the current level, not zero", first, mid, 0.5);
		bool rose = false;
		for (int n = 0; n < (int)(0.1 * sr); n++) { ad.process(10.0, false, 0.05, 0.5, dt, out, eoc); if (out > mid + 0.5) rose = true; }
		check("and rises again", rose);
	}
	{   // a retrigger during the rise changes nothing
		Ad555 a, b;
		double out = 0;
		bool eoc;
		double ta = -1, tb = -1;
		for (int n = 0; n < (int)(0.5 * sr); n++) {
			a.process(n < 3 ? 0.0 : 10.0, false, 0.1, 0.1, dt, out, eoc);
			bool pulse = n >= 3 && n < 6000 ? ((n / 1200) % 2 == 0 ? false : true) : false;
			double g = n < 3 ? 0.0 : (n < 6000 ? (pulse ? 10.0 : 0.0) : 10.0);
			if (n == 3) g = 10.0;
			b.process(g, false, 0.1, 0.1, dt, out, eoc);
			if (ta < 0 && !a.latch && n > 5) ta = n * dt;
			if (tb < 0 && !b.latch && n > 5) tb = n * dt;
		}
		checkv("extra edges during the rise do not move its end", tb, ta, 0.01 * ta + 0.002);
	}
	{   // the fall ends below the diode's knee, and the tail lingers after it
		Ad555 ad;
		double out = 0;
		bool eoc;
		int eocs = 0;
		double tEnd = -1;
		for (int n = 0; n < (int)(3.0 * sr); n++) {
			ad.process(n < 3 ? 0.0 : (n < 3 + 2 ? 10.0 : 10.0), false, 0.05, 0.2, dt, out, eoc);
			if (eoc) { eocs++; if (tEnd < 0) tEnd = n * dt; }
		}
		check("one EOC at the end of the fall", eocs == 1);
		check("the fall ends within ~1.5x RELEASE of the peak", tEnd > 0.05 && tEnd < 0.05 + 0.2 * 1.6);
		check("the capacitor is still above zero after the fall (the diode tail)", ad.rc.v > 0.01);
	}
	{   // LOOP
		Ad555 ad;
		int eocs = 0;
		run(ad, 0.0, 0.001, 0.02, 0.05, true, sr);
		stepGate(ad, 10.0, 0.0001, 0.02, 0.05, sr);
		double peak = 0;
		run(ad, 0.0, 2.0, 0.02, 0.05, true, sr, &peak, &eocs);
		check("LOOP keeps cycling with the gate low", eocs >= 15 && peak > 9.9);
	}
}

static void testAr() {
	printf("AR: TL072 + 1 uF\n");
	double sr = 48000, dt = 1.0 / sr;
	// the gate threshold, solved independently: the same current through both diodes
	double iTh = ar::kVref / 100e3, lo = 0, hi = 20;
	for (int k = 0; k < 100; k++) {
		double vg = 0.5 * (lo + hi);
		// find the current that this gate drives: vg = 300k i + 2 Vd(i)
		double a = 0, b = 1e-3;
		for (int j = 0; j < 100; j++) {
			double m = 0.5 * (a + b);
			double f = 300e3 * m + 2 * diodeDrop(m) - vg;
			if (f > 0) b = m; else a = m;
		}
		if (0.5 * (a + b) > iTh) hi = vg; else lo = vg;
	}
	checkv("gate threshold matches an independent solve", ar::gateThreshold(), 0.5 * (lo + hi), 1e-3);
	checkv("and is about 7.1 V", ar::gateThreshold(), 7.1, 0.1);
	checkv("reference is 2.105 V", ar::kVref, 2.105, 0.001);

	{
		ArTl072 a, b;
		double out = 0;
		bool eoc;
		double peakLow = 0, peakHigh = 0;
		for (int n = 0; n < (int)(1.0 * sr); n++) { a.process(6.5, false, 0.05, 0.1, dt, out, eoc); peakLow = std::fmax(peakLow, out); }
		for (int n = 0; n < (int)(1.0 * sr); n++) { b.process(7.5, false, 0.05, 0.1, dt, out, eoc); peakHigh = std::fmax(peakHigh, out); }
		check("a 6.5 V gate does not fire it", peakLow == 0.0);
		check("a 7.5 V gate does", peakHigh > 9.5);
	}
	double times[] = { 0.002, 0.02, 0.2, 2.0 };
	bool attOk = true, relOk = true, dead = true, sustainOk = true;
	for (double T : times) {
		ArTl072 ar;
		double out = 0;
		bool eoc;
		double t0 = -1, t90 = -1;
		for (int n = 0; n < (int)(T * 3 * sr) + 100; n++) {
			ar.process(10.0, false, T, T, dt, out, eoc);
			if (t0 < 0 && out > 0.0) t0 = n * dt;
			if (t90 < 0 && out >= 9.0) t90 = n * dt;
		}
		if (std::fabs(t90 / T - 1.0) > 0.10) { attOk = false; printf("    attack %g s measured %g s\n", T, t90); }
		if (!(t0 > 0.05 * T)) dead = false;       // the capacitor starts below zero
		if (out < 9.7) sustainOk = false;
		// release
		double tFall = -1;
		for (int n = 0; n < (int)(T * 4 * sr) + 100; n++) {
			ar.process(0.0, false, T, T, dt, out, eoc);
			if (tFall < 0 && out <= 0.2) tFall = n * dt;
		}
		if (std::fabs(tFall / T - 1.0) > 0.15) { relOk = false; printf("    release %g s measured %g s\n", T, tFall); }
	}
	check("gate to 90 % takes ATTACK seconds within 10 % (2 ms to 2 s)", attOk);
	check("output stays at zero for a while after the gate (the capacitor rests negative)", dead);
	check("sustain is at least 9.7 V", sustainOk);
	check("release to 2 % takes RELEASE seconds within 15 %", relOk);

	{   // retriggering soon after a release is quicker than from rest
		ArTl072 a, b;
		double out = 0;
		bool eoc;
		double T = 0.2;
		for (int n = 0; n < (int)(T * 2 * sr); n++) { a.process(10.0, false, T, T, dt, out, eoc); b.process(10.0, false, T, T, dt, out, eoc); }
		// a: release until just past zero then gate; b: release for a long time, then gate
		int n = 0;
		while (true) { a.process(0.0, false, T, T, dt, out, eoc); n++; if (out <= 0.0 || n > 1e6) break; }
		for (int k = 0; k < (int)(T * 20 * sr); k++) b.process(0.0, false, T, T, dt, out, eoc);
		double ta = -1, tb = -1;
		for (int k = 0; k < (int)(T * 3 * sr); k++) {
			a.process(10.0, false, T, T, dt, out, eoc);
			if (ta < 0 && out > 0.0) ta = k * dt;
		}
		for (int k = 0; k < (int)(T * 3 * sr); k++) {
			b.process(10.0, false, T, T, dt, out, eoc);
			if (tb < 0 && out > 0.0) tb = k * dt;
		}
		check("a retrigger just after the release starts sooner than one from rest", ta >= 0 && tb > 0 && ta < tb * 0.5);
	}
	{   // LOOP
		ArTl072 ar;
		double out = 0;
		bool eoc;
		int eocs = 0;
		double peak = 0;
		for (int n = 0; n < (int)(6.0 * sr); n++) {
			ar.process(0.0, true, 0.1, 0.1, dt, out, eoc);
			if (eoc) eocs++;
			peak = std::fmax(peak, out);
		}
		printf("    loop: %d EOCs in 6 s, peak %.3f\n", eocs, peak);
		check("LOOP cycles with no gate, to the 90 % mark", eocs >= 8 && peak > 9.0 && peak < 9.6);
	}
}

static void testRobust() {
	printf("robustness\n");
	bool ok = true;
	Ad555 ad;
	ArTl072 ar;
	srand(7);
	for (int sr : { 44100, 48000, 192000, 768000 }) {
		double dt = 1.0 / sr;
		for (int n = 0; n < sr; n++) {
			double g = (rand() % 100 < 2) ? (rand() % 2 ? 10.0 : -5.0) : (n % 5000 < 2500 ? 10.0 : 0.0);
			double att = 0.0003 * std::pow(1e5, (rand() % 1000) / 1000.0);
			double rel = 0.0003 * std::pow(1e5, (rand() % 1000) / 1000.0);
			double o1, o2;
			bool e1, e2;
			bool loop = (n / 20000) % 2;
			ad.process(g, loop, att, rel, dt, o1, e1);
			ar.process(g, loop, att, rel, dt, o2, e2);
			if (std::isnan(o1) || std::isnan(o2) || o1 < 0 || o1 > 10 || o2 < 0 || o2 > 10) ok = false;
		}
	}
	check("hammered with gates and 0.3 ms to 30 s times from 44.1 to 768 kHz: finite, 0..10 V", ok);
}

int main() {
	testDiode();
	testRc();
	testAd();
	testAr();
	testRobust();
	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}

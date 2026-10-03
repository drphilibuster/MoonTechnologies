// Kickback's XOR snare engine (Voices.hpp, XorEngine): its three oscillators are the CD40106B
// astable of XORbell (src/Cd40106.hpp, tested on its own in tests/SixFigures/test_cd40106.cpp). Here:
// that the engine sets the pot so the astable runs at the frequency TUNE asked for, that it is the
// circuit's lopsided square and not an ideal one, that BEND moves the pitch the way it always did,
// and negative controls (each check fails against a deliberately wrong reference).

#include "../../src/Drum.hpp"
#include "../../src/Kickback/Voices.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

static int checks = 0, failures = 0;
static void check(const char* what, bool ok) {
	checks++;
	if (!ok) { failures++; printf("  FAIL  %s\n", what); }
}
static void checkrel(const char* what, double got, double want, double rel) {
	checks++;
	if (!(std::fabs(got - want) <= std::fabs(want) * rel)) {
		failures++;
		printf("  FAIL  %s: got %.6g, want %.6g (rel %.2g)\n", what, got, want, rel);
	}
}
static void control(const char* what, bool detected) {
	checks++;
	if (!detected) { failures++; printf("  FAIL  negative control did not fire: %s\n", what); }
}

/** Mean period (rising zero crossings, linearly interpolated) and the fraction of time high. */
struct Meas { double period; double dutyHigh; };
static Meas measure(double f, double sag, double fs, int seconds) {
	kickback::XorEngine::Voice v;
	const double dt = 1.0 / fs;
	long n = (long)(fs * seconds);
	std::vector<float> y(n);
	for (long i = 0; i < n; i++) y[i] = v.process(f, sag, dt);
	// skip the first 0.2 s
	long i0 = (long)(fs * 0.2);
	double firstUp = -1, lastUp = 0; int ups = 0; double sum = 0;
	for (long i = i0 + 1; i < n; i++) {
		sum += y[i];
		if (y[i - 1] <= 0.f && y[i] > 0.f) {
			double t = (double)(i - 1) + (0.0 - y[i - 1]) / (y[i] - y[i - 1]);
			if (firstUp < 0) firstUp = t; else { lastUp = t; ups++; }
		}
	}
	Meas m;
	m.period = ups > 0 ? (lastUp - firstUp) / ups / fs : 0.0;
	m.dutyHigh = 0.5 * (sum / (double)(n - i0 - 1) + 1.0);   // the mean is 2 duty - 1 (the band-limited edges keep the area)
	return m;
}

int main() {
	printf("XOR snare engine (CD40106 astables)\n");

	// 1. TUNE's frequency comes out, at several rates
	for (double fs : { 44100.0, 48000.0, 96000.0 }) {
		for (double f : { 110.0, 220.0, 440.0, 900.0, 1800.0 }) {
			Meas m = measure(f, 1.0, fs, 3);
			char w[96];
			snprintf(w, sizeof w, "oscillator at %.0f Hz, fs %.0f", f, fs);
			checkrel(w, 1.0 / m.period, f, 2e-3);
		}
	}

	// 2. the circuit's duty cycle, not an ideal square's 50 %: about 0.49 at 12 V typical
	{
		Meas m = measure(300.0, 1.0, 48000.0, 3);
		cd40106::Timing t = cd40106::timing(cd40106::resistanceForPeriod(1.0 / 300.0, 0.1e-6, 12.0), 0.1e-6, 12.0);
		checkrel("duty (high fraction) = tHigh / T of the chip at 12 V (Ron and delay included)", m.dutyHigh, t.tHigh / t.period, 5e-3);
		check("duty is below 50 %", m.dutyHigh < 0.495 && m.dutyHigh > 0.47);
		control("an ideal 50 % square is rejected by the same duty check",
		        std::fabs(0.5 - m.dutyHigh) > 5e-3 * (t.tHigh / t.period));  // 0.5 vs ~0.494: 1.2 %
	}

	// 3. BEND: dividing the pot's resistance by sag < 1 lowers the pitch by about sag
	{
		double p1 = measure(300.0, 1.0, 48000.0, 3).period;
		double p2 = measure(300.0, 0.5, 48000.0, 3).period;
		checkrel("sag 0.5 halves the pitch", p1 / p2, 0.5, 3e-3);
		control("a model that ignores sag is rejected", std::fabs(p1 / p1 - 0.5) > 0.1);
	}

	// 4. the pot for 123 Hz is the 100 k of the schematic (at 12 V, typical, the on resistance left in)
	{
		double r = cd40106::resistanceForPeriod(1.0 / 123.0, 0.1e-6, 12.0);
		check("the pot for 123 Hz is about 100 k (the schematic's pot)", r > 97e3 && r < 103e3);
	}

	// 5. the whole engine: finite, bounded, decays, and the tunes differ
	{
		kickback::XorEngine e;
		e.setRate(48000.f);
		e.reset();
		e.strike(1.f);
		float peak = 0.f, tail = 0.f; bool finite = true;
		for (int i = 0; i < 48000; i++) {
			float y = e.process(330.f, 0.25f, 0.5f, 0.5f, i == 0 ? 1.f : 0.f);
			if (!std::isfinite(y)) finite = false;
			if (i < 4800 && std::fabs(y) > peak) peak = std::fabs(y);
			if (i > 40000 && std::fabs(y) > tail) tail = std::fabs(y);
		}
		check("engine output finite", finite);
		check("engine output has a strike and a decay", peak > 0.05f && peak < 3.f && tail < 0.05f * peak);
		// extremes of TUNE at several rates stay finite and bounded (high f is limited to 0.45 fs)
		bool ok = true;
		for (double fs : { 22050.0, 44100.0, 192000.0 }) {
			kickback::XorEngine e2; e2.setRate((float)fs); e2.reset(); e2.strike(1.f);
			for (int i = 0; i < 20000; i++) {
				float y = e2.process(i < 10000 ? 20.f : 9000.f, 1.1f, 1.f, 1.f, 0.f);
				if (!std::isfinite(y) || std::fabs(y) > 4.f) ok = false;
			}
		}
		check("finite and bounded across TUNE extremes and rates", ok);
	}

	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}

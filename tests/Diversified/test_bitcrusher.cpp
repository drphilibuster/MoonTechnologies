// Diversified 104: the BitCrusher board's converter chain (src/Adc0809.hpp and
// BitCrush in MiawFx.hpp), checked against what the schematic and the datasheets
// say rather than against how the old model behaved.

#include "../../src/Diversified/MiawFx.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

using divfx::MiawCtx;
using divfx::BitCrush;

static int checks = 0, failures = 0;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; \
	printf("  FAIL  "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static const float SR = 48000.f;

static MiawCtx ctx(float level, float bits, float rate) {
	MiawCtx c;
	c.sr = SR;
	c.p[0] = level; c.p[1] = bits; c.p[2] = rate;
	return c;
}

static void testClockFormula() {
	printf("LTC1799: f = 10 MHz * 10k / (N * R_SET)\n");
	using adc0809::Ltc1799;
	CHECK(std::fabs(Ltc1799::hz(10e3, 1) - 10e6) < 1.0, "10k, /1 gives %.0f Hz", Ltc1799::hz(10e3, 1));
	CHECK(std::fabs(Ltc1799::hz(100e3, 10) - 100e3) < 1.0, "100k, /10 gives %.0f Hz", Ltc1799::hz(100e3, 10));
	CHECK(std::fabs(Ltc1799::hz(50e3, 100) - 20e3) < 1.0, "50k, /100 gives %.0f Hz", Ltc1799::hz(50e3, 100));
}

static void testLadder() {
	printf("Ladder: binary weights, MSB nearest the op-amp\n");
	adc0809::R2rDac dac;
	float t[256];
	dac.buildTable(1000.f, 0xFF, false, t);
	// All eight lines high is 255/256 of one volt-leg sum, inverted: -VOH*255/256.
	CHECK(std::fabs(t[255] + 5.0 * 255.0 / 256.0) < 1e-4, "code 255 -> %.5f V", t[255]);
	CHECK(t[0] == 0.f, "code 0 -> %.5f V", t[0]);
	// Each bit is half the one above it, so code 2^k is -5/256 * 2^k.
	bool binary = true;
	for (int k = 0; k < 8; k++)
		if (std::fabs(t[1 << k] + 5.0 * (double)(1 << k) / 256.0) > 1e-5) binary = false;
	CHECK(binary, "bit weights are not binary");
	CHECK(std::fabs(t[128] - t[64] * 2.f) < 1e-5, "MSB is %.5f V, bit 6 %.5f V", t[128], t[64]);
	// And it is monotonic: the ladder, summed, never steps backwards.
	bool mono = true;
	for (int c = 1; c < 256; c++) if (t[c] > t[c - 1]) mono = false;
	CHECK(mono, "ladder is not monotonic");

	// Swapped, ADC bit 7 lands on the termination end: the weights reverse.
	float s[256];
	dac.buildTable(1000.f, 0xFF, true, s);
	CHECK(std::fabs(s[128] - t[1]) < 1e-5 && std::fabs(s[1] - t[128]) < 1e-5,
	      "swap: code 128 -> %.5f (want %.5f)", s[128], t[1]);
}

static void testOpenLegsMoveTheOthers() {
	printf("Ladder: an open leg is not a grounded one\n");
	adc0809::R2rDac dac;
	float full[256], open4[256];
	dac.buildTable(1000.f, 0xFF, false, full);
	dac.buildTable(1000.f, 0xF0, false, open4);       // low four lines unpatched
	// The upper four lines still drive the same nodes, but the legs below have
	// stopped loading them, so their weights are no longer the ideal binary ones.
	double devMsb = std::fabs(open4[128] / full[128] - 1.0);
	double devB4 = std::fabs(open4[16] / full[16] - 1.0);
	CHECK(devB4 > 1e-4, "bit 4 weight unchanged by open legs below it (%.2g)", devB4);
	CHECK(devMsb < devB4, "the error should fall away up the ladder: msb %.2g, bit4 %.2g", devMsb, devB4);
	CHECK(devB4 < 0.35, "bit 4 weight moved implausibly far (%.2g)", devB4);
}

static void testConverter() {
	printf("ADC0809: transfer and timing\n");
	using adc0809::Adc0809;
	CHECK(Adc0809::quantize(0.0) == 0, "0 V -> %d", Adc0809::quantize(0.0));
	CHECK(Adc0809::quantize(2.5) == 128, "2.5 V -> %d", Adc0809::quantize(2.5));
	CHECK(Adc0809::quantize(5.0) == 255, "5 V -> %d", Adc0809::quantize(5.0));
	CHECK(Adc0809::quantize(-1.0) == 0 && Adc0809::quantize(9.0) == 255, "input clamp");
	// Half an LSB is where the code changes.
	double lsb = 5.0 / 256.0;
	CHECK(Adc0809::quantize(10 * lsb - 0.01 * lsb) == 10 && Adc0809::quantize(10 * lsb + 0.51 * lsb) == 11,
	      "transitions are not at half an LSB");

	// Timing: one clock period at a time for a second's worth of clocks at 72 kHz;
	// EOC is where the phase wraps. 72 clocks a cycle gives 1000 a second.
	Adc0809 adc;
	float dac[256];
	for (int i = 0; i < 256; i++) dac[i] = (float) i;
	const double fclk = 72000.0, fs = 48000.0;
	{
		Adc0809 t; int cycles = 0; double last = 0.0;
		for (int n = 0; n < 72000; n++) {
			t.run(1.0, 1.0, 1.0, dac);
			if (t.phase < last) cycles++;
			last = t.phase;
		}
		CHECK(cycles == 1000, "%d conversions in 72000 clocks, want 1000 (64 + 8 clocks a cycle)", cycles);
	}
	std::vector<float> out;
	for (int n = 0; n < 48000; n++) {
		double v0 = 5.0 * n / 48000.0, v1 = 5.0 * (n + 1) / 48000.0;
		out.push_back(adc.run(fclk / fs, v0, v1, dac));
	}

	// Latency: the value read at time t is what the input was 64 clocks earlier.
	// At 72 kHz that is 0.889 ms; on a 5 V/s ramp, code error is ~0.889e-3*5/(5/256).
	double meanLag = 0.0; int cnt = 0;
	for (size_t i = 6000; i < 42000; i += 7) {
		double t = (double) i / fs;
		double want = (5.0 * t) / (5.0 / 256.0);       // ideal code now
		meanLag += want - out[i]; cnt++;
	}
	meanLag /= cnt;
	// Sample-at-start-of-conversion plus 64 clocks to the result, plus the staircase
	// and the averaging: lag between ~0.9 ms and ~1.9 ms of ramp.
	double msLag = meanLag / (256.0 / 5.0) * 1e3 / 1.0 / (5.0 / 5.0);
	(void) msLag;
	double lagMs = (meanLag / 256.0 * 5.0) / 5.0 * 1e3;   // volts/(5 V/s) in ms
	CHECK(lagMs > 0.7 && lagMs < 2.2, "conversion latency %.2f ms", lagMs);
}

static std::vector<float> run(BitCrush& b, MiawCtx& c, const std::vector<float>& in) {
	std::vector<float> out(in.size());
	for (size_t i = 0; i < in.size(); i++) b.process(c, in[i], out[i]);
	return out;
}

static std::vector<float> sine(float hz, float amp, float seconds) {
	std::vector<float> x((size_t) (seconds * SR));
	for (size_t i = 0; i < x.size(); i++) x[i] = amp * std::sin(2.f * (float) M_PI * hz * i / SR);
	return x;
}

static double rms(const std::vector<float>& x, size_t a, size_t b) {
	double s = 0; for (size_t i = a; i < b; i++) s += (double) x[i] * x[i];
	return std::sqrt(s / (double)(b - a));
}

static double corr(const std::vector<float>& a, const std::vector<float>& b, size_t i0, size_t i1) {
	double s = 0, na = 0, nb = 0;
	for (size_t i = i0; i < i1; i++) { s += (double) a[i] * b[i]; na += (double) a[i] * a[i]; nb += (double) b[i] * b[i]; }
	return s / std::sqrt(na * nb + 1e-30);
}

static void testBoardBehaviour() {
	printf("BitCrush: polarity, input range, level\n");
	// Fastest clock, all bits, filter off: the board near-transparent apart from the
	// inversion that the inverting ladder stage gives it.
	MiawCtx c = ctx(1.f, 1.f, 1.f);
	c.crushDiv = 0; c.crushLpf = false; c.crushGain = 1;
	BitCrush b; b.setSampleRate(SR); b.clear(); b.setParams(c);
	auto in = sine(220.f, 5.f, 1.f);
	auto out = run(b, c, in);
	double r = corr(in, out, (size_t) (0.3 * SR), (size_t) (0.9 * SR));
	CHECK(r < -0.99, "output should be the input inverted (the stage is inverting): corr %.3f", r);
	double g = rms(out, (size_t) (0.3 * SR), (size_t) (0.9 * SR)) / rms(in, (size_t) (0.3 * SR), (size_t) (0.9 * SR));
	CHECK(g > 0.95 && g < 1.05, "full-scale in at 2x gain should come out at ~unity, got %.3f", g);

	// Level: RP1 is an attenuator.
	c.p[0] = 0.5f; b.clear(); b.setParams(c);
	auto half = run(b, c, in);
	double gh = rms(half, (size_t) (0.3 * SR), (size_t) (0.9 * SR)) / rms(in, (size_t) (0.3 * SR), (size_t) (0.9 * SR));
	CHECK(gh > 0.45 && gh < 0.55, "LEVEL at half should halve the signal, got %.3f", gh);

	// Board-faithful input: the ADC reads 0..5 V, so the negative half is code 0.
	c = ctx(1.f, 1.f, 1.f);
	c.crushDiv = 0; c.crushLpf = false; c.crushUnipolar = true;
	b.clear(); b.setParams(c);
	auto uni = run(b, c, in);
	// Compare the two halves of the output about its own mean.
	double mean = 0; size_t i0 = (size_t) (0.3 * SR), i1 = (size_t) (0.9 * SR);
	for (size_t i = i0; i < i1; i++) mean += uni[i];
	mean /= (double)(i1 - i0);
	double lo = 1e9, hi = -1e9;
	for (size_t i = i0; i < i1; i++) { lo = std::fmin(lo, uni[i]); hi = std::fmax(hi, uni[i]); }
	// Half-wave: one side swings, the other sits at a constant. AC coupling puts the
	// constant at -mean; the swing side reaches well beyond it.
	CHECK(std::fabs(hi) > 3.0 * std::fabs(lo) || std::fabs(lo) > 3.0 * std::fabs(hi),
	      "unipolar input should be half-wave: out range %.2f .. %.2f (mean %.2f)", lo, hi, mean);
}

static void testBitsAndSwap() {
	printf("BitCrush: BITS opens legs, SWAP reverses the word\n");
	MiawCtx c = ctx(1.f, 0.f, 1.f);              // 1 bit
	c.crushDiv = 0; c.crushLpf = false;
	BitCrush b; b.setSampleRate(SR); b.clear(); b.setParams(c);
	// The ladder stage's own output: one connected leg means two levels, however
	// many codes there are. (The audio output has transition samples and the slow
	// drift of C1 in it, which would make a count of its values meaningless.)
	std::vector<float> lv;
	for (int i = 0; i < 256; i++) {
		bool seen = false;
		for (float l : lv) if (std::fabs(l - b.table[i]) < 1e-6f) { seen = true; break; }
		if (!seen) lv.push_back(b.table[i]);
	}
	CHECK(lv.size() == 2, "one bit should give two levels, got %zu", lv.size());
	// Four bits: sixteen.
	c.p[1] = 3.f / 8.f + 0.01f; b.setParams(c);
	lv.clear();
	for (int i = 0; i < 256; i++) {
		bool seen = false;
		for (float l : lv) if (std::fabs(l - b.table[i]) < 1e-6f) { seen = true; break; }
		if (!seen) lv.push_back(b.table[i]);
	}
	CHECK(lv.size() == 16, "four bits should give sixteen levels, got %zu (bitsOn %d)", lv.size(), b.bitsOn);

	// Swapped, all eight bits: the loudest bit now drives the weakest leg, so a
	// full-scale signal comes out as little more than the swapped bit's wiggle.
	c = ctx(1.f, 1.f, 1.f); c.crushDiv = 0; c.crushLpf = false; c.crushSwap = true;
	b.clear(); b.setParams(c);
	auto sw = run(b, c, sine(220.f, 5.f, 1.f));
	double rmsSw = rms(sw, (size_t) (0.3 * SR), (size_t) (0.9 * SR));
	CHECK(rmsSw > 0.05 && rmsSw < 4.0, "swapped output rms %.3f", rmsSw);
}

static void testReconstructionFilter() {
	printf("BitCrush: SW1 is a one-pole 680 ohm / 68 nF, 3.44 kHz\n");
	auto level = [](bool lpf, float hz) {
		MiawCtx c = ctx(1.f, 1.f, 1.f);
		c.crushDiv = 0; c.crushLpf = lpf;
		BitCrush b; b.setSampleRate(SR); b.clear(); b.setParams(c);
		auto in = sine(hz, 2.f, 0.8f);
		auto out = run(b, c, in);
		return rms(out, (size_t) (0.4 * SR), (size_t) (0.8 * SR));
	};
	double ref = level(true, 300.f), at = level(true, 3441.f);
	double dB = 20.0 * std::log10(at / ref);
	CHECK(dB < -2.4 && dB > -3.6, "at the corner the filter is %.2f dB, want about -3", dB);
	double off = level(false, 3441.f) / level(false, 300.f);
	CHECK(std::fabs(20.0 * std::log10(off)) < 0.5, "with SW1 out the response is flat, %.2f dB", 20.0 * std::log10(off));
}

static void testRateIsTheClock() {
	printf("BitCrush: RATE and DIV set the converter's clock\n");
	auto convRate = [](float rate, int div) {
		MiawCtx c = ctx(1.f, 1.f, rate); c.crushDiv = div; c.crushLpf = false;
		BitCrush b; b.setSampleRate(SR); b.clear(); b.setParams(c);
		return b.clocks * SR / 72.0;
	};
	double lo = convRate(0.f, 1), hi = convRate(1.f, 1);
	CHECK(lo > 120.0 && lo < 160.0, "RATE at minimum, /10: %.0f conversions/s (want ~139)", lo);
	CHECK(hi > 44000.0 && hi < 48000.0, "RATE at maximum, /10: %.0f conversions/s (want ~46k)", hi);
	CHECK(std::fabs(convRate(0.5f, 0) / convRate(0.5f, 1) - 10.0) < 0.01, "DIV /1 should be ten times /10");
	CHECK(std::fabs(convRate(0.5f, 1) / convRate(0.5f, 2) - 10.0) < 0.01, "DIV /10 should be ten times /100");
}

static void testNoThumpOnStart() {
	printf("BitCrush: C1 starts charged\n");
	MiawCtx c = ctx(1.f, 1.f, 0.7f);
	BitCrush b; b.setSampleRate(SR); b.clear(); b.setParams(c);
	float peak = 0.f;
	for (int i = 0; i < (int) (0.3 * SR); i++) {
		float o; b.process(c, 0.f, o);
		peak = std::fmax(peak, std::fabs(o));
	}
	CHECK(peak < 0.5f, "silence in should stay quiet from the first sample, peak %.2f V", peak);
}

static void benchmark() {
	printf("CPU (informational)\n");
	const int divs[] = { 0, 1 };
	for (int d : divs) {
		MiawCtx c = ctx(1.f, 1.f, 1.f); c.crushDiv = d;
		BitCrush b; b.setSampleRate(SR); b.clear(); b.setParams(c);
		const int n = (int) (5.f * SR);
		float sink = 0.f;
		auto t0 = std::chrono::steady_clock::now();
		for (int i = 0; i < n; i++) {
			float o; b.process(c, 3.f * std::sin(2.f * (float) M_PI * 220.f * i / SR), o);
			sink += o;
		}
		double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
		printf("    fastest clock, DIV /%s: %.2f%% of one core%s\n", d == 0 ? "1" : "10", 100.0 * s / 5.0,
		       sink == 12345.f ? "!" : "");
		CHECK(s / 5.0 < 0.2, "BitCrush uses %.0f%% of a core", 100.0 * s / 5.0);
	}
}

int main() {
	testClockFormula();
	testLadder();
	testOpenLegsMoveTheOthers();
	testConverter();
	testBoardBehaviour();
	testBitsAndSwap();
	testReconstructionFilter();
	testRateIsTheClock();
	testNoThumpOnStart();
	benchmark();
	printf("\n%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}

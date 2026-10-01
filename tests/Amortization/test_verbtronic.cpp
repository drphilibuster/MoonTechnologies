// Amortization: the Verbtronic's schematic, solved.
//
// The module is the public-domain schematic, block by block, so every check
// here compares a block against an *independent* statement of what the schematic
// says -- the nodal equations solved numerically, the datasheet's own figures,
// the Electric Druid measurements of the PT2399 -- rather than against the
// code's own expression of it. The PT2399's modulator internals are not in any
// datasheet; its step sizes are set so the model meets the datasheet's THD,
// output-swing and gain, and those are checked here too.
#include "../../src/Amortization/Verbtronic.hpp"

#include <cmath>
#include <complex>
#include <cstdio>
#include <vector>

using namespace amortization;
typedef std::complex<double> cd;

static int checks = 0;
static int failures = 0;

static void fail(const char* what, const char* detail) {
	failures++;
	printf("  FAIL  %s: %s\n", what, detail);
}

#define CHECK(cond, what, ...) do { checks++; if (!(cond)) { char d_[256]; snprintf(d_, sizeof d_, __VA_ARGS__); fail(what, d_); } } while (0)

static double rms(const std::vector<double>& v, size_t a, size_t b) {
	double s = 0.0;
	for (size_t i = a; i < b && i < v.size(); i++) s += v[i] * v[i];
	return std::sqrt(s / (double)(b > a ? b - a : 1));
}

/** Amplitude of `f` in x[s .. s+n). */
static double bin(const std::vector<double>& x, size_t s, size_t n, double f, double fs) {
	double a = 0, b = 0;
	for (size_t i = 0; i < n; i++) {
		double ph = 2.0 * M_PI * f * (double)(s + i) / fs;
		a += x[s + i] * std::sin(ph);
		b += x[s + i] * std::cos(ph);
	}
	a *= 2.0 / (double)n; b *= 2.0 / (double)n;
	return std::sqrt(a * a + b * b);
}

/** A tone through one bare PT2399, with the modulator fed directly. */
static std::vector<double> chipTone(double R, double amp, double f, double fs, double secs) {
	Pt2399 c;
	c.setSampleRate(fs);
	c.setPin6(R);
	size_t N = (size_t)(fs * secs);
	std::vector<double> out(N);
	double prev = 0;
	for (size_t n = 0; n < N; n++) {
		double x = amp * std::sin(2.0 * M_PI * f * (double)n / fs);
		c.begin();
		out[n] = c.demod();
		c.modulate((float)prev, (float)x);
		prev = x;
	}
	return out;
}

/** Harmonic distortion, harmonics 2..10, of a 1 kHz tone in the last second. */
static double thd(const std::vector<double>& y, double fs) {
	size_t n = (size_t)fs, s = y.size() - n;
	double f1 = bin(y, s, n, 1000.0, fs), h = 0;
	for (int k = 2; k <= 10; k++) { double v = bin(y, s, n, 1000.0 * k, fs); h += v * v; }
	return std::sqrt(h) / f1;
}

/** THD+N: everything in the last second that is not the fundamental. */
static double thdn(const std::vector<double>& y, double fs) {
	size_t n = (size_t)fs, s = y.size() - n;
	double f1 = bin(y, s, n, 1000.0, fs) / std::sqrt(2.0);
	double tot = rms(y, s, y.size());
	return std::sqrt(std::fmax(tot * tot - f1 * f1, 0.0)) / f1;
}


// --- independent statements of the analog blocks ----------------------------

/** The tone stage's transfer function, from the schematic's nodal equations,
    solved numerically (no Cf: its pole is above 100 kHz). x is the wiper's
    position from the CW lug. */
static cd toneAnalog(double f, double x) {
	cd s(0.0, 2.0 * M_PI * f);
	double R1 = x * 100e3, R3 = (1.0 - x) * 100e3;
	cd Zb = 22e3 + 1.0 / (s * 3.3e-9);
	// unknowns O, v1, v3 ; A = 1
	cd M[3][4] = {
		{ 1.0 / Zb, -(1.0 / 68e3 + 1.0 / Zb + 1.0 / R1), 0.0, -1.0 / 68e3 },
		{ 1.0 / 68e3, 0.0, -(1.0 / Zb + 1.0 / 68e3 + 1.0 / R3), -1.0 / Zb },
		{ 0.0, 1.0 / R1, 1.0 / R3, 0.0 }
	};
	for (int c = 0; c < 3; c++) {
		int p = c;
		for (int r = c + 1; r < 3; r++) if (std::abs(M[r][c]) > std::abs(M[p][c])) p = r;
		for (int k = 0; k < 4; k++) std::swap(M[c][k], M[p][k]);
		for (int r = 0; r < 3; r++) {
			if (r == c) continue;
			cd m = M[r][c] / M[c][c];
			for (int k = 0; k < 4; k++) M[r][k] -= m * M[c][k];
		}
	}
	return M[0][3] / M[0][0];
}

/** Chip 3's output stage, multiple-feedback low-pass, from its nodal equations. */
static cd lpf2Analog(double f) {
	cd s(0.0, 2.0 * M_PI * f);
	cd a = 1.0 / 5100.0;
	cd den = a + 10e3 * s * 100e-12 * (2.0 / 5100.0 + 1.0 / 10e3 + s * 5.6e-9);
	return -a / den;
}

/** A chip's input stage in full, second pole included, per unit of 1/Rk. */
static cd lpf1Analog(double f, double G) {
	cd s(0.0, 2.0 * M_PI * f);
	double a1 = 6800.0 * 510e-12 * G, a2 = 6800.0 * 510e-12 * 4.7e-9;
	return -1.0 / (1.0 / 10e3 + a1 * s + a2 * s * s);
}

/** The limiter, solved by bisection on the circuit's own equation, for the
    voltage at the attenuator resistor given the current into the 10k stage. */
static double limiterRef(double iin) {
	const double k = 1.0 / 1e3 + 1.0 / 100e3;
	auto f = [&](double V) {
		double ibr = (V < 0 ? -1.0 : 1.0) * 1e-3 * std::exp((std::fabs(V) - 6.2) / 0.16);
		return 11.0 * ibr + k * V + 10.0 * iin;
	};
	double lo = -12.0, hi = 12.0;
	for (int i = 0; i < 200; i++) { double mid = 0.5 * (lo + hi); (f(mid) > 0 ? hi : lo) = mid; }
	return 0.5 * (lo + hi);
}

static double db(double x) { return 20.0 * std::log10(x); }


int main() {
	const double fs = 48000.0;

	// --- the clocks --------------------------------------------------------------
	// Electric Druid's fit: ms = 11.46 * kohm + 29.70, and ms = 683.21 / MHz + 0.08.
	// The delay of each chip in each mode is then fixed by the schematic's
	// resistors (and the switch's on-resistance); the clock must land inside the
	// VCO's measured 2-22 MHz range.
	printf("  clocks...\n");
	{
		const double want[2][3] = {{60.2, 48.5, 39.6}, {123.7, 93.9, 73.6}};   // Tronic, Verb below
		for (int i = 0; i < 3; i++) {
			double tr = Verbtronic::delayMs(i, false), vb = Verbtronic::delayMs(i, true);
			CHECK(std::fabs(tr - want[1][i]) < 0.15, "tronic delay", "chip %d: %.2f ms, want %.1f", i + 1, tr, want[1][i]);
			CHECK(std::fabs(vb - want[0][i]) < 0.15, "verb delay", "chip %d: %.2f ms, want %.1f", i + 1, vb, want[0][i]);
			CHECK(vb < tr, "mode", "chip %d: closing the switch must shorten the delay", i + 1);
			for (int m = 0; m < 2; m++) {
				double mhz = Pt2399::clockHzFor(Verbtronic::pin6(i, m == 1)) * 1e-6;
				CHECK(mhz > 2.0 && mhz < 22.1, "VCO range", "chip %d clock %.2f MHz outside 2-22", i + 1, mhz);
			}
		}
		// the three Tronic delays are not multiples of one another: no comb lines
		CHECK(Verbtronic::delayMs(0, false) / Verbtronic::delayMs(2, false) > 1.6 &&
		      Verbtronic::delayMs(0, false) / Verbtronic::delayMs(2, false) < 1.7, "ratio", "unexpected delay ratio");
	}

	// --- a step arrives after the delay, at every sample rate -------------------------
	printf("  chip delay...\n");
	for (int rate = 0; rate < 3; rate++) {
		double sr = rate == 0 ? 44100.0 : rate == 1 ? 48000.0 : 96000.0;
		Pt2399 c;
		c.setSampleRate(sr);
		c.setPin6(Verbtronic::pin6(1, true));
		double t0 = 0.01, arrive = -1.0;
		for (size_t n = 0; n < (size_t)(sr * 0.2); n++) {
			double t = (double)n / sr;
			float x = t >= t0 ? 1.0f : 0.0f;
			c.begin();
			float y = c.demod();
			c.modulate(x, x);
			if (arrive < 0.0 && y > 0.5f) arrive = t - t0;
		}
		double want = c.delaySeconds();
		CHECK(arrive > 0 && std::fabs(arrive - want) < 0.001, "step arrival",
		      "%.0f Hz: arrived after %.2f ms, delay is %.2f ms", sr, arrive * 1e3, want * 1e3);
	}

	// --- the chip against its datasheet -------------------------------------------------
	// "AC characteristics: Vcc = 5 V, fin = 1 kHz, Vi = 100 mVrms, fck = 2 MHz".
	// fck = 2 MHz is the slowest clock the datasheet specifies: about 341 ms.
	printf("  chip against the datasheet...\n");
	{
		double R = 27.2e3;   // the pin-6 resistance that gives a 2 MHz clock
		CHECK(std::fabs(Pt2399::clockHzFor(R) - 2e6) < 0.05e6, "clock", "R=%.0f gives %.3f MHz", R, Pt2399::clockHzFor(R) * 1e-6);
		std::vector<double> y = chipTone(R, 0.1 * std::sqrt(2.0), 1000.0, fs, 3.0);
		double gain = db(bin(y, y.size() - (size_t)fs, (size_t)fs, 1000.0, fs) / (0.1 * std::sqrt(2.0)));
		double dist = thd(y, fs);
		printf("      100 mVrms, 2 MHz: gain %.2f dB, THD %.3f %%\n", gain, 100 * dist);
		CHECK(gain > -1.5 && gain < 0.5, "voltage gain", "%.2f dB; the datasheet's Gv is -0.5 dB typical", gain);
		CHECK(dist < 0.01, "THD", "%.3f %% against the datasheet's 1 %% maximum", 100 * dist);
		// the same chip at the Verb clock is cleaner: more bits per second
		std::vector<double> y2 = chipTone(Verbtronic::pin6(2, true), 0.1 * std::sqrt(2.0), 1000.0, fs, 3.0);
		CHECK(thd(y2, fs) < dist || thd(y2, fs) < 0.003, "THD vs clock", "a faster clock should not distort more");
		// Maximum output: the amplitude at which THD+N reaches 10 %, as an rms
		// output, is 1.5-2.5 Vrms in the datasheet (2 typical).
		double rmsAt10 = -1.0;
		for (double amp = 1.5; amp <= 4.6 && rmsAt10 < 0; amp += 0.1) {
			std::vector<double> z = chipTone(R, amp, 1000.0, fs, 1.5);
			if (thdn(z, fs) >= 0.10) rmsAt10 = rms(z, z.size() - (size_t)fs, z.size());
		}
		printf("      output at 10%% THD+N: %.2f Vrms\n", rmsAt10);
		CHECK(rmsAt10 > 1.5 && rmsAt10 < 2.5, "Vomax", "%.2f Vrms at 10 %% THD+N; the datasheet says 1.5-2.5", rmsAt10);
	}

	// --- changing the clock replays what is stored at the new rate -----------------------
	// The hardware's mode change: whatever is in the memory comes out at the new
	// sample rate, so it is shifted in pitch by the ratio of the clocks.
	printf("  a clock change bends what is stored...\n");
	{
		Pt2399 c;
		c.setSampleRate(fs);
		double Rl = Verbtronic::pin6(0, false), Rs = Verbtronic::pin6(0, true);
		c.setPin6(Rl);
		double ratio = Verbtronic::delayMs(0, false) / Verbtronic::delayMs(0, true);
		size_t switchAt = (size_t)(fs * 0.6), N = (size_t)(fs * 0.8);
		std::vector<double> out(N);
		double prev = 0;
		for (size_t n = 0; n < N; n++) {
			if (n == switchAt) c.setPin6(Rs);
			double x = 0.3 * std::sin(2.0 * M_PI * 1000.0 * (double)n / fs);
			c.begin(); out[n] = c.demod(); c.modulate((float)prev, (float)x); prev = x;
		}
		size_t w0 = switchAt + (size_t)(0.004 * fs), wn = (size_t)(0.040 * fs);
		double at1k = bin(out, w0, wn, 1000.0, fs), atShift = bin(out, w0, wn, 1000.0 * ratio, fs);
		printf("      clock ratio %.3f: %.3f V at 1 kHz, %.3f V at %.0f Hz\n", ratio, at1k, atShift, 1000.0 * ratio);
		CHECK(atShift > 4.0 * at1k && atShift > 0.15, "pitch shift", "the stored tone should leave at %.0f Hz", 1000.0 * ratio);
	}

	// --- the tone stage -------------------------------------------------------------------
	printf("  tone stage...\n");
	{
		const double xs[] = {0.05, 0.25, 0.5, 0.75, 0.95};
		double worst = 0;
		for (int xi = 0; xi < 5; xi++) {
			Verbtronic v;
			v.setSampleRate(fs);
			v.setTone(xs[xi]);
			for (double f = 50.0; f <= 12000.0; f *= 1.3) {
				// drive the digital section with a tone and measure it
				Section1 sec = v.tone;
				size_t n = (size_t)(fs * 0.5);
				std::vector<double> y(n);
				for (size_t i = 0; i < n; i++) y[i] = sec.process(std::sin(2.0 * M_PI * f * (double)i / fs));
				double a = 0, b = 0; size_t cnt = n / 2;
				for (size_t i = cnt; i < n; i++) { double ph = 2.0 * M_PI * f * (double)i / fs; a += y[i] * std::sin(ph); b += y[i] * std::cos(ph); }
				double dig = 2.0 * std::sqrt(a * a + b * b) / (double)cnt;
				double ana = std::abs(toneAnalog(f, xs[xi]));
				worst = std::fmax(worst, std::fabs(db(dig) - db(ana)));
			}
		}
		printf("      worst digital-vs-analog error %.2f dB, 50 Hz-12 kHz\n", worst);
		CHECK(worst < 1.0, "tone accuracy", "%.2f dB from the analog response", worst);
		// flat at centre, +-7.5 dB tilt about 1 kHz at the ends, dark at the CW lug
		CHECK(std::fabs(db(std::abs(toneAnalog(100, 0.5)))) < 0.1 && std::fabs(db(std::abs(toneAnalog(10000, 0.5)))) < 0.3,
		      "tone centre", "not flat at the pot's centre");
		CHECK(db(std::abs(toneAnalog(100, 0.02))) > 6.0 && db(std::abs(toneAnalog(10000, 0.02))) < -5.0, "tone at the CW lug", "should be bass up, treble down");
		CHECK(db(std::abs(toneAnalog(100, 0.98))) < -6.0 && db(std::abs(toneAnalog(10000, 0.98))) > 5.0, "tone at the CCW lug", "should be bass down, treble up");
		CHECK(std::fabs(std::abs(toneAnalog(1000, 0.1)) - std::abs(toneAnalog(1000, 0.9))) < 0.2, "tone pivot", "the tilt should pivot near 1 kHz");
		// the pot's direction reaches the stage: fully clockwise is the CW lug
		{
			Verbtronic w; w.setSampleRate(fs);
			Verbtronic::In in; Verbtronic::Out o;
			in.tilt = 1.0; w.process(in, o);
			CHECK(std::fabs(w.toneX - 0.0) < 1e-9, "tilt direction", "TILT fully clockwise put the wiper at %.3f, not at the CW lug", w.toneX);
			in.tilt = 0.0; w.process(in, o);
			CHECK(std::fabs(w.toneX - 1.0) < 1e-9, "tilt direction", "TILT fully anticlockwise put the wiper at %.3f", w.toneX);
		}
		// the section sign is inverting
		Verbtronic v; v.setSampleRate(fs); v.setTone(0.5);
		double y = 0; for (int i = 0; i < 20000; i++) y = v.tone.process(1.0);
		CHECK(y < -0.99 && y > -1.01, "tone sign", "DC gain at centre is %.3f, the stage inverts with unity gain", y);
	}

	// --- the chip's output filter and input stages ---------------------------------------------
	printf("  chip filters...\n");
	{
		Verbtronic v; v.setSampleRate(fs);
		double worstLpf2 = 0, worstLpf1 = 0;
		for (double f = 100.0; f <= 12000.0; f *= 1.25) {
			size_t n = (size_t)(fs * 0.3), cnt = n / 2;
			Section2 s2 = v.outLpf, s1 = v.lpf1[0];
			double a2 = 0, b2 = 0, a1 = 0, b1 = 0;
			for (size_t i = 0; i < n; i++) {
				double ph = 2.0 * M_PI * f * (double)i / fs, x = std::sin(ph);
				double y2 = s2.process(x), y1 = s1.process(x);
				if (i >= cnt) { a2 += y2 * std::sin(ph); b2 += y2 * std::cos(ph); a1 += y1 * std::sin(ph); b1 += y1 * std::cos(ph); }
			}
			double d2 = 2.0 * std::sqrt(a2 * a2 + b2 * b2) / (double)cnt, d1 = 2.0 * std::sqrt(a1 * a1 + b1 * b1) / (double)cnt;
			worstLpf2 = std::fmax(worstLpf2, std::fabs(db(d2) - db(std::abs(lpf2Analog(f)))));
			double G = 1.0 / 10e3 + 1.0 / 12.1e3 + 1.0 / 12.1e3 + 1.0 / 10e3 + 1.0 / 6.8e3;
			worstLpf1 = std::fmax(worstLpf1, std::fabs(db(d1) - db(std::abs(lpf1Analog(f, G)) / 10e3)));
		}
		printf("      LPF2 and LPF1 within %.2f and %.2f dB of the analog stages, to 12 kHz\n", worstLpf2, worstLpf1);
		CHECK(worstLpf2 < 2.0, "LPF2", "%.2f dB from the analog stage", worstLpf2);
		CHECK(worstLpf1 < 1.5, "LPF1", "%.2f dB from the analog stage", worstLpf1);
		CHECK(std::fabs(std::abs(lpf2Analog(10.0)) - 1.0) < 1e-3, "LPF2 gain", "DC gain is not unity");
	}

	// --- the feedback limiter ----------------------------------------------------------------------------
	printf("  feedback limiter...\n");
	{
		Verbtronic v; v.setSampleRate(fs);
		double amount, worst = 0;
		for (double iin = -2e-3; iin <= 2e-3; iin += 5e-5) {
			double got = v.limiter(iin, amount), want = limiterRef(iin);
			worst = std::fmax(worst, std::fabs(got - want));
		}
		CHECK(worst < 2e-4, "limiter solution", "%.6f V from an independent solve", worst);
		// small signals: inverting, a hair under unity (the 1k against the 100k)
		double small = v.limiter(1e-4, amount) / (1e-4 * 10e3);
		CHECK(std::fabs(small + 0.990) < 0.002, "limiter small-signal", "gain %.4f, want -0.990", small);
		// large signals: clamped near the zener plus two diodes, and odd
		double hi = v.limiter(-5e-3, amount), lo = v.limiter(5e-3, amount);
		CHECK(hi > 6.0 && hi < 7.0 && std::fabs(hi + lo) < 1e-9, "limiter clamp", "clamps at %.3f / %.3f V", hi, lo);
		// and it never lets go: a bigger drive does not raise it much
		double more = v.limiter(-50e-3, amount);
		CHECK(more - hi < 0.8, "limiter stiffness", "%.3f V more for 10x the drive", more - hi);
	}

	// --- the mixer ----------------------------------------------------------------------------
	// The control loop makes the VCA's gain the control current over a 120 uA
	// reference: linear in the MIX pot (loaded by its 100k), in the CV (3.61 V
	// for the whole range through 30k1) and 1 % shy of unity at the top, because
	// of the 10 M to -12 V. Dry is untouched by the wet at gain 0.
	printf("  mixer...\n");
	{
		struct Run { Verbtronic v; Verbtronic::In in; Verbtronic::Out o; Run() { v.setSampleRate(48000.0); } };
		Run r;
		r.in.vin = 3.0;
		r.in.mix = 0.0; r.in.mixAtten = 0.5;
		r.v.process(r.in, r.o);
		CHECK(r.o.gain == 0.0 && std::fabs(r.o.mix - 3.0) < 1e-12, "mix at 0", "gain %.4f, out %.6f", r.o.gain, r.o.mix);
		r.in.mix = 1.0;
		r.v.process(r.in, r.o);
		CHECK(std::fabs(r.o.gain - 0.99) < 1e-9, "mix at 100", "gain %.6f, want 0.99", r.o.gain);
		r.in.mix = 0.5;
		r.v.process(r.in, r.o);
		CHECK(std::fabs(r.o.gain - 0.5 / 1.25 + 0.01) < 1e-9, "mix loading", "gain %.6f, want %.6f", r.o.gain, 0.5 / 1.25 - 0.01);
		// attenuverter at its centre is off, however large the CV
		r.in.mix = 0.5; r.in.mixCv = 8.0; r.in.mixAtten = 0.5;
		r.v.process(r.in, r.o);
		CHECK(std::fabs(r.o.gain - (0.5 / 1.25 - 0.01)) < 1e-9, "attenuverter centre", "the CV leaked through at centre");
		// fully CW passes +CV: 3.61 V is the whole range from zero
		r.in.mix = 0.0; r.in.mixAtten = 1.0; r.in.mixCv = 3.612;
		r.v.process(r.in, r.o);
		CHECK(std::fabs(r.o.gain - 0.99) < 0.003, "mix CV range", "gain %.4f at +3.612 V", r.o.gain);
		// fully CCW inverts it: the same CV pulls a half-open mix shut
		r.in.mix = 0.8; r.in.mixAtten = 0.0; r.in.mixCv = 3.0;
		r.v.process(r.in, r.o);
		double g8 = 0.8 / (1.0 + 0.8 * 0.2) - 0.01;
		CHECK(std::fabs(r.o.gain - (g8 - 3.0 / 3.612)) < 0.003 || r.o.gain == 0.0, "attenuverter inverts", "gain %.4f", r.o.gain);
		// VERB OUT does not depend on MIX at all
		Run a, b;
		a.in.vin = b.in.vin = 2.0; a.in.mix = 0.1; b.in.mix = 0.9; a.in.feedback = b.in.feedback = 0.6;
		double dv = 0;
		for (int i = 0; i < 24000; i++) {
			a.in.vin = b.in.vin = 2.0 * std::sin(2.0 * M_PI * 300.0 * i / 48000.0);
			a.v.process(a.in, a.o); b.v.process(b.in, b.o);
			dv = std::fmax(dv, std::fabs(a.o.verb - b.o.verb));
		}
		CHECK(dv == 0.0, "VERB OUT", "VERB OUT moved with MIX by %.6f V", dv);
	}

	// --- the whole circuit ---------------------------------------------------------------------
	printf("  the network...\n");
	{
		struct Result { std::vector<double> w; double maxAbs = 0; bool finite = true; };
		auto run = [&](bool verb, double fb, double secs, double burstSecs, double amp) {
			Verbtronic v; v.setSampleRate(fs);
			Verbtronic::In in; Verbtronic::Out o;
			in.feedback = fb; in.verb = verb; in.mix = 1.0;
			Result r; size_t N = (size_t)(fs * secs); r.w.resize(N);
			for (size_t n = 0; n < N; n++) {
				double t = (double)n / fs;
				in.vin = t < burstSecs ? amp * std::sin(2.0 * M_PI * 440.0 * t) : 0.0;
				v.process(in, o);
				r.w[n] = o.verb;
				if (!std::isfinite(o.verb) || !std::isfinite(o.mix)) r.finite = false;
				r.maxAbs = std::fmax(r.maxAbs, std::fabs(o.verb));
			}
			return r;
		};
		for (int m = 0; m < 2; m++) {
			bool verb = m == 0;
			double fbs[] = {0.0, 0.25, 0.5, 0.75, 1.0};
			for (int k = 0; k < 5; k++) {
				Result r = run(verb, fbs[k], 8.0, 0.05, 5.0);
				CHECK(r.finite, "finite", "%s fb %.2f went non-finite", verb ? "Verb" : "Tronic", fbs[k]);
				CHECK(r.maxAbs <= 10.5 + 1e-9, "rails", "%s fb %.2f reached %.2f V", verb ? "Verb" : "Tronic", fbs[k], r.maxAbs);
				// it speaks after the delay, not before
				double early = rms(r.w, 0, (size_t)(fs * 0.03));
				double late = rms(r.w, (size_t)(fs * 0.10), (size_t)(fs * 0.30));
				CHECK(late > 0.05, "wet level", "%s fb %.2f: wet is %.4f V rms 100-300 ms after a 5 V burst", verb ? "Verb" : "Tronic", fbs[k], late);
				if (m == 1) CHECK(early < 0.01, "latency", "Tronic sounded %.4f V inside its shortest delay", early);
			}
			// at no feedback the tail dies; at full feedback it does not
			Result off = run(verb, 0.0, 12.0, 0.05, 5.0), full = run(verb, 1.0, 12.0, 0.05, 5.0);
			double a = rms(off.w, (size_t)(fs * 0.2), (size_t)(fs * 0.5));
			double z = rms(off.w, (size_t)(fs * 10.0), (size_t)(fs * 12.0));
			printf("      %s: tail %.3f V at 0.2-0.5 s, %.4f V at 10-12 s at no feedback; full feedback holds %.2f V\n",
			       verb ? "Verb  " : "Tronic", a, z, rms(full.w, (size_t)(fs * 10.0), (size_t)(fs * 12.0)));
			CHECK(z < a * 0.1, "tail decays", "%s: %.4f against %.4f", verb ? "Verb" : "Tronic", z, a);
			CHECK(rms(full.w, (size_t)(fs * 10.0), (size_t)(fs * 12.0)) > 5.0, "full feedback holds", "%s did not sustain", verb ? "Verb" : "Tronic");
		}
		// more feedback, longer tail
		Result f2 = run(true, 0.3, 8.0, 0.05, 5.0), f6 = run(true, 0.6, 8.0, 0.05, 5.0);
		CHECK(rms(f6.w, (size_t)(fs * 4.0), (size_t)(fs * 6.0)) > rms(f2.w, (size_t)(fs * 4.0), (size_t)(fs * 6.0)),
		      "FEEDBACK direction", "a higher FEEDBACK should ring longer");
		// Chips 1 and 2 hear the input; chip 3 hears only their sum. So at no
		// feedback the first sound out is the shorter of chips 1 and 2 plus chip
		// 3's own delay, and Tronic's is later than Verb's.
		auto onset = [&](const Result& r) { for (size_t i = 0; i < r.w.size(); i++) if (std::fabs(r.w[i]) > 0.02) return (double)i / fs; return -1.0; };
		Result tv = run(true, 0.0, 1.0, 0.05, 5.0), tt = run(false, 0.0, 1.0, 0.05, 5.0);
		double wantV = 1e-3 * (std::fmin(Verbtronic::delayMs(0, true), Verbtronic::delayMs(1, true)) + Verbtronic::delayMs(2, true));
		double wantT = 1e-3 * (std::fmin(Verbtronic::delayMs(0, false), Verbtronic::delayMs(1, false)) + Verbtronic::delayMs(2, false));
		printf("      first sound at %.1f ms (Verb, expected %.1f) and %.1f ms (Tronic, expected %.1f)\n",
		       onset(tv) * 1e3, wantV * 1e3, onset(tt) * 1e3, wantT * 1e3);
		CHECK(std::fabs(onset(tv) - wantV) < 0.008, "Verb latency", "first sound at %.1f ms, the delays say %.1f", onset(tv) * 1e3, wantV * 1e3);
		CHECK(std::fabs(onset(tt) - wantT) < 0.008, "Tronic latency", "first sound at %.1f ms, the delays say %.1f", onset(tt) * 1e3, wantT * 1e3);
		CHECK(onset(tt) > onset(tv) + 0.05, "Tronic is longer", "Tronic %.1f ms is not later than Verb %.1f ms", onset(tt) * 1e3, onset(tv) * 1e3);
	}

	// --- a mode change on a ringing loop ---------------------------------------------------------------------
	// Nothing fades: the DG202 changes the clock resistors and what is in the
	// RAM comes out at the new rate. It must stay finite, bounded, and keep
	// sounding -- and the Tronic-to-Verb change must be audibly different from
	// the same loop left alone.
	printf("  a mode change...\n");
	{
		Verbtronic v, ref; v.setSampleRate(fs); ref.setSampleRate(fs);
		Verbtronic::In in, in2; Verbtronic::Out o, o2;
		in.feedback = in2.feedback = 0.9; in.verb = in2.verb = false; in.mix = in2.mix = 1.0;
		bool finite = true; double maxAbs = 0, diff = 0; size_t N = (size_t)(fs * 4.0), at = (size_t)(fs * 2.0);
		std::vector<double> y(N);
		for (size_t n = 0; n < N; n++) {
			double t = (double)n / fs;
			in.vin = in2.vin = t < 0.3 ? 4.0 * std::sin(2.0 * M_PI * 330.0 * t) : 0.0;
			if (n == at) in.verb = true;
			v.process(in, o); ref.process(in2, o2);
			y[n] = o.verb;
			if (!std::isfinite(o.verb)) finite = false;
			maxAbs = std::fmax(maxAbs, std::fabs(o.verb));
			if (n >= at) diff = std::fmax(diff, std::fabs(o.verb - o2.verb));
		}
		CHECK(finite && maxAbs <= 10.5 + 1e-9, "mode change bounded", "finite %d, peak %.2f V", (int)finite, maxAbs);
		CHECK(diff > 0.1, "mode change is audible", "the output differed by only %.4f V from not changing", diff);
		CHECK(rms(y, at + (size_t)(0.1 * fs), at + (size_t)(0.6 * fs)) > 0.01, "mode change sounds", "went silent");
	}

	// --- sample rate -----------------------------------------------------------------------------------------
	printf("  sample rates...\n");
	{
		double want = 0;
		for (int i = 0; i < 3; i++) {
			double sr = i == 0 ? 44100.0 : i == 1 ? 48000.0 : 96000.0;
			Verbtronic v; v.setSampleRate(sr);
			Verbtronic::In in; Verbtronic::Out o;
			in.feedback = 0.0; in.verb = true; in.mix = 1.0;
			size_t N = (size_t)(sr * 0.4);
			std::vector<double> y(N);
			for (size_t n = 0; n < N; n++) { in.vin = n < (size_t)(sr * 0.002) ? 5.0 : 0.0; v.process(in, o); y[n] = o.verb; }
			size_t on = 0; while (on < N && std::fabs(y[on]) <= 0.02) on++;
			double t = (double)on / sr;
			if (i == 0) want = t;
			CHECK(on < N && std::fabs(t - want) < 0.003, "sample rate", "%.0f Hz: first sound at %.2f ms, 44.1 kHz says %.2f ms", sr, t * 1e3, want * 1e3);
		}
	}

	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}

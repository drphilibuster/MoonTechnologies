// Rebate -- the analog half of the MIDIverb, from the "Audio I/O" and "DAC" pages
// of Eric Brombaugh's schematic (MIDIVerb_RE, MIT), and the board around it.
//
// Everything runs at four times the DSP's rate (93,750 Hz), in volts:
//
//   IN L/R -- 0.1u / 51k high-pass (31 Hz) -- x5.17 (TL082, 10k/2.4k) -----------+-- dry
//     the two summed through 10k each into
//   SK1  5k+5.1k, 0.01u/3300p   ~5.5 kHz, Q 0.87          ("6.5 kHz, mostly flat")
//   SK2  10k+10k, 0.01u/330p    ~8.8 kHz, Q 2.75          ("13 kHz, 10 dB peak")
//   SK3  4.7k+4.7k, 0.047u/220p ~10.5 kHz, Q 7.3          ("18 kHz, 18 dB peak")
//        with D1 from its + input to the CD4053's VDD: 1k from +5 V, 0.1u to ground.
//        The schematic marks it "Clipping?". It holds the switch's input inside its
//        supply, clamping positive peaks at about +5.6 V; and since that rail is a
//        1k/0.1u RC, a long clip lifts it -- the clamp is soft and has a memory.
//   -- 220R -- CD4053 -- 4700p ADC hold cap: tracks from the end of one conversion
//        to the start of the next (64 clocks), then holds while the SAR converts.
//        The DSP reads that conversion at step 0 of the next sample.
//   DAC -- CD4053 -- 1000p hold cap per channel (right at clock 0xC8, left at
//        0xE8) -- buffer -- 1k/1500p -- SK 6.8k+6.8k, 3300p/1500p (~10.5 kHz) -- wet.
//        The RC and the SK are not buffered from each other: they are one third-
//        order network, solved here as one, exactly for the staircase it is fed.
//   Dual-gang MIX pot between dry and wet, into 2.4k / 560R to the jack.
//
// The model is MAME's (alesis/midiverb.cpp, BSD-3-Clause, m1macrophage) where the
// two agree, with these taken from the schematic instead: the output divider is
// 2.4k/560R (MAME: 500R); the SK3 clamp; the ADC's sample-and-hold and its
// one-sample latency; the DAC hold timing; the RC+SK as one network; the pot's
// loading. Not on the schematic, so assumed: the pot's value (MIX_POT); TL082
// output swing (VSAT); the CD4053's on-resistance (RON); D1 a 1N4148.
#pragma once
#include "Machine.hpp"

#include <algorithm>
#include <cmath>

namespace mv {

struct Analog {
	static constexpr int OS = 4;
	static constexpr double FS = Machine::SAMPLE_RATE * OS;   // 93,750 Hz
	static constexpr double T = 1.0 / FS;
	static constexpr double VSAT = 10.5;      // TL082 output swing on +/-12 V (assumed)
	static constexpr double FULL_SCALE = 4.8; // converter full scale (MAME)
	static constexpr double RON = 250.0;      // CD4053 on-resistance, VDD-VEE = 17 V (assumed)
	static constexpr double MIX_POT = 10e3;   // the mix pot (assumed; not marked)
	static constexpr double R_OUT = 2.4e3, R_JACK = 560.0;

	float mix = 1.f;                 // 0 dry .. 1 wet, the pot's rotation
	float meterGreen = 0.f, meterRed = 0.f;   // the -12 dB and 0 dB LEDs, 0..1

	Analog() { design(); reset(); }

	void reset() {
		hp[0] = hp[1] = {};
		sk1.z1 = sk1.z2 = sk2.z1 = sk2.z2 = 0.0;
		sk3x[0] = sk3x[1] = 0.0;
		sk3x[2] = V5;
		sk3In = 0.0;
		held = 0.0;
		adcWord = 1;
		dacV[0] = dacV[1] = 0.0;
		for (auto& c : outX) for (double& v : c) v = 0.0;
		meterGreen = meterRed = 0.f;
		greenHold = 0.0;
	}

	/** One DSP sample: four input frames in, four output frames out, in circuit volts
	    (frame 0 is the tick at which the ADC's hold capacitor lets go). */
	void run(Machine& m, const float in[OS][2], float out[OS][2]) {
		double dry[OS][2];
		double sk3Out = 0.0, peak = 0.0;
		for (int k = 0; k < OS; k++) {
			sk3Out = input(in[k][0], in[k][1], dry[k]);
			if (k == 0) {
				// Hold: the cap has tracked SK3 since the last conversion ended.
				held = sk3Out + (held - sk3Out) * acquire;
			}
			peak = std::max(peak, sk3Out);
		}
		meters(peak);

		// The DSP reads the conversion of the previous hold; this one is converted
		// during this sample, for the next.
		int16_t r = 0, l = 0;
		m.sample(adcWord, r, l);
		adcWord = convert(held);

		const double newV[2] = { dacVolts(l), dacVolts(r) };
		for (int k = 0; k < OS; k++) {
			double wet[2];
			for (int c = 0; c < 2; c++) {
				double* x = outX[c];
				if (k < OS - 1) propagate(x, full, dacV[c]);
				else {
					// Both hold caps are refreshed in the last quarter: right at 1/8
					// of it, left at 5/8.
					const int split = c == 0 ? 0 : 2;   // left: 5/8, right: 1/8
					propagate(x, part[split], dacV[c]);
					propagate(x, part[split + 1], newV[c]);
					dacV[c] = newV[c];
				}
				wet[c] = clampV(x[2]);
			}
			for (int c = 0; c < 2; c++) out[k][c] = float(pot(dry[k][c], wet[c]) * R_JACK / (R_OUT + R_JACK));
		}
	}

	/** One tick of the input side: the two buffered inputs (the dry signal) and
	    the third filter's output, which is what the ADC's hold cap tracks. */
	double input(double l, double r, double dry[2]) {
		const double x[2] = { l, r };
		for (int c = 0; c < 2; c++) dry[c] = clampV(hpf(hp[c], x[c]) * (1.0 + 10e3 / 2.4e3));
		return sk3(sat(biquad(sk2, sat(biquad(sk1, 0.5 * (dry[0] + dry[1]))))));
	}

	/** One tick of a channel's output network on a held DAC voltage (tests). */
	double outputTick(int c, double v) {
		propagate(outX[c], full, v);
		return outX[c][2];
	}

	/** The converter word for a held voltage: MAME's 12-bit SAR, with bit 0 set. */
	static uint16_t convert(double v) {
		const double x = std::max(-FULL_SCALE, std::min(FULL_SCALE, v)) / FULL_SCALE;
		return uint16_t(int16_t(std::floor(x * 2047.0) * 2 + 1));
	}
	static double dacVolts(int16_t w) { return FULL_SCALE * std::max(-4095, std::min(4095, int(w))) / 4095.0; }

private:
	// --- linear sections --------------------------------------------------------------
	struct Hp { double x1 = 0, y1 = 0; };
	struct Bq { double b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0; };
	Hp hp[2];
	double hpB = 0, hpA = 0;
	Bq sk1, sk2;

	static double clampV(double v) { return std::max(-VSAT, std::min(VSAT, v)); }
	static double sat(double v) { return clampV(v); }

	double hpf(Hp& s, double x) {
		const double y = hpB * (x - s.x1) + hpA * s.y1;
		s.x1 = x; s.y1 = y;
		return y;
	}
	static double biquad(Bq& q, double x) {
		const double y = q.b0 * x + q.z1;
		q.z1 = q.b1 * x - q.a1 * y + q.z2;
		q.z2 = q.b2 * x - q.a2 * y;
		return y;
	}
	/** A unity-gain Sallen-Key low-pass, R1 from the input, C1 to the output, C2 to
	    ground: H(s) = 1 / (1 + s C2 (R1 + R2) + s^2 R1 R2 C1 C2), bilinear, prewarped
	    at its own natural frequency. */
	static Bq sallenKey(double r1, double r2, double c1, double c2) {
		const double a2 = r1 * r2 * c1 * c2, a1 = c2 * (r1 + r2);
		const double w0 = 1.0 / std::sqrt(a2);
		const double K = w0 / std::tan(w0 * T / 2);
		const double D = a2 * K * K + a1 * K + 1;
		Bq q;
		q.b0 = 1 / D; q.b1 = 2 / D; q.b2 = 1 / D;
		q.a1 = (2 - 2 * a2 * K * K) / D;
		q.a2 = (a2 * K * K - a1 * K + 1) / D;
		return q;
	}

	// --- SK3 and its clamp, TR-BDF2 (the diode makes it stiff) --------------------------
	static constexpr double R3 = 4.7e3, C11 = 0.047e-6, C12 = 220e-12;
	static constexpr double R42 = 1e3, C26 = 0.1e-6, V5 = 5.0;
	static constexpr double IS = 2.52e-9, NVT = 1.752 * 0.02585;   // 1N4148
	double sk3x[3] = {};   // C11's voltage (A - B), node B (+ input = output), the clamp rail
	double sk3In = 0.0;

	struct F3 { double f[3]; double J[3][3]; };
	static F3 sk3f(const double x[3], double vin) {
		const double u = x[0], vb = x[1], v5 = x[2];
		const double va = u + vb;
		const double i1 = (vin - va) / R3, i2 = u / R3;
		const double e = std::exp(std::min(60.0, (vb - v5) / NVT));
		const double id = IS * (e - 1), gd = IS * e / NVT;
		F3 r;
		r.f[0] = (i1 - i2) / C11;
		r.f[1] = (i2 - id) / C12;
		r.f[2] = ((V5 - v5) / R42 + id) / C26;
		const double J[3][3] = {
			{ -2 / (R3 * C11), -1 / (R3 * C11), 0 },
			{ 1 / (R3 * C12), -gd / C12, gd / C12 },
			{ 0, gd / C26, (-1 / R42 - gd) / C26 },
		};
		for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) r.J[i][j] = J[i][j];
		return r;
	}
	/** Solve x = base + c * f(x, vin) by Newton, from the guess in x. */
	static void implicit(double x[3], const double base[3], double c, double vin) {
		for (int it = 0; it < 40; it++) {
			const F3 r = sk3f(x, vin);
			double g[3], M[3][3];
			for (int i = 0; i < 3; i++) {
				g[i] = x[i] - base[i] - c * r.f[i];
				for (int j = 0; j < 3; j++) M[i][j] = (i == j ? 1.0 : 0.0) - c * r.J[i][j];
			}
			double d[3];
			solve3(M, g, d);
			// Limit the diode's step, so Newton cannot leap far up its exponential.
			const double dvd = d[1] - d[2];
			const double vd = x[1] - x[2];
			double s = 1.0;
			if (vd - dvd > 0.3 && std::fabs(dvd) > 4 * NVT) s = 4 * NVT / std::fabs(dvd);
			double big = 0;
			for (int i = 0; i < 3; i++) { x[i] -= s * d[i]; big = std::max(big, std::fabs(d[i])); }
			if (big < 1e-9 && s == 1.0) return;
		}
	}
	static void solve3(double M[3][3], const double g[3], double d[3]) {
		double a[3][4];
		for (int i = 0; i < 3; i++) { for (int j = 0; j < 3; j++) a[i][j] = M[i][j]; a[i][3] = g[i]; }
		for (int c = 0; c < 3; c++) {
			int p = c;
			for (int r = c + 1; r < 3; r++) if (std::fabs(a[r][c]) > std::fabs(a[p][c])) p = r;
			if (p != c) for (int j = 0; j < 4; j++) std::swap(a[c][j], a[p][j]);
			for (int r = c + 1; r < 3; r++) {
				const double f = a[r][c] / a[c][c];
				for (int j = c; j < 4; j++) a[r][j] -= f * a[c][j];
			}
		}
		for (int i = 2; i >= 0; i--) {
			double s = a[i][3];
			for (int j = i + 1; j < 3; j++) s -= a[i][j] * d[j];
			d[i] = s / a[i][i];
		}
	}
	double sk3(double vin) {
		static const double G = 2.0 - std::sqrt(2.0);
		const double v0 = sk3In, vg = v0 + G * (vin - v0);
		double xn[3] = { sk3x[0], sk3x[1], sk3x[2] };
		const F3 fn = sk3f(xn, v0);
		// Trapezoid to t + G T ...
		double base[3], xg[3];
		for (int i = 0; i < 3; i++) { base[i] = xn[i] + G * T / 2 * fn.f[i]; xg[i] = xn[i]; }
		implicit(xg, base, G * T / 2, vg);
		// ... then BDF2 to t + T.
		const double a = 1.0 / (G * (2 - G)), b = (1 - G) * (1 - G) / (G * (2 - G)), c = (1 - G) / (2 - G) * T;
		for (int i = 0; i < 3; i++) { base[i] = a * xg[i] - b * xn[i]; sk3x[i] = xg[i]; }
		implicit(sk3x, base, c, vin);
		sk3In = vin;
		return sat(sk3x[1]);
	}

	// --- the converters ---------------------------------------------------------------
	double acquire = 0.0;      // what the ADC hold cap keeps of its last value over the track window
	double held = 0.0;
	uint16_t adcWord = 1;
	double dacV[2] = {};       // the DAC hold caps: [0] left, [1] right

	// --- the output networks: x' = A x + B u, exact for a piecewise-constant u ---------
	struct Step { double P[3][3]; double G[3]; };
	Step full, part[4];        // part: left 5/8 + 3/8, right 1/8 + 7/8
	double outX[2][3] = {};    // per channel: C15's node, SK input node, SK output

	static void propagate(double x[3], const Step& s, double u) {
		double y[3];
		for (int i = 0; i < 3; i++) y[i] = s.P[i][0] * x[0] + s.P[i][1] * x[1] + s.P[i][2] * x[2] + s.G[i] * u;
		for (int i = 0; i < 3; i++) x[i] = y[i];
	}

	/** exp([[A, B], [0, 0]] h) by scaling and squaring: Phi and its integral times B. */
	static Step discretize(const double A[3][3], const double B[3], double h) {
		double M[4][4] = {};
		for (int i = 0; i < 3; i++) { for (int j = 0; j < 3; j++) M[i][j] = A[i][j] * h; M[i][3] = B[i] * h; }
		double norm = 0;
		for (auto& r : M) for (double v : r) norm = std::max(norm, std::fabs(v));
		int sq = 0;
		while (norm > 0.5) { norm /= 2; sq++; }
		const double sc = std::ldexp(1.0, -sq);
		for (auto& r : M) for (double& v : r) v *= sc;
		double E[4][4] = {}, term[4][4] = {};
		for (int i = 0; i < 4; i++) E[i][i] = term[i][i] = 1;
		for (int n = 1; n <= 16; n++) {
			double t[4][4] = {};
			for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) for (int k = 0; k < 4; k++) t[i][j] += term[i][k] * M[k][j] / n;
			for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) { term[i][j] = t[i][j]; E[i][j] += t[i][j]; }
		}
		for (int s = 0; s < sq; s++) {
			double t[4][4] = {};
			for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) for (int k = 0; k < 4; k++) t[i][j] += E[i][k] * E[k][j];
			for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) E[i][j] = t[i][j];
		}
		Step st;
		for (int i = 0; i < 3; i++) { for (int j = 0; j < 3; j++) st.P[i][j] = E[i][j]; st.G[i] = E[i][3]; }
		return st;
	}

	void design() {
		// Input high-pass: 0.1u into 51k.
		{
			const double rc = 51e3 * 0.1e-6, K = 2 * rc / T;
			hpB = K / (K + 1);
			hpA = (K - 1) / (K + 1);
		}
		sk1 = sallenKey(5e3, 5.1e3, 0.01e-6, 3300e-12);
		sk2 = sallenKey(10e3, 10e3, 0.01e-6, 330e-12);
		// ADC hold: 220R + switch into 4700p, tracking for 64 clocks.
		acquire = std::exp(-(64.0 / Machine::CLOCK) / ((220.0 + RON) * 4700e-12));
		// Output network: u -- R22 1k -- n0 (C15 1500p) -- R21 6.8k -- nA -- R20 6.8k --
		// nB (C13 1500p), C14 3300p from nA to the output, which follows nB.
		const double R0 = 1e3, C0 = 1500e-12, R1 = 6.8e3, R2 = 6.8e3, C1 = 3300e-12, C2 = 1500e-12;
		// States v0, vA, vB:
		//   C0 v0' = (u - v0)/R0 - (v0 - vA)/R1
		//   C2 vB' = (vA - vB)/R2
		//   C1 (vA' - vB') = (v0 - vA)/R1 - (vA - vB)/R2
		const double Af[3][3] = {
			{ -1 / (R0 * C0) - 1 / (R1 * C0), 1 / (R1 * C0), 0 },
			{ 1 / (R1 * C1), -1 / (R1 * C1) - 1 / (R2 * C1) + 1 / (R2 * C2), 1 / (R2 * C1) - 1 / (R2 * C2) },
			{ 0, 1 / (R2 * C2), -1 / (R2 * C2) },
		};
		const double B[3] = { 1 / (R0 * C0), 0, 0 };
		full = discretize(Af, B, T);
		part[0] = discretize(Af, B, T * 5 / 8);   // left: old value until 5/8
		part[1] = discretize(Af, B, T * 3 / 8);
		part[2] = discretize(Af, B, T * 1 / 8);   // right: old value until 1/8
		part[3] = discretize(Af, B, T * 7 / 8);
	}

	// --- the mix pot and the level LEDs --------------------------------------------------
	double pot(double dry, double wet) const {
		// Wiper between the wet end and the dry end, loaded by 2.4k + 560R.
		const double rw = (1.0 - mix) * MIX_POT, rd = double(mix) * MIX_POT, rl = R_OUT + R_JACK;
		if (rw <= 0) return wet;
		if (rd <= 0) return dry;
		return (wet / rw + dry / rd) / (1 / rw + 1 / rd + 1 / rl);
	}

	double greenHold = 0.0;
	/** Q7 lights the red LED once SK3's output passes 0.65 V through the 10k/3.3k
	    divider (2.6 V); the green, 12 dB below that, through a 4.7u peak hold. */
	void meters(double peak) {
		meterRed = peak > 0.65 * (10e3 + 3.3e3) / 3.3e3 ? 1.f : 0.f;
		greenHold = std::max(peak, greenHold * std::exp(-1.0 / (Machine::SAMPLE_RATE * 0.023)));
		meterGreen = greenHold > 0.25 * 0.65 * (10e3 + 3.3e3) / 3.3e3 ? 1.f : 0.f;
	}
};

} // namespace mv

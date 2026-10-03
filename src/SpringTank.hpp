#pragma once
// A spring-reverb tank from the physics of its springs.
//
// What this is
// ------------
// A reverb tank is one or more helical steel springs, a driver at one end and a pickup at the
// other (Parker & Bilbao, "Spring Reverberation: A Physical Perspective", DAFx-09, section 2).
// Waves on the helix are dispersive, and the way they disperse is what a spring reverb sounds
// like. Parker & Bilbao reduce the helix to a curved rod (their system (1), after Fletcher et
// al.) and give its dispersion relation (their (5)); this header solves that relation directly
// and builds the tank from it, so nothing about the dispersion is invented:
//
//   u_tt = (E/rho)(u_ss - k v_s)
//   v_tt = -(E r^2 / 4rho)(v_ssss + 2k^2 v_ss + k^4 v) + (E k/rho)(u_s - k v)
//
// with k = 1/R the curvature, r the wire radius, u the tangential and v the radial
// displacement. A plane wave exp(i(beta s - w t)) gives, with c^2 = E/rho,
//
//   W^2 - (c^2 beta^2 + B) W + c^2 beta^2 (c^2 r^2/4)(beta^2 - k^2)^2 = 0,
//   B = (c^2 r^2/4)(beta^2 - k^2)^2 + c^2 k^2,     W = w^2
//
// (this is their (5), derived again from (1) and checked against their closed forms in
// tests/Diversified/test_spring.cpp). The smaller root is the audio-band mode. It has two
// branches that matter:
//
//   LF  beta from 0 up to beta_c: w rises, the group velocity falls from v0 = c r / 2R to zero
//       at the transition frequency Fc. Group delay L/vg therefore rises from TD/2 to
//       infinity: LOW frequencies arrive FIRST and the echo is a RISING chirp that piles up at
//       Fc. TD = 2L/v0 ~ 4LR/(r c) is the time between echoes (their (2)); Fc ~
//       3 r c / (16 sqrt5 pi R^2) (their (4)).
//   HF  beta above k: w rises from zero again; the group delay falls with frequency (a
//       shorter, descending chirp over the whole band, "much lower amplitude" than the LF
//       series - Parker, EURASIP J. Adv. Signal Process. 2011, 646134, section 2).
//
// Each branch is then a waveguide loop: one traversal is a pure delay plus a cascade of
// first-order allpass sections whose group delay is fitted to L/vg(f) from the relation above
// (fitChain below; the fit's error against the relation is reported by Design and tested), and
// the loop closes through the far end's reflection, so the pickup sees echoes at TD, 3TD/2
// ... of the one-way delay. The LF loop runs at 2.06 Fc (the allpass chain's peak is at its
// Nyquist, where the physics puts Fc) and the HF loop at a fixed 22.05 kHz; a windowed-sinc
// rate converter takes the audio to and from each.
//
// What is NOT physical here, plainly: the loss (an ad-hoc per-traversal gain from an RT60 and a
// lowpass, which is what Parker & Bilbao themselves do, "in the absence of more detailed
// insight into the loss mechanisms"), the end reflection (|R| = 1 less that loss), the HF
// series' amplitude relative to the LF series, and the springs' parameters, which are Parker's
// own measurements of two real tanks (Tables 1 and 2) and are the ONE place to change them:
// leem1210() and olsonX82() below.
//
// No Rack dependency, so tests/ drives it bare.

#include <cmath>
#include <cstdint>
#include <vector>
#include <algorithm>
#include <functional>

namespace springtank {

static const double kPi = 3.14159265358979323846;

// ------------------------------------------------------------------------------------
// The springs. Everything a tank is made of lives in this block.
// ------------------------------------------------------------------------------------

struct Geometry {
	const char* name;
	double helixRadius;     // R, m (Parker's tables give the helix DIAMETER; halved here)
	double wireRadius;      // r, m (Parker's tables give the wire diameter)
	double turns;           // N
	double helixLength;     // H, m
	double youngs;          // E, Pa
	double density;         // rho, kg/m^3

	/** The uncurled wire length, Parker & Bilbao (3). */
	double wireLength() const {
		return std::hypot(2.0 * kPi * helixRadius * turns, helixLength);
	}
	double wave() const { return std::sqrt(youngs / density); }              // c, m/s
	double curvature() const { return 1.0 / helixRadius; }                   // k, 1/m
};

/** Steel, as Parker & Bilbao use it: E = 211 GPa (their text prints "211 N/m2", a misprint:
    steel's modulus is 2.11e11 Pa and it is what reproduces their TD and Fc), rho = 7800. */
static const double kSteelE = 2.11e11;
static const double kSteelRho = 7800.0;

/** Leem Pro KA-1210 guitar-amp tank, three springs in parallel (Parker & Bilbao Table 2):
    helix length 16.3 cm, helix diameter 0.44 / 0.45 / 0.46 cm, 303 / 280 / 351 turns, wire
    diameter 0.035 cm. The measurements were taken with a micrometer and calipers; Parker
    calls them "an idea of the geometries typical to a spring reverberation unit". */
inline Geometry leem1210(int spring) {
	static const double dia[3] = {0.44e-2, 0.45e-2, 0.46e-2};
	static const double turns[3] = {303.0, 280.0, 351.0};
	int i = std::max(0, std::min(2, spring));
	Geometry g = {"Leem KA-1210", 0.5 * dia[i], 0.5 * 0.035e-2, turns[i], 16.3e-2, kSteelE, kSteelRho};
	return g;
}

/** Olson X-82 small tank, two springs (Parker & Bilbao Table 1): helix length 6.5 cm, helix
    diameter 0.54 / 0.61 cm, 148 / 133 turns, wire diameter 0.035 cm. */
inline Geometry olsonX82(int spring) {
	static const double dia[2] = {0.54e-2, 0.61e-2};
	static const double turns[2] = {148.0, 133.0};
	int i = std::max(0, std::min(1, spring));
	Geometry g = {"Olson X-82", 0.5 * dia[i], 0.5 * 0.035e-2, turns[i], 6.5e-2, kSteelE, kSteelRho};
	return g;
}

// ------------------------------------------------------------------------------------
// The dispersion relation.
// ------------------------------------------------------------------------------------

/** Angular frequency of the audio-band mode at wavenumber beta (rad/m). The root is taken as
    2P / (S + sqrt(S^2 - 4P)), which does not cancel at small beta. */
inline double omega(const Geometry& g, double beta) {
	double c2 = g.youngs / g.density, k = g.curvature(), r = g.wireRadius;
	double q = beta * beta - k * k;
	double bend = c2 * r * r * 0.25 * q * q;
	double S = c2 * beta * beta + bend + c2 * k * k;
	double P = c2 * beta * beta * bend;
	double disc = S * S - 4.0 * P;
	if (disc < 0.0) disc = 0.0;
	return std::sqrt(2.0 * P / (S + std::sqrt(disc)));
}

/** d omega / d beta, by a centred difference of relative step 1e-5. */
inline double groupVelocity(const Geometry& g, double beta) {
	double h = beta * 1e-5;
	return (omega(g, beta + h) - omega(g, beta - h)) / (2.0 * h);
}

/** The beta at which the LF branch peaks (group velocity zero): scan up from small beta to
    the sign change, then bisect. */
inline double betaCritical(const Geometry& g) {
	double k = g.curvature();
	double lo = k * 1e-3, hi = k;
	for (int i = 1; i < 2000; i++) {
		double b = k * (double) i / 2000.0;
		if (groupVelocity(g, b) <= 0.0) { hi = b; break; }
		lo = b;
	}
	for (int i = 0; i < 80; i++) {
		double m = 0.5 * (lo + hi);
		if (groupVelocity(g, m) <= 0.0) hi = m; else lo = m;
	}
	return lo;
}

/** The transition frequency Fc: the top of the LF branch, Hz. */
inline double transitionHz(const Geometry& g) {
	return omega(g, betaCritical(g)) / (2.0 * kPi);
}

/** One-way delay of the LF branch at f Hz (f below Fc), seconds. */
inline double delayLF(const Geometry& g, double f) {
	double w = 2.0 * kPi * f, a = g.curvature() * 1e-6, b = betaCritical(g);
	for (int i = 0; i < 100; i++) {
		double m = 0.5 * (a + b);
		if (omega(g, m) < w) a = m; else b = m;
	}
	return g.wireLength() / groupVelocity(g, 0.5 * (a + b));
}

/** One-way delay of the HF branch (beta above the curvature), seconds. */
inline double delayHF(const Geometry& g, double f) {
	double w = 2.0 * kPi * f, k = g.curvature(), a = k * 1.0001, b = k * 400.0;
	for (int i = 0; i < 100; i++) {
		double m = 0.5 * (a + b);
		if (omega(g, m) < w) a = m; else b = m;
	}
	return g.wireLength() / groupVelocity(g, 0.5 * (a + b));
}

/** Parker & Bilbao's closed forms (2) and (4), the oracle the numerics are tested against.
    TD is the time between echoes at the output (two traversals); FC the transition. */
inline double closedFormTD(const Geometry& g) {
	return 4.0 * g.wireLength() * g.helixRadius / (g.wireRadius * g.wave());
}
inline double closedFormFc(const Geometry& g) {
	return 3.0 * g.wireRadius * g.wave() / (16.0 * std::sqrt(5.0) * kPi * g.helixRadius * g.helixRadius);
}

// ------------------------------------------------------------------------------------
// Fitting a cascade of first-order allpasses to a group delay.
// ------------------------------------------------------------------------------------

/** Group delay in samples of (a + z^-1) / (1 + a z^-1) at w rad/sample (Valimaki et al.;
    Parker 2011, eq. (5)). */
inline double apDelay(double a, double w) {
	return (1.0 - a * a) / (1.0 + 2.0 * a * std::cos(w) + a * a);
}

struct Design {
	double fsInt = 0.0;             // the loop's sample rate, Hz
	int n0 = 0;                     // the pure delay of one traversal, samples
	std::vector<double> a;          // the allpass coefficients of one traversal
	double fHi = 0.0;               // top of the fitted band, Hz
	double tauLow = 0.0;            // the physical one-way delay at the bottom of the band, s
	double maxRel = 0.0, rmsRel = 0.0;   // fit error against the target, relative

	/** Total group delay of one traversal at w rad/sample, samples. */
	double delaySamples(double w) const {
		double d = (double) n0;
		for (size_t i = 0; i < a.size(); i++) d += apDelay(a[i], w);
		return d;
	}
};

/** Fit n0 pure-delay samples and M allpass sections so that the delay of one traversal follows
    tau(f) (seconds) at the listed frequencies, weighting by relative error.

    The first pass is a non-negative least-squares over a grid of candidate coefficients with a
    linear penalty on the number of sections, bisected until about M are asked for (coordinate
    descent); the sections are then placed at the quantiles of that density and every one is
    re-chosen against the others from the grid until nothing moves. */
inline Design fitChain(double fsInt, const std::vector<double>& fHz, const std::vector<double>& tauSec,
                       int M, int nMin, bool allowNeg) {
	const int nf = (int) fHz.size();
	std::vector<double> om(nf), T(nf), w(nf);
	for (int i = 0; i < nf; i++) {
		om[i] = 2.0 * kPi * fHz[i] / fsInt;
		T[i] = tauSec[i] * fsInt;
		w[i] = 1.0 / T[i];
	}
	// Candidate coefficients, tight near +-1 (narrow, tall delay peaks) down to 0.3.
	std::vector<double> atoms;
	for (int i = 0; i < 40; i++)
		atoms.push_back(1.0 - std::pow(10.0, -3.4 + 3.25 * (double) i / 39.0));
	if (allowNeg)
		for (int i = 0; i < 28; i++)
			atoms.push_back(-(1.0 - std::pow(10.0, -2.2 + 2.05 * (double) i / 27.0)));
	const int ng = (int) atoms.size();
	std::vector<std::vector<double> > B(ng, std::vector<double>(nf));
	std::vector<double> Hd(ng);
	for (int j = 0; j < ng; j++) {
		double h = 0.0;
		for (int i = 0; i < nf; i++) {
			B[j][i] = apDelay(atoms[j], om[i]);
			h += w[i] * w[i] * B[j][i] * B[j][i];
		}
		Hd[j] = h + 1e-30;
	}
	double H0 = 0.0;
	for (int i = 0; i < nf; i++) H0 += w[i] * w[i];

	std::vector<double> n(ng, 0.0), r(nf);
	double n0 = T[0];
	for (int i = 0; i < nf; i++) r[i] = w[i] * (n0 - T[i]);
	const double nMinD = (double) nMin;
	std::function<void(int, double)> sweeps = [&](int count, double lam) {
		for (int s = 0; s < count; s++) {
			double g0 = 0.0;
			for (int i = 0; i < nf; i++) g0 += r[i] * w[i];
			double nn0 = std::max(nMinD, n0 - g0 / H0);
			double d0 = nn0 - n0;
			if (d0 != 0.0) { for (int i = 0; i < nf; i++) r[i] += w[i] * d0; n0 = nn0; }
			for (int j = 0; j < ng; j++) {
				double gr = 0.0;
				for (int i = 0; i < nf; i++) gr += r[i] * w[i] * B[j][i];
				double nj = std::max(0.0, n[j] - (gr + lam) / Hd[j]);
				double d = nj - n[j];
				if (d != 0.0) { for (int i = 0; i < nf; i++) r[i] += w[i] * B[j][i] * d; n[j] = nj; }
			}
		}
	};
	double lo = -14.0, hi = -2.0;                     // log10 of the penalty
	for (int it = 0; it < 12; it++) {
		double mid = 0.5 * (lo + hi);
		sweeps(300, std::pow(10.0, mid));
		double tot = 0.0;
		for (int j = 0; j < ng; j++) tot += n[j];
		if (tot > (double) M) lo = mid; else hi = mid;
	}
	sweeps(600, std::pow(10.0, hi));
	double tot = 0.0;
	for (int j = 0; j < ng; j++) tot += n[j];

	// Quantiles of the density, in order of coefficient.
	std::vector<int> order(ng);
	for (int j = 0; j < ng; j++) order[j] = j;
	std::sort(order.begin(), order.end(), [&](int x, int y) { return atoms[x] < atoms[y]; });
	std::vector<int> pick(M, order[ng - 1]);
	if (tot > 0.0) {
		double cum = 0.0;
		int k = 0;
		for (int q = 0; q < ng && k < M; q++) {
			cum += n[order[q]];
			while (k < M && cum >= (k + 0.5) * tot / (double) M) { pick[k] = order[q]; k++; }
		}
	}

	// Re-choose every section against the rest, and n0 against all of them.
	std::vector<double> m(nf, 0.0);
	for (int k = 0; k < M; k++)
		for (int i = 0; i < nf; i++) m[i] += B[pick[k]][i];
	int n0i = std::max(nMin, (int) std::lround(n0));
	for (int pass = 0; pass < 12; pass++) {
		bool moved = false;
		for (int k = 0; k < M; k++) {
			for (int i = 0; i < nf; i++) m[i] -= B[pick[k]][i];
			double best = 1e300;
			int bj = pick[k];
			for (int j = 0; j < ng; j++) {
				double e = 0.0;
				for (int i = 0; i < nf; i++) {
					double x = w[i] * ((double) n0i + m[i] + B[j][i] - T[i]);
					e += x * x;
				}
				if (e < best - 1e-18) { best = e; bj = j; }
			}
			if (bj != pick[k]) moved = true;
			pick[k] = bj;
			for (int i = 0; i < nf; i++) m[i] += B[pick[k]][i];
		}
		double num = 0.0;
		for (int i = 0; i < nf; i++) num += w[i] * w[i] * (T[i] - m[i]);
		int nn = std::max(nMin, (int) std::lround(num / H0));
		if (nn != n0i) { n0i = nn; moved = true; }
		if (!moved) break;
	}

	Design d;
	d.fsInt = fsInt;
	d.n0 = n0i;
	for (int k = 0; k < M; k++) d.a.push_back(atoms[pick[k]]);
	d.fHi = fHz[nf - 1];
	d.tauLow = tauSec[0];
	double mx = 0.0, ss = 0.0;
	for (int i = 0; i < nf; i++) {
		double e = std::fabs(d.delaySamples(om[i]) - T[i]) / T[i];
		mx = std::max(mx, e);
		ss += e * e;
	}
	d.maxRel = mx;
	d.rmsRel = std::sqrt(ss / (double) nf);
	return d;
}

/** How many allpass sections each branch of one spring gets. These set the CPU and the fit's
    accuracy (the fit error is in Design and is printed by the test). */
static const int kSectionsLF = 140;
static const int kSectionsHF = 36;
/** LF loop rate as a multiple of the spring's Fc (Nyquist 3 % above Fc). */
static const double kLfRateOverFc = 2.06;
/** HF loop rate, Hz. */
static const double kHfRate = 22050.0;
/** The LF fit stops where the one-way delay reaches this multiple of its low-frequency
    value; above it the LF branch is cut off by a lowpass in the loop (the real response piles
    up at Fc, where the group velocity goes to zero, and nothing finite follows it there). */
static const double kLfDelayCap = 2.0;

struct SpringDesign {
	Geometry geom;
	double fc = 0.0;                // Hz
	double td = 0.0;                // s, 2 L / v0 from the relation at low frequency
	Design lf, hf;
	double lfCutHz = 0.0;           // where the LF branch is cut off
};

inline SpringDesign designSpring(const Geometry& g) {
	SpringDesign s;
	s.geom = g;
	s.fc = transitionHz(g);
	double t0 = delayLF(g, 20.0);
	s.td = 2.0 * t0;
	// LF: from 20 Hz to where the delay has doubled.
	double fmax = 20.0;
	for (double f = 20.0; f < s.fc; f *= 1.004) {
		if (delayLF(g, f) > kLfDelayCap * t0) break;
		fmax = f;
	}
	s.lfCutHz = fmax;
	std::vector<double> f, t;
	const int nf = 120;
	for (int i = 0; i < nf; i++) {
		double u = (double) i / (double) (nf - 1);
		double fi = 20.0 + (fmax - 20.0) * u;
		f.push_back(fi);
		t.push_back(delayLF(g, fi));
	}
	s.lf = fitChain(kLfRateOverFc * s.fc, f, t, kSectionsLF, 8, true);
	// HF: from 50 Hz to 9.5 kHz.
	f.clear(); t.clear();
	for (int i = 0; i < nf; i++) {
		double u = (double) i / (double) (nf - 1);
		double fi = 50.0 * std::pow(9500.0 / 50.0, u);
		f.push_back(fi);
		t.push_back(delayHF(g, fi));
	}
	s.hf = fitChain(kHfRate, f, t, kSectionsHF, 8, true);
	return s;
}

// ------------------------------------------------------------------------------------
// Runtime pieces.
// ------------------------------------------------------------------------------------

struct Biquad {
	double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
	void lowpass(double fc, double q, double fs) {
		double w0 = 2.0 * kPi * std::min(fc, 0.49 * fs) / fs, al = std::sin(w0) / (2.0 * q), cs = std::cos(w0);
		double a0 = 1.0 + al;
		b0 = (1.0 - cs) * 0.5 / a0; b1 = (1.0 - cs) / a0; b2 = b0;
		a1 = -2.0 * cs / a0; a2 = (1.0 - al) / a0;
	}
	double process(double x) {
		double y = b0 * x + z1;
		z1 = b1 * x - a1 * y + z2;
		z2 = b2 * x - a2 * y;
		return y;
	}
	void clear() { z1 = z2 = 0.0; }
};

/** Windowed-sinc kernel, 6 lobes each side, Kaiser beta 8, tabulated over the window. */
struct Kernel {
	static const int HW = 6;
	static const int RES = 512;
	std::vector<double> tab;       // h(v) for v = 0..1 over the window (sinc argument v * HW)
	Kernel() : tab(RES + 2) {
		const double beta = 8.0;
		for (int i = 0; i < RES + 2; i++) {
			double v = (double) i / (double) RES;
			double x = v * HW;
			double s = x < 1e-12 ? 1.0 : std::sin(kPi * x) / (kPi * x);
			double arg = beta * std::sqrt(std::max(0.0, 1.0 - v * v));
			tab[i] = s * bessel0(arg) / bessel0(beta);
		}
	}
	static double bessel0(double x) {
		double sum = 1.0, term = 1.0;
		for (int k = 1; k < 40; k++) { term *= (x * 0.5 / k) * (x * 0.5 / k); sum += term; }
		return sum;
	}
	/** Kernel at u (units of the cutoff-scaled sample), |u| < HW. */
	double at(double u) const {
		double v = std::fabs(u) * (1.0 / HW) * RES;
		int i = (int) v;
		if (i >= RES) return 0.0;
		double f = v - (double) i;
		return tab[i] + f * (tab[i + 1] - tab[i]);
	}
};
inline const Kernel& kernel() { static Kernel k; return k; }

/** Sample-rate converter pair for one loop: host rate -> loop rate, loop rate -> host rate.
    The loop-rate stream sample k is the band-limited input at host time k * q, so the pair is
    a pure delay of `latency()` loop samples end to end, and unity gain in band. */
struct Resampler {
	double q = 1.0;                 // host samples per loop sample
	double cn = 0.96;               // cutoff, as a fraction of the loop Nyquist
	double hsDec = 0.0, hsInt = 0.0;
	std::vector<double> inRing;     // host samples
	std::vector<double> outRing;    // loop samples
	int64_t nIn = 0;                // host samples pushed
	int64_t nOut = 0;               // loop samples produced
	double nextT = 0.0;             // host time of the next loop sample
	int64_t hostIdx = 0;
	int maskIn = 0, maskOut = 0;

	void setup(double hostRate, double loopRate) {
		q = hostRate / loopRate;
		double cd = std::min(1.0, cn / q);                 // decimator cutoff vs host Nyquist
		hsDec = Kernel::HW / cd;
		hsInt = Kernel::HW / cn;
		int sIn = 1; while (sIn < (int) (2.0 * hsDec + q + 8.0)) sIn <<= 1;
		int sOut = 1; while (sOut < (int) (2.0 * hsInt + 8.0)) sOut <<= 1;
		inRing.assign(sIn, 0.0); outRing.assign(sOut, 0.0);
		maskIn = sIn - 1; maskOut = sOut - 1;
		clear();
		(void) kernel();
	}
	void clear() {
		std::fill(inRing.begin(), inRing.end(), 0.0);
		std::fill(outRing.begin(), outRing.end(), 0.0);
		nIn = 0; nOut = 0; nextT = 0.0; hostIdx = 0;
	}
	/** Loop samples of delay from the host input to the host output. */
	double latency() const { return 2.0 * hsInt + 1.0; }

	/** Push a host sample. Returns true, and the loop sample in `out`, when one is due. */
	bool push(double x, double& out) {
		inRing[nIn & maskIn] = x;
		int64_t n = nIn++;
		if (std::floor(nextT + hsDec) > (double) n) return false;
		const Kernel& K = kernel();
		double cd = std::min(1.0, cn / q);
		int64_t lo = (int64_t) std::ceil(nextT - hsDec), hi = (int64_t) std::floor(nextT + hsDec);
		double acc = 0.0;
		for (int64_t k = lo; k <= hi; k++) {
			if (k < 0) continue;
			double u = ((double) k - nextT) * cd;           // in kernel units
			acc += inRing[k & maskIn] * K.at(u);
		}
		out = acc * cd;
		nextT += q;
		return true;
	}
	void pushLoop(double y) { outRing[nOut & maskOut] = y; nOut++; }
	/** One host-rate output sample. Call once per host sample, after push() / pushLoop(). */
	double pull() {
		const Kernel& K = kernel();
		double t = (double) hostIdx / q - latency();
		hostIdx++;
		int64_t lo = (int64_t) std::ceil(t - hsInt), hi = (int64_t) std::floor(t + hsInt);
		double acc = 0.0;
		for (int64_t k = lo; k <= hi; k++) {
			if (k < 0 || k >= nOut) continue;
			acc += outRing[k & maskOut] * K.at(((double) k - t) * cn);
		}
		return acc * cn;
	}
};

struct Chain {
	std::vector<double> a, s;
	void set(const std::vector<double>& coef) { a = coef; s.assign(a.size(), 0.0); }
	void clear() { std::fill(s.begin(), s.end(), 0.0); }
	double process(double x) {
		for (size_t i = 0; i < a.size(); i++) {
			double y = a[i] * x + s[i];
			s[i] = x - a[i] * y;
			x = y;
		}
		return x;
	}
};

struct DelayBuf {
	std::vector<double> b;
	int n = 1, pos = 0;
	void set(int len) { n = std::max(1, len); b.assign(n, 0.0); pos = 0; }
	void clear() { std::fill(b.begin(), b.end(), 0.0); pos = 0; }
	/** Push x and return the sample pushed n calls ago. */
	double process(double x) {
		double y = b[pos];
		b[pos] = x;
		if (++pos >= n) pos = 0;
		return y;
	}
};

/** One branch of one spring: the waveguide loop at its own rate, behind its converters.
        z = x + g * lp( D( D(z) ) ),  y = D(z)
    D is a pure delay then the allpass chain. y is the signal at the pickup end. */
struct Loop {
	const Design* d = nullptr;
	Resampler rs;
	DelayBuf dl1, dl2;
	Chain c1, c2;
	Biquad cut[2];                  // fixed band edge of the branch
	double tone = 0.0, toneState = 0.0;   // one-pole loss, per loop sample
	double g = 0.0;                 // gain per round trip
	double fb = 0.0;

	void setup(const Design& dsn, double hostRate, double cutHz, bool lowpassBand) {
		d = &dsn;
		rs.setup(hostRate, dsn.fsInt);
		dl1.set(dsn.n0); dl2.set(dsn.n0);
		c1.set(dsn.a); c2.set(dsn.a);
		cut[0].lowpass(cutHz, 0.5412, dsn.fsInt);
		cut[1].lowpass(cutHz, 1.3066, dsn.fsInt);
		(void) lowpassBand;
		clear();
	}
	void clear() {
		rs.clear(); dl1.clear(); dl2.clear(); c1.clear(); c2.clear();
		cut[0].clear(); cut[1].clear();
		toneState = 0.0; fb = 0.0;
	}
	/** One round trip, seconds, at the low-frequency end of the band. */
	double roundTrip() const {
		return 2.0 * (d->delaySamples(0.0)) / d->fsInt;
	}
	void setLoss(double rt60, double toneHz) {
		g = std::pow(10.0, -3.0 * roundTrip() / std::max(rt60, 1e-3));
		tone = 1.0 - std::exp(-2.0 * kPi * toneHz / d->fsInt);
	}
	/** Host-rate in, host-rate out (the pickup end). */
	double process(double x) {
		double xl;
		if (rs.push(x, xl)) {
			double z = xl + g * fb;
			double y = c1.process(dl1.process(z));
			double f = c2.process(dl2.process(y));
			f = cut[1].process(cut[0].process(f));
			toneState += tone * (f - toneState);
			fb = toneState;
			rs.pushLoop(y);
		}
		return rs.pull();
	}
};

/** A tank: springs in parallel, each with its LF and HF series, driven by the same velocity
    and summed at the pickup. */
struct Tank {
	static const int MAX_SPRINGS = 3;
	int count = 0;
	Loop lf[MAX_SPRINGS], hf[MAX_SPRINGS];
	double hfWeight = 0.3;

	void setup(const SpringDesign* designs, int n, double hostRate) {
		count = std::min(n, (int) MAX_SPRINGS);
		for (int i = 0; i < count; i++) {
			lf[i].setup(designs[i].lf, hostRate, designs[i].lfCutHz, true);
			hf[i].setup(designs[i].hf, hostRate, 0.45 * designs[i].hf.fsInt, false);
		}
	}
	void clear() {
		for (int i = 0; i < count; i++) { lf[i].clear(); hf[i].clear(); }
	}
	void setLoss(double rt60, double toneHz) {
		for (int i = 0; i < count; i++) {
			lf[i].setLoss(rt60, toneHz);
			hf[i].setLoss(rt60, toneHz * 2.0);
		}
	}
	double process(double x) {
		double y = 0.0;
		for (int i = 0; i < count; i++)
			y += lf[i].process(x) + hfWeight * hf[i].process(x);
		return y;
	}
};

/** The two tanks, designed once per process on first use (the fit is a one-off cost of a
    few hundred milliseconds). */
struct TankPreset {
	const char* name;
	int count;
	SpringDesign spring[Tank::MAX_SPRINGS];
};
struct TankPresets {
	TankPreset leem, olson;
	TankPresets() {
		leem.name = "Leem Pro KA-1210 (3 springs)";
		leem.count = 3;
		for (int i = 0; i < 3; i++) leem.spring[i] = designSpring(leem1210(i));
		olson.name = "Olson X-82 (2 springs)";
		olson.count = 2;
		for (int i = 0; i < 2; i++) olson.spring[i] = designSpring(olsonX82(i));
	}
};
inline const TankPreset& tankPreset(int id) {
	static TankPresets p;                 // C++11: initialised once, thread-safely
	return id == 1 ? p.olson : p.leem;
}

} // namespace springtank

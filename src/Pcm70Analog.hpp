// Pcm70Analog.hpp: everything between the Rack jacks and the digital core of the PCM 70 that is not firmware: the converter filters (as the machine's
// real, complex analog responses), the host-rate <-> machine-rate conversion they are built into, the level detector that feeds the firmware's
// HEADRM port, the wet/dry mix stage, and a DC blocker. Research steps 31-33 and 45-48.
//
// The machine runs at exactly 13 MHz / 384 = 33854.1667 Hz whatever the host rate is. The input anti-alias filter (a 9-pole elliptic, fp 14.48 kHz, 49 dB)
// sits in front of the converter and the output chain (DAC hold x the same elliptic x an aperture-correction biquad, f0 12.82 kHz Q 0.89) behind it, and both
// must be the true COMPLEX responses, not a magnitude EQ: feedback-filter programs have wrong echo decay without them (Step 47). So each conversion is one
// polyphase FIR whose kernel is the analog filter's impulse response, band-limited to the host Nyquist rate and tabulated at 128 fractional positions
// (linearly interpolated). The input one is "the analog filter, then an ideal sampler at 33854 Hz" (content above 16.9 kHz aliases exactly as far as the real
// filter lets it); the output one is "the DAC's held samples through the analog chain, sampled at the host rate" (the DAC images are there, 49 dB down).
// Positions are exact rationals, so there is no drift between the machine clock and the host clock.
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cmath>
#include <complex>
#include <vector>
#include <algorithm>

namespace pcm70 {
namespace analog {

static const double FS = 13e6 / 384.0;                 // machine sample rate, 33854.1667 Hz
static const double PI = 3.14159265358979323846;
typedef std::complex<double> cd;

// ---- the analog transfer functions ----------------------------------------------------------------------------------------------------------
// 9th-order elliptic low-pass, fp 14480 Hz, Rp 0.01 dB, As 49 dB (fit to 40 recordings, Steps 33/46): zeros and poles of the analog prototype.
inline cd elliptic(double f) {
    static const double zi[4] = { 210312.32551023568, 127034.40259451576, 106731.55321229747, 100841.66805866927 };
    static const double pr[5] = { -62852.8436897716, -45731.63597715775, -21645.832774091283, -8377.696729231377, -2123.1963010021454 };
    static const double pi_[5] = { 0.0, 57109.86847596344, 82738.7487514054, 91020.02235925772, 93443.49410111339 };
    static const double K = 2172.236427180176;
    const cd s(0.0, 2 * PI * f);
    cd num(K, 0.0), den(1.0, 0.0);
    for (int i = 0; i < 4; i++) num *= (s - cd(0.0, zi[i])) * (s + cd(0.0, zi[i]));
    den *= (s - cd(pr[0], 0.0));
    for (int i = 1; i < 5; i++) den *= (s - cd(pr[i], pi_[i])) * (s - cd(pr[i], -pi_[i]));
    return num / den;
}
inline cd aperture(double f) {                         // aperture-correction biquad (U8), peaked: f0 12821 Hz, Q 0.89
    const double w0 = 2 * PI * 12821.0, Q = 0.89; const cd s(0.0, 2 * PI * f);
    return 1.0 / (1.0 + s / (Q * w0) + (s / w0) * (s / w0));
}
inline double sinc(double x) { return std::fabs(x) < 1e-12 ? 1.0 : std::sin(PI * x) / (PI * x); }
inline cd hIn(double f) { return elliptic(f); }
inline cd hOut(double f) { return elliptic(f) * sinc(f / FS) * aperture(f); }       // DAC hold magnitude (its half-sample delay dropped)

// ---- FFT -------------------------------------------------------------------------------------------------------------------------------------------
inline void fft(std::vector<cd>& a) {                  // in place, forward-sign +j (an inverse transform up to scale), size a power of two
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; i++) { size_t bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap(a[i], a[j]); }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = 2 * PI / (double)len; const cd wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) { cd w(1.0, 0.0); for (size_t k = 0; k < len / 2; k++) { cd u = a[i + k], v = a[i + k + len / 2] * w; a[i + k] = u + v; a[i + k + len / 2] = u - v; w *= wl; } }
    }
}

// ---- polyphase kernel ------------------------------------------------------------------------------------------------------------------------------
// y(t) = sum_k x[k] g(t - k/fsrc) for a source sampled at fsrc, g = Tsrc * integral of H(f) e^{j2 pi f tau} over |f| < fband/2.
// Row p holds g((p/PH + j)/fsrc) for the source samples k = k0-B .. k0+A (ascending), where k0 = floor(t * fsrc) and p/PH = frac(t * fsrc).
struct Polyphase {
    int A = 0, B = 0, taps = 0; static const int PH = 128;
    std::vector<float> tab;
    template <class F> void build(F H, double fsrc, double fband, int pre, int post) {
        A = pre; B = post; taps = A + B + 1;
        const double Ts = 1.0 / fsrc;
        size_t N = 1; while ((double)N < (double)PH * fsrc * 12e-3 || N < (size_t)PH * (size_t)(taps + 16)) N <<= 1;     // wrap time >= 12 ms
        const double dtau = Ts / PH, df = 1.0 / ((double)N * dtau);
        std::vector<cd> S(N, cd(0.0, 0.0));
        for (size_t k = 0; k <= N / 2; k++) {
            const double f = (double)k * df; if (f > fband / 2) break;
            const cd v = H(f) * df * Ts;
            S[k] += v; if (k) S[N - k] += std::conj(v);                       // negative frequencies: the conjugate (DC counted once)
        }
        fft(S);                                                               // g(m dtau) = Re sum_k S[k] e^{+j2 pi k m / N} (conjugate-symmetric input -> real)
        tab.assign((size_t)(PH + 1) * taps, 0.0f);
        for (int p = 0; p <= PH; p++) for (int i = 0; i < taps; i++) {
            const int j = B - i;                                              // ascending k <-> descending j
            long m = (long)p + (long)j * PH; m %= (long)N; if (m < 0) m += (long)N;
            tab[(size_t)p * taps + i] = (float)S[(size_t)m].real();
        }
    }
    double eval(const double* x, double frac) const {                        // x -> k0-B (contiguous, ascending)
        const double pf = frac * PH; int p = (int)pf; if (p >= PH) p = PH - 1; const double a = pf - p;
        const float* r0 = &tab[(size_t)p * taps]; const float* r1 = r0 + taps;
        double d0 = 0.0, d1 = 0.0;
        for (int i = 0; i < taps; i++) { d0 += x[i] * (double)r0[i]; d1 += x[i] * (double)r1[i]; }
        return d0 + a * (d1 - d0);
    }
};

// A history of samples that can be read as one contiguous window.
struct Ring {
    std::vector<double> buf; size_t cap = 0, mask = 0; long long count = 0;      // count = samples pushed (including the zero preload)
    void init(size_t minCap, long long preload) { cap = 1; while (cap < minCap) cap <<= 1; mask = cap - 1; buf.assign(2 * cap, 0.0); count = 0; for (long long i = 0; i < preload; i++) push(0.0); }
    void push(double v) { const size_t i = (size_t)count & mask; buf[i] = v; buf[i + cap] = v; count++; }
    const double* window(long long start) const { return &buf[(size_t)start & mask]; }
};

inline long long gcdll(long long a, long long b) { while (b) { long long t = a % b; a = b; b = t; } return a; }

// ---- host -> machine rate with the input anti-alias filter ----------------------------------------------------------------------------------------
struct InputStage {
    Polyphase pp; Ring ring; long long P = 1, Q = 1, n = 0, hostBase = 0; int A = 0, B = 0;
    void init(double hostRate) {
        const long long fh = std::llround(hostRate);
        const long long num = fh * 384, g = gcdll(num, 13000000LL); P = num / g; Q = 13000000LL / g; n = 0; hostBase = 0;
        const double fhd = (double)fh; A = std::max(8, (int)std::ceil(0.25e-3 * fhd)); B = (int)std::ceil(5e-3 * fhd);
        pp.build([](double f) { return hIn(f); }, fhd, fhd, A, B);
        ring.init((size_t)(A + B + 1) * 2, B);                               // B zeros before the first sample: host index k lives at ring position k + B
    }
    void push(double x) { ring.push(x); }
    // produce the next machine-rate sample if the host samples it needs have arrived
    bool pull(double& out) {
        const long long num = n * P, k0 = hostBase + num / Q, rem = num % Q;
        if (ring.count - B <= k0 + A) return false;                           // host samples up to k0 + A must exist
        out = pp.eval(ring.window(k0), (double)rem / (double)Q);
        if (++n >= Q) { n -= Q; hostBase += P; }
        return true;
    }
};

// ---- machine -> host rate with the DAC hold, output elliptic and aperture stage -----------------------------------------------------------------
struct OutputStage {
    Polyphase pp; Ring ring[2]; long long P = 1, Q = 1, v = 0, base = 0; int A = 0, B = 0; long long delayHs = 0;
    int latencyHostSamples() const { return (int)delayHs; }
    void init(double hostRate, long long delayHostSamples) {
        const long long fh = std::llround(hostRate);
        const long long num = fh * 384, g = gcdll(num, 13000000LL); P = num / g; Q = 13000000LL / g; v = 0; base = 0; delayHs = delayHostSamples;
        A = 6; B = (int)std::ceil(5e-3 * FS);
        pp.build([](double f) { return hOut(f); }, FS, (double)fh, A, B);
        for (int c = 0; c < 2; c++) ring[c].init((size_t)(A + B + 1) * 2, B);
    }
    void push(double l, double r) { ring[0].push(l); ring[1].push(r); }
    // the host output sample for host index j = v + delayHs (v counts from 0); false if the machine has not produced enough yet
    bool pull(double& l, double& r) {
        const long long num = v * Q, k0 = base + num / P, rem = num % P;
        if (ring[0].count - B <= k0 + A) return false;
        const double fr = (double)rem / (double)P;
        l = pp.eval(ring[0].window(k0), fr); r = pp.eval(ring[1].window(k0), fr);
        if (++v >= P) { v -= P; base += Q; }
        return true;
    }
};

// ---- the level detector that feeds the firmware's HEADRM port (Step 45) ---------------------------------------------------------------------------
// ANIN/4 -> positive-peak hold on C42 (22 uF) with R22 (10 M to -15 V) discharging it at 68 mV/s; the ADC0804's full scale is 1.58 V and the hold voltage at digital
// full scale is 2.5 V, so code = 403.5 x the digital peak (clamped to 255). The slave's gate logic works on these absolute codes.
struct LevelDetector {
    double peak = 0.0;                                     // digital units (1.0 = converter full scale)
    static constexpr double CODE_PER_UNIT = 403.5;
    uint8_t update(double x) {
        const double droop = (15.0 + 2.5 * peak) / (1e7 * 22e-6) / 2.5 / FS;
        peak = std::max(std::max(0.0, x), std::max(0.0, peak - droop));   // positive peak (the diode as drawn); attack is ideal
        return (uint8_t)std::min(255.0, peak * CODE_PER_UNIT);
    }
    double holdVolts() const { return 2.5 * peak; }
    int leds() const {                                     // LM3915 bar, 0 / -6 / -12 / -18 / -24 dB re 2.76 V; returns how many are lit (0-5)
        const double v = holdVolts();
        for (int i = 0; i < 5; i++) if (v >= 2.76 * std::pow(10.0, -6.0 * i / 20.0)) return 5 - i;     // i = 0: 0 dB (overload) lights all five
        return 0;
    }
};

// ---- wet / dry mix (service manual 4.1.6-4.1.7) ----------------------------------------------------------------------------------------------------
// The master writes two 8-bit codes to U22 (attenuation, 0.392 dB per LSB); the control voltages are smoothed by the VCA control nodes (tau 5.4 ms), so gain
// changes are dB-linear ramps. Dry equals wet at equal codes within 0.5 dB: kDry = 1.05 (digital full scale in = digital full scale out). Run at host rate.
struct MixStage {
    double kDry = 1.05, tau = 0.0054, sw = 255.0, sd = 255.0; int cw = 255, cd = 255; double a = 0.0;
    void init(double hostRate) { a = 1.0 - std::exp(-1.0 / (tau * hostRate)); }
    static double gainOf(double code) { return std::pow(10.0, -0.392 * code / 20.0); }
    void setCodes(int wet, int dry) { cw = wet; cd = dry; }
    void process(double dryIn, double wetL, double wetR, double& outL, double& outR) {
        sw += a * (cw - sw); sd += a * (cd - sd);
        const double gw = gainOf(sw), gd = gainOf(sd);
        outL = wetL * gw + dryIn * kDry * gd; outR = wetR * gw + dryIn * kDry * gd;
    }
};

// ---- DC blocker -----------------------------------------------------------------------------------------------------------------------------------
// The real unit is AC coupled; the corner is not measured (it only has to be far below the audio band and above anything a unity-gain hold state can pile up).
struct DcBlock {
    double x1 = 0.0, y1 = 0.0, R = 0.0;
    void init(double fc = 3.0) { R = 1.0 - 2 * PI * fc / FS; }
    double process(double x) { const double y = x - x1 + R * y1; x1 = x; y1 = y; return y; }
};

}
}

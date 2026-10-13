// The PT2399, as the datasheet draws it: a 1-bit adaptive delta modulator and
// demodulator around 44 kbit of RAM, clocked by a VCO set by the resistance on
// pin 6. Shared by Amortization (three of them, as the Verbtronic wires them)
// and Racketeer (one, abused). Works in volts, whatever the sample rate; nothing
// from Rack, so tests/ drives it bare.
#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace pt2399 {

// What is not in the datasheet. The leak time constant is (4.7k inside the chip,
// 0.047u across pins 9-10 and 11-12). The step sizes and the syllabic filter are
// set so the model meets the datasheet's THD, output-swing and gain figures --
// see tests/Amortization -- which does not determine them uniquely.
namespace assumed {
    static const double ADM_VS_MIN = 0.15;      // volts, the un-adapted step
    static const double ADM_VS_MAX = 5.0;       // volts, the fully adapted step (the integrator still clips at CHIP_CLIP)
    static const double ADM_TAU_SYL = 0.1e-3;   // seconds, run-length filter
    // The chip's op-amps and modulator run from 5 V around a 2.5 V reference.
    static const double CHIP_CLIP = 2.4;
    // Sign of the demodulator relative to the modulator's input (+1: the delayed
    // copy has the same polarity). Not determinable from the datasheet.
    static const double CHIP_POLARITY = 1.0;
    // Pin 2 (REF) and the VCO. Both the datasheet and the measurements say the
    // delay-control pin (6) is a current source held at REF, so the VCO current
    // is REF / R and the clock follows REF. How much of the period that current
    // sets is not on record at small R: the measured law (below) has a 29.7 ms
    // intercept, and the part of it that is the chip's own series resistance in
    // the pin-6 path (Valve Wizard draws "1k?", speculative) is the part REF
    // can move. 1 kohm is the smallest value for which the documented pin-2
    // modulation (REF 2.5 V -> 2 V, pin 6 grounded) moves the delay by
    // milliseconds, and it is the only number anyone has drawn. ASSUMED.
    static const double PIN6_INT_OHMS = 1000.0;
    // The REF at which the delay law below was measured (5 V supply).
    static const double LAW_VREF = 2.5;
}

/** A PT2399. Per the datasheet's block diagram: LPF1 into a comparator, a 1-bit
    modulator whose integrator is OP1 (4.7k inside, 0.047u across pins 9-10), 44
    kbit of RAM, and a demodulator whose integrator is OP2 (pins 11-12). The
    clock is a VCO set by the resistance on pin 6.

    The delay is the RAM's length divided by the bit rate, so it is the clock --
    not a buffer length -- that sets it: this module reads and writes the RAM one
    bit per clock, and a change of clock changes the rate the stored bits are
    played back at. That is what a mode change does to whatever is ringing.

    LPF1 and LPF2 are the circuit's, outside the chip; this is what is inside:
    the comparator and its two integrators. */
struct Pt2399 {
    static const int kBits = 44000;            // "44K Bits RAM"
    // Delay against the pin-6 resistance, measured (Electric Druid, "Useful design
    // equations for the PT2399"): ms = 11.46 * kohm + 29.70. That fixes the bit
    // rate once the RAM length is known.
    static double delaySecondsFor(double rPin6) { return 1e-3 * (11.46 * rPin6 * 1e-3 + 29.70); }
    // The same law with REF (pin 2) as an input: the part of the delay that is
    // the VCO's ramp (11.46 ms per kohm of pin-6 path) scales as LAW_VREF / REF,
    // because the current that ramps it is REF over that path; the rest does not
    // move. At REF = LAW_VREF this is delaySecondsFor() exactly, whatever
    // PIN6_INT_OHMS is. rPin6 is the external resistance only.
    static double delaySecondsForVref(double rPin6, double vref,
                                      double rInt = assumed::PIN6_INT_OHMS) {
        double ramp = 11.46e-3 * (rPin6 + rInt) * 1e-3;
        double fixed = 29.70e-3 - 11.46e-3 * rInt * 1e-3;
        return fixed + ramp * (assumed::LAW_VREF / vref);
    }
    // Clock frequency, same source: ms = 683.21 / MHz + 0.08.
    static double clockHzFor(double rPin6) { return 683.21 / (delaySecondsFor(rPin6) * 1e3 - 0.08) * 1e6; }

    // The integrators leak into the chip's internal 4.7k against 0.047u.
    static constexpr double kTauInt = 4.7e3 * 47e-9;

    struct Adm {
        float v = 0.f;       // integrator output
        float u = 0.f;       // syllabic (run-length) filter
        uint8_t hist = 0;    // last three bits
    };

    std::vector<uint8_t> ram;
    uint32_t w = 0;
    Adm mod, dem;
    double bitRate = 0.0;
    double tauInt = kTauInt;                    // seconds, set by setIntegrator()
    float alpha = 0.01f, kSyl = 0.01f;
    float vsMin = (float)assumed::ADM_VS_MIN, vsMax = (float)assumed::ADM_VS_MAX;
    float clipV = (float)assumed::CHIP_CLIP;
    double acc = 0.0, fsAudio = 48000.0;
    int m = 0;                                  // bits this audio sample
    float lastOut = 0.f;

    /** Pipelining, for a caller with feedback around the chip (Racketeer):
        demod() then modulate() each sample, the modulator's input depending on
        the demodulator's output. The next sample's demodulator reads bits the
        current modulate() does not touch ([w+m, w+m+m') against [w, w+m)), so
        modulate() can run it in the same loop -- two independent chains
        overlap where two separate loops each wait on their own. It has to
        guess m' and the clock for the next sample; demod() takes the guess
        only if both came true and otherwise discards it and does the work as
        before. Either way the result is bit-identical. Off by default. */
    bool pipeline = false;
    struct Spec {
        bool valid = false;
        Adm dem;
        double sum = 0.0, rate = 0.0;
        int m = 0;
        uint32_t w = 0;
        float alpha = 0.f, kSyl = 0.f, vsMin = 0.f, vsMax = 0.f, clipV = 0.f;
    } spec;
    double lastBeginRate = -1.0;
    bool steady = false;                        // clock unchanged since last begin()

    /** The comparator's input noise, volts rms, drawn fresh for every decision
        (uniform, from a per-chip xorshift). 0 is off and leaves the modulator
        exactly as it was. See Amortization's `assumed::CMP_NOISE_V`. */
    float cmpNoise = 0.f;
    uint32_t noiseSeed = 0x2545F491u;

    Pt2399() { ram.assign(kBits, 0); reset(); }

    void reset() {
        for (int i = 0; i < kBits; i++) ram[i] = (uint8_t)(i & 1);
        w = 0;
        mod = Adm(); dem = Adm();
        acc = 0.0; m = 0; lastOut = 0.f;
        noiseSeed = 0x2545F491u;
        spec.valid = false;
        lastBeginRate = -1.0;
        steady = false;
    }

    void setSampleRate(double fs) { fsAudio = fs; }

    void setPin6(double rPin6) { setBitRate((double)kBits / delaySecondsFor(rPin6)); }
    /** Pin 6 through `rPin6` with pin 2 at `vref` volts (see delaySecondsForVref).
        REF is also the analog ground of every stage in the block diagram -- the
        LPF, comparator and both integrators all return to it -- so it is
        *signals* that are relative to it and this model, which works relative
        to REF, does not move with it; the clock is the one place it matters. */
    void setPin6Vref(double rPin6, double vref) {
        setBitRate((double)kBits / delaySecondsForVref(rPin6, vref));
    }
    /** The integrators' time constant when it is not the Amortization one:
        the chip's internal resistor times the capacitor across pins 9-10 and
        11-12. The default is the one the other modules were fitted with. */
    void setIntegrator(double rInternal, double cFarads) {
        tauInt = rInternal * cFarads;
        bitRate = 0.0;                          // force setBitRate to recompute alpha
    }
    void setBitRate(double fs) {
        if (fs == bitRate) return;
        bitRate = fs;
        alpha = (float)(1.0 - std::exp(-1.0 / (fs * tauInt)));
        kSyl = (float)(1.0 - std::exp(-1.0 / (fs * assumed::ADM_TAU_SYL)));
    }
    double delaySeconds() const { return (double)kBits / bitRate; }

    /** Number of clock bits that fall in this audio sample. */
    int begin() {
        steady = bitRate == lastBeginRate;
        lastBeginRate = bitRate;
        acc += bitRate / fsAudio;
        m = (int)acc;
        acc -= m;
        return m;
    }

    struct Coef { float alpha, kSyl, vsMin, vsMax, clipV; };
    Coef coef() const { return Coef{alpha, kSyl, vsMin, vsMax, clipV}; }

    /** One bit through one integrator. Static and on a copy of the state, so a
        caller can keep a whole pass in registers: a store into `ram` is a byte
        store, which the compiler must assume may alias the state it is about to
        read again. */
    static inline void advanceC(Adm& s, int bit, const Coef& k) {
        s.hist = (uint8_t)(((s.hist << 1) | bit) & 7);
        float run = (s.hist == 0 || s.hist == 7) ? 1.f : 0.f;
        s.u += k.kSyl * (run - s.u);
        float vs = k.vsMin + (k.vsMax - k.vsMin) * s.u;
        s.v += k.alpha * ((bit ? vs : -vs) - s.v);
        // The integrator is an op-amp on a 5 V supply around a 2.5 V reference.
        if (s.v > k.clipV) s.v = k.clipV;
        else if (s.v < -k.clipV) s.v = -k.clipV;
    }
    inline void advance(Adm& s, int bit) const { advanceC(s, bit, coef()); }

    /** advanceC() for the modulator, whose `bit` is the comparator's decision on
        the integrator it is about to update. Written as above, each bit waits on
        the last one's whole chain (compare, run filter, step size, integrate,
        clip). Here both outcomes are computed from the incoming state, which
        does not depend on the bit, and the bit only picks -- the same arithmetic
        in the same order, with the chain cut to the integrator's own
        subtract/fma/clamp. ~1.8x on the modulator loop at the fast clocks. */
    static inline void advanceSel(Adm& s, int bit, const Coef& k) {
        const uint8_t h0 = (uint8_t)((s.hist << 1) & 7), h1 = (uint8_t)(h0 | 1);
        const float r0 = h0 == 0 ? 1.f : 0.f, r1 = h1 == 7 ? 1.f : 0.f;
        const float u0 = s.u + k.kSyl * (r0 - s.u), u1 = s.u + k.kSyl * (r1 - s.u);
        const float span = k.vsMax - k.vsMin;
        const float vs0 = k.vsMin + span * u0, vs1 = k.vsMin + span * u1;
        float v0 = s.v + k.alpha * (-vs0 - s.v);
        float v1 = s.v + k.alpha * (vs1 - s.v);
        v0 = v0 > k.clipV ? k.clipV : (v0 < -k.clipV ? -k.clipV : v0);
        v1 = v1 > k.clipV ? k.clipV : (v1 < -k.clipV ? -k.clipV : v1);
        s.hist = bit ? h1 : h0;
        s.u = bit ? u1 : u0;
        s.v = bit ? v1 : v0;
    }

    /** Pin 12: the demodulator's output, averaged over this audio sample. Reads
        the bits that were written kBits clocks ago; call before modulate(). */
    float demod() {
        if (m <= 0) { spec.valid = false; return lastOut; }
        if (spec.valid) {
            spec.valid = false;
            if (spec.m == m && spec.w == w && spec.rate == bitRate
                && spec.alpha == alpha && spec.kSyl == kSyl && spec.vsMin == vsMin
                && spec.vsMax == vsMax && spec.clipV == clipV) {
                dem = spec.dem;
                lastOut = (float)(assumed::CHIP_POLARITY * spec.sum / m);
                return lastOut;
            }
        }
        double sum = 0.0;
        uint32_t idx = w;
        for (int j = 0; j < m; j++) {
            advance(dem, ram[idx]);
            sum += dem.v;
            if (++idx == (uint32_t)kBits) idx = 0;
        }
        lastOut = (float)(assumed::CHIP_POLARITY * sum / m);
        return lastOut;
    }

    /** The comparator and OP1: `x0` to `x1` is LPF1's output across this audio
        sample. Writes the new bits over the ones demod() just read. */
    void modulate(float x0, float x1) {
        Pt2399* self = this;
        modulateN(&self, 1, &x0, &x1);
    }

    /** modulate() for `n` (1..3) chips at once, each with its own input ramp.
        Each chip's modulator -- and, pipelined, its next demodulator -- is a
        chain that waits on its own previous bit; chains from different chips
        are independent, so stepping them in the same loop overlaps them where
        separate calls each wait. Per chip the arithmetic and its order are
        exactly modulate()'s, so the result is bit-identical (tests/Amortization). */
    static void modulateN(Pt2399** c, int n, const float* x0, const float* x1) {
        bool noisy = false;
        for (int i = 0; i < n; i++) noisy = noisy || c[i]->cmpNoise > 0.f;
        // The chip count is a template argument so the per-chip loop unrolls
        // and every chip's state stays in registers.
        if (n == 3) { if (noisy) modulateK<3, true>(c, x0, x1); else modulateK<3, false>(c, x0, x1); }
        else if (n == 2) { if (noisy) modulateK<2, true>(c, x0, x1); else modulateK<2, false>(c, x0, x1); }
        else { if (noisy) modulateK<1, true>(c, x0, x1); else modulateK<1, false>(c, x0, x1); }
    }

private:
    template <int n, bool Noisy>
    static void modulateK(Pt2399** c, const float* x0, const float* x1) {
        struct T {
            Adm md, d;
            Coef k;
            float x, dx, nz;
            double sum;
            uint32_t wr, rd, ns;
            int m, m2;
            uint8_t* r;
        } t[n];
        int jMax = 0;
        for (int i = 0; i < n; i++) {
            Pt2399& p = *c[i];
            T& s = t[i];
            p.spec.valid = false;
            s.m = p.m > 0 ? p.m : 0;
            s.dx = s.m > 0 ? (x1[i] - x0[i]) / (float)s.m : 0.f;
            s.x = x0[i];
            // State in locals for the pass: a byte store into `ram` would
            // otherwise force a reload of `mod` and `w` on every bit.
            s.md = p.mod;
            s.k = p.coef();
            s.wr = p.w;
            s.r = p.ram.data();
            s.nz = p.cmpNoise * 1.7320508f * (1.f / 2147483648.f);
            s.ns = p.noiseSeed;
            // The next sample's bit count, if the clock holds: what begin() will
            // compute then, to the same arithmetic.
            s.m2 = 0;
            if (s.m > 0 && p.pipeline && p.steady) {
                s.m2 = (int)(p.acc + p.bitRate / p.fsAudio);
                if (s.m2 > 0 && s.m + s.m2 >= kBits / 2) s.m2 = 0;
            }
            if (s.m2 > 0) {
                s.d = p.dem;
                s.sum = 0.0;
                s.rd = s.wr + (uint32_t)s.m;
                if (s.rd >= (uint32_t)kBits) s.rd -= (uint32_t)kBits;
            }
            int top = s.m > s.m2 ? s.m : s.m2;
            if (top > jMax) jMax = top;
        }
        auto dem = [](T& s) {
            advanceC(s.d, s.r[s.rd], s.k);
            s.sum += s.d.v;
            if (++s.rd == (uint32_t)kBits) s.rd = 0;
        };
        auto mod = [](T& s) {
            s.x += s.dx;
            float xc = s.x;
            if (Noisy) {
                // The comparator's own noise: a fresh draw per decision.
                s.ns ^= s.ns << 13; s.ns ^= s.ns >> 17; s.ns ^= s.ns << 5;
                xc += s.nz * (float)(int32_t)s.ns;
            }
            int bit = xc > s.md.v ? 1 : 0;
            advanceSel(s.md, bit, s.k);
            s.r[s.wr] = (uint8_t)bit;
            if (++s.wr == (uint32_t)kBits) s.wr = 0;
        };
        // Every chain of every chip, while all of them still have bits ...
        int jAll = jMax;
        bool allSpec = true;
        for (int i = 0; i < n; i++) {
            if (t[i].m < jAll) jAll = t[i].m;
            if (t[i].m2 > 0) { if (t[i].m2 < jAll) jAll = t[i].m2; }
            else allSpec = false;
        }
        int j = 0;
        if (allSpec) {
            for (; j < jAll; j++)
                for (int i = 0; i < n; i++) { dem(t[i]); mod(t[i]); }
        } else {
            for (int i = 0; i < n; i++) if (t[i].m < jAll) jAll = t[i].m;
            for (; j < jAll; j++)
                for (int i = 0; i < n; i++) { if (j < t[i].m2) dem(t[i]); mod(t[i]); }
        }
        // ... then whatever each has left.
        for (; j < jMax; j++)
            for (int i = 0; i < n; i++) {
                if (j < t[i].m2) dem(t[i]);
                if (j < t[i].m) mod(t[i]);
            }
        for (int i = 0; i < n; i++) {
            Pt2399& p = *c[i];
            T& s = t[i];
            if (s.m <= 0) continue;
            p.mod = s.md;
            p.w = s.wr;
            p.noiseSeed = s.ns;
            p.spec.w = s.wr;
            if (s.m2 > 0) {
                p.spec.valid = true;
                p.spec.dem = s.d; p.spec.sum = s.sum; p.spec.m = s.m2;
                p.spec.rate = p.bitRate;
                p.spec.alpha = p.alpha; p.spec.kSyl = p.kSyl; p.spec.vsMin = p.vsMin;
                p.spec.vsMax = p.vsMax; p.spec.clipV = p.clipV;
            }
        }
    }
public:

    /** demod() then modulate(), in one pass, for a caller whose modulator input
        does not depend on this sample's demodulator output (no feedback around
        the chip). The result is bit-identical to calling the two in turn
        (tests/Diversified checks that); it is faster because the state lives in
        registers for the whole pass. */
    float demodModulate(float x0, float x1) {
        Pt2399* self = this;
        float y;
        pass(&self, 1, x0, x1, &y);
        return y;
    }

    /** Two chips in one pass. Each modulator waits on its own previous bit, so
        one chip alone is latency-bound; two independent chains in the same loop
        overlap. Also bit-identical to doing each chip on its own. */
    static void demodModulate2(Pt2399& a, Pt2399& b, float x0, float x1,
                               float& ya, float& yb) {
        Pt2399* chips[2] = { &a, &b };
        float y[2];
        pass(chips, 2, x0, x1, y);
        ya = y[0];
        yb = y[1];
    }

private:
    /** The shared kernel: `n` (1 or 2) chips, the first min(m) bits together and
        each one's remainder alone. */
    static void pass(Pt2399** c, int n, float x0, float x1, float* y) {
        struct S {
            Adm dem, mod;
            Coef k;
            float x, dx;
            double sum;
            uint32_t w;
            int m;
        } s[2];
        int mMin = 1 << 30;
        for (int i = 0; i < n; i++) {
            Pt2399& p = *c[i];
            s[i].dem = p.dem; s[i].mod = p.mod; s[i].k = p.coef();
            s[i].m = p.m;
            s[i].dx = p.m > 0 ? (x1 - x0) / (float)p.m : 0.f;
            s[i].x = x0; s[i].sum = 0.0; s[i].w = p.w;
            if (p.m < mMin) mMin = p.m;
        }
        uint8_t* ram0 = c[0]->ram.data();
        uint8_t* ram1 = n > 1 ? c[1]->ram.data() : nullptr;
        auto one = [](S& t, uint8_t* ram) {
            advanceC(t.dem, ram[t.w], t.k);
            t.sum += t.dem.v;
            t.x += t.dx;
            int bit = t.x > t.mod.v ? 1 : 0;
            advanceSel(t.mod, bit, t.k);
            ram[t.w] = (uint8_t)bit;
            if (++t.w == (uint32_t)kBits) t.w = 0;
        };
        if (n == 2) {
            for (int j = 0; j < mMin; j++) { one(s[0], ram0); one(s[1], ram1); }
            for (int j = mMin; j < s[0].m; j++) one(s[0], ram0);
            for (int j = mMin; j < s[1].m; j++) one(s[1], ram1);
        } else {
            for (int j = 0; j < s[0].m; j++) one(s[0], ram0);
        }
        for (int i = 0; i < n; i++) {
            Pt2399& p = *c[i];
            if (s[i].m > 0) {
                p.dem = s[i].dem; p.mod = s[i].mod; p.w = s[i].w;
                p.lastOut = (float)(assumed::CHIP_POLARITY * s[i].sum / s[i].m);
            }
            y[i] = p.lastOut;
        }
    }
public:
};

} // namespace pt2399

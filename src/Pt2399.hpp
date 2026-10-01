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
    float alpha = 0.01f, kSyl = 0.01f;
    float vsMin = (float)assumed::ADM_VS_MIN, vsMax = (float)assumed::ADM_VS_MAX;
    float clipV = (float)assumed::CHIP_CLIP;
    double acc = 0.0, fsAudio = 48000.0;
    int m = 0;                                  // bits this audio sample
    float lastOut = 0.f;

    Pt2399() { ram.assign(kBits, 0); reset(); }

    void reset() {
        for (int i = 0; i < kBits; i++) ram[i] = (uint8_t)(i & 1);
        w = 0;
        mod = Adm(); dem = Adm();
        acc = 0.0; m = 0; lastOut = 0.f;
    }

    void setSampleRate(double fs) { fsAudio = fs; }

    void setPin6(double rPin6) { setBitRate((double)kBits / delaySecondsFor(rPin6)); }
    void setBitRate(double fs) {
        if (fs == bitRate) return;
        bitRate = fs;
        alpha = (float)(1.0 - std::exp(-1.0 / (fs * kTauInt)));
        kSyl = (float)(1.0 - std::exp(-1.0 / (fs * assumed::ADM_TAU_SYL)));
    }
    double delaySeconds() const { return (double)kBits / bitRate; }

    /** Number of clock bits that fall in this audio sample. */
    int begin() {
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

    /** Pin 12: the demodulator's output, averaged over this audio sample. Reads
        the bits that were written kBits clocks ago; call before modulate(). */
    float demod() {
        if (m <= 0) return lastOut;
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
        if (m <= 0) return;
        float dx = (x1 - x0) / (float)m;
        float x = x0;
        for (int j = 0; j < m; j++) {
            x += dx;
            int bit = x > mod.v ? 1 : 0;
            advance(mod, bit);
            ram[w] = (uint8_t)bit;
            if (++w == (uint32_t)kBits) w = 0;
        }
    }

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
            advanceC(t.mod, bit, t.k);
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

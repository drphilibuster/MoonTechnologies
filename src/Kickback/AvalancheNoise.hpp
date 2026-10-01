#pragma once
// The noise source of the Percussive Noise Voice (Modular in a Week Day 9,
// `Percussive Noise Voice.pdf`, circuit by A_Magic_Pulsewave, drawn by Kristian Blasol,
// 2019-10-17) as a circuit: three BC549s, T3 a transistor whose emitter-base junction is
// run in reverse breakdown as the noise source ("an EBC transistor facing backwards").
//
//   VCC --R4 4k7-- C (T2's collector)        T2 BC549, emitter to ground
//   C --R3 47k-- A --C3 0.1u-- ground        A is T3's emitter (the junction's n side)
//   T3's base is T2's base (its collector is left open), so the junction runs from A back
//   into T2's base: reverse biased, and in breakdown once A is a breakdown voltage above it
//   C --C4 1u-- B1 (T1's base), B1 --R7 1M-- VCC, B1 --C5-- ground, T1's base-emitter to ground
//
// T2's base current is the breakdown current, so T2's collector (and so A, through R3) is
// pulled down until the junction just conducts: the loop biases itself with the junction at
// the edge of breakdown, and the avalanche's noise current is amplified by T2 into the signal
// C4 hands to T1. B1 is what T1 sees, and is the output here.
//
// Solved by `src/Mna.hpp`. The breakdown's noise is a current source across the junction with
// the shot-noise spectrum multiplied by the avalanche gain, 2 q I M^2, white up to the solver's
// rate. T1's collector side (D3, R8, the envelope, C6) and the trigger envelope are not part
// of this: the module has its own envelope, vactrol and filters.
//
// ASSUMED, because neither the drawing nor a BC549 datasheet says: the supply is 12 V; the
// junction breaks down at 8 V (a BC549's VEBO rating is only 5 V, real junctions avalanche at
// 7-10 V; this is the number to measure on a real part) with an e-fold every 100 mV; the
// avalanche gain is M = 30; and C5 is the snare's 0.1 uF. The absolute noise level is
// therefore not known, and the output is normalised by the caller; the spectrum and the
// nonlinearity are the circuit's.
//
// No Rack dependency, so tests/ drives it bare.

#include "../Mna.hpp"

#include <cmath>
#include <cstdint>

namespace kickback {

struct AvalancheNoise {
    enum Node { A, B2, C2, B1, kNodes };
    enum Fixed { VCC = 0, kFixed };

    static constexpr double kVcc = 12.0;
    static constexpr double kIs = 7.6e-14, kBF = 520.0, kBR = 10.0, kVAF = 100.0;
    static constexpr double kBv = 8.0, kIbv = 1e-3, kNvtBr = 0.1;
    static constexpr double kM = 30.0;
    static constexpr double kQ = 1.602176634e-19;

    mna::Circuit ckt;
    int junction = -1, noiseSrc = -1;
    bool ok = true;
    double rate = 192000.0;
    uint32_t rng = 0x9E3779B9u;
    bool started = false;

    AvalancheNoise() {
        using namespace mna;
        ckt.n = kNodes;
        const int vcc = fixed(VCC);
        ckt.addResistor(vcc, C2, 4.7e3);                          // R4
        ckt.addNpn(C2, B2, GND, kIs, kBF, kBR, kVAF);             // T2
        ckt.addResistor(C2, A, 47e3);                             // R3
        ckt.addCapacitor(A, GND, 0.1e-6);                         // C3
        junction = ckt.addZener(B2, A, 1e-14, 0.025852, kBv, kIbv, kNvtBr);   // T3: base -> emitter
        noiseSrc = ckt.addCurrent(A, B2);
        ckt.addCapacitor(C2, B1, 1e-6);                           // C4
        ckt.addResistor(vcc, B1, 1e6);                            // R7
        ckt.addCapacitor(B1, GND, 0.1e-6);                        // C5
        ckt.addDiode(B1, GND, 7.6e-14 / 520.0, 0.025852);         // T1's base-emitter
    }

    void seed(uint32_t s) { rng = s ? s : 0x9E3779B9u; }

    /** One standard-normal sample (Box-Muller from an xorshift32). */
    double gauss() {
        auto u = [this]() {
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            return (rng + 0.5) / 4294967296.0;
        };
        double a = u(), b = u();
        return std::sqrt(-2.0 * std::log(a)) * std::cos(2.0 * 3.14159265358979323846 * b);
    }

    /** Finds the operating point and sets the step; the noise is off while it settles. */
    void start(double solverRate) {
        rate = solverRate;
        double tgt[kFixed] = { kVcc };
        ok = ckt.solveDc(tgt, kFixed, 48);
        ckt.h = 1.0 / rate;
        started = true;
    }

    /** One solver step; returns B1's voltage. `noiseOn` false gives the quiet circuit. */
    double step(bool noiseOn = true) {
        if (!started) start(rate);
        ckt.fixedV[VCC] = kVcc;
        double i = ckt.diodeCurrent(junction);
        double ib = i < 0.0 ? -i : 0.0;                    // the breakdown current, A -> base
        double sigma = std::sqrt(2.0 * kQ * ib * kM * kM * rate * 0.5);
        ckt.setCurrent(noiseSrc, noiseOn ? sigma * gauss() : 0.0);
        if (ckt.step() < 0) ok = false;
        return ckt.v[B1];
    }
};

/** The circuit as a noise source at the audio rate: the solver runs at a whole multiple of it
    (at least 192 kHz), B1's DC is taken off, and a fourth-order low-pass at 0.4 of the audio
    rate (two biquads) takes it down before every K-th sample is kept. The noise current's
    density is per hertz, so the output has the circuit's own spectrum at every rate; `kNorm`
    sets its level to the white noise this replaced (rms 1/sqrt(3) in the band at 48 kHz). */
struct AvalancheSource {
    static constexpr double kNorm = 1065.0;   // 0.57735 / 0.5421 mV: the 48 kHz rms of this circuit, as volts

    AvalancheNoise ckt;
    uint32_t seed0 = 0x5EAF00Du;
    int K = 4;
    double fs = 48000.0, dc = 0.0;
    struct Bq { double b0, b1, b2, a1, a2, z1 = 0, z2 = 0;
        double run(double x) { double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; } };
    Bq q[2];
    bool ready = false;

    explicit AvalancheSource(uint32_t seed = 0x5EAF00Du) : seed0(seed) {}

    void setRate(double audioRate) {
        fs = audioRate;
        K = (int)std::ceil(192000.0 / fs);
        if (K < 1) K = 1;
        double fc = 0.4 * fs, w0 = 2.0 * 3.14159265358979323846 * fc / (fs * K);
        double Qs[2] = { 0.5411961, 1.3065630 };               // 4th-order Butterworth
        for (int i = 0; i < 2; i++) {
            double al = std::sin(w0) / (2.0 * Qs[i]), c = std::cos(w0), a0 = 1.0 + al;
            q[i].b0 = (1.0 - c) / 2.0 / a0; q[i].b1 = (1.0 - c) / a0; q[i].b2 = q[i].b0;
            q[i].a1 = -2.0 * c / a0; q[i].a2 = (1.0 - al) / a0;
        }
        reset();
    }

    void reset() {
        ckt = AvalancheNoise();
        ckt.seed(seed0);
        ckt.start(fs * K);
        dc = ckt.ckt.v[AvalancheNoise::B1];
        for (int i = 0; i < 2; i++) q[i].z1 = q[i].z2 = 0.0;
        ready = true;
    }

    /** One audio-rate sample. */
    inline float next() {
        double y = 0.0;
        for (int k = 0; k < K; k++) {
            double v = ckt.step() - dc;
            y = q[1].run(q[0].run(v));
        }
        return (float)(y * kNorm);
    }
};

} // namespace kickback

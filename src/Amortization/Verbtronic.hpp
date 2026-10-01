// The Verbtronic's circuit, solved -- not an algorithm in its image.
//
// Pittsburgh Modular released the module's schematic into the public domain
// (the "PT2399 Verbtronic Reverb" diagram on pittsburghmodular.com/verbtronic,
// by Micheal Johnsen). It is not a reverb algorithm. It is three PT2399 echo
// chips wired into a recirculating network, an op-amp tone control and zener
// limiter in front, a make-up amplifier behind, and a linearised SSM2164 VCA
// for the wet/dry mix. "Verb" and "Tronic" are the same circuit with the three
// chips' clock resistors switched: a DG202 puts a second resistor across each
// pin 6, which speeds every clock up and every delay down.
//
// Everything below is that schematic, block by block, with the parts' values.
// Where a value is not on the schematic or the datasheets it is marked ASSUMED
// beside the constant, and every one of them is collected in `assumed` so they
// can be found, measured against a real unit and changed in one place.
//
// Works in volts, at whatever sample rate it is told. Self-contained: <cmath>,
// <cstdint>, <vector>, nothing from Rack, so tests/Amortization drives it bare.
#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace amortization {

inline double clampd(double x, double lo, double hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}

// ---------------------------------------------------------------------------
// Values off the schematic. Ohms, farads, volts.
namespace part {
    // Input attenuator: inverting, 100k in (signal) and 100k in (feedback), 2k2 feedback.
    static const double R_ATTEN_IN = 100e3, R_ATTEN_FB = 2.2e3;
    // Feedback path: 100k FEEDBACK pot on the make-up output, into a 10k inverting
    // stage (10k feedback) with a 1k output resistor, 100k on into the attenuator.
    static const double R_FB_POT = 100e3, R_FB_IN = 10e3, R_FB_F = 10e3, R_FB_OUT = 1e3;
    // Tone: 100k pot between two 68k, two 22k + 3.3 nF shelving legs.
    static const double R_TONE_POT = 100e3, R_TONE_68 = 68e3, R_TONE_22 = 22e3, C_TONE = 3.3e-9;
    // AC coupling into the chips: 2u2, into two 10k.
    static const double C_COUPLE = 2.2e-6, R_COUPLE = 10e3;
    // Each chip's input stage: multiple-feedback low-pass around LPF1.
    static const double R_LPF1_FB = 10e3, R_LPF1_IN = 6.8e3, C_LPF1_X = 4.7e-9, C_LPF1_F = 510e-12;
    static const double R_SELF = 12.1e3;     // 12k1: a chip's own output back to its input
    static const double R_MESH_U12 = 12.1e3; // 12k1: the sum back into chips 1 and 2
    static const double R_MESH_U3 = 10e3;    // 10k:  the sum into chip 3
    // The sum: chip 1's LPF2 amplifier as a summer, 10k per chip, 6k8 feedback.
    static const double R_SUM_IN = 10e3, R_SUM_FB = 6.8e3;
    // Chip 3's output stage: multiple-feedback low-pass around its LPF2.
    static const double R_OUT_A = 5.1e3, R_OUT_B = 5.1e3, R_OUT_C = 10e3;
    static const double C_OUT_SHUNT = 5.6e-9, C_OUT_F = 100e-12;
    // Make-up gain: 10u coupling, 10k in, 100k feedback.
    static const double C_MAKEUP = 10e-6, R_MAKEUP_IN = 10e3, R_MAKEUP_F = 100e3;
    // Mixer: 12 V across the MIX pot, 100k into the control summing node; MIX CV
    // through a 100k/100k attenuverter then 30k1; 10M to -12 V; reference
    // current from -12 V through 100k into the dummy VCA.
    static const double V_RAIL = 12.0;
    static const double R_MIX_LOAD = 100e3, R_MIX_CV = 30.1e3, R_MIX_BIAS = 10e6, R_MIX_REF = 100e3;
    // PT2399 clock resistors. Per chip: the always-there ("long") resistor and
    // the one the DG202 switches in across it ("short").
    static const double R_LONG[3]  = {8200.0, 5600.0, 3830.0};
    static const double R_SHORT[3] = {3830.0, 2200.0, 1000.0};
}

// ---------------------------------------------------------------------------
// Everything that is not on the schematic or in a datasheet this module read.
namespace assumed {
    // DG202 on-resistance in series with the switched clock resistor: 115 ohm
    // typical (175-200 max) at +-15 V, Maxim DG202/DG212 datasheet, Rev 3. At the
    // module's +-12 V it is somewhat higher; the datasheet does not tabulate it.
    static const double SWITCH_RON = 115.0;
    // DG202 logic: TTL-compatible, logic 1 = ON (datasheet: low 0.8 V, high 2.4 V).
    // The module's mode-gate circuit is not on the schematic; this assumes the
    // gate ORs onto the logic node the toggle drives.
    static const double GATE_HIGH = 2.4, GATE_LOW = 0.8;
    // The feedback limiter: a 5V1 zener inside a four-diode bridge. Conducts at
    // the zener plus two forward drops, with an exponential knee.
    static const double LIM_KNEE_V = 6.2, LIM_SOFT_V = 0.16, LIM_I0 = 1e-3;
    // The +-12 V op-amps clip a little inside the rails; the PT2399's own
    // op-amps and its modulator run from 5 V around a 2.5 V reference.
    static const double RAIL_CLIP = 10.5, CHIP_CLIP = 2.4;
    // PT2399 modulator. The leak time constant is the datasheet's (4.7k inside
    // the chip, 0.047u across pins 9-10 and 11-12). The step sizes and the
    // syllabic filter are NOT in the datasheet: they are set so the model meets
    // its THD, output-swing and gain specifications. See tests/Amortization.
    static const double ADM_VS_MIN = 0.15;    // volts, the un-adapted step
    static const double ADM_VS_MAX = 5.0;     // volts, the fully adapted step (the integrator still clips at CHIP_CLIP)
    static const double ADM_TAU_SYL = 0.1e-3;   // seconds, run-length filter
    // The PT2399's output noise floor: -90 dBV typical, A-weighted (datasheet).
    // Injected at each chip's comparator; it is what lets a loop past unity start
    // by itself from silence.
    static const double CHIP_NOISE_V = 40e-6;
    // Sign of the demodulator relative to the modulator's input (+1: the delayed
    // copy has the same polarity). Not determinable from the datasheet.
    static const double CHIP_POLARITY = 1.0;
}


/** First-order section H(s) = (b1 s + b0) / (a1 s + 1), bilinear, with the
    warp pinned at `pivot` rad/s so the corner lands where the analog one is. */
struct Section1 {
    double B0 = 1, B1 = 0, A1 = 0, z = 0;
    void set(double b1, double b0, double a1, double fs, double pivot) {
        double wp = pivot;
        if (wp > 0.9 * M_PI * fs) wp = 0.9 * M_PI * fs;
        double K = wp > 1e-9 ? wp / std::tan(wp / (2.0 * fs)) : 2.0 * fs;
        double A0 = a1 * K + 1.0;
        B0 = (b1 * K + b0) / A0;
        B1 = (b0 - b1 * K) / A0;
        A1 = (1.0 - a1 * K) / A0;
    }
    double process(double x) {
        double y = B0 * x + z;
        z = B1 * x - A1 * y;
        return y;
    }
    void reset() { z = 0; }
};

/** Second-order section H(s) = g / (1 + b1 s + b2 s^2), plain bilinear. */
struct Section2 {
    double B0 = 1, B1 = 0, B2 = 0, A1 = 0, A2 = 0, z1 = 0, z2 = 0;
    /** `pivot` (rad/s, optional) pins the bilinear warp so that frequency lands
        where the analog one does; it must be below Nyquist. */
    void set(double g, double b1, double b2, double fs, double pivot = 0.0) {
        double K = 2.0 * fs;
        if (pivot > 0.0 && pivot < 0.9 * M_PI * fs) K = pivot / std::tan(pivot / (2.0 * fs));
        double a0 = 1.0 + b1 * K + b2 * K * K;
        double a1 = 2.0 - 2.0 * b2 * K * K;
        double a2 = 1.0 - b1 * K + b2 * K * K;
        B0 = g / a0; B1 = 2.0 * g / a0; B2 = g / a0;
        A1 = a1 / a0; A2 = a2 / a0;
    }
    double process(double x) {
        double y = B0 * x + z1;
        z1 = B1 * x - A1 * y + z2;
        z2 = B2 * x - A2 * y;
        return y;
    }
    void reset() { z1 = z2 = 0; }
};


// ---------------------------------------------------------------------------
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

    inline void advance(Adm& s, int bit) const {
        s.hist = (uint8_t)(((s.hist << 1) | bit) & 7);
        float run = (s.hist == 0 || s.hist == 7) ? 1.f : 0.f;
        s.u += kSyl * (run - s.u);
        float vs = vsMin + (vsMax - vsMin) * s.u;
        s.v += alpha * ((bit ? vs : -vs) - s.v);
        // The integrator is an op-amp on a 5 V supply around a 2.5 V reference.
        if (s.v > clipV) s.v = clipV;
        else if (s.v < -clipV) s.v = -clipV;
    }

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
};


// ---------------------------------------------------------------------------
/** The whole module. One call per audio sample. */
struct Verbtronic {
    struct In {
        double vin = 0;        // volts at IN
        double feedback = 0;   // FEEDBACK pot, 0..1, clockwise up
        double tilt = 0.5;     // TILT pot, 0..1, clockwise up
        double mix = 0.5;      // OUTPUT MIX pot, 0..1, clockwise up
        double mixAtten = 0.5; // MIX CV attenuverter pot, 0..1 (0.5 = off)
        double mixCv = 0;      // volts at MIX CV
        bool verb = true;      // mode: true = Verb (clocks sped up), false = Tronic
    };
    struct Out {
        double mix = 0;        // volts at MIX OUT
        double verb = 0;       // volts at VERB OUT
        double wet = 0;        // the wet bus, before the output resistor
        double limiter = 0;    // 0..1, how hard the feedback zener is working
        double gain = 0;       // the VCA's gain: 0 dry .. 1 wet
        bool clipped = false;  // a chip op-amp or the make-up stage is clipping
    };

    Pt2399 chip[3];
    double fs = 48000.0;
    bool modeVerb = true;

    Section2 outLpf;                // chip 3's LPF2
    Section1 makeupHp, coupleHp;    // 10u and 2u2
    Section1 tone;
    Section2 lpf1[3];
    double p15prev[3] = {0, 0, 0};
    uint32_t rng = 0x9E3779B9u;
    double toneX = -1;
    double limV = 0;                // the limiter's last solution, a Newton start

    Verbtronic() { setSampleRate(48000.0); }

    static double par(double a, double b) { return a * b / (a + b); }

    /** The pin-6 resistance for chip i in a mode. */
    static double pin6(int i, bool verb) {
        if (!verb) return part::R_LONG[i];
        return par(part::R_SHORT[i] + assumed::SWITCH_RON, part::R_LONG[i]);
    }

    static double delayMs(int i, bool verb) { return 1e3 * Pt2399::delaySecondsFor(pin6(i, verb)); }

    void setSampleRate(double sr) {
        fs = sr;
        for (int i = 0; i < 3; i++) {
            chip[i].setSampleRate(sr);
            chip[i].setPin6(pin6(i, modeVerb));
        }
        // LPF2 of chip 3: multiple feedback. Derived in tools/ in the notes of
        // docs/Amortization.md; DC gain -1, f0 = 29.8 kHz, Q = 2.1.
        const double b1 = part::R_OUT_C * part::C_OUT_F * (2.0 / part::R_OUT_A + 1.0 / part::R_OUT_C) * part::R_OUT_A;
        const double b2 = part::R_OUT_C * part::C_OUT_F * part::C_OUT_SHUNT * part::R_OUT_A;
        outLpf.set(-1.0, b1, b2, sr);
        makeupHp.set(part::C_MAKEUP * part::R_MAKEUP_IN, 0.0, part::C_MAKEUP * part::R_MAKEUP_IN, sr,
                     1.0 / (part::C_MAKEUP * part::R_MAKEUP_IN));
        coupleHp.set(part::C_COUPLE * part::R_COUPLE * 0.5, 0.0, part::C_COUPLE * part::R_COUPLE * 0.5, sr,
                     2.0 / (part::C_COUPLE * part::R_COUPLE));
        // LPF1 of each chip: a multiple-feedback low-pass whose summing node also
        // carries the mesh resistors, so G -- the conductance there -- sets the Q.
        // H = 1 / (1 + b1 s + b2 s^2), unity at DC (the inversion and the 10k/Rk
        // gain are applied where the currents are summed): f0 = 12.5 kHz, Q 0.72
        // for chips 1 and 2 and 0.86 for chip 3.
        for (int i = 0; i < 3; i++) {
            double g = (i < 2 ? 1.0 / part::R_COUPLE + 1.0 / part::R_MESH_U12 : 1.0 / part::R_MESH_U3)
                     + 1.0 / part::R_SELF + 1.0 / part::R_LPF1_FB + 1.0 / part::R_LPF1_IN;
            double b1 = part::R_LPF1_FB * part::R_LPF1_IN * part::C_LPF1_F * g;
            double b2 = part::R_LPF1_FB * part::R_LPF1_IN * part::C_LPF1_F * part::C_LPF1_X;
            lpf1[i].set(1.0, b1, b2, sr, 1.0 / std::sqrt(b2));
        }
        toneX = -1;
        reset();
    }

    void reset() {
        for (int i = 0; i < 3; i++) { chip[i].reset(); lpf1[i].reset(); p15prev[i] = 0; }
        rng = 0x9E3779B9u;
        outLpf.reset(); makeupHp.reset(); coupleHp.reset(); tone.reset();
        limV = 0;
    }

    /** The tone op-amp, as a first-order shelf. x is the wiper's position
        from the CW lug (schematic lug 1): 0 is full CW (dark, bass up),
        1 is full CCW (bright). Derived from the schematic's nodal equations. */
    void setTone(double x) {
        if (std::fabs(x - toneX) < 1e-5) return;
        toneX = x;
        double D = 25.0 * x + 17.0;
        double n0 = (25.0 * x - 42.0) / D;
        double n1 = 33.0 * (-575.0 * x - 462.0) / (5e6 * D);
        double d1 = 33.0 * (1037.0 - 575.0 * x) / (5e6 * D);
        tone.set(n1, n0, d1, fs, 1.0 / d1);
    }

    /** The bridge-and-zener limiter on the feedback path, with its 10k stage and
        1k output resistor into the attenuator. `iin` is the current the FEEDBACK
        pot pushes into the stage; returns the voltage at the attenuator's
        resistor. Solves 11*Ibr(V) + V/1k + V/100k + 10*Iin = 0. */
    double limiter(double iin, double& amount) {
        const double k = 1.0 / part::R_FB_OUT + 1.0 / part::R_ATTEN_IN;
        const double c = (part::R_FB_F / part::R_FB_OUT + 1.0);
        double target = -(part::R_FB_F / part::R_FB_OUT) * iin / k;    // no clamp
        double mag = std::fabs(target);
        double v = mag;
        if (mag > assumed::LIM_KNEE_V - 2.5) {
            // Start where the exponential alone would balance the drive. That is
            // always to the right of the root, and f is convex and increasing, so
            // Newton then comes in monotonically -- from `mag` itself it would
            // creep down at one softness per step through an exp of e^85.
            double v0 = assumed::LIM_KNEE_V + assumed::LIM_SOFT_V * std::log(k * mag / (c * assumed::LIM_I0));
            if (v0 < v) v = v0;
            for (int it = 0; it < 40; it++) {
                double e = std::exp((v - assumed::LIM_KNEE_V) / assumed::LIM_SOFT_V);
                double f = c * assumed::LIM_I0 * e + k * v - mag * k;
                double df = c * assumed::LIM_I0 * e / assumed::LIM_SOFT_V + k;
                double nv = v - f / df;
                if (nv < 0) nv = 0;
                if (std::fabs(nv - v) < 1e-9) { v = nv; break; }
                v = nv;
            }
        }
        amount = clampd((v - 4.5) / 1.5, 0.0, 1.0);
        return target < 0 ? -v : v;
    }

    void process(const In& in, Out& out) {
        // --- the DG202s pick each chip's clock ---------------------------------
        if (in.verb != modeVerb) {
            modeVerb = in.verb;
            for (int i = 0; i < 3; i++) chip[i].setPin6(pin6(i, modeVerb));
        }
        double p12[3];
        for (int i = 0; i < 3; i++) { chip[i].begin(); p12[i] = chip[i].demod(); }

        // --- chip 1's LPF2 amplifier: the sum of all three delays --------------
        double sumRaw = -(part::R_SUM_FB / part::R_SUM_IN) * (p12[0] + p12[1] + p12[2]);
        bool clipped = false;
        double M = clampd(sumRaw, -assumed::CHIP_CLIP, assumed::CHIP_CLIP);
        clipped = clipped || M != sumRaw;

        // --- chip 3's output filter, then the make-up gain ---------------------
        double p14 = clampd(outLpf.process(p12[2]), -assumed::CHIP_CLIP, assumed::CHIP_CLIP);
        double mk = -(part::R_MAKEUP_F / part::R_MAKEUP_IN) * makeupHp.process(p14);
        double wet = clampd(mk, -assumed::RAIL_CLIP, assumed::RAIL_CLIP);
        clipped = clipped || wet != mk;

        // --- FEEDBACK pot, limiter, attenuator, tone ---------------------------
        double xf = clampd(in.feedback, 0.0, 1.0);
        double iin = wet * xf / (part::R_FB_POT * xf * (1.0 - xf) + part::R_FB_IN);
        double lim = 0;
        double vL = limiter(iin, lim);
        double va = -(part::R_ATTEN_FB / part::R_ATTEN_IN) * (in.vin + vL);
        // Wiper fraction from the CW lug: clockwise on the panel is brighter.
        setTone(1.0 - clampd(in.tilt, 0.0, 1.0));
        double vt = clampd(tone.process(va), -assumed::RAIL_CLIP, assumed::RAIL_CLIP);
        double vj = coupleHp.process(vt);

        // --- each chip's input stage -------------------------------------------
        double p15[3];
        double cur0 = vj / part::R_COUPLE + M / part::R_MESH_U12 + p12[0] / part::R_SELF;
        double cur1 = vj / part::R_COUPLE + M / part::R_MESH_U12 + p12[1] / part::R_SELF;
        double cur2 = M / part::R_MESH_U3 + p12[2] / part::R_SELF;
        double cur[3] = {cur0, cur1, cur2};
        for (int i = 0; i < 3; i++) {
            double raw = lpf1[i].process(-part::R_LPF1_FB * cur[i]);
            p15[i] = clampd(raw, -assumed::CHIP_CLIP, assumed::CHIP_CLIP);
            clipped = clipped || p15[i] != raw;
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            double noisy = p15[i] + assumed::CHIP_NOISE_V * 1.7320508 * ((double)(int32_t)rng * (1.0 / 2147483648.0));
            chip[i].modulate((float)p15prev[i], (float)noisy);
            p15prev[i] = noisy;
        }

        // --- the mixer ---------------------------------------------------------
        // MIX CV: a 100k/100k attenuverter, (2a - 1) * CV.
        double cv = clampd((2.0 * in.mixAtten - 1.0) * in.mixCv, -assumed::RAIL_CLIP, assumed::RAIL_CLIP);
        // MIX pot loaded by its 100k into the summing node.
        double xm = clampd(in.mix, 0.0, 1.0);
        double iMix = part::V_RAIL * xm / (part::R_MIX_LOAD * xm * (1.0 - xm) + part::R_MIX_LOAD);
        double iCtl = iMix + cv / part::R_MIX_CV - part::V_RAIL / part::R_MIX_BIAS;
        // The SSM2164 in the control op-amp's feedback makes gain = control
        // current / reference current; the diode keeps it from exceeding unity.
        double g = clampd(iCtl / (part::V_RAIL / part::R_MIX_REF), 0.0, 1.0);
        double mixV = clampd((1.0 - g) * in.vin + g * wet, -assumed::RAIL_CLIP, assumed::RAIL_CLIP);

        out.mix = mixV;
        out.verb = wet;
        out.wet = wet;
        out.limiter = lim;
        out.gain = g;
        out.clipped = clipped;
    }
};

} // namespace amortization

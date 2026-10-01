// The BitCrusher/BitSwapper board's converter chain, as the schematic draws it
// (Kristian Blasol, Sourcery Studios, Rev 1.0, 2021-06-20; Modular in a Week,
// Day 13): an LTC1799 clock into an ADC0809 that free-runs, its eight data lines
// into a 1k / 2k resistor ladder that sums into an op-amp's virtual ground.
//
// No Rack dependency, so tests/ drives it bare.
//
// What is from the datasheets and the schematic, and what is not, is kept apart:
// `assumed` lists everything this file had to pick.
#pragma once
#include <cmath>
#include <cstdint>

#include "TaxBracket/Ladder.hpp"

namespace adc0809 {

namespace assumed {
    // Output levels of the ADC0809's data pins into the ladder's 2k legs. The
    // datasheet guarantees only VOH >= 4.5 V at 360 uA, and a leg draws up to
    // 2.5 mA, so the real pin sags by an amount the datasheet does not give.
    static const double VOH = 5.0;
    static const double VOL = 0.0;
    // From START rising to EOC falling the chip takes 0 to 8 clock periods (plus
    // 2 us); with START tied to EOC that gap is part of every cycle. The worst
    // case is used; the datasheet gives no typical.
    static const double RESTART_CLOCKS = 8.0;
    // Where the transitions fall. The datasheet draws them at half an LSB, so the
    // code is the nearest step, not the one below.
    static const double TRANSITION_LSB = 0.5;
}

// The converter's own numbers.
static const double kVref = 5.0;           // VREF(+) on +5 V, VREF(-) on ground
static const int kLevels = 256;
static const double kConvClocks = 64.0;    // "100 us" at 640 kHz
static const double kCycleClocks = kConvClocks + assumed::RESTART_CLOCKS;

/** The LTC1799 resistor-set oscillator: f = 10 MHz * 10 kOhm / (N * R_SET), where
    N is 1, 10 or 100 for the DIV pin on V+, open or ground. Datasheet range
    1 kHz to 33 MHz (ASSUMED limits; the part's 1.5 % tolerance is not modelled). */
struct Ltc1799 {
    static double hz(double rSet, int n) {
        double f = 10e6 * (10e3 / ((double)n * rSet));
        if (f < 1e3) f = 1e3;
        if (f > 33e6) f = 33e6;
        return f;
    }
};

/** The ADC0809, wired as the board wires it: START to EOC, so it converts for
    ever, a cycle being the restart gap plus 64 clocks. The input is taken when
    the conversion proper starts and the result appears when EOC rises, 64 clocks
    later; the data pins show the output register, which only changes then.

    ALE and OUTPUT ENABLE are drawn unconnected. They are modelled as the usual
    tie (address latched to IN3, outputs enabled). */
struct Adc0809 {
    double phase = 0.0;      // clock periods into the cycle
    int code = 0;            // the output register
    int held = 0;            // the conversion in progress
    bool primed = false;

    void reset() { phase = 0.0; code = held = 0; primed = false; }

    /** The code for an input voltage, with the input protection clamping what
        is outside 0..VREF. */
    static int quantize(double v) {
        double n = std::floor(v / (kVref / (double)kLevels) + assumed::TRANSITION_LSB);
        if (n < 0.0) n = 0.0;
        if (n > (double)(kLevels - 1)) n = (double)(kLevels - 1);
        return (int)n;
    }

    /** Runs `clocks` clock periods across one audio sample, during which the
        input moves linearly from `v0` to `v1`. `dac[code]` is the volts the
        ladder stage makes of each code; returns the average over the sample, so
        a converter running faster than the audio rate is averaged rather than
        aliased. */
    float run(double clocks, double v0, double v1, const float* dac) {
        if (!primed) {
            code = held = quantize(v0);
            primed = true;
        }
        if (!(clocks > 0.0)) return dac[code];
        double t = 0.0, acc = 0.0;
        int guard = 0;
        while (t < clocks && guard++ < 100000) {
            bool sampleNext = phase < assumed::RESTART_CLOCKS;
            double ev = sampleNext ? assumed::RESTART_CLOCKS : kCycleClocks;
            double dt = ev - phase;
            if (dt > clocks - t) {
                acc += (double)dac[code] * (clocks - t);
                phase += clocks - t;
                t = clocks;
                break;
            }
            acc += (double)dac[code] * dt;
            t += dt;
            if (sampleNext) {
                phase = assumed::RESTART_CLOCKS;
                held = quantize(v0 + (v1 - v0) * (t / clocks));   // START falls
            }
            else {
                phase = 0.0;
                code = held;                                       // EOC rises
            }
        }
        return (float)(acc / clocks);
    }
};

/** The resistor ladder as the schematic draws it: 1k in the string, 2k from each
    string node to its data line, a 2k termination (R1 and R2) at the far end, and
    the node nearest the op-amp tied to its inverting input, which is a virtual
    ground. That end is the MSB; the termination end is the LSB. Left open, a leg
    is not the same as one driven low, because it stops loading its node, so the
    other bits' weights move a little: the network is solved for whichever legs are
    connected rather than assuming ideal binary weights. */
struct R2rDac {
    taxbracket::Ladder lad;
    float w[8];                    // amps into the virtual ground per volt, LSB first
    unsigned configured = 0xFFFFFFFFu;

    R2rDac() {
        lad.rSeries = 1000.f;
        lad.rBranch = 2000.f;
        lad.rTerm = 2000.f;
        lad.invalidate();
        for (int j = 0; j < 8; j++) w[j] = 0.f;
    }

    /** `driven`: bit j set if data line j (0 = LSB, 7 = MSB) is connected. */
    void configure(unsigned driven) {
        driven &= 0xFFu;
        if (driven == configured) return;
        configured = driven;
        lad.configure(driven, 1u << taxbracket::Ladder::IO);    // N8 is the virtual ground
        for (int j = 0; j < 8; j++) {
            if (!((driven >> j) & 1u)) { w[j] = 0.f; continue; }
            w[j] = (j == 7) ? 1.f / lad.rBranch                 // straight into the node
                            : lad.T[6][j] / lad.rSeries;        // down the string via R23
        }
    }

    /** The op-amp's output for every code: -Rf times the current the connected,
        high data lines push into the virtual ground. ADC bit b goes to ladder
        line b, or to 7-b when the word is swapped; `connectedAdcBits` says which
        ADC bits have a leg at all. */
    void buildTable(float rf, unsigned connectedAdcBits, bool swap, float* table) {
        unsigned drivenLines = 0;
        for (int b = 0; b < 8; b++)
            if ((connectedAdcBits >> b) & 1u)
                drivenLines |= 1u << (swap ? 7 - b : b);
        configure(drivenLines);
        const double hi = assumed::VOH, lo = assumed::VOL;
        for (int code = 0; code < kLevels; code++) {
            double i = 0.0;
            for (int b = 0; b < 8; b++) {
                if (!((connectedAdcBits >> b) & 1u)) continue;
                int line = swap ? 7 - b : b;
                i += (double)w[line] * (((code >> b) & 1) ? hi : lo);
            }
            table[code] = (float)(-(double)rf * i);
        }
    }
};

} // namespace adc0809

#pragma once
// The Modular in a Week Day 10 "variable mode CV quantizer for 16F684", run as the
// firmware it is: a PIC16F684 (src/Pic16f684.hpp) with the .HEX loaded at runtime.
//
// The contract is the Day 10 board's: a 0-5 V input on pin 10 (AN4), a mode select on
// pin 9 (AN5), and a PWM output on pin 5 (CCP1), "quantizes to 1/12th of a volt", on an
// external 20 MHz clock. Any firmware that keeps it runs here: the one this project ships
// (firmware/varimode, embedded), or the course's own, which is third-party and is loaded
// from a path the player gives the module, never carried in this repository. This header
// is the analogue world around the chip, and nothing else.
//
// What the board around the chip is, the course folder does not say (it has the
// firmware and not its schematic), so the analogue stages are assumed, here:

#include <cmath>
#include <cstdint>
#include <string>

#include "../Pic16f684.hpp"

namespace paysched {
namespace varimode {

namespace assumed {
    // The PWM is 19.5 kHz (256 cycles of 200 ns) and its ripple has to be filtered
    // out of a pitch CV. Two cascaded one-pole low-passes at this corner take it to
    // under a millivolt, at the cost of about a millisecond of settling each. The real
    // board's filter is not on record. ASSUMED.
    static const double PWM_FILTER_HZ = 160.0;
    // Nothing conditions the input: the converter's own range, 0 to VDD, is the range.
    // Below ground reads 0 and above 5 V reads full scale. ASSUMED (a real board may
    // have a buffer or a divider ahead of the pin).
}

// The firmware's five modes are the bands of ADRESH on the mode pin (AN5): 0-52 major,
// 53-104 major pentatonic, 105-153 minor, 154-204 minor pentatonic, 205-255 chromatic.
// A real board puts a pot there; the module's SCALE switch puts a voltage at the middle
// of the band instead.
enum Mode { MAJOR = 0, MAJOR_PENT, MINOR, MINOR_PENT, CHROMATIC, kModes };

static const int kModeBandCentre[kModes] = { 26, 78, 129, 179, 230 };

inline double modeVolts(Mode m) {
    return (kModeBandCentre[m] + 0.5) / 256.0 * 5.0;
}

struct Varimode {
    pic16::Pic16f684 pic;
    bool loaded = false;

    Varimode() { setSampleRate(48000.0); }

    /** Programs the flash from Intel HEX text and powers the chip on. */
    bool loadHex(const std::string& text) {
        loaded = pic.loadHex(text);
        powerOn();
        return loaded;
    }

    /** Programs the flash from 14-bit words (the firmware this project ships) and powers on. */
    void loadWords(const uint16_t* words, int n) {
        pic.load(words, n);
        loaded = true;
        powerOn();
    }

    void powerOn() {
        pic.reset();
        pic.clearPwmWindow();
        y1 = y2 = 0.0;
        stable = lastSeen = -1;
        changedFlag = false;
    }

    void setSampleRate(double sr) {
        sampleRate = sr;
        cyclesPerSample = pic16::Pic16f684::kFosc / 4.0 / sr;
        a = 1.0 - std::exp(-2.0 * M_PI * assumed::PWM_FILTER_HZ / sr);
    }

    /** One audio sample: `vin` volts on AN4, the chosen mode on AN5. Returns the
        filtered PWM, volts. */
    double process(double vin, Mode mode) {
        pic.analog[4] = vin;
        pic.analog[5] = modeVolts(mode);
        pic.clearPwmWindow();
        pic.run(cyclesPerSample);
        double v = pic.pwmAverage() * pic16::Pic16f684::kVdd;
        y1 += a * (v - y1);
        y2 += a * (y1 - y2);

        // The note the firmware has set up changes in a few instructions (CCPR1L and
        // the two DC1B bits are separate writes), so a change is only reported once the
        // register has held its new value for a whole sample.
        int duty = pic.pwmDutyRegister();
        changedFlag = false;
        if (duty == lastSeen && duty != stable) {
            if (stable >= 0) changedFlag = true;
            stable = duty;
        }
        lastSeen = duty;
        return y2;
    }

    /** True for the one sample in which the quantized note changed. */
    bool noteChanged() const { return changedFlag; }

    /** The chip has done something the model does not implement. */
    bool unsupported() const { return pic.unsupported; }

    double sampleRate = 48000.0;
    double cyclesPerSample = 104.0;
    double a = 0.01;
    double y1 = 0.0, y2 = 0.0;
    int stable = -1, lastSeen = -1;
    bool changedFlag = false;
};

} // namespace varimode
} // namespace paysched

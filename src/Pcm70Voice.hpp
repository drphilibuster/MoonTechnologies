// Pcm70Voice.hpp: one complete PCM 70: jacks -> input filter -> converter -> {master Z80 + slave Z80 + HSP} -> DAC -> output filter -> mix -> jacks.
//
// The machine's own clock (13 MHz / 384 = 33854.1667 Hz) drives everything inside; the host rate only matters to the two polyphase stages, the dry path and the
// mix. Each host sample pushed in lets the machine take as many whole samples as its look-ahead allows; each sample the machine takes is one HSP sample (the
// program in the writable control store, rebuilt whenever the slave has written to it) and 96 clocks of both Z80s, with the level detector's code on the HEADRM
// port. The audio is delayed by `latencySamples()` host samples (the look-ahead the two band-limited stages need); the dry path carries the same delay so
// dry and wet keep the relative timing the real unit has.
//
// Levels (Steps 31-33): digital 1.0 = converter full scale = `fullScaleVolts` at the jack (Rack default 5 V peak). `inputGain` is the input knob (1.0 =
// maximum), `outputPad` the output level switch (1.0 for +4, 0.0575 for -20 dB mode: -24.7 dB).
#pragma once
#include <memory>
#include "Pcm70Machine.hpp"
#include "Pcm70Hsp.hpp"
#include "Pcm70Analog.hpp"
#include "Pcm70Control.hpp"
#include "Pcm70Keys.hpp"
#include "Pcm70Midi.hpp"

namespace pcm70 {

class Voice {
public:
    double fullScaleVolts = 5.0, inputGain = 1.0, outputPad = 1.0;
    Machine machine; Hsp hsp; analog::LevelDetector detector; analog::MixStage mix;
    Control control; Keys keys; Midi midi;           // the host's hands on the firmware: parameter calls, front-panel keys, the MIDI UART
    uint64_t underruns = 0;
    double wetL = 0.0, wetR = 0.0;                   // the wet signal after the output filter, before the mix (volts, after outputPad): for the WET jacks

    Voice() : control(machine), keys(machine), midi(machine) { dc[0].init(); dc[1].init(); }
    Voice(const Voice&) = delete; Voice& operator=(const Voice&) = delete;

    // The user's images: U62/U95 firmware, U67 opcode ROM (8 KB), U48 (512 B) and U49 (32 B) sequencer PROMs. Copied; the machine cold-starts (RAM kept: a restored
    // battery image survives).
    void load(const uint8_t* u62, size_t n62, const uint8_t* u95, size_t n95, const uint8_t* u67, const uint8_t* u48, const uint8_t* u49, bool v3 = false) {
        machine.load(u62, n62, u95, n95); cycTarget = machine.m.cyc; machine.hook = &control; control.setV3(v3);
        memcpy(opcode, u67, sizeof opcode); hsp.setProms(u48, u49); hsp.reset();
        detector = analog::LevelDetector(); dc[0] = analog::DcBlock(); dc[1] = analog::DcBlock(); dc[0].init(); dc[1].init();
        lastWrites = ~0ul; lastCopies = -1; syncProgram();
        hostIndex = 0; underruns = 0; if (configured) startStages();
    }
    // Build the rate conversions for a host rate (costs a few milliseconds; call when the rate changes).
    void configure(double hostRate) { host = hostRate; configured = true; mix.init(hostRate); startStages(); }
    int latencySamples() const { return (int)delayHs; }
    // the battery RAM (8 KB: registers, configuration, working variables; no ROM data): save it in the patch, give it back before load() to resume
    const uint8_t* batteryRam() const { return machine.mram; }
    void setBatteryRam(const uint8_t* image8k) { memcpy(machine.mram, image8k, sizeof machine.mram); }
    void clearMemory() { memset(machine.mram, 0, sizeof machine.mram); }
    // switch the unit off and on again: the RAM image stays (set it first for a restore, clear it first for CLEAR MEMORY); the firmware boots again (about 9 s of machine time)
    void powerCycle() {
        machine.powerCycle(); hsp.reset(); detector = analog::LevelDetector(); dc[0] = analog::DcBlock(); dc[1] = analog::DcBlock(); dc[0].init(); dc[1].init();
        control.reset(); keys.reset(); midi.reset(); lastWrites = ~0ul; lastCopies = -1; syncProgram(); cycTarget = machine.m.cyc; hostIndex = 0; if (configured) startStages();
    }
    double hostRate() const { return host; }

    void process(double vin, double& outL, double& outR) {
        const double xd = vin / fullScaleVolts * inputGain;
        in.push(xd); dry[(size_t)(hostIndex % (long long)dry.size())] = xd;
        double x; while (in.pull(x)) step(x);
        double wl = 0.0, wr = 0.0, d = 0.0;
        if (hostIndex >= delayHs) { if (!out.pull(wl, wr)) { underruns++; wl = wr = 0.0; } d = dry[(size_t)((hostIndex - delayHs) % (long long)dry.size())]; }
        hostIndex++;
        mix.setCodes(machine.mixWet >= 0 && machine.mixDry >= 0 ? machine.mixWet : 255, machine.mixWet >= 0 && machine.mixDry >= 0 ? machine.mixDry : 255);
        double ml, mr; mix.process(d, wl, wr, ml, mr);
        outL = ml * fullScaleVolts * outputPad; outR = mr * fullScaleVolts * outputPad;
        wetL = wl * fullScaleVolts * outputPad; wetR = wr * fullScaleVolts * outputPad;
    }

private:
    analog::InputStage in; analog::OutputStage out; analog::DcBlock dc[2];
    uint64_t cycTarget = 0;
    std::vector<double> dry; long long hostIndex = 0, delayHs = 0; double host = 48000.0; bool configured = false;
    uint8_t opcode[0x2000] = {}; uint64_t lastWrites = ~0ul; int lastCopies = -1;

    void startStages() {
        in.init(host);
        const double fh = (double)std::llround(host);
        delayHs = (long long)std::ceil((double)in.A + (6 + 2) * fh / analog::FS) + 1;                      // look-ahead of the two stages (output kernel pre-ring = 6 machine samples)
        out.init(host, delayHs); dry.assign((size_t)delayHs + 1, 0.0); hostIndex = 0;
    }
    void syncProgram() {
        if (machine.wcsWrites == lastWrites && machine.copyEvents == lastCopies) return;
        lastWrites = machine.wcsWrites; lastCopies = machine.copyEvents;
        uint32_t w[128]; pcm70BuildProgram(machine.wcs, opcode, machine.lastNzPage, w); hsp.setProgram(w);
    }
    void step(double x) {                                  // one machine sample
        machine.headrm = detector.update(x);
        double l, r; hsp.process(x, l, r);
        cycTarget += 96; machine.runTo(cycTarget);                                  // absolute: a relative run(96) would overshoot to 120 clocks per sample
        syncProgram();
        keys.tick(); midi.tick();
        out.push(dc[0].process(l), dc[1].process(r));
    }
};

}

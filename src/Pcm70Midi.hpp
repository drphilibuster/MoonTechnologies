// Pcm70Midi.hpp: what the host says to the firmware's MIDI UART (Steps 37, 40 and 42).
//
// The UART takes one byte per 320 us and the firmware's main loop serves about 100 events per second in total (an edit costs the same as a MIDI message), so
// traffic is shaped here rather than queued without limit:
//   * controller-like values (CC, pitch bend, channel pressure, note + velocity) are COALESCED: only the latest value of each is ever pending, and at most `rate`
//     messages per second leave, round-robin;
//   * everything that must not be merged or reordered (program change, sysex, bank import) goes through an ordered queue with a gap after each message: a bulk
//     dump needs >= 0.06 s after its last byte before the next message starts (0.05 s loses sync, Step 54), so the gap is 0.15 s;
//   * MIDI clock (V3 firmware only: V2 ignores F8) is generated from the host's clock edges: each edge schedules the F8 pulses of the NEXT interval, spread evenly at
//     24 pulses per quarter note (the firmware's jitter filter swallows tempo changes under ~1.4 ms per beat).
// Controller sources a firmware patch can use: CC 0-31 and 64-94, pitch bend, channel pressure (aftertouch), note number, note velocity (and the soft knob,
// which is a front-panel parameter, not MIDI).
#pragma once
#include <cstdint>
#include <deque>
#include <map>
#include <vector>
#include "Pcm70Machine.hpp"

namespace pcm70 {

class Midi {
public:
    typedef std::vector<uint8_t> Bytes;
    double rate = 60.0;                      // controller messages per second, all together
    double sysexGap = 0.15;                  // seconds after a bulk message
    double orderedGap = 0.01;                // seconds after a program change and the like
    int channel = 0;                         // 0-15, must match the firmware's MIDI channel (default: channel 1, OMNI off)

    explicit Midi(Machine& m) : M(m) {}

    // ---- coalesced controllers --------------------------------------------------------------------------------------------------------------------
    void cc(int number, int value) { const int st = 0xB0 | channel; set((st << 8) | clamp7(number), st, clamp7(number), clamp7(value), 3); }
    void pitchBend(int value14) { value14 = value14 < 0 ? 0 : (value14 > 16383 ? 16383 : value14); const int st = 0xE0 | channel; set(st << 8, st, value14 & 127, value14 >> 7, 3); }
    void aftertouch(int value) { const int st = 0xD0 | channel; set(st << 8, st, clamp7(value), 0, 2); }
    // note number and velocity are separate sources for patches; a note-on carries both
    void note(int number, int velocity) { const int st = 0x90 | channel; set(st << 8, st, clamp7(number), clamp7(velocity), 3); }
    // ---- ordered ----------------------------------------------------------------------------------------------------------------------------------
    void programChange(int n) { ordered.push_back({ { (uint8_t)(0xC0 | channel), (uint8_t)clamp7(n) }, orderedGap }); }
    void sysex(const Bytes& b) { ordered.push_back({ b, sysexGap }); }
    void noteOff(int number) { ordered.push_back({ { (uint8_t)(0x80 | channel), (uint8_t)clamp7(number), 0 }, orderedGap }); }
    // register data (167 bytes) in the stored form: files it as register n (0-49) without loading it; the active form (target 50) loads and runs it
    static Bytes bulk(const uint8_t* data167, int target, bool stored, int chan = 0) {
        Bytes m = { 0xF0, 0x06, 0x00, (uint8_t)((stored ? 0x10 : 0) | chan), (uint8_t)target, 0x02, 0x4E }; unsigned sum = 0;
        for (int i = 0; i < 167; i++) { m.push_back(data167[i] >> 4); m.push_back(data167[i] & 15); sum += (data167[i] >> 4) + (data167[i] & 15); }
        m.push_back(sum & 0x7F); m.push_back(0xF7); return m;
    }
    // ---- clock (V3) -------------------------------------------------------------------------------------------------------------------------------
    // One call per incoming clock edge at `ppqnIn` edges per quarter note; the pulses for the following interval are spread over it.
    void clockEdge(int ppqnIn) {
        const uint64_t now = M.m.cyc;
        if (haveEdge) {
            const uint64_t interval = now - lastEdge; const int per = 24 / (ppqnIn < 1 ? 1 : ppqnIn) < 1 ? 1 : 24 / (ppqnIn < 1 ? 1 : ppqnIn);
            for (int i = 0; i < per; i++) pulses.push_back(now + interval * (uint64_t)i / (uint64_t)per);
        } else pulses.push_back(now);
        haveEdge = true; lastEdge = now;
    }
    void clockStart() { pulses.clear(); haveEdge = false; startPending = true; }          // FA: restart the firmware's period measurement
    void reset() { ordered.clear(); ctl.clear(); pulses.clear(); haveEdge = false; startPending = false; nextSend = 0; }
    bool pending() const { return !ordered.empty() || anyDirty() || !pulses.empty() || !M.midi.empty(); }

    void tick() {
        const uint64_t now = M.m.cyc;
        if (startPending) { M.midi.push_front(0xFA); startPending = false; }
        while (!pulses.empty() && pulses.front() <= now) { M.midi.push_front(0xF8); pulses.pop_front(); }      // real-time bytes may interleave anywhere
        if (!M.midi.empty() || now < nextSend) return;
        if (!ordered.empty()) { const Msg& m = ordered.front(); M.sendMidi(m.bytes.data(), m.bytes.size()); nextSend = now + gapCycles(m.gap, m.bytes.size()); ordered.pop_front(); return; }
        const size_t n = ctl.size(); if (!n) return;
        for (size_t i = 0; i < n; i++) {
            auto it = ctl.begin(); std::advance(it, (rr + i) % n); Ctl& c = it->second; if (!c.dirty) continue;
            uint8_t b[3] = { (uint8_t)c.status, (uint8_t)c.d1, (uint8_t)c.d2 }; M.sendMidi(b, c.len); c.dirty = false; rr = (rr + i + 1) % n;
            nextSend = now + (uint64_t)(3.25e6 / rate); return;
        }
    }

private:
    struct Msg { Bytes bytes; double gap; };
    struct Ctl { int status = 0, d1 = 0, d2 = 0, len = 3; bool dirty = false; };
    Machine& M; std::deque<Msg> ordered; std::map<int, Ctl> ctl; size_t rr = 0; uint64_t nextSend = 0;
    std::deque<uint64_t> pulses; bool haveEdge = false, startPending = false; uint64_t lastEdge = 0;
    static int clamp7(int v) { return v < 0 ? 0 : (v > 127 ? 127 : v); }
    uint64_t gapCycles(double gap, size_t bytes) const { return (uint64_t)(gap * 3.25e6) + (uint64_t)(bytes * 1040); }
    bool anyDirty() const { for (const auto& kv : ctl) if (kv.second.dirty) return true; return false; }
    void set(int key, int status, int d1, int d2, int len) { Ctl& c = ctl[key]; c.status = status; c.d1 = d1; c.d2 = d2; c.len = len; c.dirty = true; }
};

}

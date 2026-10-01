// Pcm70Machine.hpp: the PCM 70's two-processor control machine (master Z80 U62 + slave Z80 U95), peripherals per the service manual (4.4).
// Ported from the research tree's emu/pcm70.cpp with the globals and debug environment hooks removed; behaviour is the same (Steps 22-42).
//   master: 0000-7FFF EPROM U62, 8000-9FFF battery SRAM (the only RAM).   slave: 0000-3FFF U95, 4000-5FFF SRAM, 8000-83FF WCS (writable control store).
//   master I/O: 00-40 display scan, 50 STATIN, 60 STATOUT, 70 HEADRM, 80 SLVRST, 90 MCPRT, A0 RDDATA, B0 RDSTAT, C0 WRDATA, D0 WRCONT, E0 MPRTIN, F0 MPRTOUT.
//   slave I/O: 00 COPY (bits 1-6 = U67 opcode page, bit 0 strobe), 20 SPRTIN, 30 SPRTOUT, 50 SCPRT.
// Both CPUs run 3.25 MHz = 96 clocks per audio sample (33854.1667 Hz); boot to a live program takes ~9 s of machine time.
// Nothing here holds ROM bytes of its own: load() copies the user's images in.
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <deque>
#include <vector>
#include <string>
#include "Pcm70Locate.hpp"
#include "Pcm70Drive.hpp"
extern "C" {
#include "../vendor/z80/z80.h"
}
namespace pcm70 {

struct Machine;
// Something that watches the master Z80 between instructions (the firmware-call engine in Pcm70Control.hpp).
struct Hook { virtual ~Hook() {} virtual void step(Machine& m) = 0; };

struct Machine {
    static constexpr double HZ = 3.25e6;
    static const unsigned long IRQ_PERIOD = 765;      // 8 * 128 * 230 ns at 3.25 MHz, truncated as the research harness did
    static const unsigned long MIDI_PERIOD = 1040;    // 31250 baud, 10 bits = 320 us
    static const unsigned SCAN_PHASE = 0x9B3B;        // front-panel scan ISR phase (low nibble), identical in V2 and V3 (Step 41)

    z80 m, s;
    uint8_t mrom[0x8000], mram[0x2000], srom[0x4000], sram[0x2000], wcs[0x400];
    // one-byte handshake ports
    bool mdav = false; uint8_t m2s = 0;               // master -> slave latch
    bool sdav = false; uint8_t s2m = 0;               // slave -> master latch
    // MIDI in (1602-style UART): bytes are delivered one per 320 us
    std::deque<uint8_t> midi; bool da = false; uint8_t rxbyte = 0; unsigned long nextMidi = 0;
    // opcode ROM page chosen by the slave's copy strobes (bits 1-6 of the COPY port); lastNzPage = the last non-zero one
    int copyPage = -1, lastNzPage = 0; unsigned long wcsWrites = 0; int copyEvents = 0;
    std::vector<uint8_t>* m2sTrace = nullptr;               // if set, every byte the master sends the slave is appended (FF idx value triples)
    unsigned long slaveLoads = 0; uint8_t prevM2s = 0;      // the master's FF 80 command = download a program to the slave (power-up, program/register load, sysex load)
    // host inputs
    uint8_t headrm = 0;                               // port 70: level-detector ADC code (host sets it from the input peak)
    uint8_t keyCol[8] = {0, 0, 0, 0, 0, 0, 0, 0};     // key matrix: STATIN bits for each column while a key is down
    uint8_t direct = 0;                               // the direct switch bits read on even scan phases (mask 0xC5; bit7 = BYPASS footswitch)
    // host outputs
    int mixWet = -1, mixDry = -1;                     // U22 mix DAC codes (0.392 dB/LSB), selected by STATOUT bit 1
    unsigned long irqNext = 0;
    Hook* hook = nullptr;                             // if set, called after every master instruction
    Pcm70Loc loc;

    Machine() { memset(&m, 0, sizeof m); memset(&s, 0, sizeof s); memset(mrom, 0, sizeof mrom); memset(mram, 0, sizeof mram); memset(srom, 0, sizeof srom); memset(sram, 0, sizeof sram); memset(wcs, 0, sizeof wcs); }
    Machine(const Machine&) = delete; Machine& operator=(const Machine&) = delete;

    // Install the two program images (any size up to the chip's) and cold-start both CPUs. RAM is left as it is, so a restored battery image survives a power cycle.
    void load(const uint8_t* u62, size_t n62, const uint8_t* u95, size_t n95) {
        memset(mrom, 0, sizeof mrom); memset(srom, 0, sizeof srom);
        memcpy(mrom, u62, n62 < sizeof mrom ? n62 : sizeof mrom); memcpy(srom, u95, n95 < sizeof srom ? n95 : sizeof srom);
        loc = pcm70Locate(mrom, sizeof mrom);
        powerCycle();
    }
    void powerCycle() {
        z80_init(&m); m.userdata = this; m.read_byte = mrd; m.write_byte = mwr; m.port_in = mpin; m.port_out = mpout;
        z80_init(&s); s.userdata = this; s.read_byte = srd; s.write_byte = swr; s.port_in = spin; s.port_out = spout;
        mdav = sdav = false; m2s = s2m = 0; midi.clear(); da = false; irqNext = 0; nextMidi = 0;
        memset(sram, 0, sizeof sram); memset(wcs, 0, sizeof wcs); copyPage = -1; lastNzPage = 0; wcsWrites = 0; copyEvents = 0; slaveLoads = 0; prevM2s = 0;
    }
    double seconds() const { return m.cyc / HZ; }
    void sendMidi(const uint8_t* p, size_t n) { for (size_t i = 0; i < n; i++) midi.push_back(p[i]); }

    // Run the machine for `cycles` master clocks (both CPUs, kept in lock-step to within one slice).
    void run(unsigned long cycles) { runTo(m.cyc + cycles); }
    // Run until the master's clock reaches `end` (absolute). Instructions are executed in slices of 40 clocks, so the clock may finish up to 39 clocks past `end`;
    // callers that step in small pieces (one audio sample = 96 clocks) must use absolute targets, or each call would overshoot and the machine would run ~25% fast.
    void runTo(unsigned long end) {
        while (m.cyc < end) {
            if (m.cyc >= irqNext) { z80_gen_int(&m, 0xFF); z80_gen_int(&s, 0xFF); irqNext += IRQ_PERIOD; }
            if (!da && !midi.empty() && m.cyc >= nextMidi) { rxbyte = midi.front(); midi.pop_front(); da = true; nextMidi = m.cyc + MIDI_PERIOD; }
            unsigned long slice = m.cyc + 40;
            while (m.cyc < slice) { z80_step(&m); if (hook) hook->step(*this); }
            while (s.cyc < m.cyc) z80_step(&s);
        }
    }
    void runSeconds(double sec) { run((unsigned long)(sec * HZ)); }

    // The 16-digit display as the firmware wrote it: (segment0, segment1) per digit, segment1 bit 0 = decimal point.
    // Decoded through the firmware's own font tables, located by signature in the user's ROM.
    std::string displayText() const {
        uint8_t s0[16], s1[16];
        for (int i = 0; i < 16; i++) { s0[i] = mram[0x9C03 - 0x8000 + i]; s1[i] = mram[0x9BF3 - 0x8000 + i]; }
        return decodeDisplay(s0, s1);
    }
    // 16 digits of (segment0, segment1) -> text through the firmware's own font tables; a blank digit (both zero) is a space, an unknown pattern '?'
    std::string decodeDisplay(const uint8_t* seg0, const uint8_t* seg1) const {
        static const uint8_t SIG0[16] = { 0x00, 0xc7, 0x00, 0xff, 0xa3, 0x09, 0xff, 0x00, 0x40, 0x08, 0x4a, 0x22, 0x08, 0x02, 0x00, 0x08 };
        static const uint8_t SIG1[16] = { 0x00, 0x00, 0x50, 0xff, 0x96, 0x22, 0xff, 0x40, 0x20, 0x08, 0x2c, 0x14, 0x00, 0x04, 0x01, 0x20 };
        const long o0 = find(SIG0, 16), o1 = find(SIG1, 16); std::string out;
        if (o0 < 0 || o1 < 0) return out;
        for (int i = 0; i < 16; i++) {
            char ch = '?';
            if (seg0[i] == 0 && (seg1[i] & 0xFE) == 0) ch = ' ';
            else for (int c = 0; c < 64; c++) if (mrom[o0 + c] == seg0[i] && (mrom[o1 + c] & 0xFE) == (seg1[i] & 0xFE)) { ch = (char)(0x20 + c); break; }
            out += ch; if (seg1[i] & 1) out += '.';
        }
        return out;
    }
    uint8_t ramAt(unsigned a) const { return mram[a - 0x8000]; }

private:
    long find(const uint8_t* sig, size_t n) const {
        for (size_t i = 0; i + n <= sizeof mrom; i++) if (memcmp(mrom + i, sig, n) == 0) return (long)i;
        return -1;
    }
    static uint8_t mrd(void* u, uint16_t a) { Machine* M = (Machine*)u; if (a < 0x8000) return M->mrom[a]; if (a < 0xA000) return M->mram[a - 0x8000]; return 0xFF; }
    static void mwr(void* u, uint16_t a, uint8_t v) { Machine* M = (Machine*)u; if (a >= 0x8000 && a < 0xA000) M->mram[a - 0x8000] = v; }
    uint8_t statin() const {
        static const uint8_t tab[16] = { 0, 8, 1, 9, 2, 10, 3, 11, 4, 12, 5, 13, 6, 14, 7, 15 };
        int C = tab[mram[SCAN_PHASE - 0x8000] & 15];
        if (C >= 8) return keyCol[C & 7];
        if (!(C & 1)) return direct;
        return 0;
    }
    static uint8_t mpin(z80* z, uint8_t p) {
        Machine* M = (Machine*)z->userdata;
        switch (p & 0xF0) {
        case 0x50: return M->statin();
        case 0x70: return M->headrm;
        case 0x90: return 0x08 | (M->mdav ? 1 : 0) | (M->sdav ? 2 : 0);            // PFAIL high, BLOW low
        case 0xA0: M->da = false; return M->rxbyte;
        case 0xB0: return 0x20 | (M->da ? 1 : 0);                                  // TBRE, DA
        case 0xE0: M->sdav = false; return M->s2m;
        }
        return 0xFF;
    }
    int mixch = 0;
    static void mpout(z80* z, uint8_t p, uint8_t v) {
        Machine* M = (Machine*)z->userdata;
        switch (p & 0xF0) {
        case 0x60: M->mixch = v & 2; break;
        case 0x00: if (M->mixch) M->mixWet = v; else M->mixDry = v; break;
        case 0x80: {                                                               // SLVRST/: reset the slave CPU
            z80 keep = M->s; z80_init(&M->s);
            M->s.userdata = keep.userdata; M->s.read_byte = keep.read_byte; M->s.write_byte = keep.write_byte; M->s.port_in = keep.port_in; M->s.port_out = keep.port_out;
            M->s.cyc = M->m.cyc; M->mdav = M->sdav = false; break; }
        case 0xF0: M->m2s = v; M->mdav = true; if (M->prevM2s == 0xFF && v == 0x80) M->slaveLoads++; M->prevM2s = v; if (M->m2sTrace) M->m2sTrace->push_back(v); break;
        }
    }
    static uint8_t srd(void* u, uint16_t a) {
        Machine* M = (Machine*)u;
        if (a < 0x4000) return M->srom[a];
        if (a < 0x6000) return M->sram[a - 0x4000];
        if (a >= 0x8000 && a < 0x8400) return M->wcs[a - 0x8000];
        return 0xFF;
    }
    static void swr(void* u, uint16_t a, uint8_t v) {
        Machine* M = (Machine*)u;
        if (a >= 0x4000 && a < 0x6000) M->sram[a - 0x4000] = v;
        else if (a >= 0x8000 && a < 0x8400) { M->wcs[a - 0x8000] = v; M->wcsWrites++; }
    }
    static uint8_t spin(z80* z, uint8_t p) {
        Machine* M = (Machine*)z->userdata;
        switch (p & 0xF0) {
        case 0x20: M->mdav = false; return M->m2s;
        case 0x50: return 0x0C | (M->mdav ? 1 : 0) | (M->sdav ? 2 : 0);
        }
        return 0xFF;
    }
    static void spout(z80* z, uint8_t p, uint8_t v) {
        Machine* M = (Machine*)z->userdata;
        switch (p & 0xF0) {
        case 0x00: if (v & 1) { M->copyPage = v >> 1; if (v >> 1) M->lastNzPage = v >> 1; M->copyEvents++; } break;
        case 0x30: M->s2m = v; M->sdav = true; break;
        }
    }
};
}

// Pcm70BatRam.hpp: the PCM 70's battery RAM image (master RAM 0x8000-0x9FFF, 8 KB), from the research tree's tools/batram.py (Step 42).
//   register n (0..49 = row*10+col of the 5x10 register matrix) at 0x8001 + 123*n; n = 50, the active program image (the target of sysex p=50), at 0x9807.
//   A 123-byte record = the first 47 sysex data bytes verbatim (type, 2 bytes, 16-char name, MIDI patch arrays...; type 0 = UNUSED) + the 60 ten-bit
//   parameter words packed as 15 x [4 low bytes + 1 byte holding the four 2-bit highs, word 0 in bits 0-1] + 1 zero byte.
//   Zero RAM is a factory-fresh machine. The image holds the user's own registers and no ROM data.
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
namespace pcm70 {
namespace batram {
static const size_t SLOT = 123, HEADER = 47, WORDS = 60, SYSEX_DATA = 167, RAM_SIZE = 0x2000;
static const unsigned BASE = 0x8001, ACTIVE = 0x9807, MIDI_CHAN = 0x98B2, OMNI = 0x98B3, PGM_CHNG = 0x98B8, AUTO_LOAD = 0x993B;
inline unsigned slotAddr(int n) { return BASE + (unsigned)SLOT * (unsigned)n; }            // n = 0..49 (50 = ACTIVE is not on the stride)
// 167-byte sysex register data -> 123-byte RAM record
inline void pack(const uint8_t* data, uint8_t* rec) {
    memcpy(rec, data, HEADER);
    unsigned w[WORDS];
    for (size_t k = 0; k < WORDS; k++) w[k] = data[HEADER + 2 * k] | (unsigned)(data[HEADER + 1 + 2 * k] & 3) << 8;
    uint8_t* o = rec + HEADER;
    for (size_t g = 0; g < 15; g++) {
        unsigned hi = 0;
        for (size_t i = 0; i < 4; i++) { *o++ = (uint8_t)(w[4 * g + i] & 255); hi |= (w[4 * g + i] >> 8) << (2 * i); }
        *o++ = (uint8_t)hi;
    }
    *o = 0;
}
// 123-byte RAM record -> 167-byte sysex register data
inline void unpack(const uint8_t* rec, uint8_t* data) {
    memcpy(data, rec, HEADER);
    for (size_t g = 0; g < 15; g++) {
        const uint8_t* low = rec + HEADER + 5 * g; unsigned hi = rec[HEADER + 5 * g + 4];
        for (size_t i = 0; i < 4; i++) {
            unsigned w = low[i] | ((hi >> (2 * i)) & 3) << 8; size_t k = 4 * g + i;
            data[HEADER + 2 * k] = (uint8_t)(w & 255); data[HEADER + 1 + 2 * k] = (uint8_t)(w >> 8);
        }
    }
}
// store a register (n = 0..49) or the active image (n = 50) into an 8 KB RAM image
inline void put(uint8_t* ram, int n, const uint8_t* data) { pack(data, ram + ((n == 50 ? ACTIVE : slotAddr(n)) - 0x8000)); }
inline void get(const uint8_t* ram, int n, uint8_t* data) { unpack(ram + ((n == 50 ? ACTIVE : slotAddr(n)) - 0x8000), data); }
}
}

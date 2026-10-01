// Pcm70Locate.hpp (from the research tree's fwhooks.hpp): locate the firmware routines the host drives (Step 38/39) by byte signature in the master ROM, so no address is hard-coded per ROM version.
//   idle = the main loop's idle epilogue (XOR A / LD HL,13 / ADD HL,SP / LD SP,HL / POP IX / RET): safe point to inject a call
//   edit = edit(row=HL, col=DE, delta=BC), sel = select cell (HL=row, DE=col) -> vB6 word index, vB7 slave index, vB8 descriptor pointer, vBA/vBB = col/row
//   wbase = RAM base of the 10-bit parameter word image (word k at wbase+2k)
#pragma once
#include <cstdint>
#include <cstddef>
struct Pcm70Loc { bool ok = false; unsigned idle = 0, edit = 0, sel = 0, vB6 = 0, vB7 = 0, vB8 = 0, vBA = 0, vBB = 0, wbase = 0; };
inline bool pcm70Match(const uint8_t* r, size_t n, size_t at, const int* pat, size_t pl) {
    if (at + pl > n) return false;
    for (size_t i = 0; i < pl; i++) if (pat[i] >= 0 && r[at + i] != (uint8_t)pat[i]) return false;
    return true;
}
template <size_t N> inline long pcm70Find(const uint8_t* r, size_t n, const int (&pat)[N], size_t from = 0, size_t to = (size_t)-1) {
    if (to > n) to = n;
    for (size_t i = from; i + N <= to; i++) if (pcm70Match(r, n, i, pat, N)) return (long)i;
    return -1;
}
inline Pcm70Loc pcm70Locate(const uint8_t* r, size_t n) {
    Pcm70Loc L;
    static const int idle[] = { 0xaf, 0x21, 0x0d, 0x00, 0x39, 0xf9, 0xdd, 0xe1, 0xc9 };
    static const int edit[] = { 0xdd, 0xe5, 0xc5, 0xd5, 0xe5, 0xdd, 0x21, 0x06, 0x00, 0xdd, 0x39, 0xcd, -1, -1, 0x2a, -1, -1, 0x7c, 0xb5, 0x28, -1, 0xdd, 0x6e, 0xfe, 0xdd, 0x66, 0xff, 0xcd, -1, -1, 0x18, -1 };
    static const int sel[] = { 0xdd, 0xe5, 0xd5, 0xe5, 0xdd, 0x21, 0x04, 0x00, 0xdd, 0x39, 0xe5, 0xe5, 0x7d, 0x32, -1, -1, 0x7b, 0x32, -1, -1, 0x29, 0x29, 0xeb, 0x2a, -1, -1, 0x19, 0xeb, 0x21, 0x18, 0x00, 0x19 };
    static const int zero8[] = { 0x21, 0x00, 0x00, 0x22, -1, -1 };
    static const int b7[] = { 0x23, 0x23, 0x7e, 0x32, -1, -1 };
    static const int b6[] = { 0x23, 0x23, 0x23, 0x7e, 0x32, -1, -1 };
    long i = pcm70Find(r, n, idle); long e = pcm70Find(r, n, edit); long s = pcm70Find(r, n, sel);
    if (i < 0 || e < 0 || s < 0) return L;
    L.idle = (unsigned)i; L.edit = (unsigned)e; L.sel = (unsigned)s;
    L.vBB = r[s + 14] | r[s + 15] << 8; L.vBA = r[s + 18] | r[s + 19] << 8;
    long z = pcm70Find(r, n, zero8, s, s + 120), p7 = pcm70Find(r, n, b7, s, s + 160), p6 = pcm70Find(r, n, b6, s, s + 160);
    if (z < 0 || p7 < 0 || p6 < 0) return L;
    L.vB8 = r[z + 4] | r[z + 5] << 8; L.vB7 = r[p7 + 4] | r[p7 + 5] << 8; L.vB6 = r[p6 + 5] | r[p6 + 6] << 8;
    // word image base: LD A,(vB6) / LD L,A / LD H,0 / ADD HL,HL / EX DE,HL / LD HL,wbase / ADD HL,DE
    for (size_t k = 0; k + 12 < n; k++) if (r[k] == 0x3a && (unsigned)(r[k + 1] | r[k + 2] << 8) == L.vB6 && r[k + 3] == 0x6f && r[k + 4] == 0x26 && r[k + 5] == 0x00 && r[k + 6] == 0x29 && r[k + 7] == 0xeb && r[k + 8] == 0x21 && r[k + 11] == 0x19) { L.wbase = r[k + 9] | r[k + 10] << 8; break; }
    L.ok = L.wbase != 0;
    return L;
}

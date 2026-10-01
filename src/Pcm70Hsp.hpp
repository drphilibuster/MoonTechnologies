// Pcm70Hsp.hpp: the PCM 70's HSP (the 128-word, 3-tick-per-word signal processor) as a compiled core.
//
// The research model (emu/hsp_tick.hpp, kept in tests/Depreciation/hsp_ref.hpp as the reference) steps 384 ticks per sample and runs the sequencer PROMs, an event
// vector and the ALU on every one of them. Nothing in the *control* of a tick depends on audio: it depends on the program words' control fields and on a handful of
// bits of sequencer state carried from tick to tick. So the work is split:
//   decode   once per program load (and again for the first few samples after it, until the carried state settles into its cycle): run the sequencer for the
//            384 ticks of a sample and emit, in order, only the data operations that tick performs (bus select, ALU op, O latch, ACC commit, DRAM read/write,
//            ADC/DAC, R loads). Operations whose result nothing reads before it is overwritten (within the sample; every register is live at the sample's end) are
//            dropped.
//   run      every sample: a straight loop over that list. Memory offsets are looked up per sample (the slave rewrites them continuously: chorus, flange), so a
//            modulated program never forces a decode.
// Two control fields move while a program plays (chorus, flange and hall programs ramp them): a word's coefficient nibble, which selects the ALU operation of
// three ticks through U48, and its register-select bits. A word seen to change one of them is flagged, that operand is decoded as a lookup on the live value
// (ALUV, MOVR), and later changes cost nothing; any other control change is a program load and re-decodes.
// DRAM operations take 5-6 ticks, so those of the last words of a sample finish in the next one: they are carried over with their address (computed at the
// moment they were issued) and run at the same tick the reference would run them. The arithmetic is the reference's, expression for expression, so the output is
// bit-identical (tests/Depreciation/test_hsp.cpp).
//
// The two sequencer PROMs (U48 512 B, U49 32 B) are the user's own dumps, given to setProms().
#pragma once
#include <cstdint>
#include <cstring>
#include <cmath>
#include <vector>

namespace pcm70 {

// The HSP program word: bits 0-15 memory offset (WCS 0x280+i low, 0x180+i high... see program()), bits 16-23 control, bits 24-31 opcode ROM U67.
// WCS bytes live in the slave's control store; the opcode byte comes from U67 page `page` (the slave's last non-zero COPY strobe).
inline void pcm70BuildProgram(const uint8_t* wcs /*0x400*/, const uint8_t* u67 /*0x2000*/, int page, uint32_t out[128]) {
    for (int i = 0; i < 128; i++) out[i] = wcs[0x280 + i] | (uint32_t)wcs[0x180 + i] << 8 | (uint32_t)wcs[0x300 + i] << 16 | (uint32_t)u67[page * 128 + i] << 24;
}

class Hsp {
public:
    static const int SAMPLE_TICKS = 384;

    void setProms(const uint8_t* u48 /*512*/, const uint8_t* u49 /*32*/) { memcpy(U48, u48, 512); memcpy(U49, u49, 32); cache.clear(); }

    // Install a program. Control fields (the top 16 bits of each word) change only when a program is loaded; the memory offsets (low 16 bits) change continuously.
    void setProgram(const uint32_t* w) {
        bool load = !haveProgram, flagged = false;
        for (int i = 0; i < 128; i++) {
            const uint32_t diff = (w[i] ^ words[i]) >> 16;
            if (diff & ~0xCFu) load = true;
            else {
                if ((diff & 0x0F) && !isVar[i]) { isVar[i] = true; flagged = true; }
                if ((diff & 0xC0) && !isVarRa[i]) { isVarRa[i] = true; flagged = true; }
            }
            words[i] = w[i]; off[i] = (uint16_t)(w[i] & 0xFFFF); nibs[i] = (uint8_t)((w[i] >> 16) & 15); ras[i] = (uint8_t)((w[i] >> 22) & 3);
        }
        haveProgram = true;
        if (load) { memset(isVar, 0, sizeof isVar); memset(isVarRa, 0, sizeof isVarRa); }
        if (load || flagged) cache.clear();
    }

    void reset() {
        mem.assign(65536, 0.0); for (int i = 0; i < NREG; i++) r[i] = 0.0;
        pos = 0; cs = CS(); ncarry = 0; ndac = 0;
    }

    void process(double in, double& outL, double& outR) {
        const Decoded* d = find();
        outL = outR = 0.0; ndac = 0; curIn = in; L = &outL; Rr = &outR;
        run(*d);
        // events issued in this sample that complete in the next one
        int n = 0;
        for (size_t i = 0; i < d->carryOut.size(); i++) {
            const CarryDesc& c = d->carryOut[i];
            carry[n].kind = c.kind; carry[n].wa = c.wa; carry[n].due = c.due; carry[n].addr = (uint16_t)(pos + off[c.w]); n++;
        }
        ncarry = n;
        cs = d->end; pos++;
    }

    double reg(int i) const { return r[i]; }               // R0-R3 = 0-3, ACC 4, O 5, A 6, P 7
    double peek(int a) const { return mem[a & 0xFFFF]; }
    uint16_t position() const { return pos; }
    size_t opCount() const { return cache.empty() ? 0 : cache.back().ops.size(); }
    uint64_t decodes = 0;                              // how many times a sample had to be decoded (diagnostic)

private:
    enum { R0 = 0, ACC = 4, O = 5, A = 6, P = 7, T0 = 8, T1 = 9, T2 = 10, NREG = 11 };
    enum Op : uint8_t { MOVR, MOV, SAT, HALF, ADD, SUB, NEG, ZERO, ALU34, ALUV, LD, ST, IN, DAC, CARRYIN };
    struct Mop { uint8_t t, d, a, b; uint16_t w, x; };
    struct CS {                                            // sequencer state carried from sample to sample
        int st = 0, c49 = 0xE9, c48 = 0xF7, c48b = 0, c48w = 0, scq0 = 0, scq1 = 0, xh0 = 1, xh1 = 1, prevX = 0;     // c48 = -1: set by a flagged word's nibble, see c48b (U48 address without it) and c48w (the word)
        bool operator==(const CS& o) const { return st == o.st && c49 == o.c49 && c48 == o.c48 && c48b == o.c48b && c48w == o.c48w && scq0 == o.scq0 && scq1 == o.scq1 && xh0 == o.xh0 && xh1 == o.xh1 && prevX == o.prevX; }
    };
    struct CarryDesc { uint8_t kind, wa, due; uint16_t w; };
    struct Carry { uint8_t kind, wa, due; uint16_t addr; };
    struct Decoded { CS start, end; std::vector<Mop> ops; std::vector<CarryDesc> carryOut; };

    uint8_t U48[512] = {}, U49[32] = {};
    uint32_t words[128] = {}; uint16_t off[128] = {}; uint8_t nibs[128] = {}, ras[128] = {}; bool isVar[128] = {}, isVarRa[128] = {}; bool haveProgram = false;
    std::vector<double> mem = std::vector<double>(65536, 0.0);
    double r[NREG] = {};
    uint16_t pos = 0; CS cs;
    Carry carry[16]; int ncarry = 0;
    int ndac = 0; double curIn = 0, *L = nullptr, *Rr = nullptr;
    std::vector<Decoded> cache;                            // decodes for this program, keyed by their start state (most recent last)

    static double sat(double v) { return v < -1.0 ? -1.0 : (v > 0.99997 ? 0.99997 : v); }
    static double logicop34(double a, double b) {          // ~A & B on 18-bit two's complement words
        auto q = [](double v) { long i = std::lround(v * 131072.0); if (i > 131071) i = 131071; if (i < -131072) i = -131072; return i; };
        long x = ~q(a) & q(b); x &= 0x3FFFF; if (x & 0x20000) x -= 0x40000; return x / 131072.0;
    }

    const Decoded* find() {
        for (size_t i = 0; i < cache.size(); i++) if (cache[i].start == cs) { if (i + 1 != cache.size()) { Decoded t = std::move(cache[i]); cache.erase(cache.begin() + i); cache.push_back(std::move(t)); } return &cache.back(); }
        if (cache.size() >= 4) cache.erase(cache.begin());
        cache.push_back(decode(cs)); decodes++;
        return &cache.back();
    }

    // one event of the reference model: 0 DRAM read -> R, 1 DRAM write of O, 2 ADC -> R, 3 DAC <- O, 4 R <- O
    void doEvent(int kind, int wa, uint16_t addr) {
        switch (kind) {
        case 0: r[wa] = mem[addr] + 0.0; break;
        case 1: mem[addr] = r[O]; break;
        case 2: r[wa] = curIn; break;
        case 4: r[wa] = r[O]; break;
        default: if (ndac == 0) *L = r[O]; else if (ndac == 1) *Rr = r[O]; ndac++; break;
        }
    }

    void run(const Decoded& d) {
        double* g = r; const uint16_t* of = off; const uint16_t ps = pos;
        for (const Mop *m = d.ops.data(), *e = m + d.ops.size(); m < e; ++m) {
            switch (m->t) {
            case MOV: g[m->d] = g[m->a]; break;
            case MOVR: g[m->d] = g[ras[m->w]]; break;                 // the register select of a word whose select bits move (chorus programs swap it)
            case SAT: g[m->d] = sat(g[m->a]); break;
            case HALF: g[m->d] = g[m->a] / 2; break;
            case ADD: g[m->d] = g[m->a] + g[m->b]; break;
            case SUB: g[m->d] = g[m->a] - g[m->b]; break;
            case NEG: g[m->d] = -g[m->b]; break;
            case ZERO: g[m->d] = 0.0; break;
            case ALU34: g[m->d] = 0.5 * logicop34(g[m->a], g[m->b]); break;
            case ALUV: {                                   // the ALU operation selected by U48 from the live coefficient nibble
                const int e48 = U48[m->x | nibs[m->w] << 2];
                const int F = (e48 >> 3 & 1) << 2 | (e48 >> 4 & 1) << 1 | (e48 >> 5 & 1), Cc = e48 >> 2 & 1;
                const double a = g[m->a], b = g[m->b];
                double op = 0;
                if (F == 1) op = Cc ? a : a + b; else if (F == 3) op = Cc ? 0.5 * logicop34(a, b) : b; else if (F == 4) op = a - b; else if (F == 7) op = -b;
                g[m->d] = op; break; }
            case LD: g[m->d] = mem[(uint16_t)(ps + of[m->w])] + 0.0; break;
            case ST: mem[(uint16_t)(ps + of[m->w])] = g[O]; break;
            case IN: g[m->d] = curIn; break;
            case DAC: if (ndac == 0) *L = g[O]; else if (ndac == 1) *Rr = g[O]; ndac++; break;
            case CARRYIN: {                                // events issued late in the previous sample, due at tick m->d of this one
                int j = 0;
                for (int i = 0; i < ncarry; i++) if (carry[i].due == m->d) doEvent(carry[i].kind, carry[i].wa, carry[i].addr), j++;
                (void)j; break; }
            }
        }
    }

    struct Ev { int kind, wa; uint16_t w; };               // wa for R loads; w = word whose offset field forms the address
    Decoded decode(const CS& start) const {
        Decoded D; D.start = start; CS s = start;
        struct W { int mwr, mcen, op, wa, ra, acc, dpd, n, m26; } wd[128];
        for (int i = 0; i < 128; i++) {
            uint32_t x = words[i]; int h = x >> 24, c = (x >> 16) & 255;
            wd[i] = { (h >> 7) & 1, (h >> 6) & 1, (h >> 5) & 1, h & 3, (c >> 6) & 3, (c >> 5) & 1, (c >> 4) & 1, c & 15, (h >> 2) & 1 };
        }
        static const int kRL = 5, kWR = 6, kAdc = 3, kDac = 5, kRO = 5, kROmw = 5, MAXJ = 4;   // O latches on MIB26 delayed 2 ticks (xh1)
        std::vector<Ev> due[SAMPLE_TICKS];
        std::vector<Mop> ops;
        int nib = 0, dpdx = 1, ra_l = 0;
        auto emit = [&](uint8_t t, int d, int a, int b, int w = 0, int x = 0) { Mop m; m.t = t; m.d = (uint8_t)d; m.a = (uint8_t)a; m.b = (uint8_t)b; m.w = (uint16_t)w; m.x = (uint16_t)x; ops.push_back(m); };
        for (int Pn = 0; Pn < 128; Pn++) for (int ph = 0; ph < 3; ph++) {
            const int tick = 3 * Pn + ph;
            const auto& f = wd[Pn]; const auto& fp = wd[(Pn + 127) & 127]; const auto& fpp = wd[(Pn + 126) & 127];
            if (ph == 0) {
                const int w1 = (Pn + 1) & 127;
                auto sched = [&](int delay, int kind, int wa) {
                    const int dt = tick + delay;
                    if (dt < SAMPLE_TICKS) due[dt].push_back({ kind, wa, (uint16_t)w1 });
                    else D.carryOut.push_back({ (uint8_t)kind, (uint8_t)wa, (uint8_t)(dt - SAMPLE_TICKS), (uint16_t)w1 });
                };
                if (f.mcen == 0) { if (!f.mwr) sched(kRL, 0, wd[w1].wa); else sched(kWR, 1, f.wa); }
                if (f.mwr) sched(f.mcen == 0 ? kROmw : kRO, 4, wd[w1].wa);
                if (f.mcen != 0 && f.op == 0) { if (!f.mwr) sched(kAdc, 2, f.wa); else sched(kDac, 3, f.wa); }
                nib = fp.n; dpdx = fpp.dpd; ra_l = fp.ra;
            }
            const auto& raw = (ph == 2) ? f : fp; const int dp = raw.dpd, acc_in = raw.acc, dpd_now = (ph == 2) ? fp.dpd : fpp.dpd;
            const int v49 = U49[(s.st & 3) | dp << 2 | dpd_now << 3 | acc_in << 4];
            const int v48 = U48[(s.st & 3) | nib << 2 | dpdx << 6 | 0x180];
            const int e49 = s.c49, e48 = s.c48;
            const int b3 = e49 >> 7 & 1, b0 = e49 >> 6 & 1, an = e49 >> 5 & 1;
            const int F = e48 < 0 ? 0 : (e48 >> 3 & 1) << 2 | (e48 >> 4 & 1) << 1 | (e48 >> 5 & 1), Cc = e48 < 0 ? 0 : e48 >> 2 & 1;
            // the bus and the ALU read the registers as they are before this tick's DRAM events
            if (b0) emit(MOV, T0, ACC, 0); else if (isVarRa[(Pn + 127) & 127]) emit(MOVR, T0, 0, 0, (Pn + 127) & 127); else emit(MOV, T0, R0 + ra_l, 0);
            int B = P; if (b3) { emit(HALF, T2, A, 0); B = T2; }
            if (e48 < 0) emit(ALUV, T1, A, B, s.c48w, s.c48b);
            else if (F == 1) { if (Cc) emit(MOV, T1, A, 0); else emit(ADD, T1, A, B); }
            else if (F == 3) { if (Cc) emit(ALU34, T1, A, B); else emit(MOV, T1, B, 0); }
            else if (F == 4) emit(SUB, T1, A, B);
            else if (F == 7) emit(NEG, T1, 0, B);
            else emit(ZERO, T1, 0, 0);
            // XCLK: MIB26 delayed two ticks, O latches on its falling edge
            const int m26 = ((ph == 2) ? f : fp).m26;
            const int xl = s.xh1; s.xh1 = s.xh0; s.xh0 = m26;
            const int xedge = (!xl && s.prevX); s.prevX = xl;
            // DRAM / converter events due this tick: first those carried over from the previous sample, then this sample's
            if (tick < MAXJ) emit(CARRYIN, tick, 0, 0);
            for (const Ev& e : due[tick]) {
                switch (e.kind) {
                case 0: emit(LD, e.wa, 0, 0, e.w); break;
                case 1: emit(ST, 0, O, 0, e.w); break;
                case 2: emit(IN, e.wa, 0, 0); break;
                case 4: emit(MOV, R0 + e.wa, O, 0); break;
                default: emit(DAC, 0, O, 0); break;
                }
            }
            const int commit = s.scq0; s.scq0 = s.scq1; s.scq1 = (v49 >> 4 & 1);
            if (xedge) emit(SAT, O, T0, 0);
            if (commit) emit(SAT, ACC, T1, 0);
            if (an == 0) emit(MOV, A, T0, 0);
            emit(HALF, P, T1, 0);
            { const int word = (Pn + 127) & 127, base = (s.st & 3) | dpdx << 6 | 0x180;     // this tick's U48 address, before the nibble
              if (isVar[word]) { s.c48 = -1; s.c48b = base; s.c48w = word; } else { s.c48 = v48; s.c48b = 0; s.c48w = 0; } }
            s.st = v49 & 3; s.c49 = v49;
        }
        D.end = s;
        // Copy propagation: a register that was only a copy of another (the bus latch T0, the ALU result T1) is read from its source for as long as the source is
        // unchanged, so most of those copies become dead and fall to the liveness pass below.
        {
            int src[NREG]; for (int i = 0; i < NREG; i++) src[i] = i;
            auto kill = [&](int reg) { src[reg] = reg; for (int i = 0; i < NREG; i++) if (src[i] == reg && i != reg) src[i] = i; };
            for (Mop& m : ops) {
                switch (m.t) {
                case MOV: case SAT: case HALF: m.a = (uint8_t)src[m.a]; break;
                case ADD: case SUB: case ALU34: case ALUV: m.a = (uint8_t)src[m.a]; m.b = (uint8_t)src[m.b]; break;
                case NEG: m.b = (uint8_t)src[m.b]; break;
                case ST: case DAC: m.a = (uint8_t)src[m.a]; break;
                default: break;
                }
                switch (m.t) {
                case MOV: case MOVR: case SAT: case HALF: case ADD: case SUB: case NEG: case ZERO: case ALU34: case ALUV: case LD: case IN:
                    kill(m.d); if (m.t == MOV && m.a != m.d) src[m.d] = m.a; break;
                case CARRYIN: for (int i = R0; i < R0 + 4; i++) kill(i); break;
                default: break;
                }
            }
        }
        // Backward liveness: every register is live at the end of the sample; DRAM writes, the DAC and the carried-over events are side effects and always stay.
        unsigned live = 0xFF;                              // R0-R3, ACC, O, A, P
        std::vector<Mop> kept;
        for (size_t i = ops.size(); i-- > 0;) {
            const Mop& m = ops[i];
            bool keep = true; unsigned use = 0, def = 0;
            switch (m.t) {
            case MOV: case SAT: case HALF: def = 1u << m.d; use = 1u << m.a; break;
            case ADD: case SUB: case ALU34: case ALUV: def = 1u << m.d; use = 1u << m.a | 1u << m.b; break;
            case NEG: def = 1u << m.d; use = 1u << m.b; break;
            case ZERO: case IN: case LD: def = 1u << m.d; break;
            case MOVR: def = 1u << m.d; use = 0xF; break;
            case ST: case DAC: use = 1u << m.a; break;
            case CARRYIN: use = 1u << O; break;             // may also write R0-R3 (never kills)
            }
            if (def && !(live & def)) keep = false;
            if (keep) { live = (live & ~def) | use; kept.push_back(m); }
        }
        D.ops.assign(kept.rbegin(), kept.rend());
        return D;
    }
};

}

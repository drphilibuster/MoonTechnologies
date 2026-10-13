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
// The decoded list runs two ways, on one State (Pcm70HspJit.hpp): the interpreter below, and, once a decode has been used a few times, native code generated from
// the same list (src/Depreciation/HspJit.cpp; the register file in vector registers instead of memory, so a dependent chain pays the arithmetic's latency and not the
// store-to-load forwarding of each step). The first build of a decode is checked against the interpreter on scratch state; a disagreement turns the JIT off for good.
//
// The two sequencer PROMs (U48 512 B, U49 32 B) are the user's own dumps, given to setProms().
#pragma once
#include "Pcm70HspJit.hpp"
#include <cstdint>
#include <cstring>
#include <algorithm>
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

    void setProms(const uint8_t* u48 /*512*/, const uint8_t* u49 /*32*/) { memcpy(U48, u48, 512); memcpy(U49, u49, 32); cache.clear(); nibGen++; }
    /** Turn the native code on or off (on by default where it is built in). Off, the interpreter alone runs. */
    void setJit(bool on) { jitOn = on; }
    bool jitDisagreed() const { return jitMismatch; }   // a self-test found native code and interpreter apart (never seen: a code-generator bug if it is)
    bool jitRunning() const { return !cache.empty() && (bool)cache.back().code; }

    // Install a program. Control fields (the top 16 bits of each word) change only when a program is loaded; the memory offsets (low 16 bits) change continuously.
    void setProgram(const uint32_t* w) {
        if (haveProgram) {
            // by far the common call: a modulated program rewrites its memory offsets every sample and nothing else
            uint32_t ctl = 0;
            for (int i = 0; i < 128; i++) ctl |= (w[i] ^ words[i]) >> 16;
            if (ctl == 0) { for (int i = 0; i < 128; i++) { words[i] = w[i]; off[i] = (uint16_t)w[i]; } return; }
        }
        bool load = !haveProgram, flagged = false;
        for (int i = 0; i < 128; i++) {
            const uint32_t diff = (w[i] ^ words[i]) >> 16;
            if (diff & ~0xCFu) load = true;
            else {
                if ((diff & 0x0F) && !isVar[i]) { isVar[i] = true; flagged = true; }
                if ((diff & 0xC0) && !isVarRa[i]) { isVarRa[i] = true; flagged = true; }
            }
            const uint8_t nb = (uint8_t)((w[i] >> 16) & 15);
            if (nb != nibs[i]) nibGen++;
            words[i] = w[i]; off[i] = (uint16_t)(w[i] & 0xFFFF); nibs[i] = nb; ras[i] = (uint8_t)((w[i] >> 22) & 3);
        }
        haveProgram = true;
        if (load) { memset(isVar, 0, sizeof isVar); memset(isVarRa, 0, sizeof isVarRa); }
        if (load || flagged) cache.clear();
    }

    void reset() {
        mem.assign(65536, 0.0); for (int i = 0; i < NREG; i++) js.r[i] = 0.0;
        pos = 0; cs = CS(); js.ncarry = 0; js.ndac = 0;
    }

    void process(double in, double& outL, double& outR) {
        Decoded* d = find();
        State& s = js;
        s.out[0] = s.out[1] = 0.0; s.ndac = 0; s.curIn = in; s.mem = mem.data(); s.off = off; s.ras = ras; s.ps = pos;
        refreshKinds(*d); s.aluk = d->aluk.data();
#ifdef PCM70_HSP_JIT
        if (jitOn && !d->code && !d->jitTried && ++d->uses >= JIT_AFTER) buildJit(*d);
        if (d->code) d->code(&s); else
#endif
        run(*d, s);
        outL = s.out[0]; outR = s.out[1];
        // events issued in this sample that complete in the next one
        int n = 0;
        for (size_t i = 0; i < d->carryOut.size(); i++) {
            const CarryDesc& c = d->carryOut[i];
            s.ckind[n] = c.kind; s.cwa[n] = c.wa; s.cdue[n] = c.due; s.caddr[n] = (uint16_t)(pos + off[c.w]); n++;
        }
        s.ncarry = n;
        cs = d->end; pos++;
    }

    double reg(int i) const { return js.r[i]; }               // R0-R3 = 0-3, ACC 4, O 5, A 6, P 7
    double peek(int a) const { return mem[a & 0xFFFF]; }
    uint16_t position() const { return pos; }
    size_t opCount() const { return cache.empty() ? 0 : cache.back().ops.size(); }
    uint64_t decodes = 0;                              // how many times a sample had to be decoded (diagnostic)
    uint64_t jitCompiles = 0;                          // how many decodes have been turned into native code (diagnostic)

private:
    typedef hspjit::State State;
    enum { R0 = 0, ACC = 4, O = 5, A = 6, P = 7, T0 = 8, T1 = 9, T2 = 10, NREG = hspjit::NREG };
    // The last five are pairs the decoder fuses (see fuse()): the run loop pays one dispatch for two operations. The second operation's fields are d2..x2.
    // The first fifteen are hspjit::OpT's, in its order (static_assert below); x of an ALUV is its index into the kind table (aluK).
    enum Op : uint8_t { MOVR, MOV, SAT, HALF, ADD, SUB, NEG, ZERO, ALU34, ALUV, LD, ST, IN, DAC, CARRYIN, HALF_ALUV, ALUV_HALF, SAT_HALF, ALUV_SAT, HALF_ADD };
    static_assert((int)MOVR == (int)hspjit::MOVR && (int)ALUV == (int)hspjit::ALUV && (int)CARRYIN == (int)hspjit::CARRYIN && (int)HALF_ALUV == (int)hspjit::NUM_OPS, "operation numbering");
    struct Mop { uint8_t t, d, a, b; uint16_t w, x; uint8_t d2, a2, b2, pad; uint16_t w2, x2; };
    struct CS {                                            // sequencer state carried from sample to sample
        int st = 0, c49 = 0xE9, c48 = 0xF7, c48b = 0, c48w = 0, scq0 = 0, scq1 = 0, xh0 = 1, xh1 = 1, prevX = 0;     // c48 = -1: set by a flagged word's nibble, see c48b (U48 address without it) and c48w (the word)
        bool operator==(const CS& o) const { return st == o.st && c49 == o.c49 && c48 == o.c48 && c48b == o.c48b && c48w == o.c48w && scq0 == o.scq0 && scq1 == o.scq1 && xh0 == o.xh0 && xh1 == o.xh1 && prevX == o.prevX; }
    };
    struct CarryDesc { uint8_t kind, wa, due; uint16_t w; };
    struct Decoded {
        CS start, end; std::vector<Mop> ops; std::vector<CarryDesc> carryOut;
        std::vector<hspjit::Op> raw;                       // the same operations unfused, for the code generator
        std::vector<uint16_t> alW, alX;                    // per ALUV kind-table entry: the word whose live nibble selects it, and its U48 address without the nibble
        std::vector<uint8_t> aluk; uint32_t alukGen = ~0u; // the kinds for the nibbles as of nibGen
        hspjit::Code code; bool jitTried = false; int uses = 0;
    };

    uint8_t U48[512] = {}, U49[32] = {};
    uint32_t words[128] = {}; uint16_t off[128] = {}; uint8_t nibs[128] = {}, ras[128] = {}; bool isVar[128] = {}, isVarRa[128] = {}; bool haveProgram = false;
    std::vector<double> mem = std::vector<double>(65536, 0.0);
    State js;                                              // the registers, the sample's inputs and outputs, the carried events (the native code works on this too)
    uint16_t pos = 0; CS cs;
    uint32_t nibGen = 0; bool jitOn = true, jitMismatch = false;
    std::vector<Decoded> cache;                            // decodes for this program, keyed by their start state (most recent last)

    static double sat(double v) { return v < -1.0 ? -1.0 : (v > 0.99997 ? 0.99997 : v); }
    static double logicop34(double a, double b) {          // ~A & B on 18-bit two's complement words
        auto q = [](double v) { long i = std::lround(v * 131072.0); if (i > 131071) i = 131071; if (i < -131072) i = -131072; return i; };
        long x = ~q(a) & q(b); x &= 0x3FFFF; if (x & 0x20000) x -= 0x40000; return x / 131072.0;
    }

    Decoded* find() {
        for (size_t i = 0; i < cache.size(); i++) if (cache[i].start == cs) { if (i + 1 != cache.size()) { Decoded t = std::move(cache[i]); cache.erase(cache.begin() + i); cache.push_back(std::move(t)); } return &cache.back(); }
        if (cache.size() >= 4) cache.erase(cache.begin());
        cache.push_back(decode(cs)); decodes++;
        return &cache.back();
    }

    // one event of the reference model: 0 DRAM read -> R, 1 DRAM write of O, 2 ADC -> R, 3 DAC <- O, 4 R <- O
    static void doEvent(State& s, int kind, int wa, uint16_t addr) {
        switch (kind) {
        case 0: s.r[wa] = s.mem[addr] + 0.0; break;
        case 1: s.mem[addr] = s.r[O]; break;
        case 2: s.r[wa] = s.curIn; break;
        case 4: s.r[wa] = s.r[O]; break;
        default: if (s.ndac < 2) s.out[s.ndac] = s.r[O]; s.ndac++; break;
        }
    }
    /** The events carried over from the previous sample that fall due at tick `due` (called by CARRYIN, from the interpreter and from native code). */
    static void carryStep(State* s, int due) {
        for (int i = 0; i < s->ncarry; i++) if (s->cdue[i] == due) doEvent(*s, s->ckind[i], s->cwa[i], s->caddr[i]);
    }

    // The ALU operation U48 selects for a flagged word's live coefficient nibble, as a kind: 0 zero, 1 A, 2 A+B, 3 B, 4 A-B, 5 -B, 6 half of (~A & B).
    static uint8_t kindOf(int e48) {
        const int F = (e48 >> 3 & 1) << 2 | (e48 >> 4 & 1) << 1 | (e48 >> 5 & 1), Cc = e48 >> 2 & 1;
        return F == 1 ? (Cc ? 1 : 2) : F == 3 ? (Cc ? 6 : 3) : F == 4 ? 4 : F == 7 ? 5 : 0;
    }
    static inline double aluOp(int kind, double a, double b) {
        switch (kind) { case 1: return a; case 2: return a + b; case 3: return b; case 4: return a - b; case 5: return -b; case 6: return 0.5 * logicop34(a, b); default: return 0.0; }
    }
    void refreshKinds(Decoded& d) const {
        if (d.alukGen == nibGen) return;
        d.alukGen = nibGen;
        for (size_t k = 0; k < d.alW.size(); k++) d.aluk[k] = kindOf(U48[d.alX[k] | nibs[d.alW[k]] << 2]);
    }

    // One dispatch per (fused) operation. On GCC/Clang each case ends in its own indirect jump (threaded code), which the branch predictor follows far better
    // than the single jump of a switch; elsewhere it is a plain switch. The cases are written once, as macros.
#if defined(__GNUC__)
  #define HSP_CASE(name) L_##name:
  #define HSP_NEXT() do { if (++m == e) return; goto *tbl[m->t]; } while (0)
#else
  #define HSP_CASE(name) case name:
  #define HSP_NEXT() break
#endif
    static void run(const Decoded& d, State& s) {
        double* g = s.r; const uint16_t* of = s.off; const uint8_t* ras = s.ras; const uint8_t* kinds = s.aluk; double* const mem = s.mem; const uint16_t ps = (uint16_t)s.ps;
        const Mop *m = d.ops.data(), *e = m + d.ops.size();
        if (m == e) return;
#if defined(__GNUC__)
        static const void* const tbl[] = { &&L_MOVR, &&L_MOV, &&L_SAT, &&L_HALF, &&L_ADD, &&L_SUB, &&L_NEG, &&L_ZERO, &&L_ALU34, &&L_ALUV, &&L_LD, &&L_ST, &&L_IN, &&L_DAC,
                                           &&L_CARRYIN, &&L_HALF_ALUV, &&L_ALUV_HALF, &&L_SAT_HALF, &&L_ALUV_SAT, &&L_HALF_ADD };
        goto *tbl[m->t];
#else
        for (; m < e; ++m) switch (m->t) {
#endif
            HSP_CASE(MOV) g[m->d] = g[m->a]; HSP_NEXT();
            HSP_CASE(MOVR) g[m->d] = g[ras[m->w]]; HSP_NEXT();                 // the register select of a word whose select bits move (chorus programs swap it)
            HSP_CASE(SAT) g[m->d] = sat(g[m->a]); HSP_NEXT();
            HSP_CASE(HALF) g[m->d] = g[m->a] / 2; HSP_NEXT();
            HSP_CASE(ADD) g[m->d] = g[m->a] + g[m->b]; HSP_NEXT();
            HSP_CASE(SUB) g[m->d] = g[m->a] - g[m->b]; HSP_NEXT();
            HSP_CASE(NEG) g[m->d] = -g[m->b]; HSP_NEXT();
            HSP_CASE(ZERO) g[m->d] = 0.0; HSP_NEXT();
            HSP_CASE(ALU34) g[m->d] = 0.5 * logicop34(g[m->a], g[m->b]); HSP_NEXT();
            HSP_CASE(ALUV) g[m->d] = aluOp(kinds[m->x], g[m->a], g[m->b]); HSP_NEXT();
            HSP_CASE(LD) g[m->d] = mem[(uint16_t)(ps + of[m->w])] + 0.0; HSP_NEXT();
            HSP_CASE(ST) mem[(uint16_t)(ps + of[m->w])] = g[O]; HSP_NEXT();
            HSP_CASE(IN) g[m->d] = s.curIn; HSP_NEXT();
            HSP_CASE(DAC) if (s.ndac < 2) s.out[s.ndac] = g[O]; s.ndac++; HSP_NEXT();
            HSP_CASE(HALF_ALUV) g[m->d] = g[m->a] / 2; g[m->d2] = aluOp(kinds[m->x2], g[m->a2], g[m->b2]); HSP_NEXT();
            HSP_CASE(ALUV_HALF) g[m->d] = aluOp(kinds[m->x], g[m->a], g[m->b]); g[m->d2] = g[m->a2] / 2; HSP_NEXT();
            HSP_CASE(SAT_HALF) g[m->d] = sat(g[m->a]); g[m->d2] = g[m->a2] / 2; HSP_NEXT();
            HSP_CASE(ALUV_SAT) g[m->d] = aluOp(kinds[m->x], g[m->a], g[m->b]); g[m->d2] = sat(g[m->a2]); HSP_NEXT();
            HSP_CASE(HALF_ADD) g[m->d] = g[m->a] / 2; g[m->d2] = g[m->a2] + g[m->b2]; HSP_NEXT();
            HSP_CASE(CARRYIN) {                                // events issued late in the previous sample, due at tick m->d of this one
                carryStep(&s, m->d);
                HSP_NEXT(); }
#if !defined(__GNUC__)
        }
#endif
    }
  #undef HSP_CASE
  #undef HSP_NEXT

    struct Ev { int kind, wa; uint16_t w; };               // wa for R loads; w = word whose offset field forms the address
    // Fuse neighbouring operations into one dispatch. Each pair runs its two operations in order on the same register file, so the arithmetic is unchanged.
    static std::vector<Mop> fuse(const std::vector<Mop>& in) {
        std::vector<Mop> out; out.reserve(in.size());
        for (size_t i = 0; i < in.size(); i++) {
            Mop m = in[i];
            if (i + 1 < in.size()) {
                const Mop& n = in[i + 1]; int f = -1;
                if (m.t == HALF && n.t == ALUV) f = HALF_ALUV;
                else if (m.t == ALUV && n.t == HALF) f = ALUV_HALF;
                else if (m.t == SAT && n.t == HALF) f = SAT_HALF;
                else if (m.t == ALUV && n.t == SAT) f = ALUV_SAT;
                else if (m.t == HALF && n.t == ADD) f = HALF_ADD;
                if (f >= 0) { m.t = (uint8_t)f; m.d2 = n.d; m.a2 = n.a; m.b2 = n.b; m.w2 = n.w; m.x2 = n.x; i++; }
            }
            out.push_back(m);
        }
        return out;
    }

#ifdef PCM70_HSP_JIT
    static const int JIT_AFTER = 16;                       // uses of one decode before it is compiled: the transient ones while the sequencer state settles are not

    void buildJit(Decoded& d) {
        d.jitTried = true;
        hspjit::Code c = hspjit::compile(d.raw.data(), d.raw.size(), &Hsp::carryStep, &Hsp::logicop34);
        if (!c) { jitOn = false; return; }                 // no executable memory here: do not try again
        if (!selfTest(d, c)) { jitOn = false; jitMismatch = true; return; }
        d.code = std::move(c); jitCompiles++;
    }
    // One decode, both ways, on identical scratch state (random registers, delay RAM, events carried in) for several samples, with every kind of ALUV tried.
    bool selfTest(Decoded& d, const hspjit::Code& c) {
        uint64_t x = 0x9E3779B97F4A7C15ull;
        auto rnd = [&]() { x = x * 6364136223846793005ull + 1442695040888963407ull; return (double)(x >> 11) * (1.0 / 9007199254740992.0) * 2.0 - 1.0; };
        std::vector<double> ma(65536), mb;
        for (double& v : ma) v = rnd() * 1.2;
        mb = ma;
        State a, b;
        for (int i = 0; i < NREG; i++) a.r[i] = rnd() * 1.5;
        b = a; a.mem = ma.data(); b.mem = mb.data();
        std::vector<uint8_t> kinds(d.alW.size());
        for (int smp = 0; smp < 8; smp++) {
            for (size_t k = 0; k < kinds.size(); k++) kinds[k] = (uint8_t)(smp == 0 ? d.aluk[k] : (k + smp) % 7);
            a.aluk = b.aluk = kinds.data(); a.off = b.off = off; a.ras = b.ras = ras;
            a.ps = b.ps = (uint32_t)(x >> 40) & 0xFFFF; a.curIn = b.curIn = rnd(); a.ndac = b.ndac = 0; a.out[0] = a.out[1] = b.out[0] = b.out[1] = 0.0;
            a.ncarry = b.ncarry = 1 + (int)(x >> 33) % 4;
            for (int i = 0; i < a.ncarry; i++) {
                a.ckind[i] = b.ckind[i] = (uint8_t)((x >> (20 + i)) % 5); a.cwa[i] = b.cwa[i] = (uint8_t)((x >> (10 + i)) & 3);
                a.cdue[i] = b.cdue[i] = (uint8_t)(i % 4); a.caddr[i] = b.caddr[i] = (uint16_t)(x >> (7 + i));
                (void)rnd();
            }
            run(d, a); c(&b);
            for (int i = 0; i < NREG; i++) if (!(a.r[i] == b.r[i] || (a.r[i] != a.r[i] && b.r[i] != b.r[i])) && i < 8) return false;
            if (!(a.out[0] == b.out[0]) || !(a.out[1] == b.out[1]) || a.ndac != b.ndac) return false;
            for (size_t i = 0; i < ma.size(); i++) if (!(ma[i] == mb[i])) return false;
            for (int i = 0; i < NREG; i++) b.r[i] = a.r[i];   // temporaries are not part of the contract between samples
        }
        return true;
    }
#endif

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
        auto emit = [&](uint8_t t, int d, int a, int b, int w = 0, int x = 0) { Mop m; memset(&m, 0, sizeof m); m.t = t; m.d = (uint8_t)d; m.a = (uint8_t)a; m.b = (uint8_t)b; m.w = (uint16_t)w; m.x = (uint16_t)x; ops.push_back(m); };
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
        std::reverse(kept.begin(), kept.end());
        // ALUV: x (a U48 address) becomes an index into the kind table, one entry per distinct (word, address)
        for (Mop& m : kept) {
            if (m.t != ALUV) continue;
            size_t k = 0;
            while (k < D.alW.size() && !(D.alW[k] == m.w && D.alX[k] == m.x)) k++;
            if (k == D.alW.size()) { D.alW.push_back(m.w); D.alX.push_back(m.x); }
            m.x = (uint16_t)k;
        }
        D.aluk.assign(D.alW.size(), 0);
        for (const Mop& m : kept) { hspjit::Op o; o.t = m.t; o.d = m.d; o.a = m.a; o.b = m.b; o.w = m.w; o.k = m.x; D.raw.push_back(o); }
        D.ops = fuse(kept);
        return D;
    }
};

}

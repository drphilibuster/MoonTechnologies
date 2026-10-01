// Pcm70Drive.hpp (from the research tree's fwdrive.hpp): host-side driver for the master firmware's edit routine (Steps 38/39) + the CV edit scheduler.
// Pcm70Drive injects `edit(row, col, delta)` at the main loop's idle epilogue and reports completion; Pcm70EditQueue decides WHAT to inject so that any amount of CV
// traffic is coalesced, rate-limited and kept fair.  Both are plain C++ with no allocation in the hot path (cells are allocated once per program).
#pragma once
#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <functional>
#include "Pcm70Locate.hpp"
extern "C" {
#include "../vendor/z80/z80.h"
}
struct Pcm70Drive {
    Pcm70Loc L; z80* cpu = nullptr; uint8_t* ram = nullptr;          // ram[0] = master address 0x8000
    int state = 0; unsigned long injCyc = 0; int cr = 0, cc = 0, cd = 0;
    unsigned long idleHits = 0, edits = 0, busyCyc = 0, maxEditCyc = 0, stuck = 0;
    std::function<bool(unsigned long, int&, int&, int&)> pick;      // at an idle pass: choose an edit (return false = nothing to do)
    std::function<void(int, int, int, unsigned long)> done;         // edit finished: row, col, delta, duration in cycles
    uint8_t rd(unsigned a) const { return ram[a - 0x8000]; }
    unsigned word(unsigned k) const { return rd(L.wbase + 2 * k) | rd(L.wbase + 2 * k + 1) << 8; }
    void push16(unsigned v) { cpu->sp -= 2; ram[cpu->sp - 0x8000] = v & 255; ram[cpu->sp + 1 - 0x8000] = v >> 8; }
    void step() {                                                   // call after every master z80_step
        if (cpu->pc != L.idle) { if (state == 2 && cpu->cyc - injCyc > 3250000UL * 3) stuck++; return; }
        idleHits++;
        if (state == 2) { unsigned long d = cpu->cyc - injCyc; busyCyc += d; if (d > maxEditCyc) maxEditCyc = d; edits++; state = 0; if (done) done(cr, cc, cd, d); }
        int r, c, d;
        if (state == 0 && pick && pick(cpu->cyc, r, c, d)) {
            cr = r; cc = c; cd = d; injCyc = cpu->cyc; state = 2;
            push16(L.idle); cpu->h = 0; cpu->l = (uint8_t)r; cpu->d = 0; cpu->e = (uint8_t)c; cpu->b = (uint8_t)((d >> 8) & 255); cpu->c = (uint8_t)(d & 255); cpu->pc = L.edit;
        }
    }
};
struct Pcm70EqCell { int row = 0, col = 0, k = 0, lo = 0, hi = 0; bool master = false; int target = -1, word = -1, pre = -1; bool dirty = false; unsigned long lastInj = 0, dirtySince = 0, nInj = 0, superseded = 0; };
struct Pcm70EditQueue {
    static constexpr double HZ = 3.25e6;
    std::vector<Pcm70EqCell> cells; double errw = 1.0, minPlain = 0.006, minMaster = 0.150, duty = 0.65; unsigned long gapUntil = 0; int cur = -1, rr = 0;
    unsigned long overwritten = 0, nGap = 0, nNone = 0, nDirtyAtNone = 0;
    void set(int i, int tgt, unsigned long now) {                   // latest target wins (coalescing); out-of-range clamped
        Pcm70EqCell& c = cells[i]; if (tgt < c.lo) tgt = c.lo; if (tgt > c.hi) tgt = c.hi;
        if (tgt == c.target) return;
        if (c.dirty) { c.superseded++; overwritten++; }
        c.target = tgt;
        if (tgt == c.word) { c.dirty = false; return; }
        if (!c.dirty) c.dirtySince = now; c.dirty = true;
    }
    bool pick(unsigned long now, int& r, int& col, int& d) {
        if (now < gapUntil) { nGap++; return false; }
        int best = -1; double bs = -1;
        for (size_t n = 0; n < cells.size(); n++) {
            Pcm70EqCell& c = cells[n]; if (!c.dirty) continue;
            double age = (double)(now - c.lastInj) / HZ, need = c.master ? minMaster : minPlain; if (age < need) continue;
            double wait = age, err = std::fabs((double)(c.target - c.word)) / (double)(c.hi - c.lo + 1);
            double score = (wait + 0.002) * (1.0 + errw * err);      // staleness (time since this parameter was last served) x error: fair round-robin, large errors get a boost
            if (c.master && wait < 1.0) score *= 0.25;             // masters cost 50-100 ms of firmware time; after 1 s of waiting they compete equally (no starvation)
            if (score > bs) { bs = score; best = (int)n; }
        }
        if (best < 0) { nNone++; for (auto& c : cells) if (c.dirty) { nDirtyAtNone++; break; } return false; }
        Pcm70EqCell& c = cells[best]; cur = best; r = c.row; col = c.col; d = c.target - c.word; c.pre = c.word; c.lastInj = now; c.nInj++; return true;
    }
    void done(const Pcm70Drive& fw, unsigned long dur, unsigned long now) { doneWith([&](unsigned k) { return fw.word(k); }, dur, now); }
    template <class WordOf> void doneWith(WordOf wordOf, unsigned long dur, unsigned long now) {
        for (auto& c : cells) c.word = (int)wordOf((unsigned)c.k);   // the firmware is the truth: masters move children, limits clamp
        if (cur >= 0) { Pcm70EqCell& c = cells[cur]; c.dirty = (c.target != c.word) && (c.word != c.pre); }   // no progress (dynamic limit) -> stop asking
        for (auto& c : cells) if (&c != &cells[cur >= 0 ? cur : 0] && c.dirty && c.target == c.word) c.dirty = false;
        gapUntil = now + (unsigned long)((double)dur * (1.0 / duty - 1.0)); cur = -1;
    }
};

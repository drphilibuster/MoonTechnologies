// Pcm70Control.hpp: the host's hands on the master firmware (Steps 36-39, 42).
//
// The firmware's main loop passes one fixed instruction sequence, its idle epilogue, about every 7 ms; there nothing but SP and IX matters and IX is restored
// from the stack, so the host can push that address as a return and jump into any firmware routine with its own registers. Three routines are used:
//   edit(row, col, delta)   the soft-knob handler's own call: moves a parameter word by `delta` (clamped by the firmware), runs the per-type apply handler that
//                           sends the slave its commands, redraws the display if that cell is shown. One call moves any distance.
//   select(row, col)        loads the cell's slave index, word index and descriptor pointer into the selector variables;
//   formatter(descriptor)   the cell's own caption routine, which draws its value into the display RAM.
// A caption/probe call saves and blanks the display RAM and the selector variables first and restores them after, so the front panel never sees it.
//
// Control owns: the per-program cell table (harvested through select, never read from a ROM table, so it is whatever the firmware says), the edit scheduler
// that keeps any amount of CV traffic from overwhelming the master loop (latest-value slots, hysteresis, fairness, duty cap, read-back), one-shot calls
// and RAM pokes for tests and configuration, and the snapshot of the display the panel should show.
#pragma once
#include <cstdint>
#include <cstring>
#include <deque>
#include <string>
#include <vector>
#include "Pcm70Machine.hpp"
#include "Pcm70Drive.hpp"

namespace pcm70 {

struct Cell {
    bool valid = false;                     // the selector found a parameter here
    int row = 0, col = 0;
    unsigned desc = 0;                      // descriptor pointer (ROM or RAM address)
    int k = 0;                              // word index in the program's 10-bit parameter word image
    int idx = 0;                            // slave command index
    int lo = 0, hi = 0;                     // nominal range of the word (the firmware clamps, and some limits move)
    int word = 0;                           // current word, read back from the firmware
    char text[15] = {0};                    // the descriptor's name/unit text, e.g. "MIX     % WET"
    std::string caption;                    // the firmware's own rendering of the current value, e.g. "5.75 KHZ"
    bool master = false;                    // moves other words as well (REFL/DELAY MASTER: the x.0 cells below row 0)
    bool captionStale = true;
    bool editable() const { return valid && lo != hi; }
};

class Control : public Hook {
public:
    static const int ROWS = 5, COLS = 10;
    unsigned long programEpoch = 0;         // increments whenever the firmware loads a different program (cells are re-harvested)
    unsigned long wordsRefreshed = 0;
    double minTime = 9.5;                   // machine seconds before the host touches the firmware (the power-up routine is not at its main loop yet)
    bool isV3 = false;
    double captionInterval = 0.04;          // at most one caption call per this many machine seconds

    explicit Control(Machine& m) : M(m) { eq.duty = 0.65; }

    // ---- cell table --------------------------------------------------------------------------------------------------------------------------
    bool tableReady() const { return ready; }
    const Cell& cell(int row, int col) const { return cells[row * COLS + col]; }
    // names and values for the panel: ask for a caption to be kept fresh (call again with false to stop)
    void watch(int row, int col, bool on) { watched[row * COLS + col] = on; }

    // ---- scheduler lane: target words -------------------------------------------------------------------------------------------------------
    // Latest target wins; out-of-range clamped; hysteresis keeps a noisy CV from hammering the firmware. NaN/Inf are ignored.
    void setTarget(int row, int col, double word) {
        if (!ready || !(word == word) || std::fabs(word) > 1e9) return;
        const int q = cellToEq[row * COLS + col]; if (q < 0) return;
        if ((int)lastReal.size() != (int)eq.cells.size()) lastReal.assign(eq.cells.size(), 1e18);
        if (std::fabs(word - lastReal[q]) < 0.75) return;                              // hysteresis: ignore jitter under 3/4 of a word step
        lastReal[q] = word;
        eq.set(q, (int)std::lround(word), M.m.cyc);
    }
    void setNormalized(int row, int col, double v01) {
        if (!ready) return; const Cell& c = cell(row, col); if (!c.editable()) return;
        if (!(v01 == v01)) return; v01 = v01 < 0 ? 0 : (v01 > 1 ? 1 : v01);
        setTarget(row, col, c.lo + v01 * (c.hi - c.lo));
    }
    // ---- one-shot calls (tests, configuration) ------------------------------------------------------------------------------------------------
    void editNow(int row, int col, int delta) { Job j; j.kind = EDIT; j.row = row; j.col = col; j.delta = delta; jobs.push_back(j); }
    void poke(unsigned addr, uint8_t value) { Job j; j.kind = POKE; j.addr = addr; j.block.assign(1, value); jobs.push_back(j); }
    void pokeBlock(unsigned addr, const std::vector<uint8_t>& bytes) { Job j; j.kind = POKE; j.addr = addr; j.block = bytes; jobs.push_back(j); }
    // Dynamic MIDI: make controller `source` drive parameter (row, col) with `scale` (signed, -128..128, displayed as the firmware does) by writing one of the ten
    // patch records (10 bytes at 0x994E + 10 slot, Step 37). Source codes: 0x01-0x20 CC 0-31, 0x21-0x3F CC 64-94, 0x41 pitch bend, 0x42 pressure, 0x43 note, 0x44 velocity.
    // The record names the cell's descriptor, so the table must be ready; only cells the firmware calls patchable work (MIX..., not the soft knob).
    bool assignPatch(int slot, int source, int row, int col, int scale) {
        if (!ready || slot < 0 || slot > 9 || !cell(row, col).valid) return false;
        const Cell& c = cell(row, col); const int sc = scale > 0 ? scale - 1 : (scale < 0 ? 256 + scale : 0);
        std::vector<uint8_t> rec = { (uint8_t)source, (uint8_t)(row * 10 + col), (uint8_t)sc, (uint8_t)row, (uint8_t)col, 0, 1, 0, (uint8_t)(c.desc & 255), (uint8_t)(c.desc >> 8) };
        pokeBlock(0x994E + 10 * slot, rec); return true;
    }
    void clearPatch(int slot) { pokeBlock(0x994E + 10 * slot, std::vector<uint8_t>(10, 0)); }
    // ---- system configuration bytes (persist in the battery RAM, Step 42) ---------------------------------------------------------------------------------
    // The firmware version decides one address (M PROTECT moved by two bytes in V3).
    void setV3(bool v3) { isV3 = v3; }
    void setMidiChannel(int ch1to16) { poke(0x98B2, (uint8_t)((ch1to16 - 1) & 15)); }
    void setOmni(bool on) { poke(0x98B3, on ? 1 : 0); }
    void setProgramChange(bool on) { poke(0x98B8, on ? 1 : 0); }
    void setAutoLoad(bool on) { poke(0x993B, on ? 1 : 0); }
    void setMemoryProtect(bool on) { poke(isV3 ? 0x9C6C : 0x9C6A, on ? 1 : 0); }
    void probeNow(int row, int col) { Job j; j.kind = PROBE; j.row = row; j.col = col; jobs.push_back(j); }
    bool busy() const { return !jobs.empty() || phase != 0 || harvestPending > 0 || anyDirty(); }
    unsigned long editsDone() const { return nEdits; }

    // ---- what the panel should show on the 16-digit display (the live display RAM, except while a probe has it blanked) ------------------------------
    std::string displayText() const { return M.decodeDisplay(shown0, shown1); }

    // the panel's LOAD meter: the share of the last second the firmware spent inside host calls (edits, captions); the scheduler holds it under `eq.duty`
    double load() const { return loadMeter; }
    // true once the master has failed to come back from a host call within 3 s (the call is abandoned); clears after 10 clean seconds
    bool fault() const { return faultUntil > M.m.cyc; }

    // the firmware's bypass flag (0x9B33, V2 and V3): set by the BYP key and the footswitch alike
    bool bypassed() const { return M.mram[0x9B33 - 0x8000] != 0; }

    // forget everything about the running program (after a power cycle)
    void reset() { ready = false; haveIdent = false; jobs.clear(); eq.cells.clear(); lastReal.clear(); phase = 0; harvestPending = 0; faultUntil = 0; loadMeter = 0; for (Cell& c : cells) c = Cell(); }

    // diagnostics
    unsigned long probes = 0, stuck = 0, idlePasses = 0; unsigned long busyCycles = 0, maxCallCycles = 0;
    Pcm70EditQueue eq;

    void step(Machine& m) override {
        z80& cpu = m.m;
        if (cpu.pc != m.loc.idle) { if (phase != 0 && cpu.cyc - phaseStart > 3250000UL * 3) { stuck++; phase = 0; faultUntil = cpu.cyc + 32500000UL; } return; }
        if (!m.loc.ok) return;
        idlePasses++;
        if (cpu.cyc - winStart >= 3250000UL) { loadMeter = (double)(busyCycles - winBusy) / (double)(cpu.cyc - winStart); winStart = cpu.cyc; winBusy = busyCycles; }
        if (phase != 0) { advance(m); return; }
        if (m.seconds() < minTime) return;
        snapshotDisplay(m);
        detectProgram(m);
        refreshWords(m);
        startNext(m);
    }

private:
    enum Kind { EDIT, POKE, PROBE };
    struct Job { Kind kind = EDIT; int row = 0, col = 0, delta = 0; unsigned addr = 0; std::vector<uint8_t> block; bool caption = false; bool harvest = false; };

    Machine& M;
    Cell cells[ROWS * COLS]; bool ready = false; int harvestPending = 0;
    bool watched[ROWS * COLS] = {}; std::vector<double> lastReal; unsigned long captionAt[ROWS * COLS] = {};
    int cellToEq[ROWS * COLS];
    std::deque<Job> jobs;
    uint8_t shown0[16] = {}, shown1[16] = {};
    // program identity
    unsigned identLoads = 0; bool haveIdent = false; double loadMeter = 0; unsigned long winStart = 0, winBusy = 0, faultUntil = 0;
    unsigned long nextWordRefresh = 0, nextCaption = 0, nEdits = 0;
    // call in progress
    int phase = 0;                          // 0 idle, 1 edit running, 2 probe: selector running, 3 probe: formatter running
    Job cur; unsigned long phaseStart = 0; int curEq = -1;
    uint8_t savedDisp[0x21] = {}; uint8_t savedSel[64] = {}; unsigned savedSelBase = 0, savedSelLen = 0;
    unsigned curDesc = 0;

    uint8_t rd(unsigned a) const { return M.mram[a - 0x8000]; }
    unsigned rd16(unsigned a) const { return rd(a) | rd(a + 1) << 8; }
    uint8_t dd(unsigned d, unsigned off) const { return d < 0x8000 ? M.mrom[d + off] : rd(d + off); }
    unsigned word(int k) const { return rd16(M.loc.wbase + 2 * k); }
    bool anyDirty() const { for (const Pcm70EqCell& c : eq.cells) if (c.dirty) return true; return false; }
    void push16(Machine& m, unsigned v) { m.m.sp -= 2; m.mram[m.m.sp - 0x8000] = v & 255; m.mram[m.m.sp + 1 - 0x8000] = v >> 8; }
    void call(Machine& m, unsigned pc) { push16(m, m.loc.idle); m.m.pc = (uint16_t)pc; phaseStart = m.m.cyc; }
    void regs(Machine& m, int hl, int de, int bc) { m.m.h = hl >> 8; m.m.l = hl & 255; m.m.d = de >> 8; m.m.e = de & 255; m.m.b = bc >> 8; m.m.c = bc & 255; }

    void snapshotDisplay(Machine& m) { for (int i = 0; i < 16; i++) { shown0[i] = m.mram[0x9C03 - 0x8000 + i]; shown1[i] = m.mram[0x9BF3 - 0x8000 + i]; } }

    // A new program load means new cells: forget everything, harvest again.
    void detectProgram(Machine& m) {
        // the program TYPE decides which cells exist: the loader sets the per-row record pointer (0x98AF) and the type (0x98B1) when it loads one; filing a register by
        // sysex does not touch them, and loading another program of the same type only changes values (read back below)
        const unsigned id = rd16(0x98AF) | (unsigned)rd(0x98B1) << 16;
        if (haveIdent && id == identLoads) return;
        haveIdent = true; identLoads = id; programEpoch++;
        ready = false; for (int i = 0; i < ROWS * COLS; i++) { cells[i] = Cell(); cells[i].row = i / COLS; cells[i].col = i % COLS; cellToEq[i] = -1; }
        eq.cells.clear(); lastReal.clear(); jobs.clear();                                                // cells change meaning: pending edits are void
        harvestPending = ROWS * COLS;
        for (int r = 0; r < ROWS; r++) for (int c = 0; c < COLS; c++) { Job j; j.kind = PROBE; j.row = r; j.col = c; j.caption = true; j.harvest = true; jobs.push_back(j); }
    }
    // the firmware is the truth: words move under keys, masters, limits
    void refreshWords(Machine& m) {
        if (!ready || m.m.cyc < nextWordRefresh) return;
        nextWordRefresh = m.m.cyc + 325000;                                           // 100 ms
        for (int i = 0; i < ROWS * COLS; i++) {
            Cell& c = cells[i]; if (!c.valid) continue;
            const int w = (int)word(c.k);
            if (w != c.word) { c.word = w; c.captionStale = true; wordsRefreshed++; }
            c.lo = (int)(dd(c.desc, 0x18) | dd(c.desc, 0x19) << 8); c.hi = (int)(dd(c.desc, 0x1A) | dd(c.desc, 0x1B) << 8);     // RAM descriptors move their limits with the program
        }
        for (Pcm70EqCell& e : eq.cells) { e.word = (int)word(e.k); if (e.target == e.word) e.dirty = false; const Cell& c = cells[e.row * COLS + e.col]; e.lo = c.lo; e.hi = c.hi; }
    }

    void startNext(Machine& m) {
        if (!jobs.empty()) { cur = jobs.front(); jobs.pop_front(); begin(m); return; }
        // captions of watched cells whose word moved
        if (ready && m.m.cyc >= nextCaption) {
            for (int i = 0; i < ROWS * COLS; i++) if (watched[i] && cells[i].valid && (cells[i].captionStale || m.m.cyc - captionAt[i] >= 812500)) {   // a changed word, or 0.25 s (a clocked tempo moves a caption without moving the word)
                cur = Job(); cur.kind = PROBE; cur.row = i / COLS; cur.col = i % COLS; cur.caption = true; nextCaption = m.m.cyc + (unsigned long)(captionInterval * 3.25e6); begin(m); return;
            }
        }
        if (ready) {
            int r, c, d;
            if (eq.pick(m.m.cyc, r, c, d)) { cur = Job(); cur.kind = EDIT; cur.row = r; cur.col = c; cur.delta = d; curEq = eq.cur; begin(m); return; }
        }
    }
    void begin(Machine& m) {
        if (cur.kind == POKE) { for (size_t i = 0; i < cur.block.size(); i++) m.mram[cur.addr + i - 0x8000] = cur.block[i]; return; }
        if (cur.kind == EDIT) { phase = 1; regs(m, cur.row, cur.col, cur.delta & 0xFFFF); call(m, m.loc.edit); return; }
        // probe: save what the selector and formatter will disturb, blank the display RAM
        memcpy(savedDisp, &m.mram[0x9BF3 - 0x8000], 0x21); memset(&m.mram[0x9BF3 - 0x8000], 0, 0x21);
        savedSelBase = m.loc.vB6 - 4; savedSelLen = m.loc.vBB + 1 - savedSelBase; if (savedSelLen > sizeof savedSel) savedSelLen = sizeof savedSel;
        memcpy(savedSel, &m.mram[savedSelBase - 0x8000], savedSelLen);
        phase = 2; regs(m, cur.row, cur.col, 0); call(m, m.loc.sel);
    }
    void finishProbe(Machine& m) {
        memcpy(&m.mram[0x9BF3 - 0x8000], savedDisp, 0x21); memcpy(&m.mram[savedSelBase - 0x8000], savedSel, savedSelLen);
        probes++; phase = 0;
    }
    void advance(Machine& m) {
        const unsigned long d = m.m.cyc - phaseStart; busyCycles += d; if (d > maxCallCycles) maxCallCycles = d;
        if (phase == 1) {                                                              // edit finished
            nEdits++; phase = 0;
            if (curEq >= 0) { eq.doneWith([&](unsigned k) { return word((int)k); }, d, m.m.cyc); curEq = -1; } else nextWordRefresh = 0;
            return;
        }
        if (phase == 2) {                                                              // selector finished
            const unsigned desc = rd16(m.loc.vB8);
            Cell& c = cells[cur.row * COLS + cur.col];
            if (!desc) { if (cur.harvest) { c.valid = false; harvestDone(m); } finishProbe(m); return; }
            c.valid = true; c.desc = desc; c.k = rd(m.loc.vB6); c.idx = rd(m.loc.vB7);
            c.lo = (int)(dd(desc, 0x18) | dd(desc, 0x19) << 8); c.hi = (int)(dd(desc, 0x1A) | dd(desc, 0x1B) << 8);
            c.word = (int)word(c.k); for (int i = 0; i < 14; i++) c.text[i] = (char)dd(desc, i); c.text[14] = 0;
            c.master = (cur.col == 0 && cur.row > 0);
            if (cur.caption) { curDesc = desc; phase = 3; call(m, dd(desc, 0x14) | dd(desc, 0x15) << 8); }
            else { finishProbe(m); if (cur.harvest) harvestDone(m); }
            return;
        }
        if (phase == 3) {                                                              // formatter finished: read the digits
            uint8_t s0[16], s1[16]; for (int i = 0; i < 16; i++) { s0[i] = m.mram[0x9C03 - 0x8000 + i]; s1[i] = m.mram[0x9BF3 - 0x8000 + i]; }
            if (isV3) { s0[0] = s1[0] = s0[1] = s1[1] = 0; }                           // V3 keeps a symbol (MIDI-sync indicator) in the first two digits; the value is to the right
            Cell& c = cells[cur.row * COLS + cur.col];
            std::string t = m.decodeDisplay(s0, s1); size_t a = t.find_first_not_of(' '), b = t.find_last_not_of(' ');
            c.caption = a == std::string::npos ? std::string() : t.substr(a, b - a + 1); c.captionStale = false; captionAt[cur.row * COLS + cur.col] = m.m.cyc;
            finishProbe(m); if (cur.harvest) harvestDone(m);
        }
    }
    void harvestDone(Machine&) {
        if (--harvestPending > 0) return;
        // build the scheduler's cells: every editable parameter (cell 0.9 repeats MIX)
        eq.cells.clear(); lastReal.clear();
        for (int r = 0; r < ROWS; r++) for (int c = 0; c < COLS; c++) {
            cellToEq[r * COLS + c] = -1; const Cell& x = cells[r * COLS + c];
            if (!x.editable() || (r == 0 && c == 9)) continue;
            Pcm70EqCell q; q.row = r; q.col = c; q.k = x.k; q.lo = x.lo; q.hi = x.hi; q.master = x.master; q.word = x.word;
            cellToEq[r * COLS + c] = (int)eq.cells.size(); eq.cells.push_back(q);
        }
        ready = true;
    }
};

}

// Pcm70Keys.hpp: the front-panel keys, pressed the way the hardware's scan ISR sees them (Steps 36 and 41).
//
// The 8x2 key matrix is read on STATIN during scan phases 8-15 (bit 1 = row 0, bit 3 = row 1 of the column the ISR has selected); the machine keeps one STATIN
// mask per column (Machine::keyCol). A tap needs about 0.05 s down and 0.25 s between keys to be reliable (0.02 s drops keys). Key map (column, row):
//   digits 0..9 = (1,1) (2,1) (3,1) (4,1) (5,1) (1,0) (2,0) (3,0) (4,0) (5,0)    F1 ROW UP (0,0)  F2 ROW DOWN (0,1)  F3 REG (6,0)  F4 PARAM (6,1)
//   LOAD (7,1)  BYP (7,0);   the footswitch is a direct bit (0x80) on the even scan phases, a latching switch to the firmware.
// Modes (RAM 0x994D): 1 PARAM, 2 PGM, 3 PARAM hold view, 4 REG, 5 STORE modifier (F3 held). Location (row 0x994A, column 0x9949) is the program or register
// row/column in PGM/REG modes. Sequences are planned when they come up, from the firmware's own mode and row, so a request made while another is running is
// still right.
#pragma once
#include <cstdint>
#include <deque>
#include <functional>
#include "Pcm70Machine.hpp"

namespace pcm70 {

class Keys {
public:
    enum Key { K0, K1, K2, K3, K4, K5, K6, K7, K8, K9, F1, F2, F3, F4, LOAD, BYP, FOOT, NKEYS };      // FOOT = the footswitch direct bit
    static const unsigned MODE = 0x994D, ROW = 0x994A, COL = 0x9949;
    double tapSeconds = 0.05, gapSeconds = 0.25, settleSeconds = 0.4;

    explicit Keys(Machine& m) : M(m) {}
    static Key digit(int d) { return (Key)(K0 + d); }

    // ---- low level: queue events ------------------------------------------------------------------------------------------------------------------
    void tap(Key k) { enqueue([this, k]() { press(k, tapSeconds); }); }
    void hold(Key k, double seconds) { enqueue([this, k, seconds]() { press(k, seconds); }); }
    // F3 held for `seconds`, with LOAD tapped in the middle (the STORE gesture)
    void storeGesture(double seconds = 1.2) {
        enqueue([this, seconds]() { const uint64_t t = M.m.cyc; events.push_back({ t, F3, true }); events.push_back({ t + cyc(seconds * 0.4), LOAD, true }); events.push_back({ t + cyc(seconds * 0.4 + 0.1), LOAD, false }); events.push_back({ t + cyc(seconds), F3, false }); settleAt = t + cyc(seconds + settleSeconds); });
    }
    void bypassTap() { tap(BYP); }                                                       // toggles BYPASS ON / OFF (Control::bypassed() reads the flag)
    // The raw footswitch bit: the firmware treats it as a latching switch whose LEVEL is the state (it acts on each change; the first change after power-up
    // sets the flag, later ones follow the level), so a pulse is not a toggle. Use bypassTap() for a toggle.
    void footswitchLevel(bool high) { if (high) M.direct |= 0x80; else M.direct &= (uint8_t)~0x80; }

    // ---- planned sequences ------------------------------------------------------------------------------------------------------------------------
    // PGM mode: row with F1/F2 (wraps 0..7), digit = program within the row, LOAD.
    void selectProgram(int row, int col) { enqueue([this]() { gotoMode(2); }); enqueue([this, row]() { moveRow(row, 8); }); enqueue([this, col]() { press(digit(col), tapSeconds); press(LOAD, tapSeconds); }); }
    // REG mode: row 0..4 (wraps), digit = register column, LOAD.
    void selectRegister(int row, int col) { enqueue([this]() { gotoMode(4); }); enqueue([this, row]() { moveRow(row, 5); }); enqueue([this, col]() { press(digit(col), tapSeconds); press(LOAD, tapSeconds); }); }
    // file the current program as register (row, col): REG mode, choose it, hold F3 and press LOAD.
    void storeRegister(int row, int col) {
        enqueue([this]() { gotoMode(4); }); enqueue([this, row]() { moveRow(row, 5); }); enqueue([this, col]() { press(digit(col), tapSeconds); });
        enqueue([this]() { const uint64_t t = nextFree(); const double s = 1.2;
            events.push_back({ t, F3, true }); events.push_back({ t + cyc(s * 0.4), LOAD, true }); events.push_back({ t + cyc(s * 0.4 + 0.1), LOAD, false }); events.push_back({ t + cyc(s), F3, false }); tail = t + cyc(s + gapSeconds); settleAt = tail + cyc(settleSeconds); });
    }
    // PARAM mode: row (0..5) and digit, so the display shows that parameter (for the user to read; edits themselves use Control)
    void showParameter(int row, int col) { enqueue([this]() { gotoMode(1); }); enqueue([this, row]() { moveRow(row, 6); }); enqueue([this, col]() { press(digit(col), tapSeconds); }); }

    void reset() { tasks.clear(); events.clear(); tail = settleAt = 0; memset(M.keyCol, 0, sizeof M.keyCol); M.direct = 0; }
    bool busy() const { return !tasks.empty() || !events.empty() || M.m.cyc < settleAt; }
    int mode() const { return M.mram[MODE - 0x8000]; }

    // call regularly (once per machine sample is plenty)
    void tick() {
        const uint64_t now = M.m.cyc;
        while (!events.empty() && events.front().t <= now) { apply(events.front()); events.pop_front(); }
        if (events.empty() && now >= settleAt && !tasks.empty() && now >= tail) { std::function<void()> f = tasks.front(); tasks.pop_front(); tail = now; f(); }
    }

private:
    struct Ev { uint64_t t; Key k; bool down; };
    Machine& M;
    std::deque<std::function<void()>> tasks; std::deque<Ev> events; uint64_t tail = 0, settleAt = 0;
    static uint64_t cyc(double s) { return (uint64_t)(s * 3.25e6); }
    void enqueue(std::function<void()> f) { tasks.push_back(f); }
    uint64_t nextFree() { return tail > M.m.cyc ? tail : M.m.cyc; }
    void press(Key k, double down) { const uint64_t t = nextFree(); events.push_back({ t, k, true }); events.push_back({ t + cyc(down), k, false }); tail = t + cyc(down + gapSeconds); settleAt = tail + cyc(settleSeconds - gapSeconds > 0 ? settleSeconds - gapSeconds : 0); }
    static void where(Key k, int& col, int& row) {
        static const int dc[10] = { 1, 2, 3, 4, 5, 1, 2, 3, 4, 5 }, dr[10] = { 1, 1, 1, 1, 1, 0, 0, 0, 0, 0 };
        if (k <= K9) { col = dc[k]; row = dr[k]; return; }
        switch (k) { case F1: col = 0; row = 0; break; case F2: col = 0; row = 1; break; case F3: col = 6; row = 0; break; case F4: col = 6; row = 1; break; case LOAD: col = 7; row = 1; break; default: col = 7; row = 0; break; }
    }
    void apply(const Ev& e) { if (e.k == FOOT) { if (e.down) M.direct |= 0x80; else M.direct &= (uint8_t)~0x80; return; } int c, r; where(e.k, c, r); const uint8_t bit = r ? 0x08 : 0x02; if (e.down) M.keyCol[c] |= bit; else M.keyCol[c] &= (uint8_t)~bit; }
    // leave whatever mode the firmware is in and get to `want` (2 PGM, 4 REG, 1 PARAM): F4 toggles PGM<->PARAM and leaves REG for PGM, F3 enters REG from either
    void gotoMode(int want) {
        int m = mode(); if (m == want) return;
        if (want == 2) { press(F4, tapSeconds); return; }                                    // from PARAM or REG
        if (want == 4) { if (m != 1 && m != 2) press(F4, tapSeconds); press(F3, tapSeconds); return; }
        if (want == 1) { if (m == 4) press(F4, tapSeconds); press(F4, tapSeconds); return; }  // REG -> PGM -> PARAM
    }
    void moveRow(int row, int rows) {
        int cur = M.mram[ROW - 0x8000]; if (cur >= rows) cur = 0;
        int up = (row - cur + rows) % rows, down = rows - up;                                   // F1 = row + 1, F2 = row - 1
        if (up <= down) for (int i = 0; i < up; i++) press(F1, tapSeconds); else for (int i = 0; i < down; i++) press(F2, tapSeconds);
    }
};

}

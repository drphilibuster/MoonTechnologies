// Pcm70Panel.hpp: what the panel's controls mean to the machine (Rack-free, so it is tested like the rest).
//
// The module hands this the state of every control about once a millisecond and gets back what to turn and what to light. It owns:
//   * the PRESET controls: one selector picks a slot, and the FACTORY | USER switch says whether that is one of the machine's programs (rows 0..6 x 10 columns,
//     slot = 10 x row + column) or one of its 50 registers; LOAD, STORE and BYPASS act on a rising edge, exactly as a finger on the key would (the keys are
//     pressed through Pcm70Keys, so the firmware does everything the hardware does). LOAD on a register that holds nothing, and STORE while FACTORY is
//     showing, are refused and say why (Snap::refusal) rather than being sent to the firmware to do something surprising;
//   * the PARAMETER MATRIX: 5 x 9 knobs that ARE the machine's cells (row, column), whatever the running program calls them. Turning one sets the target word
//     (the edit scheduler decides when the firmware gets it); when the firmware moves a word on its own (a program load, a master, a limit) the knob follows,
//     but never while a hand is on it;
//   * the dedicated inputs, turned into the MIDI the firmware's patches listen to (mod wheel, aftertouch, note and gate, sustain), the SOFT knob CV, program
//     change, the bypass gate, and the clock (24 ppqn pulses from edges at 1/2/4/8/24 per quarter note; V3 firmware only) and run;
//   * levels: the input knob, the +4/-20 switches, the full-scale trim.
#pragma once
#include <cmath>
#include <cstring>
#include <string>
#include <vector>
#include "Pcm70BatRam.hpp"
#include "Pcm70Voice.hpp"

namespace pcm70 {

class PanelLogic {
public:
    static const int ROWS = 5, COLS = 9, FACTORY_SLOTS = 70, USER_SLOTS = 50;
    struct In {
        double dt = 0.001;                          // seconds since the previous call
        int slot = 0; bool regMode = false;          // the selector: 10 x row + column of a program (FACTORY) or a register (USER)
        bool programMissing = false;                // the module knows the firmware has no program in the selected FACTORY slot
        bool load = false, store = false, bypass = false;
        float knob[ROWS][COLS] = {};                // 0..1
        float mod = 0, at = 0, note = 0, gate = 0, sust = 0, soft = 0, pgm = 0, byp = 0;
        bool modOn = false, atOn = false, noteOn = false, gateOn = false, sustOn = false, softOn = false, pgmOn = false, bypOn = false;
        int clkDiv = 4;                             // index into {1, 2, 4, 8, 24} edges per quarter note (24 by default: the hardware's own rate)
        float input = 1.f; bool inPad20 = false, outPad20 = false; float trimVolts = 5.f;
    };
    struct Out {
        bool setKnob[ROWS][COLS] = {}; float knob[ROWS][COLS] = {};      // knobs to turn to follow the firmware
        bool bypassLed = false;
    };
    struct Snap {
        std::string display;                        // the 16-digit display
        bool valid[ROWS][COLS] = {}; std::string name[ROWS][COLS], caption[ROWS][COLS]; int lo[ROWS][COLS] = {}, hi[ROWS][COLS] = {}, word[ROWS][COLS] = {};
        int touched = -1;                           // last-touched cell (row * COLS + col) and when
        double sinceTouch = 99.0;
        int leds = 0; bool fault = false; unsigned long epoch = 0; bool tableReady = false;
        std::string refusal; double sinceRefusal = 99.0;      // why the last LOAD / STORE was not sent ("EMPTY", "SWITCH TO USER TO STORE"), and when
        int loadedSlot = -1; bool loadedUser = false;         // what the last accepted LOAD asked for (the power-up program is FACTORY 0)
    };

    PanelLogic() { for (int r = 0; r < ROWS; r++) for (int c = 0; c < COLS; c++) { last[r][c] = -1.f; touch[r][c] = -99.0; } }

    /** A machine has just been handed over: it powers up in FACTORY 0 and live (the bypass flag lives in the battery RAM, so a patch saved while bypassed would
        otherwise come back muted and look broken). */
    void powerUp() { liveChecked = false; loadedSlot = 0; loadedUser = false; }
    static bool registerUsed(const uint8_t* ram, int n) { return ram[batram::slotAddr(n) - 0x8000] != 0; }
    /** A register's own 16-character name ("" when it holds nothing). */
    static std::string registerName(const uint8_t* ram, int n) {
        if (!registerUsed(ram, n)) return std::string();
        return tidy(std::string((const char*)ram + (batram::slotAddr(n) - 0x8000) + 3, 16));
    }
    /** The firmware's 16 digits with the glyphs the font has no letter for taken out: a '?' is a symbol the display draws and this one cannot, and the dot that
        belongs to it goes too. */
    static std::string clean(const std::string& d) {
        std::string o;
        for (size_t i = 0; i < d.size(); i++) { if (d[i] == '?') { o += ' '; if (i + 1 < d.size() && d[i + 1] == '.') i++; } else o += d[i]; }
        return o;
    }

    void control(Voice& v, const In& in, Out& out) {
        now += in.dt;
        Control& C = v.control;
        // ---- levels ----
        v.fullScaleVolts = in.trimVolts < 1.f ? 1.0 : in.trimVolts;
        v.inputGain = in.input * (in.inPad20 ? 0.17783 : 1.0);                  // -20 mode: 15 dB less at the converter (Step 33)
        v.outputPad = in.outPad20 ? 0.0575 : 1.0;                                // output switch: -24.7 dB
        if (C.programEpoch != epoch) { epoch = C.programEpoch; for (int r = 0; r < ROWS; r++) for (int c = 0; c < COLS; c++) { last[r][c] = -1.f; touch[r][c] = -99.0; } }
        // ---- program ----
        const bool loadEdge = in.load && !pLoad, storeEdge = in.store && !pStore, byEdge = (in.bypass && !pBypass) || (in.bypOn && in.byp > 1.f && !pByp);
        pLoad = in.load; pStore = in.store; pBypass = in.bypass; pByp = in.bypOn && in.byp > 1.f;
        if (!liveChecked && C.tableReady()) { liveChecked = true; if (C.bypassed()) v.keys.bypassTap(); }
        const int user = in.slot < 0 ? 0 : (in.slot > USER_SLOTS - 1 ? USER_SLOTS - 1 : in.slot), fact = in.slot < 0 ? 0 : (in.slot > FACTORY_SLOTS - 1 ? FACTORY_SLOTS - 1 : in.slot);
        auto refuse = [&](const char* why) { refusal = why; refusedAt = now; };
        if (loadEdge) {
            if (in.regMode) {
                if (!registerUsed(v.batteryRam(), user)) refuse("EMPTY: NOTHING STORED THERE");
                else { v.keys.selectRegister(user / 10, user % 10); loadedSlot = user; loadedUser = true; }
            } else if (in.programMissing) refuse("NO PROGRAM IN THAT SLOT");
            else { v.keys.selectProgram(fact / 10, fact % 10); loadedSlot = fact; loadedUser = false; }
        }
        if (storeEdge) { if (in.regMode) { v.keys.storeRegister(user / 10, user % 10); loadedSlot = user; loadedUser = true; } else refuse("SWITCH TO USER TO STORE"); }
        if (byEdge) v.keys.bypassTap();
        // ---- the matrix ----
        double modSum[ROWS][COLS] = {}; bool bound[ROWS][COLS] = {};
        if (in.softOn) { modSum[0][2] += in.soft / 10.0; bound[0][2] = true; }
        const bool ready = C.tableReady();
        for (int r = 0; r < ROWS; r++) for (int c = 0; c < COLS; c++) {
            const Cell& x = C.cell(r, c); if (!ready || !x.editable() || (r == 0 && c == 9)) { last[r][c] = in.knob[r][c]; continue; }
            const float k = in.knob[r][c];
            if (last[r][c] < 0.f) last[r][c] = k;                                  // first sight after a program change: not a touch
            const bool touched = std::fabs(k - last[r][c]) > 1e-5f;
            if (touched) {
                last[r][c] = k; touch[r][c] = now; lastTouched = r * COLS + c; touchedAt = now; C.watch(r, c, true);
            }
            const bool hand = now - touch[r][c] < 0.6;
            if (!hand && now - touch[r][c] > 3.0) C.watch(r, c, bound[r][c]);        // captions only for what is being worked, and what the SOFT jack drives
            double want = k + modSum[r][c];
            if (touched || bound[r][c] || hand) { want = want < 0 ? 0 : (want > 1 ? 1 : want); C.setNormalized(r, c, want); }
            else {                                                                  // follow the firmware: its word, normalised to the cell's own limits
                const double span = x.hi - x.lo; const double wn = span > 0 ? (x.word - x.lo) / span : 0.0;
                if (std::fabs(wn - k) > 0.5 / (span > 0 ? span : 1.0)) { out.setKnob[r][c] = true; out.knob[r][c] = (float)wn; last[r][c] = (float)wn; }
            }
        }
        // ---- dedicated inputs -> MIDI ----
        Midi& M = v.midi;
        if (in.modOn) { const int m = cc7(in.mod); if (m != pMod) { M.cc(1, m); pMod = m; } }
        if (in.atOn) { const int a = cc7(in.at); if (a != pAt) { M.aftertouch(a); pAt = a; } }
        if (in.noteOn) curNote = 60 + (int)std::lround(12.0 * in.note);
        if (in.gateOn) {
            const bool g = in.gate > 1.f;
            if (g && !pGate) { M.note(curNote, cc7(in.gate)); heldNote = curNote; }
            if (!g && pGate) M.noteOff(heldNote);
            pGate = g;
        }
        if (in.sustOn) { const int s = in.sust > 1.f ? 127 : 0; if (s != pSus) { M.cc(64, s); pSus = s; } }
        if (in.pgmOn) {
            if (!pgmWas) { C.setProgramChange(true); pgmWas = true; }
            int n = (int)std::lround(in.pgm * 10.0); n = n < 0 ? 0 : (n > 49 ? 49 : n);
            if (n != pPgm) { pgmCand = n; pgmSince = now; pPgm = n; }
            if (pgmCand >= 0 && now - pgmSince > 0.03 && pgmCand != sentPgm) { M.programChange(pgmCand); sentPgm = pgmCand; pgmCand = -1; }
        } else { pgmWas = false; }
        // ---- lights ----
        out.bypassLed = C.bypassed();
        clkDiv = in.clkDiv;
    }
    // one clock edge / run edge, found by the module per host sample
    void clockEdge(Voice& v) { static const int ppqn[5] = { 1, 2, 4, 8, 24 }; v.midi.clockEdge(ppqn[clkDiv < 0 ? 0 : (clkDiv > 4 ? 4 : clkDiv)]); }
    void runEdge(Voice& v) { v.midi.clockStart(); }

    void snapshot(Voice& v, Snap& s) const {
        const Control& C = v.control; s.display = clean(C.displayText()); s.tableReady = C.tableReady(); s.epoch = C.programEpoch; s.fault = C.fault(); s.leds = v.detector.leds();
        s.touched = lastTouched; s.sinceTouch = now - touchedAt; s.refusal = refusal; s.sinceRefusal = now - refusedAt; s.loadedSlot = loadedSlot; s.loadedUser = loadedUser;
        for (int r = 0; r < ROWS; r++) for (int c = 0; c < COLS; c++) {
            const Cell& x = C.cell(r, c); const bool ok = C.tableReady() && x.editable() && !(r == 0 && c == 9);
            s.valid[r][c] = ok; s.lo[r][c] = x.lo; s.hi[r][c] = x.hi; s.word[r][c] = x.word;
            s.name[r][c] = ok ? tidy(std::string(x.text, 14)) : std::string(); s.caption[r][c] = ok ? x.caption : std::string();
        }
    }
    static std::string tidy(const std::string& t) {            // the firmware's 14-byte name/unit text: printable, single spaces
        std::string o; bool sp = false;
        for (char ch : t) { if (ch < 33 || ch > 126) { sp = !o.empty(); continue; } if (sp) o += ' '; sp = false; o += ch; }
        return o;
    }

private:
    double now = 0, touchedAt = -99; unsigned long epoch = 0; int lastTouched = -1;
    float last[ROWS][COLS]; double touch[ROWS][COLS];
    bool pLoad = false, pStore = false, pBypass = false, pByp = false, liveChecked = false; std::string refusal; double refusedAt = -99; int loadedSlot = 0; bool loadedUser = false;
    int pMod = -1, pAt = -1, pSus = -1, curNote = 60, heldNote = 60, pPgm = -1, pgmCand = -1, sentPgm = -1, clkDiv = 4; bool pGate = false, pgmWas = false; double pgmSince = 0;
    static int cc7(float volts) { int v = (int)std::lround(volts / 10.0 * 127.0); return v < 0 ? 0 : (v > 127 ? 127 : v); }
};

}

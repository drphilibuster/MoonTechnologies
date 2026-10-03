// Nordic Banking's panel map: which ADC channel each knob is, which key line each button is and
// which multiplex position each LED is on a Nord Lead 2X. A header of its own so tests/NordicBanking
// can check it against the firmware. Found by pressing every button and setting every selector over
// MIDI in the research emulator (NordLead2Research/NOTES.md, "Building Nordic Banking"); names and
// grouping from the owner's manual's panel drawings (chapter 8).
#pragma once
#include <cstdint>

namespace nb {

// The 26 knobs in panel order: the ADC channel the 68331 selects (gearmulator's KnobType ids), the byte of
// the 66-byte program (owner's manual, "Patch Dump Format") that turning it edits, and its power-on
// position. gearmulator's names for these ids do not match what the firmware does with them -- the
// channel it calls "Filter frequency" edits the amp envelope's decay -- so the table here is the
// measured one: every channel set from 20 to 230 with the edit buffer watched (tests/NordicBanking),
// the panel's function taken from the byte it moved. `offset` -1: not in the program (master volume).
struct Knob { uint8_t channel, initial; int8_t offset; const char* name; };
static const Knob KNOBS[26] = {
	{ 0x3D, 0x00, 21, "LFO 1 rate" }, { 0x5F, 0x0F, 22, "LFO 1 amount" },
	{ 0x3B, 0x00, 23, "LFO 2 / arpeggiator rate" }, { 0x74, 0x00, 24, "LFO 2 amount / arpeggiator range" },
	{ 0x58, 0x00, 18, "Mod envelope attack" }, { 0x3F, 0x00, 19, "Mod envelope decay" }, { 0x3E, 0x00, 20, "Mod envelope amount" },
	{ 0x38, 0x7F, 0, "Osc 2 semitones" }, { 0x5E, 0x7F, 1, "Osc 2 fine tune" },
	{ 0x39, 0x00, 7, "FM amount" }, { 0x68, 0x40, 6, "Pulse width" }, { 0x5D, 0x7F, 2, "Osc mix" },
	{ 0x3A, 0x00, 16, "Portamento" },
	{ 0x5A, 0xFF, 3, "Filter frequency" }, { 0x6F, 0x10, 4, "Filter resonance" }, { 0x6C, 0x00, 5, "Filter envelope amount" },
	{ 0x5B, 0x00, 8, "Filter attack" }, { 0x70, 0x00, 9, "Filter decay" }, { 0x6D, 0x7F, 10, "Filter sustain" }, { 0x6A, 0x30, 11, "Filter release" },
	{ 0x5C, 0x00, 12, "Amp attack" }, { 0x59, 0x00, 13, "Amp decay" }, { 0x6E, 0x7F, 14, "Amp sustain" }, { 0x6B, 0x90, 15, "Amp release" },
	{ 0x69, 0xFF, 17, "Amp gain" }, { 0x3C, 0xFF, -1, "Master volume" },
};
// There is no pedal: SUSTAIN is a gate, sent to the firmware as MIDI controller 64. The mod wheel and pitch bend
// come over MIDI too.
// Where the firmware keeps the program being edited: 66 bytes in the 68331's RAM (offset from $100000),
// laid out as the manual's "Patch Dump Format". Found by searching the RAM for a program sent as SysEx.
static const uint32_t EDIT_BUFFER = 0x16626;
static const int EDIT_BUFFER_SIZE = 66;
// In a performance each of the four slots has a program of its own, one after another from EDIT_BUFFER, and
// this byte (0-3) is the slot the knobs edit. In program mode only slot A's LED is lit and the knobs edit
// slot A; the byte keeps its last value there, so the LEDs say which mode it is.
static const uint32_t SELECTED_SLOT = 0x12226;

// The buttons in panel order: (key line address byte, bit mask) -- gearmulator's ButtonType ids,
// several of which it names after the wrong control (noted).
struct Button { uint16_t id; const char* name; };
static const Button BUTTONS[28] = {
	{ 0x0480, "Osc 1 waveform" }, { 0x0280, "Osc 2 waveform" }, { 0x0204, "Osc 2 KBD track" },
	{ 0x0208, "Ring mod / sync" },
	{ 0x0210, "Filter type" }, { 0x0220, "Filter velocity" },
	{ 0x0402, "Filter KBD track" },             // gearmulator: "Arp"
	{ 0x0240, "Filter distortion (shift: panic)" },
	{ 0x0410, "LFO 1 waveform" }, { 0x0420, "LFO 1 destination" },
	{ 0x0440, "Arpeggiator on (shift: hold)" }, // gearmulator: "Lfo2Shape"
	{ 0x0401, "LFO 2 destination / arp mode" }, // gearmulator: "Distortion"
	{ 0x0408, "Mod envelope destination" },
	{ 0x0680, "Shift / mod wheel destination" },
	{ 0x0610, "Play mode (poly, legato, mono)" }, { 0x0620, "Unison (shift: MIDI channel)" },
	{ 0x0640, "Portamento auto" },
	{ 0x0004, "Oct shift - (shift: dump all)" }, { 0x0002, "Oct shift + (shift: dump one)" },
	{ 0x0010, "Program up" }, { 0x0020, "Program down" }, { 0x0404, "Store" },
	{ 0x0601, "Slot A" }, { 0x0602, "Slot B" }, { 0x0604, "Slot C" }, { 0x0608, "Slot D" },
	{ 0x0201, "Velocity/morph assign (shift: clear)" }, { 0x0202, "Perf mode" },
};

// Every LED as (row, bit) of the 6 x 8 multiplex, in panel order. row 0xFF: on the panel but never
// lit by the firmware -- no position in the multiplex answers for that state (docs/NordicBanking.md).
struct Led { uint8_t row, bit; const char* id; };
static const int NUM_LEDS = 49;
static const Led LEDS[NUM_LEDS] = {
	{ 0xFF, 0, "osc1_sine" }, { 0, 2, "osc1_tri" }, { 1, 2, "osc1_saw" }, { 2, 2, "osc1_pulse" },
	{ 3, 6, "osc2_tri" }, { 1, 1, "osc2_saw" }, { 2, 1, "osc2_pulse" }, { 3, 1, "osc2_noise" },
	{ 4, 1, "osc2_kbd" }, { 1, 5, "ringmod" }, { 5, 1, "sync" },
	{ 0, 1, "hp24" }, { 1, 0, "lp24" }, { 2, 0, "lp12" },              // BP = HP + LP 24, notch = LP 24 + LP 12
	{ 5, 0, "velocity" }, { 0xFF, 0, "kbd23" }, { 5, 3, "kbd13" }, { 4, 0, "distortion" },
	{ 2, 6, "lfo1_softrnd" }, { 1, 6, "lfo1_tri" }, { 0, 6, "lfo1_rnd" },   // square, saw = pairs
	{ 0, 5, "lfo1_fm" }, { 5, 6, "lfo1_osc2" }, { 4, 6, "lfo1_pw" },        // osc 1+2, filter = pairs
	{ 0, 4, "arp" }, { 5, 2, "lfo2_top" }, { 4, 2, "lfo2_mid" }, { 3, 2, "lfo2_bottom" },
	{ 1, 7, "modenv_fm" }, { 0, 7, "modenv_osc2" },                       // PW = both
	{ 4, 3, "wheel_morph" }, { 5, 4, "wheel_osc2" }, { 4, 4, "wheel_filter" },   // LFO 1, FM = pairs
	{ 3, 4, "poly" }, { 3, 5, "legato" }, { 2, 5, "mono" }, { 4, 5, "unison" }, { 2, 4, "auto" },
	{ 0, 3, "oct_m2" }, { 1, 3, "oct_m1" }, { 2, 3, "oct_0" }, { 3, 3, "oct_p1" }, { 3, 0, "oct_p2" },
	{ 3, 7, "slot_a" }, { 4, 7, "slot_b" }, { 5, 7, "slot_c" }, { 5, 5, "slot_d" },
	{ 2, 7, "velmorph" }, { 0, 0, "kbdsplit" },
};
// Row 1 bit 4 is the one position never seen lit (possibly OSC 2's OCT LED, which did not light at
// any octave of the semitone knob); gearmulator's "Trigger" button ($0001) is not on the panel above:
// what it is on the unit is not yet known.

} // namespace nb

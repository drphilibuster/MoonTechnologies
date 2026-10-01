// Contagion's panel map: which key-matrix position each button is and which multiplex bit
// each LED is. A header of its own so tests/Contagion can check it against the firmware.
#pragma once
#include <cstdint>

namespace vc {

// The 35 buttons in panel order, as (row, column) of the key matrix. Named from the owner's
// manual's section drawings, each matched to the firmware by pressing it and reading what it
// does, what it sends the DSP and which LEDs answer (VirusResearch/NOTES.md, "Panel map").
static const uint8_t KEY[35][2] = {
	{ 0, 5 }, { 1, 0 }, { 1, 1 }, { 0, 6 },             // LFOS/MOD: EDIT, SELECT, SHAPE, AMOUNT
	{ 1, 4 }, { 1, 5 }, { 1, 6 }, { 3, 0 }, { 4, 0 },   // OSCILLATORS: EDIT, SYNC, OSC 1, OSC 2, OSC 3,
	{ 4, 1 },                                           //   OSC 3 ON
	{ 0, 1 }, { 0, 4 }, { 0, 2 },                       // EFFECTS: EDIT, SELECT; DELAY/REVERB: EDIT
	{ 0, 3 }, { 0, 0 },                                 // ARP: ON, EDIT
	{ 1, 2 }, { 1, 3 }, { 2, 0 },                       // display: EDIT, GLOBAL/MULTI EDIT, RANDOM
	{ 2, 1 }, { 2, 4 }, { 2, 5 }, { 2, 6 },             // UNDO, STORE, MULTI, SINGLE
	{ 3, 1 }, { 3, 4 }, { 3, 2 }, { 3, 5 },             // PART -, PART +, PARAMETER <, PARAMETER >
	{ 3, 3 }, { 3, 6 },                                 // VALUE -, VALUE +
	{ 4, 2 }, { 4, 3 }, { 4, 4 }, { 4, 5 }, { 4, 6 },   // FILTERS: EDIT, FILT 1, FILT 2, SELECT FILT 1, 2
	{ 2, 2 }, { 2, 3 },                                 // TRANSPOSE -, +
};
static const char* const KEY_NAME[35] = { "LFO edit", "LFO select", "LFO shape", "LFO amount (destination)",
	"Oscillator edit", "Sync", "Oscillator 1", "Oscillator 2", "Oscillator 3", "Oscillator 3 on",
	"Effects edit", "Effects select", "Delay/reverb edit", "Arpeggiator on", "Arpeggiator edit",
	"Edit", "Global / multi edit", "Random", "Undo", "Store", "Multi", "Single (hold: category)",
	"Part -", "Part +", "Parameter < (bank -)", "Parameter > (bank +)", "Value - (program -)",
	"Value + (program +)", "Filter edit", "Filter 1 mode", "Filter 2 mode", "Select filter 1",
	"Select filter 2", "Transpose -", "Transpose +" };

// Every LED of the 80C515's multiplex, as (group, bit): seven groups of two 7-bit latches,
// bits 0-4 and 7-11 used. Mapped by pressing each control and watching which bit answers
// (VirusResearch/NOTES.md, "Panel map"). Three more outputs, group 6 bits 9-11, are lit in
// every state the firmware has been seen in and match no LED in the manual's drawings; they
// are left off the panel. The two RATE LEDs are not here: the DSP drives those.
static const uint8_t LED[67][2] = {
	{ 0, 9 },                                           //  0 LFO EDIT
	{ 1, 4 }, { 1, 7 }, { 1, 8 }, { 1, 9 },             //  1 LFO 1, 2, 3, MOD
	{ 2, 11 }, { 3, 11 }, { 3, 10 }, { 3, 9 }, { 3, 8 },// 5 SHAPE: sine, triangle, saw, square, wave
	{ 4, 10 }, { 4, 11 },                               // 10 OSC EDIT, SYNC
	{ 5, 2 }, { 5, 3 }, { 5, 4 }, { 5, 7 },             // 12 OSC 1, 2, 3, OSC 3 ON
	{ 0, 1 }, { 0, 4 }, { 0, 7 }, { 0, 8 },             // 16 EFFECTS EDIT, DIST, PHA, CHO
	{ 0, 2 },                                           // 20 DELAY/REVERB EDIT
	{ 0, 10 }, { 0, 11 }, { 1, 0 }, { 1, 1 }, { 1, 2 }, { 1, 3 },   // 21 LFO 1: OSC 1, OSC 2, PW, RESO, GAIN, ASSIGN
	{ 1, 10 }, { 1, 11 }, { 2, 0 }, { 2, 1 }, { 2, 2 }, { 2, 3 },   // 27 LFO 2: FILT 1, 2, SHAPE, FM, PAN, ASSIGN
	{ 2, 4 }, { 2, 7 }, { 2, 8 }, { 2, 9 }, { 2, 10 },              // 33 LFO 3: OSC 1, OSC 2, PW 1, PW 2, SYNC PH
	{ 3, 7 }, { 3, 0 }, { 3, 1 }, { 3, 2 }, { 3, 3 }, { 3, 4 },     // 38 MOD: ASSIGN 1-6
	{ 0, 3 }, { 0, 0 },                                 // 44 ARP ON, ARP EDIT
	{ 4, 0 }, { 4, 1 },                                 // 46 EDIT, GLOBAL/MULTI EDIT
	{ 5, 0 }, { 5, 1 },                                 // 48 MULTI, SINGLE
	{ 5, 8 },                                           // 50 FILTERS EDIT
	{ 6, 0 }, { 5, 9 }, { 5, 10 }, { 5, 11 },           // 51 FILT 1: LP, HP, BP, BS
	{ 6, 1 }, { 6, 2 }, { 6, 3 }, { 6, 4 },             // 55 FILT 2: LP, HP, BP, BS
	{ 6, 7 }, { 6, 8 },                                 // 59 SELECT FILT 1, FILT 2
	{ 4, 3 }, { 4, 4 }, { 4, 7 }, { 4, 8 }, { 4, 9 },   // 61 TRANSPOSE -2, -1, 0, +1, +2 octaves
	{ 4, 2 },                                           // 66 BPM
};
// The buttons whose LED is in the button itself (a bezel): SYNC, OSC 3 ON, ARP ON.
static const int BEZEL[3][2] = { { 5, 11 }, { 9, 15 }, { 13, 44 } };   // key index, LED index

// Indices into KEY[] and LED[] that the tests and the module name.
enum Key { K_LFO_SELECT = 1, K_OSC2 = 7, K_MULTI = 20, K_SINGLE = 21, K_FLT1_MODE = 29 };
enum Led { L_LFO1 = 1, L_LFO2 = 2, L_OSC1 = 12, L_OSC2 = 13, L_MULTI = 48, L_SINGLE = 49, L_F1_LP = 51, L_F1_HP = 52 };

} // namespace vc

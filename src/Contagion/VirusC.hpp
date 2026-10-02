// Contagion -- an Access Virus C: its own 80C515 firmware running the front panel, the
// LCD, MIDI and preset memory, and booting and driving its DSP56362 through the HI08 --
// the DSP emulated by dsp56300 (vendor/dsp56300, GPLv3, the core gearmulator runs on).
// Nothing of the host side is reimplemented: what the firmware does, the board does.
//
// The board, as the firmware shows it (research: VirusResearch/NOTES.md):
//   80C515 at 12 MHz. Code $0000-$7FFF = flash $00000-$07FFF; $8000-$FFFF = the 32 KB bank
//   in P5[7:4], for code and MOVX alike. Banks 8-B ($40000-$5FFFF: multis, user banks A
//   and B) are battery RAM, written by STORE with plain MOVX; the rest is flash.
//   XRAM $0000-$7FFF: RAM (global memory, edit buffers); with P3.3 low, $0400-$0407 is the
//   DSP's HI08 host port instead. /HREQ is INT0 (P3.2).
//   LCD: HD44780, 2 x 16, 4-bit on P1 (D4-D7 = P1.1-P1.4, E = P1.5, R/W = P1.6, RS = P1.7).
//   Buttons: 5 x 7 matrix, row P5[2:0], read on P4[6:0] (low = pressed).
//   LEDs: 7 groups (P5[2:0]), two 7-bit latches loaded from P4[6:0] on P4.7 / P5.3 falling.
//   Pots: 32 = ADC channel 0-7 x group P3.4/P3.5. MIDI in on RXD, 31,250 baud.
//
// This header carries no emulator types, so the module (C++11) can include it; VirusC.cpp
// is compiled as C++17 against the vendored DSP core.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace vc {

class VirusC {
public:
	static constexpr double SAMPLE_RATE = 12e6 / 256;   // 46,875 Hz
	static constexpr size_t FLASH_SIZE = 0x80000;
	static constexpr size_t GLOBAL_RAM = 0x8000, BANK_RAM = 0x20000;
	static constexpr int MAX_BLOCK = 1024;

	VirusC();
	~VirusC();

	/** Load a 512 KB flash image (an A/B/C OS). Empty on success, else what is wrong. */
	std::string load(const std::vector<uint8_t>& flash);
	/** The battery RAM as a patch saved it; empty vectors leave the power-on state
	    (global RAM zero, so the firmware initialises it; bank RAM = the image's presets). */
	void setRam(const std::vector<uint8_t>& global, const std::vector<uint8_t>& banks);
	void copyRam(std::vector<uint8_t>& global, std::vector<uint8_t>& banks) const;

	/** Power on and run until the DSP's audio port is live (a few seconds of machine time;
	    call it off the audio thread). False if the firmware never got the DSP running. */
	bool boot();

	/** One block at 46,875 Hz: two inputs in, six outputs (3 stereo pairs) out, volts
	    as the DSP's full scale = 1. The 80C515 runs its share of the block first. */
	void process(const float* inL, const float* inR, float* const out[6], int frames);

	// --- the front panel ---------------------------------------------------------------
	void midi(uint8_t byte);
	void setPot(int index, uint8_t value);        // index = group * 8 + ADC channel
	void setButton(int row, int col, bool down);
	/** The LCD: 32 character codes (2 x 16 as shown) and the 64 bytes of CGRAM. */
	void lcd(uint8_t chars[32], uint8_t cgram[64]) const;
	/** Each LED's brightness since the last call, as the eye sees it: its lit share of
	    its group's multiplex slot (7 groups x 14 bits; see the panel map in Contagion.cpp). */
	void leds(float bright[7][14]);
	/** The two RATE LEDs (LFO 1, LFO 2/3), which the DSP drives from its timers. */
	void rateLeds(float out[2]) const;
	/** Read the microcontroller's external RAM (the firmware's global memory, edit buffers included). */
	void xram(uint16_t addr, uint8_t* out, size_t n) const;
	std::string lcdText() const;
	long dspWordsIn() const;

private:
	struct Impl;
	std::unique_ptr<Impl> impl;
};

/** A character's 5 x 8 dots (bit 4 = leftmost), from the HD44780's A00 character ROM. */
const uint8_t* lcdGlyph(uint8_t code);

} // namespace vc

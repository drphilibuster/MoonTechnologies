// Nordic Banking -- a Clavia Nord Lead 2X: its own MC68331 firmware (on Musashi, instruction by
// instruction) running the panel, MIDI and program memory and driving two DSP56362s, all from the
// 512 KB OS image the user supplies. The machine is gearmulator's n2xLib (vendor/gearmulator,
// GPLv3) on dsp56300 (vendor/dsp56300); this is the part of it the module sees.
//
// The board (gearmulator's n2xtypes.h; research: NordLead2Research/NOTES.md):
//   ROM $000000 (512 KB), RAM $100000 (256 KB), the DSPs' host ports at $200000, the panel at
//   $202000 (CS6: buttons, LEDs, display) and $202800 (CS4: the knob ADC). Program memory is a 64 KB
//   I2C flash. Two DSP56362: A computes half the voices and passes them to B over ESAI; B adds its
//   own and drives four DAC channels at 98,200 Hz.
//   The panel LEDs and the three-digit display are one multiplex of 11 slots (a data byte, then a
//   row or digit select): six rows of eight LEDs, then the digits.
//
// No emulator types here, so the module (C++11) can include it; Nord2x.cpp is C++17.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace nb {

class Nord2x {
public:
	static constexpr double SAMPLE_RATE = 98200.0;
	static constexpr size_t ROM_SIZE = 0x80000, FLASH_SIZE = 0x10000;

	Nord2x();
	~Nord2x();

	/** Empty if the image is a Nord Lead 2X OS, else what is wrong with it. */
	static std::string check(const std::vector<uint8_t>& rom);

	/** Power on with this OS and flash (empty flash = erased) and run until the firmware shows its
	    first program. Blocks for the boot (about a second of machine time): call it off the audio
	    thread. False if the image is not a 2X OS. */
	bool boot(const std::vector<uint8_t>& rom, const std::vector<uint8_t>& flash);

	/** One block at 98.2 kHz: four outputs (A, B, C, D), the DSP's full scale = 1. */
	void process(float* const out[4], int frames);

	// --- the front panel and MIDI (from the audio thread) ---------------------------------
	/** A complete MIDI message: a channel message, or a whole SysEx F0 ... F7. */
	void midi(const uint8_t* bytes, size_t length);
	void setKnob(uint8_t channel, uint8_t value);   // an ADC channel, as in PanelMap.hpp
	void setButton(uint16_t id, bool down);          // a key line, as in PanelMap.hpp

	/** Each multiplexed LED's and display segment's brightness since the last call, as the eye sees
	    the multiplex: its lit share of its own row's or digit's slots. Rows 0-5 x bits 0-7; digits
	    0-2 x segments (bit 7 top, 1 middle, 4 bottom, 2 upper left, 6 upper right, 3 lower left,
	    5 lower right, 0 point). Any thread. */
	void leds(float rows[6][8], float digits[3][8]);

	/** The flash as it is now, for the patch (64 KB). Any thread. */
	void copyFlash(std::vector<uint8_t>& out) const;

private:
	struct Impl;
	std::unique_ptr<Impl> impl;
};

} // namespace nb

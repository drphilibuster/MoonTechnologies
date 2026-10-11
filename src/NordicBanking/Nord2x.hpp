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
#include <functional>
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

	// --- the machine on a thread of its own ------------------------------------------------
	// process() makes the caller wait for the emulated DSPs and the 68331, so on a busy audio thread one late
	// thread is a gap in the sound. The stream instead runs the machine on a worker, ahead of the reader, into a
	// ring of `targetFrames` (at 98.2 kHz); read() only copies, never waits. Use the stream or process(), not both.
	struct StreamStats {
		uint64_t underruns = 0;      // read() calls that found the ring short
		uint64_t starvedFrames = 0;  // frames it was short by
		double worstBlockMs = 0;     // the longest the worker took over one block of 128 frames
		uint32_t minFill = 0;        // the lowest the ring has been, in frames, after a read
		uint32_t fill = 0, target = 0;
	};
	/** Start the worker. `log`, when given, is called from it (never from the reader) with a line whenever
	    reads have run short. */
	void startStream(uint32_t targetFrames, std::function<void(const std::string&)> log = {});
	void stopStream();
	void setStreamTarget(uint32_t targetFrames);
	/** Up to `frames` frames of the four outputs; returns how many there were (fewer than asked = an underrun,
	    and 0 until the ring has first filled). Wait-free: one reader thread. */
	size_t read(float* const out[4], size_t frames);
	StreamStats streamStats(bool reset = false);

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

	/** The 68331's RAM (256 KB at $100000), `length` bytes from `offset`, as the firmware sees it now: the
	    program edit buffer is in it. Any thread; the firmware may be writing, so a multi-byte value can
	    be caught half written. */
	void ram(uint32_t offset, uint8_t* out, size_t length) const;

	/** The flash as it is now, for the patch (64 KB). Any thread. */
	void copyFlash(std::vector<uint8_t>& out) const;

private:
	struct Impl;
	std::unique_ptr<Impl> impl;
};

} // namespace nb

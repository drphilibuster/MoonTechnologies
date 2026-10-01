// Rebate -- the digital half of an Alesis MIDIverb (or MIDIFex): the 80C31 running
// its own firmware, and the discrete DSP running the microcode EPROM, on one
// 6 MHz clock. The analog half -- filters, converters' hold capacitors, the mix
// pot -- is Analog.hpp.
//
// The DSP step below is a port of the loop in MAME's alesis/midiverb.cpp
// (BSD-3-Clause, copyright m1macrophage), restructured to run one instruction at
// a time; tests/Rebate checks it sample for sample against that loop.
//
// The board, from MAME's alesis/midiverb.cpp (BSD-3-Clause, m1macrophage) and
// Eric Brombaugh's schematic and Verilog of it (MIDIVerb_RE, MIT):
//   - 80C31 at 6 MHz. Program ROM U54 (2764) with A12 tied high, so its code is
//     the image's upper 4 KB, mirrored through $0000-$7FFF. MOVX writes, any
//     address, latch the seven-segment pattern; P1.0/P1.1 select a digit (low).
//     P1.2-P1.7 are the DSP program: the top six address bits of the microcode
//     ROM. P3.0 is MIDI in; P3.2-P3.5 are CHANNEL, UP, DOWN, DEFEAT (low).
//   - The DSP steps through 128 two-byte instructions per sample, two clocks
//     each: 23,437.5 Hz. A 2-bit opcode and a 14-bit delay-RAM delta, pipelined:
//     the delta of instruction n-1 and the opcode of n-2 act at step n. The ADC
//     word is on the bus at step 0; the DAC takes it at steps 0x60 (right) and
//     0x70 (left). 16K x 16 delay RAM.
//
// The CPU and the DSP are interleaved on the oscillator clock, so a program
// change reaches the DSP at the instruction it would on the board: the six
// program bits go straight to the ROM's address pins, with no latch.
#pragma once
#include "Mcs51.hpp"

#include <array>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

namespace mv {

/** CRC-32 (IEEE), to name a ROM image. */
inline uint32_t crc32(const std::vector<uint8_t>& d) {
	uint32_t c = 0xffffffffu;
	for (uint8_t b : d) {
		c ^= b;
		for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1)));
	}
	return ~c;
}

/** The dumps this was verified against. Others of the right size are run too. */
inline std::string romName(uint32_t crc) {
	switch (crc) {
	case 0x14d6596d: return "MVOP 4-7-86";
	case 0xde8ca203: return "MIDIverb MVOBJ 2-6-86";
	case 0x1aa25250: return "MIDIverb MVOBJ 2-6-86 (27256)";
	case 0x098cc1b4: return "MIDIFEX 7-17-86";
	default: return "";
	}
}

struct Machine : Mcs51Bus {
	static constexpr double CLOCK = 6000000.0;
	static constexpr int CLOCKS_PER_SAMPLE = 256;
	static constexpr double SAMPLE_RATE = CLOCK / CLOCKS_PER_SAMPLE;   // 23,437.5 Hz
	enum Button { CHANNEL, UP, DOWN, DEFEAT };

	Mcs51 cpu;

	/** Load the images. CPU: the 8 KB 2764 (code in its upper half) or just those
	    4 KB. DSP: 16 KB, or a 32 KB 27256 of which the board uses the upper half. */
	std::string load(const std::vector<uint8_t>& cpuImage, const std::vector<uint8_t>& dspImage) {
		if (cpuImage.size() == 0x2000) cpuRom.assign(cpuImage.begin() + 0x1000, cpuImage.end());
		else if (cpuImage.size() == 0x1000) cpuRom = cpuImage;
		else return "CPU ROM NOT 8 KB";
		if (dspImage.size() == 0x4000) dspRom = dspImage;
		else if (dspImage.size() == 0x8000) dspRom.assign(dspImage.begin() + 0x4000, dspImage.end());
		else return "DSP ROM NOT 16 KB";
		return "";
	}

	void powerOn() {
		cpu.bus = this;
		dram.fill(0);
		accum = reg = 0;
		ramOffset = 0;
		opLatch = 0;
		program = 0x3f;          // P1 resets high: the silent program until the firmware picks one
		pendingN = 0;
		clock = 0;
		segLatch = 0x7f;
		digit[0] = digit[1] = 0;
		midiQ.clear();
		midiBusy = false;
		lineFree = 0;
		replay.clear();
		for (bool& b : pressed) b = false;
		conflicts = floating = 0;
		cpu.reset();
	}

	/** One DSP sample, interleaved with the CPU. `adc` is the converter's word
	    (13 bits, sign-extended), on the bus at step 0. Returns the two DAC words. */
	void sample(uint16_t adc, int16_t& right, int16_t& left) {
		if (!replay.empty()) {
			while (!replay.empty() && replay.front().at <= replayClock) {
				const Action a = replay.front();
				replay.pop_front();
				if (a.kind == MIDI) midi(uint8_t(a.arg));
				else button(Button(a.arg), a.kind == PRESS);
			}
			replayClock++;
		}
		for (int pc = 0; pc < 128; pc++) {
			const uint64_t t = clock + 2 * uint64_t(pc);
			while (cpu.cycles * 12 <= t) cpu.step();
			while (pendingN && pending[0].at <= t) {
				program = uint8_t(pending[0].value >> 2);
				for (int i = 1; i < pendingN; i++) pending[i - 1] = pending[i];
				pendingN--;
			}
			step(pc, adc, right, left);
		}
		clock += CLOCKS_PER_SAMPLE;
	}

	/** Clear the DSP and point it at `prog` as MAME's first pass finds it: the
	    opcode in flight is instruction 126's. For the tests and the audit. */
	void dspReset(uint8_t prog) {
		dram.fill(0);
		accum = reg = ramOffset = 0;
		program = uint8_t(prog & 0x3f);
		opLatch = uint8_t(dspRom[(unsigned(program) << 8) + 2 * 126 + 1] >> 6);
	}

	/** The DSP alone for one sample, on `prog`, with the CPU held: for the tests'
	    comparison with MAME's loop and the ROM audit. */
	void dspSample(uint8_t prog, uint16_t adc, int16_t& right, int16_t& left) {
		program = uint8_t(prog & 0x3f);
		for (int pc = 0; pc < 128; pc++) step(pc, adc, right, left);
	}

	// --- the front panel ---------------------------------------------------------------
	void button(Button b, bool down) { pressed[b] = down; }
	/** The two digits' segments, a..g in bits 0..6, as last lit by the multiplex. */
	uint8_t segments(int d) const { return digit[d]; }
	int dspProgram() const { return program; }

	/** A MIDI byte into the DIN: it is sent down the line at 31,250 baud behind
	    whatever is already on its way. */
	void midi(uint8_t b) { if (midiQ.size() < 4096) midiQ.push_back(b); }

	long conflicts = 0, floating = 0;   // microcode bus errors (should stay 0)

	// --- what the user set, and getting it back --------------------------------------------
	// The MIDIverb keeps nothing over a power cycle: it always wakes on program 22,
	// MIDI channel 1. A patch remembers what was set, and after power-on the module
	// plays it back through the firmware's own inputs -- the buttons and the MIDI
	// DIN -- the way a person (or a sequencer) would. Read from the firmware's RAM:
	// $20 the program (bit 7: defeat), $24 $C0 + the MIDI channel.
	struct Settings { int program = 21; bool defeat = false; int channel = 0; };
	Settings settings() const {
		Settings s;
		s.program = cpu.ram[0x20] & 0x3f;
		s.defeat = cpu.ram[0x20] & 0x80;
		s.channel = cpu.ram[0x24] & 0x0f;
		return s;
	}
	bool replaying() const { return !replay.empty(); }

	/** Queue the presses and the program change that bring a unit fresh from
	    power-on to `s`. Played from sample(), in the unit's own time. */
	void restore(const Settings& s) {
		replay.clear();
		const long ms = long(SAMPLE_RATE / 1000);
		long t = 900 * ms;                       // the firmware ignores MIDI for its first half second
		auto press = [&](Button b) {
			replay.push_back({ t, PRESS, b }); t += 40 * ms;
			replay.push_back({ t, RELEASE, b }); t += 80 * ms;
		};
		if (s.channel) {
			// The channel shows, and UP/DOWN change it, only while CHANNEL is held.
			replay.push_back({ t, PRESS, CHANNEL }); t += 80 * ms;
			for (int i = 0; i < s.channel; i++) press(UP);
			replay.push_back({ t, RELEASE, CHANNEL }); t += 80 * ms;
		}
		if (s.program != 21 && s.program < 63) {
			replay.push_back({ t, MIDI, 0xc0 + s.channel });
			replay.push_back({ t, MIDI, s.program });
			t += 250 * ms;
		}
		if (s.defeat) press(DEFEAT);
		replayClock = 0;
	}

	// --- Mcs51Bus -------------------------------------------------------------------------
	uint8_t code(uint16_t a) override { return a < 0x8000 ? cpuRom[a & 0x0fff] : 0xff; }
	void xwrite(uint16_t, uint8_t v) override {
		// The data bus reaches the latch, and the latch the segments, scrambled
		// (MAME: bitswap<7>(data, 1, 6, 7, 4, 0, 2, 5)); the segments are active low.
		static const int from[7] = { 5, 2, 0, 4, 7, 6, 1 };   // segment a..g <- data bit
		uint8_t s = 0;
		for (int i = 0; i < 7; i++) if (!(v >> from[i] & 1)) s |= uint8_t(1 << i);
		segLatch = s;
		lightDigits();
	}
	void portOut(int p, uint8_t v) override {
		if (p != 1) return;
		p1 = v;
		lightDigits();
		if (pendingN < int(pending.size())) pending[pendingN++] = { cpu.instrEnd * 12, v };
	}
	uint8_t pins(int p) override {
		if (p != 3) return 0xff;
		uint8_t v = 0xff;
		if (!rxd(cpu.cycles)) v &= 0xfe;
		for (int b = 0; b < 4; b++) if (pressed[b]) v &= uint8_t(~(0x04 << b));
		return v;
	}
	bool rxd(uint64_t cycle) override {
		// 31,250 baud at 500,000 machine cycles a second: 16 cycles a bit.
		for (;;) {
			if (!midiBusy) {
				if (midiQ.empty()) return true;
				midiByte = midiQ.front();
				midiQ.pop_front();
				midiStart = cycle > lineFree ? cycle : lineFree;   // back to back behind the last
				midiBusy = true;
			}
			if (cycle < midiStart) return true;
			const uint64_t bit = (cycle - midiStart) / 16;
			if (bit >= 10) { midiBusy = false; lineFree = midiStart + 160; continue; }
			if (bit == 0) return false;
			if (bit == 9) return true;
			return midiByte >> (bit - 1) & 1;
		}
	}

private:
	std::vector<uint8_t> cpuRom = std::vector<uint8_t>(0x1000, 0xff);
	std::vector<uint8_t> dspRom = std::vector<uint8_t>(0x4000, 0);
	std::array<uint16_t, 0x4000> dram{};
	uint16_t accum = 0, reg = 0, ramOffset = 0;
	uint8_t opLatch = 0, program = 0x3f, p1 = 0xff;
	uint64_t clock = 0;
	struct Pending { uint64_t at; uint8_t value; };
	std::array<Pending, 8> pending{};
	int pendingN = 0;
	bool pressed[4] = {};
	uint8_t segLatch = 0x7f, digit[2] = {};
	std::deque<uint8_t> midiQ;
	bool midiBusy = false;
	uint8_t midiByte = 0;
	uint64_t midiStart = 0, lineFree = 0;
	enum Kind { PRESS, RELEASE, MIDI };
	struct Action { long at; Kind kind; int arg; };
	std::deque<Action> replay;
	long replayClock = 0;

	void lightDigits() {
		for (int d = 0; d < 2; d++) if (!(p1 >> d & 1)) digit[d] = segLatch;
	}

	/** One DSP instruction (MAME's sound_stream_update, one step of it). */
	void step(int pc, uint16_t adc, int16_t& right, int16_t& left) {
		const uint8_t* w = &dspRom[(unsigned(program) << 8) + 2 * ((pc + 127) & 127)];
		const int op = opLatch;
		const uint16_t delta = uint16_t((w[1] & 0x3f) << 8 | w[0]);
		opLatch = uint8_t(w[1] >> 6);

		const bool modeRc0 = pc == 0;                     // ADC onto the bus
		const bool ldDac = pc == 0x60 || pc == 0x70;      // DAC takes the bus
		const bool ldDsp = !modeRc0 && !ldDac;            // accumulator/register load
		const bool rdR0 = op == 2, rdR1 = op == 3;
		const bool dramW = (op & 2) || modeRc0;
		uint16_t bus = 0;
		int writers = 0;
		if (modeRc0) { bus = adc; writers++; }
		if (rdR0) { bus = reg; writers++; }
		if (rdR1) { bus = uint16_t(~reg); writers++; }
		if (!dramW) { bus = dram[ramOffset]; writers++; }
		if (!writers) floating++;
		else if (writers > 1) conflicts++;
		if (dramW) dram[ramOffset] = bus;
		if (ldDac) (pc == 0x70 ? left : right) = int16_t(bus);
		if (op & 1) accum = 0;
		if (ldDsp) {
			const uint16_t sign = bus >> 15;
			accum = uint16_t(accum + ((sign << 15) | (bus >> 1)) + sign);
			reg = accum;
		}
		ramOffset = uint16_t((ramOffset + delta) & 0x3fff);
	}
};

} // namespace mv

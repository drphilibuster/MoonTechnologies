// Apportionment -- one Ensoniq ES5510 "ESP", as the DP/4's host sees it.
//
// The DSP itself is MAME's es5510 core (vendor/mame/es5510, BSD-3-Clause,
// Christian Brunschen), compiled in Esp.cpp against a small stand-in for MAME's
// framework. This header is the only view of it the rest of the module has, so
// nothing MAME-shaped leaks into a translation unit that also includes Rack.
//
// What the facade adds on top of MAME, all from the ESP specification Rev. 2.4:
//  * The host access handshake (sec. 5.1.2). While the ESP runs, a GPR or INSTR
//    transfer requested through $80/$A0/$C0/$E0 is performed at the next END,
//    one per sample period, and Host Access OK/ ($12 bit 2, low = OK) reads 1
//    until then. While halted, transfers are immediate. The firmware polls for
//    it and times its writes around it; MAME performs them instantly.
//  * $16 reads the live program counter, and 0 while halted (sec. 5.3.1: HALT
//    restarts the PC). The firmware waits for PC <= $40 before each transfer.
#pragma once
#include <cstdint>
#include <memory>

namespace dp4 {

class Esp {
public:
	/** Serial register indices for ser(): left/right of each of the four ports. */
	enum { S0L, S0R, S1L, S1R, S2L, S2R, S3L, S3R };
	/** 64K words of delay RAM per ESP, as on the DP/4 main board. */
	static constexpr int DP4_DRAM_WORDS = 0x10000;

	Esp();
	~Esp();
	Esp(const Esp&) = delete;
	Esp& operator=(const Esp&) = delete;

	void reset();
	uint8_t hostRead(int offset);
	void hostWrite(int offset, uint8_t data);
	void setHalt(bool asserted);
	/** Execute `steps` microinstructions (fewer if it halts). */
	void run(int steps);

	bool halted() const;
	int pc() const;
	int16_t serRead(int reg) const;
	void serWrite(int reg, int16_t value);

	// Introspection, for tests.
	int32_t gpr(int reg) const;
	int16_t dram(int addr) const;
	uint64_t transfersDeferred() const { return deferred_; }
	uint64_t transferCollisions() const { return collisions_; }

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
	bool pending_ = false;
	uint8_t pendOffset_ = 0, pendData_ = 0;
	uint64_t deferred_ = 0, collisions_ = 0;
	void completeTransfer();
	bool running() const;
};

} // namespace dp4

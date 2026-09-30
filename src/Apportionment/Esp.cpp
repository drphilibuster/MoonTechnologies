// Apportionment -- compiles MAME's ES5510 core (vendor/mame/es5510, BSD-3-Clause,
// Christian Brunschen) against the framework stand-in beside it, and wraps it in
// the dp4::Esp facade. See Esp.hpp. This is the only translation unit that sees
// MAME's headers; it must not include Rack.
//
// Standard headers first: MAME's private state is opened up for the facade
// with a #define that must not reach them.
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <memory>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

#include "../../vendor/mame/es5510/emu.h"
#define private public
#define protected public
#include "../../vendor/mame/es5510/es5510.cpp"
#include "../../vendor/mame/es5510/es5510d.cpp"
#undef private
#undef protected

#include "Esp.hpp"

namespace dp4 {

static machine_config s_mconfig;
static address_space s_space;

static_assert(es5510_device::DRAM_SIZE == Esp::DP4_DRAM_WORDS, "the patched core's DRAM is the DP/4's");

/** es5510_device::alu_operation, verbatim but for being inlinable (the
    original is a member the compiler will not fold into the loop). */
static inline __attribute__((always_inline)) int32_t aluOperation(uint8_t op, int32_t a, int32_t b, uint8_t& flags) {
	int32_t tmp;
	switch (op) {
	case 0x0: tmp = add(a, b, flags); return saturate(tmp, flags, (a & 0x00800000) != 0);            // ADD
	case 0x1: tmp = add(a, negate(b), flags); return saturate(tmp, flags, (a & 0x00800000) != 0);    // SUB
	case 0x2: return add(a, b, flags);                                                                // ADDU
	case 0x3: return add(a, negate(b), flags);                                                        // SUBU
	case 0x4: add(a, negate(b), flags); return a;                                                     // CMP
	case 0x5: a &= b; flags = setFlagTo(flags, FLAG_N, (a & 0x00800000) != 0); flags = setFlagTo(flags, FLAG_Z, a == 0); return a;   // AND
	case 0x6: a |= b; flags = setFlagTo(flags, FLAG_N, (a & 0x00800000) != 0); flags = setFlagTo(flags, FLAG_Z, a == 0); return a;   // OR
	case 0x7: a ^= b; flags = setFlagTo(flags, FLAG_N, (a & 0x00800000) != 0); flags = setFlagTo(flags, FLAG_Z, a == 0); return a;   // XOR
	case 0x8: {                                                                                       // ABS
		flags = clearFlag(flags, FLAG_N);
		const bool isNegative = (b & 0x00800000) != 0;
		flags = setFlagTo(flags, FLAG_C, isNegative);
		const int32_t result = isNegative ? (0x00ffffff ^ b) : b;
		flags = setFlagTo(flags, FLAG_Z, (result & 0x00ffffff) == 0);
		return result;
	}
	case 0x9: return b;                                                                               // MOV
	case 0xa: return asl(b, 2, flags);                                                                // ASL2
	case 0xb: return asl(b, 8, flags);                                                                // ASL8
	case 0xc:                                                                                         // LS15
		flags = clearFlag(flags, FLAG_N);
		flags = setFlagTo(flags, FLAG_C, (b & 0x00800000) != 0);
		return (b << 15) & 0x007fffff;
	case 0xd: return add(0x007fffff, negate(b), flags);                                              // DIFF
	case 0xe:                                                                                         // ASR
		flags = setFlagTo(flags, FLAG_N, (b & 0x00800000) != 0);
		flags = setFlagTo(flags, FLAG_C, (b & 1) != 0);
		return (b >> 1) | (b & 0x00800000);
	default: return 0;                                                                                // END
	}
}

/** One microinstruction's fields, taken apart once when the host writes it
    instead of on every step it runs. */
struct Decoded {
	uint8_t aReg, bReg, cReg, dReg, op;
	es5510_device::ram_cycle_t ramCycle;             // RAM_CONTROL[field], looked up once
	es5510_device::ram_control_access_t ramAccess;
	es5510_device::op_src_dst_t aluSrc, aluDst, macSrc, macDst;   // OPERAND_SELECT[field]
	bool skippable, accumulate;
};

static Decoded decode(uint64_t instr) {
	Decoded c;
	c.aReg = (instr >> 16) & 0xff;
	c.bReg = (instr >> 24) & 0xff;
	c.cReg = (uint8_t)((instr >> 32) & 0xff);
	c.dReg = (uint8_t)((instr >> 40) & 0xff);
	c.op = (instr >> 12) & 0x0f;
	const es5510_device::ram_control_t& ramControl = es5510_device::RAM_CONTROL[(instr >> 3) & 0x07];
	c.ramCycle = ramControl.cycle;
	c.ramAccess = ramControl.access;
	const es5510_device::op_select_t& opSelect = es5510_device::OPERAND_SELECT[(instr >> 8) & 0x0f];
	c.aluSrc = opSelect.alu_src; c.aluDst = opSelect.alu_dst;
	c.macSrc = opSelect.mac_src; c.macDst = opSelect.mac_dst;
	c.skippable = (instr & (0x01 << 7)) != 0;
	c.accumulate = ((instr >> 6) & 0x01) != 0;
	return c;
}

/** es5510_device::execute_run, restated for speed and nothing else.
 *
 * MAME keeps every register of the chip in the device object, so each store
 * to GPR or DRAM makes the compiler reload all of them; that is most of what
 * the ESPs cost. This is the same machine, step for step and in the same order
 * -- RAM cycle N-2 read, RAM cycle N address, multiplier write N-1 and start N,
 * ALU write N-1 and start N, RAM cycle N-1 write/dump -- with the working state
 * held in locals and written back when it returns. It calls MAME's own helpers
 * (add, saturate, asl, alu_operation, round_24_to_16) for the arithmetic, and
 * carries the DP/4 patches in es5510-dp4.patch. tests/Apportionment checks it
 * against execute_run itself, frame by frame, on the real firmware. */
static void runFast(es5510_device& d, const Decoded* code, int steps) {
	using es = es5510_device;
	int32_t* const gpr = d.gpr.get();
	int16_t* const dram = d.dram.get();

	uint8_t pc = d.pc;
	es::state_t state = d.state;
	const bool haltAsserted = d.halt_asserted;
	int64_t machl = d.machl;
	bool macOverflow = d.mac_overflow;
	bool prevSkippable = d.prev_skippable;
	int16_t dil = d.dil;
	int32_t memmask = d.memmask, memincrement = d.memincrement;
	int8_t memshift = d.memshift;
	int32_t dlength = d.dlength, abase = d.abase, bbase = d.bbase, dbase = d.dbase;
	int32_t sigreg = d.sigreg;
	int mulshift = d.mulshift;
	int8_t ccr = d.ccr, cmr = d.cmr;
	int16_t dol0 = d.dol[0], dol1 = d.dol[1];
	int dolCount = d.dol_count;
	es::alu_t alu = d.alu;
	es::mulacc_t mulacc = d.mulacc;
	es::ram_t ram = d.ram, ramP = d.ram_p, ramPP = d.ram_pp;
	uint8_t hostControl = d.host_control;

	auto readReg = [&](uint8_t reg) __attribute__((always_inline)) -> int32_t {
		if (reg < 0xc0) return gpr[reg];
		switch (reg) {
		case 234: return d.ser0r << 8;
		case 235: return d.ser0l << 8;
		case 236: return d.ser1r << 8;
		case 237: return d.ser1l << 8;
		case 238: return d.ser2r << 8;
		case 239: return d.ser2l << 8;
		case 240: return d.ser3r << 8;
		case 241: return d.ser3l << 8;
		case 242: return macOverflow ? (machl < 0 ? 0x00000000 : 0x00ffffff) : (machl >> 0) & 0x00ffffff;
		case 243: return macOverflow ? (machl < 0 ? 0x00800000 : 0x007fffff) : (machl >> 24) & 0x00ffffff;
		case 244: return dil << 8;
		case 245: return dlength;
		case 246: return abase;
		case 247: return bbase;
		case 248: return dbase;
		case 249: return sigreg;
		case 250: return ccr << 16;
		case 251: return cmr << 16;
		case 252: return 0x00ffffff;
		case 253: return 0x00800000;
		case 254: return 0x007fffff;
		default: return 0;
		}
	};

	auto writeReg = [&](uint8_t reg, int32_t value) __attribute__((always_inline)) {
		value &= 0x00ffffff;
		if (reg < 0xc0) { gpr[reg] = value; return; }
		switch (reg) {
		case 234: d.ser0r = ((value >> 8) & 0xffff); break;
		case 235: d.ser0l = ((value >> 8) & 0xffff); break;
		case 236: d.ser1r = ((value >> 8) & 0xffff); break;
		case 237: d.ser1l = ((value >> 8) & 0xffff); break;
		case 238: d.ser2r = ((value >> 8) & 0xffff); break;
		case 239: d.ser2l = ((value >> 8) & 0xffff); break;
		case 240: d.ser3r = ((value >> 8) & 0xffff); break;
		case 241: d.ser3l = ((value >> 8) & 0xffff); break;
		case 242: {
			const int64_t masked = machl & (s64(0x00ffffffU) << 24);
			const int64_t shifted = (int64_t)(value & 0x00ffffff) << 0;
			machl = util::sext(masked | shifted, 48);
			break;
		}
		case 243: {
			const int64_t masked = machl & (s64(0x00ffffffU) << 0);
			const int64_t shifted = (int64_t)(value & 0x00ffffff) << 24;
			machl = util::sext(masked | shifted, 48);
			macOverflow = false;
			break;
		}
		case 244:
			memshift = countLowOnes(value);
			d.memsiz = 0x00ffffff >> (24 - memshift);
			memmask = 0x00ffffff & ~d.memsiz;
			memincrement = 1 << memshift;
			dbase &= memmask;
			break;
		case 245: dlength = value; break;
		case 246: abase = value; break;
		case 247: bbase = value; break;
		case 248: dbase = value; break;
		case 249: sigreg = value; mulshift = BIT(sigreg, 22) ? 1 : 2; break;
		case 250: ccr = (value >> 16) & FLAG_MASK; break;
		case 251: cmr = (value >> 16) & (FLAG_MASK | FLAG_NOT); break;
		default: break;
		}
	};

	auto writeDol = [&](int32_t value) __attribute__((always_inline)) {
		const int16_t dol16 = round_24_to_16(value);
		if (dolCount >= 2) { dol0 = dol1; dol1 = dol16; }
		else if (dolCount == 1) { dol1 = dol16; dolCount = 2; }
		else { dol0 = dol16; dolCount = 1; }
	};

	for (int n = steps; n > 0; --n) {
		if (state == es::STATE_HALTED) {
			if (haltAsserted) {
				hostControl &= ~0x04;
				continue;
			}
			state = es::STATE_RUNNING;
			hostControl |= 0x04;
			pc = 0;
			continue;
		}

		ramPP = ramP;
		ramP = ram;

		// T0: instruction N; RAM cycle N-2 read lands in DIL.
		const Decoded& c = code[pc];
		if (ramPP.cycle != es::RAM_CYCLE_WRITE)
			dil = ramPP.io ? 0 : dram[ramPP.address & es::DRAM_MASK];

		// RAM cycle N: its address.
		ram.cycle = c.ramCycle;
		ram.io = c.ramAccess == es::RAM_CONTROL_IO;
		const int32_t offset = pc < 0xc0 ? gpr[pc] : 0;
		switch (c.ramAccess) {
		case es::RAM_CONTROL_DELAY: {
			// (dbase + offset) % length, without the divide in the usual case: both are 24-bit and non-negative.
			const int32_t length = dlength + memincrement;
			int32_t sum = dbase + offset;
			if (sum >= length) { sum -= length; if (sum >= length) sum %= length; }
			ram.address = (sum & memmask) >> memshift;
			break;
		}
		case es::RAM_CONTROL_TABLE_A: ram.address = ((abase + offset) & memmask) >> memshift; break;
		case es::RAM_CONTROL_TABLE_B: ram.address = ((bbase + offset) & memmask) >> memshift; break;
		case es::RAM_CONTROL_IO: ram.address = offset & 0x00fffff0; break;
		}

		// T1: instruction N-1's skip condition, sampled as its results are written
		// (the DP/4 patch); then its multiplier result.
		const bool skippable = c.skippable;
		bool skip = false;
		if (prevSkippable) {
			skip = (ccr & cmr & FLAG_MASK) != 0;
			if (isFlagSet(cmr, FLAG_NOT)) skip = !skip;
		}
		prevSkippable = skippable;
		if (mulacc.write_result && !skip) {
			mulacc.product = mul_32x32(util::sext(mulacc.cValue, 24), util::sext(mulacc.dValue, 24)) << mulshift;
			mulacc.result = mulacc.accumulate ? mulacc.product + machl : mulacc.product;
			macOverflow = mulacc.result < -(s64(1) << 47) || mulacc.result >= (s64(1) << 47);
			machl = mulacc.result;
			const int32_t tmp = macOverflow ? (machl < 0 ? 0x00800000 : 0x007fffff) : (mulacc.result & 0x0000ffffff000000ULL) >> 24;
			if (mulacc.dst & es::SRC_DST_REG) writeReg(mulacc.cReg, tmp);
			if (mulacc.dst & es::SRC_DST_DELAY) writeDol(tmp);
		}

		// Start multiplier N.
		mulacc.cReg = c.cReg;
		mulacc.dReg = c.dReg;
		mulacc.src = c.macSrc;
		mulacc.dst = c.macDst;
		mulacc.accumulate = c.accumulate;
		mulacc.write_result = true;
		if (mulacc.src == es::SRC_DST_REG) mulacc.cValue = readReg(mulacc.cReg);
		else mulacc.cValue = uint32_t(dil << 8);
		mulacc.dValue = readReg(mulacc.dReg);

		// T2: write ALU result N-1.
		if (alu.write_result && (!skip || alu.op == OP_CMP)) {
			uint8_t flags = ccr;
			alu.result = aluOperation(alu.op, alu.aValue, alu.bValue, flags);
			if (alu.op != OP_CMP) {
				if (alu.dst & es::SRC_DST_REG) writeReg(alu.aReg, alu.result);
				if (alu.dst & es::SRC_DST_DELAY) writeDol(alu.result);
			}
			if (alu.update_ccr) ccr = flags;
		}

		// Start ALU N.
		alu.aReg = c.aReg;
		alu.bReg = c.bReg;
		alu.op = c.op;
		alu.src = c.aluSrc;
		alu.dst = c.aluDst;
		alu.write_result = true;
		alu.update_ccr = !skippable || (alu.op == OP_CMP);
		if (alu.op == 0xf) {
			// END (alu_operation_end, with the DP/4 patches).
			if (haltAsserted) {
				state = es::STATE_HALTED;
				hostControl &= ~0x04;
				dolCount = 0;
			}
			dbase -= memincrement;
			if (dbase < 0) dbase = dlength;
			if (state == es::STATE_RUNNING) pc = 0xff;
		} else if (es::ALU_OPS[alu.op].operands == 1) {
			alu.bValue = alu.src == es::SRC_DST_REG ? readReg(alu.bReg) : dil << 8;
		} else {
			alu.aValue = alu.src == es::SRC_DST_REG ? readReg(alu.aReg) : dil << 8;
			alu.bValue = readReg(alu.bReg);
		}

		// RAM cycle N-1: write the front of the DOL, or dump it.
		if (ramP.cycle != es::RAM_CYCLE_READ) {
			if (ramP.cycle == es::RAM_CYCLE_WRITE && !ramP.io) dram[ramP.address & es::DRAM_MASK] = dol0;
			dol0 = dol1;
			if (dolCount > 0) --dolCount;
		}

		++pc;
	}

	d.icount = std::min(steps, 0);
	d.pc = pc;
	d.state = state;
	d.machl = machl;
	d.mac_overflow = macOverflow;
	d.prev_skippable = prevSkippable;
	d.dil = dil;
	d.memmask = memmask; d.memincrement = memincrement; d.memshift = memshift;
	d.dlength = dlength; d.abase = abase; d.bbase = bbase; d.dbase = dbase;
	d.sigreg = sigreg; d.mulshift = mulshift;
	d.ccr = ccr; d.cmr = cmr;
	d.dol[0] = dol0; d.dol[1] = dol1;
	d.dol_count = dolCount;
	d.alu = alu;
	d.mulacc = mulacc;
	d.ram = ram; d.ram_p = ramP; d.ram_pp = ramPP;
	d.host_control = hostControl;
}

struct Esp::Impl {
	es5510_device dev{s_mconfig, "esp", nullptr, 0};
	/** All 256 values of the 8-bit PC: past step 159 there is no instruction
	    memory, and the patched core reads a zero word there, as does this. */
	Decoded code[256];
	void decodeAll() { for (int i = 0; i < 256; i++) code[i] = decode(i < 160 ? dev.instr[i] : 0); }
	/** host_w, keeping the decoded program in step with the one MAME holds. */
	void hostWrite(uint8_t offset, uint8_t data) {
		dev.host_w(offset, data);
		if ((offset == 0xc0 || offset == 0xe0) && data < 0xa0) code[data] = decode(dev.instr[data]);
	}
};

Esp::Esp() : impl_(new Impl) {
	impl_->dev.device_start();
	impl_->dev.device_reset();
	impl_->decodeAll();
}

Esp::~Esp() = default;

void Esp::reset() {
	impl_->dev.device_reset();
	impl_->decodeAll();
	pending_ = false;
}

bool Esp::running() const { return impl_->dev.state == es5510_device::STATE_RUNNING; }
bool Esp::halted() const { return impl_->dev.state == es5510_device::STATE_HALTED; }
int Esp::pc() const { return impl_->dev.pc; }
int16_t Esp::serRead(int reg) const { return impl_->dev.ser_r(reg); }
void Esp::serWrite(int reg, int16_t v) { impl_->dev.ser_w(reg, v); }
void Esp::setHalt(bool a) { impl_->dev.set_HALT(a); }
int32_t Esp::gpr(int r) const { return impl_->dev.gpr[r % 0xc0]; }
int16_t Esp::dram(int a) const { return impl_->dev._dram(a); }

void Esp::completeTransfer() {
	impl_->hostWrite(pendOffset_, pendData_);
	pending_ = false;
}

uint8_t Esp::hostRead(int o) {
	switch (o) {
	case 0x12: return uint8_t((impl_->dev.host_control & 0x03) | (pending_ ? 0x04 : 0x00));
	case 0x16: return running() ? impl_->dev.pc : 0;
	default:   return impl_->dev.host_r(s_space, o);
	}
}

void Esp::hostWrite(int o, uint8_t d) {
	const bool transfer = o == 0x80 || o == 0xa0 || o == 0xc0 || o == 0xe0;
	if (!transfer || !running()) {
		if (transfer && pending_) completeTransfer();
		impl_->hostWrite(uint8_t(o), d);
		return;
	}
	if (pending_) {
		// The firmware strobes some write-selects twice in a row: same request.
		if (pendOffset_ == o && pendData_ == d) return;
		// A different request while busy "can upset" the transfer (spec 5.1.2).
		// Complete the older one rather than lose it; the firmware never does this.
		completeTransfer();
		collisions_++;
	}
	pending_ = true;
	pendOffset_ = uint8_t(o);
	pendData_ = d;
	deferred_++;
}

void Esp::execute(int steps) {
	auto& d = impl_->dev;
	if (reference_) { d.icount = steps; d.execute_run(); }
	else runFast(d, impl_->code, steps);
}

void Esp::run(int steps) {
	auto& d = impl_->dev;
	if (!pending_) { execute(steps); return; }
	// A transfer is waiting: step singly so it lands exactly on the END step.
	for (int n = 0; n < steps; n++) {
		const bool atEnd = running() && ((d.instr[d.pc % 160] >> 12) & 0x0f) == 0x0f;
		execute(1);
		if (pending_ && (atEnd || !running())) completeTransfer();
		if (!pending_) {
			if (n + 1 < steps) execute(steps - n - 1);
			return;
		}
	}
}

uint64_t Esp::stateHash() const {
	const auto& d = impl_->dev;
	uint64_t h = 1469598103934665603ull;
	auto mix = [&h](uint64_t v) { h = (h ^ v) * 1099511628211ull; };
	for (int i = 0; i < 0xc0; i++) mix(uint32_t(d.gpr[i]));
	for (int i = 0; i < 160; i++) mix(d.instr[i]);
	for (uint32_t i = 0; i < es5510_device::DRAM_SIZE; i++) mix(uint16_t(d.dram[i]));
	const int64_t scalars[] = { d.pc, d.state, d.machl, d.mac_overflow, d.prev_skippable, d.dil, d.memsiz, d.memmask, d.memincrement,
		d.memshift, d.dlength, d.abase, d.bbase, d.dbase, d.sigreg, d.mulshift, d.ccr, d.cmr, d.dol[0], d.dol[1],
		d.dol_count, d.host_control, d.ser0l, d.ser0r, d.ser1l, d.ser1r, d.ser2l, d.ser2r, d.ser3l, d.ser3r,
		d.alu.aReg, d.alu.bReg, d.alu.src, d.alu.dst, d.alu.op, d.alu.aValue, d.alu.bValue, d.alu.result,
		d.alu.update_ccr, d.alu.write_result, d.mulacc.cReg, d.mulacc.dReg, d.mulacc.src, d.mulacc.dst,
		d.mulacc.accumulate, d.mulacc.cValue, d.mulacc.dValue, d.mulacc.product, d.mulacc.result,
		d.mulacc.write_result, d.ram.address, d.ram.io, d.ram.cycle, d.ram_p.address, d.ram_p.io, d.ram_p.cycle,
		d.ram_pp.address, d.ram_pp.io, d.ram_pp.cycle };
	for (int64_t v : scalars) mix(uint64_t(v));
	return h;
}

} // namespace dp4

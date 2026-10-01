// Rebate -- the MIDIverb's 80C31 (Intel MCS-51), written from Intel's MCS-51
// Microcontroller Family User's Manual: the whole instruction set with its cycle
// counts, timers 0 and 1 in all four modes, the serial port receiving bit by bit
// (it is fed a line, not bytes), and the two-level interrupt system with the
// manual's polling rules.
//
// Time is counted in machine cycles (12 oscillator periods). Everything that
// happens inside a cycle -- a timer increment, the serial receiver's 16x
// sampling, the interrupt flags being latched for polling -- happens in tick(),
// once per cycle, in the order the manual gives.
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>

namespace mv {

/** What the 80C31 sees outside itself. */
struct Mcs51Bus {
	virtual ~Mcs51Bus() = default;
	virtual uint8_t code(uint16_t addr) = 0;
	virtual uint8_t xread(uint16_t) { return 0xff; }
	virtual void xwrite(uint16_t, uint8_t) {}
	/** External drive on a port's pins: 0 bits pull the (quasi-bidirectional) pin low. */
	virtual uint8_t pins(int) { return 0xff; }
	/** A port latch was written. */
	virtual void portOut(int, uint8_t) {}
	/** The RXD pin (P3.0) at machine cycle `cycle`. */
	virtual bool rxd(uint64_t) { return true; }
};

struct Mcs51 {
	Mcs51Bus* bus = nullptr;

	// An 80C31 has 128 bytes of internal RAM.
	uint8_t ram[128] = {};
	uint8_t sfr[128] = {};
	uint16_t pc = 0;
	uint64_t cycles = 0;          // machine cycles since reset
	uint64_t instrEnd = 0;        // the cycle the instruction in progress completes at
	long badIndirect = 0;         // @Ri / stack accesses past the 128 bytes (should stay 0)
	long unsupported = 0;         // serial modes this core does not model (should stay 0)

	enum : uint8_t {
		P0 = 0x80, SP = 0x81, DPL = 0x82, DPH = 0x83, PCON = 0x87, TCON = 0x88, TMOD = 0x89,
		TL0 = 0x8a, TL1 = 0x8b, TH0 = 0x8c, TH1 = 0x8d, P1 = 0x90, SCON = 0x98, SBUF = 0x99,
		P2 = 0xa0, IE = 0xa8, P3 = 0xb0, IP = 0xb8, PSW = 0xd0, ACC = 0xe0, B = 0xf0,
	};

	void reset() {
		for (uint8_t& s : sfr) s = 0;
		S(P0) = S(P1) = S(P2) = S(P3) = 0xff;
		S(SP) = 0x07;
		pc = 0;
		rxBuf = 0;
		inService[0] = inService[1] = false;
		polled = 0;
		blockOnce = false;
		rxActive = false; rxPrev = true; rxDiv = 0; txLeft = 0; t1Half = false;
		t0Prev = t1Prev = true;
		for (int p = 0; p < 4; p++) if (bus) bus->portOut(p, 0xff);
	}

	/** Execute one instruction, and enter an interrupt after it if one is due.
	    Returns the machine cycles consumed. */
	int step() {
		const uint8_t op = fetch();
		const int n = opCycles(op);
		instrEnd = cycles + n;
		blockOnce = false;
		execute(op);
		for (int i = 0; i < n; i++) tick();
		int total = n;
		if (!blockOnce) total += interrupt();
		return total;
	}

	// --- inspection -------------------------------------------------------------
	uint8_t latch(int port) const { return sfr[0x80 + 0x10 * port - 0x80]; }
	uint8_t acc() const { return sfr[ACC - 0x80]; }

private:
	uint8_t rxBuf = 0;
	bool inService[2] = {};
	uint8_t polled = 0;           // interrupt requests as latched at the end of the previous cycle
	bool blockOnce = false;       // RETI or a write to IE/IP: the next instruction runs first
	// serial
	bool rxActive = false, rxPrev = true;
	int rxCount = 0, rxBit = 0, rxVotes = 0;
	uint16_t rxShift = 0;
	int rxDiv = 0;                // mode 2's oscillator-derived 16x clock
	int txLeft = 0;               // 16x ticks until TI (modes 1-3), or cycles (mode 0)
	bool t1Half = false;          // timer 1 overflows divided by two when SMOD = 0
	bool t0Prev = true, t1Prev = true;

	uint8_t& S(uint8_t a) { return sfr[a - 0x80]; }

	uint8_t code(uint16_t a) { return bus->code(a); }
	uint8_t fetch() { return code(pc++); }

	// --- flags ------------------------------------------------------------------
	enum : uint8_t { CY = 0x80, AC = 0x40, OV = 0x04 };
	bool cy() const { return sfr[PSW - 0x80] & CY; }
	void setFlag(uint8_t f, bool v) { if (v) S(PSW) |= f; else S(PSW) &= uint8_t(~f); }
	static uint8_t parity(uint8_t v) { v ^= v >> 4; v ^= v >> 2; v ^= v >> 1; return v & 1; }

	// --- memory spaces ----------------------------------------------------------
	uint8_t& R(int n) { return ram[(S(PSW) & 0x18) + n]; }

	uint8_t iread(uint8_t a) {
		if (a & 0x80) { badIndirect++; return 0xff; }
		return ram[a];
	}
	void iwrite(uint8_t a, uint8_t v) {
		if (a & 0x80) { badIndirect++; return; }
		ram[a] = v;
	}

	/** A direct-address read. Ports read their pins unless `latch` (the
	    read-modify-write instructions read the latch). */
	uint8_t rd(uint8_t a, bool latchRead = false) {
		if (a < 0x80) return ram[a];
		switch (a) {
		case P0: case P1: case P2: case P3: {
			const int p = (a - 0x80) >> 4;
			return latchRead ? S(a) : uint8_t(S(a) & bus->pins(p));
		}
		case SBUF: return rxBuf;
		case PSW: return uint8_t((S(PSW) & 0xfe) | parity(S(ACC)));
		default: return S(a);
		}
	}
	void wr(uint8_t a, uint8_t v) {
		if (a < 0x80) { ram[a] = v; return; }
		switch (a) {
		case P0: case P1: case P2: case P3:
			S(a) = v;
			bus->portOut((a - 0x80) >> 4, v);
			return;
		case SBUF: startTx(); return;
		case IE: case IP: S(a) = v; blockOnce = true; return;
		default: S(a) = v; return;
		}
	}
	/** Read-modify-write of a direct byte. */
	template <typename F> void rmw(uint8_t a, F f) { wr(a, f(rd(a, true))); }

	uint8_t bitByte(uint8_t b) const { return b < 0x80 ? uint8_t(0x20 + (b >> 3)) : uint8_t(b & 0xf8); }
	bool getBit(uint8_t b) { return rd(bitByte(b)) >> (b & 7) & 1; }
	void setBit(uint8_t b, bool v) {
		const uint8_t m = uint8_t(1 << (b & 7));
		rmw(bitByte(b), [&](uint8_t x) { return v ? uint8_t(x | m) : uint8_t(x & ~m); });
	}

	void push(uint8_t v) { S(SP)++; iwrite(S(SP), v); }
	uint8_t pop() { const uint8_t v = iread(S(SP)); S(SP)--; return v; }
	void pushPc() { push(uint8_t(pc)); push(uint8_t(pc >> 8)); }
	uint16_t dptr() { return uint16_t(S(DPH) << 8 | S(DPL)); }
	void jumpRel(uint8_t rel) { pc = uint16_t(pc + int8_t(rel)); }

	// --- arithmetic -------------------------------------------------------------
	void add(uint8_t b, bool c) {
		const uint8_t a = S(ACC);
		const unsigned r = unsigned(a) + b + c;
		setFlag(CY, r > 0xff);
		setFlag(AC, (a & 0xf) + (b & 0xf) + c > 0xf);
		setFlag(OV, ((a ^ r) & (b ^ r) & 0x80) != 0);
		S(ACC) = uint8_t(r);
	}
	void subb(uint8_t b) {
		const uint8_t a = S(ACC);
		const bool c = cy();
		const unsigned r = unsigned(a) - b - c;
		setFlag(CY, unsigned(a) < unsigned(b) + c);
		setFlag(AC, unsigned(a & 0xf) < unsigned(b & 0xf) + c);
		setFlag(OV, ((a ^ b) & (a ^ r) & 0x80) != 0);
		S(ACC) = uint8_t(r);
	}
	void cjne(uint8_t x, uint8_t y, uint8_t rel) {
		setFlag(CY, x < y);
		if (x != y) jumpRel(rel);
	}

	// --- the instruction set ------------------------------------------------------
	void execute(uint8_t op) {
		const int lo = op & 0x0f;
		// The register/indirect columns 6-F share one decode across most rows.
		auto operand = [&](bool latchRead = false) -> uint8_t {
			if (lo >= 8) return R(lo - 8);
			if (lo >= 6) return iread(R(lo - 6));
			return rd(fetch(), latchRead);   // lo == 5: direct
		};
		if ((op & 0x1f) == 0x01) {   // AJMP
			const uint8_t a = fetch();
			pc = uint16_t((pc & 0xf800) | ((op >> 5) << 8) | a);
			return;
		}
		if ((op & 0x1f) == 0x11) {   // ACALL
			const uint8_t a = fetch();
			pushPc();
			pc = uint16_t((pc & 0xf800) | ((op >> 5) << 8) | a);
			return;
		}
		switch (op) {
		case 0x00: return;                                           // NOP
		case 0x02: { const uint8_t h = fetch(), l = fetch(); pc = uint16_t(h << 8 | l); return; }  // LJMP
		case 0x03: S(ACC) = uint8_t(S(ACC) >> 1 | S(ACC) << 7); return;      // RR A
		case 0x04: S(ACC)++; return;                                  // INC A
		case 0x05: { const uint8_t a = fetch(); rmw(a, [](uint8_t x) { return uint8_t(x + 1); }); return; }
		case 0x10: {                                                  // JBC
			const uint8_t b = fetch(), rel = fetch();
			if (rd(bitByte(b), true) >> (b & 7) & 1) { setBit(b, false); jumpRel(rel); }
			return;
		}
		case 0x12: { const uint8_t h = fetch(), l = fetch(); pushPc(); pc = uint16_t(h << 8 | l); return; }  // LCALL
		case 0x13: { const bool c = S(ACC) & 1; S(ACC) = uint8_t(S(ACC) >> 1 | (cy() ? 0x80 : 0)); setFlag(CY, c); return; }  // RRC
		case 0x14: S(ACC)--; return;
		case 0x15: { const uint8_t a = fetch(); rmw(a, [](uint8_t x) { return uint8_t(x - 1); }); return; }
		case 0x20: { const uint8_t b = fetch(), rel = fetch(); if (getBit(b)) jumpRel(rel); return; }   // JB
		case 0x22: { const uint8_t h = pop(), l = pop(); pc = uint16_t(h << 8 | l); return; }           // RET
		case 0x23: S(ACC) = uint8_t(S(ACC) << 1 | S(ACC) >> 7); return;      // RL A
		case 0x24: add(fetch(), false); return;
		case 0x30: { const uint8_t b = fetch(), rel = fetch(); if (!getBit(b)) jumpRel(rel); return; }  // JNB
		case 0x32: {                                                  // RETI
			const uint8_t h = pop(), l = pop();
			pc = uint16_t(h << 8 | l);
			if (inService[1]) inService[1] = false; else inService[0] = false;
			blockOnce = true;
			return;
		}
		case 0x33: { const bool c = S(ACC) & 0x80; S(ACC) = uint8_t(S(ACC) << 1 | (cy() ? 1 : 0)); setFlag(CY, c); return; }  // RLC
		case 0x34: add(fetch(), cy()); return;
		case 0x40: { const uint8_t rel = fetch(); if (cy()) jumpRel(rel); return; }
		case 0x42: { const uint8_t a = fetch(), v = S(ACC); rmw(a, [&](uint8_t x) { return uint8_t(x | v); }); return; }
		case 0x43: { const uint8_t a = fetch(), v = fetch(); rmw(a, [&](uint8_t x) { return uint8_t(x | v); }); return; }
		case 0x44: S(ACC) |= fetch(); return;
		case 0x50: { const uint8_t rel = fetch(); if (!cy()) jumpRel(rel); return; }
		case 0x52: { const uint8_t a = fetch(), v = S(ACC); rmw(a, [&](uint8_t x) { return uint8_t(x & v); }); return; }
		case 0x53: { const uint8_t a = fetch(), v = fetch(); rmw(a, [&](uint8_t x) { return uint8_t(x & v); }); return; }
		case 0x54: S(ACC) &= fetch(); return;
		case 0x60: { const uint8_t rel = fetch(); if (!S(ACC)) jumpRel(rel); return; }
		case 0x62: { const uint8_t a = fetch(), v = S(ACC); rmw(a, [&](uint8_t x) { return uint8_t(x ^ v); }); return; }
		case 0x63: { const uint8_t a = fetch(), v = fetch(); rmw(a, [&](uint8_t x) { return uint8_t(x ^ v); }); return; }
		case 0x64: S(ACC) ^= fetch(); return;
		case 0x70: { const uint8_t rel = fetch(); if (S(ACC)) jumpRel(rel); return; }
		case 0x72: { const uint8_t b = fetch(); setFlag(CY, cy() || getBit(b)); return; }   // ORL C,bit
		case 0x73: pc = uint16_t(dptr() + S(ACC)); return;            // JMP @A+DPTR
		case 0x74: S(ACC) = fetch(); return;
		case 0x75: { const uint8_t a = fetch(), v = fetch(); wr(a, v); return; }
		case 0x76: case 0x77: iwrite(R(op - 0x76), fetch()); return;
		case 0x80: { const uint8_t rel = fetch(); jumpRel(rel); return; }               // SJMP
		case 0x82: { const uint8_t b = fetch(); setFlag(CY, cy() && getBit(b)); return; }   // ANL C,bit
		case 0x83: S(ACC) = code(uint16_t(pc + S(ACC))); return;      // MOVC A,@A+PC
		case 0x84: {                                                  // DIV AB
			const uint8_t a = S(ACC), b = S(B);
			setFlag(CY, false);
			if (!b) { setFlag(OV, true); return; }
			S(ACC) = uint8_t(a / b); S(B) = uint8_t(a % b);
			setFlag(OV, false);
			return;
		}
		case 0x85: { const uint8_t src = fetch(), dst = fetch(); wr(dst, rd(src)); return; }   // MOV dir,dir
		case 0x86: case 0x87: { const uint8_t a = fetch(); wr(a, iread(R(op - 0x86))); return; }
		case 0x90: { const uint8_t h = fetch(), l = fetch(); S(DPH) = h; S(DPL) = l; return; }
		case 0x92: { const uint8_t b = fetch(); setBit(b, cy()); return; }          // MOV bit,C
		case 0x93: S(ACC) = code(uint16_t(dptr() + S(ACC))); return;  // MOVC A,@A+DPTR
		case 0x94: subb(fetch()); return;
		case 0xa0: { const uint8_t b = fetch(); setFlag(CY, cy() || !getBit(b)); return; }  // ORL C,/bit
		case 0xa2: { const uint8_t b = fetch(); setFlag(CY, getBit(b)); return; }           // MOV C,bit
		case 0xa3: { const uint16_t d = uint16_t(dptr() + 1); S(DPH) = uint8_t(d >> 8); S(DPL) = uint8_t(d); return; }
		case 0xa4: {                                                  // MUL AB
			const unsigned r = unsigned(S(ACC)) * S(B);
			S(ACC) = uint8_t(r); S(B) = uint8_t(r >> 8);
			setFlag(CY, false); setFlag(OV, r > 0xff);
			return;
		}
		case 0xa5: return;                                            // reserved
		case 0xa6: case 0xa7: { const uint8_t a = fetch(); iwrite(R(op - 0xa6), rd(a)); return; }
		case 0xb0: { const uint8_t b = fetch(); setFlag(CY, cy() && !getBit(b)); return; }  // ANL C,/bit
		case 0xb2: { const uint8_t b = fetch(); setBit(b, !(rd(bitByte(b), true) >> (b & 7) & 1)); return; }  // CPL bit
		case 0xb3: setFlag(CY, !cy()); return;
		case 0xb4: { const uint8_t v = fetch(), rel = fetch(); cjne(S(ACC), v, rel); return; }
		case 0xb5: { const uint8_t a = fetch(), rel = fetch(); cjne(S(ACC), rd(a), rel); return; }
		case 0xb6: case 0xb7: { const uint8_t v = fetch(), rel = fetch(); cjne(iread(R(op - 0xb6)), v, rel); return; }
		case 0xc0: { const uint8_t a = fetch(); push(rd(a)); return; }               // PUSH
		case 0xc2: { const uint8_t b = fetch(); setBit(b, false); return; }
		case 0xc3: setFlag(CY, false); return;
		case 0xc4: S(ACC) = uint8_t(S(ACC) << 4 | S(ACC) >> 4); return;      // SWAP
		case 0xc5: { const uint8_t a = fetch(), v = rd(a, true); wr(a, S(ACC)); S(ACC) = v; return; }
		case 0xc6: case 0xc7: { const uint8_t r = R(op - 0xc6), v = iread(r); iwrite(r, S(ACC)); S(ACC) = v; return; }
		case 0xd0: { const uint8_t a = fetch(); wr(a, pop()); return; }               // POP
		case 0xd2: { const uint8_t b = fetch(); setBit(b, true); return; }
		case 0xd3: setFlag(CY, true); return;
		case 0xd4: {                                                  // DA A
			unsigned t = S(ACC);
			if ((t & 0x0f) > 9 || (S(PSW) & AC)) { t += 0x06; if (t > 0xff) setFlag(CY, true); }
			if (((t >> 4) & 0x0f) > 9 || cy()) { t += 0x60; if (t > 0xff) setFlag(CY, true); }
			S(ACC) = uint8_t(t);
			return;
		}
		case 0xd5: {                                                  // DJNZ dir,rel
			const uint8_t a = fetch(), rel = fetch();
			uint8_t v = 0;
			rmw(a, [&](uint8_t x) { v = uint8_t(x - 1); return v; });
			if (v) jumpRel(rel);
			return;
		}
		case 0xd6: case 0xd7: {                                       // XCHD A,@Ri
			const uint8_t r = R(op - 0xd6), v = iread(r);
			iwrite(r, uint8_t((v & 0xf0) | (S(ACC) & 0x0f)));
			S(ACC) = uint8_t((S(ACC) & 0xf0) | (v & 0x0f));
			return;
		}
		case 0xe0: S(ACC) = bus->xread(dptr()); return;
		case 0xe2: case 0xe3: S(ACC) = bus->xread(uint16_t(S(P2) << 8 | R(op - 0xe2))); return;
		case 0xe4: S(ACC) = 0; return;
		case 0xf0: bus->xwrite(dptr(), S(ACC)); return;
		case 0xf2: case 0xf3: bus->xwrite(uint16_t(S(P2) << 8 | R(op - 0xf2)), S(ACC)); return;
		case 0xf4: S(ACC) = uint8_t(~S(ACC)); return;
		case 0xf5: { const uint8_t a = fetch(); wr(a, S(ACC)); return; }
		case 0xf6: case 0xf7: iwrite(R(op - 0xf6), S(ACC)); return;
		default: break;
		}
		// The regular rows: column 5 direct, 6-7 @Ri, 8-F Rn.
		const int row = op >> 4;
		if (lo == 0x06 || lo == 0x07 || lo >= 0x08) {
			// INC / DEC on @Ri and Rn
			if (row == 0x0 || row == 0x1) {
				const int d = row == 0 ? 1 : -1;
				if (lo >= 8) R(lo - 8) = uint8_t(R(lo - 8) + d);
				else { const uint8_t r = R(lo - 6); iwrite(r, uint8_t(iread(r) + d)); }
				return;
			}
		}
		switch (row) {
		case 0x2: add(operand(), false); return;
		case 0x3: add(operand(), cy()); return;
		case 0x4: S(ACC) |= operand(); return;
		case 0x5: S(ACC) &= operand(); return;
		case 0x6: S(ACC) ^= operand(); return;
		case 0x7: R(lo - 8) = fetch(); return;                         // MOV Rn,#
		case 0x8: { const uint8_t a = fetch(); wr(a, R(lo - 8)); return; }   // MOV dir,Rn
		case 0x9: subb(operand()); return;
		case 0xa: { const uint8_t a = fetch(); R(lo - 8) = rd(a); return; }  // MOV Rn,dir
		case 0xb: { const uint8_t v = fetch(), rel = fetch(); cjne(R(lo - 8), v, rel); return; }
		case 0xc: { const uint8_t v = R(lo - 8); R(lo - 8) = S(ACC); S(ACC) = v; return; }   // XCH A,Rn
		case 0xd: { const uint8_t rel = fetch(); if (--R(lo - 8)) jumpRel(rel); return; }    // DJNZ Rn
		case 0xe: S(ACC) = operand(); return;                         // MOV A,dir/@Ri/Rn
		case 0xf: R(lo - 8) = S(ACC); return;                         // MOV Rn,A
		default: return;
		}
	}

	// --- interrupts ---------------------------------------------------------------
	enum { REQ_IE0 = 1, REQ_TF0 = 2, REQ_IE1 = 4, REQ_TF1 = 8, REQ_SER = 16 };

	uint8_t requests() {
		const uint8_t t = S(TCON), s = S(SCON);
		uint8_t r = 0;
		if (t & 0x02) r |= REQ_IE0;
		if (t & 0x20) r |= REQ_TF0;
		if (t & 0x08) r |= REQ_IE1;
		if (t & 0x80) r |= REQ_TF1;
		if (s & 0x03) r |= REQ_SER;
		return r;
	}

	/** Polled in the last cycle of an instruction: the flags as latched at the end
	    of the cycle before it. */
	int interrupt() {
		const uint8_t ie = S(IE);
		if (!(ie & 0x80)) return 0;
		const uint8_t want = polled & ie & 0x1f;
		if (!want || inService[1]) return 0;
		const uint8_t hi = want & S(IP);
		int src = -1;
		for (int i = 0; i < 5; i++) if (hi >> i & 1) { src = i; break; }
		int level = 1;
		if (src < 0) {
			if (inService[0]) return 0;
			for (int i = 0; i < 5; i++) if (want >> i & 1) { src = i; break; }
			level = 0;
		}
		inService[level] = true;
		// The hardware clears the timer flags, and edge-triggered external flags, as it vectors.
		if (src == 0 && (S(TCON) & 0x01)) S(TCON) &= uint8_t(~0x02);
		if (src == 1) S(TCON) &= uint8_t(~0x20);
		if (src == 2 && (S(TCON) & 0x04)) S(TCON) &= uint8_t(~0x08);
		if (src == 3) S(TCON) &= uint8_t(~0x80);
		pushPc();
		pc = uint16_t(0x03 + 8 * src);
		instrEnd = cycles + 2;
		tick(); tick();                // the hardware LCALL
		return 2;
	}

	// --- one machine cycle ------------------------------------------------------------
	void tick() {
		polled = requests();
		cycles++;
		const uint8_t tmod = S(TMOD), tcon = S(TCON), p3 = uint8_t(S(P3) & bus->pins(3));
		// External interrupt pins: INT0 = P3.2, INT1 = P3.3.
		extPin(0, p3 >> 2 & 1);
		extPin(1, p3 >> 3 & 1);
		// T0 / T1 counter inputs: falling edges on P3.4 / P3.5.
		const bool t0 = p3 >> 4 & 1, t1 = p3 >> 5 & 1;
		const bool t0Edge = t0Prev && !t0, t1Edge = t1Prev && !t1;
		t0Prev = t0; t1Prev = t1;

		const int m0 = tmod & 3, m1 = tmod >> 4 & 3;
		auto counts = [&](int t, bool edge) {
			const uint8_t c = uint8_t(tmod >> (4 * t));
			const bool run = (tcon >> (4 + 2 * t)) & 1;
			const bool gate = c & 0x08;
			const bool intPin = (p3 >> (2 + t)) & 1;
			if (!run || (gate && !intPin)) return false;
			return (c & 0x04) ? edge : true;
		};
		// Timer 0
		if (m0 == 3) {
			if (counts(0, t0Edge)) { if (++S(TL0) == 0) S(TCON) |= 0x20; }
			if ((tcon & 0x40) && ++S(TH0) == 0) S(TCON) |= 0x80;   // TH0 runs on TR1 and owns TF1
		}
		else if (counts(0, t0Edge)) count(TL0, TH0, m0, 0x20, false);
		// Timer 1 (stopped in its own mode 3; with timer 0 in mode 3 it runs free and sets no flag)
		bool t1Overflow = false;
		if (m1 != 3) {
			const bool t1Runs = m0 == 3 ? (!(tmod & 0x40) || t1Edge) : counts(1, t1Edge);
			if (t1Runs) t1Overflow = count(TL1, TH1, m1, m0 == 3 ? 0 : 0x80, true);
		}
		serial(t1Overflow);
	}

	bool extPrev[2] = { true, true };
	void extPin(int n, bool level) {
		const uint8_t it = uint8_t(n ? 0x04 : 0x01), flag = uint8_t(n ? 0x08 : 0x02);
		if (S(TCON) & it) { if (extPrev[n] && !level) S(TCON) |= flag; }
		else { if (!level) S(TCON) |= flag; else S(TCON) &= uint8_t(~flag); }
		extPrev[n] = level;
	}

	/** One count of a timer in mode 0, 1 or 2. Returns true on overflow. */
	bool count(uint8_t tl, uint8_t th, int mode, uint8_t tf, bool) {
		bool over = false;
		switch (mode) {
		case 0: {
			const uint8_t low = uint8_t((S(tl) + 1) & 0x1f);
			S(tl) = uint8_t((S(tl) & 0xe0) | low);
			if (!low && ++S(th) == 0) over = true;
			break;
		}
		case 1:
			if (++S(tl) == 0 && ++S(th) == 0) over = true;
			break;
		default:
			if (++S(tl) == 0) { S(tl) = S(th); over = true; }
			break;
		}
		if (over && tf) S(TCON) |= tf;
		return over;
	}

	// --- serial port ----------------------------------------------------------------------
	void startTx() {
		const int mode = S(SCON) >> 6;
		txLeft = mode == 0 ? 8 : (mode == 1 ? 9 : 10) * 16;   // TI rises at the start of the stop bit
		if (mode == 0) unsupported++;
	}

	void serial(bool t1Overflow) {
		const int mode = S(SCON) >> 6;
		int ticks16 = 0;                      // 16x baud-clock ticks this cycle
		if (mode == 1 || mode == 3) {
			if (t1Overflow) {
				if (S(PCON) & 0x80) ticks16 = 1;
				else { t1Half = !t1Half; ticks16 = t1Half ? 0 : 1; }
			}
		}
		else if (mode == 2) {
			// fosc/64 (or /32 with SMOD) per bit: 16x is 3 (or 6) ticks per machine cycle.
			ticks16 = (S(PCON) & 0x80) ? 6 : 3;
		}
		else {
			if (txLeft > 0 && --txLeft == 0) S(SCON) |= 0x02;
			if (S(SCON) & 0x10) unsupported++;     // mode 0 reception
			return;
		}
		for (int k = 0; k < ticks16; k++) {
			if (txLeft > 0 && --txLeft == 0) S(SCON) |= 0x02;
			rxTick(mode);
		}
	}

	void rxTick(int mode) {
		const bool line = bus->rxd(cycles);
		if (!(S(SCON) & 0x10)) { rxActive = false; rxPrev = line; return; }   // REN off
		if (!rxActive) {
			if (rxPrev && !line) { rxActive = true; rxCount = 0; rxBit = 0; rxVotes = 0; rxShift = 0; }
			rxPrev = line;
			return;
		}
		rxPrev = line;
		rxCount++;
		if (rxCount >= 7 && rxCount <= 9) rxVotes += line;
		if (rxCount == 9) {
			const bool bit = rxVotes >= 2;
			const int bits = mode == 1 ? 8 : 9;
			if (rxBit == 0) {
				if (bit) { rxActive = false; return; }   // a false start
			}
			else if (rxBit <= bits) rxShift = uint16_t(rxShift | (bit << (rxBit - 1)));
			else {
				// The stop bit: the frame is kept only if RI is clear and SM2 allows it.
				const uint8_t sc = S(SCON);
				const bool rb8 = mode == 1 ? bit : (rxShift >> 8 & 1);
				if (!(sc & 0x01) && (!(sc & 0x20) || rb8)) {
					rxBuf = uint8_t(rxShift);
					S(SCON) = uint8_t((sc & ~0x04) | (rb8 ? 0x04 : 0) | 0x01);
				}
				rxActive = false;
				return;
			}
			rxBit++;
			rxVotes = 0;
		}
		if (rxCount == 16) rxCount = 0;
	}

public:
	// Machine cycles per instruction, from the manual's instruction table. A function-local
	// table: indexed at run time, a static constexpr member would need an out-of-line
	// definition before C++17, and GCC links without one only by luck of inlining.
	static int opCycles(uint8_t op) {
	static constexpr uint8_t CYCLES[256] = {
	//  0 1 2 3 4 5 6 7 8 9 A B C D E F
		1,2,2,1,1,1,1,1,1,1,1,1,1,1,1,1, // 0
		2,2,2,1,1,1,1,1,1,1,1,1,1,1,1,1, // 1
		2,2,2,1,1,1,1,1,1,1,1,1,1,1,1,1, // 2
		2,2,2,1,1,1,1,1,1,1,1,1,1,1,1,1, // 3
		2,2,1,2,1,1,1,1,1,1,1,1,1,1,1,1, // 4
		2,2,1,2,1,1,1,1,1,1,1,1,1,1,1,1, // 5
		2,2,1,2,1,1,1,1,1,1,1,1,1,1,1,1, // 6
		2,2,2,2,1,2,1,1,1,1,1,1,1,1,1,1, // 7
		2,2,2,2,4,2,2,2,2,2,2,2,2,2,2,2, // 8
		2,2,2,2,1,1,1,1,1,1,1,1,1,1,1,1, // 9
		2,2,1,2,4,1,2,2,2,2,2,2,2,2,2,2, // A
		2,2,1,1,2,2,2,2,2,2,2,2,2,2,2,2, // B
		2,2,1,1,1,1,1,1,1,1,1,1,1,1,1,1, // C
		2,2,1,1,1,2,1,1,2,2,2,2,2,2,2,2, // D
		2,2,2,2,1,1,1,1,1,1,1,1,1,1,1,1, // E
		2,2,2,2,1,1,1,1,1,1,1,1,1,1,1,1, // F
	};
	// Instruction lengths in bytes (for the disassembler and the tests).
	static constexpr uint8_t LENGTH[256] = {
		1,2,3,1,1,2,1,1,1,1,1,1,1,1,1,1,
		3,2,3,1,1,2,1,1,1,1,1,1,1,1,1,1,
		3,2,1,1,2,2,1,1,1,1,1,1,1,1,1,1,
		3,2,1,1,2,2,1,1,1,1,1,1,1,1,1,1,
		2,2,2,3,2,2,1,1,1,1,1,1,1,1,1,1,
		2,2,2,3,2,2,1,1,1,1,1,1,1,1,1,1,
		2,2,2,3,2,2,1,1,1,1,1,1,1,1,1,1,
		2,2,2,1,2,3,2,2,2,2,2,2,2,2,2,2,
		2,2,2,1,1,3,2,2,2,2,2,2,2,2,2,2,
		3,2,2,1,2,2,1,1,1,1,1,1,1,1,1,1,
		2,2,2,1,1,1,2,2,2,2,2,2,2,2,2,2,
		2,2,2,1,3,3,3,3,3,3,3,3,3,3,3,3,
		2,2,2,1,1,2,1,1,1,1,1,1,1,1,1,1,
		2,2,2,1,1,3,1,1,2,2,2,2,2,2,2,2,
		1,2,1,1,1,2,1,1,1,1,1,1,1,1,1,1,
		1,2,1,1,1,2,1,1,1,1,1,1,1,1,1,1,
	};
	return CYCLES[op];
	}
};

} // namespace mv

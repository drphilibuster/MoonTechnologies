// The PIC16F684 core: every instruction's result, flags and cycle count, checked
// against arithmetic done here and not against the emulator's own helpers, plus the
// chip's banking, indirect addressing, computed jumps, A/D converter and PWM.

#include "../../src/Pic16f684.hpp"

#include <cstdio>
#include <vector>

using pic16::Pic16f684;

static int checks = 0, failures = 0;
static void check(const char* what, bool ok, const char* detail = 0) {
	checks++;
	if (!ok) {
		failures++;
		printf("  FAIL  %s%s%s\n", what, detail ? ": " : "", detail ? detail : "");
	}
}

// ---- an assembler of one-line helpers --------------------------------------------
typedef uint16_t W;
static W MOVLW(int k) { return 0x3000 | (k & 0xFF); }
static W ADDLW(int k) { return 0x3E00 | (k & 0xFF); }
static W SUBLW(int k) { return 0x3C00 | (k & 0xFF); }
static W ANDLW(int k) { return 0x3900 | (k & 0xFF); }
static W IORLW(int k) { return 0x3800 | (k & 0xFF); }
static W XORLW(int k) { return 0x3A00 | (k & 0xFF); }
static W RETLW(int k) { return 0x3400 | (k & 0xFF); }
static W MOVWF(int f) { return 0x0080 | f; }
static W CLRF(int f) { return 0x0180 | f; }
static const W CLRW = 0x0100, NOP = 0x0000, RETURN = 0x0008;
static W ADDWF(int f, int d) { return 0x0700 | (d << 7) | f; }
static W SUBWF(int f, int d) { return 0x0200 | (d << 7) | f; }
static W ANDWF(int f, int d) { return 0x0500 | (d << 7) | f; }
static W IORWF(int f, int d) { return 0x0400 | (d << 7) | f; }
static W XORWF(int f, int d) { return 0x0600 | (d << 7) | f; }
static W MOVF(int f, int d) { return 0x0800 | (d << 7) | f; }
static W COMF(int f, int d) { return 0x0900 | (d << 7) | f; }
static W DECF(int f, int d) { return 0x0300 | (d << 7) | f; }
static W INCF(int f, int d) { return 0x0A00 | (d << 7) | f; }
static W DECFSZ(int f, int d) { return 0x0B00 | (d << 7) | f; }
static W INCFSZ(int f, int d) { return 0x0F00 | (d << 7) | f; }
static W RRF(int f, int d) { return 0x0C00 | (d << 7) | f; }
static W RLF(int f, int d) { return 0x0D00 | (d << 7) | f; }
static W SWAPF(int f, int d) { return 0x0E00 | (d << 7) | f; }
static W BCF(int f, int b) { return 0x1000 | (b << 7) | f; }
static W BSF(int f, int b) { return 0x1400 | (b << 7) | f; }
static W BTFSC(int f, int b) { return 0x1800 | (b << 7) | f; }
static W BTFSS(int f, int b) { return 0x1C00 | (b << 7) | f; }
static W CALL(int a) { return 0x2000 | (a & 0x7FF); }
static W GOTO(int a) { return 0x2800 | (a & 0x7FF); }

enum { Fa = 0x20, Fb = 0x21 };
static const int C = 1, DC = 2, Z = 4;

static void program(Pic16f684& c, std::vector<W> p) {
	c.load(p.data(), (int) p.size());
	c.reset();
}
static int status(Pic16f684& c) { return c.ram[Pic16f684::STATUS] & 7; }

/** Runs one instruction word from a clean state with f=Fa holding `a` and W holding `b`. */
static int one(Pic16f684& c, W op, int a, int b, int carryIn = 0) {
	W p[1] = { op };
	c.load(p, 1);
	c.reset();
	c.ram[Fa] = (uint8_t) a;
	c.w = (uint8_t) b;
	c.ram[Pic16f684::STATUS] = (uint8_t) (0x18 | (carryIn ? 1 : 0));
	return c.step();
}

static void testArithmeticFlags() {
	printf("ALU: every operand pair, result and C, DC, Z\n");
	Pic16f684 c;
	bool addOk = true, subOk = true, addlwOk = true, sublwOk = true;
	char d[96] = "";
	for (int a = 0; a < 256 && addOk; a++) {
		for (int b = 0; b < 256; b++) {
			// ADDWF f,F : f + W
			one(c, ADDWF(Fa, 1), a, b);
			int sum = a + b;
			int wantF = sum & 0xFF;
			int wantS = (sum > 255 ? C : 0) | (((a & 15) + (b & 15)) > 15 ? DC : 0) | (wantF == 0 ? Z : 0);
			if (c.ram[Fa] != wantF || status(c) != wantS) {
				addOk = false;
				snprintf(d, sizeof d, "ADDWF a=%d b=%d got %d/%d want %d/%d", a, b, c.ram[Fa], status(c), wantF, wantS);
				break;
			}
			// SUBWF f,F : f - W, C set when there is no borrow
			one(c, SUBWF(Fa, 1), a, b);
			int diff = (a - b) & 0xFF;
			wantS = (a >= b ? C : 0) | ((a & 15) >= (b & 15) ? DC : 0) | (diff == 0 ? Z : 0);
			if (c.ram[Fa] != diff || status(c) != wantS) {
				subOk = false;
				snprintf(d, sizeof d, "SUBWF a=%d b=%d got %d/%d want %d/%d", a, b, c.ram[Fa], status(c), diff, wantS);
			}
			// ADDLW k : k + W
			one(c, ADDLW(a), 0, b);
			if (c.w != ((a + b) & 0xFF) || ((status(c) & C) != 0) != (a + b > 255)) addlwOk = false;
			// SUBLW k : k - W (this is the instruction the quantizer's tables are made of)
			one(c, SUBLW(a), 0, b);
			wantS = (a >= b ? C : 0) | ((a & 15) >= (b & 15) ? DC : 0) | (((a - b) & 0xFF) == 0 ? Z : 0);
			if (c.w != ((a - b) & 0xFF) || status(c) != wantS) sublwOk = false;
		}
	}
	check("ADDWF over all 65536 pairs", addOk, d);
	check("SUBWF over all 65536 pairs", subOk, d);
	check("ADDLW result and carry", addlwOk);
	check("SUBLW over all 65536 pairs (C = literal >= W)", sublwOk);
}

static void testLogicAndMoves() {
	printf("ALU: logic, moves, rotates\n");
	Pic16f684 c;
	bool ok = true;
	for (int a = 0; a < 256; a += 3) {
		for (int b = 0; b < 256; b += 5) {
			one(c, ANDWF(Fa, 1), a, b); if (c.ram[Fa] != (a & b) || ((status(c) & Z) != 0) != ((a & b) == 0)) ok = false;
			one(c, IORWF(Fa, 1), a, b); if (c.ram[Fa] != (a | b) || ((status(c) & Z) != 0) != ((a | b) == 0)) ok = false;
			one(c, XORWF(Fa, 1), a, b); if (c.ram[Fa] != (a ^ b) || ((status(c) & Z) != 0) != ((a ^ b) == 0)) ok = false;
			one(c, ANDWF(Fa, 0), a, b); if (c.w != (a & b) || c.ram[Fa] != a) ok = false;   // d=0: to W
			one(c, ANDLW(a), 0, b); if (c.w != (a & b)) ok = false;
			one(c, IORLW(a), 0, b); if (c.w != (a | b)) ok = false;
			one(c, XORLW(a), 0, b); if (c.w != (a ^ b)) ok = false;
		}
		one(c, MOVF(Fa, 0), a, 7); if (c.w != a || ((status(c) & Z) != 0) != (a == 0)) ok = false;
		one(c, MOVF(Fa, 1), a, 7); if (c.ram[Fa] != a || ((status(c) & Z) != 0) != (a == 0)) ok = false;
		one(c, COMF(Fa, 1), a, 0); if (c.ram[Fa] != ((~a) & 0xFF)) ok = false;
		one(c, INCF(Fa, 1), a, 0); if (c.ram[Fa] != ((a + 1) & 0xFF) || ((status(c) & Z) != 0) != (((a + 1) & 0xFF) == 0)) ok = false;
		one(c, DECF(Fa, 1), a, 0); if (c.ram[Fa] != ((a - 1) & 0xFF) || ((status(c) & Z) != 0) != (((a - 1) & 0xFF) == 0)) ok = false;
		one(c, SWAPF(Fa, 1), a, 0); if (c.ram[Fa] != (((a << 4) | (a >> 4)) & 0xFF)) ok = false;
		for (int cin = 0; cin < 2; cin++) {
			one(c, RLF(Fa, 1), a, 0, cin);
			if (c.ram[Fa] != (((a << 1) | cin) & 0xFF) || (status(c) & C) != ((a >> 7) & 1)) ok = false;
			one(c, RRF(Fa, 1), a, 0, cin);
			if (c.ram[Fa] != ((a >> 1) | (cin << 7)) || (status(c) & C) != (a & 1)) ok = false;
		}
	}
	check("AND, IOR, XOR, MOVF, COMF, INCF, DECF, SWAPF, RLF and RRF", ok);
	one(c, CLRF(Fa), 0x55, 0);
	check("CLRF clears and sets Z", c.ram[Fa] == 0 && (status(c) & Z));
	one(c, CLRW, 0, 0x55);
	check("CLRW clears W and sets Z", c.w == 0 && (status(c) & Z));
	one(c, MOVWF(Fa), 0, 0x9C);
	check("MOVWF stores without touching flags", c.ram[Fa] == 0x9C && status(c) == 0);
	check("MOVLW loads W", (one(c, MOVLW(0xA5), 0, 0), c.w == 0xA5));
}

static void testBitsAndSkips() {
	printf("Bit instructions, skips and cycle counts\n");
	Pic16f684 c;
	for (int b = 0; b < 8; b++) {
		one(c, BSF(Fa, b), 0x00, 0);
		check("BSF sets exactly its bit", c.ram[Fa] == (1 << b));
		one(c, BCF(Fa, b), 0xFF, 0);
		check("BCF clears exactly its bit", c.ram[Fa] == (0xFF & ~(1 << b)));
	}
	int cyc = one(c, BTFSC(Fa, 3), 0x08, 0);          // bit set: no skip
	check("BTFSC on a set bit does not skip, 1 cycle", c.pc == 1 && cyc == 1);
	cyc = one(c, BTFSC(Fa, 3), 0x00, 0);
	check("BTFSC on a clear bit skips, 2 cycles", c.pc == 2 && cyc == 2);
	cyc = one(c, BTFSS(Fa, 3), 0x08, 0);
	check("BTFSS on a set bit skips, 2 cycles", c.pc == 2 && cyc == 2);
	cyc = one(c, BTFSS(Fa, 3), 0x00, 0);
	check("BTFSS on a clear bit does not skip, 1 cycle", c.pc == 1 && cyc == 1);
	cyc = one(c, DECFSZ(Fa, 1), 1, 0);
	check("DECFSZ reaching 0 skips, 2 cycles", c.ram[Fa] == 0 && c.pc == 2 && cyc == 2);
	cyc = one(c, DECFSZ(Fa, 1), 5, 0);
	check("DECFSZ not reaching 0 does not skip", c.ram[Fa] == 4 && c.pc == 1 && cyc == 1);
	cyc = one(c, INCFSZ(Fa, 1), 0xFF, 0);
	check("INCFSZ wrapping to 0 skips", c.ram[Fa] == 0 && c.pc == 2 && cyc == 2);
	cyc = one(c, INCFSZ(Fa, 0), 0x10, 0);
	check("INCFSZ,W leaves f alone", c.w == 0x11 && c.ram[Fa] == 0x10 && c.pc == 1);
	cyc = one(c, GOTO(0x123), 0, 0);
	check("GOTO is 2 cycles", c.pc == 0x123 && cyc == 2);
	cyc = one(c, NOP, 0, 0);
	check("NOP is 1 cycle", cyc == 1 && c.pc == 1);
}

static void testCallsAndTables() {
	printf("CALL, RETURN, RETLW and computed jumps\n");
	Pic16f684 c;
	// 0: CALL 4; 1: MOVWF Fa; 2: GOTO 2 (spin); 4: MOVLW 0x42; 5: RETURN
	program(c, { CALL(4), MOVWF(Fa), GOTO(2), NOP, MOVLW(0x42), RETURN });
	int n = 0, cycles = 0;
	while (c.pc != 2 && n < 20) { cycles += c.step(); n++; }
	check("CALL then RETURN comes back to the next instruction", c.ram[Fa] == 0x42);
	check("CALL and RETURN are 2 cycles each, the rest 1", cycles == 2 + 1 + 2 + 1, "cycle total");

	// A lookup table: W holds the index, ADDWF PCL,F jumps into a run of RETLWs.
	// 0: CALL 8; 1: MOVWF Fa; 2: GOTO 2;  8: ADDWF PCL,F; 9..: RETLW 10, 20, 30, 40
	program(c, { CALL(8), MOVWF(Fa), GOTO(2), NOP, NOP, NOP, NOP, NOP,
	             ADDWF(Pic16f684::PCL, 1), RETLW(10), RETLW(20), RETLW(30), RETLW(40) });
	bool ok = true;
	for (int idx = 0; idx < 4; idx++) {
		c.reset();
		c.w = (uint8_t) idx;
		int g = 0;
		while (c.pc != 2 && g++ < 30) c.step();
		if (c.ram[Fa] != 10 * (idx + 1)) ok = false;
	}
	check("a computed jump through PCL reaches the table entry", ok);
	// ADDWF PCL,F costs the extra cycle a jump does.
	program(c, { ADDWF(Pic16f684::PCL, 1), NOP, NOP });
	c.w = 1;
	int cy = c.step();
	check("a write to PCL is 2 cycles", cy == 2 && c.pc == 2, "pc");

	// The stack is eight deep and wraps.
	program(c, { CALL(1), CALL(2), RETURN, RETURN });
	check("nested calls unwind in order", (c.step(), c.step(), c.step() == 2 && c.pc == 2));
}

static void testBankingAndIndirect() {
	printf("Banks, common RAM and indirect addressing\n");
	Pic16f684 c;
	// BSF STATUS,5 ; MOVLW 0x03 ; MOVWF TRISC(0x87 -> 0x07 in bank 1) ; BCF STATUS,5 ; MOVLW 0x55 ; MOVWF PORTC (0x07)
	program(c, { BSF(Pic16f684::STATUS, 5), MOVLW(0x03), MOVWF(0x07), BCF(Pic16f684::STATUS, 5),
	             MOVLW(0x55), MOVWF(0x07) });
	for (int i = 0; i < 6; i++) c.step();
	check("bank 1 address 07h is TRISC", c.ram[Pic16f684::TRISC] == 0x03);
	check("bank 0 address 07h is PORTC, a different register", c.ram[Pic16f684::PORTC] == 0x55);
	// Common RAM: 70h in either bank is the same byte.
	program(c, { MOVLW(0x6B), MOVWF(0x70), BSF(Pic16f684::STATUS, 5), MOVF(0x70, 0) });
	for (int i = 0; i < 4; i++) c.step();
	check("RAM at 70h is common to both banks", c.w == 0x6B);
	// General RAM is per bank: 20h in bank 1 is not 20h in bank 0.
	program(c, { MOVLW(0x11), MOVWF(0x20), BSF(Pic16f684::STATUS, 5), MOVLW(0x22), MOVWF(0x20),
	             BCF(Pic16f684::STATUS, 5), MOVF(0x20, 0) });
	for (int i = 0; i < 7; i++) c.step();
	check("general RAM is banked", c.w == 0x11 && c.ram[0x80 | 0x20] == 0x22);
	// Indirect: FSR points at a byte, INDF reads and writes it.
	program(c, { MOVLW(0x30), MOVWF(Pic16f684::FSR), MOVLW(0x5A), MOVWF(0x00), MOVF(0x00, 0), INCF(0x00, 1) });
	for (int i = 0; i < 6; i++) c.step();
	check("INDF reads and writes the byte FSR points at", c.ram[0x30] == 0x5B && c.w == 0x5A);
	// INDF through FSR=0 reads zero.
	program(c, { CLRF(Pic16f684::FSR), MOVF(0x00, 0) });
	c.w = 9;
	c.step(); c.step();
	check("INDF with FSR=0 reads zero", c.w == 0);
}

static void testStatusAndTimingRules() {
	printf("STATUS rules\n");
	Pic16f684 c;
	program(c, { MOVLW(0xFF), MOVWF(Pic16f684::STATUS) });
	c.step(); c.step();
	check("TO and PD cannot be written", (c.ram[Pic16f684::STATUS] & 0x18) == 0x18);
	program(c, { MOVLW(0x00), MOVWF(Pic16f684::STATUS) });
	c.step(); c.step();
	check("TO and PD stay set when 0 is written", (c.ram[Pic16f684::STATUS] & 0x18) == 0x18);
}

static void testAdc() {
	printf("A/D converter\n");
	Pic16f684 c;
	// The firmware's own set-up: ADCON1 = FOSC/64, ANSEL AN4+AN5 analog, ADCON0 = AN4, left-justified.
	auto setup = [&](double v, int adcon0) {
		c.reset();
		c.ram[Pic16f684::ADCON1] = 0x60;
		c.ram[Pic16f684::ANSEL] = 0x30;
		c.analog[4] = v;
		c.ram[Pic16f684::ADCON0] = (uint8_t) adcon0;
	};
	// Left-justified, volts across the range, against ideal 10-bit arithmetic.
	bool ok = true;
	for (int i = 0; i <= 100; i++) {
		double v = 5.0 * i / 100.0;
		setup(v, 0x11);
		c.writeReg(Pic16f684::ADCON0 & 0x7F, 0x13);          // GO
		for (int k = 0; k < 400; k++) c.advance(1);
		int ideal = (int) std::floor(v / 5.0 * 1024.0);
		if (ideal > 1023) ideal = 1023;
		int got = (c.ram[Pic16f684::ADRESH] << 2) | (c.ram[Pic16f684::ADRESL] >> 6);
		if (got != ideal) { ok = false; printf("    %.2f V: got %d want %d\n", v, got, ideal); }
	}
	check("ADRESH:ADRESL is the ideal 10-bit code, left-justified", ok);
	setup(2.5, 0x91);
	c.writeReg(Pic16f684::ADCON0 & 0x7F, 0x93);
	for (int k = 0; k < 400; k++) c.advance(1);
	check("right-justified puts the top two bits in ADRESH", c.ram[Pic16f684::ADRESH] == 2 && c.ram[Pic16f684::ADRESL] == 0);
	setup(9.0, 0x11);
	c.writeReg(Pic16f684::ADCON0 & 0x7F, 0x13);
	for (int k = 0; k < 400; k++) c.advance(1);
	check("above VDD saturates at 255 in ADRESH", c.ram[Pic16f684::ADRESH] == 255);
	setup(-1.0, 0x11);
	c.writeReg(Pic16f684::ADCON0 & 0x7F, 0x13);
	for (int k = 0; k < 400; k++) c.advance(1);
	check("below ground reads 0", c.ram[Pic16f684::ADRESH] == 0);

	// Timing: GO/DONE reads high for eleven TAD, TAD = 64 oscillator periods = 16 cycles.
	setup(1.0, 0x11);
	c.writeReg(Pic16f684::ADCON0 & 0x7F, 0x13);
	int busy = 0;
	while ((c.readReg(Pic16f684::ADCON0 & 0x7F) & 2) && busy < 1000) { c.advance(1); busy++; }
	char d[48];
	snprintf(d, sizeof d, "busy for %d cycles", busy);
	check("a conversion at FOSC/64 takes 11 x 16 = 176 cycles (+1 to start)", busy >= 176 && busy <= 178, d);
	// Channel select reaches the right input.
	setup(0.0, 0x15);                         // AN5
	c.analog[5] = 3.0;
	c.writeReg(Pic16f684::ADCON0 & 0x7F, 0x17);
	for (int k = 0; k < 400; k++) c.advance(1);
	check("CHS selects AN5", c.ram[Pic16f684::ADRESH] == 153);
	// The input is taken at the start, not the end.
	setup(1.0, 0x11);
	c.writeReg(Pic16f684::ADCON0 & 0x7F, 0x13);
	for (int k = 0; k < 20; k++) c.advance(1);
	c.analog[4] = 4.0;
	for (int k = 0; k < 400; k++) c.advance(1);
	check("a change after the conversion has started is not seen", c.ram[Pic16f684::ADRESH] == 51);
}

/** The firmware's own PWM set-up: PR2 = 255, T2CON = 04h, CCP1CON = 0Ch, then CCPR1L and DC1B. */
static void pwmSetup(Pic16f684& c, int ccpr1l, int dc1b) {
	c.reset();
	c.ram[Pic16f684::PR2] = 255;
	c.writeReg(Pic16f684::T2CON, 0x04);
	c.ram[Pic16f684::CCPR1L] = (uint8_t) ccpr1l;
	c.writeReg(Pic16f684::CCP1CON, (uint8_t) (0x0C | (dc1b << 4)));
}

static void testPwm() {
	printf("CCP1 PWM\n");
	Pic16f684 c;
	// The average over whole periods is duty/1024 of full scale, exactly, for every duty.
	bool ok = true;
	for (int duty = 0; duty <= 1023; duty += 7) {
		pwmSetup(c, duty >> 2, duty & 3);
		c.advance(256 * 3);                       // let the duty reach the pin
		c.clearPwmWindow();
		c.advance(256 * 20);
		double avg = c.pwmAverage();
		if (std::fabs(avg - duty / 1024.0) > 1e-9) { ok = false; printf("    duty %d: %.6f\n", duty, avg); }
	}
	check("the pin is high duty/1024 of the time for every duty tried", ok);
	// The period is 256 cycles: one rising edge every 256.
	pwmSetup(c, 128, 0);
	c.advance(256 * 3);
	int rises = 0; bool last = c.pwmPin;
	for (int i = 0; i < 256 * 10; i++) { c.advance(1); if (c.pwmPin && !last) rises++; last = c.pwmPin; }
	check("PWM period is 256 cycles (51.2 us at 20 MHz)", rises == 10);
	// Double buffering: a new CCPR1L does not reach the pin until the period ends.
	pwmSetup(c, 64, 0);
	c.advance(256 * 3 + 100);                    // part-way through a period
	c.clearPwmWindow();
	c.ram[Pic16f684::CCPR1L] = 192;              // new duty, written mid-period
	c.advance(256 - 100);                        // the rest of this period
	double rest = c.pwmHighTicks;
	c.clearPwmWindow();
	c.advance(256);                              // the next whole period
	check("a duty written mid-period waits for the next one",
	      rest == 0.0 && std::fabs(c.pwmHighTicks - 192 * 4) < 1e-9, "latched late or early");
	// Nothing on the pin until the timer is on.
	c.reset();
	c.ram[Pic16f684::CCPR1L] = 200;
	c.writeReg(Pic16f684::CCP1CON, 0x0C);
	c.clearPwmWindow();
	c.advance(256 * 4);
	check("TMR2 off: no PWM", c.pwmHighTicks == 0);
}

static void testUnsupported() {
	printf("Unsupported features are flagged, not silently skipped\n");
	Pic16f684 c;
	pwmSetup(c, 10, 0);
	check("the firmware's own set-up is fully supported", !c.unsupported);
	c.writeReg(Pic16f684::T2CON, 0x05);
	check("a TMR2 prescaler is flagged", c.unsupported);
	c.reset();
	c.writeReg(Pic16f684::INTCON, 0x80);
	check("interrupts enabled are flagged", c.unsupported);
	c.reset();
	c.writeReg(Pic16f684::CCP1CON, 0x05);
	check("a CCP mode other than PWM is flagged", c.unsupported);
	c.reset();
	program(c, { 0x0063 });
	c.step();
	check("SLEEP is flagged", c.unsupported);
	c.reset();
	c.readReg(0x1A);
	check("an SFR outside the model is flagged", c.unsupported && c.unsupportedAt == 0x1A);
}

static void testHex() {
	printf("Intel HEX loader\n");
	// 4 words at 0: GOTO 1, MOVLW 4, NOP, NOP -- checked against the checksums.
	const char* text =
		":0800000001280430000000009B\r\n"
		":00000001FF\r\n";
	Pic16f684 c;
	check("a good file loads", c.loadHex(text));
	check("words are little-endian at byte addresses", c.prog[0] == 0x2801 && c.prog[1] == 0x3004 && c.prog[2] == 0 && c.prog[4] == 0x3FFF);
	check("a bad checksum is refused", !c.loadHex(":0800000001280430000000009A\n:00000001FF\n"));
	c.loadHex(text);
	c.ram[Pic16f684::STATUS] = 0;
	c.pc = 5;
	c.reset();
	check("a power-on reset keeps the flash and restarts at 0", c.prog[0] == 0x2801 && c.pc == 0);
}

int main() {
	testArithmeticFlags();
	testLogicAndMoves();
	testBitsAndSkips();
	testCallsAndTables();
	testBankingAndIndirect();
	testStatusAndTimingRules();
	testAdc();
	testPwm();
	testUnsupported();
	testHex();
	printf("\n%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}

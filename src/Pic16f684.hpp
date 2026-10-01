#pragma once
// A PIC16F684, running its own firmware: the mid-range 14-bit core with all 35
// instructions, and the parts of the chip the Varimode quantizer's firmware (the
// Modular in a Week Day 10 PIC project) uses -- the A/D converter, TMR2 and the
// CCP1 PWM, the I/O ports and their direction registers.
//
// No Rack dependency, so tests/ drives it bare.
//
// Timing is the part's own. One instruction cycle is four oscillator periods; an
// instruction takes one, or two if it branches, skips, or writes PCL. The A/D
// converter takes eleven TAD, TAD set by ADCS, and the PWM is generated tick by
// tick from TMR2, PR2, CCPR1L and the two DC1B bits, double-buffered as the
// datasheet has it, so what the pin does between a firmware write to CCPR1L and
// the next period is what the chip does.
//
// What is not here is not silently wrong: anything the firmware does that this
// model does not implement -- an interrupt enabled, SLEEP, TMR2 prescaled, a CCP
// mode other than PWM, an external A/D reference, a special-function register
// outside the list -- sets `unsupported` and records where, so a test (or a
// caller) can see that the emulation has left what it knows.

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace pic16 {

struct Pic16f684 {
    // ---- the chip --------------------------------------------------------------
    static const int kProgWords = 2048;
    static constexpr double kFosc = 20e6;      // the course's external 20 MHz clock
    static constexpr double kVdd = 5.0;

    uint16_t prog[kProgWords];
    uint8_t ram[512];                          // bank*128 + offset; SFRs are in here too
    uint16_t pc = 0;
    uint8_t w = 0;
    uint16_t stack[8];
    int sp = 0;

    bool sleeping = false;
    double budget = 0.0;                       // cycles owed, for run()

    // ---- what it is wired to ---------------------------------------------------
    double analog[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };   // volts on AN0..AN7

    // ---- what is not modelled, if the firmware reaches it ------------------------
    bool unsupported = false;
    int unsupportedAt = -1;                    // the register address, or -1 for an instruction
    const char* unsupportedWhy = "";

    // ---- the PWM, for whoever listens to the pin --------------------------------
    double pwmHighTicks = 0.0;                 // oscillator periods the pin has been high
    double pwmTotalTicks = 0.0;                // and in total, since clearPwmWindow()
    bool pwmPin = false;

    // ---- internals -------------------------------------------------------------
    int adcCycles = 0;                         // cycles to the end of the conversion
    int adcStartWait = 0;                      // cycles until the input is taken
    int adcResult = 0;
    int adcSampleCh = 0;
    uint8_t ccpr1h = 0;                        // the PWM duty's slave register
    uint8_t dc1bLatch = 0;                     // and the two bits that go with it
    bool pwmArmed = false;
    long cyclesRun = 0;
    bool pclWritten = false;

    Pic16f684() { eraseFlash(); reset(); }

    /** Erased flash reads as 3FFF. */
    void eraseFlash() { for (int i = 0; i < kProgWords; i++) prog[i] = 0x3FFF; }

    // Register addresses (bank included).
    enum {
        INDF = 0x00, TMR0 = 0x01, PCL = 0x02, STATUS = 0x03, FSR = 0x04, PORTA = 0x05,
        PORTC = 0x07, PCLATH = 0x0A, INTCON = 0x0B, PIR1 = 0x0C, TMR2 = 0x11, T2CON = 0x12,
        CCPR1L = 0x13, CCPR1H = 0x14, CCP1CON = 0x15, ADRESH = 0x1E, ADCON0 = 0x1F,
        OPTION_REG = 0x81, TRISA = 0x85, TRISC = 0x87, PIE1 = 0x8C, PCON = 0x8E,
        OSCCON = 0x8F, OSCTUNE = 0x90, ANSEL = 0x91, PR2 = 0x92, ADRESL = 0x9E, ADCON1 = 0x9F
    };
    enum { ST_C = 0, ST_DC = 1, ST_Z = 2, ST_PD = 3, ST_TO = 4, ST_RP0 = 5, ST_RP1 = 6, ST_IRP = 7 };

    /** A power-on reset: the registers go to their reset values and the program counter
        to zero. The flash is not touched. */
    void reset() {
        std::memset(ram, 0, sizeof ram);
        pc = 0; w = 0; sp = 0; sleeping = false; budget = 0.0;
        // Power-on values the firmware relies on: ports are inputs, pins analog, the
        // status flags TO and PD set.
        ram[STATUS] = 0x18;
        ram[TRISA] = 0xFF; ram[TRISC] = 0x3F;
        ram[ANSEL] = 0xFF;
        ram[OPTION_REG] = 0xFF;
        ram[PR2] = 0xFF;
        unsupported = false; unsupportedAt = -1; unsupportedWhy = "";
        adcCycles = adcStartWait = 0; adcResult = 0;
        ccpr1h = 0; dc1bLatch = 0; pwmArmed = false; pwmPin = false;
        clearPwmWindow();
        tmr2 = 0;
        cyclesRun = 0;
    }

    /** Programs the flash with 14-bit words, from address 0. */
    void load(const uint16_t* words, int n) {
        eraseFlash();
        for (int i = 0; i < n && i < kProgWords; i++) prog[i] = words[i] & 0x3FFF;
    }

    /** Programs the flash from Intel HEX text (byte addresses, little-endian words). */
    bool loadHex(const std::string& text) {
        eraseFlash();
        std::vector<uint8_t> mem(0x10000, 0xFF);
        uint32_t ext = 0;
        size_t pos = 0;
        while (pos < text.size()) {
            size_t eol = text.find('\n', pos);
            std::string ln = text.substr(pos, eol == std::string::npos ? std::string::npos : eol - pos);
            pos = eol == std::string::npos ? text.size() : eol + 1;
            while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ')) ln.pop_back();
            if (ln.size() < 11 || ln[0] != ':') continue;
            auto hex = [&](size_t i, int n) { return (uint32_t) std::stoul(ln.substr(i, n), nullptr, 16); };
            uint32_t len = hex(1, 2), addr = hex(3, 4), type = hex(7, 2);
            uint32_t sum = 0;
            for (size_t i = 1; i + 1 < ln.size(); i += 2) sum += hex(i, 2);
            if ((sum & 0xFF) != 0) return false;
            if (type == 0) {
                for (uint32_t i = 0; i < len; i++) {
                    uint32_t a = (ext << 16) + addr + i;
                    if (a < mem.size()) mem[a] = (uint8_t) hex(9 + 2 * i, 2);
                }
            }
            else if (type == 4) ext = hex(9, 4);
            else if (type == 1) break;
        }
        for (int i = 0; i < kProgWords; i++)
            prog[i] = (uint16_t) ((mem[2 * i] | (mem[2 * i + 1] << 8)) & 0x3FFF);
        return true;
    }

    // ---- the register file -------------------------------------------------------

    int bank() const { return (ram[STATUS] >> ST_RP0) & 3; }

    /** The storage index for a 7-bit file address in the current bank. SFRs that every
        bank sees (INDF, PCL, STATUS, FSR, PCLATH, INTCON) live in bank 0, and the top
        16 bytes of every bank are one block of RAM. */
    int index(int f7, int ind = -1) const {
        if (ind >= 0) {
            int a9 = ind & 0x1FF;
            return index2(a9);
        }
        int b = bank();
        return index2((b << 7) | (f7 & 0x7F));
    }
    static int index2(int a9) {
        int f = a9 & 0x7F, b = (a9 >> 7) & 3;
        if (f == 0x00 || f == 0x02 || f == 0x03 || f == 0x04 || f == 0x0A || f == 0x0B) return f;
        if (f >= 0x70) return f;               // common RAM
        return ((b & 1) << 7) | f;             // banks 2 and 3 are not implemented: they alias 0 and 1
    }

    void flagUnsupported(int addr, const char* why) {
        if (!unsupported) { unsupported = true; unsupportedAt = addr; unsupportedWhy = why; }
    }

    uint8_t readReg(int f7) {
        int idx = f7 == INDF ? index(0, fsrAddr()) : index(f7);
        if (f7 == INDF && (idx == 0)) return 0;       // INDF through INDF reads zero
        return readStorage(idx);
    }

    uint8_t readStorage(int idx) {
        switch (idx) {
            case PCL: return (uint8_t) pc;
            case PORTA: return (uint8_t) (ram[PORTA] & ~ram[TRISA]);
            case PORTC: return (uint8_t) (ram[PORTC] & ~ram[TRISC]);   // inputs read 0: the pins are analog
            case TMR2: return tmr2;
            case ADCON0: return (uint8_t) ((ram[ADCON0] & ~2) | (adcBusy() ? 2 : 0));
            case ADRESH: case ADRESL: case CCPR1L: case CCPR1H: case T2CON: case CCP1CON:
            case ANSEL: case TRISA: case TRISC: case PR2: case ADCON1: case OSCCON:
            case OPTION_REG: case STATUS: case FSR: case PCLATH: case INTCON: case PIR1:
            case PIE1: case PCON: case OSCTUNE: case TMR0:
                return ram[idx];
            default:
                if ((idx & 0x7F) < 0x20) flagUnsupported(idx, "register not modelled");
                return ram[idx];
        }
    }

    void writeReg(int f7, uint8_t v) {
        int idx = f7 == INDF ? index(0, fsrAddr()) : index(f7);
        writeStorage(idx, v);
    }

    void writeStorage(int idx, uint8_t v) {
        switch (idx) {
            case INDF: break;
            case PCL: pc = (uint16_t) (((ram[PCLATH] & 0x1F) << 8) | v); pclWritten = true; break;
            case STATUS:
                // TO and PD are read-only; the rest are written as given.
                ram[STATUS] = (uint8_t) ((ram[STATUS] & 0x18) | (v & ~0x18));
                break;
            case PORTA: case PORTC: ram[idx] = v; break;
            case TMR2: tmr2 = v; break;
            case T2CON:
                ram[idx] = v;
                if ((v & 3) != 0) flagUnsupported(idx, "TMR2 prescaler other than 1:1");
                break;
            case CCPR1L: ram[idx] = v; break;
            case CCP1CON: {
                ram[idx] = v;
                int mode = v & 0x0F;
                if (mode != 0 && mode != 0x0C) flagUnsupported(idx, "CCP1 mode other than PWM");
                if ((v & 0xC0) != 0) flagUnsupported(idx, "enhanced PWM output modes");
                break;
            }
            case ADCON0: {
                uint8_t old = ram[ADCON0];
                ram[ADCON0] = (uint8_t) (v & ~2);
                if ((v & 1) && (v & 2) && !(adcBusy())) startConversion();
                else if (!(v & 2) && adcBusy()) { adcCycles = 0; adcStartWait = 0; }   // GO cleared: aborted
                if ((v & 0x40) != 0) flagUnsupported(idx, "external A/D reference");
                (void) old;
                break;
            }
            case ADCON1: ram[idx] = v; break;
            case INTCON:
                ram[idx] = v;
                if (v & 0xC0) flagUnsupported(idx, "interrupts enabled");
                break;
            case ADRESH: case ADRESL: break;          // read-only results
            case TRISA: case TRISC: case ANSEL: case PR2: case OSCCON: case OPTION_REG:
            case FSR: case PCLATH: case PIR1: case PIE1: case PCON: case OSCTUNE: case TMR0:
            case CCPR1H:
                ram[idx] = v;
                break;
            default:
                if ((idx & 0x7F) < 0x20) flagUnsupported(idx, "register not modelled");
                ram[idx] = v;
        }
    }

    int fsrAddr() const { return ram[FSR] | ((ram[STATUS] >> ST_IRP & 1) << 8); }

    // ---- flags ---------------------------------------------------------------------
    void setC(bool b) { ram[STATUS] = (uint8_t) ((ram[STATUS] & ~1) | (b ? 1 : 0)); }
    void setDC(bool b) { ram[STATUS] = (uint8_t) ((ram[STATUS] & ~2) | (b ? 2 : 0)); }
    void setZ(uint8_t r) { ram[STATUS] = (uint8_t) ((ram[STATUS] & ~4) | (r == 0 ? 4 : 0)); }
    bool carry() const { return ram[STATUS] & 1; }

    uint8_t add8(uint8_t a, uint8_t b) {
        unsigned r = (unsigned) a + b;
        setC(r > 0xFF);
        setDC(((a & 0xF) + (b & 0xF)) > 0xF);
        uint8_t res = (uint8_t) r;
        setZ(res);
        return res;
    }
    /** a - b as the PIC does it: a + ~b + 1, so C is "no borrow". */
    uint8_t sub8(uint8_t a, uint8_t b) {
        unsigned r = (unsigned) a + (uint8_t) ~b + 1u;
        setC(r > 0xFF);
        setDC(((a & 0xF) + ((~b) & 0xF) + 1) > 0xF);
        uint8_t res = (uint8_t) r;
        setZ(res);
        return res;
    }

    // ---- the core ------------------------------------------------------------------
    void push(uint16_t a) { stack[sp & 7] = a; sp++; }
    uint16_t pop() { sp--; return stack[sp & 7]; }

    /** Executes one instruction and runs the peripherals for the cycles it took. */
    int step() {
        if (sleeping) { flagUnsupported(-1, "SLEEP"); advance(1); return 1; }
        uint16_t op = prog[pc & (kProgWords - 1)];
        pc = (uint16_t) ((pc + 1) & 0x1FFF);
        int cycles = 1;
        pclWritten = false;
        const int f = op & 0x7F;
        const bool d = (op >> 7) & 1;           // 1: result to f, 0: to W
        auto store = [&](uint8_t r) { if (d) writeReg(f, r); else w = r; };

        switch (op >> 12 & 3) {
        case 0: {
            switch ((op >> 8) & 0x0F) {
                case 0x0:
                    if (op & 0x80) writeReg(f, w);                       // MOVWF
                    else {
                        switch (op) {
                            case 0x0008: pc = pop(); cycles = 2; break;                // RETURN
                            case 0x0009: pc = pop(); cycles = 2;                       // RETFIE
                                         ram[INTCON] |= 0x80; break;
                            case 0x0063: sleeping = true; flagUnsupported(-1, "SLEEP"); break;
                            case 0x0064: break;                                        // CLRWDT
                            default: break;                                            // NOP
                        }
                    }
                    break;
                case 0x1:
                    if (op & 0x80) { writeReg(f, 0); setZ(0); }                       // CLRF
                    else { w = 0; setZ(0); }                                          // CLRW
                    break;
                case 0x2: { uint8_t r = sub8(readReg(f), w); store(r); break; }      // SUBWF
                case 0x3: { uint8_t r = (uint8_t) (readReg(f) - 1); setZ(r); store(r); break; }   // DECF
                case 0x4: { uint8_t r = readReg(f) | w; setZ(r); store(r); break; }  // IORWF
                case 0x5: { uint8_t r = readReg(f) & w; setZ(r); store(r); break; }  // ANDWF
                case 0x6: { uint8_t r = readReg(f) ^ w; setZ(r); store(r); break; }  // XORWF
                case 0x7: { uint8_t r = add8(readReg(f), w); store(r); break; }      // ADDWF
                case 0x8: { uint8_t r = readReg(f); setZ(r); store(r); break; }      // MOVF
                case 0x9: { uint8_t r = (uint8_t) ~readReg(f); setZ(r); store(r); break; }       // COMF
                case 0xA: { uint8_t r = (uint8_t) (readReg(f) + 1); setZ(r); store(r); break; }  // INCF
                case 0xB: { uint8_t r = (uint8_t) (readReg(f) - 1); store(r);                    // DECFSZ
                            if (r == 0) { pc = (uint16_t) ((pc + 1) & 0x1FFF); cycles = 2; } break; }
                case 0xC: { uint8_t v = readReg(f); uint8_t r = (uint8_t) ((v >> 1) | (carry() ? 0x80 : 0)); // RRF
                            setC(v & 1); store(r); break; }
                case 0xD: { uint8_t v = readReg(f); uint8_t r = (uint8_t) ((v << 1) | (carry() ? 1 : 0));    // RLF
                            setC(v & 0x80); store(r); break; }
                case 0xE: { uint8_t v = readReg(f); store((uint8_t) ((v << 4) | (v >> 4))); break; }  // SWAPF
                case 0xF: { uint8_t r = (uint8_t) (readReg(f) + 1); store(r);                    // INCFSZ
                            if (r == 0) { pc = (uint16_t) ((pc + 1) & 0x1FFF); cycles = 2; } break; }
            }
            break;
        }
        case 1: {                                                    // bit-oriented
            int b = (op >> 7) & 7;
            switch ((op >> 10) & 3) {
                case 0: writeReg(f, (uint8_t) (readReg(f) & ~(1u << b))); break;     // BCF
                case 1: writeReg(f, (uint8_t) (readReg(f) | (1u << b))); break;      // BSF
                case 2: if (!((readReg(f) >> b) & 1)) { pc = (uint16_t) ((pc + 1) & 0x1FFF); cycles = 2; } break;   // BTFSC
                case 3: if ((readReg(f) >> b) & 1) { pc = (uint16_t) ((pc + 1) & 0x1FFF); cycles = 2; } break;      // BTFSS
            }
            break;
        }
        case 2: {                                                    // CALL, GOTO
            uint16_t target = (uint16_t) (((ram[PCLATH] & 0x18) << 8) | (op & 0x7FF));
            if (op & 0x0800) pc = target;                            // GOTO
            else { push(pc); pc = target; }                          // CALL
            cycles = 2;
            break;
        }
        case 3: {                                                    // literal
            uint8_t k = (uint8_t) (op & 0xFF);
            switch ((op >> 8) & 0x0F) {
                case 0x0: case 0x1: case 0x2: case 0x3: w = k; break;               // MOVLW
                case 0x4: case 0x5: case 0x6: case 0x7: w = k; pc = pop(); cycles = 2; break;   // RETLW
                case 0x8: w |= k; setZ(w); break;                                    // IORLW
                case 0x9: w &= k; setZ(w); break;                                    // ANDLW
                case 0xA: w ^= k; setZ(w); break;                                    // XORLW
                case 0xC: case 0xD: w = sub8(k, w); break;                           // SUBLW
                case 0xE: case 0xF: w = add8(k, w); break;                           // ADDLW
                default: break;
            }
            break;
        }
        }
        // A write to PCL is a jump, and costs the extra cycle.
        if (pclWritten && cycles < 2) cycles = 2;
        advance(cycles);
        cyclesRun += cycles;
        return cycles;
    }

    /** Runs for `cycles` instruction cycles (fractional counts carry), returning the
        number actually executed. An instruction in flight at the end of the window is
        completed and its excess charged to the next. */
    void run(double cycles) {
        budget += cycles;
        while (budget >= 1.0)
            budget -= (double) step();
    }

    // ---- the peripherals --------------------------------------------------------------
    uint8_t tmr2 = 0;

    bool adcBusy() const { return adcCycles > 0 || adcStartWait > 0; }

    static int adcDivider(int adcs) {
        static const int d[8] = { 2, 8, 32, 0, 4, 16, 64, 0 };
        return d[adcs & 7];
    }

    void startConversion() {
        int div = adcDivider((ram[ADCON1] >> 4) & 7);
        if (div == 0) div = 32;                       // the internal RC: nominally 4 us
        // Eleven TAD, a TAD being `div` oscillator periods, four of which are a cycle.
        adcCycles = 11 * div / 4;
        adcStartWait = 1;                             // the conversion starts on the next cycle
        adcSampleCh = (ram[ADCON0] >> 2) & 7;
    }

    void advance(int cycles) {
        for (int c = 0; c < cycles; c++) {
            if (adcStartWait > 0) {
                if (--adcStartWait == 0) {
                    double v = analog[adcSampleCh];
                    if (v < 0) v = 0;
                    int r = (int) std::floor(v / kVdd * 1024.0);
                    adcResult = r < 0 ? 0 : (r > 1023 ? 1023 : r);
                }
            }
            else if (adcCycles > 0) {
                if (--adcCycles == 0) {
                    if (ram[ADCON0] & 0x80) {                           // right-justified
                        ram[ADRESH] = (uint8_t) (adcResult >> 8);
                        ram[ADRESL] = (uint8_t) (adcResult & 0xFF);
                    }
                    else {                                              // left-justified
                        ram[ADRESH] = (uint8_t) (adcResult >> 2);
                        ram[ADRESL] = (uint8_t) ((adcResult & 3) << 6);
                    }
                }
            }
            pwmCycle();
        }
    }

    /** One instruction cycle of TMR2 and the CCP1 PWM. */
    void pwmCycle() {
        bool timerOn = (ram[T2CON] >> 2) & 1;
        bool pwm = (ram[CCP1CON] & 0x0F) == 0x0C;
        if (!timerOn) {
            pwmTotalTicks += 4;
            return;
        }
        // The duty in oscillator periods: CCPR1H and the two latched bits.
        int duty = (ccpr1h << 2) | dc1bLatch;
        int t = tmr2;
        double high = 0;
        if (pwm && pwmArmed)
            high = duty - 4 * t;                  // ticks of this cycle that are below the compare
        if (high < 0) high = 0;
        if (high > 4) high = 4;
        pwmHighTicks += high;
        pwmTotalTicks += 4;
        pwmPin = high > 0;
        if (t == ram[PR2]) {                      // period match
            tmr2 = 0;
            ccpr1h = ram[CCPR1L];                 // the duty is taken from CCPR1L...
            dc1bLatch = (uint8_t) ((ram[CCP1CON] >> 4) & 3);   // ...and the two bits with it
            pwmArmed = pwm;
        }
        else tmr2 = (uint8_t) (tmr2 + 1);
    }

    void clearPwmWindow() { pwmHighTicks = 0; pwmTotalTicks = 0; }

    /** The fraction of the window the PWM pin was high. */
    double pwmAverage() const { return pwmTotalTicks > 0 ? pwmHighTicks / pwmTotalTicks : 0.0; }

    /** The duty the firmware has set up, as CCPR1L and the two DC1B bits make it,
        0..1023. This is what has been written, not what has reached the pin yet. */
    int pwmDutyRegister() const { return (ram[CCPR1L] << 2) | ((ram[CCP1CON] >> 4) & 3); }
};

} // namespace pic16

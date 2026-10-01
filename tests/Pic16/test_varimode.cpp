// The Varimode quantizer firmware on the emulated PIC16F684, and the wrapper that
// gives the chip its analogue surroundings.
//
// Two kinds of test. The wrapper's own behaviour -- the PWM filter, the mode voltages,
// the note-change report -- is checked with a few instructions of hand-assembled
// stand-in firmware, so it always runs. What the *real* firmware does needs the real
// image, which is never in this repository: set VARIMODE_HEX to the .HEX. Set
// VARIMODE_ASM to its source as well and the test adds an independent oracle -- a small
// interpreter for the assembly text -- and compares it with the emulator on every
// input in every mode.

#include "../../src/PaymentSchedule/Varimode.hpp"
#include "../../src/PaymentSchedule/VarimodeFirmware.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <vector>

using namespace paysched::varimode;
using pic16::Pic16f684;

static int checks = 0, failures = 0;
static void check(const char* what, bool ok, const char* detail = 0) {
	checks++;
	if (!ok) {
		failures++;
		printf("  FAIL  %s%s%s\n", what, detail ? ": " : "", detail ? detail : "");
	}
}

static std::string slurp(const char* path) {
	std::ifstream f(path);
	std::stringstream ss;
	ss << f.rdbuf();
	return ss.str();
}

// ---- a stand-in firmware: the Varimode's PWM set-up, then ADRESH straight into CCPR1L ---
typedef uint16_t W;
static std::vector<W> standIn() {
	enum { STATUS = 3, T2CON = 0x12, CCPR1L = 0x13, CCP1CON = 0x15, ADRESH = 0x1E, ADCON0 = 0x1F,
	       PR2 = 0x12 /* in bank 1: 0x92 */, ADCON1 = 0x1F /* bank 1: 0x9F */, ANSEL = 0x11 /* bank 1: 0x91 */ };
	auto MOVLW = [](int k) { return (W) (0x3000 | k); };
	auto MOVWF = [](int f) { return (W) (0x0080 | f); };
	auto BSF = [](int f, int b) { return (W) (0x1400 | (b << 7) | f); };
	auto BCF = [](int f, int b) { return (W) (0x1000 | (b << 7) | f); };
	auto BTFSC = [](int f, int b) { return (W) (0x1800 | (b << 7) | f); };
	auto MOVF = [](int f, int d) { return (W) (0x0800 | (d << 7) | f); };
	auto GOTO = [](int a) { return (W) (0x2800 | a); };
	std::vector<W> p;
	p.push_back(MOVLW(0x04)); p.push_back(MOVWF(T2CON));                  // 0,1  TMR2 on
	p.push_back(BSF(STATUS, 5));                                           // 2    bank 1
	p.push_back(MOVLW(255)); p.push_back(MOVWF(PR2));                      // 3,4
	p.push_back(MOVLW(0x60)); p.push_back(MOVWF(ADCON1));                  // 5,6  FOSC/64
	p.push_back(MOVLW(0x30)); p.push_back(MOVWF(ANSEL));                   // 7,8  AN4 and AN5 analog
	p.push_back(BCF(STATUS, 5));                                           // 9
	p.push_back(MOVLW(0x0C)); p.push_back(MOVWF(CCP1CON));                 // 10,11 PWM
	// loop (12): AN4, convert, copy ADRESH into CCPR1L
	p.push_back(MOVLW(0x11)); p.push_back(MOVWF(ADCON0));                  // 12,13
	p.push_back(BSF(ADCON0, 1));                                           // 14
	p.push_back(BTFSC(ADCON0, 1)); p.push_back(GOTO(15));                  // 15,16  wait
	p.push_back(MOVF(ADRESH, 0)); p.push_back(MOVWF(CCPR1L));              // 17,18
	p.push_back(GOTO(12));                                                 // 19
	return p;
}

// ---- our own firmware, checked against an independent quantizer ----------------------------

static const int kMajor[] = { 0, 2, 4, 5, 7, 9, 11 };
static const int kMajPent[] = { 0, 2, 4, 7, 9 };
static const int kMinor[] = { 0, 2, 3, 5, 7, 8, 10 };
static const int kMinPent[] = { 0, 3, 5, 7, 10 };
static const int kChrom[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };

/** What a quantizer should do with a 10-bit code, worked from the definition and in integers: the
    input is code * 60 / 1024 semitones; the answer is the in-scale semitone nearest to it, an
    exact tie going up; the output is the duty that puts that many twelfths of a volt on a 5 V,
    10-bit PWM, round-half-up. Nothing here is the firmware's method. */
static int idealDuty(int code, Mode mode) {
	const int* deg; int n;
	switch (mode) {
		case MAJOR: deg = kMajor; n = 7; break;
		case MAJOR_PENT: deg = kMajPent; n = 5; break;
		case MINOR: deg = kMinor; n = 7; break;
		case MINOR_PENT: deg = kMinPent; n = 5; break;
		default: deg = kChrom; n = 12; break;
	}
	long x1024 = 60L * code;                      // the input in 1/1024 semitones
	long bestD = -1; int best = 0;
	for (int oct = -1; oct <= 5; oct++)
		for (int i = 0; i < n; i++) {
			int note = 12 * oct + deg[i];
			long d = std::labs(1024L * note - x1024);
			if (bestD < 0 || d < bestD || (d == bestD && note > best)) { bestD = d; best = note; }
		}
	if (best > 60) best = 60;
	if (best < 0) best = 0;
	long duty = (best * 1024L + 30) / 60;         // round half up (the .5 case cannot occur: 15 is odd)
	return (int) (duty > 1023 ? 1023 : duty);
}

static void testOurFirmware() {
	printf("Our firmware (firmware/varimode/varimode_fixed.asm)\n");
	Pic16f684 c;
	c.load(paysched::firmware::kVarimodeFixed, paysched::firmware::kVarimodeFixedWords);
	c.reset();
	int bad = 0, cases = 0, firstBad = -1;
	for (int m = 0; m < kModes; m++) {
		for (int code = 0; code < 1024; code++) {
			c.analog[4] = (code + 0.5) / 1024.0 * 5.0;
			c.analog[5] = modeVolts((Mode) m);
			c.run(2500);
			int got = c.pwmDutyRegister();
			int want = idealDuty(code, (Mode) m);
			cases++;
			if (got != want) {
				if (firstBad < 0) { firstBad = m * 1024 + code; printf("    mode %d code %d: firmware %d, ideal %d\n", m, code, got, want); }
				bad++;
			}
		}
	}
	char d[64];
	snprintf(d, sizeof d, "%d of %d inputs differ", bad, cases);
	check("every one of 1024 input codes in all five modes gives the ideal quantizer's duty", bad == 0, d);
	check("it never leaves what the emulator implements", !c.unsupported, c.unsupportedWhy);

	// The mode bands, at their edges: the top 8 bits of the mode pin.
	static const int edge[4] = { 52, 104, 153, 204 };
	bool bands = true;
	for (int e = 0; e < 4; e++)
		for (int side = 0; side < 2; side++) {
			int m8 = edge[e] + side;
			Mode want = (Mode) (e + side);
			c.analog[4] = (300 + 0.5) / 1024.0 * 5.0;
			c.analog[5] = (m8 + 0.5) / 256.0 * 5.0;
			c.run(2500);
			if (c.pwmDutyRegister() != idealDuty(300, want)) bands = false;
		}
	check("the five scales change over at mode codes 52/53, 104/105, 153/154 and 204/205", bands);

	// The output's own accuracy: every semitone lands within half a count of n/12 volts.
	double worst = 0;
	for (int n = 0; n <= 60; n++) {
		int code = (int) std::floor(n * 1024.0 / 60.0 + 0.5);
		if (code > 1023) code = 1023;
		c.analog[4] = (code + 0.5) / 1024.0 * 5.0;
		c.analog[5] = modeVolts(CHROMATIC);
		c.run(2500);
		double volts = c.pwmDutyRegister() / 1024.0 * 5.0;
		double e = std::fabs(volts - n / 12.0);
		if (e > worst) worst = e;
	}
	char w[64];
	snprintf(w, sizeof w, "worst %.2f mV", worst * 1000.0);
	check("every chromatic note is within 5 mV of n/12 volts", worst < 0.005, w);

	// Speed.
	Pic16f684 s;
	s.load(paysched::firmware::kVarimodeFixed, paysched::firmware::kVarimodeFixedWords);
	s.reset();
	s.analog[4] = 1.0; s.analog[5] = modeVolts(CHROMATIC);
	s.run(5000);
	int changes = 0, last = s.pwmDutyRegister();
	for (int i = 0; i < 400; i++) {
		s.analog[4] = (i % 2) ? 0.5 : 3.5;
		s.run(2500);                                  // a pass of the loop is 794 cycles (159 us)
		if (s.pwmDutyRegister() != last) { changes++; last = s.pwmDutyRegister(); }
	}
	check("it decides on every pass: a new note within 1100 cycles (220 us) of the input changing", changes >= 399);

	// Through the wrapper: a note comes out at its voltage, and the note-change report fires.
	Varimode v;
	v.setSampleRate(48000.0);
	v.pic.load(paysched::firmware::kVarimodeFixed, paysched::firmware::kVarimodeFixedWords);
	v.loaded = true;
	v.powerOn();
	double out = 0;
	for (int i = 0; i < 9600; i++) out = v.process(2.5, MAJOR);       // 2.5 V = 30 semitones: F#, nearest F or G
	double want = idealDuty((int) std::floor(2.5 / 5.0 * 1024.0), MAJOR) / 1024.0 * 5.0;
	check("2.5 V in, major, settles at the quantized note", std::fabs(out - want) < 0.003, "settled value");
	check("the wrapper runs it without leaving the model", !v.unsupported());
}

static void testWrapperWithStandIn() {
	printf("Wrapper: filter, mode voltages and note changes (stand-in firmware)\n");
	std::vector<W> prog = standIn();

	// Mode voltages land in the right ADRESH band, which is what the firmware reads.
	static const int lo[kModes] = { 0, 53, 105, 154, 205 }, hi[kModes] = { 52, 104, 153, 204, 255 };
	bool bands = true;
	for (int m = 0; m < kModes; m++) {
		int code = (int) std::floor(modeVolts((Mode) m) / 5.0 * 256.0);
		if (code < lo[m] || code > hi[m]) bands = false;
	}
	check("each mode's voltage is inside its ADRESH band", bands);

	Varimode v;
	v.setSampleRate(48000.0);
	v.pic.load(prog.data(), (int) prog.size());
	v.loaded = true;
	v.powerOn();

	// A constant input settles to ADRESH/1024 of the way up: the 8-bit code from AN4 lands in
	// CCPR1L, so the PWM is (code*4)/1024 of VDD = code/256.
	double out = 0;
	for (int i = 0; i < 4800; i++) out = v.process(2.5, CHROMATIC);
	check("a steady 2.5 V in settles at 2.5 V out", std::fabs(out - 2.5) < 0.02, "settled value");
	check("the stand-in never leaves what the model implements", !v.unsupported());

	// Two cascaded poles at 160 Hz: a step reaches 1 - (1+x)e^-x of the way, x = t/tau.
	Varimode u;
	u.setSampleRate(48000.0);
	u.pic.load(prog.data(), (int) prog.size());
	u.loaded = true;
	u.powerOn();
	for (int i = 0; i < 4800; i++) u.process(0.0, CHROMATIC);
	double tau = 1.0 / (2.0 * M_PI * assumed::PWM_FILTER_HZ);
	double y = 0, tAt = 0;
	int n = 0;
	for (; n < 48000 && y < 0.5 * 4.0; n++) y = u.process(4.0, CHROMATIC);
	tAt = n / 48000.0;
	// Half of a two-pole step: x ~ 1.678 (solve 1-(1+x)e^-x = 0.5), t = 1.678 tau, plus the
	// conversion and PWM-period latency of the chip itself (about 0.2 ms).
	double want = 1.678 * tau;
	check("the output filter is two poles at 160 Hz", tAt > want && tAt < want + 0.0004, "half-step time");

	// A note change is reported once, after the register has held its new value a sample.
	Varimode w;
	w.setSampleRate(48000.0);
	w.pic.load(prog.data(), (int) prog.size());
	w.loaded = true;
	w.powerOn();
	int reports = 0;
	for (int i = 0; i < 4800; i++) { w.process(1.0, CHROMATIC); reports += w.noteChanged(); }
	int first = reports;
	for (int i = 0; i < 4800; i++) { w.process(1.0, CHROMATIC); reports += w.noteChanged(); }
	check("a steady input reports no further changes", reports == first);
	int before = reports;
	for (int i = 0; i < 480; i++) { w.process(3.0, CHROMATIC); reports += w.noteChanged(); }
	check("a new note is reported exactly once", reports == before + 1);
	check("the same note again is not", (w.process(3.0, CHROMATIC), !w.noteChanged()));

	// Power-on restarts the program but keeps it.
	w.powerOn();
	for (int i = 0; i < 4800; i++) out = w.process(2.5, CHROMATIC);
	check("a power cycle keeps the flash and runs again", std::fabs(out - 2.5) < 0.02);
}

// ---- the real firmware ---------------------------------------------------------------

/** An interpreter for the Varimode source's own text: just the instructions its tables are
    made of. It is the oracle, and shares nothing with the emulator but the language. */
struct Oracle {
	struct Stmt { std::string op, a, b; };
	std::vector<Stmt> prog;
	std::map<std::string, int> label;
	int volthi = 0, modehi = 0, ccpr1l = 0, ccp1con = 0x0C;
	int w = 0;
	bool c = false;
	int start = -1;

	static std::string trim(std::string s) {
		size_t a = s.find_first_not_of(" \t\r"), b = s.find_last_not_of(" \t\r");
		return a == std::string::npos ? "" : s.substr(a, b - a + 1);
	}
	static int lit(const std::string& t) {
		if (t[0] == '.') return atoi(t.c_str() + 1);
		if (t[0] == 'B' || t[0] == 'b') return (int) strtol(t.substr(2, t.size() - 3).c_str(), nullptr, 2);
		return atoi(t.c_str());
	}
	bool load(const std::string& text) {
		std::istringstream in(text);
		std::string ln;
		while (std::getline(in, ln)) {
			size_t sc = ln.find(';');
			if (sc != std::string::npos) ln = ln.substr(0, sc);
			bool startsWithSpace = !ln.empty() && (ln[0] == ' ' || ln[0] == '\t');
			ln = trim(ln);
			if (ln.empty()) continue;
			std::istringstream ts(ln);
			std::vector<std::string> tok;
			std::string t;
			while (ts >> t) tok.push_back(t);
			static const char* ops[] = { "MOVLW", "MOVWF", "MOVF", "SUBLW", "BTFSC", "BSF", "BCF", "GOTO", "CLRF", "BSF", "LIST", "List", "ORG", "END", "EQU", "MOVLW" };
			auto isOp = [&](const std::string& s) { for (auto o : ops) if (s == o) return true; return false; };
			size_t i = 0;
			if (!startsWithSpace && !isOp(tok[0]) && tok.size() >= 1 && !(tok.size() > 1 && tok[1] == "EQU")) {
				label[tok[0]] = (int) prog.size();
				i = 1;
			}
			if (i >= tok.size()) continue;
			const std::string op = tok[i];
			if (op == "EQU" || (tok.size() > i + 1 && tok[i + 1] == "EQU") || op == "List" || op == "LIST" || op == "ORG" || op == "END") continue;
			Stmt s;
			s.op = op;
			if (i + 1 < tok.size()) {
				std::string args = tok[i + 1];
				size_t comma = args.find(',');
				if (comma == std::string::npos) s.a = args;
				else { s.a = args.substr(0, comma); s.b = args.substr(comma + 1); }
			}
			prog.push_back(s);
		}
		// The dispatcher starts at the first "MOVF MODEHI,W" that is followed by "SUBLW .52".
		for (size_t k = 0; k + 1 < prog.size(); k++)
			if (prog[k].op == "MOVF" && prog[k].a == "MODEHI" && prog[k + 1].op == "SUBLW" && lit(prog[k + 1].a) == 52) { start = (int) k; break; }
		return start >= 0 && label.count("BEGIN");
	}
	/** One pass of the main loop from the dispatcher with these ADC readings. */
	int run(int vol, int mode) {
		volthi = vol; modehi = mode;
		int pc = start, guard = 0;
		while (guard++ < 100000 && pc < (int) prog.size()) {
			const Stmt& s = prog[pc];
			if (s.op == "MOVF") { w = s.a == "MODEHI" ? modehi : volthi; pc++; }
			else if (s.op == "SUBLW") { int k = lit(s.a) & 0xFF; c = k >= w; w = (k - w) & 0xFF; pc++; }
			else if (s.op == "BTFSC") { pc += (!c ? 2 : 1); }               // STATUS,0 is the only bit tested
			else if (s.op == "GOTO") { if (s.a == "BEGIN") break; pc = label[s.a]; }
			else if (s.op == "MOVLW") { w = lit(s.a); pc++; }
			else if (s.op == "MOVWF") { if (s.a == "CCPR1L") ccpr1l = w; pc++; }
			else if (s.op == "BSF") { if (s.a == "CCP1CON") ccp1con |= 1 << lit(s.b); pc++; }
			else if (s.op == "BCF") { if (s.a == "CCP1CON") ccp1con &= ~(1 << lit(s.b)); pc++; }
			else pc++;
		}
		return (ccpr1l << 2) | ((ccp1con >> 4) & 3);
	}
};

static void testRealFirmware(const char* hexPath, const char* asmPath) {
	printf("Varimode firmware on the emulated PIC16F684\n");
	std::string hex = slurp(hexPath);
	Pic16f684 c;
	check("the .HEX loads and its checksums are good", c.loadHex(hex));
	c.reset();
	int words = 0;
	for (int i = 0; i < Pic16f684::kProgWords; i++) if (c.prog[i] != 0x3FFF) words = i + 1;
	check("it is a program of the size the source implies (about 1100 words)", words > 1000 && words < 1300);
	check("it starts with GOTO 1 and sets up TMR2", c.prog[0] == 0x2801 && c.prog[1] == 0x3004);

	// A sweep: every input code in every mode, in the order the oracle sees it.
	Oracle o;
	bool haveOracle = asmPath && o.load(slurp(asmPath));
	if (asmPath) check("the source parses into the oracle", haveOracle);
	const int modeCode[kModes] = { 26, 78, 129, 179, 230 };
	int mismatches = 0, cases = 0;
	long cyclesLoop = 0; int loops = 0;
	int firstBad = -1;
	std::vector<std::vector<int> > duty(kModes, std::vector<int>(256, 0));
	for (int m = 0; m < kModes; m++) {
		for (int code = 0; code < 256; code++) {
			c.analog[4] = (code + 0.5) / 256.0 * 5.0;
			c.analog[5] = (modeCode[m] + 0.5) / 256.0 * 5.0;
			c.run(3000);
			int got = c.pwmDutyRegister();
			duty[m][code] = got;
			cases++;
			if (haveOracle) {
				int want = o.run(code, modeCode[m]);
				if (got != want) {
					if (firstBad < 0) { firstBad = m * 256 + code; printf("    mode %d code %d: emulator %d, oracle %d\n", m, code, got, want); }
					mismatches++;
				}
			}
		}
	}
	check("the real firmware never leaves what the model implements", !c.unsupported, c.unsupportedWhy);
	if (haveOracle) {
		char d[64];
		snprintf(d, sizeof d, "%d of %d inputs differ", mismatches, cases);
		check("the emulator running the .HEX agrees with an interpreter of the source on every input in every mode", mismatches == 0, d);
	}

	// Properties that hold of the firmware whatever its tables say.
	bool mono = true, inRange = true;
	for (int m = 0; m < kModes; m++)
		for (int code = 1; code < 256; code++) {
			if (duty[m][code] < duty[m][code - 1]) mono = false;
			if (duty[m][code] < 0 || duty[m][code] > 1023) inRange = false;
		}
	check("output never falls as the input rises, in any mode", mono);
	check("output is a 10-bit duty", inRange);
	int distinct[kModes];
	for (int m = 0; m < kModes; m++) {
		std::map<int, int> s;
		for (int code = 0; code < 256; code++) s[duty[m][code]] = 1;
		distinct[m] = (int) s.size();
	}
	check("chromatic has one step per semitone: 61 levels over five octaves", distinct[CHROMATIC] == 61 || distinct[CHROMATIC] == 60);
	check("the modes have fewer notes: pentatonics fewer than diatonics fewer than chromatic",
	      distinct[MAJOR_PENT] < distinct[MAJOR] && distinct[MINOR_PENT] < distinct[MINOR]
	      && distinct[MAJOR] < distinct[CHROMATIC] && distinct[MINOR] < distinct[CHROMATIC]);

	// Tuning: the chromatic steps against the ideal 1/12 V (17.07 counts of 1024).
	double worst = 0;
	for (int semis = 0; semis <= 60; semis++) {
		int code = (int) std::floor((semis / 12.0) / 5.0 * 256.0 + 0.5);
		if (code > 255) code = 255;
		double ideal = semis * 1023.0 / 60.0;
		double e = std::fabs(duty[CHROMATIC][code] - ideal);
		if (e > worst) worst = e;
	}
	printf("    chromatic mode, worst step error against 1/12 V: %.1f counts (%.1f mV)\n", worst, worst / 1024.0 * 5000.0);
	check("the tuning error is under a third of a semitone (28 mV)", worst < 1024.0 * 0.028 / 5.0);

	// Speed: how often the firmware decides.
	Pic16f684 d2;
	d2.loadHex(hex);
	d2.reset();
	d2.analog[4] = 2.0; d2.analog[5] = modeVolts(CHROMATIC);
	long total = 0;
	int last = -1;
	std::vector<long> change;
	for (int step = 0; step < 4000; step++) {
		d2.analog[4] = 0.5 + (step % 2) * 3.0;           // keep it changing every 2000 cycles
		d2.run(1000);
		total += 1000;
		if (d2.pwmDutyRegister() != last) { change.push_back(total); last = d2.pwmDutyRegister(); }
	}
	(void) cyclesLoop; (void) loops;
	check("the loop decides several times a millisecond: it has settled after a conversion pair", change.size() > 3);

	// Through the wrapper: the quantizer as the module will use it.
	Varimode v;
	v.setSampleRate(48000.0);
	check("the wrapper loads the firmware", v.loadHex(hex));
	double out = 0;
	for (int i = 0; i < 9600; i++) out = v.process(1.0, CHROMATIC);          // 1.0 V: twelve semitones
	check("1.0 V in, chromatic: the output settles at the firmware's note for 1.0 V",
	      std::fabs(out - duty[CHROMATIC][(int) std::floor(1.0 / 5.0 * 256.0 + 0.5)] / 1024.0 * 5.0) < 0.005);
	for (int i = 0; i < 9600; i++) out = v.process(1.03, CHROMATIC);        // a little sharp: same note
	check("a little off the note quantizes back onto it", std::fabs(out - 1.0) < 0.06);
	check("the wrapper runs it without leaving the model", !v.unsupported());
}

int main() {
	testWrapperWithStandIn();
	testOurFirmware();
	const char* hex = getenv("VARIMODE_HEX");
	if (hex && *hex) testRealFirmware(hex, getenv("VARIMODE_ASM"));
	else printf("SKIP  the real-firmware tests: set VARIMODE_HEX to varimodequantizer_100.HEX (and VARIMODE_ASM to its source)\n");
	printf("\n%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}

// Apportionment -- the Ensoniq DP/4 main board: an MC68B03 host running the real
// operating system, four ES5510 ESPs running the real microcode, and everything
// the firmware talks to between them. No Rack in here, so it can be tested.
//
// What is known about the board, and how (the research notes are in
// claude_music_tools/DP4Research/NOTES.md; the service manual has no schematic):
//
//   $0000-$001F  6803 on-chip registers        $0080-$00FF  on-chip RAM
//   $0200        read in IRQ1 (acknowledge)    $0400        CV-pedal A/D
//   $0600        latch: low nibble = UCODE ROM bank, high nibble = HALT, ESP A..D
//   $0800/0A00/0C00/0E00  ESP A/B/C/D host windows (offset = addr & $FF)
//   $1800/$1801  6850-style ACIA to the front-panel board
//   $2000-$3FFF  8 KB window into the 128 KB UCODE ROM
//   $4000-$5FFF  battery RAM                   $6000-$7FFF  banked RAM, P1 bits 0-1
//   $8000-$FFFF  OS ROM
//   P1 bits 2-3  the C/D input mux (below)     P2 bit 1     data-knob direction
//   TIN / ICF    one edge per data-knob detent
//
// Audio: every ESP's host serial control is $6B -- slave, Sony format, SER0 and
// SER2 in, SER1 and SER3 out -- and every program ENDs at step $55, so each
// frame is 86 microinstructions. The wiring below is what every one of the 50
// ROM Config presets' I/O code is consistent with:
//
//     ADC 1/2 -> A.SER0, B.SER0       A.SER3 -> B.SER2     B.SER3 -> A.SER2
//     mux     -> C.SER0, D.SER0       C.SER3 -> D.SER2     D.SER3 -> C.SER2
//     B.SER1  -> DAC 1/2              D.SER1 -> DAC 3/4
//
// with the mux, from $4978 via $8728: 00 = B.SER1, 01 = ADC 1/2, 10 = ADC 3/4.
// The frame rate is 34.875 kHz: at that rate the service-mode "1 kHz" test tone
// is exactly 1 kHz, matching its oscillator coefficient.
#pragma once
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

#include "Esp.hpp"
#include "M6803.hpp"

namespace dp4 {

/** The live Config, read straight from the firmware's RAM. */
struct Routing {
	int sources = 1;     // 1..4
	int abRoute = 0;     // 0 serial, 1 parallel, 2 feedback1, 3 feedback2
	int cdRoute = 0;
	int abToCd = 0;      // 0 serial, 1 parallel, 2 independent (2+ sources)
	int abAmount = 0;    // 0..99: dry path around AB, or feedback B to A
	int cdAmount = 0;
	int abMono = 0;      // input select: 0 stereo, 1 mono
	int cdMono = 0;
	int abOut = 0;       // 3-4 sources: 0 "A>1 B>2 DualMono", 1 "1-2 Mixed Stereo"
	int cdOut = 0;       // 4 sources:   0 "C>3 D>4 DualMono", 1 "3-4 Mixed Stereo"
	int kill[4] = {};    // per unit, what bypass does: 0 bypass (dry through), 1 kill (mute)
	bool operator==(const Routing& o) const {
		return sources == o.sources && abRoute == o.abRoute && cdRoute == o.cdRoute &&
			abToCd == o.abToCd && abAmount == o.abAmount && cdAmount == o.cdAmount &&
			abMono == o.abMono && cdMono == o.cdMono && abOut == o.abOut && cdOut == o.cdOut &&
			kill[0] == o.kill[0] && kill[1] == o.kill[1] && kill[2] == o.kill[2] && kill[3] == o.kill[3];
	}
};

/** Addresses of the Config fields in battery RAM (found by diffing RAM around
    edits on the Edit/Config pages; see the research notes). */
namespace cfg {
enum : uint16_t {
	SOURCES = 0x4979,    // sources - 1
	AB_TO_CD = 0x4978,   // derived: 0 serial, 1 parallel, 2 independent
	AB_ROUTE = 0x4930,   // with 3-4 sources: AB output select
	AB_AMOUNT = 0x4931,
	CD_ROUTE = 0x4976,   // with 4 sources: CD output select
	CD_AMOUNT = 0x4977,
	AB_MONO = 0x497a,
	CD_MONO = 0x497b,
	KILL_A = 0x497e,     // ..0x4981 for A..D: bypass/kill, one Config page each
	EDIT_PAGE = 0x520e,  // the Config page the firmware is showing, while editing
};
}

/** The DP/4's buttons, numbered as the service manual's "BUTTON#" test. */
enum Button {
	BTN_CONFIG = 0, BTN_D = 1, BTN_C = 2, BTN_B = 3, BTN_A = 4, BTN_SYSTEM = 5,
	BTN_EDIT = 6, BTN_FOOT_R = 7, BTN_CANCEL = 8, BTN_WRITE = 10, BTN_RIGHT = 11,
	BTN_LEFT = 12, BTN_SELECT = 13, BTN_FOOT_L = 15,
};

/** What the front-panel board is showing, decoded from the firmware's byte stream:
    a 2x16 LCD, the two seven-segment digits and the indicator LEDs. */
struct Display {
	char lcd[32];
	bool blink[32];      // the flashing (being edited) field
	uint32_t leds = 0;   // bits 0-15 indicators (numbered as the buttons), 16-31 segments
	uint32_t generation = 0;
	Display() { clear(); }
	void clear() { std::memset(lcd, ' ', sizeof lcd); std::memset(blink, 0, sizeof blink); }
	std::string line(int n) const { return std::string(lcd + 16 * n, 16); }
	bool led(int n) const { return (leds >> n) & 1; }
	/** A unit's red bypass LED (unit 0 = A): indicators 12, 11, 10, 9. */
	bool bypassed(int unit) const { return led(12 - unit); }
	/** Segment k of digit d (0 = left), k in the order a b c d e f g dp. */
	bool segment(int d, int k) const {
		static const int bit[8] = { 7, 6, 5, 4, 3, 2, 1, 0 };
		return (leds >> (16 + 8 * d + bit[k])) & 1;
	}
};

class Machine : private CpuBus {
public:
	static constexpr double E_CLOCK = 2000000.0;
	static constexpr double FRAME_RATE = 34875.0;
	static constexpr int STEPS = 0x56;
	static constexpr int SLICES = 8;
	static constexpr size_t BATTERY_BYTES = 0x2000 + 4 * 0x2000;

	Machine() { cpu_.bus = this; }

	/** Takes the two EPROM images. Returns an error message, or "" when usable. */
	std::string load(const std::vector<uint8_t>& os, const std::vector<uint8_t>& ucode) {
		if (os.size() != 0x8000) return "OS ROM must be 32 KB";
		if (ucode.size() != 0x20000) return "UCODE ROM must be 128 KB";
		os_ = os;
		ucode_ = ucode;
		return "";
	}
	bool loaded() const { return !os_.empty(); }

	/** Power on: the 6803 comes out of reset; RAM keeps whatever the battery kept. */
	void powerOn() {
		for (auto& e : esp_) e.reset();
		latch0600_ = 0;
		cpu_.reset();
		panel_ = Display();
		osVersion_.clear();
		acia_.clear();
		pendingKeys_.clear();
		pendingKnob_ = 0;
		for (auto& p : prev_) std::fill(std::begin(p), std::end(p), 0);
		cpuBudget_ = 0;
		frames_ = 0;
	}

	// --- battery-backed RAM ---------------------------------------------------
	std::vector<uint8_t> batteryRam() const {
		std::vector<uint8_t> v;
		copyBatteryRam(v);
		return v;
	}
	/** The same, into `v` -- without allocating once `v` has held it before, so
	    the audio thread can keep the patch's copy current. */
	void copyBatteryRam(std::vector<uint8_t>& v) const {
		v.resize(BATTERY_BYTES);
		std::copy(ram_ + 0x4000, ram_ + 0x6000, v.begin());
		for (int b = 0; b < 4; b++) std::copy(bank_[b], bank_[b] + 0x2000, v.begin() + 0x2000 * (b + 1));
	}
	void setBatteryRam(const std::vector<uint8_t>& v) {
		if (v.size() != BATTERY_BYTES) return;
		std::copy(v.begin(), v.begin() + 0x2000, ram_ + 0x4000);
		for (int b = 0; b < 4; b++) std::copy(v.begin() + 0x2000 * (b + 1), v.begin() + 0x2000 * (b + 2), bank_[b]);
	}
	/** Wipe it: the firmware reinitialises and reloads its ROM presets on the next power-on. */
	void clearBatteryRam() {
		std::memset(ram_ + 0x4000, 0, 0x2000);
		std::memset(bank_, 0, sizeof bank_);
	}

	// --- front panel ------------------------------------------------------------
	/** A button press or release, as the front-panel board reports it. Queued
	    and paced, so a burst from the UI cannot overrun the ACIA. */
	void button(int n, bool down) { pendingKeys_.push_back(uint8_t((n & 0x7f) | (down ? 0x80 : 0))); }
	/** Turn the data-entry knob. Detents are paced as a hand would deliver them. */
	void knob(int detents) { pendingKnob_ += detents; }
	/** Footswitches are buttons 15 (left) and 7 (right). */
	void footswitch(int which, bool down) { button(which ? BTN_FOOT_R : BTN_FOOT_L, down); }
	/** CV pedal: 255 reads as "unplugged", 0 fully up. */
	void setPedal(uint8_t v) { pedal_ = v; }
	bool panelBusy() const { return !pendingKeys_.empty() || pendingKnob_ != 0 || !acia_.empty(); }
	const Display& display() const { return panel_; }

	// --- state --------------------------------------------------------------------
	uint8_t peek(uint16_t a) {
		if (a >= 0x6000 && a < 0x8000) return bank_[cpu_.port_out[0] & 3][a - 0x6000];
		return a < 0x8000 ? ram_[a] : os_[a - 0x8000];
	}
	Routing routing() {
		Routing r;
		r.sources = std::min(4, peek(cfg::SOURCES) + 1);
		r.abRoute = peek(cfg::AB_ROUTE) & 3;
		r.cdRoute = peek(cfg::CD_ROUTE) & 3;
		r.abToCd = std::min(2, int(peek(cfg::AB_TO_CD)));
		r.abAmount = std::min(99, int(peek(cfg::AB_AMOUNT)));
		r.cdAmount = std::min(99, int(peek(cfg::CD_AMOUNT)));
		r.abMono = peek(cfg::AB_MONO) & 1;
		r.cdMono = peek(cfg::CD_MONO) & 1;
		for (int u = 0; u < 4; u++) r.kill[u] = peek(uint16_t(cfg::KILL_A + u)) & 1;
		// The routing bytes double as output selects once the pair is split up.
		if (r.sources >= 3) { r.abOut = r.abRoute & 1; r.abRoute = 0; }
		if (r.sources >= 4) { r.cdOut = r.cdRoute & 1; r.cdRoute = 0; }
		return r;
	}
	int inputMux() const { return (cpu_.port_out[0] >> 2) & 3; }
	/** The OS version the firmware announced on its boot screen ("1.15"), or "" before it has. */
	const std::string& osVersion() const { return osVersion_; }
	uint16_t cpuPc() const { return cpu_.pc(); }
	uint64_t frames() const { return frames_; }
	const Esp& esp(int i) const { return esp_[i]; }
	/** Run the ESPs on MAME's execute_run rather than Esp.cpp's fast restatement (tests). */
	void useReferenceEsp(bool on) { for (auto& e : esp_) e.setReference(on); }

	// --- audio ----------------------------------------------------------------------
	/** One DSP frame at FRAME_RATE. `adc` are inputs 1-4; `dac` outputs 1-4;
	    `taps` each ESP's own output port, L/R: A.SER3, B.SER1, C.SER3, D.SER1. */
	void frame(const int16_t adc[4], int16_t dac[4], int16_t taps[8]) {
		const int mux = inputMux();
		int16_t cdL, cdR;
		if (mux == 0) { cdL = prev_[1][Esp::S1L]; cdR = prev_[1][Esp::S1R]; }
		else if (mux == 1) { cdL = adc[0]; cdR = adc[1]; }
		else { cdL = adc[2]; cdR = adc[3]; }
		const int16_t in[4][4] = {
			{ adc[0], adc[1], prev_[1][Esp::S3L], prev_[1][Esp::S3R] },
			{ adc[0], adc[1], prev_[0][Esp::S3L], prev_[0][Esp::S3R] },
			{ cdL, cdR, prev_[3][Esp::S3L], prev_[3][Esp::S3R] },
			{ cdL, cdR, prev_[2][Esp::S3L], prev_[2][Esp::S3R] },
		};
		for (int i = 0; i < 4; i++) {
			esp_[i].serWrite(Esp::S0L, in[i][0]); esp_[i].serWrite(Esp::S0R, in[i][1]);
			esp_[i].serWrite(Esp::S2L, in[i][2]); esp_[i].serWrite(Esp::S2R, in[i][3]);
		}
		// One program pass, interleaved with the host in slices so the firmware's
		// PC-window and Host-Access-OK polls see the ESPs move.
		for (int k = 0; k < SLICES; k++) {
			const int steps = STEPS * (k + 1) / SLICES - STEPS * k / SLICES;
			for (auto& e : esp_) e.run(steps);
			cpuBudget_ += E_CLOCK / FRAME_RATE / SLICES;
			const int whole = int(cpuBudget_);
			if (whole > 0) { runCpu(whole); cpuBudget_ -= whole; }
		}
		for (int i = 0; i < 4; i++)
			for (int c = 0; c < 8; c++) prev_[i][c] = esp_[i].serRead(c);
		if (dac) {
			dac[0] = prev_[1][Esp::S1L]; dac[1] = prev_[1][Esp::S1R];
			dac[2] = prev_[3][Esp::S1L]; dac[3] = prev_[3][Esp::S1R];
		}
		if (taps) {
			static const int port[4] = { Esp::S3L, Esp::S1L, Esp::S3L, Esp::S1L };
			for (int i = 0; i < 4; i++) { taps[2 * i] = prev_[i][port[i]]; taps[2 * i + 1] = prev_[i][port[i] + 1]; }
		}
		frames_++;
		servicePanel();
	}

	/** Run silently for `seconds` of machine time: power-on to the main screen is ~3 s. */
	void idle(double seconds) {
		const int16_t zero[4] = {};
		for (uint64_t n = uint64_t(seconds * FRAME_RATE); n--;) frame(zero, nullptr, nullptr);
	}

private:
	M6803 cpu_;
	Esp esp_[4];
	std::vector<uint8_t> os_, ucode_;
	uint8_t ram_[0x8000] = {};
	uint8_t bank_[4][0x2000] = {};
	uint8_t latch0600_ = 0;
	uint8_t pedal_ = 255;
	std::deque<uint8_t> acia_;
	std::deque<uint8_t> pendingKeys_;
	int pendingKnob_ = 0;
	uint64_t nextPanelEvent_ = 0;   // CPU cycle before which no new key/detent is delivered
	int16_t prev_[4][8] = {};
	double cpuBudget_ = 0;
	uint64_t frames_ = 0;
	Display panel_;
	int decodeArg_ = -1;            // command byte awaiting its argument
	int decodeSkip_ = 0;
	int cursor_ = 0;
	int field_[8] = {};
	bool blinking_ = false;
	std::string osVersion_;

	void runCpu(int cycles) {
		cpu_.port_in[1] = uint8_t((cpu_.port_in[1] & ~0x02) | (knobDir_ ? 0x02 : 0));
		cpu_.run(cycles);
	}
	bool knobDir_ = true;

	/** Deliver queued keys and detents at a human pace: 10 ms between key bytes,
	    30 ms between detents (the firmware debounces the knob over two timer ticks). */
	void servicePanel() {
		const uint64_t now = cpu_.total_cycles;
		if (now < nextPanelEvent_ || !acia_.empty()) return;
		if (!pendingKeys_.empty()) {
			acia_.push_back(pendingKeys_.front());
			pendingKeys_.pop_front();
			cpu_.setIrq1(true);
			nextPanelEvent_ = now + uint64_t(0.010 * E_CLOCK);
		}
		else if (pendingKnob_ != 0) {
			knobDir_ = pendingKnob_ > 0;
			cpu_.port_in[1] = uint8_t((cpu_.port_in[1] & ~0x02) | (knobDir_ ? 0x02 : 0));
			cpu_.inputCapture();
			pendingKnob_ += knobDir_ ? -1 : 1;
			nextPanelEvent_ = now + uint64_t(0.030 * E_CLOCK);
		}
	}

	// --- the bus ----------------------------------------------------------------------
	u8 read(u16 a) override {
		if (a >= 0x8000) return os_[a - 0x8000];
		if (a >= 0x6000) return bank_[cpu_.port_out[0] & 3][a - 0x6000];
		if (a >= 0x4000) return ram_[a];
		if (a >= 0x2000) return ucode_[(latch0600_ & 0x0f) * 0x2000 + (a - 0x2000)];
		if (a >= 0x0800 && a < 0x1000) return esp_[(a >> 9) & 3].hostRead(a & 0xff);
		if (a == 0x1800) return acia_.empty() ? 0x02 : 0x83;
		if (a == 0x1801) {
			if (acia_.empty()) return 0;
			const u8 v = acia_.front();
			acia_.pop_front();
			cpu_.setIrq1(!acia_.empty());
			return v;
		}
		if (a == 0x0400) return pedal_;
		return ram_[a];
	}

	void write(u16 a, u8 v) override {
		if (a >= 0x8000) return;
		if (a >= 0x6000) { bank_[cpu_.port_out[0] & 3][a - 0x6000] = v; return; }
		if (a >= 0x0800 && a < 0x1000) { esp_[(a >> 9) & 3].hostWrite(a & 0xff, v); return; }
		if (a >= 0x0600 && a < 0x0800) {
			// An ESP only stops at its END step; the real chip is ~80x faster than
			// the host, so it has halted before the next host access. Run it there
			// now, or the firmware's RAM clear (honoured only while halted) is lost.
			for (int i = 0; i < 4; i++) {
				const bool halt = (v >> (4 + i)) & 1;
				esp_[i].setHalt(halt);
				for (int n = 0; halt && !esp_[i].halted() && n < 200; n++) esp_[i].run(1);
			}
			latch0600_ = v;
			return;
		}
		if (a == 0x1801) { decode(v); return; }
		if (a == 0x1800) return;
		ram_[a] = v;
	}

	/** The boot screen reads "ENSONIQ * DP/4" over "OS Version  1.15"; the version
	    is not stored as text in the EPROM, so it is taken from there. */
	void findOsVersion() {
		const std::string lcd(panel_.lcd, sizeof panel_.lcd);
		const size_t at = lcd.find("Version");
		if (at == std::string::npos) return;
		size_t v = at + 7;
		while (v < lcd.size() && lcd[v] == ' ') v++;
		size_t e = v;
		while (e < lcd.size() && (std::isdigit((unsigned char) lcd[e]) || lcd[e] == '.')) e++;
		if (e - v >= 4) osVersion_ = lcd.substr(v, e - v);   // "1.15", once all of it has arrived
	}

	/** The front-panel board's command set, as far as the OS uses it:
	    $87 p   cursor to cell p (row 2 starts at $10)   $8C  clear
	    $88 n   remember the cursor as field n           $89 n  cursor to field n
	    $8D n / $8E n   LED n on / off (0-15 indicators, 16-31 digit segments)
	    $90 / $91   start / end a flashing field          $93 + 4 bytes  set-up
	    $86 and $8F take one argument and are not needed to draw the panel. */
	void decode(u8 b) {
		if (decodeSkip_ > 0) { decodeSkip_--; return; }
		if (decodeArg_ >= 0) {
			const int cmd = decodeArg_;
			decodeArg_ = -1;
			if (cmd == 0x87) cursor_ = b & 31;
			else if (cmd == 0x88) field_[b & 7] = cursor_;
			else if (cmd == 0x89) cursor_ = field_[b & 7];
			else if (cmd == 0x8d) panel_.leds |= 1u << (b & 31);
			else if (cmd == 0x8e) panel_.leds &= ~(1u << (b & 31));
			panel_.generation++;
			return;
		}
		switch (b) {
		case 0x86: case 0x87: case 0x88: case 0x89: case 0x8d: case 0x8e: case 0x8f:
			decodeArg_ = b; return;
		case 0x93: decodeSkip_ = 4; return;
		case 0x8c: panel_.clear(); cursor_ = 0; blinking_ = false; panel_.generation++; return;
		case 0x90: blinking_ = true; return;
		case 0x91: blinking_ = false; return;
		default: break;
		}
		// $0D is the one custom glyph the OS prints inside text: the feedback
		// symbol between two units ("pit/rev"), set as a slash as the manual does.
		if ((b >= 0x20 && b < 0x80) || b == 0x0d) {
			panel_.lcd[cursor_] = b == 0x0d ? '/' : char(b);
			panel_.blink[cursor_] = blinking_;
			cursor_ = (cursor_ + 1) & 31;
			panel_.generation++;
			if (osVersion_.empty()) findOsVersion();
		}
	}
};

} // namespace dp4

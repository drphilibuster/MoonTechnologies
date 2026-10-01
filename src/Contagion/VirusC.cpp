// Contagion -- the Virus C board. See VirusC.hpp for the map; VirusResearch/NOTES.md for how
// each fact was found. Compiled as C++17 against vendor/dsp56300 (GPLv3) and gearmulator's
// HD44780 model (vendor/gearmulator, GPLv3); the DSP wrapper follows gearmulator's
// virusLib::DspSingle for the A/B/C (memory sizes, peripherals, JIT setting).
#include "VirusC.hpp"
#include "Mcs515.hpp"

#include "dsp56kBase/logging.h"
#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/dspBootCode.h"
#include "dsp56kEmu/dspthread.h"
#include "dsp56kEmu/jit.h"
#include "dsp56kEmu/memory.h"
#include "dsp56kEmu/peripherals.h"
#include "dsp56kEmu/utils.h"

#include "../../vendor/gearmulator/hardwareLib/hd44780.h"
#include "../../vendor/gearmulator/hardwareLib/lcdfonts.h"

#include <algorithm>
#include <cstring>
#include <deque>
#include <functional>

namespace vc {

// --- the DSP56362 -----------------------------------------------------------------------------

/** One DSP56362 as the Virus A/B/C has it: 256K words, external memory from $20000, the
    56362's peripherals (ESAI, HI08), the JIT, its own thread once booted. */
struct Dsp56362 {
	static constexpr dsp56k::TWord MEMORY = 0x040000, EXTERNAL = 0x020000;
	std::vector<uint8_t> buffer;
	dsp56k::DefaultMemoryValidator validator;
	dsp56k::Peripherals56362 periphX;
	dsp56k::PeripheralsNop periphNop;
	dsp56k::Memory* memory = nullptr;
	dsp56k::DSP* dsp = nullptr;
	std::unique_ptr<dsp56k::DSPThread> thread;

	Dsp56362() {
		const size_t need = dsp56k::alignedSize<dsp56k::DSP>() + dsp56k::alignedSize<dsp56k::Memory>()
			+ dsp56k::Memory::calcMemSize(MEMORY, EXTERNAL) * sizeof(uint32_t);
		buffer.resize(dsp56k::alignedSize(need));
		uint8_t* buf = dsp56k::alignedAddress(buffer.data());
		uint8_t* memClass = buf + dsp56k::alignedSize<dsp56k::DSP>();
		uint8_t* memSpace = memClass + dsp56k::alignedSize<dsp56k::Memory>();
		memory = new (memClass) dsp56k::Memory(validator, MEMORY, MEMORY, EXTERNAL, reinterpret_cast<dsp56k::TWord*>(memSpace));
		dsp = new (buf) dsp56k::DSP(*memory, &periphX, &periphNop);
		auto conf = dsp->getJit().getConfig();
		conf.aguSupportBitreverse = false;   // as gearmulator configures the A/B/C
		dsp->getJit().setConfig(conf);
	}
	~Dsp56362() {
		thread.reset();
		dsp->~DSP();
		memory->~Memory();
	}
	void start() {
		thread.reset(new dsp56k::DSPThread(*dsp, "Contagion"));
		thread->setLogToStdout(false);
		thread->setLogToDebug(false);
	}
	dsp56k::HDI08& hi08() { return periphX.getHDI08(); }
	dsp56k::Audio& audio() { return periphX.getEsai(); }
};

// --- the HI08's host side ---------------------------------------------------------------------

/** The host side of the DSP56362's HI08, as the 80C515 sees it at $0400-$0407 (DSP56300
    Family Manual, HI08 host-side registers): ICR, CVR, ISR, IVR, -, and three data bytes in
    the order HLEND (ICR bit 5) selects. A word is complete when the byte at $7 is written
    and consumed when the byte at $7 is read -- the last of the three in either order, which
    is how the 80C515 walks them. gearmulator's mc68k::Hdi08 consumes on $5 in little-endian
    mode, which suits the 68k boards' access order and tears words on this one. */
struct HostPort {
	enum { RXDF = 1, TXDE = 2, TRDY = 4 };
	uint8_t icr = 0, cvr = 0x12, ivr = 0x0f;
	bool rxFull = false;
	uint32_t rxWord = 0;
	uint8_t tx[3] = {};
	std::function<bool(uint32_t&)> pull;      // the DSP's next word for the host, if any
	std::function<void(uint32_t)> push;       // a word for the DSP
	std::function<void(uint8_t)> command;     // host command: vector address
	std::function<uint8_t()> hf23;            // DSP-side HF2/HF3, in ISR position

	bool le() const { return icr & 0x20; }
	void fill() { if (!rxFull && pull(rxWord)) rxFull = true; }
	/** /HREQ asserted: RXDF with RREQ, or TXDE with TREQ (ICR bits 0, 1). */
	bool hreq() { fill(); return ((icr & 1) && rxFull) || (icr & 2); }
	uint8_t read(int r) {
		switch (r) {
		case 0: return icr;
		case 1: return cvr;
		case 2: fill(); return uint8_t((rxFull ? RXDF : 0) | TXDE | TRDY | hf23());
		case 3: return ivr;
		case 5: case 6: case 7: {
			fill();
			const uint8_t v = uint8_t(rxWord >> (8 * (le() ? r - 5 : 7 - r)));
			if (r == 7) rxFull = false;
			return v;
		}
		default: return 0;
		}
	}
	void write(int r, uint8_t v) {
		switch (r) {
		case 0: icr = uint8_t(v & 0x7f); return;          // INIT completes at once
		case 1:
			cvr = uint8_t(v & 0x7f);
			if (v & 0x80) command(uint8_t((v & 0x7f) << 1));
			return;
		case 3: ivr = v; return;
		case 5: case 6: case 7:
			tx[le() ? r - 5 : 7 - r] = v;
			if (r == 7) push(uint32_t(tx[2]) << 16 | uint32_t(tx[1]) << 8 | tx[0]);
			return;
		default: return;
		}
	}
};

// --- the board ----------------------------------------------------------------------------------

struct VirusC::Impl : Mcs515Bus {
	static constexpr double UC_CYCLES_PER_SECOND = 1e6;   // 12 MHz / 12

	Mcs515 uc;
	std::vector<uint8_t> flash = std::vector<uint8_t>(FLASH_SIZE, 0xff);
	std::vector<uint8_t> ram = std::vector<uint8_t>(GLOBAL_RAM, 0);
	std::vector<uint8_t> bankRam = std::vector<uint8_t>(BANK_RAM, 0xff);
	HostPort hdi;
	std::unique_ptr<Dsp56362> dsp;
	std::unique_ptr<dsp56k::DspBoot> loader;
	bool booted = false;
	long wordsIn = 0;
	uint32_t lastHf01 = 0;
	double owed = 0;
	float silence[MAX_BLOCK] = {}, unused[MAX_BLOCK] = {};   // the DSP's channels nothing is patched to

	Impl() {
		// The emulator's own log (register writes, MIPS) has no place in Rack's.
		Logging::setLogFunc([](const std::string&) {});
		hdi.push = [this](uint32_t w) {
			if (!booted) {
				// The DSP56362's bootstrap through the HI08: length, address, code.
				if (loader->hdiWriteTX(w)) { booted = true; dsp->start(); }
				return;
			}
			wordsIn++;
			dsp->hi08().writeRX(&w, 1);
		};
		hdi.pull = [this](uint32_t& w) {
			if (!booted) return false;
			auto& h = dsp->hi08();
			if (!h.hasTX()) h.injectTXInterrupt();          // the DSP's host-transmit interrupt
			if (!h.hasTX()) return false;
			w = h.readTX();
			return true;
		};
		hdi.command = [this](uint8_t vector) { if (booted) dsp->dsp->injectExternalInterrupt(vector); };
		hdi.hf23 = [this]() -> uint8_t { return booted ? uint8_t(dsp->hi08().readControlRegister() & 0x18) : 0; };
		uc.bus = this;
	}

	// --- memory ----------------------------------------------------------------------------
	static bool isRamBank(int b) { return b >= 8 && b <= 0xb; }
	int bankNo() const { return uc.latch(5) >> 4; }
	uint8_t window(uint16_t a) const {
		const int b = bankNo();
		if (isRamBank(b)) return bankRam[size_t(b - 8) * 0x8000 + (a & 0x7fff)];
		return flash[size_t(b) * 0x8000 + (a & 0x7fff)];
	}
	/** The HI08 answers $0400-$0407 only with P3.3 low; with it high those are RAM (the
	    firmware's "Initialize Global Memory" writes straight through them). */
	bool hostSelected(uint16_t a) const { return a >= 0x0400 && a < 0x0408 && !(uc.latch(3) & 0x08); }

	uint8_t code(uint16_t a) override { return a < 0x8000 ? flash[a] : window(a); }
	uint8_t xread(uint16_t a) override {
		if (hostSelected(a)) return hdi.read(a - 0x0400);
		return a < 0x8000 ? ram[a] : window(a);
	}
	void xwrite(uint16_t a, uint8_t v) override {
		if (hostSelected(a)) { hdi.write(a - 0x0400, v); return; }
		if (a < 0x8000) { ram[a] = v; return; }
		const int b = bankNo();
		if (isRamBank(b)) bankRam[size_t(b - 8) * 0x8000 + (a & 0x7fff)] = v;
	}

	// --- the LCD on port 1 ---------------------------------------------------------------------
	// The controller's execution time is what makes the firmware's init work: it sends 0x28
	// twice from power-on (8-bit mode), and the second nibble of the first lands while the
	// function set is still executing, so it is ignored and the nibble phase comes out right.
	// Busy 37 us per instruction, 1.52 ms for clear and home (HD44780 datasheet, 270 kHz).
	hwLib::Hd44780 lcdc{16, 2};
	bool lcd8bit = true, lcdHalf = false, lcdE = false;
	uint8_t lcdHigh = 0, lcdReadByte = 0, lcdReadNibble = 0;
	uint64_t lcdBusyUntil = 0;
	bool lcdBusy() const { return uc.cycles < lcdBusyUntil; }
	void lcdWrite(bool rs, uint8_t b) {
		lcdc.exec(rs, false, b);
		if (!rs && (b & 0xe0) == 0x20) lcd8bit = b & 0x10;   // function set: DL
		lcdBusyUntil = uc.cycles + ((!rs && b <= 0x03) ? 1520 : 37);
	}
	void lcdPort(uint8_t p1) {
		const bool e = p1 & 0x20, rw = p1 & 0x40, rs = p1 & 0x80;
		const uint8_t nib = uint8_t(p1 >> 1 & 0x0f);
		if (e && !lcdE && rw) {
			if (lcd8bit || !lcdHalf) {
				const auto r = lcdc.exec(rs, true, 0);
				lcdReadByte = uint8_t((r ? *r : 0) | ((!rs && lcdBusy()) ? 0x80 : 0));
				lcdReadNibble = uint8_t(lcdReadByte >> 4);
				if (!lcd8bit) lcdHalf = true;
			}
			else { lcdReadNibble = uint8_t(lcdReadByte & 0x0f); lcdHalf = false; }
		}
		if (!e && lcdE && !rw && !lcdBusy()) {
			if (lcd8bit) { lcdWrite(rs, uint8_t(nib << 4)); lcdHalf = false; }
			else if (!lcdHalf) { lcdHigh = nib; lcdHalf = true; }
			else { lcdHalf = false; lcdWrite(rs, uint8_t(lcdHigh << 4 | nib)); }
		}
		lcdE = e;
	}

	// --- buttons and LEDs on ports 4/5 ----------------------------------------------------------
	uint8_t pressed[5] = {};
	uint8_t ledA = 0, ledB = 0, lastP4 = 0xff, lastP5 = 0xff;
	uint64_t ledSince = 0;
	// Time each LED is lit, and time each group is driven at all: the eye sees an LED's
	// share of its own group's slot, so a steady one is full and a flashing one is not.
	double ledOn[7][14] = {}, groupOn[7] = {};
	void ledAccumulate() {
		const double dt = double(uc.cycles - ledSince);
		ledSince = uc.cycles;
		const int g = uc.latch(5) & 7;
		if (g >= 7) return;
		groupOn[g] += dt;
		for (int b = 0; b < 7; b++) {
			if (ledA >> b & 1) ledOn[g][b] += dt;
			if (ledB >> b & 1) ledOn[g][7 + b] += dt;
		}
	}
	void portOut(int p, uint8_t v) override {
		if (p == 1) { lcdPort(v); return; }
		if (p != 4 && p != 5) return;
		ledAccumulate();
		if (p == 4) {
			if ((lastP4 & 0x80) && !(v & 0x80)) ledA = uint8_t(lastP4 & 0x7f);
			lastP4 = v;
		}
		else {
			if ((lastP5 & 0x08) && !(v & 0x08)) ledB = uint8_t(uc.latch(4) & 0x7f);
			lastP5 = v;
		}
	}

	// --- pots, MIDI, pins --------------------------------------------------------------------------
	uint8_t pots[32];
	uint8_t analog(int ch) override { return pots[(uc.latch(3) >> 4 & 3) * 8 + (ch & 7)]; }

	std::deque<uint8_t> midiQ;
	bool midiBusy = false;
	uint8_t midiByte = 0;
	uint64_t midiStart = 0, lineFree = 0;
	bool rxd(uint64_t cycle) override {
		// 31,250 baud = 32 machine cycles a bit.
		for (;;) {
			if (!midiBusy) {
				if (midiQ.empty()) return true;
				midiByte = midiQ.front();
				midiQ.pop_front();
				midiStart = std::max(cycle, lineFree);
				midiBusy = true;
			}
			if (cycle < midiStart) return true;
			const uint64_t bit = (cycle - midiStart) / 32;
			if (bit >= 10) { midiBusy = false; lineFree = midiStart + 320; continue; }
			if (bit == 0) return false;
			if (bit == 9) return true;
			return midiByte >> (bit - 1) & 1;
		}
	}
	uint8_t pins(int p) override {
		if (p == 1 && (uc.latch(1) & 0x60) == 0x60) return uint8_t(0xe1 | lcdReadNibble << 1);   // LCD read
		if (p == 4) {
			const int row = uc.latch(5) & 7;
			return row < 5 ? uint8_t(~pressed[row] | 0x80) : 0xff;
		}
		if (p == 3) {
			uint8_t v = 0xff;
			if (!rxd(uc.cycles)) v &= 0xfe;
			if (hdi.hreq()) v &= 0xfb;                    // P3.2 = /HREQ
			return v;
		}
		return 0xff;
	}

	void runUc(long cycles) {
		const uint64_t end = uc.cycles + uint64_t(cycles);
		while (uc.cycles < end) {
			uc.step();
			if (booted) {
				const uint32_t hf01 = hdi.icr & 0x18;    // host flags HF0/HF1 to the DSP
				if (hf01 != lastHf01) { lastHf01 = hf01; dsp->hi08().setPendingHostFlags01(hf01); }
			}
		}
	}
};

// --- the public face --------------------------------------------------------------------------

VirusC::VirusC() : impl(new Impl) { std::memset(impl->pots, 0x80, sizeof(impl->pots)); }
VirusC::~VirusC() = default;

std::string VirusC::load(const std::vector<uint8_t>& image) {
	if (image.size() != FLASH_SIZE) return "NOT A 512 KB IMAGE";
	// The 80C515's reset vector (LJMP) and the five DSP chunks at $18000 (ids 4..0).
	if (image[0] != 0x02 || image[0x18000] != 0x04 || image[0x20000] != 0x03) return "NOT A VIRUS A/B/C OS";
	impl->flash = image;
	impl->bankRam.assign(image.begin() + 0x40000, image.begin() + 0x60000);
	return "";
}

void VirusC::setRam(const std::vector<uint8_t>& global, const std::vector<uint8_t>& banks) {
	if (global.size() == GLOBAL_RAM) impl->ram = global;
	if (banks.size() == BANK_RAM) impl->bankRam = banks;
}

void VirusC::copyRam(std::vector<uint8_t>& global, std::vector<uint8_t>& banks) const {
	global = impl->ram;
	banks = impl->bankRam;
}

bool VirusC::boot() {
	Impl& m = *impl;
	m.dsp.reset(new Dsp56362);
	m.loader.reset(new dsp56k::DspBoot(*m.dsp->dsp));
	m.booted = false;
	m.uc.reset();
	// Until the DSP runs the 80C515 runs alone; then until the audio port is live, with
	// silence offered in. Ten seconds of machine time is far more than a unit needs.
	while (!m.booted && m.uc.cycles < uint64_t(10 * Impl::UC_CYCLES_PER_SECOND)) m.runUc(1000);
	if (!m.booted) return false;
	auto& audio = m.dsp->audio();
	const uint64_t c0 = m.uc.cycles;
	while (audio.getAudioOutputs().empty() && m.uc.cycles - c0 < uint64_t(20 * Impl::UC_CYCLES_PER_SECOND)) {
		m.runUc(500);
		if (audio.getAudioInputs().size() < 8) audio.writeEmptyAudioIn(8);
	}
	return !audio.getAudioOutputs().empty();
}

void VirusC::process(const float* inL, const float* inR, float* const out[6], int frames) {
	Impl& m = *impl;
	m.owed += Impl::UC_CYCLES_PER_SECOND / SAMPLE_RATE * frames;
	const long c = long(m.owed);
	m.owed -= double(c);
	m.runUc(c);
	const float* z = m.silence;
	float* d = m.unused;
	const float* ins[8] = { inL, inR, z, z, z, z, z, z };
	float* outs[12] = { out[0], out[1], out[2], out[3], out[4], out[5], d, d, d, d, d, d };
	m.dsp->audio().processAudioInterleaved(ins, outs, uint32_t(frames));
}

void VirusC::midi(uint8_t b) { if (impl->midiQ.size() < 4096) impl->midiQ.push_back(b); }
void VirusC::setPot(int i, uint8_t v) { if (i >= 0 && i < 32) impl->pots[i] = v; }
void VirusC::setButton(int row, int col, bool down) {
	if (row < 0 || row >= 5 || col < 0 || col >= 7) return;
	if (down) impl->pressed[row] |= uint8_t(1 << col);
	else impl->pressed[row] &= uint8_t(~(1 << col));
}

void VirusC::lcd(uint8_t chars[32], uint8_t cgram[64]) const {
	for (uint32_t l = 0; l < 2; l++)
		for (uint32_t c = 0; c < 16; c++) chars[l * 16 + c] = impl->lcdc.getVisibleCharacter(l, c);
	const auto& cg = impl->lcdc.getCgRam();
	std::copy(cg.begin(), cg.end(), cgram);
}

std::string VirusC::lcdText() const {
	uint8_t ch[32], cg[64];
	lcd(ch, cg);
	std::string t;
	for (int i = 0; i < 32; i++) {
		t += (ch[i] >= 32 && ch[i] < 127) ? char(ch[i]) : '.';
		if (i == 15) t += '|';
	}
	return t;
}

void VirusC::leds(float bright[7][14]) {
	Impl& m = *impl;
	m.ledAccumulate();
	for (int g = 0; g < 7; g++) {
		for (int b = 0; b < 14; b++)
			bright[g][b] = m.groupOn[g] > 0 ? float(std::min(1.0, m.ledOn[g][b] / m.groupOn[g])) : 0.f;
		for (double& t : m.ledOn[g]) t = 0;
		m.groupOn[g] = 0;
	}
}

void VirusC::rateLeds(float out[2]) const {
	// Not in the 80C515's multiplex: the DSP drives the two RATE LEDs itself, as PWM from
	// its timers -- timer 2 for LFO 1, timer 1 for LFO 2/3 (gearmulator's frontpanelState,
	// for the A/B/C). Brightness is the compare value's place between load and full scale.
	const Impl& m = *impl;
	if (!m.dsp) { out[0] = out[1] = 0.f; return; }
	const auto& t = m.dsp->periphX.getTimers();
	const int timer[2] = { 2, 1 };
	for (int i = 0; i < 2; i++) {
		const double load = t.readTLR(timer[i]), range = double(0xffffff) - load;
		out[i] = range > 0 ? float(std::max(0.0, std::min(1.0, (double(t.readTCPR(timer[i])) - load) / range))) : 0.f;
	}
}

long VirusC::dspWordsIn() const { return impl->wordsIn; }

const uint8_t* lcdGlyph(uint8_t code) { return hwLib::getCharacterData(code); }

} // namespace vc

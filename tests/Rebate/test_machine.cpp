// Rebate's MIDIverb, run without Rack against the real firmware.
//
// The ROMs are Alesis's and are never in this repository: point MIDIVERB_ROMS at
// a directory holding the CPU image (MVOP, 8 KB) and a DSP image (MVOBJ or
// MIDIFEX, 16 KB). Without it every test prints SKIP and passes.
#include "../../src/Rebate/Machine.hpp"
#include "../../src/Rebate/Analog.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace mv;

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); failures++; } } while (0)

static std::vector<uint8_t> readFile(const std::string& p) {
	std::ifstream f(p, std::ios::binary);
	return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}

struct Roms { std::vector<uint8_t> cpu, verb, fex; };

static bool findRoms(Roms& r) {
	const char* dir = std::getenv("MIDIVERB_ROMS");
	if (!dir) return false;
	std::vector<std::string> todo{ dir };
	while (!todo.empty()) {
		const std::string d = todo.back();
		todo.pop_back();
		DIR* h = opendir(d.c_str());
		if (!h) continue;
		while (dirent* e = readdir(h)) {
			const std::string n = e->d_name;
			if (n == "." || n == "..") continue;
			const std::string p = d + "/" + n;
			if (e->d_type == DT_DIR) { todo.push_back(p); continue; }
			const std::vector<uint8_t> b = readFile(p);
			const std::string name = romName(crc32(b));
			if (name == "MVOP 4-7-86") r.cpu = b;
			else if (name.find("MIDIverb") == 0) r.verb = b;
			else if (name == "MIDIFEX 7-17-86") r.fex = b;
		}
		closedir(h);
	}
	return !r.cpu.empty() && !r.verb.empty();
}

/** A digit's segments back to a character. */
static char glyph(uint8_t s) {
	static const uint8_t seg[10] = { 0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f };
	if (!s) return ' ';
	if (s == 0x40) return '-';
	for (int i = 0; i < 10; i++) if (seg[i] == s) return char('0' + i);
	if (s == 0x7c) return '6';
	if (s == 0x27) return '7';
	if (s == 0x67) return '9';
	return '?';
}

static std::string display(const Machine& m) { return std::string{ glyph(m.segments(0)), glyph(m.segments(1)) }; }

static void run(Machine& m, double seconds, uint16_t adc = 1) {
	int16_t l, r;
	const long n = long(seconds * Machine::SAMPLE_RATE);
	for (long i = 0; i < n; i++) m.sample(adc, r, l);
}

// --- 1. the firmware: boot, buttons, MIDI ---------------------------------------------
static void testFirmware(const Roms& roms) {
	Machine m;
	CHECK(m.load(roms.cpu, roms.verb).empty(), "load");
	m.powerOn();
	run(m, 1.5);
	std::printf("  boot: display \"%s\", DSP program %d\n", display(m).c_str(), m.dspProgram());
	// The firmware starts on program 21 (shown one-based as 22).
	CHECK(display(m) == "22", "display after boot is \"%s\"", display(m).c_str());
	CHECK(m.dspProgram() == 21, "program after boot is %d", m.dspProgram());

	// UP: the DSP goes silent (program 63) while the change settles, then moves on.
	m.button(Machine::UP, true);
	run(m, 0.03);
	m.button(Machine::UP, false);
	run(m, 0.02);
	CHECK(m.dspProgram() == 63, "program during the change is %d (want the silent 63)", m.dspProgram());
	run(m, 0.3);
	CHECK(display(m) == "23" && m.dspProgram() == 22, "after UP: \"%s\", program %d", display(m).c_str(), m.dspProgram());

	// Holding DOWN auto-repeats.
	m.button(Machine::DOWN, true);
	run(m, 1.0);
	m.button(Machine::DOWN, false);
	run(m, 0.3);
	std::printf("  DOWN held 1 s: \"%s\", program %d\n", display(m).c_str(), m.dspProgram());
	CHECK(m.dspProgram() < 21, "DOWN held did not repeat (program %d)", m.dspProgram());

	// MIDI program change on channel 1 (the default) selects; channel 2 does not.
	m.midi(0xc0); m.midi(4);
	run(m, 0.3);
	CHECK(display(m) == "05" || display(m) == " 5", "after PC 4: \"%s\"", display(m).c_str());
	CHECK(m.dspProgram() == 4, "after PC 4 the program is %d", m.dspProgram());
	m.midi(0xc1); m.midi(9);
	run(m, 0.3);
	CHECK(m.dspProgram() == 4, "a channel-2 PC moved the program to %d", m.dspProgram());

	// DEFEAT mutes (program 63), and again restores.
	m.button(Machine::DEFEAT, true); run(m, 0.05); m.button(Machine::DEFEAT, false); run(m, 0.3);
	CHECK(m.dspProgram() == 63, "DEFEAT: program %d", m.dspProgram());
	std::printf("  defeat: display \"%s\"\n", display(m).c_str());
	m.button(Machine::DEFEAT, true); run(m, 0.05); m.button(Machine::DEFEAT, false); run(m, 0.3);
	CHECK(m.dspProgram() == 4, "DEFEAT again: program %d", m.dspProgram());

	// A patch's settings, played back into a unit fresh from power-on.
	for (int defeat = 0; defeat < 2; defeat++) {
		Machine::Settings want;
		want.program = 40; want.channel = 3; want.defeat = defeat;
		Machine r;
		r.load(roms.cpu, roms.verb);
		r.powerOn();
		r.restore(want);
		run(r, 4.0);
		const Machine::Settings got = r.settings();
		std::printf("  restore: program %d channel %d defeat %d -> %d %d %d, display \"%s\", DSP %d\n", want.program,
			want.channel, want.defeat, got.program, got.channel, got.defeat, display(r).c_str(), r.dspProgram());
		CHECK(!r.replaying() && got.program == 40 && got.channel == 3 && got.defeat == bool(defeat), "restore");
		CHECK(r.dspProgram() == (defeat ? 63 : 40), "restored DSP program %d", r.dspProgram());
	}

	CHECK(m.cpu.badIndirect == 0, "%ld indirect accesses past 128 bytes", m.cpu.badIndirect);
	CHECK(m.cpu.unsupported == 0, "%ld unsupported serial events", m.cpu.unsupported);
	CHECK(m.conflicts == 0 && m.floating == 0, "DSP bus: %ld conflicts, %ld floating", m.conflicts, m.floating);
}

// --- 2. the DSP against MAME's own loop ----------------------------------------------------
/** MAME's midiverb_dsp_device::sound_stream_update, transcribed: the reference. */
struct MameDsp {
	const std::vector<uint8_t>* rom;
	uint8_t program = 0;
	uint16_t accum = 0, reg = 0, ramOffset = 0;
	std::vector<uint16_t> ram = std::vector<uint16_t>(0x4000, 0);
	void sample(uint16_t adc, int16_t& left, int16_t& right) {
		const uint16_t base = uint16_t(program << 8);
		uint16_t addr = uint16_t(base + 2 * 126);
		for (int pc = 0; pc <= 0x7f; pc++) {
			const uint8_t op = (*rom)[addr + 1] >> 6;
			addr = uint16_t(base + 2 * ((pc + 0x7f) & 0x7f));
			const uint16_t delta = uint16_t(((*rom)[addr + 1] & 0x3f) << 8 | (*rom)[addr]);
			const bool rc0 = pc == 0, ldDac = pc == 0x60 || pc == 0x70, ldDsp = !rc0 && !ldDac;
			const bool dramW = (op & 2) || rc0;
			uint16_t bus = 0;
			if (rc0) bus = adc;
			if (op == 2) bus = reg;
			if (op == 3) bus = uint16_t(~reg);
			if (!dramW) bus = ram[ramOffset];
			if (dramW) ram[ramOffset] = bus;
			if (ldDac) (pc == 0x70 ? left : right) = int16_t(bus);
			if (op & 1) accum = 0;
			if (ldDsp) { const uint16_t sg = bus >> 15; accum = uint16_t(accum + ((sg << 15) | (bus >> 1)) + sg); reg = accum; }
			ramOffset = uint16_t((ramOffset + delta) & 0x3fff);
		}
	}
};

static void testDsp(const Roms& roms, const std::vector<uint8_t>& image, const char* name) {
	Machine m;
	m.load(roms.cpu, image);
	m.powerOn();
	std::vector<uint8_t> rom(image.end() - 0x4000, image.end());
	int bad = 0;
	uint32_t seed = 1;
	for (int p = 0; p < 64; p++) {
		MameDsp ref;
		ref.rom = &rom;
		ref.program = uint8_t(p);
		m.dspReset(uint8_t(p));
		const long c0 = m.conflicts + m.floating;
		for (int n = 0; n < 4000; n++) {
			seed = seed * 1664525u + 1013904223u;
			// A burst of noise, then silence: the tail exercises the feedback paths.
			const uint16_t adc = n < 600 ? uint16_t((int16_t(seed >> 16) >> 4) | 1) : 1;
			int16_t rl, rr, ml, mr;
			ref.sample(adc, rl, rr);
			m.dspSample(uint8_t(p), adc, mr, ml);
			if (rl != ml || rr != mr) { bad++; break; }
		}
		CHECK(m.conflicts + m.floating == c0, "%s program %d: microcode bus errors", name, p);
	}
	std::printf("  %s: %d of 64 programs differ from MAME's loop\n", name, bad);
	CHECK(bad == 0, "%s: %d programs differ", name, bad);
}

// --- 3. the analog chain against the schematic's own annotations ---------------------------
static double toneGain(double hz, int which) {
	Analog a;
	double peakIn = 0, peakOut = 0;
	const int n = int(Analog::FS * 0.2);
	for (int i = 0; i < n; i++) {
		const double x = 0.01 * std::sin(2 * M_PI * hz * i / Analog::FS);
		double dry[2];
		double y = 0;
		if (which == 0) y = a.input(x, x, dry);
		else y = a.outputTick(0, x);
		if (i > n / 2) { peakIn = std::max(peakIn, std::fabs(x)); peakOut = std::max(peakOut, std::fabs(y)); }
	}
	return peakOut / peakIn;
}

static void testAnalog() {
	// Input side, both inputs driven: the x5.17 stage, then three Sallen-Keys that
	// MAME sums up as "a cutoff of ~12 kHz and a boost peaking at 9.7 kHz".
	const double g1k = toneGain(1000, 0);
	double fPeak = 0, gPeak = 0, f3 = 0;
	for (double f = 2000; f < 20000; f += 50) {
		const double g = toneGain(f, 0);
		if (g > gPeak) { gPeak = g; fPeak = f; }
		if (!f3 && f > fPeak && g < g1k / std::sqrt(2.0)) f3 = f;
	}
	std::printf("  input: x%.2f at 1 kHz, peak %+.1f dB at %.0f Hz, -3 dB at %.0f Hz\n",
		g1k, 20 * std::log10(gPeak / g1k), fPeak, f3);
	// x5.17, and the resonant stages are already a little above unity at 1 kHz.
	CHECK(g1k > 5.17 && g1k < 5.45, "input gain at 1 kHz is %.2f", g1k);
	CHECK(fPeak > 8500 && fPeak < 11000, "input peak at %.0f Hz", fPeak);
	CHECK(f3 > 11000 && f3 < 14000, "input -3 dB at %.0f Hz", f3);

	// Output network: unity at DC, the RC and the SK as one third-order low-pass.
	const double o1k = toneGain(1000, 1), o10k = toneGain(10000, 1), o20k = toneGain(20000, 1);
	std::printf("  output: %.3f at 1 kHz, %.2f at 10 kHz, %.2f at 20 kHz\n", o1k, o10k, o20k);
	CHECK(std::fabs(o1k - 1) < 0.02, "output gain at 1 kHz is %.3f", o1k);
	CHECK(o10k > 0.5 && o10k < 1.0 && o20k < 0.35, "output roll-off %.2f / %.2f", o10k, o20k);

	// The clamp: driven hard, SK3's positive peaks stop near the CD4053's supply
	// plus a diode drop; the negative ones are left to the op-amp.
	Analog a;
	double hi = 0, lo = 0;
	for (int i = 0; i < int(Analog::FS * 0.1); i++) {
		const double x = 1.6 * std::sin(2 * M_PI * 1000 * i / Analog::FS);
		double dry[2];
		const double y = a.input(x, x, dry);
		hi = std::max(hi, y); lo = std::min(lo, y);
	}
	std::printf("  clamp: SK3 swings %+.2f / %+.2f V for 8.3 V peaks in\n", hi, lo);
	CHECK(hi > 5.2 && hi < 6.2, "positive clamp at %.2f V", hi);
	CHECK(lo < -7.5, "negative side clamped too (%.2f V)", lo);
}

// --- 4. the whole board: dry, wet, determinism, cost -----------------------------------------
struct Board {
	Machine m;
	Analog a;
	explicit Board(const Roms& r) { m.load(r.cpu, r.verb); m.powerOn(); }
	/** n DSP samples of a stereo input function, circuit volts; returns the outputs. */
	template <typename F> std::vector<float> run(long n, F in) {
		std::vector<float> out;
		out.reserve(size_t(n) * Analog::OS * 2);
		float x[Analog::OS][2], y[Analog::OS][2];
		long t = 0;
		for (long i = 0; i < n; i++) {
			for (int k = 0; k < Analog::OS; k++, t++) { x[k][0] = in(t, 0); x[k][1] = in(t, 1); }
			a.run(m, x, y);
			for (int k = 0; k < Analog::OS; k++) { out.push_back(y[k][0]); out.push_back(y[k][1]); }
		}
		return out;
	}
};

static void testBoard(const Roms& roms) {
	const long second = long(Machine::SAMPLE_RATE);
	auto tone = [](long t, int) { return float(0.5 * std::sin(2 * M_PI * 440 * t / Analog::FS)); };

	// Fully dry: the input buffer's x5.17 and the 2.4k/560R divider, nothing else.
	{
		Board b(roms);
		b.a.mix = 0.f;
		const std::vector<float> o = b.run(second, tone);
		float pk = 0;
		for (size_t i = o.size() / 2; i < o.size(); i += 2) pk = std::max(pk, std::fabs(o[i]));
		std::printf("  dry: 0.5 V in, %.3f V out (x%.3f)\n", pk, pk / 0.5);
		CHECK(std::fabs(pk / 0.5 - 5.1667 * 560 / 2960) < 0.01, "dry gain %.3f", pk / 0.5);
	}
	// Fully wet, after boot, on the firmware's program: a tone burst leaves a tail.
	std::vector<float> first;
	for (int pass = 0; pass < 2; pass++) {
		Board b(roms);
		b.run(long(1.5 * second), [](long, int) { return 0.f; });
		const std::vector<float> o = b.run(3 * second, [&](long t, int c) {
			return t < Analog::FS / 5 ? tone(t, c) : 0.f;
		});
		double tail = 0;
		for (size_t i = size_t(Analog::FS * 2 * 0.5); i < size_t(Analog::FS * 2 * 1.0); i++) tail += double(o[i]) * o[i];
		tail = std::sqrt(tail / (Analog::FS * 2 * 0.5));
		if (pass == 0) {
			std::printf("  wet, program %d: tail RMS %.4f V from 0.3 to 0.8 s after the burst\n", b.m.dspProgram(), tail);
			CHECK(tail > 0.005, "no reverb tail (%.5f)", tail);
			first = o;
		}
		else CHECK(o == first, "two runs from power-on differ");
	}
	// Cost: one second of audio, wet, on the reverb.
	{
		Board b(roms);
		const auto t0 = std::chrono::steady_clock::now();
		b.run(second, tone);
		const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
		std::printf("  cost: %.1f%% of a core (machine + analog at %.0f Hz)\n", 100 * s, Analog::FS);
	}
}

// --- 5. the channel stepper: CHANNEL held, UP or DOWN pressed once, for each click ---------------
static void testChannelStepper(const Roms& roms) {
	Machine m;
	CHECK(m.load(roms.cpu, roms.verb).empty(), "load");
	m.powerOn();
	run(m, 1.5);
	auto settle = [&]() { for (int i = 0; i < 40 && m.replaying(); i++) run(m, 0.1); run(m, 0.3); };
	CHECK(m.settings().channel == 0, "a fresh unit is on channel 1");
	const int prog = m.settings().program;
	// Three clicks up, queued together, play out in order.
	m.stepChannel(+1); m.stepChannel(+1); m.stepChannel(+1);
	settle();
	std::printf("  three clicks up: channel %d, display \"%s\"\n", m.settings().channel + 1, display(m).c_str());
	CHECK(m.settings().channel == 3, "three clicks up gave channel index %d", m.settings().channel);
	// One back down.
	m.stepChannel(-1);
	settle();
	CHECK(m.settings().channel == 2, "a click down gave channel index %d", m.settings().channel);
	// The program is not touched: only CHANNEL was held when UP/DOWN were pressed.
	CHECK(m.settings().program == prog, "the program moved from %d to %d", prog, m.settings().program);
	// The new channel is the one the unit listens on: a program change on it lands, on the old one it does not.
	m.midi(0xc2); m.midi(9);
	run(m, 0.4);
	CHECK(m.dspProgram() == 9, "a program change on the new channel did not land (program %d)", m.dspProgram());
	m.midi(0xc0); m.midi(30);
	run(m, 0.4);
	CHECK(m.dspProgram() == 9, "a program change on the old channel moved the program to %d", m.dspProgram());
	// The ends: DOWN from channel 1, UP from channel 16.
	Machine e;
	e.load(roms.cpu, roms.verb);
	e.powerOn();
	run(e, 1.5);
	e.stepChannel(-1);
	for (int i = 0; i < 40 && e.replaying(); i++) run(e, 0.1);
	run(e, 0.3);
	std::printf("  a click down from channel 1: channel %d\n", e.settings().channel + 1);
	for (int i = 0; i < 16; i++) e.stepChannel(+1);
	for (int i = 0; i < 200 && e.replaying(); i++) run(e, 0.1);
	run(e, 0.3);
	std::printf("  then sixteen clicks up: channel %d\n", e.settings().channel + 1);
	CHECK(e.settings().channel >= 0 && e.settings().channel < 16, "the channel left its range: %d", e.settings().channel);
}

int main() {
	Roms roms;
	if (!findRoms(roms)) {
		std::printf("SKIP: set MIDIVERB_ROMS to a directory with the MIDIverb ROM images\n");
		return 0;
	}
	std::printf("1. firmware\n");
	testFirmware(roms);
	std::printf("2. DSP vs MAME\n");
	testDsp(roms, roms.verb, "MIDIverb");
	if (!roms.fex.empty()) testDsp(roms, roms.fex, "MIDIFEX");
	std::printf("3. analog\n");
	testAnalog();
	std::printf("4. board\n");
	testBoard(roms);
	std::printf("5. channel stepper\n");
	testChannelStepper(roms);
	if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
	std::printf("all passed\n");
	return 0;
}

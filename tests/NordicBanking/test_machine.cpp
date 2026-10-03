// Nordic Banking's Nord Lead 2X, run without Rack against the real firmware. See the Makefile.
#include "../../src/NordicBanking/Nord2x.hpp"
#include "../../src/NordicBanking/PanelMap.hpp"
#include "../../src/NordicBanking/KnobSync.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using nb::Nord2x;

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); failures++; } } while (0)

// Every 512 KB file under NL2X_ROMS; the first that check() accepts is the OS, and any that it
// rejects are kept to test the rejection.
static void findImages(std::vector<uint8_t>& os, std::vector<uint8_t>& other) {
	const char* dir = std::getenv("NL2X_ROMS");
	if (!dir) return;
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
			std::ifstream f(p, std::ios::binary);
			std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)), {});
			if (b.size() != Nord2x::ROM_SIZE) continue;
			if (Nord2x::check(b).empty()) { if (os.empty()) { std::printf("image: %s\n", p.c_str()); os = b; } }
			else if (other.empty()) other = b;
		}
		closedir(h);
	}
}

struct Run {
	Nord2x& n;
	static constexpr int B = 64;
	float o[4][B] = {};
	double energy = 0;
	long count = 0;
	explicit Run(Nord2x& n) : n(n) {}
	void seconds(double s) {
		float* const outs[4] = { o[0], o[1], o[2], o[3] };
		energy = 0;
		count = 0;
		for (long b = 0; b < long(s * Nord2x::SAMPLE_RATE / B); b++) {
			n.process(outs, B);
			for (int i = 0; i < B; i++) for (int c = 0; c < 4; c++) energy += double(o[c][i]) * o[c][i];
			count += B;
		}
	}
	double rms() const { return count ? std::sqrt(energy / count) : 0; }
	void press(uint16_t id) { n.setButton(id, true); seconds(0.12); n.setButton(id, false); seconds(0.4); }
};

static const nb::Led* led(const char* id) {
	for (const auto& l : nb::LEDS) if (std::string(l.id) == id) return &l;
	return nullptr;
}

int main() {
	std::vector<uint8_t> os, other;
	findImages(os, other);
	if (os.empty()) { std::printf("SKIP: set NL2X_ROMS to a directory with a Nord Lead 2X OS image\n"); return 0; }

	std::printf("1. check: 2X image accepted%s\n", other.empty() ? "" : ", another 512 KB image rejected");
	if (!other.empty()) CHECK(!Nord2x::check(other).empty(), "a non-2X image is rejected");

	const auto t0 = std::chrono::steady_clock::now();
	Nord2x n;
	const bool ok = n.boot(os, {});
	const double tBoot = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
	std::printf("2. boot (erased flash): %s in %.2f s\n", ok ? "ok" : "FAILED", tBoot);
	CHECK(ok, "boot");
	if (!ok) return 1;

	Run r(n);
	r.seconds(1.0);
	const double idle = r.rms();
	const uint8_t on[3] = { 0x90, 60, 100 }, off[3] = { 0x80, 60, 0 };
	n.midi(on, 3);
	r.seconds(1.0);
	const double held = r.rms();
	n.midi(off, 3);
	std::printf("3. MIDI note: RMS %.4f idle, %.4f held\n", idle, held);
	CHECK(idle < 1e-4, "silence before any note (RMS %.2g)", idle);
	CHECK(held > 0.01, "the note sounds");

	// The display: the firmware's first program, "1" on the third digit (segments upper and lower
	// right), and the LEDs as the map says.
	r.seconds(2.0);
	float rows[6][8], digits[3][8];
	n.leds(rows, digits);
	r.seconds(0.5);
	n.leds(rows, digits);
	std::printf("4. display: digit 3 right segments %.2f %.2f, left %.2f\n", digits[2][6], digits[2][5], digits[2][2]);
	CHECK(digits[2][6] > 0.3 && digits[2][5] > 0.3 && digits[2][2] < 0.05, "the display shows 1");
	const nb::Led* slotA = led("slot_a");
	CHECK(slotA && rows[slotA->row][slotA->bit] > 0.9, "the SLOT A LED is lit");

	// RING MOD/SYNC cycles off -> sync -> ring -> both: each press moves the SYNC LED.
	const nb::Led* sync = led("sync");
	const float before = rows[sync->row][sync->bit];
	r.press(nb::BUTTONS[3].id);
	n.leds(rows, digits); r.seconds(0.5); n.leds(rows, digits);
	const float after = rows[sync->row][sync->bit];
	std::printf("5. RING MOD/SYNC: SYNC LED %.2f -> %.2f\n", before, after);
	CHECK(std::fabs(after - before) > 0.9, "the button moves the SYNC LED");

	// STORE twice writes the program into the flash, which the patch keeps; a unit booted from
	// that flash comes up with it.
	std::vector<uint8_t> f0, f1;
	n.copyFlash(f0);
	r.press(nb::BUTTONS[21].id);
	r.press(nb::BUTTONS[21].id);
	r.seconds(1.5);
	n.copyFlash(f1);
	long changed = 0;
	for (size_t i = 0; i < std::min(f0.size(), f1.size()); i++) changed += f0[i] != f1[i];
	std::printf("6. STORE: %ld flash bytes changed\n", changed);
	CHECK(f1.size() == Nord2x::FLASH_SIZE, "the flash is 64 KB");
	CHECK(changed > 0, "STORE wrote to the flash");

	Nord2x n2;
	const bool ok2 = n2.boot(os, f1);
	Run r2(n2);
	r2.seconds(1.0);
	n2.leds(rows, digits); r2.seconds(0.5); n2.leds(rows, digits);
	const float sync2 = rows[sync->row][sync->bit];
	std::printf("7. reboot with the stored flash: %s, SYNC LED %.2f\n", ok2 ? "ok" : "FAILED", sync2);
	CHECK(ok2, "boots from a saved flash");
	CHECK(std::fabs(sync2 - after) < 0.1, "the stored program comes back (SYNC as stored)");

	// Clavia's factory programs, as SysEx into an erased unit (NL2X_SYSEX: a directory holding the
	// factory bank's bank0.syx; SKIP without). Paced as the module sends them: one message per tick.
	if (const char* sx = std::getenv("NL2X_SYSEX")) {
		std::ifstream f(std::string(sx) + "/bank0.syx", std::ios::binary);
		std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), {});
		std::vector<std::vector<uint8_t>> msgs;
		for (size_t i = 0; i < d.size(); i++) {
			if (d[i] != 0xF0) continue;
			size_t j = i + 1;
			while (j < d.size() && d[j] != 0xF7) j++;
			if (j >= d.size()) break;
			msgs.emplace_back(d.begin() + long(i), d.begin() + long(j) + 1);
			i = j;
		}
		Nord2x n3;
		CHECK(n3.boot(os, {}), "boot for the factory bank");
		Run r3(n3);
		r3.seconds(1.0);
		std::vector<uint8_t> e0, e1;
		n3.copyFlash(e0);
		for (auto& m : msgs) { n3.midi(m.data(), m.size()); r3.seconds(0.025); }
		r3.seconds(2.0);
		n3.copyFlash(e1);
		long filled = 0;
		for (size_t i = 0; i < e1.size(); i++) filled += e0[i] != e1[i];
		// Program 1 again (down, up), now the factory one: no longer the blank program, whose
		// all-0xFF data lights unison and distortion together.
		r3.press(nb::BUTTONS[20].id);
		r3.press(nb::BUTTONS[19].id);
		r3.seconds(0.5);
		n3.leds(rows, digits); r3.seconds(0.5); n3.leds(rows, digits);
		const nb::Led* uni = led("unison");
		const nb::Led* dist = led("distortion");
		const bool blankLook = rows[uni->row][uni->bit] > 0.5f && rows[dist->row][dist->bit] > 0.5f;
		n3.midi(on, 3);
		r3.seconds(1.0);
		const double factory = r3.rms();
		n3.midi(off, 3);
		std::printf("8. factory bank 0: %zu messages, %ld flash bytes written, program 1 %s, note RMS %.4f\n",
			msgs.size(), filled, blankLook ? "STILL BLANK" : "loaded", factory);
		CHECK(msgs.size() > 90, "the bank file holds the programs");
		CHECK(filled > 5000, "the programs land in the flash");
		CHECK(!blankLook, "program 1 is the factory program, not the blank one");
		CHECK(factory > 0.005, "the factory program sounds");

		// The knobs follow the sound: for every knob, the position KnobSync would move it to must reproduce the
		// program's own value, and a knob already there must be left alone. Programs 1, 2, 3 and 4.
		int unsynced = 0, moved = 0;
		for (int p = 0; p < 4; p++) {
			uint8_t prog[nb::EDIT_BUFFER_SIZE];
			n3.ram(nb::EDIT_BUFFER, prog, sizeof prog);
			for (int k = 0; k < 26; k++) {
				const nb::KnobSync::Curve c = nb::KnobSync::curve(k);
				if (c == nb::KnobSync::NONE) continue;
				const int code = nb::KnobSync::code(c, prog[nb::KNOBS[k].offset]);
				if (std::abs(nb::KnobSync::value(c, code) - prog[nb::KNOBS[k].offset]) > nb::KnobSync::tolerance(c)) unsynced++;
				if (nb::KnobSync::resync(k, code, prog) >= 0) moved++;
			}
			r3.press(nb::BUTTONS[19].id);   // next program
			r3.seconds(0.3);
		}
		std::printf("   knobs follow the sound: %d knobs that cannot reach their program's value, %d that would be moved twice\n", unsynced, moved);
		CHECK(unsynced == 0, "every knob can be put where the program has its parameter");
		CHECK(moved == 0, "a knob put there is left alone");
	} else {
		std::printf("8. factory bank: SKIP (set NL2X_SYSEX to the factory bank's SysEx directory)\n");
	}

	const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
	std::printf("   %.1f s wall in all\n", wall);
	// Every knob edits the byte of the edit buffer PanelMap.hpp says, by the law KnobSync.hpp says. gearmulator's
	// names for the A/D channels are not the firmware's (its "Filter frequency" edits the amp decay), so this
	// is what keeps the panel honest: a wrong channel here is a knob that does the wrong thing.
	{
		Nord2x n4;
		CHECK(n4.boot(os, {}), "boot for the knob map");
		Run r4(n4);
		r4.seconds(1.0);
		int wrong = 0, offLaw = 0;
		for (int k = 0; k < 26; k++) {
			const nb::Knob& kn = nb::KNOBS[k];
			if (kn.offset < 0) continue;
			uint8_t before[nb::EDIT_BUFFER_SIZE], after[nb::EDIT_BUFFER_SIZE];
			for (int code : { 40, 200, 90, 168 }) {   // 168 after 90 is a move in the other direction; two codes land on a detent
				n4.setKnob(kn.channel, uint8_t(code));
				r4.seconds(0.4);
				n4.ram(nb::EDIT_BUFFER, after, sizeof after);
				const int want = nb::KnobSync::value(nb::KnobSync::curve(k), code);
				if (std::abs(int(after[kn.offset]) - want) > nb::KnobSync::tolerance(nb::KnobSync::curve(k))) {
					offLaw++;
					std::printf("   knob %d (%s) channel %02x code %d: byte %d is %d, law says %d\n", k, kn.name, kn.channel, code, kn.offset, after[kn.offset], want);
				}
				(void)before;
			}
			// and nothing else moved much: the other 65 bytes are what they were before the last move
			n4.setKnob(kn.channel, 20);
			r4.seconds(0.4);
			n4.ram(nb::EDIT_BUFFER, before, sizeof before);
			n4.setKnob(kn.channel, 230);
			r4.seconds(0.4);
			n4.ram(nb::EDIT_BUFFER, after, sizeof after);
			int others = 0;
			for (int i = 0; i < nb::EDIT_BUFFER_SIZE; i++) if (i != kn.offset && before[i] != after[i]) others++;
			if (others) { wrong++; std::printf("   knob %d (%s) channel %02x moved %d other bytes\n", k, kn.name, kn.channel, others); }
		}
		std::printf("9. knob map: %d knobs off their law, %d moving other bytes\n", offLaw, wrong);
		CHECK(offLaw == 0, "every knob edits its byte by its law");
		CHECK(wrong == 0, "no knob edits a byte of another");
	}

	if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
	std::printf("all passed\n");
	return 0;
}

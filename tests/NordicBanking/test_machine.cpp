// Nordic Banking's Nord Lead 2X, run without Rack against the real firmware. See the Makefile.
#include "../../src/NordicBanking/Nord2x.hpp"
#include "../../src/NordicBanking/PanelMap.hpp"

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

	const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
	std::printf("   %.1f s wall in all\n", wall);
	if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
	std::printf("all passed\n");
	return 0;
}

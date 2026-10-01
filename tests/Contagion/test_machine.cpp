// Contagion's Virus C, run without Rack against the real firmware. See the Makefile.
#include "../../src/Contagion/VirusC.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using vc::VirusC;

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); failures++; } } while (0)

static std::vector<uint8_t> findImage() {
	const char* dir = std::getenv("VIRUS_ROMS");
	if (!dir) return {};
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
			VirusC probe;
			if (probe.load(b).empty()) { closedir(h); std::printf("image: %s\n", p.c_str()); return b; }
		}
		closedir(h);
	}
	return {};
}

struct Run {
	VirusC& v;
	static constexpr int B = 64;
	float in[B] = {}, o[6][B] = {};
	double peak = 0, energy = 0;
	long n = 0;
	explicit Run(VirusC& v) : v(v) {}
	void seconds(double s) {
		float* const outs[6] = { o[0], o[1], o[2], o[3], o[4], o[5] };
		peak = energy = 0;
		n = 0;
		for (long b = 0; b < long(s * VirusC::SAMPLE_RATE / B); b++) {
			v.process(in, in, outs, B);
			for (int i = 0; i < B; i++) {
				peak = std::max(peak, double(std::fabs(o[0][i])));
				energy += double(o[0][i]) * o[0][i];
			}
			n += B;
		}
	}
	double rms() const { return n ? std::sqrt(energy / n) : 0; }
};

int main() {
	const std::vector<uint8_t> image = findImage();
	if (image.empty()) { std::printf("SKIP: set VIRUS_ROMS to a directory with a Virus A/B/C OS image\n"); return 0; }

	VirusC v;
	CHECK(v.load(image).empty(), "load");
	for (int i = 0; i < 32; i++) v.setPot(i, 0xc0);   // knobs where a hand left them
	const auto t0 = std::chrono::steady_clock::now();
	const bool ok = v.boot();
	std::printf("1. boot: %s, LCD [%s]\n", ok ? "audio port live" : "FAILED", v.lcdText().c_str());
	CHECK(ok, "boot");
	if (!ok) return 1;

	Run r(v);
	r.seconds(10.0);
	std::printf("2. after 10 s: LCD [%s]\n", v.lcdText().c_str());
	CHECK(v.lcdText().find("A0") != std::string::npos, "main screen shows program A0");
	CHECK(r.peak < 1e-3, "silence before any note (peak %.2g)", r.peak);

	// MIDI through the 80C515's UART.
	for (int k : { 48, 55, 60, 64 }) { v.midi(0x90); v.midi(uint8_t(k)); v.midi(100); }
	r.seconds(2.0);
	const double held = r.rms();
	for (int k : { 48, 55, 60, 64 }) { v.midi(0x80); v.midi(uint8_t(k)); v.midi(0); }
	r.seconds(1.5);
	r.seconds(1.0);
	const double after = r.rms();
	std::printf("3. chord: RMS %.3f held, %.5f a second after release\n", held, after);
	CHECK(held > 0.05, "the chord sounds");
	CHECK(after < held * 0.01, "and stops");

	// A knob edits the program: the LCD shows the parameter, the program turns lowercase.
	v.setPot(13, 0x30);   // group 1, channel 5: Cutoff
	r.seconds(0.6);
	std::printf("4. cutoff knob: LCD [%s]\n", v.lcdText().c_str());
	CHECK(v.lcdText().find("Cutoff") != std::string::npos, "the cutoff knob shows Cutoff");

	// STORE twice: the edited single lands in the user bank's battery RAM.
	std::vector<uint8_t> g0, b0, g1, b1;
	v.copyRam(g0, b0);
	for (int k = 0; k < 2; k++) { v.setButton(2, 4, true); r.seconds(0.15); v.setButton(2, 4, false); r.seconds(0.5); }
	r.seconds(1.0);
	v.copyRam(g1, b1);
	long changed = 0;
	for (size_t i = 0; i < b0.size(); i++) changed += b0[i] != b1[i];
	std::printf("5. store: LCD [%s], %ld bank-RAM bytes changed\n", v.lcdText().c_str(), changed);
	CHECK(changed > 0, "STORE changed bank RAM");

	const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
	std::printf("   %.1f s of machine time in %.1f s wall\n", 16.75 + 3 + 1.3, wall);
	if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
	std::printf("all passed\n");
	return 0;
}

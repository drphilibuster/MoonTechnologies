// Contagion's Virus C, run without Rack against the real firmware. See the Makefile.
#include "../../src/Contagion/PanelMap.hpp"
#include "../../src/Contagion/VirusC.hpp"

#include <algorithm>
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

	// The panel map: press a button by its matrix position and the LED the map gives it
	// must answer. One second back in play mode first, then each press settles for 0.6 s.
	v.setButton(vc::KEY[vc::K_SINGLE][0], vc::KEY[vc::K_SINGLE][1], true); r.seconds(0.15);
	v.setButton(vc::KEY[vc::K_SINGLE][0], vc::KEY[vc::K_SINGLE][1], false); r.seconds(1.0);
	float g[7][14];
	auto led = [&](int i) { return g[vc::LED[i][0]][vc::LED[i][1]]; };
	auto look = [&]() { v.leds(g); r.seconds(0.5); v.leds(g); };
	auto press = [&](int k) {
		v.setButton(vc::KEY[k][0], vc::KEY[k][1], true); r.seconds(0.15);
		v.setButton(vc::KEY[k][0], vc::KEY[k][1], false); r.seconds(0.45);
		look();
	};
	look();
	std::printf("6. panel map: SINGLE %.2f MULTI %.2f, FILT 1 LP %.2f", led(vc::L_SINGLE), led(vc::L_MULTI), led(vc::L_F1_LP));
	CHECK(led(vc::L_SINGLE) > 0.8 && led(vc::L_MULTI) < 0.1, "SINGLE lit, MULTI dark in single mode");
	CHECK(led(vc::L_F1_LP) > 0.8 && led(vc::L_F1_HP) < 0.1, "filter 1 starts low-pass");
	press(vc::K_FLT1_MODE);
	std::printf(" -> HP %.2f", led(vc::L_F1_HP));
	CHECK(led(vc::L_F1_HP) > 0.8 && led(vc::L_F1_LP) < 0.1, "FILT 1 steps the mode to high-pass");
	press(vc::K_OSC2);
	std::printf(", OSC 2 %.2f", led(vc::L_OSC2));
	CHECK(led(vc::L_OSC2) > 0.8 && led(vc::L_OSC1) < 0.1, "OSC 2 selects oscillator 2");
	press(vc::K_LFO_SELECT);
	std::printf(", LFO 2 %.2f", led(vc::L_LFO2));
	CHECK(led(vc::L_LFO2) > 0.8 && led(vc::L_LFO1) < 0.1, "SELECT steps to LFO 2");
	press(vc::K_MULTI);
	std::printf(", MULTI %.2f\n", led(vc::L_MULTI));
	CHECK(led(vc::L_MULTI) > 0.8 && led(vc::L_SINGLE) < 0.1, "MULTI lights MULTI");

	// The RATE LEDs come from the DSP's timers and follow the LFOs.
	float lo[2] = { 1, 1 }, hi[2] = { 0, 0 };
	for (int i = 0; i < 200; i++) {
		r.seconds(0.01);
		float rate[2];
		v.rateLeds(rate);
		for (int k = 0; k < 2; k++) { lo[k] = std::min(lo[k], rate[k]); hi[k] = std::max(hi[k], rate[k]); }
	}
	std::printf("7. RATE LEDs over 2 s: LFO 1 %.2f-%.2f, LFO 2/3 %.2f-%.2f\n", lo[0], hi[0], lo[1], hi[1]);
	CHECK(hi[0] - lo[0] > 0.2, "the LFO 1 RATE LED moves");
	CHECK(hi[1] - lo[1] > 0.2, "the LFO 2/3 RATE LED moves");

	const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
	std::printf("   %.1f s of machine time in %.1f s wall\n", 16.75 + 3 + 1.3 + 1.15 + 5 * 1.1 + 2, wall);
	if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
	std::printf("all passed\n");
	return 0;
}

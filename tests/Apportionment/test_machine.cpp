// Apportionment: the DP/4 machine and its router, run against the real firmware.
//
// Needs DP4_ROMS pointing at a directory with the OS (32 KB) and UCODE (128 KB)
// EPROM images; without it, prints SKIP and passes. The ROMs are Ensoniq's and
// never live in this repository.
#include "../../src/Apportionment/Router.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <iterator>
#include <string>

using namespace dp4;

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static std::vector<uint8_t> slurp(const std::string& p) {
	std::ifstream f(p, std::ios::binary);
	return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}

/** The same rule the module uses: the 32 KB image is the OS, the 128 KB one the UCODE. */
static bool findRoms(const std::string& dir, std::vector<uint8_t>& os, std::vector<uint8_t>& uc) {
	DIR* d = opendir(dir.c_str());
	if (!d) return false;
	while (dirent* e = readdir(d)) {
		const std::vector<uint8_t> v = slurp(dir + "/" + e->d_name);
		if (v.size() == 0x8000) os = v;
		if (v.size() == 0x20000) uc = v;
	}
	closedir(d);
	return !os.empty() && !uc.empty();
}

static void run(Machine& m, Router* r, double seconds) {
	const int16_t z[4] = {};
	for (uint64_t n = uint64_t(seconds * Machine::FRAME_RATE); n--;) {
		m.frame(z, nullptr, nullptr);
		if (r) r->tick(m);
	}
}

/** Drive the router to `t` and wait for it; returns seconds of machine time used. */
static double route(Machine& m, Router& r, const Routing& t) {
	r.request(t);
	double s = 0;
	while (r.busy() && s < 60) { run(m, &r, 0.05); s += 0.05; }
	run(m, &r, 0.5);
	return s;
}

static bool routingMatches(Machine& m, const Routing& t) {
	const Routing now = m.routing();
	for (int f = 0; f < Router::F_COUNT; f++) {
		if (Router::page(now.sources, Router::Field(f)) < 0) continue;
		if (Router::value(now, Router::Field(f)) != Router::value(t, Router::Field(f))) return false;
	}
	return now.sources == t.sources;
}

int main() {
	const char* dir = getenv("DP4_ROMS");
	std::vector<uint8_t> os, uc;
	if (!dir || !findRoms(dir, os, uc)) {
		printf("SKIP: set DP4_ROMS to a directory holding the DP/4 OS and UCODE EPROM images\n");
		return 0;
	}

	Machine m;
	CHECK(m.load(os, uc).empty(), "ROMs rejected");
	m.powerOn();
	run(m, nullptr, 4.0);

	// 1. Boot: the OS reaches Select mode on its first power-on.
	CHECK(m.display().line(0).compare(0, 6, "Select") == 0, "boot display is [%s][%s]",
		m.display().line(0).c_str(), m.display().line(1).c_str());
	CHECK(m.esp(0).transferCollisions() == 0, "host transfer collisions on ESP A");
	// The OS version, as the boot screen announced it (the module warns below 1.15).
	const std::string ver = m.osVersion();
	CHECK(ver.size() == 4 && ver[0] == '1' && ver[1] == '.', "OS version not read from the boot screen: [%s]", ver.c_str());
	printf("%s OS version %s read from the boot screen\n", ver.size() == 4 ? "ok  " : "FAIL", ver.c_str());

	// 1b. The firmware's tables land where its programs read them. Unit A's
	// compressor has a reciprocal table (32640/n) at Table A = $3F00, which the
	// host loads through DADR -- left-justified, so the word address is its top
	// 16 bits (spec 5.1.3). Read as right-justified, it landed at $F0000-up and
	// the compressor read zeros.
	CHECK(m.esp(0).dram(0x3f01) == 32640 && m.esp(0).dram(0x3f02) == 16320 && m.esp(0).dram(0x3f04) == 8160,
		"unit A's reciprocal table is not at $3F00 (%d %d %d)", m.esp(0).dram(0x3f01), m.esp(0).dram(0x3f02), m.esp(0).dram(0x3f04));

	// 1c. Esp.cpp's fast ESP loop against MAME's own execute_run: two machines,
	// power-on to Select mode, a tone, a unit bypassed and back -- every output
	// sample identical, and every register, pipeline latch and DRAM word of all
	// four chips identical twice a second. Two separate machines also means two
	// heaps: anything that reads uninitialised memory shows up here as well.
	{
		Machine fast, ref;
		ref.useReferenceEsp(true);
		fast.load(os, uc); ref.load(os, uc);
		fast.powerOn(); ref.powerOn();
		int16_t tin[4] = {}, of[4], orf[4], tf[8], tr[8];
		long firstAudio = -1, firstState = -1;
		const long N = long(6.5 * Machine::FRAME_RATE);
		for (long n = 0; n < N; n++) {
			if (n == long(5.0 * Machine::FRAME_RATE) || n == long(5.5 * Machine::FRAME_RATE))
				for (Machine* x : { &fast, &ref }) { x->button(BTN_A, true); x->button(BTN_A, false); }
			tin[0] = tin[1] = n > long(4.5 * Machine::FRAME_RATE) ? int16_t(6000 * std::sin(n * 2 * M_PI * 220 / Machine::FRAME_RATE)) : 0;
			fast.frame(tin, of, tf);
			ref.frame(tin, orf, tr);
			if (firstAudio < 0 && (std::memcmp(of, orf, sizeof of) || std::memcmp(tf, tr, sizeof tf))) firstAudio = n;
			if (firstState < 0 && n % 17437 == 0) {
				bool same = fast.cpuPc() == ref.cpuPc();
				for (int i = 0; i < 4; i++) same = same && fast.esp(i).stateHash() == ref.esp(i).stateHash();
				if (!same) firstState = n;
			}
		}
		CHECK(firstAudio < 0, "fast ESP core's audio departs from MAME's at frame %ld", firstAudio);
		CHECK(firstState < 0, "fast ESP core's state departs from MAME's by frame %ld", firstState);
		printf("%s fast ESP core matches MAME's execute_run for %.1f s\n", firstAudio < 0 && firstState < 0 ? "ok  " : "FAIL", N / Machine::FRAME_RATE);
	}

	// 1d. A sustained signal keeps coming out. Unit A opens with a noise gate
	// whose hold is refreshed by a skippable step right after a MOV to CMR; with
	// the skip condition sampled at issue rather than at write-back (as MAME had
	// it), the gate never saw its mask, shut 0.1 s in, and the DP/4 went silent.
	{
		int16_t tin[4] = {}, out[4], taps[8];
		double e = 0;
		const int N = int(1.0 * Machine::FRAME_RATE);
		for (int n = 0; n < N; n++) {
			tin[0] = tin[1] = int16_t(6000 * std::sin(n * 2 * M_PI * 220 / Machine::FRAME_RATE));
			m.frame(tin, out, taps);
			if (n >= N / 2) e += double(taps[0]) * taps[0];
		}
		const double rms = std::sqrt(e / (N / 2));
		CHECK(rms > 1000, "unit A's output died under a steady tone (rms %.0f in the last half second)", rms);
		printf("%s a steady tone keeps coming out of unit A (rms %.0f)\n", rms > 1000 ? "ok  " : "FAIL", rms);
	}

	// 2. The router reaches a sequence of Configs, verified in the firmware's RAM.
	Router router;
	Routing t = m.routing();
	struct Step { const char* what; Routing r; };
	std::vector<Step> steps;
	t.sources = 1; t.abToCd = 1; t.abRoute = 2; t.abAmount = 37;           steps.push_back({ "1 source, AB->CD parallel, AB feedback1, amount 37", t });
	t.sources = 2; t.abRoute = 1; t.cdRoute = 3; t.cdMono = 1;             steps.push_back({ "2 sources, A+B, CD feedback2, CD mono", t });
	t.sources = 3; t.abOut = 1; t.cdRoute = 0;                             steps.push_back({ "3 sources, AB mixed stereo, CD serial", t });
	t.sources = 4; t.abOut = 0; t.cdOut = 1;                               steps.push_back({ "4 sources, CD mixed stereo", t });
	t.kill[0] = 1; t.kill[2] = 1;                                          steps.push_back({ "4 sources, A and C kill on bypass", t });
	t.sources = 2; t.kill[0] = 0; t.kill[1] = 1; t.kill[3] = 1;            steps.push_back({ "2 sources, B C D kill, A bypass", t });
	t.sources = 1; t.abToCd = 0; t.abRoute = 0; t.cdRoute = 1; t.abAmount = 0; t.abMono = 0;
	t.kill[0] = t.kill[1] = t.kill[2] = t.kill[3] = 0;
	                                                                       steps.push_back({ "back to 1 source, all serial", t });
	for (auto& st : steps) {
		const double s = route(m, router, st.r);
		const bool ok = routingMatches(m, st.r);
		CHECK(ok, "router did not reach: %s (display [%s][%s])", st.what,
			m.display().line(0).c_str(), m.display().line(1).c_str());
		printf("%s %-58s %5.1f s  [%s]\n", ok ? "ok  " : "FAIL", st.what, s, m.display().line(1).c_str());
		CHECK(!m.display().led(BTN_EDIT), "router left the editor open after: %s", st.what);
	}

	// 3. Bypass: pressing a unit's button selects it, pressing it again bypasses
	// it, and the unit's red LED (indicators 12..9 for A..D) follows.
	for (int u = 0; u < 4; u++) {
		const int btn = BTN_A - u;
		auto tap = [&]() { m.button(btn, true); m.button(btn, false); run(m, nullptr, 0.4); };
		tap();
		const bool before = m.display().bypassed(u);
		tap();
		CHECK(m.display().bypassed(u) != before, "unit %c: bypass LED did not toggle", 'A' + u);
		tap();
		CHECK(m.display().bypassed(u) == before, "unit %c: bypass LED did not toggle back", 'A' + u);
	}
	printf("ok   bypass LEDs follow units A-D\n");

	// Kill versus bypass, heard: bypass unit A at the head of the serial chain
	// and send a steady tone through. B/K = bypass passes it, kill silences it.
	auto energyThrough = [&](int kill) {
		Routing k = m.routing();
		k.kill[0] = kill;
		route(m, router, k);
		auto tap = [&]() { m.button(BTN_A, true); m.button(BTN_A, false); run(m, nullptr, 0.4); };
		tap(); if (!m.display().bypassed(0)) tap();
		int16_t tin[4] = {}, out[4], taps[8];
		double e = 0;
		for (int n = 0; n < int(0.5 * Machine::FRAME_RATE); n++) {
			tin[0] = tin[1] = int16_t(8000 * std::sin(n * 2 * M_PI * 440 / Machine::FRAME_RATE));
			m.frame(tin, out, taps);
			for (int c = 0; c < 4; c++) e += double(out[c]) * out[c];
		}
		tap();   // un-bypass
		return e;
	};
	const double passed = energyThrough(0), killed = energyThrough(1);
	CHECK(passed > 0 && killed < passed * 1e-3, "kill did not mute a bypassed unit (bypass %.3g, kill %.3g)", passed, killed);
	printf("%s bypass passes, kill mutes (energy %.3g vs %.3g)\n", killed < passed * 1e-3 ? "ok  " : "FAIL", passed, killed);
	{ Routing k = m.routing(); k.kill[0] = 0; route(m, router, k); }

	// 4. Audio: a tone burst through the Config now running comes back and dies
	// away. A burst, not an impulse: unit A's program opens with a noise gate,
	// and a single sample does not open it -- on the DP/4 or here.
	int16_t in[4] = {}, out[4], taps[8];
	double early = 0, late = 0, lateDc = 0;
	const int N = int(3 * Machine::FRAME_RATE), burst = int(0.2 * Machine::FRAME_RATE);
	for (int n = 0; n < N; n++) {
		in[0] = in[1] = n < burst ? int16_t(8000 * std::sin(n * 2 * M_PI * 440 / Machine::FRAME_RATE)) : 0;
		m.frame(in, out, taps);
		double e = 0;
		for (int c = 0; c < 4; c++) e += double(out[c]) * out[c];
		if (n < N / 10) early += e;
		if (n >= N - N / 10) { late += e; lateDc += out[2]; }
	}
	CHECK(early > 0, "no signal came out");
	CHECK(late < early * 1e-3, "response did not decay (late/early energy %.3g)", late / (early + 1e-9));
	CHECK(std::fabs(lateDc / (N / 10)) < 64, "DC on output 3: %.1f", lateDc / (N / 10));

	for (int i = 0; i < 4; i++) CHECK(m.esp(i).transferCollisions() == 0, "ESP %c transfer collisions", 'A' + i);

	printf(failures ? "%d FAILED\n" : "all passed\n", failures);
	return failures ? 1 : 0;
}

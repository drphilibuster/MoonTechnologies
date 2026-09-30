// Apportionment: the DP/4 machine and its router, run against the real firmware.
//
// Needs DP4_ROMS pointing at a directory with the OS (32 KB) and UCODE (128 KB)
// EPROM images; without it, prints SKIP and passes. The ROMs are Ensoniq's and
// never live in this repository.
#include "../../src/Apportionment/Router.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
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

	// 2. The router reaches a sequence of Configs, verified in the firmware's RAM.
	Router router;
	Routing t = m.routing();
	struct Step { const char* what; Routing r; };
	std::vector<Step> steps;
	t.sources = 1; t.abToCd = 1; t.abRoute = 2; t.abAmount = 37;           steps.push_back({ "1 source, AB->CD parallel, AB feedback1, amount 37", t });
	t.sources = 2; t.abRoute = 1; t.cdRoute = 3; t.cdMono = 1;             steps.push_back({ "2 sources, A+B, CD feedback2, CD mono", t });
	t.sources = 3; t.abOut = 1; t.cdRoute = 0;                             steps.push_back({ "3 sources, AB mixed stereo, CD serial", t });
	t.sources = 4; t.abOut = 0; t.cdOut = 1;                               steps.push_back({ "4 sources, CD mixed stereo", t });
	t.sources = 1; t.abToCd = 0; t.abRoute = 0; t.cdRoute = 1; t.abAmount = 0; t.abMono = 0;
	                                                                       steps.push_back({ "back to 1 source, all serial", t });
	for (auto& st : steps) {
		const double s = route(m, router, st.r);
		const bool ok = routingMatches(m, st.r);
		CHECK(ok, "router did not reach: %s (display [%s][%s])", st.what,
			m.display().line(0).c_str(), m.display().line(1).c_str());
		printf("%s %-58s %5.1f s  [%s]\n", ok ? "ok  " : "FAIL", st.what, s, m.display().line(1).c_str());
		CHECK(!m.display().led(BTN_EDIT), "router left the editor open after: %s", st.what);
	}

	// 3. Audio: an impulse through the Config now running comes back and dies away.
	int16_t in[4] = { 16384, 16384, 0, 0 }, out[4], taps[8];
	double early = 0, late = 0, lateDc = 0;
	const int N = int(3 * Machine::FRAME_RATE);
	for (int n = 0; n < N; n++) {
		m.frame(in, out, taps);
		in[0] = in[1] = 0;
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

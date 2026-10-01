// Depreciation: the compiled HSP core (src/Pcm70Hsp.hpp) against the reference tick model (hsp_ref.hpp, a transcription of the research model).
// They must agree on every output sample AND on every register and DRAM word afterwards, for:
//   * random programs on random sequencer PROMs (no ROM needed: always runs),
//   * random programs on the real PROMs, program words changing under the running core (offsets every sample, control words now and then),
//   * every register of the two library banks as the firmware itself loads them,
//   * a live run, program rebuilt from the machine's control store every sample (the modulated words of chorus/flange programs).
// With $PCM70_ROMS / $PCM70_SYX unset the real-data parts print SKIP and pass.
#include "fixtures.hpp"
#include "hsp_ref.hpp"
#include "../../src/Pcm70Hsp.hpp"
#include "../../src/Pcm70Machine.hpp"

#include <chrono>
#include <cmath>
#include <random>

using namespace pcm70; using namespace fx;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

struct Pair {
	HspRef ref; Hsp opt; unsigned long samples = 0, bad = 0; double energy = 0, maxdiff = 0;
	explicit Pair(const uint8_t* u48, const uint8_t* u49) { ref.U48 = u48; ref.U49 = u49; opt.setProms(u48, u49); ref.reset(); opt.reset(); }
	void program(const uint32_t* w) { ref.setProgram(w); opt.setProgram(w); }
	void step(double in) {
		double a, b, c, d; ref.process(in, a, b); opt.process(in, c, d);
		samples++; energy += std::fabs(a) + std::fabs(b);
		if (a != c || b != d) { bad++; maxdiff = std::fmax(maxdiff, std::fmax(std::fabs(a - c), std::fabs(b - d))); }
	}
	// the whole machine state, not only the outputs
	unsigned long stateDiffs() const {
		unsigned long n = 0;
		for (int i = 0; i < 4; i++) n += ref.R[i] != opt.reg(i);
		n += ref.ACC != opt.reg(4); n += ref.O != opt.reg(5); n += ref.A != opt.reg(6); n += ref.P != opt.reg(7);
		for (int a = 0; a < 65536; a++) n += ref.mem[a] != opt.peek(a);
		return n;
	}
};

static void report(const char* what, const Pair& p) {
	std::printf("%-44s %8lu samples, %lu output mismatches, %lu state differences, energy %.1f, %lu decodes\n", what, p.samples, p.bad, p.stateDiffs(), p.energy, p.opt.decodes);
	CHECK(p.bad == 0); CHECK(p.stateDiffs() == 0);
}

// ---- random programs ----------------------------------------------------------------------------------------------------
static void testRandomPrograms(const char* label, const uint8_t* u48, const uint8_t* u49, int programs, int samples, bool realProms) {
	std::mt19937 g(realProms ? 7 : 11); std::normal_distribution<double> nd(0.0, 0.3);
	unsigned long bad = 0, decodes = 0, n = 0; double energy = 0; unsigned long sd = 0;
	for (int k = 0; k < programs; k++) {
		Pair p(u48, u49); uint32_t w[128];
		const int style = k % 3;                                      // 0: any word; 1: sparse (few memory words, like real programs); 2: real-looking control, random offsets
		for (int i = 0; i < 128; i++) {
			w[i] = (uint32_t)g();
			if (style == 1 && (g() & 3)) w[i] |= 0x40000000u;         // MCEN set: no DRAM cycle
			if (style == 2) w[i] = (w[i] & 0xFFFF) | (w[i] & 0x07FF0000u) | (g() & 1 ? 0x40000000u : 0);
		}
		p.program(w);
		for (int t = 0; t < samples; t++) {
			if (t % 97 == 40) for (int j = 0; j < 4; j++) { int i = g() % 128; w[i] = (w[i] & 0xFFFF0000u) | (g() & 0xFFFF); p.program(w); }   // offsets move under the core
			if (t % 5 == 2) { for (int j = 0; j < 3; j++) { int i = 20 + 7 * j; w[i] = (w[i] & ~0x00CF0000u) | ((g() & 0xCF) << 16); } p.program(w); }       // coefficient nibbles and register selects move (chorus-style modulation)
			if (t % 331 == 200) { int i = g() % 128; w[i] = (uint32_t)g(); p.program(w); }                                                         // a control word changes
			p.step(t < samples / 2 ? nd(g) : 0.0);
		}
		bad += p.bad; decodes += p.opt.decodes; n += p.samples; energy += p.energy; sd += p.stateDiffs();
		if (p.bad || p.stateDiffs()) std::printf("  program %d (style %d): %lu mismatches, %lu state differences\n", k, style, p.bad, p.stateDiffs());
	}
	std::printf("%-44s %8lu samples in %d programs, %lu output mismatches, %lu state differences, energy %.1f, %lu decodes\n", label, n, programs, bad, sd, energy, decodes);
	CHECK(bad == 0); CHECK(sd == 0); CHECK(energy > 0);
}

// ---- stored programs ------------------------------------------------------------------------------------------------------
static void testBanks(const std::vector<Fw>& fws) {
	const char* sd = std::getenv("PCM70_SYX");
	if (fws.empty() || !sd) { std::printf("SKIP stored programs (PCM70_ROMS / PCM70_SYX not set)\n"); return; }
	std::vector<std::string> files; walk(sd, files);
	std::mt19937 g(3); std::normal_distribution<double> nd(0.0, 0.2);
	for (const Fw& fw : fws) {
		if (fw.u48.empty() || fw.u49.empty() || fw.u67.empty()) { std::printf("SKIP V%s (no PROM / opcode ROM)\n", fw.family.c_str()); continue; }
		const bool v3 = fw.family == "3.01";
		for (const std::string& f : files) {
			std::string low = f; for (char& c : low) c = (char)std::tolower(c);
			if (low.size() < 4 || low.substr(low.size() - 4) != ".syx" || (low.find("ver-3") != std::string::npos) != v3) continue;
			std::vector<Reg> regs = parseSyx(readFile(f)); if (regs.empty()) continue;
			Machine* M = new Machine(); M->load(fw.u62.data(), fw.u62.size(), fw.u95.data(), fw.u95.size()); M->runSeconds(9.0);
			unsigned long bad = 0, sdiff = 0, n = 0, decodes = 0, progs = 0; double energy = 0, worstUs = 0; size_t worstOps = 0;
			for (const Reg& r : regs) {
				if (r.data[0] == 0) continue;                                         // unused register: no program
				Bytes m = activeMessage(r); M->sendMidi(m.data(), m.size());                     // a stored-form message only files the register; the active form loads and runs it
				while (!M->midi.empty()) M->runSeconds(0.1);
				M->runSeconds(5.0);                                                   // the slave finishes writing the control store
				uint32_t w[128]; pcm70BuildProgram(M->wcs, fw.u67.data(), M->lastNzPage, w);
				Pair p(fw.u48.data(), fw.u49.data()); p.program(w);
				for (int t = 0; t < 3000; t++) p.step(t < 1500 ? nd(g) : 0.0);
				{ Hsp h; h.setProms(fw.u48.data(), fw.u49.data()); h.reset(); h.setProgram(w); double l, r; for (int t = 0; t < 200; t++) h.process(0.0, l, r);
				  auto t0 = std::chrono::steady_clock::now(); for (int t = 0; t < 20000; t++) h.process(t & 1 ? 0.1 : 0.0, l, r);
				  double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / 20000; if (us > worstUs) worstUs = us; if (h.opCount() > worstOps) worstOps = h.opCount(); }
				bad += p.bad; sdiff += p.stateDiffs(); n += p.samples; decodes += p.opt.decodes; energy += p.energy; progs++;
				if (p.bad || p.stateDiffs()) std::printf("  register %d: %lu mismatches\n", r.n, p.bad);
			}
			std::printf("V%s %s: %lu programs, %lu samples, %lu output mismatches, %lu state differences, energy %.1f, %lu decodes\n", fw.family.c_str(), f.substr(f.rfind('/') + 1).c_str(), progs, n, bad, sdiff, energy, decodes);
			std::printf("   slowest program: %.3f us/sample (%.1f%% of a core), most micro-ops %zu\n", worstUs, worstUs * 3.3854, worstOps);
			CHECK(bad == 0); CHECK(sdiff == 0); CHECK(progs >= 30); CHECK(energy > 0);
			delete M;
		}
	}
}

// ---- live: the program is rebuilt from the machine's control store every sample -------------------------------------------------
static void testLive(const std::vector<Fw>& fws) {
	const char* sd = std::getenv("PCM70_SYX");
	if (fws.empty() || !sd) { std::printf("SKIP live run (PCM70_ROMS / PCM70_SYX not set)\n"); return; }
	std::vector<std::string> files; walk(sd, files);
	for (const Fw& fw : fws) {
		if (fw.u48.empty() || fw.u49.empty() || fw.u67.empty()) continue;
		const bool v3 = fw.family == "3.01";
		for (const std::string& f : files) {
			std::string low = f; for (char& c : low) c = (char)std::tolower(c);
			if (low.size() < 4 || low.substr(low.size() - 4) != ".syx" || (low.find("ver-3") != std::string::npos) != v3) continue;
			std::vector<Reg> regs = parseSyx(readFile(f));
			const char* want[] = { "CHORUS", "FLANGE", "CONCERT HALL", "MOD WOBBLE", "ECHORUS" };    // modulated programs: the control store changes while it plays
			for (const char* nm : want) {
				const Reg* pick = nullptr; for (const Reg& r : regs) if (r.data[0] && !strncmp((const char*)&r.data[3], nm, strlen(nm))) { pick = &r; break; }
				if (!pick) continue;
				Machine* M = new Machine(); M->load(fw.u62.data(), fw.u62.size(), fw.u95.data(), fw.u95.size()); M->runSeconds(9.0);
				Bytes m = activeMessage(*pick); M->sendMidi(m.data(), m.size());
				while (!M->midi.empty()) M->runSeconds(0.1);
				M->runSeconds(5.0);
				Pair p(fw.u48.data(), fw.u49.data()); std::mt19937 g(9); std::normal_distribution<double> nd(0.0, 0.25);
				uint32_t w[128], prev[128] = {}; unsigned long changes = 0; const int N = 60000; unsigned long cyc = M->m.cyc;
				for (int t = 0; t < N; t++) {
					pcm70BuildProgram(M->wcs, fw.u67.data(), M->lastNzPage, w);
					if (memcmp(w, prev, sizeof w)) { changes++; memcpy(prev, w, sizeof w); }
					p.program(w);
					p.step(t < 8000 ? nd(g) : 0.0);
					cyc += 96; M->runTo(cyc);                                              // one audio sample of machine time
				}
				char label[96]; std::snprintf(label, sizeof label, "V%s live \"%s\" (%lu program updates)", fw.family.c_str(), nm, changes);
				report(label, p);
				CHECK(changes > 1);
				delete M;
			}
		}
	}
}

// ---- speed -----------------------------------------------------------------------------------------------------------------
static void benchmark(const std::vector<Fw>& fws) {
	const char* sd = std::getenv("PCM70_SYX");
	if (fws.empty() || !sd) return;
	for (const Fw& fw : fws) {
		if (fw.u48.empty() || fw.u67.empty()) continue;
		Machine* M = new Machine(); M->load(fw.u62.data(), fw.u62.size(), fw.u95.data(), fw.u95.size());
		M->runSeconds(9.0);
		uint32_t w[128]; pcm70BuildProgram(M->wcs, fw.u67.data(), M->lastNzPage, w);
		auto t0 = std::chrono::steady_clock::now(); const int N = 200000;
		for (int t = 0; t < N; t++) M->run(96);
		double zus = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / N;
		Hsp h; h.setProms(fw.u48.data(), fw.u49.data()); h.reset(); h.setProgram(w);
		t0 = std::chrono::steady_clock::now(); double l, r, acc = 0;
		for (int t = 0; t < N; t++) { h.process((t & 1023) < 64 ? 0.1 : 0.0, l, r); acc += l; }
		double hus = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / N;
		std::printf("V%s speed: both Z80s %.3f us/sample (%.1f%% of a core at 33.854 kHz), HSP %.3f us/sample (%.1f%%, %zu micro-ops), total %.1f%%  [%g]\n", fw.family.c_str(), zus, zus * 3.3854, hus, hus * 3.3854, h.opCount(), (zus + hus) * 3.3854, acc);
		delete M;
	}
}

int main() {
	std::mt19937 g(1); uint8_t u48[512], u49[32];
	for (auto& b : u48) b = (uint8_t)g();
	for (auto& b : u49) b = (uint8_t)g();
	testRandomPrograms("random programs, random PROMs", u48, u49, 30, 700, false);
	const std::vector<Fw> fws = loadFirmware();
	if (!fws.empty() && !fws[0].u48.empty()) testRandomPrograms("random programs, real PROMs", fws[0].u48.data(), fws[0].u49.data(), 60, 900, true);
	else std::printf("SKIP random programs on the real PROMs (PCM70_ROMS not set)\n");
	testBanks(fws);
	testLive(fws);
	benchmark(fws);
	if (failures) { std::printf("%d FAILED\n", failures); return 1; }
	std::printf("ok\n");
	return 0;
}

// Depreciation: the host's control of the firmware (src/Pcm70Control.hpp, Pcm70Keys.hpp, Pcm70Midi.hpp, ...), against the research results and against the
// firmware's own behaviour. Needs the user's ROMs and the library banks (PCM70_ROMS, PCM70_SYX); PCM70_CELLS = the research tree's results/cell_tables_v2.json
// enables the cell-table comparison. Each missing input prints SKIP.
#include "fixtures.hpp"
#include "../../src/Pcm70Machine.hpp"
#include "../../src/Pcm70Control.hpp"
#include "../../src/Pcm70Keys.hpp"
#include "../../src/Pcm70Midi.hpp"
#include "../../src/Pcm70BatRam.hpp"

#include <cmath>
#include <map>
#include <memory>

using namespace pcm70; using namespace fx;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

struct Rig {
	std::unique_ptr<Machine> M; std::unique_ptr<Control> C; std::unique_ptr<Keys> K; std::unique_ptr<Midi> Mi; bool v3 = false;
	explicit Rig(const Fw& fw, const uint8_t* ram = nullptr) : M(new Machine()), C(new Control(*M)), K(new Keys(*M)), Mi(new Midi(*M)), v3(fw.family == "3.01") {
		if (ram) memcpy(M->mram, ram, 0x2000);
		M->load(fw.u62.data(), fw.u62.size(), fw.u95.data(), fw.u95.size()); M->hook = C.get(); C->setV3(v3); }
	unsigned long target = 0;
	void run(double s) { if (!target) target = M->m.cyc; const long n = (long)(s * 3.25e6 / 96.0); for (long i = 0; i < n; i++) { target += 96; M->runTo(target); K->tick(); Mi->tick(); } }
	void send(const Bytes& b) { M->sendMidi(b.data(), b.size()); while (!M->midi.empty()) run(0.1); }
	void settle(double maxSeconds = 20.0) { double t = 0; run(0.5); while (t < maxSeconds && (C->busy() || K->busy() || Mi->pending())) { run(0.25); t += 0.25; } }
	void loadReg(const Reg& r) { send(activeMessage(r)); run(5.0); settle(); }
};

static const Reg* find(const std::vector<Reg>& regs, int n) { for (const Reg& r : regs) if (r.n == n) return &r; return nullptr; }
// 'C' and '[' are the same seven-segment pattern; the research decoder happened to print '[' in one caption
static std::string unbracket(std::string s) { for (char& c : s) if (c == '[') c = 'C'; return s; }
static std::string trim(const std::string& s) { size_t a = s.find_first_not_of(' '), b = s.find_last_not_of(' '); return a == std::string::npos ? std::string() : s.substr(a, b - a + 1); }

// ---- cell table against the research harvest ------------------------------------------------------------------------------------------------
static void testCells(const Fw& fw, const std::vector<Reg>& regs) {
	const char* cj = std::getenv(fw.family == "3.01" ? "PCM70_CELLS3" : "PCM70_CELLS");
	Rig R(fw); R.run(12.0); R.settle();
	CHECK(R.C->tableReady());
	std::printf("V%s default program: epoch %lu, %lu probes, display \"%s\"\n", fw.family.c_str(), R.C->programEpoch, R.C->probes, R.C->displayText().c_str());
	int valid = 0; for (int r = 0; r < 5; r++) for (int c = 0; c < 10; c++) valid += R.C->cell(r, c).valid;
	CHECK(valid > 20);
	// MIX at 0.0: the same descriptor family on every program: "MIX     % WET", 462..562
	const Cell& mix = R.C->cell(0, 0); CHECK(mix.valid && mix.lo == 462 && mix.hi == 562); CHECK(std::string(mix.text, 14).find("MIX") == 0);
	std::printf("  0.0 \"%s\" %d..%d word %d caption \"%s\"\n", mix.text, mix.lo, mix.hi, mix.word, mix.caption.c_str());
	if (!cj) { std::printf("SKIP cell table vs research harvest (PCM70_CELLS / PCM70_CELLS3 not set)\n"); return; }
	std::string js((std::istreambuf_iterator<char>(*std::unique_ptr<std::ifstream>(new std::ifstream(cj)))), {});
	// minimal reader for results/cell_tables_v2.json: "<type>": {"reg": N, "cells": {"r.c": {"desc": D, "k": K, "idx": I, "min": A, "max": B, "word": W, "text": "...", "caption": "..."}}}
	struct Row { int desc, k, idx, lo, hi, word; std::string text, caption; };
	std::map<int, std::map<std::string, Row>> table; std::map<int, int> regOf;
	size_t p = 0;
	while ((p = js.find("\"reg\":", p)) != std::string::npos) {
		const size_t tstart = js.rfind("\n \"", p); int type = std::atoi(js.c_str() + tstart + 3); regOf[type] = std::atoi(js.c_str() + p + 6);
		const size_t end = js.find("\n \"", p + 1); size_t q = p;
		while ((q = js.find("\n   \"", q)) != std::string::npos && (end == std::string::npos || q < end)) {
			const std::string key = js.substr(q + 5, js.find('"', q + 5) - (q + 5)); const size_t e = js.find('}', q); const std::string body = js.substr(q, e - q);
			auto num = [&](const char* f) { size_t a = body.find(std::string("\"") + f + "\":"); return a == std::string::npos ? 0 : std::atoi(body.c_str() + a + std::strlen(f) + 3); };
			auto str = [&](const char* f) { size_t a = body.find(std::string("\"") + f + "\": \""); if (a == std::string::npos) return std::string(); a += std::strlen(f) + 5; return body.substr(a, body.find('"', a) - a); };
			table[type][key] = { num("desc"), num("k"), num("idx"), num("min"), num("max"), num("word"), str("text"), str("caption") }; q = e;
		}
		p++;
	}
	int checked = 0, bad = 0;
	for (auto& tt : table) {
		const Reg* reg = find(regs, regOf[tt.first]); if (!reg) continue;
		Rig X(fw); X.run(9.0); X.loadReg(*reg);
		CHECK(X.C->tableReady());
		for (auto& kv : tt.second) {
			const int r = kv.first[0] - '0', c = std::atoi(kv.first.c_str() + 2); const Cell& cl = X.C->cell(r, c); const Row& w = kv.second; checked++;
			std::string txt(cl.text, 14); for (char& ch : txt) if (ch < 32 || ch > 126) ch = '|';
			const bool ok = cl.valid && (int)cl.desc == w.desc && cl.k == w.k && cl.idx == w.idx && cl.lo == w.lo && cl.hi == w.hi && cl.word == w.word && txt == w.text && (fw.family == "3.01" || trim(cl.caption) == trim(unbracket(w.caption)));    // the research V3 captions were decoded with the wrong font offsets
			if (!ok) { bad++; if (bad <= 6) std::printf("  type %d cell %s: got desc %04X k %d idx %d %d..%d word %d \"%s\" \"%s\" | research desc %04X k %d idx %d %d..%d word %d \"%s\" \"%s\"\n", tt.first, kv.first.c_str(), cl.desc, cl.k, cl.idx, cl.lo, cl.hi, cl.word, txt.c_str(), cl.caption.c_str(), w.desc, w.k, w.idx, w.lo, w.hi, w.word, w.text.c_str(), w.caption.c_str()); }
		}
	}
	std::printf("cell tables, %zu program types: %d cells compared with the research harvest, %d differ\n", table.size(), checked, bad);
	CHECK(checked > 200); CHECK(bad == 0);
}


// ---- edits: the firmware's own edit call reproduces a fresh load -------------------------------------------------------------------------------
static void setWord(Bytes& d, int k, int v) { d[47 + 2 * k] = (uint8_t)(v & 255); d[48 + 2 * k] = (uint8_t)((d[48 + 2 * k] & ~3) | ((v >> 8) & 3)); }
static Bytes wcsOf(Rig& R) { return Bytes(R.M->wcs, R.M->wcs + 0x400); }

static void testEdits(const Fw& fw, const std::vector<Reg>& regs, int regNo, const char* what, int delta = 3) {
	const Reg* reg = find(regs, regNo); CHECK(reg != nullptr); if (!reg) return;
	// words that move on their own (LFOs, tempo): compare three snapshots of an undisturbed load
	std::vector<bool> tv(0x400, false);
	{ Rig R(fw); R.run(9.0); R.loadReg(*reg); Bytes a = wcsOf(R); R.run(0.37); Bytes b = wcsOf(R); R.run(0.54); Bytes c = wcsOf(R); for (int i = 0; i < 0x400; i++) if (a[i] != b[i] || b[i] != c[i]) tv[i] = true; }
	int ntv = 0; for (bool b : tv) ntv += b;
	Rig base(fw); base.run(9.0); base.loadReg(*reg);
	std::vector<std::pair<int, int>> list; for (int r = 0; r < 5; r++) for (int c = 0; c < 10; c++) { const Cell& x = base.C->cell(r, c); if (x.editable() && !(r == 0 && c == 9)) list.push_back({ r, c }); }
	int equal = 0, masterDiff = 0, bad = 0, seqBad = 0, wordBad = 0;
	for (auto& rc : list) {
		Rig A(fw); A.run(9.0); A.loadReg(*reg);
		const Cell cell = A.C->cell(rc.first, rc.second);
		A.C->editNow(rc.first, rc.second, delta); A.settle(); A.run(1.0);
		const int after = (int)A.C->cell(rc.first, rc.second).word;
		Rig B(fw); B.run(9.0); Bytes d = reg->data; setWord(d, cell.k, after); Reg r2 = *reg; r2.data = d; B.loadReg(r2);
		Rig S(fw); S.run(9.0); S.loadReg(*reg); for (int i = 0; i < delta; i++) { S.C->editNow(rc.first, rc.second, 1); S.settle(); } S.run(1.0);
		Bytes wa = wcsOf(A), wb = wcsOf(B), ws = wcsOf(S); int dab = 0, das = 0;
		for (int i = 0; i < 0x400; i++) if (!tv[i]) { dab += wa[i] != wb[i]; das += wa[i] != ws[i]; }
		const bool wordSame = (int)S.C->cell(rc.first, rc.second).word == after;
		if (dab == 0) equal++; else if (cell.master) masterDiff++; else { bad++; std::printf("  %s cell %d.%d (%s): %d words differ from a fresh load\n", what, rc.first, rc.second, std::string(cell.text, 14).c_str(), dab); }
		if (das) seqBad++; if (!wordSame) wordBad++;
	}
	std::printf("%s: %zu editable cells, %d time-varying control-store words ignored: edit(+%d) equals a fresh load on %d, differs on %d masters (by design), %d others; one call vs %d single steps differ on %d cells, %d word mismatches\n", what, list.size(), ntv, delta, equal, masterDiff, bad, delta, seqBad, wordBad);
	CHECK(bad == 0); CHECK(seqBad == 0); CHECK(wordBad == 0); CHECK(equal >= (int)list.size() - 3);
}

// ---- overload: a 1 kHz flood of random targets on every parameter, with a different program loaded in the middle ----------------------------------
static void testFlood(const Fw& fw, const std::vector<Reg>& regs) {
	const Reg* a = find(regs, 32); const Reg* b = find(regs, 26); CHECK(a && b); if (!a || !b) return;
	Rig R(fw); R.run(9.0); R.loadReg(*a);
	unsigned rs = 12345; auto rnd = [&]() { rs = rs * 1664525u + 1013904223u; return (rs >> 8) / 16777216.0; };
	const unsigned long t0 = R.M->m.cyc; unsigned long calls = 0;
	for (int ms = 0; ms < 15000; ms++) {
		if (ms == 7000) R.M->sendMidi(activeMessage(*b).data(), activeMessage(*b).size());
		for (int r = 0; r < 5; r++) for (int c = 0; c < 10; c++) { const Cell& x = R.C->cell(r, c); if (R.C->tableReady() && x.editable()) { R.C->setTarget(r, c, x.lo + rnd() * (x.hi - x.lo)); calls++; } }
		R.M->run(3250);
	}
	const double total = (double)(R.M->m.cyc - t0);
	std::printf("flood: %lu target updates in 15 s, %lu edits/probes done, firmware busy %.0f%%, longest call %.0f ms, stuck %lu, epochs %lu, queue superseded %lu\n", calls, R.C->editsDone(), 100.0 * R.C->busyCycles / total, R.C->maxCallCycles / 3250.0, R.C->stuck, R.C->programEpoch, R.C->eq.overwritten);
	CHECK(R.C->stuck == 0); CHECK(R.C->busyCycles / total < 0.75); CHECK(R.C->programEpoch >= 2);          // Tiled Room (type 8), then Concert Hall (type 7)
	// quiet: fixed targets on everything, settle, then the control store must be what a fresh load of the same words gives
	std::vector<std::pair<int, int>> list; for (int r = 0; r < 5; r++) for (int c = 0; c < 10; c++) { const Cell& x = R.C->cell(r, c); if (x.editable() && !(r == 0 && c == 9)) list.push_back({ r, c }); }
	R.settle(30.0);
	for (auto& rc : list) { const Cell& x = R.C->cell(rc.first, rc.second); R.C->setTarget(rc.first, rc.second, x.lo + 0.5 * (x.hi - x.lo) + (rc.first + rc.second) % 7); }
	R.settle(60.0); R.run(2.0);
	Reg r2 = *b; Bytes d = r2.data; int moved = 0; for (auto& rc : list) { const Cell& x = R.C->cell(rc.first, rc.second); setWord(d, x.k, x.word); moved++; } r2.data = d;
	Rig F(fw); F.run(9.0); F.loadReg(r2);
	std::vector<bool> tv(0x400, false); { Rig Q(fw); Q.run(9.0); Q.loadReg(r2); Bytes x1 = wcsOf(Q); Q.run(0.37); Bytes x2 = wcsOf(Q); Q.run(0.54); Bytes x3 = wcsOf(Q); for (int i = 0; i < 0x400; i++) if (x1[i] != x2[i] || x2[i] != x3[i]) tv[i] = true; }
	Bytes wa = wcsOf(R), wf = wcsOf(F); int diff = 0; for (int i = 0; i < 0x400; i++) if (!tv[i] && wa[i] != wf[i]) diff++;
	// the independent parameters (rows 0-2 except PDELAY, whose limit moves) must be at their target within the 3/4-step hysteresis; the reflection/delay rows move with their
	// masters and dynamic limits, as on the hardware
	int atTarget = 0, indep = 0;
	for (auto& rc : list) { const Cell& x = R.C->cell(rc.first, rc.second); if (rc.first > 2 || (rc.first == 0 && rc.second == 5)) continue; indep++;
		const int tg = std::min(x.hi, std::max(x.lo, (int)std::lround(x.lo + 0.5 * (x.hi - x.lo) + (rc.first + rc.second) % 7))); if (std::abs(x.word - tg) <= 1) atTarget++; else if (std::getenv("FLOODDBG")) std::printf("   off target: %d.%d %.14s %d..%d word %d wanted %d\n", rc.first, rc.second, x.text, x.lo, x.hi, x.word, tg); }
	std::printf("after the flood: %d of %d independent parameters at their final target (+-1); control store differs from a fresh load of the same words on %d words\n", atTarget, indep, diff);
	CHECK(atTarget == indep); CHECK(diff < 30);
}

// ---- keys, bank import, MIDI patches, configuration, battery RAM ------------------------------------------------------------------------------
static void testKeysAndState(const Fw& fw, const std::vector<Reg>& regs, int regNo) {
	const Reg* r32 = find(regs, regNo); CHECK(r32 != nullptr); if (!r32) return;
	std::vector<bool> tv(0x400, false);
	{ Rig Q(fw); Q.run(9.0); Q.loadReg(*r32); Bytes x1 = wcsOf(Q); Q.run(0.37); Bytes x2 = wcsOf(Q); Q.run(0.54); Bytes x3 = wcsOf(Q); for (int i = 0; i < 0x400; i++) if (x1[i] != x2[i] || x2[i] != x3[i]) tv[i] = true; }
	Rig ref(fw); ref.run(9.0); ref.loadReg(*r32); const Bytes want = wcsOf(ref);
	auto same = [&](const Bytes& w) { int d = 0; for (int i = 0; i < 0x400; i++) if (!tv[i] && w[i] != want[i]) d++; return d; };
	// import the whole bank through the UART, one message at a time with the gap the firmware needs
	Rig R(fw); R.run(12.0);
	for (const Reg& r : regs) { uint8_t d[167]; memcpy(d, r.data.data(), 167); R.Mi->sysex(Midi::bulk(d, r.n, true)); }
	R.settle(60.0); R.run(1.0);
	{ int bad = 0; for (const Reg& r : regs) { uint8_t want[batram::SLOT]; batram::pack(r.data.data(), want); if (memcmp(want, R.M->mram + (batram::slotAddr(r.n) - 0x8000), batram::SLOT)) bad++; }
	  std::printf("V%s bank import through the UART: %d of %zu register records differ from the firmware's own\n", fw.family.c_str(), bad, regs.size()); CHECK(bad == 0); }
	// register 3.2 by keys: REG, row 3, digit 2, LOAD
	R.K->selectRegister(regNo / 10, regNo % 10); R.settle(30.0); R.run(5.0); R.settle(10.0);
	const int d1 = same(wcsOf(R));
	std::printf("V%s bank import + keys (REG, row %d, digit %d, LOAD): display \"%s\", control store differs from the sysex load of register %d on %d words; program epoch %lu\n", fw.family.c_str(), regNo / 10, regNo % 10, R.C->displayText().c_str(), regNo, d1, R.C->programEpoch);
	CHECK(d1 == 0);
	// a factory program by keys: 1.3
	R.K->selectProgram(1, 3); R.settle(30.0); R.run(3.0);
	std::printf("PGM 1.3: display \"%s\"\n", R.C->displayText().c_str()); CHECK(R.C->displayText().find(fw.family == "3.01" ? "MIDI MOD PA" : "CIRCULAR") != std::string::npos);
	// battery RAM: a power cycle on the same image resumes; a zero image is a factory-fresh machine
	Bytes ram(R.M->mram, R.M->mram + 0x2000);
	Rig P(fw, ram.data()); P.run(12.0); P.K->selectRegister(regNo / 10, regNo % 10); P.settle(30.0); P.run(5.0); P.settle(10.0);
	const int d2 = same(wcsOf(P));
	std::printf("power cycle on the saved RAM image, the register by keys: %d words differ\n", d2); CHECK(d2 == 0);
	{ Rig Z(fw); Z.run(12.0); Z.settle(); CHECK(Z.C->displayText().find("CHORUS") != std::string::npos || fw.family == "3.01"); }
}

static void testPatchAndConfig(const Fw& fw, const std::vector<Reg>& regs, int regNo) {
	const Reg* r32 = find(regs, regNo); if (!r32) return;
	std::vector<bool> tv(0x400, false);
	Rig base(fw); base.run(9.0); base.loadReg(*r32); const Cell rtmid = base.C->cell(1, 1); CHECK(rtmid.valid);
	std::printf("RT MID is cell 1.1: \"%.14s\" word %d (%d..%d)\n", rtmid.text, rtmid.word, rtmid.lo, rtmid.hi);
	Bytes d = r32->data; setWord(d, rtmid.k, rtmid.word + 8); Reg shifted = *r32; shifted.data = d;
	Rig want(fw); want.run(9.0); want.loadReg(shifted); const Bytes ww = wcsOf(want);
	{ Rig Q(fw); Q.run(9.0); Q.loadReg(shifted); Bytes x1 = wcsOf(Q); Q.run(0.37); Bytes x2 = wcsOf(Q); Q.run(0.54); Bytes x3 = wcsOf(Q); for (int i = 0; i < 0x400; i++) if (x1[i] != x2[i] || x2[i] != x3[i]) tv[i] = true; }
	auto diff = [&](const Bytes& w) { int n = 0; for (int i = 0; i < 0x400; i++) if (!tv[i] && w[i] != ww[i]) n++; return n; };
	// CC 1 -> RT MID, scale 128, value 8: the firmware adds 8 steps (to the slave only: the stored word stays)
	{ Rig R(fw); R.run(9.0); R.loadReg(*r32); CHECK(R.C->assignPatch(0, 0x02, 1, 1, 128)); R.settle(); R.Mi->cc(1, 8); R.settle(); R.run(1.0);
	  const int n = diff(wcsOf(R)); std::printf("patch CC1 -> RT MID (scale 128), CC1 = 8: control store differs from RT MID %d+8 on %d words; stored word %d\n", rtmid.word, n, R.C->cell(1, 1).word);
	  CHECK(n == 0); CHECK(R.C->cell(1, 1).word == rtmid.word); }
	// the configuration path: MIDI channel 5; only channel-5 controllers reach the patch
	{ Rig R(fw); R.run(9.0); R.loadReg(*r32); R.C->setMidiChannel(5); CHECK(R.C->assignPatch(0, 0x02, 1, 1, 128)); R.settle();
	  R.Mi->channel = 0; R.Mi->cc(1, 8); R.settle(); R.run(1.0); const int off = diff(wcsOf(R));
	  R.Mi->channel = 4; R.Mi->cc(1, 8); R.settle(); R.run(1.0); const int on = diff(wcsOf(R));
	  std::printf("MIDI channel 5: a channel-1 CC leaves the control store %s (%d words from the shifted target), a channel-5 CC moves it (%d)\n", off ? "alone" : "MOVED", off, on);
	  CHECK(off != 0); CHECK(on == 0); CHECK(R.M->mram[0x98B2 - 0x8000] == 4); }
}

// MIDI clock (V3 firmware only): with a 24-ppqn clock running, a BPM program's RATE parameter is an OFFSET to the clock tempo (Step 40); V2 ignores F8
static int bpmOf(const std::string& cap) { return std::atoi(cap.c_str()); }
static void testClock(const Fw& fw, const std::vector<Reg>& regs3) {
	const Reg* pick = nullptr; for (const Reg& r : regs3) if (r.data[0] >= 11 && r.data[0] <= 13) { pick = &r; break; }
	if (!pick) { std::printf("SKIP clock (no BPM program in the V3 bank)\n"); return; }
	Rig R(fw); R.run(9.0); R.loadReg(*pick);
	int rr = -1, rc = -1; for (int r = 0; r < 5 && rr < 0; r++) for (int c = 0; c < 10; c++) if (!strncmp(R.C->cell(r, c).text, "RATE", 4)) { rr = r; rc = c; break; }
	std::printf("V3 BPM program \"%.13s\" (type %d): ", (const char*)&pick->data[3], pick->data[0]); CHECK(rr >= 0); if (rr < 0) { std::printf("no RATE cell\n"); return; }
	R.C->watch(rr, rc, true); R.run(1.0);
	const int noClock = bpmOf(R.C->cell(rr, rc).caption);
	// RATE word 448 = no offset
	R.C->setTarget(rr, rc, 448); R.settle(); R.run(1.0);
	R.Mi->clockStart(); const int bpm = 120; const double edge = 60.0 / bpm / 24.0;
	for (int i = 0; i < (int)(10.0 / edge); i++) { R.Mi->clockEdge(24); R.run(edge); }
	R.run(1.0); R.settle(); R.C->watch(rr, rc, true); R.run(1.0);
	const Cell& c = R.C->cell(rr, rc);
	std::printf("RATE caption without clock \"%d\", with a 120 BPM clock and RATE at 448 (offset 0): \"%s\"\n", noClock, c.caption.c_str());
	CHECK(std::abs(bpmOf(c.caption) - bpm) <= 2);
	// V2 ignores the clock
	const Fw* v2f = nullptr; (void)v2f;
}

// Infinite A T (V3 register 32): aftertouch and the mod wheel are patched to 0.4 REV TIME; the slave sees these values (Step 44): AT 100 -> 248, AT 0 -> 254, MW 127 -> 176
static void testInfiniteAT(const Fw& fw, const std::vector<Reg>& regs3) {
	const Reg* reg = nullptr; for (const Reg& r : regs3) if (r.data[0] && !strncmp((const char*)&r.data[3], "INFINITE A T", 12)) reg = &r;
	if (!reg) { std::printf("SKIP Infinite A T (not in the bank)\n"); return; }
	Rig R(fw); R.run(9.0); R.loadReg(*reg);
	std::vector<uint8_t> trace; R.M->m2sTrace = &trace;
	auto lastValue = [&](int idx) { int v = -1; for (size_t i = 0; i + 2 < trace.size(); i++) if (trace[i] == 0xFF && trace[i + 1] == idx) v = trace[i + 2]; return v; };
	// find which slave index REV TIME uses: cell 0.4
	const Cell& rt = R.C->cell(0, 4); std::printf("Infinite A T: cell 0.4 \"%.14s\" slave index %d word %d\n", rt.text, rt.idx, rt.word);
	const int idx = rt.idx;
	trace.clear(); R.Mi->channel = 0; R.Mi->aftertouch(100); R.settle(); R.run(1.0); const int at100 = lastValue(idx);
	trace.clear(); R.Mi->aftertouch(0); R.settle(); R.run(1.0); const int at0 = lastValue(idx);
	trace.clear(); R.Mi->cc(1, 127); R.settle(); R.run(1.0); const int mw127 = lastValue(idx);
	std::printf("  slave value for AT 100: %d (research 248), AT 0: %d (254), mod wheel 127: %d (176)\n", at100, at0, mw127);
	CHECK(at100 == 248); CHECK(at0 == 254); CHECK(mw127 == 176);
	R.M->m2sTrace = nullptr;
}
// V2 ignores MIDI clock
static void testClockInertV2(const Fw& fw, const std::vector<Reg>& regs2) {
	const Reg* pick = nullptr; for (const Reg& r : regs2) if (r.data[0] >= 11 && r.data[0] <= 13) { pick = &r; break; }
	if (!pick) return;
	Rig R(fw); R.run(9.0); R.loadReg(*pick);
	int rr = -1, rc = -1; for (int r = 0; r < 5 && rr < 0; r++) for (int c = 0; c < 10; c++) if (!strncmp(R.C->cell(r, c).text, "RATE", 4)) { rr = r; rc = c; break; }
	if (rr < 0) return;
	R.C->watch(rr, rc, true); R.run(1.0); const std::string before = R.C->cell(rr, rc).caption;
	R.Mi->clockStart(); const double edge = 60.0 / 150 / 24.0; for (int i = 0; i < (int)(8.0 / edge); i++) { R.Mi->clockEdge(24); R.run(edge); }
	R.run(1.0); const std::string after = R.C->cell(rr, rc).caption;
	std::printf("V2 \"%.13s\": RATE caption \"%s\" before and \"%s\" after a 150 BPM clock (V2 ignores F8)\n", (const char*)&pick->data[3], before.c_str(), after.c_str()); CHECK(before == after);
}

// a probe (selector + caption formatter) must leave the firmware's selector variables and the display exactly as they were
static void testProbeInvisible(const Fw& fw, const std::vector<Reg>& regs) {
	const Reg* reg = find(regs, 32); if (!reg) return;
	Rig R(fw); R.run(9.0); R.loadReg(*reg); R.settle();
	const unsigned lo = R.M->loc.vB6 - 4, hi = R.M->loc.vBB + 1;
	Bytes sel(R.M->mram + (lo - 0x8000), R.M->mram + (hi - 0x8000)); const std::string disp = R.C->displayText();
	for (int i = 0; i < 12; i++) { R.C->probeNow(i % 5, (i * 3) % 10); }
	R.settle(); R.run(1.0);
	Bytes sel2(R.M->mram + (lo - 0x8000), R.M->mram + (hi - 0x8000));
	std::printf("12 probes: selector variables %s, display \"%s\" -> \"%s\"\n", sel == sel2 ? "unchanged" : "CHANGED", disp.c_str(), R.C->displayText().c_str());
	CHECK(sel == sel2); CHECK(disp == R.C->displayText());
}

int main() {
	const std::vector<Fw> fws = loadFirmware(); const char* sd = std::getenv("PCM70_SYX");
	const Fw* v2 = nullptr; for (const Fw& f : fws) if (f.family == "2.0") v2 = &f;
	if (!v2 || !sd) { std::printf("SKIP control (PCM70_ROMS / PCM70_SYX not set)\n"); return 0; }
	std::vector<std::string> files; walk(sd, files); std::vector<Reg> regs2;
	for (const std::string& f : files) if (f.find("Ver-2.syx") != std::string::npos) regs2 = parseSyx(readFile(f));
	testCells(*v2, regs2);
	testProbeInvisible(*v2, regs2);
	testEdits(*v2, regs2, 32, "Tiled Room");
	testFlood(*v2, regs2);
	testKeysAndState(*v2, regs2, 32);
	testPatchAndConfig(*v2, regs2, 32);
	testClockInertV2(*v2, regs2);
	// ---- V3 -------------------------------------------------------------------------------------------------------------------------------------
	const Fw* v3 = nullptr; for (const Fw& f : fws) if (f.family == "3.01") v3 = &f;
	if (v3) {
		std::vector<Reg> regs3; for (const std::string& f : files) if (f.find("Ver-3") != std::string::npos && f.find(".syx") != std::string::npos) regs3 = parseSyx(readFile(f));
		int tiled = -1; for (const Reg& r : regs3) if (r.data[0] && !strncmp((const char*)&r.data[3], "LOCKER ROOM", 11)) tiled = r.n;
		CHECK(tiled >= 0);
		if (tiled >= 0) {
			testCells(*v3, regs3);
			testEdits(*v3, regs3, tiled, "V3 Locker Room", 10);   // within 3 steps of the loaded value the V3 firmware leaves DEFINITION's coefficient bytes alone (knob and call alike)
			testKeysAndState(*v3, regs3, tiled);
			testPatchAndConfig(*v3, regs3, tiled);
		}
		testClock(*v3, regs3);
		testInfiniteAT(*v3, regs3);
	} else std::printf("SKIP V3 control (no V3 ROM set)\n");
	if (failures) { std::printf("%d FAILED\n", failures); return 1; }
	std::printf("ok\n");
	return 0;
}

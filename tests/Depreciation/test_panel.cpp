// Depreciation: the panel's logic (src/Pcm70Panel.hpp) driving a complete voice: program select (one selector, FACTORY | USER), the parameter matrix in both
// directions, the dedicated inputs as MIDI, bypass, levels, and what the display shows. Needs the user's ROMs and the library banks (PCM70_ROMS, PCM70_SYX); prints SKIP without them.
#include "fixtures.hpp"
#include "../../src/Pcm70Panel.hpp"
#include "../../src/Pcm70Names.hpp"
#include <set>

#include <cmath>
#include <memory>

using namespace pcm70; using namespace fx;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

// Plays the part of the Rack module: owns the control values, calls the logic once a millisecond, applies what it asks for.
struct Sim {
	std::unique_ptr<Voice> V; PanelLogic L; PanelLogic::In in; PanelLogic::Out out; double host = 48000.0; long long n = 0;
	float clockV = 0, prevClock = 0, runV = 0, prevRun = 0;
	explicit Sim(const Fw& fw) : V(new Voice()) {
		V->load(fw.u62.data(), fw.u62.size(), fw.u95.data(), fw.u95.size(), fw.u67.data(), fw.u48.data(), fw.u49.data(), fw.family == "3.01"); V->configure(host);
		for (int r = 0; r < 5; r++) for (int c = 0; c < 9; c++) in.knob[r][c] = 0.5f;
	}
	void step() {
		double l, r; V->process(0.0, l, r); n++;
		if (clockV > 1.f && prevClock <= 1.f) L.clockEdge(*V); prevClock = clockV;
		if (runV > 1.f && prevRun <= 1.f) L.runEdge(*V); prevRun = runV;
		if (n % 48 == 0) {
			in.dt = 0.001; out = PanelLogic::Out(); L.control(*V, in, out);
			for (int a = 0; a < 5; a++) for (int b = 0; b < 9; b++) if (out.setKnob[a][b]) in.knob[a][b] = out.knob[a][b];
		}
	}
	void run(double s) { for (long long i = 0, k = (long long)(s * host); i < k; i++) step(); }
	void settle(double maxS) { run(0.5); for (double t = 0; t < maxS && (V->control.busy() || V->keys.busy() || V->midi.pending()); t += 0.25) run(0.25); run(1.0); }
	void press(bool PanelLogic::In::*b) { in.*b = true; run(0.01); in.*b = false; run(0.01); }
	std::string disp() { return V->control.displayText(); }          // what the panel shows: never the display RAM a caption probe has blanked
};

static void testV2(const Fw& fw) {
	Sim S(fw); S.run(12.0); S.settle(10.0);
	std::printf("V2 boot: display \"%s\"; knob 0.0 (MIX) follows the firmware: %.3f\n", S.disp().c_str(), S.in.knob[0][0]);
	CHECK(S.disp().find("CHORUS") != std::string::npos); CHECK(std::fabs(S.in.knob[0][0] - 1.0f) < 0.02f);
	// program 1.3 is FACTORY slot 13
	S.in.slot = 13; S.press(&PanelLogic::In::load); S.settle(30.0); S.run(5.0); S.settle(10.0);
	std::printf("slot 13, LOAD: \"%s\"\n", S.disp().c_str()); CHECK(S.disp().find("CIRCULAR") != std::string::npos);
	// the knobs now follow the new program: 0.0 is MIX again, and every valid knob sits where its word is
	int off = 0, valid = 0; for (int r = 0; r < 5; r++) for (int c = 0; c < 9; c++) { const Cell& x = S.V->control.cell(r, c); if (!x.editable()) continue; valid++; const double wn = (double)(x.word - x.lo) / (x.hi - x.lo); if (std::fabs(wn - S.in.knob[r][c]) > 1.5 / (x.hi - x.lo) + 0.01) off++; }
	std::printf("  %d parameters, %d knobs away from their word\n", valid, off); CHECK(valid > 20); CHECK(off == 0);
	// turn MIX to 0: the word, then the firmware's own caption
	S.in.knob[0][0] = 0.f; S.settle(10.0);
	const Cell& mix = S.V->control.cell(0, 0); std::printf("  MIX knob 0 -> word %d, caption \"%s\"\n", mix.word, mix.caption.c_str()); CHECK(mix.word == mix.lo);
	// a program change moves the knobs back (they follow the firmware, not the other way round)
	S.in.slot = 42; S.press(&PanelLogic::In::load); S.settle(30.0); S.run(5.0); S.settle(10.0);
	std::printf("  slot 42: \"%s\", MIX knob %.3f\n", S.disp().c_str(), S.in.knob[0][0]);
	// bypass button
	bool seen = false; S.press(&PanelLogic::In::bypass); for (int i = 0; i < 40; i++) { S.run(0.1); if (S.disp().find("BYPASS ON") != std::string::npos) seen = true; } std::printf("  BYPASS button: %s, LED %d\n", seen ? "BYPASS ON shown" : "nothing", (int)S.out.bypassLed); CHECK(seen); CHECK(S.out.bypassLed);
	S.press(&PanelLogic::In::bypass); S.run(4.0); CHECK(!S.out.bypassLed);
	// refusals: LOAD on an empty register and STORE in FACTORY are not sent to the firmware; they say why
	{ const std::string before = S.disp();
	  S.in.regMode = true; S.in.slot = 7; S.press(&PanelLogic::In::load); S.run(1.0);
	  PanelLogic::Snap sn; S.L.snapshot(*S.V, sn); std::printf("  LOAD on empty USER 07: \"%s\"\n", sn.refusal.c_str()); CHECK(sn.refusal.find("EMPTY") != std::string::npos && sn.sinceRefusal < 2.0);
	  S.settle(10.0); CHECK(S.disp() == before);                                   // nothing reached the machine
	  S.in.regMode = false; S.in.slot = 13; S.press(&PanelLogic::In::store); S.run(1.0); S.L.snapshot(*S.V, sn);
	  std::printf("  STORE in FACTORY: \"%s\"\n", sn.refusal.c_str()); CHECK(sn.refusal.find("USER") != std::string::npos);
	  S.settle(10.0); CHECK(!PanelLogic::registerUsed(S.V->batteryRam(), 13)); }
	// STORE in USER files the running program, and then LOAD on it is accepted
	{ S.in.regMode = true; S.in.slot = 7; S.press(&PanelLogic::In::store); S.settle(30.0); S.run(3.0); S.settle(10.0);
	  CHECK(PanelLogic::registerUsed(S.V->batteryRam(), 7));
	  const std::string nm = PanelLogic::registerName(S.V->batteryRam(), 7); std::printf("  STORE USER 07 -> \"%s\"\n", nm.c_str()); CHECK(!nm.empty());
	  S.in.regMode = false; }
	// levels
	S.in.inPad20 = true; S.in.outPad20 = true; S.in.trimVolts = 10.f; S.in.input = 0.5f; S.run(0.1);
	std::printf("  levels: input gain %.4f, output pad %.4f, full scale %.1f V\n", S.V->inputGain, S.V->outputPad, S.V->fullScaleVolts);
	CHECK(std::fabs(S.V->inputGain - 0.5 * 0.17783) < 1e-4); CHECK(std::fabs(S.V->outputPad - 0.0575) < 1e-4); CHECK(S.V->fullScaleVolts == 10.0);
}

static void testV3(const Fw& fw, const std::vector<Reg>& regs3) {
	const Reg* inf = nullptr; const Reg* bpm = nullptr;
	for (const Reg& r : regs3) { if (r.data[0] && !strncmp((const char*)&r.data[3], "INFINITE A T", 12)) inf = &r; if (r.data[0] == 12 && !bpm) bpm = &r; }
	CHECK(inf && bpm); if (!inf || !bpm) return;
	Sim S(fw); S.run(12.0); S.settle(10.0);
	for (const Reg& g : regs3) { uint8_t d[167]; memcpy(d, g.data.data(), 167); S.V->midi.sysex(Midi::bulk(d, g.n, true)); }
	S.settle(60.0);
	// REG mode: the register by ROW/COL/LOAD
	S.in.regMode = true; S.in.slot = inf->n; S.press(&PanelLogic::In::load); S.settle(30.0); S.run(5.0); S.settle(10.0);
	std::printf("V3 REG %d: \"%s\" (raw RAM \"%s\")\n", inf->n, S.disp().c_str(), S.V->machine.displayText().c_str());
	// aftertouch and mod wheel through the dedicated jacks (the register's own patches pick them up)
	std::vector<uint8_t> trace; S.V->machine.m2sTrace = &trace; const int idx = S.V->control.cell(0, 4).idx;
	auto lastVal = [&]() { int v = -1; for (size_t i = 0; i + 2 < trace.size(); i++) if (trace[i] == 0xFF && trace[i + 1] == idx) v = trace[i + 2]; return v; };
	S.in.atOn = true; S.in.at = 100.f / 127.f * 10.f; trace.clear(); S.settle(10.0); const int at100 = lastVal();
	S.in.at = 0.f; trace.clear(); S.settle(10.0); const int at0 = lastVal();
	S.in.modOn = true; S.in.mod = 10.f; trace.clear(); S.settle(10.0); const int mw127 = lastVal();
	std::printf("  AT jack 7.87 V -> %d (248), AT 0 V -> %d (254), MOD jack 10 V -> %d (176)\n", at100, at0, mw127); CHECK(at100 == 248); CHECK(at0 == 254); CHECK(mw127 == 176);
	S.V->machine.m2sTrace = nullptr; S.in.atOn = S.in.modOn = false;
	// clock: BPM program in REG mode, RATE knob at no offset, a 120 BPM clock on CLOCK (24 per quarter note)
	S.in.slot = bpm->n; S.press(&PanelLogic::In::load); S.settle(30.0); S.run(5.0); S.settle(10.0);
	int rr = -1, rc = -1; for (int r = 0; r < 5 && rr < 0; r++) for (int c = 0; c < 9; c++) if (!strncmp(S.V->control.cell(r, c).text, "RATE", 4)) { rr = r; rc = c; break; }
	CHECK(rr >= 0); if (rr < 0) return;
	S.in.knob[rr][rc] = 0.f; S.settle(10.0);
	S.in.clkDiv = 4; const double edge = 60.0 / 120.0 / 24.0; double next = 0, t = 0; S.run(0.1);
	S.V->midi.clockStart(); for (int i = 0; i < (int)(10.0 / edge); i++) { S.clockV = 5.f; S.run(0.002); S.clockV = 0.f; S.run(edge - 0.002); (void)next; (void)t; }
	S.in.knob[rr][rc] = 0.0001f; S.run(2.0);                                  // touching the RATE knob puts its caption back on watch
	std::printf("  CLOCK jack at 120 BPM: RATE caption \"%s\"\n", S.V->control.cell(rr, rc).caption.c_str()); CHECK(std::abs(std::atoi(S.V->control.cell(rr, rc).caption.c_str()) - 120) <= 2);
}

// What the panel shows must not flicker or carry glyphs it cannot draw: sampled every 20 ms for ten seconds of an idle machine and of one being edited, the
// text only ever changes when the firmware changes it (a probe blanks display RAM for a moment; the panel reads the copy taken between probes).
static void testDisplay(const Fw& fw) {
	Sim S(fw); S.run(12.0); S.settle(10.0);
	std::set<std::string> seen; int blanks = 0; auto sample = [&](double secs) { for (long long i = 0, n = (long long)(secs / 0.02); i < n; i++) { S.run(0.02); PanelLogic::Snap sn; S.L.snapshot(*S.V, sn); seen.insert(sn.display); if (sn.display.find_first_not_of(' ') == std::string::npos) blanks++; CHECK(sn.display.find('?') == std::string::npos); } };
	sample(4.0);
	for (int k = 0; k < 6; k++) { S.in.knob[0][0] = k % 2 ? 0.2f : 0.8f; sample(1.0); }       // the matrix being worked: captions probed all the while
	std::printf("display over 10 s of idling and knob work: %zu distinct frames, %d blank\n", seen.size(), blanks);
	for (const std::string& d : seen) std::printf("    [%s]\n", d.c_str());
	CHECK(blanks == 0); CHECK(seen.size() <= 3);
	CHECK(PanelLogic::clean(" ?.0.1 UNUSED     ") == "  0.1 UNUSED     "); CHECK(PanelLogic::clean("??.REGISTER UNUSE") == "  REGISTER UNUSE");
}

// A patch saved while bypassed comes back live: the flag lives in the battery RAM image.
static void testStartsLive(const Fw& fw) {
	std::vector<uint8_t> ram;
	{ Sim S(fw); S.run(12.0); S.settle(10.0); S.press(&PanelLogic::In::bypass); S.run(4.0); CHECK(S.V->control.bypassed()); ram.assign(S.V->batteryRam(), S.V->batteryRam() + 0x2000); CHECK(ram[0x9B33 - 0x8000] != 0); }       // the flag is in the saved image
	Sim T(fw); T.V->setBatteryRam(ram.data()); T.V->load(fw.u62.data(), fw.u62.size(), fw.u95.data(), fw.u95.size(), fw.u67.data(), fw.u48.data(), fw.u49.data(), fw.family == "3.01"); T.V->configure(T.host);
	T.L.powerUp(); T.run(12.0); T.settle(15.0); T.run(4.0);
	std::printf("saved bypassed, powered up: bypassed=%d\n", (int)T.V->control.bypassed()); CHECK(!T.V->control.bypassed());
}

// The factory names the selector shows come out of the user's own images.
static void testNames(const Fw& fw) {
	std::atomic<bool> cancel{false};
	const ProgramNames n = ProgramNames::harvest(fw.u62, fw.u95, fw.u67, fw.u48, fw.u49, fw.family == "3.01", cancel);
	int have = 0; for (int s = 0; s < ProgramNames::SLOTS; s++) if (!n.name[s].empty()) have++;
	std::printf("V%s factory names: %d programs; 0 = \"%s\", 13 = \"%s\"\n", fw.family.c_str(), have, n.name[0].c_str(), n.name[13].c_str());
	CHECK(n.done); CHECK(have > 40 && have < 70); CHECK(!n.name[0].empty() && !n.name[13].empty()); CHECK(n.name[17].empty() || fw.family != "3.01");
	CHECK(ProgramNames::finish("INFINITE A", { &fw.u62, &fw.u95 }) == (fw.family == "3.01" ? "INFINITE A T" : "INFINITE A"));
}

int main() {
	const std::vector<Fw> fws = loadFirmware(); const char* sd = std::getenv("PCM70_SYX");
	const Fw* v2 = nullptr; const Fw* v3 = nullptr; for (const Fw& f : fws) { if (f.u67.empty() || f.u48.empty()) continue; if (f.family == "2.0") v2 = &f; else v3 = &f; }
	if (!v2 || !sd) { std::printf("SKIP panel logic (PCM70_ROMS / PCM70_SYX not set)\n"); return 0; }
	testV2(*v2); testDisplay(*v2); testStartsLive(*v2); testNames(*v2);
	if (v3) { testNames(*v3); std::vector<std::string> files; walk(sd, files); std::vector<Reg> regs3; for (const std::string& f : files) if (f.find("Ver-3") != std::string::npos && f.find(".syx") != std::string::npos) regs3 = parseSyx(readFile(f)); testV3(*v3, regs3); }
	if (failures) { std::printf("%d FAILED\n", failures); return 1; }
	std::printf("ok\n");
	return 0;
}

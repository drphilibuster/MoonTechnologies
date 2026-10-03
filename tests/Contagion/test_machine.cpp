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

#include "../../src/Contagion/Controls.hpp"
#include "../../src/Contagion/KnobSync.hpp"
#include "../../src/Contagion/Presets.hpp"

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

static uint8_t snapChars[32], snapCg[64];

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

	// 8. The panel's knobs as the unit's buttons: Controls.hpp's selectors drive the real firmware
	// through its key matrix and read the answer back off its LEDs.
	{
		struct Sel { const char* name; int stepKey; const int* direct; int led0, n; bool dark; vc::Selector s; int knob; };
		static const int oscKeys[3] = { 6, 7, 8 };
		Sel sels[] = {
			{ "LFO select", 1, nullptr, 1, 4, false, {}, 0 }, { "LFO shape", 2, nullptr, 5, 5, true, {}, 0 },
			{ "OSC", -1, oscKeys, 12, 3, false, {}, 0 }, { "FX select", 11, nullptr, 17, 3, false, {}, 0 },
			{ "FILT 1", 29, nullptr, 51, 4, false, {}, 0 }, { "FILT 2", 30, nullptr, 55, 4, false, {}, 0 },
		};
		vc::KeyPresser keys;
		for (Sel& x : sels) { x.s.steps = x.n; x.s.stepKey = x.stepKey; x.s.directKey = x.direct; x.s.lastWhenDark = x.dark; }
		const int usedKeys[] = { 1, 2, 6, 7, 8, 11, 29, 30 };
		bool held[35] = {};
		float lg[7][14];
		auto lit = [&](int i) { return lg[vc::LED[i][0]][vc::LED[i][1]]; };
		const float blocksPerSec = float(VirusC::SAMPLE_RATE) / Run::B;
		const int blocksPerTick = int(blocksPerSec * 0.025f);
		auto drive = [&](double seconds) {
			for (int t = 0; t < int(seconds / 0.025); t++) {
				for (int b = 0; b < blocksPerTick; b++) {
					for (int k : usedKeys) {
						const bool d = keys.process(k, blocksPerSec);
						if (d != held[k]) { v.setButton(vc::KEY[k][0], vc::KEY[k][1], d); held[k] = d; }
					}
					r.seconds(double(Run::B) / VirusC::SAMPLE_RATE);
				}
				v.leds(lg);
				for (Sel& x : sels) {
					int ids[5];
					for (int i = 0; i < x.n; i++) ids[i] = x.led0 + i;
					const int obs = vc::litPosition(lit, ids, x.n);
					const int k = x.s.tick(x.knob, obs, keys, 0.025f);
					if (k >= 0) x.knob = k;
				}
			}
		};
		drive(1.5);
		int start[6];
		for (int i = 0; i < 6; i++) start[i] = sels[i].knob;
		std::printf("8. knobs as buttons: unit starts at");
		for (int i = 0; i < 6; i++) std::printf(" %d", start[i]);
		const int wish[6] = { 3, 2, 2, 2, 3, 2 };
		for (int i = 0; i < 6; i++) sels[i].knob = wish[i];
		drive(8.0);
		for (int i = 0; i < 6; i++) {
			int ids[5];
			for (int k = 0; k < sels[i].n; k++) ids[k] = sels[i].led0 + k;
			int obs = vc::litPosition(lit, ids, sels[i].n);
			if (obs < 0 && sels[i].dark) obs = sels[i].n - 1;
			std::printf(" | %s %d", sels[i].name, obs);
			CHECK(obs == wish[i], "%s: knob asked for %d, the unit shows %d", sels[i].name, wish[i], obs);
			CHECK(sels[i].knob == wish[i], "%s: the knob settled on %d", sels[i].name, sels[i].knob);
		}
		std::printf("\n");
	}

	// 9. Presets: the names Presets.hpp reads out of the image and the battery RAM are the ones the
	// firmware shows when MIDI selects them, bank by bank, including C-H flash banks and the user
	// banks it keeps in RAM.
	{
		vc::Presets pre;
		CHECK(pre.loadImage(image), "image names");
		std::vector<uint8_t> g, b;
		v.copyRam(g, b);
		int checked = 0, wrong = 0;
		const int progs[] = { 0, 1, 37, 64, 127 };
		for (int bank = 0; bank < vc::Presets::BANKS; bank++)
			for (int p : progs) {
				for (uint8_t byte : vc::Presets::select(0, bank, p)) v.midi(byte);
				r.seconds(0.4);
				const std::string lcd = v.lcdText();
				const std::string want = pre.name(bank, p, b);
				const std::string lab = vc::Presets::label(bank, p);
				const bool labelOk = lcd.find(lab) != std::string::npos;
				// the unit writes its own idea of an unprintable character, so compare the printable run
				const bool nameOk = want.empty() || lcd.find(want.substr(0, std::min<size_t>(want.size(), 6))) != std::string::npos
					|| want.find('?') != std::string::npos;
				checked++;
				if (!labelOk || !nameOk) { wrong++; std::printf("   %s: wanted [%s] LCD [%s]\n", lab.c_str(), want.c_str(), lcd.c_str()); }
			}
		std::printf("9. presets: %d sounds selected over MIDI, %d disagree with the names read from the image/RAM\n", checked, wrong);
		CHECK(wrong == 0, "preset names match the firmware");
		// the screen parser, on first lines as the firmware writes them
		auto parse = [](const char* t) { uint8_t l[16]; for (int i = 0; i < 16; i++) l[i] = uint8_t(t[i]); return vc::Presets::fromScreen(l); };
		CHECK(parse("\x00 A0  AutoBendBC") == 0, "A0");
		CHECK(parse("\x00\x01" "C127 - START -") == 2 * 128 + 127, "C127");
		CHECK(parse("\x00 H5  2-Brass RP") == 7 * 128 + 5, "H5");
		CHECK(parse("\x00 m0  Sequencer ") == -1, "a multi is not a single");
		CHECK(parse("                ") == -1 && parse("\x00               ") == -1, "blank lines");
	}

	// 10. Knobs follow the sound (KnobSync.hpp). The map and the curves are measured; here they are
	// checked against the firmware in every context, and a knob moved to the sound's value is shown
	// to be harmless to touch.
	{
		using vc::KnobSync;
		static const int POT_INDEX[32] = { 0, 1, 24, 3, 27, 11, 19, 9, 2, 26, 5, 21, 29, 17, 25, 8, 10, 18, 16, 13, 20, 23, 31, 14,
			12, 7, 15, 6, 4, 28, 22, 30 };
		auto pc = [&](int bank, int prog) { v.midi(0xB0); v.midi(32); v.midi(uint8_t(bank)); v.midi(0xC0); v.midi(uint8_t(prog)); r.seconds(0.7); };
		auto tap = [&](int k) {
			v.setButton(vc::KEY[k][0], vc::KEY[k][1], true); r.seconds(0.06);
			v.setButton(vc::KEY[k][0], vc::KEY[k][1], false); r.seconds(0.3);
		};
		float lg2[7][14];
		auto context = [&]() {
			v.leds(lg2); r.seconds(0.3); v.leds(lg2);
			return KnobSync::context([&](int i) { return lg2[vc::LED[i][0]][vc::LED[i][1]]; });
		};
		uint8_t buf[256];
		int curveChecks = 0, curveBad = 0, touches = 0, touchBad = 0, controls = 0, controlJumped = 0;
		auto edit = [&](uint8_t* b) { v.xram(0, b, 256); };
		// 1. the curve of every knob that has one, in the context that is up: five codes each
		auto sweep = [&](const char* what, std::vector<int> knobs) {
			const KnobSync::Context cx = context();
			for (int k : knobs) {
				for (int code : { 24, 72, 128, 200, 248 }) {
					edit(buf);
					const KnobSync::Map m = KnobSync::map(k, cx, buf);
					if (m.curve == KnobSync::NONE) break;
					v.setPot(POT_INDEX[k], uint8_t(code));
					r.seconds(0.35);
					edit(buf);
					const KnobSync::Map after = KnobSync::map(k, cx, buf);
					if (after.byte != m.byte) break;           // the knob moved the shape across a threshold
					const int got = buf[m.byte], want = KnobSync::value(m.curve, code);
					curveChecks++;
					if (std::abs(got - want) > KnobSync::tolerance(m.curve)) {
						curveBad++;
						std::printf("   %s knob %d code %d: byte %d is %d, the curve says %d\n", what, k, code, m.byte, got, want);
					}
				}
			}
		};
		tap(vc::K_SINGLE);                                // earlier sections left it in MULTI
		r.seconds(1.0);
		pc(0, 3);
		sweep("defaults", { 1, 2, 4, 5, 6, 8, 9, 10, 11, 12, 14, 15, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31 });
		for (int i = 0; i < 4; i++) { sweep("lfo", { 0 }); tap(1); }           // LFO 1, 2, 3, MOD
		for (int osc : { 6, 7, 8 }) { tap(osc); sweep("osc", { 2, 3, 4, 5, 6 }); }
		for (int i = 0; i < 3; i++) { tap(11); sweep("fx", { 7, 13 }); }
		for (int sel : { 31, 32 }) { tap(sel); sweep("filter", { 21, 22 }); }
		// 2. a knob moved to the sound's value, then touched, leaves the sound where it was
		for (int bank : { 0, 1, 3, 5 })
			for (int prog : { 4, 40, 90 }) {
				tap(6);
				pc(bank, prog);
				const KnobSync::Context cx = context();
				edit(buf);
				for (int k : { 1, 2, 4, 5, 6, 8, 9, 10, 11, 12, 14, 15, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31 }) {
					const KnobSync::Map m = KnobSync::map(k, cx, buf);
					if (m.curve == KnobSync::NONE) continue;
					const int want = buf[m.byte];
					const int c = KnobSync::resync(k, want < 64 ? 250 : 5, cx, buf);     // the knob was somewhere else
					if (c < 0) continue;
					v.setPot(POT_INDEX[k], uint8_t(c));
					r.seconds(0.35);
					uint8_t after[256];
					edit(after);
					touches++;
					const int moved = std::abs(after[m.byte] - want);
					if (moved > KnobSync::tolerance(m.curve) + 3) {     // the firmware settles a knob within a few steps
						touchBad++;
						std::printf("   %c%d knob %d: sound %d, touched at its synced position it went to %d\n", vc::Presets::letter(bank), prog, k, want, after[m.byte]);
					}
					// the control: the knob left where it was and touched, which is what the unit does, jumps
					v.setPot(POT_INDEX[k], uint8_t(want < 64 ? 250 : 5));
					r.seconds(0.35);
					edit(after);
					controls++;
					if (std::abs(after[m.byte] - want) > 8) controlJumped++;
				}
			}
		std::printf("10. knobs follow the sound: %d curve readings (%d off), %d touches after a sync (%d jumped); "
			"left where they were, %d of %d jump\n", curveChecks, curveBad, touches, touchBad, controlJumped, controls);
		CHECK(controls > 100 && controlJumped * 10 >= controls * 9, "the control: an unsynced knob jumps the sound (%d of %d)", controlJumped, controls);
		CHECK(curveChecks > 150 && curveBad == 0, "curves agree with the firmware");
		CHECK(touches > 100 && touchBad == 0, "a synced knob is safe to touch");
	}

	// 11. The BPM knob: a SysEx parameter change to the single edit buffer sets the sound's clock tempo.
	{
		auto pc = [&](int bank, int prog) { v.midi(0xB0); v.midi(32); v.midi(uint8_t(bank)); v.midi(0xC0); v.midi(uint8_t(prog)); r.seconds(0.7); };
		v.setButton(vc::KEY[vc::K_SINGLE][0], vc::KEY[vc::K_SINGLE][1], true); r.seconds(0.15);
		v.setButton(vc::KEY[vc::K_SINGLE][0], vc::KEY[vc::K_SINGLE][1], false); r.seconds(1.0);
		pc(0, 7);
		int bad = 0;
		for (int want : { 0, 20, 77, 127, 5 }) {
			uint8_t m[11];
			vc::KnobSync::tempoMessage(want, m);
			for (uint8_t b : m) v.midi(b);
			r.seconds(0.5);
			uint8_t buf[256];
			v.xram(0, buf, 256);
			if (buf[vc::KnobSync::TEMPO_BYTE] != want) { bad++; std::printf("   tempo %d: the sound has %d\n", want, buf[vc::KnobSync::TEMPO_BYTE]); }
		}
		std::printf("11. BPM knob: tempo set over SysEx, %d of 5 wrong\n", bad);
		CHECK(bad == 0, "the tempo message sets the clock tempo");
	}

	// The chorded gestures, driven through the module's own loop: a KeyPresser per key, the Chord helper ticking at 40 Hz, and
	// the holder down for as long as the Chord says. A fresh unit for each, so none starts from another's screen.
	{
		struct Drive {
			VirusC& v;
			vc::KeyPresser keys;
			vc::Chord chord;
			bool down[35] = {};
			double acc = 0;
			float in[64] = {}, o[6][64] = {};
			explicit Drive(VirusC& v) : v(v) {}
			void seconds(double s) {
				float* const outs[6] = { o[0], o[1], o[2], o[3], o[4], o[5] };
				for (long b = 0; b < long(s * VirusC::SAMPLE_RATE / 64); b++) {
					acc += 64.0 / VirusC::SAMPLE_RATE;
					if (acc >= 1.0 / 40) { acc -= 1.0 / 40; chord.tick(keys, 1.f / 40); }
					for (int i = 0; i < 35; i++) {
						const bool d = keys.process(i, VirusC::SAMPLE_RATE / 64.f) || i == chord.holder();
						if (d != down[i]) { v.setButton(vc::KEY[i][0], vc::KEY[i][1], d); down[i] = d; }
					}
					v.process(in, in, outs, 64);
				}
			}
			bool lcdHas(const char* t) const { return v.lcdText().find(t) != std::string::npos; }
		};
		auto fresh = [&](VirusC& u) { u.load(image); for (int i = 0; i < 32; i++) u.setPot(i, 0xc0); u.boot(); Run rr(u); rr.seconds(10.0); };
		const int SINGLE = 21, PARAM_DN = 24, PARAM_UP = 25, VALUE_DN = 26, VALUE_UP = 27;

		{   // PAGE: in the EDIT menu, one PARAMETER button held and the other pressed jumps a group, in the held button's direction
			VirusC u; fresh(u); Drive d(u);
			d.keys.press(15); d.seconds(1.0);
			const std::string start = u.lcdText();
			d.chord.request(PARAM_UP, PARAM_DN, 0.4f); d.seconds(2.0);
			const std::string fwd = u.lcdText();
			d.chord.request(PARAM_DN, PARAM_UP, 0.4f); d.seconds(2.0);
			const std::string back = u.lcdText();
			std::printf("12. page: [%s] -> forward [%s] -> back [%s]\n", start.c_str(), fwd.c_str(), back.c_str());
			CHECK(fwd != start && fwd.substr(0, 16) != start.substr(0, 16), "a click up scrolls to another group");
			// The unit's own law, not a mirror: forward jumps past the whole COMMON group, back lands on the start of it.
			CHECK(back != fwd && back.substr(0, 8) != fwd.substr(0, 8), "a click down scrolls the other way");
		}
		{   // CATEGORY and IN CATEGORY: SINGLE held, PARAMETER steps the category and VALUE the sounds in it
			VirusC u; fresh(u); Drive d(u);
			const std::string prog0 = u.lcdText().substr(0, 16);
			d.chord.request(SINGLE, PARAM_UP, 1.5f); d.seconds(1.2);
			const bool shown = d.lcdHas("Acid");
			d.seconds(2.5);                     // let go: the program screen comes back
			const bool back = d.lcdHas("A0") && !d.lcdHas("Acid");
			d.chord.request(SINGLE, VALUE_UP, 1.5f); d.seconds(1.2);
			const std::string in1 = u.lcdText();
			d.seconds(2.5);
			d.chord.request(SINGLE, VALUE_UP, 1.5f); d.seconds(1.2);
			const std::string in2 = u.lcdText();
			std::printf("13. category: step -> %s, released -> %s; sounds in it [%s] then [%s] (was [%s])\n", shown ? "Acid shown" : "NOT shown",
				back ? "program screen back" : "STILL AWAY", in1.c_str(), in2.c_str(), prog0.c_str());
			CHECK(shown, "a click on CATEGORY shows the next category while SINGLE is held");
			CHECK(back, "SINGLE is let go and the program screen returns");
			CHECK(in1.substr(0, 16) != prog0 && in2.substr(0, 16) != in1.substr(0, 16), "IN CATEGORY moves through sounds");
		}
		{   // MULTI+SINGLE: both keys together enter Multi-Single (both LEDs); SINGLE alone leaves it
			VirusC u; fresh(u); Drive d(u);
			float g[7][14];
			auto leds = [&]() { u.leds(g); d.seconds(0.4); u.leds(g); return std::string(g[vc::LED[48][0]][vc::LED[48][1]] > 0.5f ? "1" : "0") + (g[vc::LED[49][0]][vc::LED[49][1]] > 0.5f ? "1" : "0"); };
			const std::string before = leds();
			d.keys.press(20); d.keys.press(21); d.seconds(1.0);
			const std::string in = leds();
			d.keys.press(21); d.seconds(1.0);
			const std::string out = leds();
			std::printf("14. multi+single: MULTI/SINGLE LEDs %s -> %s -> %s\n", before.c_str(), in.c_str(), out.c_str());
			CHECK(before == "01" && in == "11" && out == "01", "MULTI and SINGLE together light both, and SINGLE alone leaves");
		}
	}

	const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
	std::printf("   %.1f s of machine time in %.1f s wall\n", 16.75 + 3 + 1.3 + 1.15 + 5 * 1.1 + 2, wall);
	if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
	std::printf("all passed\n");
	return 0;
}

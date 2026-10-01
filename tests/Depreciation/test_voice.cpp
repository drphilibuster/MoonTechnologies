// Depreciation: the whole voice (jacks -> filters -> Z80s + HSP -> filters -> mix -> jacks). Needs the user's ROMs and the V2 library bank (PCM70_ROMS, PCM70_SYX);
// without them it prints SKIP.
#include "fixtures.hpp"
#include "../../src/Pcm70Voice.hpp"
#include <chrono>
#include <cmath>
#include <memory>

using namespace pcm70; using namespace fx;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

struct Loaded { std::unique_ptr<Voice> v; std::string display; };

static std::vector<double> impulseResponse(const Fw& fw, const Reg& reg, double host, double secs, double volts, Voice** keep = nullptr) {
	std::unique_ptr<Voice> V(new Voice());
	V->load(fw.u62.data(), fw.u62.size(), fw.u95.data(), fw.u95.size(), fw.u67.data(), fw.u48.data(), fw.u49.data()); V->configure(host);
	double l, r; for (long long i = 0, n = (long long)(9.0 * host); i < n; i++) V->process(0.0, l, r);
	Bytes m = activeMessage(reg); V->machine.sendMidi(m.data(), m.size());
	for (long long i = 0, n = (long long)(6.0 * host); i < n; i++) V->process(0.0, l, r);
	std::vector<double> y; const long long n = (long long)(secs * host);
	for (long long i = 0; i < n; i++) { V->process(i == 0 ? volts : 0.0, l, r); y.push_back(l); }
	CHECK(V->underruns == 0);
	if (keep) *keep = V.release();
	return y;
}

static void testVoice(const std::vector<Fw>& fws) {
	const char* sd = std::getenv("PCM70_SYX");
	const Fw* fw = nullptr; for (const Fw& f : fws) if (f.family == "2.0" && !f.u67.empty() && !f.u48.empty() && !f.u49.empty()) fw = &f;
	if (!fw || !sd) { std::printf("SKIP voice (PCM70_ROMS / PCM70_SYX not set)\n"); return; }
	std::vector<std::string> files; walk(sd, files); std::vector<Reg> regs;
	for (const std::string& f : files) if (f.find("Ver-2.syx") != std::string::npos) regs = parseSyx(readFile(f));
	const Reg* single = nullptr; for (const Reg& r : regs) if (!strncmp((const char*)&r.data[3], "SINGLE DELAY", 12)) single = &r;
	CHECK(single != nullptr); if (!single) return;
	const double hosts[] = { 44100.0, 48000.0, 96000.0 };
	double firstSpacing = 0;
	for (double host : hosts) {
		Voice* kept = nullptr;
		std::vector<double> y = impulseResponse(*fw, *single, host, 2.0, 0.142, &kept);
		std::unique_ptr<Voice> V(kept);
		// the two strongest pulses, at least 100 ms apart: the first echo and the repeat
		size_t i1 = 0; for (size_t i = 0; i < y.size(); i++) if (std::fabs(y[i]) > std::fabs(y[i1])) i1 = i;
		size_t i2 = 0; double best = 0; for (size_t i = 0; i < y.size(); i++) if (std::fabs((double)i - (double)i1) > 0.1 * host && std::fabs(y[i]) > best) { best = std::fabs(y[i]); i2 = i; }
		const double spacing = std::fabs((double)i2 - (double)i1) / host;
		std::printf("host %6.0f Hz: latency %d samples, strongest pulses %.4f s apart, level %.1f dB, peak %.4f V, mix codes %d/%d\n", host, V->latencySamples(), spacing, 20 * std::log10(best / std::fabs(y[i1])), std::fabs(y[i1]), V->machine.mixWet, V->machine.mixDry);
		CHECK(std::fabs(spacing - 0.5361) < 0.0002);                                   // Single Delay as the library recordings have it (60/112 s = 0.5357 s nominal)
		if (firstSpacing == 0) firstSpacing = spacing; CHECK(std::fabs(spacing - firstSpacing) < 0.0003);   // the same at every host rate
		CHECK(V->machine.mixWet >= 0 && V->machine.mixDry >= 0);
		CHECK(V->latencySamples() > 0 && V->latencySamples() < 0.003 * host);
	}
	// silence in, silence out
	{ std::unique_ptr<Voice> V(new Voice()); V->load(fw->u62.data(), fw->u62.size(), fw->u95.data(), fw->u95.size(), fw->u67.data(), fw->u48.data(), fw->u49.data()); V->configure(48000.0);
	  double l, r, mx = 0; for (int i = 0; i < 48000 * 12; i++) { V->process(0.0, l, r); mx = std::fmax(mx, std::fmax(std::fabs(l), std::fabs(r))); }
	  std::printf("machine clock after 12 s of host audio: %.4f s\n", V->machine.seconds()); CHECK(std::fabs(V->machine.seconds() - 12.0) < 0.002);        // the machine must not run fast or slow against the audio
	  std::printf("silence in: largest output %.3g V; display \"%s\"\n", mx, V->machine.displayText().c_str()); CHECK(mx < 1e-6); CHECK(V->underruns == 0);
	  CHECK(V->machine.displayText().find("CHORUS") != std::string::npos);
	  // the level detector reaches the firmware's HEADRM port
	  for (int i = 0; i < 480; i++) V->process(5.0 * std::sin(2 * 3.14159265 * 1000.0 * i / 48000.0), l, r);          // 10 ms of a full-scale 1 kHz tone
	  std::printf("full-scale tone: HEADRM %d, %d LEDs\n", V->machine.headrm, V->detector.leds()); CHECK(V->machine.headrm == 255); CHECK(V->detector.leds() >= 4);
	  // speed (whole voice, the default program)
	  auto t0 = std::chrono::steady_clock::now(); const int N = 48000 * 4; for (int i = 0; i < N; i++) V->process(0.01 * std::sin(0.05 * i), l, r);
	  const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / N;
	  std::printf("speed at 48 kHz host: %.3f us per host sample = %.1f%% of one core\n", us, us * 4.8); }
}

// the whole thing driven the way the panel will: bank in through the UART, a register by keys, a parameter by the scheduler, bypass, power cycle on the saved RAM
static void testPanelMechanics(const Fw& fw, const std::vector<Reg>& regs) {
	std::unique_ptr<Voice> V(new Voice()); V->load(fw.u62.data(), fw.u62.size(), fw.u95.data(), fw.u95.size(), fw.u67.data(), fw.u48.data(), fw.u49.data(), false); V->configure(48000.0);
	double l, r; auto run = [&](double s) { for (long long i = 0, n = (long long)(s * 48000); i < n; i++) V->process(0.0, l, r); };
	auto settle = [&](double maxS) { run(0.5); for (double t = 0; t < maxS && (V->control.busy() || V->keys.busy() || V->midi.pending()); t += 0.25) run(0.25); };
	run(12.0); settle(10.0);
	for (const Reg& g : regs) { uint8_t d[167]; memcpy(d, g.data.data(), 167); V->midi.sysex(Midi::bulk(d, g.n, true)); }
	settle(60.0);
	V->keys.selectRegister(1, 0); settle(30.0); run(5.0); settle(10.0);                    // register 10 = Single Delay
	std::printf("by keys: display \"%s\"\n", V->machine.displayText().c_str()); CHECK(V->machine.displayText().find("SINGLE DELAY") != std::string::npos);
	// impulse response of what the keys loaded
	std::vector<double> y; for (int i = 0; i < 96000; i++) { V->process(i == 0 ? 0.142 : 0.0, l, r); y.push_back(l); }
	size_t i1 = 0; for (size_t i = 0; i < y.size(); i++) if (std::fabs(y[i]) > std::fabs(y[i1])) i1 = i;
	size_t i2 = 0; double best = 0; for (size_t i = 0; i < y.size(); i++) if (std::fabs((double)i - (double)i1) > 4800 && std::fabs(y[i]) > best) { best = std::fabs(y[i]); i2 = i; }
	const double spacing = std::fabs((double)i2 - (double)i1) / 48000.0; std::printf("  strongest pulses %.4f s apart, peak %.4f V\n", spacing, std::fabs(y[i1])); CHECK(std::fabs(spacing - 0.5361) < 0.0002);
	// MIX to 0 % through the scheduler: the wet path closes (the dry impulse is not delayed)
	const Cell& mix = V->control.cell(0, 0); CHECK(mix.valid);
	V->control.setNormalized(0, 0, 0.0); settle(10.0); run(2.0);
	std::vector<double> z; for (int i = 0; i < 96000; i++) { V->process(i == 0 ? 0.142 : 0.0, l, r); z.push_back(l); }
	double late = 0, early = 0; for (size_t i = 0; i < z.size(); i++) { if (i > 4800) late = std::fmax(late, std::fabs(z[i])); else early = std::fmax(early, std::fabs(z[i])); }
	std::printf("  MIX 0%%: word %d, caption \"%s\"; echoes after 0.1 s peak %.2e V, dry impulse %.4f V\n", V->control.cell(0, 0).word, V->control.cell(0, 0).caption.c_str(), late, early);
	CHECK(V->control.cell(0, 0).word == 462); CHECK(late < 1e-3 * early); CHECK(early > 0.01);
	// bypass footswitch
	{ V->keys.bypassTap(); bool seen = false; std::string d; for (int i = 0; i < 40; i++) { run(0.1); d = V->machine.displayText(); if (d.find("BYPASS ON") != std::string::npos) seen = true; }
	  std::printf("  BYP key: BYPASS ON %s\n", seen ? "shown" : "NOT shown"); CHECK(seen);
	  // bypassed: what does the audio do? (set MIX back first so there is a wet signal to lose)
	  run(1.0); std::printf("  bypassed: mix codes %d/%d\n", V->machine.mixWet, V->machine.mixDry);
	  V->keys.bypassTap(); run(4.0); CHECK(!V->control.bypassed()); }
	// the LOAD meter and the fault light stay sane
	std::printf("  firmware load %.0f%%, fault %d\n", 100 * V->control.load(), (int)V->control.fault()); CHECK(!V->control.fault());
	// power cycle on the saved RAM: the register is still there
	std::vector<uint8_t> ram(V->batteryRam(), V->batteryRam() + 0x2000); V->setBatteryRam(ram.data()); V->powerCycle();
	run(12.0); settle(10.0); V->keys.selectRegister(1, 0); settle(30.0); run(5.0); settle(10.0);
	std::printf("  after a power cycle: display \"%s\"\n", V->machine.displayText().c_str()); CHECK(V->machine.displayText().find("SINGLE DELAY") != std::string::npos);
	// CLEAR MEMORY: all registers gone, factory-fresh again
	V->clearMemory(); V->powerCycle(); run(12.0); settle(10.0); std::printf("  after CLEAR MEMORY: display \"%s\"\n", V->machine.displayText().c_str()); CHECK(V->machine.displayText().find("CHORUS") != std::string::npos);
}

int main() {
	const std::vector<Fw> fws = loadFirmware();
	testVoice(fws);
	{ const Fw* fw = nullptr; for (const Fw& f : fws) if (f.family == "2.0" && !f.u67.empty()) fw = &f; const char* sd = std::getenv("PCM70_SYX");
	  if (fw && sd) { std::vector<std::string> files; walk(sd, files); std::vector<Reg> regs; for (const std::string& f : files) if (f.find("Ver-2.syx") != std::string::npos) regs = parseSyx(readFile(f)); testPanelMechanics(*fw, regs); } }
	if (failures) { std::printf("%d FAILED\n", failures); return 1; }
	std::printf("ok\n");
	return 0;
}

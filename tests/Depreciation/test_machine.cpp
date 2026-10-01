// Depreciation: the control machine (two Z80s, memory map, ports), the firmware locator and the battery-RAM image.
// Needs the user's own files, all outside the repository:
//   PCM70_ROMS    the ROM dumps (any folder layout)            -> boot, locator
//   PCM70_SYX     the library .syx banks                        -> battery-RAM records written by the firmware itself
//   PCM70_ORACLE  m1_oracle/ from the research harness          -> bit-exact comparison with the harness this was ported from
// Each missing input prints SKIP and passes, so `make test` stays green without them.
#include "fixtures.hpp"
#include "../../src/Pcm70Machine.hpp"
#include "../../src/Pcm70BatRam.hpp"

#include <cmath>

using namespace pcm70;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

using namespace fx;

// ---- locator ------------------------------------------------------------------------------------------------------
static void testLocator(const std::vector<Fw>& fws) {
	if (fws.empty()) { std::printf("SKIP locator (PCM70_ROMS not set)\n"); return; }
	for (const Fw& fw : fws) {
		const Pcm70Loc L = pcm70Locate(fw.u62.data(), fw.u62.size());
		const bool v3 = fw.family == "3.01";
		std::printf("locator V%s: idle %04X edit %04X sel %04X wbase %04X\n", fw.family.c_str(), L.idle, L.edit, L.sel, L.wbase);
		CHECK(L.ok);
		CHECK(L.idle == (v3 ? 0x33BBu : 0x3346u)); CHECK(L.edit == (v3 ? 0x4FB7u : 0x4F15u)); CHECK(L.sel == (v3 ? 0x4FEBu : 0x4F49u));
		CHECK(L.wbase == 0x9A34u); CHECK(L.vB6 > 0x8000 && L.vB7 > 0x8000 && L.vB8 > 0x8000 && L.vBA > 0x8000 && L.vBB > 0x8000);
		// an unknown ROM is refused, never half-located
		Bytes junk(fw.u62.size(), 0xFF); CHECK(!pcm70Locate(junk.data(), junk.size()).ok);
		Bytes cut = fw.u62; for (unsigned i = 0; i < 9; i++) cut[L.idle + i] ^= 0x55; CHECK(!pcm70Locate(cut.data(), cut.size()).ok);     // idle epilogue gone
		cut = fw.u62; cut[L.edit + 11] ^= 0x55; CHECK(!pcm70Locate(cut.data(), cut.size()).ok);                                          // edit prologue gone
	}
}

// ---- boot + sysex, against the research harness -----------------------------------------------------------------------
static void testBoot(const std::vector<Fw>& fws) {
	if (fws.empty()) { std::printf("SKIP boot (PCM70_ROMS not set)\n"); return; }
	for (const Fw& fw : fws) {
		Machine* M = new Machine(); M->load(fw.u62.data(), fw.u62.size(), fw.u95.data(), fw.u95.size());
		CHECK(M->loc.ok);
		M->runSeconds(9.0);
		const std::string disp = M->displayText();
		std::printf("V%s boot 9 s: display \"%s\" copyPage %d wcsWrites %lu\n", fw.family.c_str(), disp.c_str(), M->copyPage, M->wcsWrites);
		CHECK(disp.find(fw.family == "3.01" ? "MOD WOBBLE" : "CHORUS") != std::string::npos);   // power-up default: V2 0.0 CHORUS, V3 0.0 MOD WOBBLE
		CHECK(M->copyPage == 1);                                              // the default program is on opcode page 1
		const char* od = std::getenv("PCM70_ORACLE");
		if (od) {                                                            // Tiled Room sysex, then 6 s: every byte of WCS/RAM equals the research harness
			const std::string dir = std::string(od) + (fw.family == "3.01" ? "/v3/" : "/v2/");
			Bytes mid = readFile(std::string(od) + "/tiled_active.mid"), wcs = readFile(dir + "out_wcs.bin"), mram = readFile(dir + "out_mram.bin"), sram = readFile(dir + "out_sram.bin");
			CHECK(!mid.empty() && wcs.size() == 0x400 && mram.size() == 0x2000 && sram.size() == 0x2000);
			M->sendMidi(mid.data(), mid.size()); M->runSeconds(6.0);
			int dw = 0, dm = 0, ds = 0;
			for (size_t i = 0; i < 0x400 && i < wcs.size(); i++) dw += M->wcs[i] != wcs[i];
			for (size_t i = 0; i < 0x2000 && i < mram.size() && i < sram.size(); i++) { dm += M->mram[i] != mram[i]; ds += M->sram[i] != sram[i]; }
			std::printf("V%s Tiled Room: %d WCS bytes, %d master RAM bytes, %d slave RAM bytes differ from the harness; copyPage %d lastNz %d\n", fw.family.c_str(), dw, dm, ds, M->copyPage, M->lastNzPage);
			CHECK(dw == 0); CHECK(dm == 0); CHECK(ds == 0);
			CHECK(M->lastNzPage == 4);
		} else std::printf("SKIP Tiled Room WCS (PCM70_ORACLE not set)\n");
		delete M;
	}
}

// ---- battery RAM ----------------------------------------------------------------------------------------------------
static void testPackSynthetic() {
	Bytes d(batram::SYSEX_DATA, 0); for (size_t i = 0; i < d.size(); i++) d[i] = (uint8_t)(i * 37 + 11);
	for (size_t k = 0; k < batram::WORDS; k++) d[47 + 2 * k + 1] &= 3;       // a word's high byte carries only 2 bits
	uint8_t rec[batram::SLOT], back[batram::SYSEX_DATA];
	batram::pack(d.data(), rec); batram::unpack(rec, back);
	CHECK(memcmp(back, d.data(), d.size()) == 0);
	CHECK(memcmp(rec, d.data(), batram::HEADER) == 0 && rec[batram::SLOT - 1] == 0);
	// word 0 = 0x2A5: low byte first in its group, high bits in bits 0-1 of the group's fifth byte
	Bytes e(batram::SYSEX_DATA, 0); e[47] = 0xA5; e[48] = 0x02; batram::pack(e.data(), rec);
	CHECK(rec[47] == 0xA5 && rec[51] == 0x02);
	e[47 + 2 * 3] = 0x11; e[48 + 2 * 3] = 0x03; batram::pack(e.data(), rec);      // word 3: highs in bits 6-7
	CHECK(rec[50] == 0x11 && rec[51] == (0x02 | (0x03 << 6)));
	CHECK(batram::slotAddr(0) == 0x8001 && batram::slotAddr(49) == 0x8001 + 123 * 49 && batram::ACTIVE == 0x9807);
}
static void testBatRam(const std::vector<Fw>& fws) {
	const char* sd = std::getenv("PCM70_SYX");
	if (fws.empty() || !sd) { std::printf("SKIP battery RAM against the firmware (PCM70_ROMS / PCM70_SYX not set)\n"); return; }
	std::vector<std::string> files; walk(sd, files);
	for (const Fw& fw : fws) {
		const bool v3 = fw.family == "3.01"; int sets = 0;
		for (const std::string& f : files) {
			std::string low = f; for (char& c : low) c = (char)std::tolower(c);
			if (low.size() < 4 || low.substr(low.size() - 4) != ".syx") continue;
			if ((low.find("ver-3") != std::string::npos) != v3) continue;       // each version's bank goes to its own firmware
			std::vector<Reg> regs = parseSyx(readFile(f)); if (regs.empty()) continue;
			sets++;
			Machine* M = new Machine(); M->load(fw.u62.data(), fw.u62.size(), fw.u95.data(), fw.u95.size()); M->runSeconds(9.0);
			double t0 = M->seconds(), gap = std::getenv("BATGAP") ? atof(std::getenv("BATGAP")) : 0.15;   // between messages: the firmware needs ~0.06 s after a message's F7 (0.05 s loses sync)
			for (const Reg& r : regs) { Bytes m = storedMessage(r); M->sendMidi(m.data(), m.size()); if (gap > 0) { while (!M->midi.empty()) M->runSeconds(0.0005); M->runSeconds(gap); } }
			while (!M->midi.empty()) M->runSeconds(0.5);                         // the firmware paces the UART itself
			M->runSeconds(3.0);
			std::printf("  %zu registers took %.1f s of machine time\n", regs.size(), M->seconds() - t0);
			int bad = 0, rt = 0, empty = 0;
			for (const Reg& r : regs) {
				uint8_t want[batram::SLOT], back[batram::SYSEX_DATA];
				batram::pack(r.data.data(), want);
				const uint8_t* got = M->mram + (batram::slotAddr(r.n) - 0x8000);
				if (memcmp(want, got, batram::SLOT) != 0) {
					if (bad++ < 3) { std::printf("  reg %d type %02X differs at:", r.n, r.data[0]); int shown = 0; for (size_t i = 0; i < batram::SLOT && shown < 8; i++) if (want[i] != got[i]) { std::printf(" +%zu want %02X got %02X;", i, want[i], got[i]); shown++; } std::printf("\n"); }
				}
				batram::unpack(want, back); if (memcmp(back, r.data.data(), r.data.size()) != 0) rt++;
				if (r.data[0] == 0) empty++;
			}
			std::printf("V%s %s: %zu registers (%d unused), %d records differ from the firmware's own, %d round trips fail\n", fw.family.c_str(), f.substr(f.rfind('/') + 1).c_str(), regs.size(), empty, bad, rt);
			CHECK(bad == 0); CHECK(rt == 0); CHECK(regs.size() >= 40);
			delete M;
		}
		CHECK(sets >= 1);
	}
}

static void testConstants() {                                              // derived independently of the header's literals
	CHECK(Machine::MIDI_PERIOD == (unsigned long)(3.25e6 * 10.0 / 31250.0));            // 31250 baud, 10 bits per byte
	CHECK(Machine::IRQ_PERIOD == (unsigned long)(3.25e6 * 8 * 128 * 230e-9));           // 8 x 128 x 230 ns interrupt divider, truncated as in the research harness
	CHECK(Machine::HZ == 13e6 / 4);                                                      // 13 MHz / 4; 96 clocks per audio sample = 33854.1667 Hz
	CHECK(std::fabs(Machine::HZ / 96.0 - 33854.1667) < 1e-3);
}

int main() {
	testConstants();
	testPackSynthetic();
	const std::vector<Fw> fws = loadFirmware();
	testLocator(fws);
	testBoot(fws);
	testBatRam(fws);
	if (failures) { std::printf("%d FAILED\n", failures); return 1; }
	std::printf("ok\n");
	return 0;
}

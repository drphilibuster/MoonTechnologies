// voice_ir: run the complete voice on a stored register and write the stereo response to an impulse (research tool, needs the user's ROMs and .syx bank).
//   voice_ir ROMS_DIR BANK.syx REG HOST_RATE SECONDS IMPULSE_VOLTS OUT.f64 [noise=SECONDS]
// The output is raw little-endian doubles, interleaved L R, in Rack volts at the host rate. The impulse is one host sample.
#include "fixtures.hpp"
#include "../../src/Pcm70Voice.hpp"
#include <random>
using namespace pcm70; using namespace fx;
int main(int argc, char** argv) {
	if (argc < 8) { std::fprintf(stderr, "usage: voice_ir ROMS_DIR BANK.syx REG HOST_RATE SECONDS IMPULSE_VOLTS OUT.f64\n"); return 2; }
	setenv("PCM70_ROMS", argv[1], 1);
	const std::vector<Fw> fws = loadFirmware(); const bool v3 = std::string(argv[2]).find("Ver-3") != std::string::npos;
	const Fw* fw = nullptr; for (const Fw& f : fws) if ((f.family == "3.01") == v3) fw = &f;
	if (!fw || fw->u67.empty() || fw->u48.empty()) { std::fprintf(stderr, "ROM set incomplete\n"); return 2; }
	std::vector<Reg> regs = parseSyx(readFile(argv[2])); const int reg = std::atoi(argv[3]); const Reg* pick = nullptr;
	const bool isFile = std::string(argv[3]).find('/') != std::string::npos;
	if (!isFile) for (const Reg& r : regs) if (r.n == reg) pick = &r;
	Reg custom;                                                         // REG may instead be the path of a raw 167-byte register (data[0..166])
	if (isFile) { custom.n = 50; custom.data = readFile(argv[3]); if (custom.data.size() == 167) pick = &custom; }
	if (!pick) { std::fprintf(stderr, "register %d not in bank\n", reg); return 2; }
	const double host = std::atof(argv[4]), secs = std::atof(argv[5]), volts = std::atof(argv[6]);
	double noise = 0; for (int i = 8; i < argc; i++) if (!strncmp(argv[i], "noise=", 6)) noise = std::atof(argv[i] + 6);
	std::unique_ptr<Voice> V(new Voice());
	V->load(fw->u62.data(), fw->u62.size(), fw->u95.data(), fw->u95.size(), fw->u67.data(), fw->u48.data(), fw->u49.data()); V->configure(host);
	double l, r; const long long nBoot = (long long)(9.0 * host);
	for (long long i = 0; i < nBoot; i++) V->process(0.0, l, r);
	Bytes m = activeMessage(*pick); V->machine.sendMidi(m.data(), m.size());
	const long long nLoad = (long long)(6.0 * host);
	for (long long i = 0; i < nLoad; i++) V->process(0.0, l, r);
	FILE* f = std::fopen(argv[7], "wb"); const long long n = (long long)(secs * host); std::mt19937 g(1); std::normal_distribution<double> nd(0, 1);
	for (long long i = 0; i < n; i++) {
		double x = i == 0 ? volts : 0.0; if (noise > 0 && i < noise * host) x = volts * 0.3 * nd(g);
		V->process(x, l, r); double b[2] = { l, r }; std::fwrite(b, 8, 2, f);
	}
	std::fclose(f);
	std::fprintf(stderr, "latency %d host samples, underruns %lu, detector peak %.4f, mix codes %d/%d\n", V->latencySamples(), V->underruns, V->detector.peak, V->machine.mixWet, V->machine.mixDry);
	return 0;
}

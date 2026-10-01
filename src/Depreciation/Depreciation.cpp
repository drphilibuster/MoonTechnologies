// Depreciation -- a Lexicon PCM 70 digital effects processor, running its own firmware.
//
// The five firmware images are Lexicon's and are not distributed: the context menu loads them from wherever the user keeps them, the patch remembers the paths
// and never the bytes, and the panel says what it found. The machine (two Z80s, the HSP signal processor, the analog converter stages) lives in src/Pcm70*.hpp
// and is tested without Rack; the panel's meaning (what each control does to it) is src/Pcm70Panel.hpp, tested the same way. This file is only the Rack side:
// parameters in, voltages out, a read-out well, and the menus for what is not a control.
//
// Threads: the machine is built and booted (about nine seconds of its own time) on a worker, then handed to the audio thread, which is the only thing that
// touches it afterwards. The UI reads a snapshot the audio thread leaves under a lock it never waits on, and asks for anything it wants done (bank import,
// configuration bytes) through a queue the audio thread drains.
#include "../plugin.hpp"
#include "Panel.hpp"
#include "Roms.hpp"
#include "../Pcm70Panel.hpp"
#include "../Pcm70BatRam.hpp"

#include <osdialog.h>

#include <atomic>
#include <fstream>
#include <functional>
#include <iterator>
#include <mutex>
#include <thread>

namespace {

std::vector<uint8_t> readFile(const std::string& path) {
	std::ifstream f(path, std::ios::binary);
	if (!f) return {};
	return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}

/** Rack's user-directory settings file shared by the plugin's modules (paths only). */
std::string settingsPath() { return asset::user("MoonTechnologies/settings.json"); }

std::string base64(const std::vector<uint8_t>& in) {
	static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string out;
	for (size_t i = 0; i < in.size(); i += 3) {
		uint32_t v = in[i] << 16 | (i + 1 < in.size() ? in[i + 1] << 8 : 0) | (i + 2 < in.size() ? in[i + 2] : 0);
		out += T[v >> 18 & 63]; out += T[v >> 12 & 63];
		out += i + 1 < in.size() ? T[v >> 6 & 63] : '='; out += i + 2 < in.size() ? T[v & 63] : '=';
	}
	return out;
}

std::vector<uint8_t> unbase64(const std::string& s) {
	std::vector<uint8_t> out; uint32_t v = 0; int bits = 0;
	for (char ch : s) {
		int d = ch >= 'A' && ch <= 'Z' ? ch - 'A' : ch >= 'a' && ch <= 'z' ? ch - 'a' + 26 : ch >= '0' && ch <= '9' ? ch - '0' + 52 : ch == '+' ? 62 : ch == '/' ? 63 : -1;
		if (d < 0) continue;
		v = v << 6 | (uint32_t)d; bits += 6;
		if (bits >= 8) { bits -= 8; out.push_back((uint8_t)(v >> bits & 255)); }
	}
	return out;
}

#define ROWPOS(r) { panel::P##r##0_POS, panel::P##r##1_POS, panel::P##r##2_POS, panel::P##r##3_POS, panel::P##r##4_POS, panel::P##r##5_POS, panel::P##r##6_POS, panel::P##r##7_POS, panel::P##r##8_POS }
#define ROWCAP(r) { panel::CAP##r##0_POS, panel::CAP##r##1_POS, panel::CAP##r##2_POS, panel::CAP##r##3_POS, panel::CAP##r##4_POS, panel::CAP##r##5_POS, panel::CAP##r##6_POS, panel::CAP##r##7_POS, panel::CAP##r##8_POS }
const Vec CELL_POS[5][9] = { ROWPOS(0), ROWPOS(1), ROWPOS(2), ROWPOS(3), ROWPOS(4) };
const Vec CAP_POS[5][9] = { ROWCAP(0), ROWCAP(1), ROWCAP(2), ROWCAP(3), ROWCAP(4) };
const Vec CV_POS[8] = { panel::CV1_POS, panel::CV2_POS, panel::CV3_POS, panel::CV4_POS, panel::CV5_POS, panel::CV6_POS, panel::CV7_POS, panel::CV8_POS };
const Vec ATT_POS[8] = { panel::ATT1_POS, panel::ATT2_POS, panel::ATT3_POS, panel::ATT4_POS, panel::ATT5_POS, panel::ATT6_POS, panel::ATT7_POS, panel::ATT8_POS };
const Vec ASG_POS[8] = { panel::ASG1_POS, panel::ASG2_POS, panel::ASG3_POS, panel::ASG4_POS, panel::ASG5_POS, panel::ASG6_POS, panel::ASG7_POS, panel::ASG8_POS };
const Vec ASG_LED_POS[8] = { panel::ASG_LED1_POS, panel::ASG_LED2_POS, panel::ASG_LED3_POS, panel::ASG_LED4_POS, panel::ASG_LED5_POS, panel::ASG_LED6_POS, panel::ASG_LED7_POS, panel::ASG_LED8_POS };
const Vec LANE_POS[8] = { panel::LANE1_POS, panel::LANE2_POS, panel::LANE3_POS, panel::LANE4_POS, panel::LANE5_POS, panel::LANE6_POS, panel::LANE7_POS, panel::LANE8_POS };

} // namespace

struct Depreciation : Module {
	static const int ROWS = 5, COLS = 9, LANES = 8;
	enum ParamId {
		ROW_PARAM, COL_PARAM, REGMODE_PARAM, LOAD_PARAM, STORE_PARAM, BYPASS_PARAM, INPUT_PARAM, TRIM_PARAM, IN_PAD_PARAM, OUT_PAD_PARAM, CLK_DIV_PARAM,
		ATT_PARAM, ASG_PARAM = ATT_PARAM + LANES, CELL_PARAM = ASG_PARAM + LANES, PARAMS_LEN = CELL_PARAM + ROWS * COLS
	};
	enum InputId {
		IN_INPUT, IN_R_INPUT, CV_INPUT, MOD_INPUT = CV_INPUT + LANES, AT_INPUT, NOTE_INPUT, GATE_INPUT, SUSTAIN_INPUT, SOFT_INPUT, CLOCK_INPUT, RUN_INPUT, PGM_INPUT, BYPASS_CV_INPUT, INPUTS_LEN
	};
	enum OutputId { OUT_L_OUTPUT, OUT_R_OUTPUT, WET_L_OUTPUT, WET_R_OUTPUT, OUTPUTS_LEN };
	enum LightId { BYPASS_LED, ASG_LED, LIGHTS_LEN = ASG_LED + LANES };

	// ROMs (UI thread; guarded)
	std::mutex romMutex;
	pcm70::RomSet roms;
	std::string status = "LOAD ROMS";

	// the machine
	std::unique_ptr<pcm70::Voice> voice;               // audio thread only
	std::atomic<pcm70::Voice*> handover{nullptr};
	std::thread bootThread;
	std::atomic<bool> booting{false};
	double voiceRate = 0.0;
	pcm70::PanelLogic logic;                            // audio thread only (laneTarget is saved in the patch)

	// UI <-> audio
	std::mutex snapMutex;
	pcm70::PanelLogic::Snap snap;                       // guarded by snapMutex
	std::vector<uint8_t> battery;                       // the 8 KB RAM image; guarded by snapMutex
	std::string family;                                 // guarded by snapMutex
	std::mutex cmdMutex;
	std::vector<std::function<void(pcm70::Voice&)>> cmds;
	std::atomic<bool> cmdPending{false};

	// audio-thread bookkeeping
	int ctlCount = 0, snapCount = 0, ramCount = 0;
	float prevClock = 0.f, prevRun = 0.f;
	bool clockWas = false, runWas = false;

	Depreciation() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(ROW_PARAM, 0.f, 7.f, 0.f, "Program row (register row 0-4)")->snapEnabled = true;
		configParam(COL_PARAM, 0.f, 9.f, 0.f, "Program column")->snapEnabled = true;
		configSwitch(REGMODE_PARAM, 0.f, 1.f, 0.f, "LOAD / STORE act on", { "Programs (PGM)", "Registers (REG)" });
		configButton(LOAD_PARAM, "Load the chosen program or register");
		configButton(STORE_PARAM, "Store the running program as the chosen register");
		configButton(BYPASS_PARAM, "Bypass (footswitch)");
		configParam(INPUT_PARAM, 0.f, 1.f, 1.f, "Input level", "%", 0.f, 100.f);
		configParam(TRIM_PARAM, 1.f, 10.f, 5.f, "Full scale (the converter's limit)", " V peak");
		configSwitch(IN_PAD_PARAM, 0.f, 1.f, 0.f, "Input level", { "+4 dBu", "-20 dBV (15 dB less)" });
		configSwitch(OUT_PAD_PARAM, 0.f, 1.f, 0.f, "Output level", { "+4 dBu", "-20 dBV (24.7 dB less)" });
		configSwitch(CLK_DIV_PARAM, 0.f, 4.f, 4.f, "Clock edges per quarter note", { "1", "2", "4", "8", "24" });
		for (int i = 0; i < LANES; i++) {
			configParam(ATT_PARAM + i, -1.f, 1.f, 1.f, "CV " + std::to_string(i + 1) + " amount", "%", 0.f, 100.f);
			configButton(ASG_PARAM + i, "CV " + std::to_string(i + 1) + ": then touch a parameter to assign it (touch it again to release)");
			configLight(ASG_LED + i, "CV " + std::to_string(i + 1) + " assigned");
			configInput(CV_INPUT + i, "CV " + std::to_string(i + 1));
		}
		for (int r = 0; r < ROWS; r++) for (int c = 0; c < COLS; c++) {
			CellQuantity* q = configParam<CellQuantity>(CELL_PARAM + r * COLS + c, 0.f, 1.f, 0.5f, "Parameter " + std::to_string(r) + "." + std::to_string(c));
			q->module_ = this; q->r = r; q->c = c;
		}
		configLight(BYPASS_LED, "Bypassed");
		configInput(IN_INPUT, "Input"); configInput(IN_R_INPUT, "Input right (summed; the hardware is mono-in)");
		configInput(MOD_INPUT, "Mod wheel (0-10 V = CC 1)"); configInput(AT_INPUT, "Aftertouch (0-10 V)"); configInput(NOTE_INPUT, "Note (1 V/oct, 0 V = middle C)");
		configInput(GATE_INPUT, "Gate (velocity = voltage)"); configInput(SUSTAIN_INPUT, "Sustain pedal gate"); configInput(SOFT_INPUT, "Soft knob CV (0-10 V = its whole range)");
		configInput(CLOCK_INPUT, "Clock (V3 firmware only)"); configInput(RUN_INPUT, "Run / restart the clock measurement (V3 firmware only)");
		configInput(PGM_INPUT, "Register change (0.1 V per register; turns program change on)"); configInput(BYPASS_CV_INPUT, "Bypass gate");
		configOutput(OUT_L_OUTPUT, "Output left"); configOutput(OUT_R_OUTPUT, "Output right");
		configOutput(WET_L_OUTPUT, "Wet left (after the output filter, before the mix)"); configOutput(WET_R_OUTPUT, "Wet right (likewise)");
	}

	~Depreciation() {
		if (bootThread.joinable()) bootThread.join();
		delete handover.exchange(nullptr);
	}

	/** What a matrix knob is called and says right now: the firmware's own text. */
	struct CellQuantity : ParamQuantity {
		Depreciation* module_ = nullptr; int r = 0, c = 0;
		std::string getLabel() override {
			if (!module_) return ParamQuantity::getLabel();
			std::lock_guard<std::mutex> lock(module_->snapMutex);
			const std::string& n = module_->snap.name[r][c];
			return std::to_string(r) + "." + std::to_string(c) + (n.empty() ? std::string(" (not used by this program)") : " " + n);
		}
		std::string getDisplayValueString() override {
			if (!module_) return ParamQuantity::getDisplayValueString();
			std::lock_guard<std::mutex> lock(module_->snapMutex);
			const std::string& cap = module_->snap.caption[r][c];
			return cap.empty() ? std::to_string(module_->snap.word[r][c]) : cap;
		}
		std::string getUnit() override { return ""; }
	};

	// --- ROMs -------------------------------------------------------------------------------------
	void loadFiles(const std::vector<std::string>& paths) {
		std::vector<pcm70::Candidate> files;
		for (const std::string& p : paths) {
			std::vector<uint8_t> b = readFile(p);
			if (!b.empty()) files.push_back({ p, std::move(b) });
		}
		pcm70::RomSet s = pcm70::assemble(files);
		{
			std::lock_guard<std::mutex> lock(romMutex);
			roms = std::move(s);
			status = pcm70::statusLine(roms);
			INFO("Depreciation: %s", status.c_str());
		}
		boot();
	}

	void loadFolder(const std::string& dir) {
		std::vector<std::string> files;
		for (const std::string& f : system::getEntries(dir))
			if (system::isFile(f)) files.push_back(f);
		loadFiles(files);
	}

	std::vector<std::string> currentPaths() {
		std::vector<std::string> out;
		std::lock_guard<std::mutex> lock(romMutex);
		for (int r = 0; r < pcm70::ROLES; r++)
			if (roms.have(r)) out.push_back(roms.path[r]);
		return out;
	}

	void rememberRoms() {
		const std::vector<std::string> paths = currentPaths();
		const std::string path = settingsPath();
		system::createDirectories(system::getDirectory(path));
		json_t* root = system::isFile(path) ? json_load_file(path.c_str(), 0, NULL) : NULL;
		if (!root) root = json_object();
		json_t* arr = json_array();
		for (const std::string& p : paths) json_array_append_new(arr, json_string(p.c_str()));
		json_object_set_new(root, "depreciationRoms", arr);
		json_dump_file(root, path.c_str(), JSON_INDENT(2));
		json_decref(root);
	}

	// --- booting ----------------------------------------------------------------------------------
	void setStatus(const std::string& s) { std::lock_guard<std::mutex> lock(romMutex); status = s; }

	/** Build the machine from the images and the saved battery RAM and let it power up (about 9 s of machine time) on a worker; the audio thread takes it over
	    when it is ready. UI thread. */
	void boot(double rate = 0.0) {
		pcm70::RomSet set;
		{
			std::lock_guard<std::mutex> lock(romMutex);
			if (!roms.ok() || !roms.have(pcm70::U67) || !roms.have(pcm70::U48) || !roms.have(pcm70::U49)) return;
			set = roms;
			status = "POWERING ON";
		}
		if (bootThread.joinable()) bootThread.join();
		std::vector<uint8_t> ram;
		{
			std::lock_guard<std::mutex> lock(snapMutex);
			ram = battery; family = set.family;
		}
		if (rate <= 0.0) rate = APP->engine->getSampleRate();
		booting = true;
		bootThread = std::thread([this, set, ram, rate]() {
			std::unique_ptr<pcm70::Voice> v(new pcm70::Voice());
			if (ram.size() == 0x2000) v->setBatteryRam(ram.data());
			v->load(set.data[pcm70::U62].data(), set.data[pcm70::U62].size(), set.data[pcm70::U95].data(), set.data[pcm70::U95].size(), set.data[pcm70::U67].data(),
				set.data[pcm70::U48].data(), set.data[pcm70::U49].data(), set.family == "3.01");
			v->configure(rate);
			double l, r;
			const long long n = (long long)(9.6 * rate);
			for (long long i = 0; i < n; i++) v->process(0.0, l, r);        // the power-up routine runs against silence
			delete handover.exchange(v.release());
			voiceRate = rate;
			setStatus("");
			rememberRoms();
			booting = false;
		});
	}

	void onAdd(const AddEvent& e) override {
		{
			std::lock_guard<std::mutex> lock(romMutex);
			if (roms.ok() || roms.have(pcm70::U62)) return;     // a patch has already set the paths
		}
		const std::string path = settingsPath();
		json_t* root = system::isFile(path) ? json_load_file(path.c_str(), 0, NULL) : NULL;
		if (!root) return;
		std::vector<std::string> paths;
		if (json_t* arr = json_object_get(root, "depreciationRoms"))
			for (size_t i = 0; i < json_array_size(arr); i++) {
				json_t* s = json_array_get(arr, i);
				if (json_is_string(s)) paths.push_back(json_string_value(s));
			}
		json_decref(root);
		if (!paths.empty()) loadFiles(paths);
	}

	void ask(std::function<void(pcm70::Voice&)> f) {
		std::lock_guard<std::mutex> lock(cmdMutex);
		cmds.push_back(std::move(f));
		cmdPending = true;
	}

	/** Power-cycle on the given image (empty = a factory-fresh machine). UI thread. */
	void restart(const std::vector<uint8_t>& image) {
		{
			std::lock_guard<std::mutex> lock(snapMutex);
			battery = image;
		}
		restartRequested = true;                             // the audio thread drops the running machine on its next block
		boot();
	}
	std::atomic<bool> restartRequested{false};

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_t* arr = json_array();
		for (const std::string& p : currentPaths()) json_array_append_new(arr, json_string(p.c_str()));
		json_object_set_new(root, "romPaths", arr);          // paths only, never the images
		{
			std::lock_guard<std::mutex> lock(snapMutex);
			if (battery.size() == 0x2000) json_object_set_new(root, "batteryRam", json_string(base64(battery).c_str()));   // the registers and settings: the user's own, no ROM data
		}
		json_t* lanes = json_array();
		for (int i = 0; i < LANES; i++) json_array_append_new(lanes, json_integer(logic.laneTarget[i]));
		json_object_set_new(root, "laneTargets", lanes);
		return root;
	}

	void dataFromJson(json_t* root) override {
		if (json_t* l = json_object_get(root, "laneTargets"))
			for (int i = 0; i < LANES && i < (int)json_array_size(l); i++) logic.laneTarget[i] = (int)json_integer_value(json_array_get(l, i));
		if (json_t* b = json_object_get(root, "batteryRam"))
			if (json_is_string(b)) { std::vector<uint8_t> ram = unbase64(json_string_value(b)); if (ram.size() == 0x2000) { std::lock_guard<std::mutex> lock(snapMutex); battery = ram; } }
		std::vector<std::string> paths;
		if (json_t* arr = json_object_get(root, "romPaths"))
			for (size_t i = 0; i < json_array_size(arr); i++) {
				json_t* s = json_array_get(arr, i);
				if (json_is_string(s)) paths.push_back(json_string_value(s));
			}
		if (!paths.empty()) loadFiles(paths);
	}

	// --- audio ------------------------------------------------------------------------------------
	void process(const ProcessArgs& args) override {
		if (restartRequested.exchange(false)) voice.reset();
		if (voice && std::fabs(args.sampleRate - voiceRate) > 1.0) {          // the host rate changed: the converter stages are rebuilt, the RAM is kept
			{ std::lock_guard<std::mutex> lock(snapMutex); battery.assign(voice->batteryRam(), voice->batteryRam() + 0x2000); }
			voice.reset();
			boot(args.sampleRate);
		}
		if (!voice) {
			if (pcm70::Voice* h = handover.exchange(nullptr)) { voice.reset(h); voiceRate = args.sampleRate; ctlCount = 0; clockWas = runWas = false; }
		}
		if (!voice) {
			outputs[OUT_L_OUTPUT].setVoltage(0.f); outputs[OUT_R_OUTPUT].setVoltage(0.f); outputs[WET_L_OUTPUT].setVoltage(0.f); outputs[WET_R_OUTPUT].setVoltage(0.f);
			return;
		}
		pcm70::Voice& v = *voice;
		if (cmdPending.load(std::memory_order_relaxed)) {
			std::unique_lock<std::mutex> lock(cmdMutex, std::try_to_lock);
			if (lock) { std::vector<std::function<void(pcm70::Voice&)>> run; run.swap(cmds); cmdPending = false; lock.unlock(); for (auto& f : run) f(v); }
		}
		// clock and run: edges found per sample
		if (inputs[CLOCK_INPUT].isConnected()) {
			const float c = inputs[CLOCK_INPUT].getVoltage(); if (c > 1.f && !clockWas) logic.clockEdge(v); if (c > 1.f) clockWas = true; else if (c < 0.2f) clockWas = false;
		}
		if (inputs[RUN_INPUT].isConnected()) {
			const float c = inputs[RUN_INPUT].getVoltage(); if (c > 1.f && !runWas) logic.runEdge(v); if (c > 1.f) runWas = true; else if (c < 0.2f) runWas = false;
		}
		if (--ctlCount <= 0) {
			const int n = std::max(1, (int)(args.sampleRate / 1000.f)); ctlCount = n;
			pcm70::PanelLogic::In in; in.dt = n * args.sampleTime;
			in.row = params[ROW_PARAM].getValue(); in.col = params[COL_PARAM].getValue(); in.regMode = params[REGMODE_PARAM].getValue() > 0.5f;
			in.load = params[LOAD_PARAM].getValue() > 0.5f; in.store = params[STORE_PARAM].getValue() > 0.5f; in.bypass = params[BYPASS_PARAM].getValue() > 0.5f;
			for (int r = 0; r < ROWS; r++) for (int c = 0; c < COLS; c++) in.knob[r][c] = params[CELL_PARAM + r * COLS + c].getValue();
			for (int i = 0; i < LANES; i++) {
				in.att[i] = params[ATT_PARAM + i].getValue(); in.asg[i] = params[ASG_PARAM + i].getValue() > 0.5f;
				in.cvOn[i] = inputs[CV_INPUT + i].isConnected(); in.cv[i] = inputs[CV_INPUT + i].getVoltageSum();
			}
			auto jack = [&](int id, float& val, bool& on) { on = inputs[id].isConnected(); val = on ? inputs[id].getVoltageSum() : 0.f; };
			jack(MOD_INPUT, in.mod, in.modOn); jack(AT_INPUT, in.at, in.atOn); jack(NOTE_INPUT, in.note, in.noteOn); jack(GATE_INPUT, in.gate, in.gateOn);
			jack(SUSTAIN_INPUT, in.sust, in.sustOn); jack(SOFT_INPUT, in.soft, in.softOn); jack(PGM_INPUT, in.pgm, in.pgmOn); jack(BYPASS_CV_INPUT, in.byp, in.bypOn);
			in.clkDiv = (int)(params[CLK_DIV_PARAM].getValue() + 0.5f);
			in.input = params[INPUT_PARAM].getValue(); in.inPad20 = params[IN_PAD_PARAM].getValue() > 0.5f; in.outPad20 = params[OUT_PAD_PARAM].getValue() > 0.5f; in.trimVolts = params[TRIM_PARAM].getValue();
			pcm70::PanelLogic::Out out;
			logic.control(v, in, out);
			for (int r = 0; r < ROWS; r++) for (int c = 0; c < COLS; c++) if (out.setKnob[r][c]) params[CELL_PARAM + r * COLS + c].setValue(out.knob[r][c]);
			for (int i = 0; i < LANES; i++) lights[ASG_LED + i].setBrightness(out.asgLight[i]);
			lights[BYPASS_LED].setBrightness(out.bypassLed ? 1.f : 0.f);
		}
		// the signal
		float vin = 0.f; const bool a = inputs[IN_INPUT].isConnected(), b = inputs[IN_R_INPUT].isConnected();
		if (a) vin += inputs[IN_INPUT].getVoltageSum(); if (b) vin += inputs[IN_R_INPUT].getVoltageSum();
		double l, r; v.process(vin, l, r);
		outputs[OUT_L_OUTPUT].setVoltage((float)l); outputs[OUT_R_OUTPUT].setVoltage((float)r);
		outputs[WET_L_OUTPUT].setVoltage((float)v.wetL); outputs[WET_R_OUTPUT].setVoltage((float)v.wetR);
		// what the UI shows
		if (--snapCount <= 0) {
			snapCount = std::max(1, (int)(args.sampleRate / 50.f));
			std::unique_lock<std::mutex> lock(snapMutex, std::try_to_lock);
			if (lock) {
				logic.snapshot(v, snap);
				if (--ramCount <= 0) { ramCount = 50; battery.assign(v.batteryRam(), v.batteryRam() + 0x2000); }
			}
		}
	}

};

// --- widgets ------------------------------------------------------------------------------

namespace {

/** The read-out well: the machine's own 16-digit display, the last parameter touched, the headroom bar, the firmware's load, and what the module is doing. */
struct WellDisplay : widget::Widget {
	Depreciation* module = nullptr;

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) return;
		NVGcontext* vg = args.vg;
		const float mmx = mm2px(1.f);
		pcm70::PanelLogic::Snap s; std::string status, family; bool ok = false;
		if (module) {
			{ std::lock_guard<std::mutex> lock(module->snapMutex); s = module->snap; family = module->family; }
			{ std::lock_guard<std::mutex> lock(module->romMutex); status = module->status; ok = module->roms.ok(); }
		}
		const bool live = module && status.empty();
		// the 16 digits
		const float dx = 6.f * mmx, dy = 1.4f * mmx, dw = 6.2f * mmx, dh = 9.6f * mmx;
		std::string txt = live ? s.display : (module ? status : std::string("DEPRECIATION"));
		std::vector<std::pair<char, bool>> digits;
		for (char ch : txt) { if (ch == '.' && !digits.empty() && !digits.back().second && live) digits.back().second = true; else digits.push_back({ ch, false }); }
		while (digits.size() < 16) digits.push_back({ ' ', false });
		const panel::TextStyle big(panel::Face::Mono, 8.6f * mmx, live ? panel::LIME : (ok ? panel::SAGE : panel::CLAY), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		for (int i = 0; i < 16 && i < (int)digits.size(); i++) {
			const float x = dx + i * dw;
			nvgBeginPath(vg); nvgRoundedRect(vg, x + 0.25f * mmx, dy, dw - 0.5f * mmx, dh, 0.5f * mmx); nvgFillColor(vg, panel::alpha(panel::LIME, 0.05f)); nvgFill(vg);
			const char buf[2] = { digits[i].first, 0 };
			if (digits[i].first != ' ') panel::text(vg, big, x + dw / 2 - (digits[i].second ? 0.4f * mmx : 0.f), dy + dh / 2 + 0.3f * mmx, buf);
			if (digits[i].second) { nvgBeginPath(vg); nvgCircle(vg, x + dw - 0.9f * mmx, dy + dh - 1.0f * mmx, 0.45f * mmx); nvgFillColor(vg, panel::LIME); nvgFill(vg); }
		}
		const panel::TextStyle small(panel::Face::Mono, 3.5f * mmx, panel::SAGE, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		// the last parameter touched
		if (live && s.touched >= 0 && s.sinceTouch < 4.0) {
			const int r = s.touched / 9, c = s.touched % 9;
			panel::text(vg, small.inked(panel::LIME), dx, dy + dh + 2.0f * mmx, (std::to_string(r) + "." + std::to_string(c) + "  " + s.name[r][c] + "  " + s.caption[r][c]).c_str());
		} else if (live) {
			panel::text(vg, small, dx, dy + dh + 2.0f * mmx, family.empty() ? "" : ("PCM 70 V" + family).c_str());
		}
		// headroom bar (the detector the firmware's gates read), 0 / -6 / -12 / -18 / -24 dB
		const float hx = 112.f * mmx, hy = 3.f * mmx;
		panel::text(vg, small, hx, hy - 0.6f * mmx, "HEADROOM");
		for (int i = 0; i < 5; i++) {
			const bool lit = live && s.leds >= 5 - i;
			nvgBeginPath(vg); nvgRoundedRect(vg, hx + i * 5.2f * mmx, hy + 1.2f * mmx, 4.4f * mmx, 2.6f * mmx, 0.4f * mmx);
			nvgFillColor(vg, panel::alpha(i == 4 ? panel::CLAY : panel::LIME, lit ? 1.f : 0.14f)); nvgFill(vg);
		}
		panel::text(vg, small.sized(2.9f * mmx), hx, hy + 6.4f * mmx, "-24  -18  -12  -6   0");
		// the firmware's load, and the fault light
		const float lx = 144.f * mmx;
		panel::text(vg, small, lx, hy - 0.6f * mmx, "LOAD");
		nvgBeginPath(vg); nvgRoundedRect(vg, lx, hy + 1.2f * mmx, 24.f * mmx, 2.6f * mmx, 0.4f * mmx); nvgFillColor(vg, panel::alpha(panel::LIME, 0.14f)); nvgFill(vg);
		if (live) { nvgBeginPath(vg); nvgRoundedRect(vg, lx, hy + 1.2f * mmx, 24.f * mmx * std::min(1.0, s.load / 0.7), 2.6f * mmx, 0.4f * mmx); nvgFillColor(vg, s.fault ? panel::CLAY : panel::LIME); nvgFill(vg); }
		if (live && s.fault) panel::text(vg, small.inked(panel::CLAY), lx, hy + 6.4f * mmx, "FIRMWARE STALLED");
		// what the module is doing
		if (!live && module) panel::text(vg, small.inked(ok ? panel::SAGE : panel::CLAY), 112.f * mmx, 9.5f * mmx, ok ? "POWER-UP TAKES ABOUT 9 SECONDS" : "RIGHT-CLICK: LOAD ROM FOLDER");
	}
};

/** The 45 plates under the matrix knobs: each parameter's own name, and its printed value for a moment after a touch; the knobs the program does not use are
    dimmed. The lane plates name what each CV lane drives. */
struct MatrixPlates : widget::Widget {
	Depreciation* module = nullptr;
	panel::FittedText fitted[5 * 9 + 8];

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) return;
		NVGcontext* vg = args.vg;
		pcm70::PanelLogic::Snap s;
		if (module) { std::lock_guard<std::mutex> lock(module->snapMutex); s = module->snap; }
		const float pw = mm2px(panel::READOUT_W), ph = mm2px(panel::READOUT_H);
		const panel::TextStyle st(panel::Face::Mono, ph * 0.80f, panel::SAGE, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		for (int r = 0; r < 5; r++) for (int c = 0; c < 9; c++) {
			const Vec p = mm2px(CAP_POS[r][c]) - box.pos;
			const bool valid = s.valid[r][c];
			std::string t = valid ? s.name[r][c] : "";
			NVGcolor ink = panel::SAGE;
			if (valid && s.touched == r * 9 + c && s.sinceTouch < 2.0 && !s.caption[r][c].empty()) { t = s.caption[r][c]; ink = panel::LIME; }
			else if (!valid) t = "--";
			panel::text(vg, st.inked(valid ? ink : panel::alpha(panel::SAGE, 0.35f)), p.x, p.y, fitted[r * 9 + c].get(vg, st.inked(valid ? ink : panel::alpha(panel::SAGE, 0.35f)), t, pw - 1.f).c_str());
			if (!valid) {                                                    // a knob the running program does not have
				const Vec k = mm2px(CELL_POS[r][c]) - box.pos;
				nvgBeginPath(vg); nvgCircle(vg, k.x, k.y, mm2px(3.9f)); nvgFillColor(vg, nvgRGBAf(0.f, 0.f, 0.f, 0.45f)); nvgFill(vg);
			}
		}
		for (int i = 0; i < 8; i++) {
			const Vec p = mm2px(LANE_POS[i]) - box.pos;
			panel::text(vg, st.inked(s.laneName[i] == "TOUCH" ? panel::LIME : panel::SAGE), p.x, p.y, fitted[45 + i].get(vg, st, s.laneName[i].empty() ? std::string("--") : s.laneName[i], pw - 1.f).c_str());
		}
	}
};

} // namespace

struct DepreciationWidget : ModuleWidget {
	DepreciationWidget(Depreciation* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Depreciation.svg")));
		panel::addScrews(this);
		panel::addLabels(this);

		WellDisplay* well = new WellDisplay;
		well->module = module;
		well->box.pos = panel::mm(panel::GLASS_X, panel::GLASS_Y);
		well->box.size = panel::mm(panel::GLASS_W, panel::GLASS_H);
		addChild(well);

		addParam(createParamCentered<RoundBlackKnob>(panel::mm(panel::ROW_POS.x, panel::ROW_POS.y), module, Depreciation::ROW_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(panel::mm(panel::COL_POS.x, panel::COL_POS.y), module, Depreciation::COL_PARAM));
		addParam(createParamCentered<CKSS>(panel::mm(panel::REGMODE_POS.x, panel::REGMODE_POS.y), module, Depreciation::REGMODE_PARAM));
		addParam(createParamCentered<VCVButton>(panel::mm(panel::LOAD_POS.x, panel::LOAD_POS.y), module, Depreciation::LOAD_PARAM));
		addParam(createParamCentered<VCVButton>(panel::mm(panel::STORE_POS.x, panel::STORE_POS.y), module, Depreciation::STORE_PARAM));
		addParam(createParamCentered<VCVButton>(panel::mm(panel::BYPASS_POS.x, panel::BYPASS_POS.y), module, Depreciation::BYPASS_PARAM));
		addChild(createLightCentered<SmallLight<panel::ClayLight>>(panel::mm(panel::BYPASS_LED_POS.x, panel::BYPASS_LED_POS.y), module, Depreciation::BYPASS_LED));
		addParam(createParamCentered<RoundBlackKnob>(panel::mm(panel::INPUT_POS.x, panel::INPUT_POS.y), module, Depreciation::INPUT_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(panel::mm(panel::TRIM_POS.x, panel::TRIM_POS.y), module, Depreciation::TRIM_PARAM));
		addParam(createParamCentered<CKSS>(panel::mm(panel::IN_PAD_POS.x, panel::IN_PAD_POS.y), module, Depreciation::IN_PAD_PARAM));
		addParam(createParamCentered<CKSS>(panel::mm(panel::OUT_PAD_POS.x, panel::OUT_PAD_POS.y), module, Depreciation::OUT_PAD_PARAM));
		addParam(createParamCentered<Trimpot>(panel::mm(panel::CLK_DIV_POS.x, panel::CLK_DIV_POS.y), module, Depreciation::CLK_DIV_PARAM));

		for (int r = 0; r < 5; r++) for (int c = 0; c < 9; c++)
			addParam(createParamCentered<Trimpot>(panel::mm(CELL_POS[r][c].x, CELL_POS[r][c].y), module, Depreciation::CELL_PARAM + r * 9 + c));
		for (int i = 0; i < 8; i++) {
			addInput(createInputCentered<panel::PortIn>(panel::mm(CV_POS[i].x, CV_POS[i].y), module, Depreciation::CV_INPUT + i));
			addParam(createParamCentered<Trimpot>(panel::mm(ATT_POS[i].x, ATT_POS[i].y), module, Depreciation::ATT_PARAM + i));
			addParam(createParamCentered<VCVButton>(panel::mm(ASG_POS[i].x, ASG_POS[i].y), module, Depreciation::ASG_PARAM + i));
			addChild(createLightCentered<SmallLight<panel::LimeLight>>(panel::mm(ASG_LED_POS[i].x, ASG_LED_POS[i].y), module, Depreciation::ASG_LED + i));
		}
		MatrixPlates* plates = new MatrixPlates;
		plates->module = module;
		plates->box.pos = Vec(0.f, 0.f);
		plates->box.size = panel::mm(panel::W, panel::H);
		addChild(plates);

		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::MOD_POS.x, panel::MOD_POS.y), module, Depreciation::MOD_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::AT_POS.x, panel::AT_POS.y), module, Depreciation::AT_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::NOTE_POS.x, panel::NOTE_POS.y), module, Depreciation::NOTE_INPUT));
		addInput(createInputCentered<panel::PortTrigIn>(panel::mm(panel::GATE_POS.x, panel::GATE_POS.y), module, Depreciation::GATE_INPUT));
		addInput(createInputCentered<panel::PortTrigIn>(panel::mm(panel::SUSTAIN_POS.x, panel::SUSTAIN_POS.y), module, Depreciation::SUSTAIN_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::SOFT_POS.x, panel::SOFT_POS.y), module, Depreciation::SOFT_INPUT));
		addInput(createInputCentered<panel::PortTrigIn>(panel::mm(panel::CLOCK_POS.x, panel::CLOCK_POS.y), module, Depreciation::CLOCK_INPUT));
		addInput(createInputCentered<panel::PortTrigIn>(panel::mm(panel::RUN_POS.x, panel::RUN_POS.y), module, Depreciation::RUN_INPUT));
		addInput(createInputCentered<panel::PortInMain>(panel::mm(panel::IN_POS.x, panel::IN_POS.y), module, Depreciation::IN_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::IN_R_POS.x, panel::IN_R_POS.y), module, Depreciation::IN_R_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::PGM_POS.x, panel::PGM_POS.y), module, Depreciation::PGM_INPUT));
		addInput(createInputCentered<panel::PortTrigIn>(panel::mm(panel::BYPASS_CV_POS.x, panel::BYPASS_CV_POS.y), module, Depreciation::BYPASS_CV_INPUT));
		addOutput(createOutputCentered<panel::PortOutMain>(panel::mm(panel::OUT_L_POS.x, panel::OUT_L_POS.y), module, Depreciation::OUT_L_OUTPUT));
		addOutput(createOutputCentered<panel::PortOutMain>(panel::mm(panel::OUT_R_POS.x, panel::OUT_R_POS.y), module, Depreciation::OUT_R_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::WET_L_POS.x, panel::WET_L_POS.y), module, Depreciation::WET_L_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::WET_R_POS.x, panel::WET_R_POS.y), module, Depreciation::WET_R_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		Depreciation* m = static_cast<Depreciation*>(module);
		if (!m) return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("PCM 70 ROMs (not included)"));
		{
			std::lock_guard<std::mutex> lock(m->romMutex);
			for (int r = 0; r < pcm70::ROLES; r++)
				menu->addChild(createMenuLabel(std::string(pcm70::roleName(r)) + ": " +
					(m->roms.have(r) ? system::getFilename(m->roms.path[r]) : std::string("none"))));
			if (m->roms.ok())
				menu->addChild(createMenuLabel("Software version " + m->roms.family + (m->roms.family == "3.01" ? "" : " (ignores MIDI clock)")));
			else
				menu->addChild(createMenuLabel(m->status));
		}
		menu->addChild(createMenuItem("Load ROM folder...", "", [=]() {
			char* dir = osdialog_file(OSDIALOG_OPEN_DIR, NULL, NULL, NULL);
			if (!dir) return;
			m->loadFolder(dir);
			std::free(dir);
		}));
		menu->addChild(createMenuItem("Forget ROMs", "", [=]() { m->loadFiles({}); }));

		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("MIDI and system (stored in the unit's battery RAM)"));
		std::vector<uint8_t> ram; { std::lock_guard<std::mutex> lock(m->snapMutex); ram = m->battery; }
		const bool have = ram.size() == 0x2000; const bool v3 = [&]() { std::lock_guard<std::mutex> lock(m->snapMutex); return m->family == "3.01"; }();
		auto byteAt = [&](unsigned a) { return have ? ram[a - 0x8000] : 0; };
		menu->addChild(createSubmenuItem("MIDI channel", have ? std::to_string(byteAt(0x98B2) + 1) : "1", [=](Menu* sub) {
			for (int ch = 1; ch <= 16; ch++) sub->addChild(createCheckMenuItem(std::to_string(ch), "", [=]() { return have && ((ram[0x98B2 - 0x8000] & 15) + 1) == ch; }, [=]() { m->ask([=](pcm70::Voice& v) { v.control.setMidiChannel(ch); v.midi.channel = ch - 1; }); }));
		}));
		menu->addChild(createCheckMenuItem("OMNI mode", "", [=]() { return have && ram[0x98B3 - 0x8000] != 0; }, [=]() { const bool on = !(have && ram[0x98B3 - 0x8000] != 0); m->ask([=](pcm70::Voice& v) { v.control.setOmni(on); }); }));
		menu->addChild(createCheckMenuItem("Program change enabled", "", [=]() { return have && ram[0x98B8 - 0x8000] != 0; }, [=]() { const bool on = !(have && ram[0x98B8 - 0x8000] != 0); m->ask([=](pcm70::Voice& v) { v.control.setProgramChange(on); }); }));
		menu->addChild(createCheckMenuItem("Auto load", "", [=]() { return have && ram[0x993B - 0x8000] != 0; }, [=]() { const bool on = !(have && ram[0x993B - 0x8000] != 0); m->ask([=](pcm70::Voice& v) { v.control.setAutoLoad(on); }); }));
		const unsigned prot = v3 ? 0x9C6C : 0x9C6A;
		menu->addChild(createCheckMenuItem("Memory protect", "", [=]() { return have && ram[prot - 0x8000] != 0; }, [=]() { const bool on = !(have && ram[prot - 0x8000] != 0); m->ask([=](pcm70::Voice& v) { v.control.setMemoryProtect(on); }); }));

		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Registers"));
		menu->addChild(createMenuItem("Import SysEx bank...", "", [=]() {
			char* path = osdialog_file(OSDIALOG_OPEN, NULL, NULL, osdialog_filters_parse("SysEx:syx,mid"));
			if (!path) return;
			const std::vector<uint8_t> bytes = readFile(path);
			std::free(path);
			std::vector<std::vector<uint8_t>> msgs; std::vector<uint8_t> cur;
			for (uint8_t b : bytes) { if (b == 0 && cur.empty()) continue; cur.push_back(b); if (b == 0xF7) { msgs.push_back(cur); cur.clear(); } }
			m->ask([=](pcm70::Voice& v) { for (const auto& g : msgs) v.midi.sysex(g); });
		}));
		menu->addChild(createMenuItem("Export SysEx bank...", "", [=]() {
			if (!have) return;
			char* path = osdialog_file(OSDIALOG_SAVE, NULL, "PCM70-registers.syx", osdialog_filters_parse("SysEx:syx"));
			if (!path) return;
			std::vector<uint8_t> out;
			for (int n = 0; n < 50; n++) {
				uint8_t data[167]; pcm70::batram::get(ram.data(), n, data);
				if (data[0] == 0) continue;                                     // unused register
				const std::vector<uint8_t> msg = pcm70::Midi::bulk(data, n, true); out.insert(out.end(), msg.begin(), msg.end());
			}
			std::ofstream f(path, std::ios::binary); f.write((const char*)out.data(), (std::streamsize)out.size());
			std::free(path);
		}));
		menu->addChild(createMenuItem("Power cycle", "", [=]() { std::vector<uint8_t> r; { std::lock_guard<std::mutex> lock(m->snapMutex); r = m->battery; } m->restart(r); }));
		menu->addChild(createMenuItem("Clear memory (factory fresh)", "", [=]() { m->restart(std::vector<uint8_t>()); }));
	}
};

Model* modelDepreciation = createModel<Depreciation, DepreciationWidget>("Depreciation");

// Depreciation -- a Lexicon PCM 70 digital effects processor, running its own firmware.
//
// The five firmware images are Lexicon's and are not distributed: the context menu loads them from wherever the user keeps them, the patch remembers the paths
// and never the bytes, and the panel says what it found. The machine (two Z80s, the HSP signal processor, the analog converter stages) lives in src/Pcm70*.hpp
// and is tested without Rack; the panel's meaning (what each control does to it) is src/Pcm70Panel.hpp, tested the same way. This file is only the Rack side:
// parameters in, voltages out, a read-out well, and the menus for what is not a control.
//
// There are no CV lanes: eight of them were each a stream of edits into the master Z80 through the firmware's own soft-knob routine, and that routine is slow
// enough that the machine spent most of its time answering them. The ids they held are kept (below) so a saved patch's cables and knobs stay where they were.
//
// There are no CV lanes: eight of them were each a stream of edits into the master Z80 through the firmware's own soft-knob routine, and that routine is slow
// enough that the machine spent most of its time answering them. The ids they held are kept (below) so a saved patch's cables and knobs stay where they were.
//
// Threads: the machine is built and booted (about nine seconds of its own time) on a worker, then handed to the audio thread, which is the only thing that
// touches it afterwards. The UI reads a snapshot the audio thread leaves under a lock it never waits on, and asks for anything it wants done (bank import,
// configuration bytes) through a queue the audio thread drains.
#include "../plugin.hpp"
#include "Panel.hpp"
#include "Roms.hpp"
#include "../Pcm70Panel.hpp"
#include "../Pcm70BatRam.hpp"
#include "../Pcm70Names.hpp"
#include "../Pcm70Names.hpp"

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

#define ROWFIELD(r) { panel::FIELD_P##r##0, panel::FIELD_P##r##1, panel::FIELD_P##r##2, panel::FIELD_P##r##3, panel::FIELD_P##r##4, panel::FIELD_P##r##5, panel::FIELD_P##r##6, panel::FIELD_P##r##7, panel::FIELD_P##r##8 }
// the matrix's cells on the read-out, row by row: where each is drawn and where it is grabbed
const Rect CELL_FIELD[5][9] = { ROWFIELD(0), ROWFIELD(1), ROWFIELD(2), ROWFIELD(3), ROWFIELD(4) };

} // namespace

struct Depreciation : Module {
	static const int ROWS = 5, COLS = 9, LANES = 8;       // LANES: the retired CV lanes' ids, held so old patches' cables and knobs keep their meaning
	enum ParamId {
		SLOT_PARAM, COL_PARAM_RETIRED, REGMODE_PARAM, LOAD_PARAM, STORE_PARAM, BYPASS_PARAM, INPUT_PARAM, TRIM_PARAM, IN_PAD_PARAM, OUT_PAD_PARAM, CLK_DIV_PARAM,
		LANE_PARAMS_RETIRED, CELL_PARAM = LANE_PARAMS_RETIRED + 2 * LANES, OUT_LEVEL_PARAM = CELL_PARAM + ROWS * COLS, PARAMS_LEN
	};
	enum InputId {
		IN_INPUT, IN_R_INPUT, CV_INPUTS_RETIRED, MOD_INPUT = CV_INPUTS_RETIRED + LANES, AT_INPUT, NOTE_INPUT, GATE_INPUT, SUSTAIN_INPUT, SOFT_INPUT, CLOCK_INPUT, RUN_INPUT, PGM_INPUT, BYPASS_CV_INPUT, INPUTS_LEN
	};
	enum OutputId { OUT_L_OUTPUT, OUT_R_OUTPUT, WET_L_OUTPUT, WET_R_OUTPUT, OUTPUTS_LEN };
	enum LightId { BYPASS_LED, LANE_LIGHTS_RETIRED, LIGHTS_LEN = LANE_LIGHTS_RETIRED + LANES };

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
	pcm70::PanelLogic logic;                            // audio thread only

	// UI <-> audio
	std::mutex snapMutex;
	pcm70::PanelLogic::Snap snap;                       // guarded by snapMutex
	std::vector<uint8_t> battery;                       // the 8 KB RAM image; guarded by snapMutex
	pcm70::ProgramNames names;                          // the factory programs' names, read from a scratch machine after power-up; guarded by snapMutex
	std::atomic<bool> cancelNames{false};
	std::string family;                                 // guarded by snapMutex
	std::mutex cmdMutex;
	std::vector<std::function<void(pcm70::Voice&)>> cmds;
	std::atomic<bool> cmdPending{false};

	// audio-thread bookkeeping
	int ctlCount = 0, snapCount = 0, ramCount = 0;
	int lastFactoryCached = pcm70::PanelLogic::FACTORY_SLOTS - 1; bool factoryMissing[pcm70::ProgramNames::SLOTS] = {};     // from `names`, refreshed with the snapshot
	float prevClock = 0.f, prevRun = 0.f;
	bool clockWas = false, runWas = false;

	Depreciation() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		SlotQuantity* sq = configParam<SlotQuantity>(SLOT_PARAM, 0.f, (float)(pcm70::PanelLogic::FACTORY_SLOTS - 1), 0.f, "Preset");
		sq->module_ = this; sq->snapEnabled = true;
		configSwitch(REGMODE_PARAM, 0.f, 1.f, 0.f, "Preset bank", { "Factory programs (the machine's own effects)", "User registers (your stored settings)" });
		configButton(LOAD_PARAM, "Load the chosen preset");
		configButton(STORE_PARAM, "Store the running program in the chosen user register");
		configButton(BYPASS_PARAM, "Bypass (footswitch)");
		configParam(INPUT_PARAM, 0.f, 1.f, 1.f, "Input level", "%", 0.f, 100.f);
		configParam(TRIM_PARAM, 1.f, 10.f, 5.f, "Full scale (the converter's limit)", " V peak");
		// make-up gain after the machine's output stage (OUT only; the WET jacks stay as the machine has them). Unity by default: the emulation is untouched
		configParam(OUT_LEVEL_PARAM, 0.f, 4.f, 1.f, "Output level (OUT L / OUT R only)", " dB", -10.f, 20.f);
		configSwitch(IN_PAD_PARAM, 0.f, 1.f, 0.f, "Input level", { "+4 dBu", "-20 dBV (15 dB less)" });
		configSwitch(OUT_PAD_PARAM, 0.f, 1.f, 0.f, "Output level", { "+4 dBu", "-20 dBV (24.7 dB less)" });
		configSwitch(CLK_DIV_PARAM, 0.f, 4.f, 4.f, "Clock edges per quarter note", { "1", "2", "4", "8", "24" });
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
		cancelNames = true;
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

	/** What the preset selector points at, in words: "FACTORY 13  MIDI MOD PAN", "USER 07  SINGLE DELAY", "USER 01  EMPTY". The slot number is 10 x row + column, so
	    13 is the firmware's own 1.3. UI thread. */
	std::string slotText(bool user, int slot, bool* empty = nullptr) {
		char num[8]; std::snprintf(num, sizeof num, "%02d", slot);
		std::lock_guard<std::mutex> lock(snapMutex);
		std::string nm; bool none = false;
		if (user) {
			if (battery.size() == 0x2000) nm = pcm70::PanelLogic::registerName(battery.data(), slot < 50 ? slot : 49);
			none = nm.empty();
			if (none) nm = battery.size() == 0x2000 ? "EMPTY" : "";
		} else if (slot >= 0 && slot < pcm70::ProgramNames::SLOTS) {
			nm = names.name[slot]; none = names.done && nm.empty();
			if (none) nm = "NO PROGRAM HERE";
		}
		if (empty) *empty = none;
		return std::string(user ? "USER " : "FACTORY ") + num + (nm.empty() ? "" : "  " + nm);
	}

	/** The selector: its range follows the bank (50 registers, or the programs the firmware has) and its tooltip says what it points at. */
	struct SlotQuantity : ParamQuantity {
		Depreciation* module_ = nullptr;
		float getMaxValue() override {
			if (!module_) return ParamQuantity::getMaxValue();
			return module_->params[REGMODE_PARAM].getValue() > 0.5f ? (float)(pcm70::PanelLogic::USER_SLOTS - 1) : (float)module_->lastFactoryUi();
		}
		std::string getDisplayValueString() override {
			if (!module_) return ParamQuantity::getDisplayValueString();
			return module_->slotText(module_->params[REGMODE_PARAM].getValue() > 0.5f, (int)std::lround(getValue()));
		}
		std::string getUnit() override { return ""; }
	};
	/** The highest FACTORY slot that holds a program (all 70 until the names are read). UI thread. */
	int lastFactoryUi() {
		std::lock_guard<std::mutex> lock(snapMutex);
		if (names.done) for (int s = pcm70::ProgramNames::SLOTS - 1; s >= 0; s--) if (!names.name[s].empty()) return s;
		return pcm70::PanelLogic::FACTORY_SLOTS - 1;
	}

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
		cancelNames = true;                                  // a name harvest from the previous set stops at its next sample
		if (bootThread.joinable()) bootThread.join();
		cancelNames = false;
		std::vector<uint8_t> ram;
		{
			std::lock_guard<std::mutex> lock(snapMutex);
			ram = battery; family = set.family; names = pcm70::ProgramNames();
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
			// the factory names: a scratch machine, off the audio thread, a few seconds of CPU
			pcm70::ProgramNames found = pcm70::ProgramNames::harvest(set.data[pcm70::U62], set.data[pcm70::U95], set.data[pcm70::U67], set.data[pcm70::U48], set.data[pcm70::U49],
				set.family == "3.01", cancelNames);
			if (found.done) { std::lock_guard<std::mutex> lock(snapMutex); names = found; }
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
		return root;
	}

	void dataFromJson(json_t* root) override {
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
			if (pcm70::Voice* h = handover.exchange(nullptr)) { voice.reset(h); voiceRate = args.sampleRate; ctlCount = 0; clockWas = runWas = false; logic.powerUp(); }
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
			in.regMode = params[REGMODE_PARAM].getValue() > 0.5f;
			{
				// the selector's range follows the bank: coming back from 69 to USER must not leave it pointing past register 49
				const float top = (float)(in.regMode ? pcm70::PanelLogic::USER_SLOTS - 1 : lastFactoryCached);
				if (params[SLOT_PARAM].getValue() > top) params[SLOT_PARAM].setValue(top);
				in.slot = (int)std::lround(params[SLOT_PARAM].getValue());
				in.programMissing = !in.regMode && in.slot >= 0 && in.slot < pcm70::ProgramNames::SLOTS && factoryMissing[in.slot];
			}
			in.load = params[LOAD_PARAM].getValue() > 0.5f; in.store = params[STORE_PARAM].getValue() > 0.5f; in.bypass = params[BYPASS_PARAM].getValue() > 0.5f;
			for (int r = 0; r < ROWS; r++) for (int c = 0; c < COLS; c++) in.knob[r][c] = params[CELL_PARAM + r * COLS + c].getValue();
			auto jack = [&](int id, float& val, bool& on) { on = inputs[id].isConnected(); val = on ? inputs[id].getVoltageSum() : 0.f; };
			jack(MOD_INPUT, in.mod, in.modOn); jack(AT_INPUT, in.at, in.atOn); jack(NOTE_INPUT, in.note, in.noteOn); jack(GATE_INPUT, in.gate, in.gateOn);
			jack(SUSTAIN_INPUT, in.sust, in.sustOn); jack(SOFT_INPUT, in.soft, in.softOn); jack(PGM_INPUT, in.pgm, in.pgmOn); jack(BYPASS_CV_INPUT, in.byp, in.bypOn);
			in.clkDiv = (int)(params[CLK_DIV_PARAM].getValue() + 0.5f);
			in.input = params[INPUT_PARAM].getValue(); in.inPad20 = params[IN_PAD_PARAM].getValue() > 0.5f; in.outPad20 = params[OUT_PAD_PARAM].getValue() > 0.5f; in.trimVolts = params[TRIM_PARAM].getValue();
			pcm70::PanelLogic::Out out;
			logic.control(v, in, out);
			for (int r = 0; r < ROWS; r++) for (int c = 0; c < COLS; c++) if (out.setKnob[r][c]) params[CELL_PARAM + r * COLS + c].setValue(out.knob[r][c]);
			lights[BYPASS_LED].setBrightness(out.bypassLed ? 1.f : 0.f);
		}
		// the signal
		float vin = 0.f; const bool a = inputs[IN_INPUT].isConnected(), b = inputs[IN_R_INPUT].isConnected();
		if (a) vin += inputs[IN_INPUT].getVoltageSum(); if (b) vin += inputs[IN_R_INPUT].getVoltageSum();
		double l, r; v.process(vin, l, r);
		const float og = params[OUT_LEVEL_PARAM].getValue();
		outputs[OUT_L_OUTPUT].setVoltage((float)l * og); outputs[OUT_R_OUTPUT].setVoltage((float)r * og);
		outputs[WET_L_OUTPUT].setVoltage((float)v.wetL); outputs[WET_R_OUTPUT].setVoltage((float)v.wetR);
		// what the UI shows
		if (--snapCount <= 0) {
			snapCount = std::max(1, (int)(args.sampleRate / 50.f));
			std::unique_lock<std::mutex> lock(snapMutex, std::try_to_lock);
			if (lock) {
				logic.snapshot(v, snap);
				lastFactoryCached = pcm70::PanelLogic::FACTORY_SLOTS - 1;
				for (int s = 0; s < pcm70::ProgramNames::SLOTS; s++) { factoryMissing[s] = names.done && names.name[s].empty(); if (!factoryMissing[s]) lastFactoryCached = std::max(lastFactoryCached, s); }
				if (--ramCount <= 0) { ramCount = 50; battery.assign(v.batteryRam(), v.batteryRam() + 0x2000); }
			}
		}
	}

};

// --- widgets ------------------------------------------------------------------------------

namespace {

/** The read-out well: the machine's own 16-digit display, what the selector points at (and what LOAD will do with it), the last parameter touched, the headroom bar
    and what the module is doing; under it the preset strip, the 9 x 5 parameter matrix and the setup strip, whose cells are the FIELD_* rectangles the fields sit on. */
struct WellDisplay : widget::Widget {
	Depreciation* module = nullptr;
	int shownSlot = -1; bool shownUser = false; double slotMovedAt = -99.0;      // UI-thread memory: when the selector last moved, so the cue gives way to a touched parameter only after
	panel::FittedText fitted[5 * 9 * 2], fitStat, fitSlot;

	/** A strip cell: a small caption over its value, both centred. */
	void cell(NVGcontext* vg, const Rect& f, const char* tag, const std::string& value, NVGcolor ink) {
		const Rect c = panel::inGlass(f);
		const float mmx = mm2px(1.f), cx = c.pos.x + c.size.x / 2.f;
		const panel::TextStyle TAG(panel::Face::Mono, 2.0f * mmx, panel::SAGE, NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE);
		const panel::TextStyle VAL(panel::Face::Mono, 2.9f * mmx, ink, NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE);
		panel::text(vg, TAG, cx, c.pos.y + c.size.y * 0.40f, tag);
		panel::text(vg, VAL, cx, c.pos.y + c.size.y * 0.88f, value);
	}

	std::string shown(int id) {
		engine::ParamQuantity* q = module ? module->paramQuantities[id] : nullptr;
		return q ? q->getDisplayValueString() + q->getUnit() : std::string("--");
	}

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
		// the 16 digits, across the width
		const float dx = (panel::X0 - panel::GLASS_X) * mmx, dy = (panel::DIG_Y - panel::GLASS_Y) * mmx;
		const float dw = panel::IW / 16.f * mmx, dh = panel::DIG_H * mmx;
		std::string txt = live ? s.display : (module ? status : std::string("DEPRECIATION"));
		std::vector<std::pair<char, bool>> digits;
		for (char ch : txt) { if (ch == '.' && !digits.empty() && !digits.back().second && live) digits.back().second = true; else digits.push_back({ ch, false }); }
		while (digits.size() < 16) digits.push_back({ ' ', false });
		const panel::TextStyle big(panel::Face::Mono, 0.95f * dh, live ? panel::LIME : (ok ? panel::SAGE : panel::CLAY), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		for (int i = 0; i < 16 && i < (int)digits.size(); i++) {
			const float x = dx + i * dw;
			nvgBeginPath(vg); nvgRoundedRect(vg, x + 0.2f * mmx, dy, dw - 0.4f * mmx, dh, 0.5f * mmx); nvgFillColor(vg, panel::alpha(panel::LIME, 0.05f)); nvgFill(vg);
			const char buf[2] = { digits[i].first, 0 };
			if (digits[i].first != ' ') panel::text(vg, big, x + dw / 2 - (digits[i].second ? 0.3f * mmx : 0.f), dy + dh / 2 + 0.3f * mmx, buf);
			if (digits[i].second) { nvgBeginPath(vg); nvgCircle(vg, x + dw - 0.7f * mmx, dy + dh - 0.9f * mmx, 0.4f * mmx); nvgFillColor(vg, panel::LIME); nvgFill(vg); }
		}
		// the status line: a refusal, the last parameter touched, or what the selector points at and what LOAD will do; the headroom bar at its right
		const panel::TextStyle small(panel::Face::Mono, 3.0f * mmx, panel::SAGE, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
		const float ly = (panel::STAT_Y - panel::GLASS_Y) * mmx;
		const float barW = 24.f * mmx, hx = dx + panel::IW * mmx - barW;
		const float statW = hx - dx - 2.f * mmx;
		if (live) {
			const bool user = module->params[Depreciation::REGMODE_PARAM].getValue() > 0.5f;
			const int slot = (int)std::lround(module->params[Depreciation::SLOT_PARAM].getValue());
			const double now = glfwGetTime();
			if (slot != shownSlot || user != shownUser) { if (shownSlot >= 0) slotMovedAt = now; shownSlot = slot; shownUser = user; }
			std::string line; NVGcolor ink = panel::LIME;
			if (!s.refusal.empty() && s.sinceRefusal < 3.0) { line = s.refusal; ink = panel::CLAY; }
			else if (s.touched >= 0 && s.sinceTouch < 4.0 && now - slotMovedAt > s.sinceTouch) {
				const int r = s.touched / 9, c = s.touched % 9;
				line = std::to_string(r) + "." + std::to_string(c) + "  " + s.name[r][c] + "  " + s.caption[r][c];
			} else {
				bool empty = false; const std::string what = module->slotText(user, slot, &empty);
				const bool running = s.loadedSlot == slot && s.loadedUser == user;
				line = what + (empty ? "" : (running ? "  RUNNING" : "  PRESS LOAD")); ink = empty ? panel::CLAY : panel::LIME;
			}
			panel::text(vg, small.inked(ink), dx, ly, fitStat.get(vg, small, line, statW));
		}
		else if (module)
			panel::text(vg, small.inked(ok ? panel::SAGE : panel::CLAY), dx, ly, fitStat.get(vg, small, ok ? "POWER-UP TAKES ABOUT 9 SECONDS" : "RIGHT-CLICK: LOAD ROM FOLDER", statW));
		// headroom (the detector the firmware's gates read), -24 / -18 / -12 / -6 / 0 dB
		for (int i = 0; i < 5; i++) {
			const bool lit = live && s.leds >= i + 1;      // leds() counts from the bottom of the bar: 1 = only -24 dB, 5 = 0 dB overload
			nvgBeginPath(vg); nvgRoundedRect(vg, hx + i * barW / 5.f, ly - 2.4f * mmx, barW / 5.f - 0.6f * mmx, 2.4f * mmx, 0.3f * mmx);
			nvgFillColor(vg, panel::alpha(i == 4 ? panel::CLAY : panel::LIME, lit ? 1.f : 0.14f)); nvgFill(vg);
		}
		if (live && s.fault) panel::text(vg, small.inked(panel::CLAY), dx, ly, "FIRMWARE STALLED");

		// the preset strip
		{
			const bool user = module && module->params[Depreciation::REGMODE_PARAM].getValue() > 0.5f;
			const int slot = module ? (int)std::lround(module->params[Depreciation::SLOT_PARAM].getValue()) : 0;
			bool empty = false;
			const std::string what = module ? module->slotText(user, slot, &empty) : std::string("--");
			const Rect f = panel::inGlass(panel::FIELD_SLOT);
			const panel::TextStyle VAL(panel::Face::Mono, 2.9f * mmx, empty ? panel::CLAY : panel::PAPER, NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE);
			cell(vg, panel::FIELD_SLOT, "PRESET", "", panel::PAPER);
			panel::text(vg, VAL, f.pos.x + f.size.x / 2.f, f.pos.y + f.size.y * 0.88f, fitSlot.get(vg, VAL, what, f.size.x - 1.5f * mmx));
			cell(vg, panel::FIELD_REGMODE, "BANK", user ? "USER" : "FACTORY", panel::LIME);
			const bool running = live && s.loadedSlot == slot && s.loadedUser == user;
			cell(vg, panel::FIELD_LOAD, "", "LOAD", running ? panel::alpha(panel::LIME, 0.45f) : panel::LIME);
			cell(vg, panel::FIELD_STORE, "", "STORE", user ? panel::LIME : panel::alpha(panel::SAGE, 0.5f));
			const bool byp = module && module->lights[Depreciation::BYPASS_LED].getBrightness() > 0.5f;
			cell(vg, panel::FIELD_BYPASS, "", "BYPASS", byp ? panel::CLAY : panel::SAGE);
		}

		// the matrix: each cell the firmware's own name over its printed value; a cell the program does not use is dimmed
		{
			const panel::TextStyle NAME(panel::Face::Mono, 2.1f * mmx, panel::SAGE, NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE);
			const panel::TextStyle VAL(panel::Face::Mono, 2.6f * mmx, panel::PAPER, NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE);
			for (int r = 0; r < 5; r++) for (int c = 0; c < 9; c++) {
				const Rect f = panel::inGlass(CELL_FIELD[r][c]);
				const float cx = f.pos.x + f.size.x / 2.f, w = f.size.x - 0.8f * mmx;
				const bool valid = !module || s.valid[r][c];
				if (!valid) {
					panel::text(vg, NAME.inked(panel::alpha(panel::SAGE, 0.3f)), cx, f.pos.y + f.size.y * 0.66f, "--");
					continue;
				}
				const bool touched = module && s.touched == r * 9 + c && s.sinceTouch < 2.0;
				if (touched) {
					nvgBeginPath(vg); nvgRoundedRect(vg, f.pos.x + 0.3f, f.pos.y + 0.3f, f.size.x - 0.6f, f.size.y - 0.6f, 1.2f);
					nvgFillColor(vg, panel::alpha(panel::LIME, 0.10f)); nvgFill(vg);
				}
				const std::string name = module ? s.name[r][c] : std::to_string(r) + "." + std::to_string(c);
				const std::string val = module ? (s.caption[r][c].empty() ? std::to_string(s.word[r][c]) : s.caption[r][c]) : std::string("");
				panel::text(vg, NAME, cx, f.pos.y + f.size.y * 0.40f, fitted[(r * 9 + c) * 2].get(vg, NAME, name, w));
				panel::text(vg, VAL.inked(touched ? panel::LIME : panel::PAPER), cx, f.pos.y + f.size.y * 0.86f, fitted[(r * 9 + c) * 2 + 1].get(vg, VAL, val, w));
			}
		}

		// the setup strip
		cell(vg, panel::FIELD_INPUT, "INPUT", shown(Depreciation::INPUT_PARAM), panel::LIME);
		cell(vg, panel::FIELD_TRIM, "FULL SCALE", shown(Depreciation::TRIM_PARAM), panel::LIME);
		cell(vg, panel::FIELD_IN_PAD, "IN", module && module->params[Depreciation::IN_PAD_PARAM].getValue() > 0.5f ? "-20" : "+4", panel::LIME);
		cell(vg, panel::FIELD_OUT_PAD, "OUT", module && module->params[Depreciation::OUT_PAD_PARAM].getValue() > 0.5f ? "-20" : "+4", panel::LIME);
		cell(vg, panel::FIELD_CLK_DIV, "CLK /", shown(Depreciation::CLK_DIV_PARAM), panel::LIME);
		cell(vg, panel::FIELD_OUT_LEVEL, "OUT LEVEL", shown(Depreciation::OUT_LEVEL_PARAM), panel::LIME);
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

		// Every control is a cell of the read-out, bound to the param its knob or button was.
		panel::ScreenSelect* slot = panel::createField<panel::ScreenSelect>(panel::FIELD_SLOT, module, Depreciation::SLOT_PARAM);
		slot->nameOf = [module](int i) {
			if (!module) return std::to_string(i);
			return module->slotText(module->params[Depreciation::REGMODE_PARAM].getValue() > 0.5f, i);
		};
		addParam(slot);
		addParam(panel::createField<panel::ScreenSwitch>(panel::FIELD_REGMODE, module, Depreciation::REGMODE_PARAM));
		addParam(panel::createField<panel::ScreenButton>(panel::FIELD_LOAD, module, Depreciation::LOAD_PARAM));
		addParam(panel::createField<panel::ScreenButton>(panel::FIELD_STORE, module, Depreciation::STORE_PARAM));
		addParam(panel::createField<panel::ScreenButton>(panel::FIELD_BYPASS, module, Depreciation::BYPASS_PARAM));
		for (int r = 0; r < 5; r++) for (int c = 0; c < 9; c++)
			addParam(panel::createField<panel::ScreenKnob>(CELL_FIELD[r][c], module, Depreciation::CELL_PARAM + r * 9 + c));
		addParam(panel::createField<panel::ScreenKnob>(panel::FIELD_INPUT, module, Depreciation::INPUT_PARAM));
		addParam(panel::createField<panel::ScreenKnob>(panel::FIELD_TRIM, module, Depreciation::TRIM_PARAM));
		addParam(panel::createField<panel::ScreenSwitch>(panel::FIELD_IN_PAD, module, Depreciation::IN_PAD_PARAM));
		addParam(panel::createField<panel::ScreenSwitch>(panel::FIELD_OUT_PAD, module, Depreciation::OUT_PAD_PARAM));
		addParam(panel::createField<panel::ScreenSelect>(panel::FIELD_CLK_DIV, module, Depreciation::CLK_DIV_PARAM));
		addParam(panel::createField<panel::ScreenKnob>(panel::FIELD_OUT_LEVEL, module, Depreciation::OUT_LEVEL_PARAM));

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

// Apportionment -- an Ensoniq DP/4 parallel effects processor, running its own
// firmware. See Machine.hpp for the board and Router.hpp for the routing panel.
//
// The EPROMs are Ensoniq's and are not distributed with the plugin: the context
// menu loads them from wherever the user keeps them, and the patch remembers the
// paths, never the contents. The battery-backed RAM -- user presets, Configs,
// system settings -- is saved in the patch, as the DP/4's battery would keep it.
#include "../plugin.hpp"
#include "Panel.hpp"
#include "Router.hpp"

#include <osdialog.h>

#include <atomic>
#include <fstream>
#include <iterator>
#include <mutex>
#include <thread>

namespace {

using dp4::Machine;
using dp4::Router;
using dp4::Routing;

std::vector<uint8_t> readFile(const std::string& path) {
	std::ifstream f(path, std::ios::binary);
	if (!f) return {};
	return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}

/** Everything the UI thread draws, copied out of the machine a few times a second. */
struct Snapshot {
	dp4::Display display;
	Routing routing;
	int mux = 0;
	bool routerBusy = false;
};

} // namespace

struct Apportionment : Module {
	enum ParamId {
		UNIT_A_PARAM, UNIT_B_PARAM, UNIT_C_PARAM, UNIT_D_PARAM,
		CONFIG_PARAM, SYSTEM_PARAM, EDIT_PARAM,
		SELECT_PARAM, LEFT_PARAM, RIGHT_PARAM, CANCEL_PARAM, WRITE_PARAM,
		SOURCES_PARAM, AB_ROUTE_PARAM, CD_ROUTE_PARAM, AB_CD_PARAM,
		AB_AMOUNT_PARAM, CD_AMOUNT_PARAM, AB_MONO_PARAM, CD_MONO_PARAM,
		AB_OUT_PARAM, CD_OUT_PARAM, IN_LEVEL_PARAM, OUT_LEVEL_PARAM,
		PARAMS_LEN
	};
	enum InputId { IN1_INPUT, IN2_INPUT, IN3_INPUT, IN4_INPUT, PEDAL_INPUT, FS_L_INPUT, FS_R_INPUT, INPUTS_LEN };
	enum OutputId { OUT1_OUTPUT, OUT2_OUTPUT, OUT3_OUTPUT, OUT4_OUTPUT, TAP_A_OUTPUT, TAP_B_OUTPUT, TAP_C_OUTPUT, TAP_D_OUTPUT, OUTPUTS_LEN };
	enum LightId { UNIT_A_LIGHT, UNIT_B_LIGHT, UNIT_C_LIGHT, UNIT_D_LIGHT, CONFIG_LIGHT, SYSTEM_LIGHT, EDIT_LIGHT, ROUTING_LIGHT, LIGHTS_LEN };

	/** The first twelve params are the DP/4's own buttons, in this order. */
	static constexpr int BUTTONS = 12;
	int buttonNumber(int param) const {
		static const int n[BUTTONS] = {
			dp4::BTN_A, dp4::BTN_B, dp4::BTN_C, dp4::BTN_D, dp4::BTN_CONFIG, dp4::BTN_SYSTEM,
			dp4::BTN_EDIT, dp4::BTN_SELECT, dp4::BTN_LEFT, dp4::BTN_RIGHT, dp4::BTN_CANCEL, dp4::BTN_WRITE,
		};
		return n[param];
	}

	// --- the machine: built and booted off the audio thread, then handed over -------
	std::unique_ptr<Machine> machine;             // audio thread only
	std::atomic<Machine*> handover{nullptr};
	std::thread bootThread;
	std::atomic<bool> booting{false};
	Router router;

	std::string osPath, ucodePath;               // UI thread
	std::string status = "LOAD DP/4 EPROMS";     // guarded by snapMutex
	std::vector<uint8_t> battery;                // guarded by snapMutex
	std::mutex snapMutex;
	Snapshot snap;                               // guarded by snapMutex

	// --- audio-thread state -------------------------------------------------------
	bool pressed[BUTTONS] = {};
	std::atomic<int> dataDetents{0};              // from the endless knob widget
	bool fsDown[2] = {};
	Routing applied;                             // what the CONFIG controls last agreed with
	bool routingSynced = false;
	int housekeeping = 0;
	int batteryCountdown = 0;

	dsp::SampleRateConverter<4> inSrc;
	dsp::SampleRateConverter<12> outSrc;
	dsp::DoubleRingBuffer<dsp::Frame<4>, 256> inBuf;
	dsp::DoubleRingBuffer<dsp::Frame<12>, 256> outBuf;

	Apportionment() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		static const char* names[BUTTONS] = { "Unit A", "Unit B", "Unit C", "Unit D", "Config", "System/MIDI",
			"Edit/Compare", "Select", "Previous parameter", "Next parameter", "Cancel/Undo", "Write/Copy" };
		for (int i = 0; i < BUTTONS; i++) configButton(i, names[i]);

		configSwitch(SOURCES_PARAM, 0.f, 3.f, 0.f, "Sources", { "1 source (1,2 > ABCD)", "2 sources (12 > AB, 34 > CD)", "3 sources (1 > A, 2 > B, 34 > CD)", "4 sources (one per unit)" });
		const std::vector<std::string> pair = { "Serial", "Parallel", "Feedback 1", "Feedback 2" };
		configSwitch(AB_ROUTE_PARAM, 0.f, 3.f, 0.f, "A-B routing", pair);
		configSwitch(CD_ROUTE_PARAM, 0.f, 3.f, 1.f, "C-D routing", pair);
		configSwitch(AB_CD_PARAM, 0.f, 1.f, 0.f, "AB to CD (1 source)", { "Serial", "Parallel" });
		configParam(AB_AMOUNT_PARAM, 0.f, 99.f, 0.f, "AB dry path / feedback amount")->snapEnabled = true;
		configParam(CD_AMOUNT_PARAM, 0.f, 99.f, 0.f, "CD dry path / feedback amount")->snapEnabled = true;
		configSwitch(AB_MONO_PARAM, 0.f, 1.f, 0.f, "AB input", { "Stereo (1,2)", "Mono (1)" });
		configSwitch(CD_MONO_PARAM, 0.f, 1.f, 0.f, "CD input", { "Stereo (3,4)", "Mono (3)" });
		configSwitch(AB_OUT_PARAM, 0.f, 1.f, 0.f, "AB output (3-4 sources)", { "A > 1, B > 2, dual mono", "1-2 mixed stereo" });
		configSwitch(CD_OUT_PARAM, 0.f, 1.f, 0.f, "CD output (4 sources)", { "C > 3, D > 4, dual mono", "3-4 mixed stereo" });
		configParam(IN_LEVEL_PARAM, 0.f, 2.f, 1.f, "Input level", "%", 0.f, 100.f);
		configParam(OUT_LEVEL_PARAM, 0.f, 2.f, 1.f, "Output level", "%", 0.f, 100.f);

		configInput(IN1_INPUT, "Input 1 (left)");
		configInput(IN2_INPUT, "Input 2 (right; normalled to 1)");
		configInput(IN3_INPUT, "Input 3 (left)");
		configInput(IN4_INPUT, "Input 4 (right; normalled to 3)");
		configInput(PEDAL_INPUT, "CV pedal, 0-10 V");
		configInput(FS_L_INPUT, "Left footswitch gate");
		configInput(FS_R_INPUT, "Right footswitch gate");
		configOutput(OUT1_OUTPUT, "Output 1 (left)");
		configOutput(OUT2_OUTPUT, "Output 2 (right)");
		configOutput(OUT3_OUTPUT, "Output 3 (left)");
		configOutput(OUT4_OUTPUT, "Output 4 (right)");
		configOutput(TAP_A_OUTPUT, "Unit A tap (stereo, polyphonic)");
		configOutput(TAP_B_OUTPUT, "Unit B tap (stereo, polyphonic)");
		configOutput(TAP_C_OUTPUT, "Unit C tap (stereo, polyphonic)");
		configOutput(TAP_D_OUTPUT, "Unit D tap (stereo, polyphonic)");
		configLight(ROUTING_LIGHT, "Working the Config pages");
		for (int i = TAP_A_OUTPUT; i <= TAP_D_OUTPUT; i++) outputs[i].setChannels(2);
	}

	~Apportionment() {
		if (bootThread.joinable()) bootThread.join();
		delete handover.exchange(nullptr);
	}

	// --- booting --------------------------------------------------------------------
	void setStatus(const std::string& s) {
		std::lock_guard<std::mutex> lock(snapMutex);
		status = s;
	}

	/** Build a fresh machine from the EPROMs and the saved battery RAM, and run it to
	    its main screen on a worker thread (about a second). The audio thread picks
	    it up when it is ready. UI thread. */
	void boot() {
		if (osPath.empty() || ucodePath.empty()) { setStatus("LOAD DP/4 EPROMS"); return; }
		if (bootThread.joinable()) bootThread.join();
		std::vector<uint8_t> ram;
		{
			std::lock_guard<std::mutex> lock(snapMutex);
			ram = battery;
			status = "POWERING ON";
		}
		booting = true;
		const std::string os = osPath, uc = ucodePath;
		bootThread = std::thread([this, os, uc, ram]() {
			std::unique_ptr<Machine> m(new Machine);
			const std::string err = m->load(readFile(os), readFile(uc));
			if (!err.empty()) {
				setStatus(err);
				booting = false;
				return;
			}
			m->setBatteryRam(ram);
			if (ram.empty()) m->clearBatteryRam();
			m->powerOn();
			m->idle(4.0);
			INFO("Apportionment: DP/4 booted from %s + %s: [%s] [%s]", system::getFilename(os).c_str(),
				system::getFilename(uc).c_str(), m->display().line(0).c_str(), m->display().line(1).c_str());
			delete handover.exchange(m.release());
			setStatus("");
			rememberRoms();
			booting = false;
		});
	}

	/** The last EPROMs that booted are remembered plugin-wide, in the family's
	    MoonTechnologies/settings.json in Rack's user directory (the same file
	    Repossession keeps its tools folder in; each writer merges its own keys),
	    so a new Apportionment finds them without a trip to the menu. Paths only,
	    never the images. */
	static std::string settingsPath() { return asset::user("MoonTechnologies/settings.json"); }

	void rememberRoms() {
		const std::string path = settingsPath();
		system::createDirectories(system::getDirectory(path));
		json_t* root = system::isFile(path) ? json_load_file(path.c_str(), 0, NULL) : NULL;
		if (!root) root = json_object();
		json_object_set_new(root, "apportionmentOs", json_string(osPath.c_str()));
		json_object_set_new(root, "apportionmentUcode", json_string(ucodePath.c_str()));
		json_dump_file(root, path.c_str(), JSON_INDENT(2));
		json_decref(root);
	}

	void onAdd(const AddEvent& e) override {
		// A patch has already set the paths (dataFromJson runs first); a new module has not.
		if (!osPath.empty() || booting || machine) return;
		const std::string path = settingsPath();
		json_t* root = system::isFile(path) ? json_load_file(path.c_str(), 0, NULL) : NULL;
		if (!root) return;
		json_t* o = json_object_get(root, "apportionmentOs");
		json_t* u = json_object_get(root, "apportionmentUcode");
		if (o && u && json_is_string(o) && json_is_string(u)) {
			osPath = json_string_value(o);
			ucodePath = json_string_value(u);
		}
		json_decref(root);
		if (!osPath.empty()) boot();
	}

	/** Wipe the battery RAM: the OS reinitialises and reloads its ROM presets. */
	void factoryReset() {
		{
			std::lock_guard<std::mutex> lock(snapMutex);
			battery.clear();
		}
		routingSynced = false;
		boot();
	}

	// --- the CONFIG controls <-> the firmware's Config ------------------------------
	Routing fromParams() {
		Routing r;
		r.sources = int(params[SOURCES_PARAM].getValue() + 1.5f);
		r.abRoute = int(params[AB_ROUTE_PARAM].getValue() + .5f);
		r.cdRoute = int(params[CD_ROUTE_PARAM].getValue() + .5f);
		r.abToCd = int(params[AB_CD_PARAM].getValue() + .5f);
		r.abAmount = int(params[AB_AMOUNT_PARAM].getValue() + .5f);
		r.cdAmount = int(params[CD_AMOUNT_PARAM].getValue() + .5f);
		r.abMono = int(params[AB_MONO_PARAM].getValue() + .5f);
		r.cdMono = int(params[CD_MONO_PARAM].getValue() + .5f);
		r.abOut = int(params[AB_OUT_PARAM].getValue() + .5f);
		r.cdOut = int(params[CD_OUT_PARAM].getValue() + .5f);
		return r;
	}

	static int paramFor(Router::Field f) {
		static const int p[Router::F_COUNT] = { SOURCES_PARAM, AB_CD_PARAM, AB_ROUTE_PARAM, CD_ROUTE_PARAM,
			AB_AMOUNT_PARAM, CD_AMOUNT_PARAM, AB_MONO_PARAM, CD_MONO_PARAM, AB_OUT_PARAM, CD_OUT_PARAM };
		return p[f];
	}

	static float paramValue(Router::Field f, int v) { return f == Router::F_SOURCES ? float(v - 1) : float(v); }

	/** A control the user moved is sent to the firmware; a Config the firmware
	    changed on its own (a Config preset, the front panel) moves the controls. */
	void syncRouting() {
		if (router.busy()) return;
		const Routing fw = machine->routing();
		if (!routingSynced) {
			// First contact after power-on: the firmware's battery RAM is the truth.
			for (int f = 0; f < Router::F_COUNT; f++)
				if (f == Router::F_SOURCES || Router::page(fw.sources, Router::Field(f)) >= 0)
					params[paramFor(Router::Field(f))].setValue(paramValue(Router::Field(f), Router::value(fw, Router::Field(f))));
			applied = fromParams();
			routingSynced = true;
			return;
		}
		const Routing p = fromParams();
		bool moved = false;
		for (int f = 0; f < Router::F_COUNT; f++)
			if (Router::value(p, Router::Field(f)) != Router::value(applied, Router::Field(f))) moved = true;
		if (moved) {
			INFO("Apportionment: routing to %d source(s), A-B %d, C-D %d, AB>CD %d", p.sources, p.abRoute, p.cdRoute, p.abToCd);
			router.request(p);
			applied = p;
			return;
		}
		// Nothing moved on the panel: follow the firmware wherever the field exists.
		for (int f = 0; f < Router::F_COUNT; f++) {
			const Router::Field fld = Router::Field(f);
			if (f != Router::F_SOURCES && Router::page(fw.sources, fld) < 0) continue;
			if (Router::value(fw, fld) != Router::value(p, fld))
				params[paramFor(fld)].setValue(paramValue(fld, Router::value(fw, fld)));
		}
		applied = fromParams();
	}

	// --- audio -------------------------------------------------------------------------
	void process(const ProcessArgs& args) override {
		if (Machine* m = handover.exchange(nullptr)) {
			INFO("Apportionment: machine running, engine at %.0f Hz", args.sampleRate);
			machine.reset(m);
			routingSynced = false;
			inBuf.clear();
			outBuf.clear();
		}
		if (!machine) {
			for (int i = 0; i < OUTPUTS_LEN; i++) outputs[i].setVoltage(0.f);
			for (int i = TAP_A_OUTPUT; i <= TAP_D_OUTPUT; i++) outputs[i].setChannels(2);
			return;
		}

		// The front panel.
		for (int i = 0; i < BUTTONS; i++) {
			const bool down = params[i].getValue() > 0.5f;
			if (down != pressed[i]) { machine->button(buttonNumber(i), down); pressed[i] = down; }
		}
		if (const int d = dataDetents.exchange(0)) machine->knob(d);
		for (int i = 0; i < 2; i++) {
			const bool down = inputs[FS_L_INPUT + i].getVoltage() > 1.f;
			if (down != fsDown[i]) { machine->footswitch(i, down); fsDown[i] = down; }
		}
		machine->setPedal(inputs[PEDAL_INPUT].isConnected()
			? uint8_t(clamp(inputs[PEDAL_INPUT].getVoltage() / 10.f, 0.f, 1.f) * 225.f) : 255);

		// Inputs, normalled as the DP/4's jacks are: 2 follows 1, 4 follows 3.
		const float inGain = params[IN_LEVEL_PARAM].getValue() / 5.f;
		dsp::Frame<4> fin;
		for (int c = 0; c < 4; c++) {
			const int src = (c % 2 == 1 && !inputs[IN1_INPUT + c].isConnected()) ? c - 1 : c;
			fin.samples[c] = inputs[IN1_INPUT + src].getVoltage() * inGain;
		}
		if (!inBuf.full()) inBuf.push(fin);

		// Run the machine at its own rate, a block at a time.
		if (outBuf.size() < 16) {
			inSrc.setRates(int(args.sampleRate), int(Machine::FRAME_RATE));
			outSrc.setRates(int(Machine::FRAME_RATE), int(args.sampleRate));
			dsp::Frame<4> mIn[64];
			int inLen = int(inBuf.size()), mLen = 64;
			inSrc.process(inBuf.startData(), &inLen, mIn, &mLen);
			inBuf.startIncr(inLen);
			dsp::Frame<12> mOut[64];
			for (int n = 0; n < mLen; n++) {
				int16_t adc[4], dac[4], taps[8];
				for (int c = 0; c < 4; c++)
					adc[c] = int16_t(clamp(mIn[n].samples[c], -1.f, 1.f) * 32767.f);
				machine->frame(adc, dac, taps);
				router.tick(*machine);
				for (int c = 0; c < 4; c++) mOut[n].samples[c] = dac[c] / 32768.f;
				for (int c = 0; c < 8; c++) mOut[n].samples[4 + c] = taps[c] / 32768.f;
			}
			int oLen = int(outBuf.capacity());
			outSrc.process(mOut, &mLen, outBuf.endData(), &oLen);
			outBuf.endIncr(oLen);
		}
		dsp::Frame<12> fo = {};
		if (!outBuf.empty()) { fo = outBuf.shift(); }

		// Outputs, switched as the DP/4's jacks are: with 3 unplugged, 3/4 are mixed
		// onto 1/2; with 2 (or 4) unplugged, its pair is summed to mono on 1 (or 3).
		const float outGain = params[OUT_LEVEL_PARAM].getValue() * 5.f;
		float o[4] = { fo.samples[0], fo.samples[1], fo.samples[2], fo.samples[3] };
		if (!outputs[OUT3_OUTPUT].isConnected()) { o[0] += o[2]; o[1] += o[3]; }
		if (!outputs[OUT2_OUTPUT].isConnected()) o[0] = 0.5f * (o[0] + o[1]);
		if (!outputs[OUT4_OUTPUT].isConnected()) o[2] = 0.5f * (o[2] + o[3]);
		for (int c = 0; c < 4; c++) outputs[OUT1_OUTPUT + c].setVoltage(o[c] * outGain);
		for (int u = 0; u < 4; u++) {
			outputs[TAP_A_OUTPUT + u].setChannels(2);
			outputs[TAP_A_OUTPUT + u].setVoltage(fo.samples[4 + 2 * u] * outGain, 0);
			outputs[TAP_A_OUTPUT + u].setVoltage(fo.samples[5 + 2 * u] * outGain, 1);
		}

		// Housekeeping, a few hundred times a second: the routing controls, the
		// lights, the UI's copy of the display, and the battery RAM for the patch.
		if (++housekeeping >= 128) {
			housekeeping = 0;
			syncRouting();
			const dp4::Display& d = machine->display();
			static const int led[7] = { dp4::BTN_A, dp4::BTN_B, dp4::BTN_C, dp4::BTN_D, dp4::BTN_CONFIG, dp4::BTN_SYSTEM, dp4::BTN_EDIT };
			for (int i = 0; i < 7; i++) lights[UNIT_A_LIGHT + i].setBrightness(d.led(led[i]) ? 1.f : 0.f);
			lights[ROUTING_LIGHT].setBrightness(router.busy() ? 1.f : 0.f);
			if (snapMutex.try_lock()) {
				snap.display = d;
				snap.routing = machine->routing();
				snap.mux = machine->inputMux();
				snap.routerBusy = router.busy();
				// The battery RAM is 40 KB: copy it for the patch every couple of seconds.
				if (--batteryCountdown <= 0) { battery = machine->batteryRam(); batteryCountdown = 600; }
				snapMutex.unlock();
			}
		}
	}

	// --- the patch -----------------------------------------------------------------------
	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "osPath", json_string(osPath.c_str()));
		json_object_set_new(root, "ucodePath", json_string(ucodePath.c_str()));
		std::lock_guard<std::mutex> lock(snapMutex);
		if (!battery.empty())
			json_object_set_new(root, "batteryRam", json_string(string::toBase64(battery).c_str()));
		return root;
	}

	void dataFromJson(json_t* root) override {
		if (json_t* j = json_object_get(root, "osPath")) osPath = json_string_value(j);
		if (json_t* j = json_object_get(root, "ucodePath")) ucodePath = json_string_value(j);
		{
			std::lock_guard<std::mutex> lock(snapMutex);
			battery.clear();
			if (json_t* j = json_object_get(root, "batteryRam")) battery = string::fromBase64(json_string_value(j));
		}
		routingSynced = false;
		boot();
	}
};

// --- widgets ------------------------------------------------------------------------------

namespace {

/** The DP/4's 2 x 16 LCD, from the firmware's own byte stream. */
struct LcdDisplay : widget::Widget {
	Apportionment* module = nullptr;

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) return;
		NVGcontext* vg = args.vg;
		const float cw = box.size.x / 17.f, lh = box.size.y / 2.f;
		const panel::TextStyle st(panel::Face::Mono, lh * 0.62f, panel::LIME, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		if (!module) {
			panel::text(vg, st, box.size.x / 2, lh * 0.55f, "ENSONIQ * DP/4");
			panel::text(vg, st.inked(panel::SAGE), box.size.x / 2, lh * 1.45f, "APPORTIONMENT");
			return;
		}
		dp4::Display d;
		std::string status;
		{
			std::lock_guard<std::mutex> lock(module->snapMutex);
			d = module->snap.display;
			status = module->status;
		}
		if (!status.empty()) {
			panel::text(vg, st.inked(panel::CLAY), box.size.x / 2, lh * 0.55f, status);
			panel::text(vg, st.inked(panel::SAGE), box.size.x / 2, lh * 1.45f,
				module->booting ? "PLEASE WAIT" : "RIGHT-CLICK: LOAD");
			return;
		}
		const bool blinkOff = std::fmod(system::getTime(), 0.6) > 0.4;
		for (int row = 0; row < 2; row++)
			for (int c = 0; c < 16; c++) {
				const int i = 16 * row + c;
				if (d.blink[i] && blinkOff) continue;
				const char s[2] = { d.lcd[i], 0 };
				panel::text(vg, st, cw * (c + 1.f), lh * (row + 0.55f), s);
			}
	}
};

/** The DP/4's two seven-segment digits, segment by segment as the firmware drives them. */
struct DigitDisplay : widget::Widget {
	Apportionment* module = nullptr;

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) return;
		dp4::Display d;
		if (module) {
			std::lock_guard<std::mutex> lock(module->snapMutex);
			d = module->snap.display;
		}
		NVGcontext* vg = args.vg;
		const float dw = box.size.x * 0.34f, dh = box.size.y * 0.62f, t = dw * 0.16f;
		for (int dig = 0; dig < 2; dig++) {
			const float x0 = box.size.x * (dig ? 0.56f : 0.12f), y0 = box.size.y * 0.19f;
			// a b c d e f g, each as (x, y, w, h) in the digit's box
			const float seg[7][4] = {
				{ t, 0, dw - 2 * t, t }, { dw - t, t, t, dh / 2 - 1.5f * t }, { dw - t, dh / 2 + 0.5f * t, t, dh / 2 - 1.5f * t },
				{ t, dh - t, dw - 2 * t, t }, { 0, dh / 2 + 0.5f * t, t, dh / 2 - 1.5f * t }, { 0, t, t, dh / 2 - 1.5f * t },
				{ t, dh / 2 - t / 2, dw - 2 * t, t },
			};
			for (int k = 0; k < 8; k++) {
				const bool on = module && d.segment(dig, k);
				nvgBeginPath(vg);
				if (k < 7) nvgRect(vg, x0 + seg[k][0], y0 + seg[k][1], seg[k][2], seg[k][3]);
				else nvgCircle(vg, x0 + dw + t * 1.2f, y0 + dh - t / 2, t * 0.6f);
				nvgFillColor(vg, on ? panel::LIME : panel::alpha(panel::SAGE, 0.12f));
				nvgFill(vg);
			}
		}
	}
};

/** The routing the machine is running, read back from its Config. Inputs on the
    left, the four units as the DP/4 pairs them (A-B above, C-D below), outputs on
    the right. Signal runs left to right; feedback is the clay arc. */
struct RoutingMap : widget::Widget {
	Apportionment* module = nullptr;
	NVGcontext* vg = nullptr;
	NVGcolor ink;

	void poly(std::initializer_list<Vec> pts, NVGcolor c) {
		nvgBeginPath(vg);
		bool first = true;
		for (const Vec& p : pts) {
			if (first) nvgMoveTo(vg, p.x, p.y); else nvgLineTo(vg, p.x, p.y);
			first = false;
		}
		nvgLineJoin(vg, NVG_ROUND);
		nvgStrokeColor(vg, c);
		nvgStrokeWidth(vg, 1.1f);
		nvgStroke(vg);
	}
	void poly(std::initializer_list<Vec> pts) { poly(pts, ink); }

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) return;
		vg = args.vg;
		Routing r;
		bool ready = false;
		if (module) {
			std::lock_guard<std::mutex> lock(module->snapMutex);
			r = module->snap.routing;
			ready = module->status.empty();
		}
		ink = ready ? panel::LIME : panel::alpha(panel::SAGE, 0.35f);
		const float W = box.size.x, H = box.size.y;
		const float bw = W * 0.12f, bh = H * 0.22f;
		const float yT = H * 0.30f, yB = H * 0.74f, xIn = W * 0.08f, xOut = W * 0.92f;
		const float xA = W * 0.38f, xB = W * 0.62f;
		const Vec unit[4] = { Vec(xA, yT), Vec(xB, yT), Vec(xA, yB), Vec(xB, yB) };
		auto L = [&](int u) { return unit[u].minus(Vec(bw / 2, 0)); };
		auto R = [&](int u) { return unit[u].plus(Vec(bw / 2, 0)); };
		const float gap = bh * 0.85f;   // how far a bypassing path clears a unit

		// One pair between an entry point and an exit point on its own row.
		// Returns nothing; draws serial, parallel or serial-with-feedback.
		auto pair = [&](int a, int b, int route, Vec in, Vec out) {
			const float y = unit[a].y, up = (a < 2) ? -gap : gap;
			if (route == 1) {
				const Vec split(L(a).x - bw * 0.35f, y), join(R(b).x + bw * 0.35f, y);
				poly({ in, split, L(a) });
				poly({ split, Vec(split.x, y + up), Vec(L(b).x - bw * 0.25f, y + up), Vec(L(b).x - bw * 0.25f, y), L(b) });
				poly({ R(a), Vec(R(a).x + bw * 0.25f, y), Vec(R(a).x + bw * 0.25f, y - up), Vec(join.x, y - up), join });
				poly({ R(b), join, out });
			}
			else {
				poly({ in, L(a) });
				poly({ R(a), L(b) });
				poly({ R(b), out });
				if (route >= 2) {
					const float fy = y + up * 0.9f;
					poly({ Vec(unit[b].x, y + up * 0.45f), Vec(unit[b].x, fy), Vec(unit[a].x, fy), Vec(unit[a].x, y + up * 0.45f) }, panel::CLAY);
				}
			}
		};

		const Vec inT(xIn, yT), inB(xIn, yB), outT(xOut, yT), outB(xOut, yB);
		switch (r.sources) {
		case 1:
			if (r.abToCd == 0) {
				// AB feeds CD: out of B, down the right side, back under to C.
				const float xr = R(1).x + bw * 0.45f, xl = L(2).x - bw * 0.45f, ym = (yT + yB) / 2;
				pair(0, 1, r.abRoute, inT, Vec(xr, yT));
				poly({ Vec(xr, yT), Vec(xr, ym), Vec(xl, ym), Vec(xl, yB) });
				pair(2, 3, r.cdRoute, Vec(xl, yB), Vec(xOut - bw * 0.3f, yB));
				poly({ Vec(xOut - bw * 0.3f, yB), Vec(xOut - bw * 0.3f, yT), outT });
			}
			else {
				// AB beside CD, both from inputs 1,2, both to outputs 1,2.
				const float xs = xIn + bw * 0.45f, xj = xOut - bw * 0.3f;
				pair(0, 1, r.abRoute, inT, Vec(xj, yT));
				poly({ Vec(xs, yT), Vec(xs, yB), Vec(L(2).x - bw * 0.45f, yB) });
				pair(2, 3, r.cdRoute, Vec(L(2).x - bw * 0.45f, yB), Vec(xj, yB));
				poly({ Vec(xj, yB), Vec(xj, yT), outT });
			}
			break;
		case 2:
			pair(0, 1, r.abRoute, inT, outT);
			pair(2, 3, r.cdRoute, inB, outB);
			break;
		case 3: {
			const float o = bh * 0.8f;
			poly({ Vec(xIn, yT - o), Vec(L(0).x - bw * 0.3f, yT - o), Vec(L(0).x - bw * 0.3f, yT), L(0) });
			poly({ R(0), Vec(R(0).x + bw * 0.3f, yT), Vec(R(0).x + bw * 0.3f, yT - gap), Vec(xOut, yT - gap), Vec(xOut, yT - o) });
			poly({ Vec(xIn, yT + o), Vec(L(1).x - bw * 0.3f, yT + o), Vec(L(1).x - bw * 0.3f, yT), L(1) });
			poly({ R(1), Vec(xOut, yT), Vec(xOut, yT + o) });
			pair(2, 3, r.cdRoute, inB, outB);
			break;
		}
		default:
			for (int u = 0; u < 4; u++) {
				const float y = unit[u].y, o = (u % 2 ? 1.f : -1.f) * bh * 0.8f;
				poly({ Vec(xIn, y + o), Vec(L(u).x - bw * 0.3f, y + o), Vec(L(u).x - bw * 0.3f, y), L(u) });
				poly({ R(u), Vec(R(u).x + bw * 0.3f, y), Vec(R(u).x + bw * 0.3f, y + o), Vec(xOut, y + o) });
			}
			break;
		}

		const panel::TextStyle st(panel::Face::Mono, bh * 0.66f, panel::PAPER, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		for (int u = 0; u < 4; u++) {
			nvgBeginPath(vg);
			nvgRoundedRect(vg, unit[u].x - bw / 2, unit[u].y - bh / 2, bw, bh, 1.5f);
			nvgFillColor(vg, panel::GLASS);
			nvgFill(vg);
			nvgStrokeColor(vg, ink);
			nvgStrokeWidth(vg, 1.f);
			nvgStroke(vg);
			const char s[2] = { char('A' + u), 0 };
			panel::text(vg, st.inked(ready ? panel::PAPER : panel::SAGE), unit[u].x, unit[u].y, s);
		}

		// The jacks at each end, named the way the DP/4's Config screen names them.
		const panel::TextStyle io(panel::Face::Mono, bh * 0.44f, panel::SAGE, NVG_ALIGN_CENTER | NVG_ALIGN_BOTTOM);
		const float ty = bh * 0.72f;
		const char* inTop = r.sources >= 3 ? "1  2" : (r.abMono ? "1" : "1,2");
		const char* outTop = (r.sources >= 3 && !r.abOut) ? "1  2" : "1,2";
		panel::text(vg, io, xIn, yT - ty, inTop);
		panel::text(vg, io, xOut, yT - ty, outTop);
		if (r.sources >= 2) {
			const panel::TextStyle iob = io.aligned(NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
			panel::text(vg, iob, xIn, yB + ty, r.sources == 4 ? "3  4" : (r.cdMono ? "3" : "3,4"));
			panel::text(vg, iob, xOut, yB + ty, (r.sources == 4 && !r.cdOut) ? "3  4" : "3,4");
		}
	}
};

/** The DP/4's data-entry knob is an encoder: endless, reported in detents. */
struct DataKnob : widget::OpaqueWidget {
	Apportionment* module = nullptr;
	widget::FramebufferWidget* fb;
	widget::TransformWidget* tw;
	widget::SvgWidget* sw;
	float angle = 0.f, drag = 0.f;
	static constexpr float PX_PER_DETENT = 6.f;
	static constexpr int DETENTS_PER_TURN = 24;

	DataKnob() {
		fb = new widget::FramebufferWidget;
		addChild(fb);
		widget::SvgWidget* bg = new widget::SvgWidget;
		bg->setSvg(Svg::load(asset::system("res/ComponentLibrary/RoundLargeBlackKnob_bg.svg")));
		fb->addChild(bg);
		tw = new widget::TransformWidget;
		fb->addChild(tw);
		sw = new widget::SvgWidget;
		sw->setSvg(Svg::load(asset::system("res/ComponentLibrary/RoundLargeBlackKnob.svg")));
		tw->addChild(sw);
		box.size = sw->box.size;
		fb->box.size = box.size;
		bg->box.size = box.size;
		tw->box.size = box.size;
	}

	void turn(int detents) {
		if (!detents) return;
		if (module) module->dataDetents += detents;
		angle += detents * 2.f * float(M_PI) / DETENTS_PER_TURN;
		tw->identity();
		const Vec c = sw->box.getCenter();
		tw->translate(c);
		tw->rotate(angle);
		tw->translate(c.neg());
		fb->setDirty();
	}

	void onDragStart(const DragStartEvent& e) override {
		if (e.button == GLFW_MOUSE_BUTTON_LEFT) drag = 0.f;
	}
	void onDragMove(const DragMoveEvent& e) override {
		drag += (e.mouseDelta.x - e.mouseDelta.y) / std::max(0.1f, getAbsoluteZoom());
		const int n = int(drag / PX_PER_DETENT);
		drag -= n * PX_PER_DETENT;
		turn(n);
	}
	void onHoverScroll(const HoverScrollEvent& e) override {
		turn(e.scrollDelta.y > 0 ? 1 : e.scrollDelta.y < 0 ? -1 : 0);
		e.consume(this);
	}
};

} // namespace

struct ApportionmentWidget : ModuleWidget {
	ApportionmentWidget(Apportionment* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Apportionment.svg")));
		panel::addScrews(this);
		panel::addLabels(this);

		LcdDisplay* lcd = new LcdDisplay;
		lcd->module = module;
		lcd->box = panel::mmRect(panel::LCD_X, panel::LCD_Y, panel::LCD_W, panel::LCD_H);
		addChild(lcd);
		DigitDisplay* led = new DigitDisplay;
		led->module = module;
		led->box = panel::mmRect(panel::LED_X, panel::LED_Y, panel::LED_W, panel::LED_H);
		addChild(led);
		RoutingMap* map = new RoutingMap;
		map->module = module;
		map->box = panel::mmRect(panel::MAP_X, panel::MAP_Y, panel::MAP_W, panel::MAP_H);
		addChild(map);

		const Vec lit[7] = { panel::UNIT_A_POS, panel::UNIT_B_POS, panel::UNIT_C_POS, panel::UNIT_D_POS,
			panel::CONFIG_POS, panel::SYSTEM_POS, panel::EDIT_POS };
		for (int i = 0; i < 7; i++)
			addParam(createLightParamCentered<VCVLightBezel<panel::LimeLight> >(
				panel::mm(lit[i].x, lit[i].y), module, Apportionment::UNIT_A_PARAM + i, Apportionment::UNIT_A_LIGHT + i));
		const Vec plain[5] = { panel::SELECT_POS, panel::LEFT_POS, panel::RIGHT_POS, panel::CANCEL_POS, panel::WRITE_POS };
		for (int i = 0; i < 5; i++)
			addParam(createParamCentered<VCVButton>(panel::mm(plain[i].x, plain[i].y), module, Apportionment::SELECT_PARAM + i));

		DataKnob* knob = new DataKnob;
		knob->module = module;
		knob->box.pos = panel::mm(panel::DATA_POS.x, panel::DATA_POS.y).minus(knob->box.size.div(2));
		addChild(knob);

		addParam(createParamCentered<RoundBlackKnob>(panel::mm(panel::SOURCES_POS.x, panel::SOURCES_POS.y), module, Apportionment::SOURCES_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(panel::mm(panel::AB_ROUTE_POS.x, panel::AB_ROUTE_POS.y), module, Apportionment::AB_ROUTE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(panel::mm(panel::CD_ROUTE_POS.x, panel::CD_ROUTE_POS.y), module, Apportionment::CD_ROUTE_PARAM));
		addParam(createParamCentered<CKSS>(panel::mm(panel::AB_CD_POS.x, panel::AB_CD_POS.y), module, Apportionment::AB_CD_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(panel::mm(panel::AB_AMOUNT_POS.x, panel::AB_AMOUNT_POS.y), module, Apportionment::AB_AMOUNT_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(panel::mm(panel::CD_AMOUNT_POS.x, panel::CD_AMOUNT_POS.y), module, Apportionment::CD_AMOUNT_PARAM));
		addParam(createParamCentered<CKSS>(panel::mm(panel::AB_MONO_POS.x, panel::AB_MONO_POS.y), module, Apportionment::AB_MONO_PARAM));
		addParam(createParamCentered<CKSS>(panel::mm(panel::CD_MONO_POS.x, panel::CD_MONO_POS.y), module, Apportionment::CD_MONO_PARAM));
		addParam(createParamCentered<CKSS>(panel::mm(panel::AB_OUT_POS.x, panel::AB_OUT_POS.y), module, Apportionment::AB_OUT_PARAM));
		addParam(createParamCentered<CKSS>(panel::mm(panel::CD_OUT_POS.x, panel::CD_OUT_POS.y), module, Apportionment::CD_OUT_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(panel::mm(panel::IN_LEVEL_POS.x, panel::IN_LEVEL_POS.y), module, Apportionment::IN_LEVEL_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(panel::mm(panel::OUT_LEVEL_POS.x, panel::OUT_LEVEL_POS.y), module, Apportionment::OUT_LEVEL_PARAM));
		addChild(createLightCentered<SmallLight<panel::LimeLight> >(panel::mm(panel::ROUTING_POS.x, panel::ROUTING_POS.y), module, Apportionment::ROUTING_LIGHT));

		addInput(createInputCentered<panel::PortInMain>(panel::mm(panel::IN1_POS.x, panel::IN1_POS.y), module, Apportionment::IN1_INPUT));
		addInput(createInputCentered<panel::PortInMain>(panel::mm(panel::IN2_POS.x, panel::IN2_POS.y), module, Apportionment::IN2_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::IN3_POS.x, panel::IN3_POS.y), module, Apportionment::IN3_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::IN4_POS.x, panel::IN4_POS.y), module, Apportionment::IN4_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::PEDAL_IN_POS.x, panel::PEDAL_IN_POS.y), module, Apportionment::PEDAL_INPUT));
		addInput(createInputCentered<panel::PortTrigIn>(panel::mm(panel::FS_L_IN_POS.x, panel::FS_L_IN_POS.y), module, Apportionment::FS_L_INPUT));
		addInput(createInputCentered<panel::PortTrigIn>(panel::mm(panel::FS_R_IN_POS.x, panel::FS_R_IN_POS.y), module, Apportionment::FS_R_INPUT));
		addOutput(createOutputCentered<panel::PortOutMain>(panel::mm(panel::OUT1_POS.x, panel::OUT1_POS.y), module, Apportionment::OUT1_OUTPUT));
		addOutput(createOutputCentered<panel::PortOutMain>(panel::mm(panel::OUT2_POS.x, panel::OUT2_POS.y), module, Apportionment::OUT2_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::OUT3_POS.x, panel::OUT3_POS.y), module, Apportionment::OUT3_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::OUT4_POS.x, panel::OUT4_POS.y), module, Apportionment::OUT4_OUTPUT));
		const Vec taps[4] = { panel::TAP_A_POS, panel::TAP_B_POS, panel::TAP_C_POS, panel::TAP_D_POS };
		for (int u = 0; u < 4; u++)
			addOutput(createOutputCentered<panel::PortOut>(panel::mm(taps[u].x, taps[u].y), module, Apportionment::TAP_A_OUTPUT + u));
	}

	void appendContextMenu(Menu* menu) override {
		Apportionment* m = static_cast<Apportionment*>(module);
		if (!m) return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("DP/4 EPROMs (not included)"));
		menu->addChild(createMenuLabel("OS: " + (m->osPath.empty() ? std::string("none") : system::getFilename(m->osPath))));
		menu->addChild(createMenuLabel("UCODE: " + (m->ucodePath.empty() ? std::string("none") : system::getFilename(m->ucodePath))));
		menu->addChild(createMenuItem("Load EPROM folder...", "", [=]() {
			char* dir = osdialog_file(OSDIALOG_OPEN_DIR, NULL, NULL, NULL);
			if (!dir) return;
			// The OS image is 32 KB and the UCODE image 128 KB; the names vary by dump.
			for (const std::string& f : system::getEntries(dir)) {
				const int64_t size = system::getFileSize(f);
				if (size == 0x8000) m->osPath = f;
				if (size == 0x20000) m->ucodePath = f;
			}
			std::free(dir);
			m->boot();
		}));
		menu->addChild(createMenuItem("Load OS EPROM (32 KB)...", "", [=]() {
			char* p = osdialog_file(OSDIALOG_OPEN, NULL, NULL, NULL);
			if (!p) return;
			m->osPath = p;
			std::free(p);
			m->boot();
		}));
		menu->addChild(createMenuItem("Load UCODE EPROM (128 KB)...", "", [=]() {
			char* p = osdialog_file(OSDIALOG_OPEN, NULL, NULL, NULL);
			if (!p) return;
			m->ucodePath = p;
			std::free(p);
			m->boot();
		}));
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuItem("Power cycle", "", [=]() { m->boot(); }));
		menu->addChild(createMenuItem("Reinitialize (clear battery RAM)", "", [=]() { m->factoryReset(); }));
	}
};

Model* modelApportionment = createModel<Apportionment, ApportionmentWidget>("Apportionment");

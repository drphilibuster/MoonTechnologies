// Rebate -- an Alesis MIDIverb (or MIDIFEX) running its own firmware. See
// Machine.hpp for the digital board and Analog.hpp for the circuit around it.
//
// The EPROMs are Alesis's and are not distributed with the plugin: the context
// menu loads them from wherever the user keeps them, and the patch remembers the
// paths, never the contents. The unit itself remembers nothing over a power
// cycle; the patch keeps the program, MIDI channel and defeat, and the module
// plays them back into the firmware after power-on (Machine::restore).
#include "../plugin.hpp"
#include "Panel.hpp"
#include "Analog.hpp"

#include <osdialog.h>

#include <atomic>
#include <fstream>
#include <iterator>
#include <mutex>

namespace {

std::vector<uint8_t> readFile(const std::string& path) {
	std::ifstream f(path, std::ios::binary);
	if (!f) return {};
	return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}

/** A board: the machine and its analog circuit. Built on the UI thread, handed over. */
struct Unit {
	mv::Machine machine;
	mv::Analog analog;
};

} // namespace

struct Rebate : Module {
	enum ParamId { CHANNEL_PARAM, UP_PARAM, DOWN_PARAM, DEFEAT_PARAM, MIX_PARAM, PARAMS_LEN };
	enum InputId { IN_L_INPUT, IN_R_INPUT, INPUTS_LEN };
	enum OutputId { OUT_L_OUTPUT, OUT_R_OUTPUT, OUTPUTS_LEN };
	enum LightId { METER_GREEN_LIGHT, METER_RED_LIGHT, LIGHTS_LEN };

	/** Rack volts to the circuit's: a 10 V peak-to-peak signal is 1 V peak at the
	    jack, a hot line level. The output is scaled back the same way. */
	static constexpr float VOLTS = 5.f;

	std::unique_ptr<Unit> unit;                  // audio thread only
	std::atomic<Unit*> handover{nullptr};

	std::string cpuPath, dspPath;                // UI thread
	std::mutex snapMutex;
	std::string status = "LOAD MIDIVERB ROMS";   // guarded by snapMutex
	std::string romLabel;                        // guarded by snapMutex
	mv::Machine::Settings saved;                 // guarded by snapMutex: what the patch keeps
	std::atomic<uint8_t> digits[2];

	midi::InputQueue midiInput;
	bool pressed[4] = {};
	int housekeeping = 0;

	dsp::SampleRateConverter<2> inSrc, outSrc;
	dsp::DoubleRingBuffer<dsp::Frame<2>, 256> inBuf, outBuf;
	// Analog-rate input waiting to make up a whole DSP sample (four frames).
	dsp::Frame<2> carry[mv::Analog::OS * 32];
	int carryLen = 0;

	Rebate() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configButton(CHANNEL_PARAM, "Channel (hold, then UP/DOWN to set the MIDI channel)");
		configButton(UP_PARAM, "Up");
		configButton(DOWN_PARAM, "Down");
		configButton(DEFEAT_PARAM, "Defeat");
		configParam(MIX_PARAM, 0.f, 1.f, 1.f, "Mix (dry to wet)", "%", 0.f, 100.f);
		configInput(IN_L_INPUT, "Left");
		configInput(IN_R_INPUT, "Right");
		configOutput(OUT_L_OUTPUT, "Left");
		configOutput(OUT_R_OUTPUT, "Right");
		configLight(METER_GREEN_LIGHT, "-12 dB");
		configLight(METER_RED_LIGHT, "0 dB");
		configBypass(IN_L_INPUT, OUT_L_OUTPUT);
		configBypass(IN_R_INPUT, OUT_R_OUTPUT);
		digits[0] = digits[1] = 0;
	}

	~Rebate() { delete handover.exchange(nullptr); }

	void setStatus(const std::string& s) {
		std::lock_guard<std::mutex> lock(snapMutex);
		status = s;
	}

	/** Power the unit on from the images, and queue the patch's settings to be
	    played back into it. UI thread; quick enough not to need a worker. */
	void boot() {
		if (cpuPath.empty() || dspPath.empty()) { setStatus("LOAD MIDIVERB ROMS"); return; }
		const std::vector<uint8_t> cpu = readFile(cpuPath), dsp = readFile(dspPath);
		std::unique_ptr<Unit> u(new Unit);
		const std::string err = u->machine.load(cpu, dsp);
		if (!err.empty()) { setStatus(err); return; }
		u->machine.powerOn();
		std::string label = mv::romName(mv::crc32(dsp));
		label = label.find("MIDIFEX") == 0 ? "MIDIFEX" : label.empty() ? "UNKNOWN DSP ROM" : "MIDIVERB";
		{
			std::lock_guard<std::mutex> lock(snapMutex);
			u->machine.restore(saved);
			romLabel = label;
			status = "";
		}
		INFO("Rebate: %s powered on from %s + %s", label.c_str(), system::getFilename(cpuPath).c_str(),
			system::getFilename(dspPath).c_str());
		delete handover.exchange(u.release());
		rememberRoms();
	}

	/** The last images that booted are remembered plugin-wide, beside
	    Apportionment's, in MoonTechnologies/settings.json. Paths only. */
	static std::string settingsPath() { return asset::user("MoonTechnologies/settings.json"); }

	void rememberRoms() {
		const std::string path = settingsPath();
		system::createDirectories(system::getDirectory(path));
		json_t* root = system::isFile(path) ? json_load_file(path.c_str(), 0, NULL) : NULL;
		if (!root) root = json_object();
		json_object_set_new(root, "rebateCpu", json_string(cpuPath.c_str()));
		json_object_set_new(root, "rebateDsp", json_string(dspPath.c_str()));
		json_dump_file(root, path.c_str(), JSON_INDENT(2));
		json_decref(root);
	}

	void onAdd(const AddEvent& e) override {
		if (!cpuPath.empty() || handover.load()) return;
		const std::string path = settingsPath();
		json_t* root = system::isFile(path) ? json_load_file(path.c_str(), 0, NULL) : NULL;
		if (!root) return;
		json_t* c = json_object_get(root, "rebateCpu");
		json_t* d = json_object_get(root, "rebateDsp");
		if (c && d && json_is_string(c) && json_is_string(d)) {
			cpuPath = json_string_value(c);
			dspPath = json_string_value(d);
		}
		json_decref(root);
		if (!cpuPath.empty()) boot();
	}

	/** Scan a folder for the images, by size: 8 KB is the CPU's; 16 or 32 KB a DSP's.
	    MIDIFEX and MIDIverb DSP images are told apart by their CRC. */
	static void scan(const std::string& dir, std::string& cpu, std::string& verb, std::string& fex) {
		for (const std::string& f : system::getEntries(dir)) {
			const int64_t size = system::getFileSize(f);
			if (size == 0x2000) cpu = f;
			if (size == 0x4000 || size == 0x8000) {
				const std::string name = mv::romName(mv::crc32(readFile(f)));
				if (name.find("MIDIFEX") == 0) fex = f;
				else verb = f;
			}
		}
	}

	// --- audio ---------------------------------------------------------------------------
	void process(const ProcessArgs& args) override {
		if (Unit* u = handover.exchange(nullptr)) {
			unit.reset(u);
			inBuf.clear();
			outBuf.clear();
			carryLen = 0;
			for (bool& p : pressed) p = false;
		}
		midi::Message msg;
		if (!unit) {
			while (midiInput.tryPop(&msg, args.frame)) {}
			outputs[OUT_L_OUTPUT].setVoltage(0.f);
			outputs[OUT_R_OUTPUT].setVoltage(0.f);
			return;
		}
		mv::Machine& m = unit->machine;

		// The front panel. While a patch's settings are being played in, the
		// buttons are the module's; a press then would only fight it.
		if (!m.replaying())
			for (int i = 0; i < 4; i++) {
				const bool down = params[CHANNEL_PARAM + i].getValue() > 0.5f;
				if (down != pressed[i]) { m.button(mv::Machine::Button(i), down); pressed[i] = down; }
			}
		while (midiInput.tryPop(&msg, args.frame))
			for (int i = 0; i < msg.getSize(); i++) m.midi(msg.bytes[i]);
		unit->analog.mix = params[MIX_PARAM].getValue();

		dsp::Frame<2> fin;
		fin.samples[0] = inputs[IN_L_INPUT].getVoltage() / VOLTS;
		fin.samples[1] = inputs[IN_R_INPUT].getVoltage() / VOLTS;
		if (!inBuf.full()) inBuf.push(fin);

		// Run the board at its own rate, a block at a time.
		if (outBuf.size() < 16) {
			const int fs = int(mv::Analog::FS);
			inSrc.setRates(int(args.sampleRate), fs);
			outSrc.setRates(fs, int(args.sampleRate));
			int inLen = int(inBuf.size()), cLen = int(sizeof(carry) / sizeof(carry[0])) - carryLen;
			inSrc.process(inBuf.startData(), &inLen, carry + carryLen, &cLen);
			inBuf.startIncr(inLen);
			carryLen += cLen;
			const int samples = carryLen / mv::Analog::OS;
			dsp::Frame<2> aOut[sizeof(carry) / sizeof(carry[0])];
			for (int s = 0; s < samples; s++) {
				float x[mv::Analog::OS][2], y[mv::Analog::OS][2];
				for (int k = 0; k < mv::Analog::OS; k++)
					for (int c = 0; c < 2; c++) x[k][c] = carry[s * mv::Analog::OS + k].samples[c];
				unit->analog.run(m, x, y);
				for (int k = 0; k < mv::Analog::OS; k++)
					for (int c = 0; c < 2; c++) aOut[s * mv::Analog::OS + k].samples[c] = y[k][c];
			}
			const int used = samples * mv::Analog::OS;
			for (int i = used; i < carryLen; i++) carry[i - used] = carry[i];
			carryLen -= used;
			int frames = used, oLen = int(outBuf.capacity());
			outSrc.process(aOut, &frames, outBuf.endData(), &oLen);
			outBuf.endIncr(oLen);
		}
		dsp::Frame<2> fo = {};
		if (!outBuf.empty()) fo = outBuf.shift();
		outputs[OUT_L_OUTPUT].setVoltage(fo.samples[0] * VOLTS);
		outputs[OUT_R_OUTPUT].setVoltage(fo.samples[1] * VOLTS);

		// Housekeeping: the LEDs, the digits, and what the patch should keep.
		if (++housekeeping >= 64) {
			housekeeping = 0;
			const float dt = args.sampleTime * 64;
			lights[METER_GREEN_LIGHT].setBrightnessSmooth(unit->analog.meterGreen, dt);
			lights[METER_RED_LIGHT].setBrightnessSmooth(unit->analog.meterRed, dt);
			digits[0] = m.segments(0);
			digits[1] = m.segments(1);
			if (!m.replaying() && snapMutex.try_lock()) {
				saved = m.settings();
				snapMutex.unlock();
			}
		}
	}

	// --- the patch ---------------------------------------------------------------------------
	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "cpuPath", json_string(cpuPath.c_str()));
		json_object_set_new(root, "dspPath", json_string(dspPath.c_str()));
		{
			std::lock_guard<std::mutex> lock(snapMutex);
			json_object_set_new(root, "program", json_integer(saved.program));
			json_object_set_new(root, "channel", json_integer(saved.channel));
			json_object_set_new(root, "defeat", json_boolean(saved.defeat));
		}
		json_object_set_new(root, "midi", midiInput.toJson());
		return root;
	}

	void dataFromJson(json_t* root) override {
		if (json_t* j = json_object_get(root, "cpuPath")) cpuPath = json_string_value(j);
		if (json_t* j = json_object_get(root, "dspPath")) dspPath = json_string_value(j);
		{
			std::lock_guard<std::mutex> lock(snapMutex);
			if (json_t* j = json_object_get(root, "program")) saved.program = clamp(int(json_integer_value(j)), 0, 62);
			if (json_t* j = json_object_get(root, "channel")) saved.channel = clamp(int(json_integer_value(j)), 0, 15);
			if (json_t* j = json_object_get(root, "defeat")) saved.defeat = json_boolean_value(j);
		}
		if (json_t* j = json_object_get(root, "midi")) midiInput.fromJson(j);
		boot();
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		{
			std::lock_guard<std::mutex> lock(snapMutex);
			saved = mv::Machine::Settings();
		}
		boot();
	}
};

// --- widgets ------------------------------------------------------------------------------

namespace {

/** The MIDIverb's two seven-segment digits, segment by segment as the firmware lights them. */
struct DigitDisplay : widget::Widget {
	Rebate* module = nullptr;

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) return;
		uint8_t seg[2] = { 0, 0 };
		if (module) { seg[0] = module->digits[0]; seg[1] = module->digits[1]; }
		else { seg[0] = 0x5b; seg[1] = 0x5b; }   // "22", the unit's power-on program
		NVGcontext* vg = args.vg;
		const float dw = box.size.x * 0.27f, dh = box.size.y * 0.70f, t = dw * 0.17f;
		for (int dig = 0; dig < 2; dig++) {
			const float x0 = box.size.x * (dig ? 0.56f : 0.17f), y0 = box.size.y * 0.15f;
			// a b c d e f g, each as (x, y, w, h) in the digit's box
			const float r[7][4] = {
				{ t, 0, dw - 2 * t, t }, { dw - t, t, t, dh / 2 - 1.5f * t }, { dw - t, dh / 2 + 0.5f * t, t, dh / 2 - 1.5f * t },
				{ t, dh - t, dw - 2 * t, t }, { 0, dh / 2 + 0.5f * t, t, dh / 2 - 1.5f * t }, { 0, t, t, dh / 2 - 1.5f * t },
				{ t, dh / 2 - t / 2, dw - 2 * t, t },
			};
			for (int k = 0; k < 7; k++) {
				const bool on = seg[dig] >> k & 1;
				nvgBeginPath(vg);
				nvgRect(vg, x0 + r[k][0], y0 + r[k][1], r[k][2], r[k][3]);
				nvgFillColor(vg, on ? panel::CLAY : panel::alpha(panel::SAGE, 0.12f));
				nvgFill(vg);
			}
		}
	}
};

/** One line: what the module needs, or which EPROM it is running. */
struct StatusLine : widget::Widget {
	Rebate* module = nullptr;

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) return;
		std::string s = "ALESIS MIDIVERB", label;
		NVGcolor ink = panel::SAGE;
		if (module) {
			std::lock_guard<std::mutex> lock(module->snapMutex);
			if (!module->status.empty()) { s = module->status + " - RIGHT-CLICK"; ink = panel::CLAY; }
			else s = module->romLabel;
		}
		const panel::TextStyle st(panel::Face::Mono, box.size.y * 0.55f, ink, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		panel::text(args.vg, st, box.size.x / 2, box.size.y / 2, s);
	}
};

} // namespace

struct RebateWidget : ModuleWidget {
	RebateWidget(Rebate* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Rebate.svg")));
		panel::addScrews(this);
		panel::addLabels(this);

		DigitDisplay* d = new DigitDisplay;
		d->module = module;
		d->box = panel::mmRect(panel::DIG_X, panel::DIG_Y, panel::DIG_W, panel::DIG_H);
		addChild(d);
		StatusLine* s = new StatusLine;
		s->module = module;
		s->box = panel::mmRect(panel::TXT_X, panel::TXT_Y, panel::TXT_W, panel::TXT_H);
		addChild(s);

		addChild(createLightCentered<MediumLight<panel::LimeLight> >(panel::mm(panel::METER_GREEN_POS.x, panel::METER_GREEN_POS.y), module, Rebate::METER_GREEN_LIGHT));
		addChild(createLightCentered<MediumLight<panel::ClayLight> >(panel::mm(panel::METER_RED_POS.x, panel::METER_RED_POS.y), module, Rebate::METER_RED_LIGHT));
		const Vec buttons[4] = { panel::CHANNEL_POS, panel::UP_POS, panel::DOWN_POS, panel::DEFEAT_POS };
		for (int i = 0; i < 4; i++)
			addParam(createParamCentered<VCVButton>(panel::mm(buttons[i].x, buttons[i].y), module, Rebate::CHANNEL_PARAM + i));
		addParam(createParamCentered<RoundLargeBlackKnob>(panel::mm(panel::MIX_POS.x, panel::MIX_POS.y), module, Rebate::MIX_PARAM));

		addInput(createInputCentered<panel::PortInMain>(panel::mm(panel::IN_L_POS.x, panel::IN_L_POS.y), module, Rebate::IN_L_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::IN_R_POS.x, panel::IN_R_POS.y), module, Rebate::IN_R_INPUT));
		addOutput(createOutputCentered<panel::PortOutMain>(panel::mm(panel::OUT_L_POS.x, panel::OUT_L_POS.y), module, Rebate::OUT_L_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::OUT_R_POS.x, panel::OUT_R_POS.y), module, Rebate::OUT_R_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		Rebate* m = static_cast<Rebate*>(module);
		if (!m) return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("MIDIverb EPROMs (not included)"));
		menu->addChild(createMenuLabel("CPU: " + (m->cpuPath.empty() ? std::string("none") : system::getFilename(m->cpuPath))));
		menu->addChild(createMenuLabel("DSP: " + (m->dspPath.empty() ? std::string("none") : system::getFilename(m->dspPath))));
		auto folder = [=](bool fex) {
			char* dir = osdialog_file(OSDIALOG_OPEN_DIR, NULL, NULL, NULL);
			if (!dir) return;
			std::string cpu, verb, fex2;
			Rebate::scan(dir, cpu, verb, fex2);
			std::free(dir);
			if (!cpu.empty()) m->cpuPath = cpu;
			const std::string dsp = fex ? (fex2.empty() ? verb : fex2) : (verb.empty() ? fex2 : verb);
			if (!dsp.empty()) m->dspPath = dsp;
			m->boot();
		};
		menu->addChild(createMenuItem("Load EPROM folder as MIDIverb...", "", [=]() { folder(false); }));
		menu->addChild(createMenuItem("Load EPROM folder as MIDIFEX...", "", [=]() { folder(true); }));
		menu->addChild(createMenuItem("Load CPU EPROM (MVOP, 8 KB)...", "", [=]() {
			char* p = osdialog_file(OSDIALOG_OPEN, NULL, NULL, NULL);
			if (!p) return;
			m->cpuPath = p;
			std::free(p);
			m->boot();
		}));
		menu->addChild(createMenuItem("Load DSP EPROM (MVOBJ or MIDIFEX, 16 KB)...", "", [=]() {
			char* p = osdialog_file(OSDIALOG_OPEN, NULL, NULL, NULL);
			if (!p) return;
			m->dspPath = p;
			std::free(p);
			m->boot();
		}));
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("MIDI in (program changes, channel set by CHANNEL)"));
		appendMidiMenu(menu, &m->midiInput);
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuItem("Power cycle", "", [=]() { m->boot(); }));
	}
};

Model* modelRebate = createModel<Rebate, RebateWidget>("Rebate");

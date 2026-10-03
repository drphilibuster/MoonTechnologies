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

#include <algorithm>
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

/** A control that has no end: its tooltip says what the field does rather than a number of detents. */
struct ChannelQuantity : ParamQuantity {
	std::string getDisplayValueString() override { return "drag, or click to pick"; }
	std::string getUnit() override { return ""; }
};

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
	long chanSeen = 0;                           // the CHANNEL field's count at the last look
	bool chanInit = false;
	int housekeeping = 0;

	dsp::SampleRateConverter<2> inSrc, outSrc;
	dsp::DoubleRingBuffer<dsp::Frame<2>, 256> inBuf, outBuf;
	// Analog-rate input waiting to make up a whole DSP sample (four frames).
	dsp::Frame<2> carry[mv::Analog::OS * 32];
	int carryLen = 0;

	Rebate() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		// On the unit CHANNEL is held while UP or DOWN is pressed, which a mouse cannot do: each step of this holds CHANNEL
		// and presses the one key, and the digits show the channel while it does.
		configParam<ChannelQuantity>(CHANNEL_PARAM, -INFINITY, INFINITY, 0.f, "MIDI receive channel");
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

	/** Standing quick-load folders: drop EPROM images in, pick them from the menu.
	    MoonTechnologies/roms/midiverb/{cpu,dsp} in Rack's user folder. */
	static std::string romDir(const char* kind) {
		return asset::user("MoonTechnologies/roms/midiverb/") + kind;
	}

	/** Images in a quick-load folder, sorted; `cpu` picks the 8 KB CPU images,
	    otherwise the 16/32 KB DSP images. */
	static std::vector<std::string> romsIn(bool cpu) {
		std::vector<std::string> out;
		const std::string dir = romDir(cpu ? "cpu" : "dsp");
		system::createDirectories(dir);
		for (const std::string& f : system::getEntries(dir)) {
			const int64_t size = system::getFileSize(f);
			if (cpu ? size == 0x2000 : (size == 0x4000 || size == 0x8000)) out.push_back(f);
		}
		std::sort(out.begin(), out.end());
		return out;
	}

	void onAdd(const AddEvent& e) override {
		if (!cpuPath.empty() || handover.load()) return;
		const std::string path = settingsPath();
		json_t* root = system::isFile(path) ? json_load_file(path.c_str(), 0, NULL) : NULL;
		if (root) {
			json_t* c = json_object_get(root, "rebateCpu");
			json_t* d = json_object_get(root, "rebateDsp");
			if (c && d && json_is_string(c) && json_is_string(d)) {
				cpuPath = json_string_value(c);
				dspPath = json_string_value(d);
			}
			json_decref(root);
		}
		// Nothing remembered: take the first images in the quick-load folders.
		if (cpuPath.empty()) {
			const std::vector<std::string> cpus = romsIn(true), dsps = romsIn(false);
			if (!cpus.empty() && !dsps.empty()) {
				cpuPath = cpus[0];
				dspPath = dsps[0];
			}
		}
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
		// CHANNEL's steps since the last sample (not a jump when a patch loads with it somewhere). At most
		// fifteen: a pick from the screen's channel list is one jump across the whole range.
		int chanClicks = 0;
		{
			const long v = long(std::floor(double(params[CHANNEL_PARAM].getValue()) + 0.5));
			if (chanInit) chanClicks = int(std::max(-15L, std::min(15L, v - chanSeen)));
			chanSeen = v;
			chanInit = true;
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
		for (int c = 0; c < std::abs(chanClicks); c++) m.stepChannel(chanClicks > 0 ? 1 : -1);
		if (!m.replaying())
			for (int i = 1; i < 4; i++) {   // UP, DOWN, DEFEAT: CHANNEL is a count, not a button
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

/** The read-out: the MIDIverb's two seven-segment digits, segment by segment as the firmware
 *  lights them, with CHANNEL and DEFEAT beside them and the module's status line below. The
 *  digits, CHANNEL and DEFEAT are the FIELD_* cells (src/Rebate/Panel.hpp), and the fields that
 *  take the mouse sit on the same rectangles: the top half of the digits is UP, the bottom half
 *  DOWN. Text goes through panel:: only. */
struct RebateDisplay : widget::Widget {
	Rebate* module = nullptr;

	static void digits(NVGcontext* vg, const Rect& box, const uint8_t seg[2]) {
		const float dw = box.size.x * 0.27f, dh = box.size.y * 0.70f, t = dw * 0.17f;
		for (int dig = 0; dig < 2; dig++) {
			const float x0 = box.pos.x + box.size.x * (dig ? 0.56f : 0.17f), y0 = box.pos.y + box.size.y * 0.15f;
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

	/** A small arrow at the digits' right edge: which way the half under it steps. */
	static void arrow(NVGcontext* vg, const Rect& c, int dir) {
		const float cx = c.pos.x + c.size.x - 6.f, cy = c.pos.y + c.size.y / 2.f, a = 2.6f;
		nvgBeginPath(vg);
		nvgMoveTo(vg, cx, cy - dir * a);
		nvgLineTo(vg, cx + a, cy + dir * a * 0.7f);
		nvgLineTo(vg, cx - a, cy + dir * a * 0.7f);
		nvgClosePath(vg);
		nvgFillColor(vg, panel::alpha(panel::SAGE, 0.45f));
		nvgFill(vg);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) return;
		NVGcontext* vg = args.vg;
		uint8_t seg[2] = { 0x5b, 0x5b };   // "22", the unit's power-on program
		std::string s = "ALESIS MIDIVERB";
		NVGcolor ink = panel::SAGE;
		int channel = 0;
		bool defeat = false;
		if (module) {
			seg[0] = module->digits[0];
			seg[1] = module->digits[1];
			std::lock_guard<std::mutex> lock(module->snapMutex);
			if (!module->status.empty()) { s = module->status + " - RIGHT-CLICK"; ink = panel::CLAY; }
			else s = module->romLabel;
			channel = module->saved.channel;
			defeat = module->saved.defeat;
		}

		// The digits span UP over DOWN: one number, pressed on the half you want it to go.
		const Rect up = panel::inGlass(panel::FIELD_UP), down = panel::inGlass(panel::FIELD_DOWN);
		const Rect dig(up.pos, Vec(up.size.x, down.pos.y + down.size.y - up.pos.y));
		digits(vg, dig, seg);
		arrow(vg, up, +1);
		arrow(vg, down, -1);

		// CHANNEL: its caption over the MIDI receive channel, 1 to 16.
		const panel::TextStyle TAG(panel::Face::Mono, 7.f, panel::SAGE, NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE);
		const panel::TextStyle VAL(panel::Face::Mono, 10.f, panel::LIME, NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE);
		const Rect ch = panel::inGlass(panel::FIELD_CHANNEL);
		panel::text(vg, TAG, ch.pos.x + ch.size.x / 2, ch.pos.y + ch.size.y * 0.36f, "CH");
		panel::text(vg, VAL, ch.pos.x + ch.size.x / 2, ch.pos.y + ch.size.y * 0.72f, string::f("%d", channel + 1));

		// DEFEAT: lit clay while the effect is defeated, as the digits' "--" says too.
		const Rect df = panel::inGlass(panel::FIELD_DEFEAT);
		const panel::TextStyle DEF(panel::Face::Mono, 7.f, defeat ? panel::CLAY : panel::SAGE,
			NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE);
		panel::text(vg, DEF, df.pos.x + df.size.x / 2, df.pos.y + df.size.y * 0.36f, "DEF");
		panel::text(vg, DEF, df.pos.x + df.size.x / 2, df.pos.y + df.size.y * 0.72f, defeat ? "ON" : "OFF");

		// The status line: the glass's last row, under the fields and not one of them -- it
		// says to right-click the module, which a field would answer with a param menu.
		const Rect cl = panel::inGlass(panel::FIELD_CHANNEL), de = panel::inGlass(panel::FIELD_DEFEAT);
		const float top = cl.pos.y + cl.size.y + mm2px(0.5f), bottom = box.size.y - mm2px(0.8f);
		const float left = cl.pos.x, right = de.pos.x + de.size.x;
		const panel::TextStyle st(panel::Face::Mono, (bottom - top) * 0.55f, ink, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		panel::text(vg, st, (left + right) / 2, (top + bottom) / 2, s);
	}
};

/** CHANNEL on the screen. The param is the old stepper's endless count, which the module turns
 *  into one CHANNEL-held UP or DOWN per step; dragging walks it a channel at a time, and a click
 *  lists the sixteen channels and steps the count by the difference, so the firmware still sees
 *  only the unit's own presses. */
struct ChannelField : panel::ScreenSelect {
	void click() override {
		Rebate* m = dynamic_cast<Rebate*>(module);
		engine::ParamQuantity* pq = getParamQuantity();
		if (!m || !pq) return;
		int cur;
		{
			std::lock_guard<std::mutex> lock(m->snapMutex);
			cur = m->saved.channel;
		}
		ui::Menu* menu = createMenu();
		menu->addChild(createMenuLabel("MIDI receive channel"));
		const int id = paramId;
		const int64_t moduleId = m->id;
		for (int i = 0; i < 16; i++)
			menu->addChild(createCheckMenuItem(string::f("Channel %d", i + 1), "",
				[=]() { return i == cur; },
				[=]() {
					engine::Module* mod = APP->engine->getModule(moduleId);
					if (!mod || i == cur) return;
					engine::ParamQuantity* q = mod->paramQuantities[id];
					const float before = q->getValue();
					q->setValue(std::round(before) + float(i - cur));
					history::ParamChange* h = new history::ParamChange;
					h->name = "select MIDI receive channel";
					h->moduleId = moduleId;
					h->paramId = id;
					h->oldValue = before;
					h->newValue = q->getValue();
					APP->history->push(h);
				}));
	}
};

} // namespace

struct RebateWidget : ModuleWidget {
	RebateWidget(Rebate* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Rebate.svg")));
		panel::addScrews(this);
		panel::addLabels(this);

		RebateDisplay* d = new RebateDisplay;
		d->module = module;
		d->box = panel::mmRect(panel::GLASS_X, panel::GLASS_Y, panel::GLASS_W, panel::GLASS_H);
		addChild(d);

		// The unit's four buttons, on the read-out over the cells it draws them in. Each is the
		// param its panel button was, so the firmware's key path is unchanged.
		addParam(panel::createField<ChannelField>(panel::FIELD_CHANNEL, module, Rebate::CHANNEL_PARAM));
		addParam(panel::createField<panel::ScreenButton>(panel::FIELD_UP, module, Rebate::UP_PARAM));
		addParam(panel::createField<panel::ScreenButton>(panel::FIELD_DOWN, module, Rebate::DOWN_PARAM));
		addParam(panel::createField<panel::ScreenButton>(panel::FIELD_DEFEAT, module, Rebate::DEFEAT_PARAM));

		addChild(createLightCentered<MediumLight<panel::LimeLight> >(panel::mm(panel::METER_GREEN_POS.x, panel::METER_GREEN_POS.y), module, Rebate::METER_GREEN_LIGHT));
		addChild(createLightCentered<MediumLight<panel::ClayLight> >(panel::mm(panel::METER_RED_POS.x, panel::METER_RED_POS.y), module, Rebate::METER_RED_LIGHT));
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
		menu->addChild(createSubmenuItem("Quick load: CPU ROMs", "", [=](Menu* sub) {
			const std::vector<std::string> roms = Rebate::romsIn(true);
			for (const std::string& f : roms)
				sub->addChild(createCheckMenuItem(system::getFilename(f), "", [=]() { return m->cpuPath == f; }, [=]() {
					m->cpuPath = f;
					m->boot();
				}));
			if (roms.empty()) sub->addChild(createMenuLabel("empty: drop 8 KB CPU images in the folder"));
			sub->addChild(new MenuSeparator);
			sub->addChild(createMenuItem("Open CPU folder", "", [=]() { system::openDirectory(Rebate::romDir("cpu")); }));
		}));
		menu->addChild(createSubmenuItem("Quick load: DSP ROMs", "", [=](Menu* sub) {
			const std::vector<std::string> roms = Rebate::romsIn(false);
			for (const std::string& f : roms) {
				const std::string name = mv::romName(mv::crc32(readFile(f)));
				sub->addChild(createCheckMenuItem(system::getFilename(f) + (name.empty() ? "" : "  [" + name + "]"), "",
					[=]() { return m->dspPath == f; }, [=]() {
						m->dspPath = f;
						m->boot();
					}));
			}
			if (roms.empty()) sub->addChild(createMenuLabel("empty: drop 16/32 KB MIDIverb or MIDIFEX images in the folder"));
			sub->addChild(new MenuSeparator);
			sub->addChild(createMenuItem("Open DSP folder", "", [=]() { system::openDirectory(Rebate::romDir("dsp")); }));
		}));
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

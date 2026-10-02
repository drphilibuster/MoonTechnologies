// Nordic Banking -- a Clavia Nord Lead 2X running its own firmware. See Nord2x.hpp for the board.
//
// The OS image is Clavia's and is not distributed with the plugin: the context menu loads it from
// wherever the user keeps it, and the patch remembers the path, never the contents. The unit's
// program memory -- the 64 KB flash -- is saved in the patch, as the flash would keep it. Clavia's
// factory programs are SysEx the user loads from the menu into the firmware, as on the unit.
#include "../plugin.hpp"
#include "Panel.hpp"
#include "PanelMap.hpp"
#include "Nord2x.hpp"

#include <osdialog.h>

#include <atomic>
#include <cstring>
#include <fstream>
#include <iterator>
#include <mutex>
#include <thread>

namespace {

std::vector<uint8_t> readFile(const std::string& path) {
	std::ifstream f(path, std::ios::binary);
	if (!f) return {};
	return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}

/** What the UI thread draws: the three digits' segments. */
struct Snapshot {
	float digits[3][8] = {};
};

} // namespace

struct NordicBanking : Module {
	enum ParamId { KNOB_PARAM, BUTTON_PARAM = KNOB_PARAM + 26, PARAMS_LEN = BUTTON_PARAM + 28 };
	enum InputId { PEDAL_INPUT, INPUTS_LEN };
	enum OutputId { OUT_A_OUTPUT, OUT_B_OUTPUT, OUT_C_OUTPUT, OUT_D_OUTPUT, OUTPUTS_LEN };
	enum LightId { LED_LIGHT, LIGHTS_LEN = LED_LIGHT + nb::NUM_LEDS };

	static constexpr float VOLTS = 5.f;   // the DSP's full scale, as a Rack audio level

	std::unique_ptr<nb::Nord2x> unit;           // audio thread only
	std::atomic<nb::Nord2x*> handover{nullptr};
	std::thread bootThread;
	std::atomic<bool> booting{false};

	std::string imagePath;                      // UI thread
	std::mutex snapMutex;
	std::string status = "LOAD A NORD LEAD 2X OS";   // guarded by snapMutex
	std::vector<uint8_t> flash;                 // guarded by snapMutex: what the patch keeps
	Snapshot snap;                              // guarded by snapMutex
	std::vector<std::vector<uint8_t>> pendingSysex;   // guarded by snapMutex: from the menu, sent by process()

	midi::InputQueue midiInput;
	bool buttonDown[28] = {};
	uint8_t knobSent[26], pedalSent = 0xff;
	int housekeeping = 0, flashCountdown = 0;

	dsp::SampleRateConverter<4> outSrc;
	dsp::DoubleRingBuffer<dsp::Frame<4>, 256> outBuf;

	NordicBanking() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		for (int i = 0; i < 26; i++)
			configParam(KNOB_PARAM + i, 0.f, 1.f, nb::KNOBS[i].initial / 255.f, nb::KNOBS[i].name, "%", 0.f, 100.f);
		for (int i = 0; i < 28; i++) configButton(BUTTON_PARAM + i, nb::BUTTONS[i].name);
		configInput(PEDAL_INPUT, "Expression pedal (0-10 V)");
		configOutput(OUT_A_OUTPUT, "Out A");
		configOutput(OUT_B_OUTPUT, "Out B");
		configOutput(OUT_C_OUTPUT, "Out C");
		configOutput(OUT_D_OUTPUT, "Out D");
		std::memset(knobSent, 0xff, sizeof(knobSent));
	}

	~NordicBanking() {
		if (bootThread.joinable()) bootThread.join();
		delete handover.exchange(nullptr);
	}

	void setStatus(const std::string& s) {
		std::lock_guard<std::mutex> lock(snapMutex);
		status = s;
	}

	/** Power the unit on from the image and the saved flash, on a worker thread: the firmware
	    takes about a second of machine time to boot the DSPs. */
	void boot() {
		if (imagePath.empty()) { setStatus("LOAD A NORD LEAD 2X OS"); return; }
		if (bootThread.joinable()) bootThread.join();
		std::vector<uint8_t> f;
		{
			std::lock_guard<std::mutex> lock(snapMutex);
			f = flash;
			status = "POWERING ON";
		}
		booting = true;
		const std::string path = imagePath;
		bootThread = std::thread([this, path, f]() {
			const std::vector<uint8_t> rom = readFile(path);
			const std::string err = nb::Nord2x::check(rom);
			if (!err.empty()) { setStatus(err); booting = false; return; }
			std::unique_ptr<nb::Nord2x> n(new nb::Nord2x);
			if (!n->boot(rom, f)) { setStatus("THE OS DID NOT START"); booting = false; return; }
			INFO("Nordic Banking: Nord Lead 2X OS booted from %s", system::getFilename(path).c_str());
			delete handover.exchange(n.release());
			setStatus("");
			rememberImage();
			booting = false;
		});
	}

	uint8_t knobCode(int i) { return uint8_t(clamp(params[KNOB_PARAM + i].getValue(), 0.f, 1.f) * 255.f + 0.5f); }

	static std::string settingsPath() { return asset::user("MoonTechnologies/settings.json"); }
	void rememberImage() {
		const std::string path = settingsPath();
		system::createDirectories(system::getDirectory(path));
		json_t* root = system::isFile(path) ? json_load_file(path.c_str(), 0, NULL) : NULL;
		if (!root) root = json_object();
		json_object_set_new(root, "nordicBankingOs", json_string(imagePath.c_str()));
		json_dump_file(root, path.c_str(), JSON_INDENT(2));
		json_decref(root);
	}
	void onAdd(const AddEvent& e) override {
		if (!imagePath.empty() || booting || handover.load()) return;
		const std::string path = settingsPath();
		json_t* root = system::isFile(path) ? json_load_file(path.c_str(), 0, NULL) : NULL;
		if (!root) return;
		if (json_t* o = json_object_get(root, "nordicBankingOs")) if (json_is_string(o)) imagePath = json_string_value(o);
		json_decref(root);
		if (!imagePath.empty()) boot();
	}

	/** Erase the program memory: the unit powers on with an erased flash, as a new one would
	    before its programs were loaded. */
	void eraseFlash() {
		{
			std::lock_guard<std::mutex> lock(snapMutex);
			flash.clear();
		}
		boot();
	}

	/** Every F0 ... F7 message in a .syx file, queued for the firmware's MIDI input. */
	bool loadSysex(const std::string& path) {
		const std::vector<uint8_t> d = readFile(path);
		std::vector<std::vector<uint8_t>> msgs;
		for (size_t i = 0; i < d.size(); i++) {
			if (d[i] != 0xF0) continue;
			size_t j = i + 1;
			while (j < d.size() && d[j] != 0xF7) j++;
			if (j >= d.size()) break;
			msgs.emplace_back(d.begin() + long(i), d.begin() + long(j) + 1);
			i = j;
		}
		if (msgs.empty()) return false;
		std::lock_guard<std::mutex> lock(snapMutex);
		for (auto& m : msgs) pendingSysex.push_back(std::move(m));
		return true;
	}

	// --- audio -----------------------------------------------------------------------------
	void process(const ProcessArgs& args) override {
		if (nb::Nord2x* n = handover.exchange(nullptr)) {
			unit.reset(n);
			outBuf.clear();
			std::memset(knobSent, 0xff, sizeof(knobSent));
			pedalSent = 0xff;
			for (bool& b : buttonDown) b = false;
		}
		midi::Message msg;
		if (!unit) {
			while (midiInput.tryPop(&msg, args.frame)) {}
			for (int i = 0; i < OUTPUTS_LEN; i++) outputs[i].setVoltage(0.f);
			return;
		}
		nb::Nord2x& n = *unit;

		// The front panel: knobs into the ADC, buttons onto the key lines, the pedal input.
		for (int i = 0; i < 26; i++) {
			const uint8_t c = knobCode(i);
			if (c != knobSent[i]) { n.setKnob(nb::KNOBS[i].channel, c); knobSent[i] = c; }
		}
		for (int i = 0; i < 28; i++) {
			const bool down = params[BUTTON_PARAM + i].getValue() > 0.5f;
			if (down != buttonDown[i]) { n.setButton(nb::BUTTONS[i].id, down); buttonDown[i] = down; }
		}
		const uint8_t pedal = inputs[PEDAL_INPUT].isConnected()
			? uint8_t(clamp(inputs[PEDAL_INPUT].getVoltage() / 10.f, 0.f, 1.f) * 255.f + 0.5f) : 0;
		if (pedal != pedalSent) { n.setKnob(nb::PEDAL_CHANNEL, pedal); pedalSent = pedal; }
		while (midiInput.tryPop(&msg, args.frame))
			if (msg.getSize() > 0) n.midi(msg.bytes.data(), size_t(msg.getSize()));

		// Run the machine at its own rate, a block at a time.
		if (outBuf.size() < 16) {
			outSrc.setRates(int(nb::Nord2x::SAMPLE_RATE), int(args.sampleRate));
			const int mLen = 32;
			float o[4][mLen];
			float* const outs[4] = { o[0], o[1], o[2], o[3] };
			n.process(outs, mLen);
			dsp::Frame<4> mOut[mLen];
			for (int k = 0; k < mLen; k++) for (int c = 0; c < 4; c++) mOut[k].samples[c] = o[c][k];
			int frames = mLen, oLen = int(outBuf.capacity());
			outSrc.process(mOut, &frames, outBuf.endData(), &oLen);
			outBuf.endIncr(oLen);
		}
		dsp::Frame<4> fo = {};
		if (!outBuf.empty()) fo = outBuf.shift();
		for (int c = 0; c < 4; c++) outputs[OUT_A_OUTPUT + c].setVoltage(fo.samples[c] * VOLTS);

		// Housekeeping, about 40 times a second: SysEx from the menu, the LEDs, the display and
		// the flash.
		if (++housekeeping >= int(args.sampleRate / 40)) {
			housekeeping = 0;
			float rows[6][8], digits[3][8];
			n.leds(rows, digits);
			for (int i = 0; i < nb::NUM_LEDS; i++) {
				const nb::Led& l = nb::LEDS[i];
				lights[LED_LIGHT + i].setBrightness(l.row < 6 ? rows[l.row][l.bit] : 0.f);
			}
			if (snapMutex.try_lock()) {
				std::memcpy(snap.digits, digits, sizeof digits);
				// One message per tick: the firmware takes SysEx as a unit would, at MIDI's pace.
				if (!pendingSysex.empty()) {
					n.midi(pendingSysex.front().data(), pendingSysex.front().size());
					pendingSysex.erase(pendingSysex.begin());
				}
				// 64 KB of flash: copy it for the patch every few seconds.
				if (--flashCountdown <= 0) { n.copyFlash(flash); flashCountdown = 120; }
				snapMutex.unlock();
			}
		}
	}

	// --- the patch -------------------------------------------------------------------------
	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "osPath", json_string(imagePath.c_str()));
		std::lock_guard<std::mutex> lock(snapMutex);
		if (!flash.empty()) json_object_set_new(root, "flash", json_string(string::toBase64(flash).c_str()));
		json_object_set_new(root, "midi", midiInput.toJson());
		return root;
	}

	void dataFromJson(json_t* root) override {
		if (json_t* j = json_object_get(root, "osPath")) imagePath = json_string_value(j);
		{
			std::lock_guard<std::mutex> lock(snapMutex);
			flash.clear();
			if (json_t* j = json_object_get(root, "flash")) flash = string::fromBase64(json_string_value(j));
		}
		if (json_t* j = json_object_get(root, "midi")) midiInput.fromJson(j);
		boot();
	}
};

// --- widgets ------------------------------------------------------------------------------------

namespace {

/** The unit's three seven-segment digits, each segment as bright as the multiplex drives it.
    Segment bits as the firmware writes them: 7 top, 1 middle, 4 bottom, 2 upper left, 6 upper
    right, 3 lower left, 5 lower right, 0 point. */
struct SevenSegment : widget::Widget {
	NordicBanking* module = nullptr;

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) return;
		NVGcontext* vg = args.vg;
		Snapshot s;
		std::string status = "NORD LEAD 2X";
		if (module) {
			std::lock_guard<std::mutex> lock(module->snapMutex);
			s = module->snap;
			status = module->status;
		}
		if (!module || !status.empty()) {
			const panel::TextStyle st(panel::Face::Mono, box.size.y * 0.3f, panel::CLAY, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
			panel::text(vg, st, box.size.x / 2, box.size.y * 0.32f, module ? status : "NORD LEAD 2X");
			panel::text(vg, st.inked(panel::SAGE), box.size.x / 2, box.size.y * 0.74f,
				!module ? "NORDIC BANKING" : module->booting ? "PLEASE WAIT" : "RIGHT-CLICK: LOAD OS");
			return;
		}
		const float h = box.size.y * 0.86f, w = h * 0.52f, t = h * 0.11f, gap = box.size.x / 3.f;
		for (int d = 0; d < 3; d++) {
			const float x0 = gap * d + (gap - w) / 2 - t / 2, y0 = (box.size.y - h) / 2;
			auto seg = [&](int bit, float x, float y, float sw, float sh) {
				const float b = s.digits[d][bit];
				nvgBeginPath(vg);
				nvgRoundedRect(vg, x0 + x, y0 + y, sw, sh, t * 0.4f);
				nvgFillColor(vg, nvgLerpRGBA(panel::alpha(panel::CLAY, 0.08f), panel::CLAY, b));
				nvgFill(vg);
			};
			seg(7, t, 0, w - 2 * t, t);                  // top
			seg(1, t, (h - t) / 2, w - 2 * t, t);        // middle
			seg(4, t, h - t, w - 2 * t, t);              // bottom
			seg(2, 0, t * 0.6f, t, h / 2 - t * 0.9f);    // upper left
			seg(6, w - t, t * 0.6f, t, h / 2 - t * 0.9f); // upper right
			seg(3, 0, h / 2 + t * 0.3f, t, h / 2 - t * 0.9f);    // lower left
			seg(5, w - t, h / 2 + t * 0.3f, t, h / 2 - t * 0.9f); // lower right
			seg(0, w + t * 0.4f, h - t, t, t);           // point
		}
	}
};

} // namespace

struct NordicBankingWidget : ModuleWidget {
	NordicBankingWidget(NordicBanking* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/NordicBanking.svg")));
		panel::addScrews(this);
		panel::addLabels(this);

		SevenSegment* display = new SevenSegment;
		display->module = module;
		display->box = panel::mmRect((panel::W - panel::DISPLAY_W) / 2, panel::DISPLAY_Y, panel::DISPLAY_W, panel::DISPLAY_H);
		addChild(display);

		// In nb::KNOBS order.
		const Vec knobs[26] = { panel::LFO1_RATE_POS, panel::LFO1_AMT_POS, panel::LFO2_RATE_POS, panel::LFO2_AMT_POS,
			panel::MOD_A_POS, panel::MOD_D_POS, panel::MOD_AMT_POS, panel::SEMI_POS, panel::FINE_POS, panel::FM_POS,
			panel::PW_POS, panel::MIX_POS, panel::PORTA_POS, panel::CUTOFF_POS, panel::RESO_POS, panel::F_ENV_POS,
			panel::F_A_POS, panel::F_D_POS, panel::F_S_POS, panel::F_R_POS, panel::A_A_POS, panel::A_D_POS,
			panel::A_S_POS, panel::A_R_POS, panel::GAIN_POS, panel::VOLUME_POS };
		for (int i = 0; i < 26; i++)
			addParam(createParamCentered<RoundBlackKnob>(panel::mm(knobs[i].x, knobs[i].y), module, NordicBanking::KNOB_PARAM + i));

		// In nb::BUTTONS order.
		const Vec buttons[28] = { panel::B_OSC1_POS, panel::B_OSC2_POS, panel::B_KBD2_POS, panel::B_RINGSYNC_POS,
			panel::B_FTYPE_POS, panel::B_VELO_POS, panel::B_FKBD_POS, panel::B_DIST_POS, panel::B_LFO1WAVE_POS,
			panel::B_LFO1DEST_POS, panel::B_ARP_POS, panel::B_LFO2DEST_POS, panel::B_MODENV_POS, panel::B_SHIFT_POS,
			panel::B_PLAY_POS, panel::B_UNISON_POS, panel::B_AUTO_POS, panel::B_OCTDN_POS, panel::B_OCTUP_POS,
			panel::B_UP_POS, panel::B_DOWN_POS, panel::B_STORE_POS, panel::B_SLOTA_POS, panel::B_SLOTB_POS,
			panel::B_SLOTC_POS, panel::B_SLOTD_POS, panel::B_VELMORPH_POS, panel::B_PERF_POS };
		for (int i = 0; i < 28; i++)
			addParam(createParamCentered<VCVButton>(panel::mm(buttons[i].x, buttons[i].y), module, NordicBanking::BUTTON_PARAM + i));

		// In nb::LEDS order.
		const Vec leds[nb::NUM_LEDS] = { panel::OSC1_SINE_POS, panel::OSC1_TRI_POS, panel::OSC1_SAW_POS,
			panel::OSC1_PULSE_POS, panel::OSC2_TRI_POS, panel::OSC2_SAW_POS, panel::OSC2_PULSE_POS, panel::OSC2_NOISE_POS,
			panel::OSC2_KBD_POS, panel::RINGMOD_POS, panel::SYNC_POS, panel::HP24_POS, panel::LP24_POS, panel::LP12_POS,
			panel::VELOCITY_POS, panel::KBD23_POS, panel::KBD13_POS, panel::DISTORTION_POS, panel::LFO1_SOFTRND_POS,
			panel::LFO1_TRI_POS, panel::LFO1_RND_POS, panel::LFO1_FM_POS, panel::LFO1_OSC2_POS, panel::LFO1_PW_POS,
			panel::ARP_POS, panel::LFO2_TOP_POS, panel::LFO2_MID_POS, panel::LFO2_BOTTOM_POS, panel::MODENV_FM_POS,
			panel::MODENV_OSC2_POS, panel::WHEEL_MORPH_POS, panel::WHEEL_OSC2_POS, panel::WHEEL_FILTER_POS,
			panel::POLY_POS, panel::LEGATO_POS, panel::MONO_POS, panel::UNISON_POS, panel::AUTO_POS, panel::OCT_M2_POS,
			panel::OCT_M1_POS, panel::OCT_0_POS, panel::OCT_P1_POS, panel::OCT_P2_POS, panel::SLOT_A_POS,
			panel::SLOT_B_POS, panel::SLOT_C_POS, panel::SLOT_D_POS, panel::VELMORPH_POS, panel::KBDSPLIT_POS };
		for (int i = 0; i < nb::NUM_LEDS; i++)
			addChild(createLightCentered<SmallLight<panel::ClayLight> >(panel::mm(leds[i].x, leds[i].y), module, NordicBanking::LED_LIGHT + i));

		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::PEDAL_POS.x, panel::PEDAL_POS.y), module, NordicBanking::PEDAL_INPUT));
		const Vec outs[4] = { panel::OUT_A_POS, panel::OUT_B_POS, panel::OUT_C_POS, panel::OUT_D_POS };
		for (int i = 0; i < 4; i++)
			addOutput(createOutputCentered<panel::PortOutMain>(panel::mm(outs[i].x, outs[i].y), module, NordicBanking::OUT_A_OUTPUT + i));
	}

	void appendContextMenu(Menu* menu) override {
		NordicBanking* m = static_cast<NordicBanking*>(module);
		if (!m) return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Nord Lead 2X OS image (not included)"));
		menu->addChild(createMenuLabel("OS: " + (m->imagePath.empty() ? std::string("none") : system::getFilename(m->imagePath))));
		menu->addChild(createMenuItem("Load OS image (512 KB)...", "", [=]() {
			char* p = osdialog_file(OSDIALOG_OPEN, NULL, NULL, NULL);
			if (!p) return;
			m->imagePath = p;
			std::free(p);
			m->boot();
		}));
		menu->addChild(createMenuItem("Send SysEx file (programs, performances)...", "", [=]() {
			char* p = osdialog_file(OSDIALOG_OPEN, NULL, NULL, NULL);
			if (!p) return;
			const std::string path = p;
			std::free(p);
			if (!m->loadSysex(path)) m->setStatus("NO SYSEX IN THAT FILE");
		}));
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("MIDI in"));
		appendMidiMenu(menu, &m->midiInput);
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuItem("Power cycle", "", [=]() { m->boot(); }));
		menu->addChild(createMenuItem("Erase program memory (flash)", "", [=]() { m->eraseFlash(); }));
	}
};

Model* modelNordicBanking = createModel<NordicBanking, NordicBankingWidget>("NordicBanking");

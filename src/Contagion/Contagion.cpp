// Contagion -- an Access Virus C running its own firmware. See VirusC.hpp for the board.
//
// The OS image is Access's and is not distributed with the plugin: the context menu
// loads it from wherever the user keeps it, and the patch remembers the path, never the
// contents. The unit's battery RAM -- global settings, edit buffers, user banks A and B --
// is saved in the patch, as the battery would keep it.
#include "../plugin.hpp"
#include "Panel.hpp"
#include "PanelMap.hpp"
#include "VirusC.hpp"

#include <osdialog.h>

#include <atomic>
#include <cstring>
#include <fstream>
#include <iterator>
#include <mutex>
#include <thread>

using vc::KEY;
using vc::KEY_NAME;
using vc::LED;
using vc::BEZEL;

namespace {

std::vector<uint8_t> readFile(const std::string& path) {
	std::ifstream f(path, std::ios::binary);
	if (!f) return {};
	return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}

// The 32 pots in panel order, as (ADC group * 8 + channel) -- found by turning each one and
// reading what the firmware shows (VirusResearch/out/pots.txt).
const int POT_INDEX[32] = {
	0, 1,                       // LFO RATE, DELAY/REV TIME
	24, 3, 27, 11, 19, 9,       // SHAPE, WAVE SEL/PW, SEMITONE, DETUNE 2/3, FM AMOUNT, effects INTENSITY
	2, 26, 5, 21, 29,           // OSC BAL, SUB OSC, OSC VOL, NOISE, RING MOD
	17, 25, 8,                  // effects TYPE/MIX, delay FEEDBACK/DAMPING, delay SEND
	10, 18, 16,                 // SOFT KNOB 1, SOFT KNOB 2 (VALUE), MASTER VOLUME
	13, 20, 23, 31, 14,         // CUTOFF, CUTOFF 2, RESO, ENV AMT, FLT BAL
	12, 7, 15, 6,               // filter ATTACK, DECAY, SUSTAIN, RELEASE
	4, 28, 22, 30,              // amp ATTACK, DECAY, SUSTAIN, RELEASE
};

/** What the UI thread draws. */
struct Snapshot {
	uint8_t chars[32] = {};
	uint8_t cgram[64] = {};
};

} // namespace

struct Contagion : Module {
	enum ParamId { POT_PARAM, KEY_PARAM = POT_PARAM + 32, PARAMS_LEN = KEY_PARAM + 35 };
	enum InputId { IN_L_INPUT, IN_R_INPUT, INPUTS_LEN };
	enum OutputId { OUT1L_OUTPUT, OUT1R_OUTPUT, OUT2L_OUTPUT, OUT2R_OUTPUT, OUT3L_OUTPUT, OUT3R_OUTPUT, OUTPUTS_LEN };
	enum LightId { LED_LIGHT, RATE_LIGHT = LED_LIGHT + 67, LIGHTS_LEN = RATE_LIGHT + 2 };

	static constexpr float VOLTS = 5.f;   // the DSP's full scale, as a Rack audio level

	std::unique_ptr<vc::VirusC> unit;           // audio thread only
	std::atomic<vc::VirusC*> handover{nullptr};
	std::thread bootThread;
	std::atomic<bool> booting{false};

	std::string imagePath;                      // UI thread
	std::mutex snapMutex;
	std::string status = "LOAD A VIRUS C OS";   // guarded by snapMutex
	std::vector<uint8_t> globalRam, bankRam;   // guarded by snapMutex: what the patch keeps
	Snapshot snap;                              // guarded by snapMutex

	midi::InputQueue midiInput;
	bool keyDown[35] = {};
	uint8_t potSent[32];
	int housekeeping = 0, ramCountdown = 0;

	dsp::SampleRateConverter<2> inSrc;
	dsp::SampleRateConverter<6> outSrc;
	dsp::DoubleRingBuffer<dsp::Frame<2>, 256> inBuf;
	dsp::DoubleRingBuffer<dsp::Frame<6>, 256> outBuf;

	Contagion() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		static const char* pots[32] = { "LFO rate", "Delay/reverb time", "Osc shape", "Wave select / pulse width",
			"Semitone", "Detune 2/3", "FM amount", "Effect intensity", "Osc balance", "Sub osc", "Osc volume", "Noise",
			"Ring mod", "Effect type/mix", "Delay feedback / reverb damping", "Delay/reverb send", "Soft knob 1",
			"Soft knob 2 / value", "Master volume", "Cutoff", "Cutoff 2", "Resonance", "Envelope amount",
			"Filter balance", "Filter attack", "Filter decay", "Filter sustain", "Filter release", "Amp attack",
			"Amp decay", "Amp sustain", "Amp release" };
		for (int i = 0; i < 32; i++) configParam(POT_PARAM + i, 0.f, 1.f, 0.75f, pots[i], "%", 0.f, 100.f);
		for (int i = 0; i < 35; i++) configButton(KEY_PARAM + i, KEY_NAME[i]);
		configInput(IN_L_INPUT, "Left");
		configInput(IN_R_INPUT, "Right");
		static const char* outs[6] = { "Out 1 left", "Out 1 right", "Out 2 left", "Out 2 right", "Out 3 left", "Out 3 right" };
		for (int i = 0; i < 6; i++) configOutput(OUT1L_OUTPUT + i, outs[i]);
		std::memset(potSent, 0xff, sizeof(potSent));
	}

	~Contagion() {
		if (bootThread.joinable()) bootThread.join();
		delete handover.exchange(nullptr);
	}

	void setStatus(const std::string& s) {
		std::lock_guard<std::mutex> lock(snapMutex);
		status = s;
	}

	/** Power the unit on from the image and the saved battery RAM, on a worker thread:
	    the firmware takes a few seconds of machine time to boot the DSP. */
	void boot() {
		if (imagePath.empty()) { setStatus("LOAD A VIRUS C OS"); return; }
		if (bootThread.joinable()) bootThread.join();
		std::vector<uint8_t> g, b;
		{
			std::lock_guard<std::mutex> lock(snapMutex);
			g = globalRam;
			b = bankRam;
			status = "POWERING ON";
		}
		std::vector<uint8_t> pots(32);
		for (int i = 0; i < 32; i++) pots[POT_INDEX[i]] = potCode(i);
		booting = true;
		const std::string path = imagePath;
		bootThread = std::thread([this, path, g, b, pots]() {
			std::unique_ptr<vc::VirusC> v(new vc::VirusC);
			const std::string err = v->load(readFile(path));
			if (!err.empty()) { setStatus(err); booting = false; return; }
			v->setRam(g, b);
			for (int i = 0; i < 32; i++) v->setPot(i, pots[i]);   // the knobs where they stand
			if (!v->boot()) { setStatus("DSP DID NOT START"); booting = false; return; }
			INFO("Contagion: Virus OS booted from %s: [%s]", system::getFilename(path).c_str(), v->lcdText().c_str());
			delete handover.exchange(v.release());
			setStatus("");
			rememberImage();
			booting = false;
		});
	}

	uint8_t potCode(int i) { return uint8_t(clamp(params[POT_PARAM + i].getValue(), 0.f, 1.f) * 255.f + 0.5f); }

	static std::string settingsPath() { return asset::user("MoonTechnologies/settings.json"); }
	void rememberImage() {
		const std::string path = settingsPath();
		system::createDirectories(system::getDirectory(path));
		json_t* root = system::isFile(path) ? json_load_file(path.c_str(), 0, NULL) : NULL;
		if (!root) root = json_object();
		json_object_set_new(root, "contagionOs", json_string(imagePath.c_str()));
		json_dump_file(root, path.c_str(), JSON_INDENT(2));
		json_decref(root);
	}
	void onAdd(const AddEvent& e) override {
		if (!imagePath.empty() || booting || handover.load()) return;
		const std::string path = settingsPath();
		json_t* root = system::isFile(path) ? json_load_file(path.c_str(), 0, NULL) : NULL;
		if (!root) return;
		if (json_t* o = json_object_get(root, "contagionOs")) if (json_is_string(o)) imagePath = json_string_value(o);
		json_decref(root);
		if (!imagePath.empty()) boot();
	}

	/** Clear the battery RAM: the firmware reinitialises its global memory, and the user
	    banks return to what the image holds. */
	void factoryReset() {
		{
			std::lock_guard<std::mutex> lock(snapMutex);
			globalRam.clear();
			bankRam.clear();
		}
		boot();
	}

	// --- audio -----------------------------------------------------------------------------
	void process(const ProcessArgs& args) override {
		if (vc::VirusC* v = handover.exchange(nullptr)) {
			unit.reset(v);
			inBuf.clear();
			outBuf.clear();
			std::memset(potSent, 0xff, sizeof(potSent));
			for (bool& k : keyDown) k = false;
		}
		midi::Message msg;
		if (!unit) {
			while (midiInput.tryPop(&msg, args.frame)) {}
			for (int i = 0; i < OUTPUTS_LEN; i++) outputs[i].setVoltage(0.f);
			return;
		}
		vc::VirusC& v = *unit;

		// The front panel: pots into the A/D converter, buttons into the key matrix.
		for (int i = 0; i < 32; i++) {
			const uint8_t c = potCode(i);
			if (c != potSent[i]) { v.setPot(POT_INDEX[i], c); potSent[i] = c; }
		}
		for (int i = 0; i < 35; i++) {
			const bool down = params[KEY_PARAM + i].getValue() > 0.5f;
			if (down != keyDown[i]) { v.setButton(KEY[i][0], KEY[i][1], down); keyDown[i] = down; }
		}
		while (midiInput.tryPop(&msg, args.frame))
			for (int i = 0; i < msg.getSize(); i++) v.midi(msg.bytes[i]);

		dsp::Frame<2> fin;
		fin.samples[0] = inputs[IN_L_INPUT].getVoltage() / VOLTS;
		fin.samples[1] = (inputs[IN_R_INPUT].isConnected() ? inputs[IN_R_INPUT] : inputs[IN_L_INPUT]).getVoltage() / VOLTS;
		if (!inBuf.full()) inBuf.push(fin);

		// Run the machine at its own rate, a block at a time.
		if (outBuf.size() < 16) {
			inSrc.setRates(int(args.sampleRate), int(vc::VirusC::SAMPLE_RATE));
			outSrc.setRates(int(vc::VirusC::SAMPLE_RATE), int(args.sampleRate));
			dsp::Frame<2> mIn[64];
			int inLen = int(inBuf.size()), mLen = 64;
			inSrc.process(inBuf.startData(), &inLen, mIn, &mLen);
			inBuf.startIncr(inLen);
			if (mLen > 0) {
				float l[64], r[64], o[6][64];
				for (int n = 0; n < mLen; n++) { l[n] = mIn[n].samples[0]; r[n] = mIn[n].samples[1]; }
				float* const outs[6] = { o[0], o[1], o[2], o[3], o[4], o[5] };
				v.process(l, r, outs, mLen);
				dsp::Frame<6> mOut[64];
				for (int n = 0; n < mLen; n++) for (int c = 0; c < 6; c++) mOut[n].samples[c] = o[c][n];
				int frames = mLen, oLen = int(outBuf.capacity());
				outSrc.process(mOut, &frames, outBuf.endData(), &oLen);
				outBuf.endIncr(oLen);
			}
		}
		dsp::Frame<6> fo = {};
		if (!outBuf.empty()) fo = outBuf.shift();
		for (int c = 0; c < 6; c++) outputs[OUT1L_OUTPUT + c].setVoltage(fo.samples[c] * VOLTS);

		// Housekeeping, about 40 times a second: the LEDs, the LCD, and the battery RAM.
		if (++housekeeping >= int(args.sampleRate / 40)) {
			housekeeping = 0;
			float g[7][14];
			v.leds(g);
			for (int i = 0; i < 67; i++) lights[LED_LIGHT + i].setBrightness(g[LED[i][0]][LED[i][1]]);
			float rate[2];
			v.rateLeds(rate);
			for (int i = 0; i < 2; i++) lights[RATE_LIGHT + i].setBrightness(rate[i]);
			if (snapMutex.try_lock()) {
				v.lcd(snap.chars, snap.cgram);
				// 160 KB of battery RAM: copy it for the patch every few seconds.
				if (--ramCountdown <= 0) { v.copyRam(globalRam, bankRam); ramCountdown = 120; }
				snapMutex.unlock();
			}
		}
	}

	// --- the patch -------------------------------------------------------------------------
	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "osPath", json_string(imagePath.c_str()));
		std::lock_guard<std::mutex> lock(snapMutex);
		if (!globalRam.empty()) json_object_set_new(root, "globalRam", json_string(string::toBase64(globalRam).c_str()));
		if (!bankRam.empty()) json_object_set_new(root, "bankRam", json_string(string::toBase64(bankRam).c_str()));
		json_object_set_new(root, "midi", midiInput.toJson());
		return root;
	}

	void dataFromJson(json_t* root) override {
		if (json_t* j = json_object_get(root, "osPath")) imagePath = json_string_value(j);
		{
			std::lock_guard<std::mutex> lock(snapMutex);
			globalRam.clear();
			bankRam.clear();
			if (json_t* j = json_object_get(root, "globalRam")) globalRam = string::fromBase64(json_string_value(j));
			if (json_t* j = json_object_get(root, "bankRam")) bankRam = string::fromBase64(json_string_value(j));
		}
		if (json_t* j = json_object_get(root, "midi")) midiInput.fromJson(j);
		boot();
	}
};

// --- widgets ------------------------------------------------------------------------------------

namespace {

/** The Virus C's 2 x 16 LCD, dot by dot: its character ROM for the fixed glyphs and the
    controller's CGRAM for the eight the firmware defines. */
struct LcdDisplay : widget::Widget {
	Contagion* module = nullptr;

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) return;
		NVGcontext* vg = args.vg;
		Snapshot s;
		std::string status = "ACCESS VIRUS C";
		bool booting = false;
		if (module) {
			std::lock_guard<std::mutex> lock(module->snapMutex);
			s = module->snap;
			status = module->status;
			booting = module->booting;
		}
		if (!module || !status.empty()) {
			const panel::TextStyle st(panel::Face::Mono, box.size.y * 0.32f, panel::CLAY, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
			panel::text(vg, st, box.size.x / 2, box.size.y * 0.3f, module ? status : "ACCESS VIRUS C");
			panel::text(vg, st.inked(panel::SAGE), box.size.x / 2, box.size.y * 0.72f, !module ? "CONTAGION" : booting ? "PLEASE WAIT" : "RIGHT-CLICK: LOAD OS");
			return;
		}
		// 16 cells of 5 x 8 dots per line, a dot's gap between dots and a cell's between cells.
		const float cellW = box.size.x / 16.f, cellH = box.size.y / 2.f;
		const float dot = std::min(cellW / 6.2f, cellH / 9.2f);
		for (int i = 0; i < 32; i++) {
			const uint8_t code = s.chars[i];
			const uint8_t* rows = code < 16 ? &s.cgram[(code & 7) * 8] : vc::lcdGlyph(code);
			const float x0 = (i % 16) * cellW + (cellW - 5.6f * dot) / 2, y0 = (i / 16) * cellH + (cellH - 8.6f * dot) / 2;
			for (int y = 0; y < 8; y++)
				for (int x = 0; x < 5; x++) {
					const bool on = rows[y] >> (4 - x) & 1;
					nvgBeginPath(vg);
					nvgRect(vg, x0 + x * dot * 1.12f, y0 + y * dot * 1.08f, dot, dot);
					nvgFillColor(vg, on ? panel::LIME : panel::alpha(panel::SAGE, 0.08f));
					nvgFill(vg);
				}
		}
	}
};

} // namespace

struct ContagionWidget : ModuleWidget {
	ContagionWidget(Contagion* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Contagion.svg")));
		panel::addScrews(this);
		panel::addLabels(this);

		LcdDisplay* lcd = new LcdDisplay;
		lcd->module = module;
		lcd->box = panel::mmRect((panel::W - panel::LCD_W) / 2, panel::LCD_Y, panel::LCD_W, panel::LCD_H);
		addChild(lcd);

		const Vec pots[32] = { panel::RATE_POS, panel::DLY_TIME_POS, panel::SHAPE_POS, panel::WAVE_POS,
			panel::SEMITONE_POS, panel::DETUNE_POS, panel::FM_POS, panel::FX_INT_POS, panel::OSC_BAL_POS, panel::SUB_POS,
			panel::OSC_VOL_POS, panel::NOISE_POS, panel::RING_POS, panel::FX_MIX_POS, panel::DLY_FB_POS,
			panel::DLY_SEND_POS, panel::SOFT1_POS, panel::SOFT2_POS, panel::VOLUME_POS, panel::CUTOFF_POS,
			panel::CUTOFF2_POS, panel::RESO_POS, panel::ENV_AMT_POS, panel::FLT_BAL_POS, panel::F_ATT_POS,
			panel::F_DEC_POS, panel::F_SUS_POS, panel::F_REL_POS, panel::A_ATT_POS, panel::A_DEC_POS, panel::A_SUS_POS,
			panel::A_REL_POS };
		for (int i = 0; i < 32; i++)
			addParam(createParamCentered<RoundSmallBlackKnob>(panel::mm(pots[i].x, pots[i].y), module, Contagion::POT_PARAM + i));

		const Vec keys[35] = { panel::LFO_EDIT_POS, panel::LFO_SELECT_POS, panel::LFO_SHAPE_POS, panel::LFO_AMOUNT_POS,
			panel::OSC_EDIT_POS, panel::SYNC_POS, panel::OSC1_POS, panel::OSC2_POS, panel::OSC3_POS, panel::OSC3_ON_POS,
			panel::FX_EDIT_POS, panel::FX_SELECT_POS, panel::DLY_EDIT_POS, panel::ARP_ON_POS, panel::ARP_EDIT_POS,
			panel::EDIT_POS, panel::GLOBAL_POS, panel::RANDOM_POS, panel::UNDO_POS, panel::STORE_POS, panel::MULTI_POS,
			panel::SINGLE_POS, panel::PART_DN_POS, panel::PART_UP_POS, panel::PARAM_DN_POS, panel::PARAM_UP_POS,
			panel::VALUE_DN_POS, panel::VALUE_UP_POS, panel::FLT_EDIT_POS, panel::FLT1_MODE_POS, panel::FLT2_MODE_POS,
			panel::FLT_SEL1_POS, panel::FLT_SEL2_POS, panel::TR_DN_POS, panel::TR_UP_POS };
		for (int i = 0; i < 35; i++) {
			int light = -1;
			for (auto& b : BEZEL) if (b[0] == i) light = b[1];
			if (light >= 0)
				addParam(createLightParamCentered<VCVLightBezel<panel::LimeLight> >(panel::mm(keys[i].x, keys[i].y), module,
					Contagion::KEY_PARAM + i, Contagion::LED_LIGHT + light));
			else
				addParam(createParamCentered<VCVButton>(panel::mm(keys[i].x, keys[i].y), module, Contagion::KEY_PARAM + i));
		}
		// The LEDs in LED[] order; the bezels' entries are never drawn here.
		const Vec leds[67] = { panel::LFO_EDIT_LED_POS, panel::LFO1_POS, panel::LFO2_POS, panel::LFO3_POS, panel::LFO4_POS,
			panel::SHP1_POS, panel::SHP2_POS, panel::SHP3_POS, panel::SHP4_POS, panel::SHP5_POS,
			panel::OSC_EDIT_LED_POS, Vec(), panel::OSC1_LED_POS, panel::OSC2_LED_POS, panel::OSC3_LED_POS, Vec(),
			panel::FX_EDIT_LED_POS, panel::FX1_POS, panel::FX2_POS, panel::FX3_POS, panel::DLY_EDIT_LED_POS,
			panel::D1_1_POS, panel::D1_2_POS, panel::D1_3_POS, panel::D1_4_POS, panel::D1_5_POS, panel::D1_6_POS,
			panel::D2_1_POS, panel::D2_2_POS, panel::D2_3_POS, panel::D2_4_POS, panel::D2_5_POS, panel::D2_6_POS,
			panel::D3_1_POS, panel::D3_2_POS, panel::D3_3_POS, panel::D3_4_POS, panel::D3_5_POS,
			panel::DM_1_POS, panel::DM_2_POS, panel::DM_3_POS, panel::DM_4_POS, panel::DM_5_POS, panel::DM_6_POS,
			Vec(), panel::ARP_EDIT_LED_POS, panel::EDIT_LED_POS, panel::GLOBAL_LED_POS,
			panel::MULTI_LED_POS, panel::SINGLE_LED_POS, panel::FLT_EDIT_LED_POS,
			panel::F1M1_POS, panel::F1M2_POS, panel::F1M3_POS, panel::F1M4_POS,
			panel::F2M1_POS, panel::F2M2_POS, panel::F2M3_POS, panel::F2M4_POS, panel::SEL1_LED_POS, panel::SEL2_LED_POS,
			panel::TR1_POS, panel::TR2_POS, panel::TR3_POS, panel::TR4_POS, panel::TR5_POS, panel::BPM_POS };
		for (int i = 0; i < 67; i++) {
			bool bezel = false;
			for (auto& b : BEZEL) if (b[1] == i) bezel = true;
			if (!bezel)
				addChild(createLightCentered<SmallLight<panel::ClayLight> >(panel::mm(leds[i].x, leds[i].y), module, Contagion::LED_LIGHT + i));
		}
		addChild(createLightCentered<SmallLight<panel::ClayLight> >(panel::mm(panel::RATE1_POS.x, panel::RATE1_POS.y), module, Contagion::RATE_LIGHT));
		addChild(createLightCentered<SmallLight<panel::ClayLight> >(panel::mm(panel::RATE23_POS.x, panel::RATE23_POS.y), module, Contagion::RATE_LIGHT + 1));

		addInput(createInputCentered<panel::PortInMain>(panel::mm(panel::IN_L_POS.x, panel::IN_L_POS.y), module, Contagion::IN_L_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::IN_R_POS.x, panel::IN_R_POS.y), module, Contagion::IN_R_INPUT));
		const Vec outs[6] = { panel::OUT1L_POS, panel::OUT1R_POS, panel::OUT2L_POS, panel::OUT2R_POS, panel::OUT3L_POS, panel::OUT3R_POS };
		for (int i = 0; i < 6; i++)
			addOutput(createOutputCentered<panel::PortOutMain>(panel::mm(outs[i].x, outs[i].y), module, Contagion::OUT1L_OUTPUT + i));
	}

	void appendContextMenu(Menu* menu) override {
		Contagion* m = static_cast<Contagion*>(module);
		if (!m) return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Virus A/B/C OS image (not included)"));
		menu->addChild(createMenuLabel("OS: " + (m->imagePath.empty() ? std::string("none") : system::getFilename(m->imagePath))));
		menu->addChild(createMenuItem("Load OS image (512 KB flash dump)...", "", [=]() {
			char* p = osdialog_file(OSDIALOG_OPEN, NULL, NULL, NULL);
			if (!p) return;
			m->imagePath = p;
			std::free(p);
			m->boot();
		}));
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("MIDI in"));
		appendMidiMenu(menu, &m->midiInput);
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuItem("Power cycle", "", [=]() { m->boot(); }));
		menu->addChild(createMenuItem("Clear battery RAM (factory state)", "", [=]() { m->factoryReset(); }));
	}
};

Model* modelContagion = createModel<Contagion, ContagionWidget>("Contagion");

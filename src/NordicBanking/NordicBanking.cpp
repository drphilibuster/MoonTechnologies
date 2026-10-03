// Nordic Banking -- a Clavia Nord Lead 2X running its own firmware. See Nord2x.hpp for the board.
//
// The OS image is Clavia's and is not distributed with the plugin: the context menu loads it from
// wherever the user keeps it, and the patch remembers the path, never the contents. The unit's
// program memory -- the 64 KB flash -- is saved in the patch, as the flash would keep it. Clavia's
// factory programs are SysEx the user loads from the menu into the firmware, as on the unit.
#include "../plugin.hpp"
#include "Panel.hpp"
#include "PanelMap.hpp"
#include "KnobSync.hpp"
#include "../CvMidi.hpp"
#include "Nord2x.hpp"

#include <osdialog.h>

#include <algorithm>
#include <atomic>
#include <cmath>
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
	enum InputId { V_OCT_INPUT, GATE_INPUT, SUSTAIN_INPUT, INPUTS_LEN };
	enum OutputId { OUT_A_OUTPUT, OUT_B_OUTPUT, OUT_C_OUTPUT, OUT_D_OUTPUT, OUTPUTS_LEN };
	enum LightId { LED_LIGHT, LIGHTS_LEN = LED_LIGHT + nb::NUM_LEDS };

	// The DSP's full scale is 1, and the unit never gets near it: a held note at full MASTER VOL peaks around
	// 0.04, which is 0.2 V at 5 V full scale -- 28 dB under a Rack audio source. The make-up gain is a menu
	// choice (0 dB = the DSP's full scale at 5 V) and the default brings a typical note to a few volts.
	static constexpr float VOLTS = 5.f;
	// 12 dB: a held middle C across the 120 factory programs peaks at 0.09 of full scale (median) to 0.44 (loudest),
	// which at 5 V full scale is 0.4 to 2.2 V; +12 dB puts the loudest near 9 V and the median near 2 V.
	int outGainDb = 12, outGainCachedDb = -1;
	float outGain = 1.f;

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
	size_t sysexTotal = 0;                      // guarded by snapMutex: how many of this load there were, 0 = none loading
	std::atomic<bool> noPrograms{false};        // the flash holds no programs: blank, every switch on

	midi::InputQueue midiInput;
	bool buttonDown[28] = {};
	uint8_t knobSent[26];
	vc::CvMidi cvMidi;                          // audio thread; its settings are saved in the patch
	float minNoteMs = 0.f;                      // settings for cvMidi's minimum note length
	int knobHold[26] = {};                      // housekeeping ticks a knob the hand just moved is left alone
	int housekeeping = 0, flashCountdown = 0;

	dsp::SampleRateConverter<4> outSrc;
	dsp::DoubleRingBuffer<dsp::Frame<4>, 256> outBuf;

	NordicBanking() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		for (int i = 0; i < 26; i++)
			configParam(KNOB_PARAM + i, 0.f, 1.f, nb::KNOBS[i].initial / 255.f, nb::KNOBS[i].name, "%", 0.f, 100.f);
		for (int i = 0; i < 28; i++) configButton(BUTTON_PARAM + i, nb::BUTTONS[i].name);
		configInput(V_OCT_INPUT, "V/Oct (polyphonic)");
		configInput(GATE_INPUT, "Gate (polyphonic)");
		configInput(SUSTAIN_INPUT, "Sustain (gate: the pedal, held while high)");
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
			if (!n->boot(rom, f)) {
				// A stored flash the firmware cannot boot with: keep it in a file, power on from an erased one.
				if (f.empty()) { setStatus("THE OS DID NOT START"); booting = false; return; }
				const std::string bad = asset::user("MoonTechnologies/nordic-banking-flash-unbootable.bin");
				system::createDirectories(system::getDirectory(bad));
				std::ofstream(bad, std::ios::binary).write(reinterpret_cast<const char*>(f.data()), std::streamsize(f.size()));
				WARN("Nordic Banking: the stored program memory would not boot; saved to %s and started erased", bad.c_str());
				n.reset(new nb::Nord2x);
				if (!n->boot(rom, {})) { setStatus("THE OS DID NOT START"); booting = false; return; }
				{
					std::lock_guard<std::mutex> lock(snapMutex);
					flash.clear();
				}
			}
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

	/** Every F0 ... F7 message in a .syx file, appended to the queue for the firmware's MIDI input. */
	static std::vector<std::vector<uint8_t>> readSysex(const std::string& path) {
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
		return msgs;
	}
	bool loadSysex(const std::string& path) {
		std::vector<std::vector<uint8_t>> msgs = readSysex(path);
		if (msgs.empty()) return false;
		std::lock_guard<std::mutex> lock(snapMutex);
		for (auto& m : msgs) pendingSysex.push_back(std::move(m));
		sysexTotal = pendingSysex.size();
		return true;
	}
	/** Every .syx file in a folder, in name order (bank0 ... bank3, then Perf0): the whole factory library.
	    Returns how many files held SysEx. */
	int loadSysexFolder(const std::string& dir) {
		std::vector<std::string> files;
		for (const std::string& f : system::getEntries(dir)) {
			std::string ext = string::lowercase(system::getExtension(f));
			if (ext == ".syx" && system::isFile(f)) files.push_back(f);
		}
		std::sort(files.begin(), files.end(), [](const std::string& a, const std::string& b) {
			return string::lowercase(system::getFilename(a)) < string::lowercase(system::getFilename(b)); });
		int loaded = 0;
		for (const std::string& f : files) if (loadSysex(f)) loaded++;
		return loaded;
	}

	// --- audio -----------------------------------------------------------------------------
	void process(const ProcessArgs& args) override {
		if (nb::Nord2x* n = handover.exchange(nullptr)) {
			unit.reset(n);
			outBuf.clear();
			// The firmware is not told where the knobs are at power-on: a knob reaching it through the A/D converter
			// is a knob someone turned, and it would rewrite the program it just loaded. The knobs are moved to the
			// sound instead (KnobSync.hpp), and only what the hand turns is sent.
			for (int i = 0; i < 26; i++) { knobSent[i] = knobCode(i); knobHold[i] = 0; }
			cvMidi.reset();
			for (bool& b : buttonDown) b = false;
		}
		midi::Message msg;
		if (!unit) {
			while (midiInput.tryPop(&msg, args.frame)) {}
			for (int i = 0; i < OUTPUTS_LEN; i++) outputs[i].setVoltage(0.f);
			return;
		}
		nb::Nord2x& n = *unit;

		// The front panel: knobs into the ADC and buttons onto the key lines.
		for (int i = 0; i < 26; i++) {
			const uint8_t c = knobCode(i);
			if (c != knobSent[i]) { n.setKnob(nb::KNOBS[i].channel, c); knobSent[i] = c; knobHold[i] = 12; }
		}
		for (int i = 0; i < 28; i++) {
			const bool down = params[BUTTON_PARAM + i].getValue() > 0.5f;
			if (down != buttonDown[i]) { n.setButton(nb::BUTTONS[i].id, down); buttonDown[i] = down; }
		}
		// The cables become MIDI, the one thing the firmware understands: a rising gate is a note-on at the pitch read
		// then, a falling gate its note-off, and SUSTAIN is the pedal (controller 64).
		{
			vc::CvMidi::In in;
			Input& nv = inputs[V_OCT_INPUT];
			in.voices = nv.isConnected() && inputs[GATE_INPUT].isConnected()
				? std::min(int(vc::CvMidi::VOICES), std::max(1, nv.getChannels())) : 0;
			for (int c = 0; c < in.voices; c++) {
				in.pitch[c] = nv.getPolyVoltage(c);
				in.gate[c] = inputs[GATE_INPUT].getPolyVoltage(c);
			}
			in.susConnected = inputs[SUSTAIN_INPUT].isConnected();
			in.sus = inputs[SUSTAIN_INPUT].getVoltage();
			cvMidi.minSamples = int(minNoteMs * 0.001f * args.sampleRate);
			cvMidi.process(in, [&](int a, int b, int c, int len) {
				const uint8_t m[3] = { uint8_t(a), uint8_t(b), uint8_t(c) };
				n.midi(m, size_t(len));
			});
		}
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
		if (outGainDb != outGainCachedDb) { outGainCachedDb = outGainDb; outGain = std::pow(10.f, outGainDb / 20.f); }
		dsp::Frame<4> fo = {};
		if (!outBuf.empty()) fo = outBuf.shift();
		for (int c = 0; c < 4; c++) outputs[OUT_A_OUTPUT + c].setVoltage(fo.samples[c] * VOLTS * outGain);

		// Housekeeping, about 40 times a second: SysEx from the menu, the LEDs, the display and
		// the flash.
		if (++housekeeping >= int(args.sampleRate / 40)) {
			housekeeping = 0;
			float rows[6][8], digits[3][8];
			n.leds(rows, digits);
			// The knobs follow the sound: where a knob and the program being edited disagree it is moved to the
			// program's value on screen only, with knobSent set to match so the firmware is never told. A knob the
			// hand is on is left alone until it has settled.
			{
				// Which program the knobs edit: slot A in program mode (the other slots' LEDs dark), else the selected slot.
				int slot = 0;
				float others = 0.f;
				for (int i = 44; i <= 46; i++) others = std::max(others, rows[nb::LEDS[i].row][nb::LEDS[i].bit]);
				if (others > 0.1f) { uint8_t sl = 0; n.ram(nb::SELECTED_SLOT, &sl, 1); slot = sl & 3; }
				uint8_t prog[nb::EDIT_BUFFER_SIZE];
				n.ram(nb::EDIT_BUFFER + uint32_t(slot * nb::EDIT_BUFFER_SIZE), prog, sizeof prog);
				for (int i = 0; i < 26; i++) {
					if (knobHold[i] > 0) { knobHold[i]--; continue; }
					const int c = nb::KnobSync::resync(i, knobCode(i), prog);
					if (c < 0) continue;
					params[KNOB_PARAM + i].setValue(float(c) / 255.f);
					knobSent[i] = uint8_t(c);
				}
			}
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
				if (sysexTotal) {
					if (pendingSysex.empty()) { sysexTotal = 0; status.clear(); flashCountdown = 80; }   // done: look at the flash in two seconds, once the firmware has written it
					else status = "LOADING PROGRAMS " + std::to_string(100 * (sysexTotal - pendingSysex.size()) / sysexTotal) + "%";
				}
				// 64 KB of flash: copy it for the patch every few seconds.
				if (--flashCountdown <= 0) {
					n.copyFlash(flash);
					flashCountdown = 120;
					// A unit with no programs has an erased flash: all 0xFF but the few bytes the firmware writes itself.
					size_t used = 0;
					for (uint8_t b : flash) used += b != 0xFF;
					noPrograms = flash.size() == nb::Nord2x::FLASH_SIZE && used < 500;
				}
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
		json_object_set_new(root, "outLevelDb", json_integer(outGainDb));
		json_object_set_new(root, "cvMidiChannel", json_integer(cvMidi.channel));
		json_object_set_new(root, "cvPolyToChannels", json_boolean(cvMidi.polyToChannels));
		json_object_set_new(root, "cvMinNoteMs", json_real(minNoteMs));
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
		if (json_t* j = json_object_get(root, "outLevelDb")) outGainDb = clamp(int(json_integer_value(j)), 0, 48);
		if (json_t* j = json_object_get(root, "cvMidiChannel")) cvMidi.channel = clamp(int(json_integer_value(j)), 0, 15);
		if (json_t* j = json_object_get(root, "cvPolyToChannels")) cvMidi.polyToChannels = json_is_true(j);
		if (json_t* j = json_object_get(root, "cvMinNoteMs")) minNoteMs = clamp(float(json_number_value(j)), 0.f, 4000.f);
		boot();
	}
};

// --- widgets ------------------------------------------------------------------------------------

namespace {

/** The read-out well, all of it one piece of glass: the unit's three seven-segment digits, each
    segment as bright as the multiplex drives it, and the lamps the unit answers its selector
    buttons with, grouped by the button that steps them. LED indices are nb::LEDS order.
    Segment bits as the firmware writes them: 7 top, 1 middle, 4 bottom, 2 upper left, 6 upper
    right, 3 lower left, 5 lower right, 0 point. */
struct DisplayWidget : widget::Widget {
	NordicBanking* module = nullptr;

	struct Lamp { const char* text; int led; };
	struct Group { const char* label; int n; Lamp lamps[5]; };

	// Four columns of three, in the order the panel's buttons stand. The unit's selectors light
	// one LED, or a pair of neighbours for the setting between them (LFO 1's square wave is the top
	// two lit); the two lamps no multiplex position answers for (SIN, 2/3) stay dark, as on the unit.
	static const Group* groups() {
		static const Group g[12] = {
			{ "OSC 1", 4, { { "SIN", 0 }, { "TRI", 1 }, { "SAW", 2 }, { "PLS", 3 } } },
			{ "OSC 2", 4, { { "TRI", 4 }, { "SAW", 5 }, { "PLS", 6 }, { "NOISE", 7 } } },
			{ "RING/SYNC", 2, { { "RING", 9 }, { "SYNC", 10 } } },
			{ "LFO 1", 3, { { "S.RND", 18 }, { "TRI", 19 }, { "RND", 20 } } },
			{ "LFO 1 DEST", 3, { { "FM", 21 }, { "OSC 2", 22 }, { "PW", 23 } } },
			{ "LFO 2", 3, { { "ECHO", 25 }, { "UP", 26 }, { "DWN", 27 } } },
			{ "MOD ENV", 2, { { "FM", 28 }, { "OSC 2", 29 } } },
			{ "FILTER", 3, { { "HP 24", 11 }, { "LP 24", 12 }, { "LP 12", 13 } } },
			{ "KBD TRACK", 2, { { "2/3", 15 }, { "1/3", 16 } } },
			{ "PLAY", 3, { { "POLY", 33 }, { "LEGATO", 34 }, { "MONO", 35 } } },
			{ "WHEEL", 3, { { "MORPH", 30 }, { "OSC 2", 31 }, { "FILTER", 32 } } },
			{ "OCT", 5, { { "-2", 38 }, { "-1", 39 }, { "0", 40 }, { "+1", 41 }, { "+2", 42 } } },
		};
		return g;
	}

	void drawDigits(NVGcontext* vg, const Snapshot& s, float x, float y, float width, float height) {
		const float h = height, w = h * 0.52f, t = h * 0.11f, gap = width / 3.f;
		for (int d = 0; d < 3; d++) {
			const float x0 = x + gap * d + (gap - w) / 2 - t / 2, y0 = y;
			auto seg = [&](int bit, float sx, float sy, float sw, float sh) {
				const float b = s.digits[d][bit];
				nvgBeginPath(vg);
				nvgRoundedRect(vg, x0 + sx, y0 + sy, sw, sh, t * 0.4f);
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

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) return;
		NVGcontext* vg = args.vg;
		Snapshot snap;
		std::string status = "NORD LEAD 2X";
		bool booting = false;
		if (module) {
			std::lock_guard<std::mutex> lock(module->snapMutex);
			snap = module->snap;
			status = module->status;
			booting = module->booting;
		}
		const float s = box.size.x / panel::GLASS_W;                 // pixels per mm
		// Millimetres across a glass GLASS_W wide: the digits at the left, then four columns of lamps.
		const float digitsW = 22.f, digitsH = 9.f, x0 = 4.f, colGap = 3.f, pitch = 3.2f, y0 = 3.9f;
		drawDigits(vg, snap, x0 * s, (panel::GLASS_H - digitsH) / 2 * s, digitsW * s, digitsH * s);

		const float lx = x0 + digitsW + 4.f;
		const float colW = (panel::GLASS_W - lx - 3.f - 3 * colGap) / 4.f;
		if (module && (!status.empty() || module->noPrograms)) {
			const panel::TextStyle word(panel::Face::Mono, 7.5f, panel::CLAY, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
			const float cx = (lx + (panel::GLASS_W - 3.f - lx) / 2) * s;
			// A status (booting, loading, no OS) first; a unit with an erased flash has no programs to play.
			const bool ours = status.empty();
			panel::text(vg, word, cx, panel::GLASS_H * 0.36f * s, ours ? "NO PROGRAMS LOADED" : status);
			panel::text(vg, word.inked(panel::SAGE), cx, panel::GLASS_H * 0.68f * s,
				ours ? "RIGHT-CLICK: LOAD PROGRAM BANKS" : (booting || status.rfind("LOADING", 0) == 0) ? "PLEASE WAIT" : "RIGHT-CLICK: LOAD OS");
			return;
		}
		const panel::TextStyle st(panel::Face::Mono, 7.f, panel::SAGE, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
		const Group* g = groups();
		for (int c = 0; c < 4; c++) {
			// The lamps follow the widest label of their column, so the words line up under one another.
			float labelW = 0.f;
			for (int r = 0; r < 3; r++) labelW = std::max(labelW, panel::textWidth(vg, st, g[c * 3 + r].label) / s);
			labelW += 1.8f;
			const float gx = lx + c * (colW + colGap);
			for (int r = 0; r < 3; r++) {
				const Group& grp = g[c * 3 + r];
				const float gy = y0 + r * pitch;   // columns run down: three groups, then the next column
				panel::text(vg, st.inked(panel::PAPER), gx * s, gy * s, grp.label);
				float x = gx + labelW;
				for (int k = 0; k < grp.n; k++) {
					const float b = module ? module->lights[NordicBanking::LED_LIGHT + grp.lamps[k].led].getBrightness() : 0.f;
					const NVGcolor ink = b > 0.05f ? panel::alpha(panel::LIME, 0.35f + 0.65f * b) : panel::alpha(panel::SAGE, 0.55f);
					x = panel::text(vg, st.inked(ink), x * s, gy * s, grp.lamps[k].text) / s + 1.8f;
				}
			}
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

		DisplayWidget* display = new DisplayWidget;
		display->module = module;
		display->box = panel::mmRect(panel::GLASS_X, panel::GLASS_Y, panel::GLASS_W, panel::GLASS_H);
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

		// The lamps that stay lamps: one beside each button that has a light of its own. The rest of
		// the unit's LEDs are drawn on the display.
		struct Lamp { int led; Vec pos; };
		const Lamp lamps[12] = { { 8, panel::OSC2_KBD_POS }, { 14, panel::VELOCITY_POS }, { 17, panel::DISTORTION_POS },
			{ 24, panel::ARP_POS }, { 36, panel::UNISON_POS }, { 37, panel::AUTO_POS }, { 43, panel::SLOT_A_POS },
			{ 44, panel::SLOT_B_POS }, { 45, panel::SLOT_C_POS }, { 46, panel::SLOT_D_POS }, { 47, panel::VELMORPH_POS },
			{ 48, panel::KBDSPLIT_POS } };
		for (const Lamp& l : lamps)
			addChild(createLightCentered<SmallLight<panel::ClayLight> >(panel::mm(l.pos.x, l.pos.y), module, NordicBanking::LED_LIGHT + l.led));

		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::VOCT_POS.x, panel::VOCT_POS.y), module, NordicBanking::V_OCT_INPUT));
		addInput(createInputCentered<panel::PortTrigIn>(panel::mm(panel::GATE_POS.x, panel::GATE_POS.y), module, NordicBanking::GATE_INPUT));
		addInput(createInputCentered<panel::PortTrigIn>(panel::mm(panel::SUSTAIN_POS.x, panel::SUSTAIN_POS.y), module, NordicBanking::SUSTAIN_INPUT));
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
		menu->addChild(createMenuItem("Load program banks from folder...", "", [=]() {
			char* p = osdialog_file(OSDIALOG_OPEN_DIR, NULL, NULL, NULL);
			if (!p) return;
			const std::string dir = p;
			std::free(p);
			if (m->loadSysexFolder(dir) == 0) m->setStatus("NO SYSEX IN THAT FOLDER");
		}));
		menu->addChild(createMenuItem("Send SysEx file (programs, performances)...", "", [=]() {
			char* p = osdialog_file(OSDIALOG_OPEN, NULL, NULL, NULL);
			if (!p) return;
			const std::string path = p;
			std::free(p);
			if (!m->loadSysex(path)) m->setStatus("NO SYSEX IN THAT FILE");
		}));
		menu->addChild(new MenuSeparator);
		{
			static const int dbs[7] = { 0, 6, 12, 18, 24, 30, 36 };
			std::vector<std::string> names;
			for (int d : dbs) names.push_back("+" + std::to_string(d) + " dB" + (d == 12 ? " (default)" : ""));
			menu->addChild(createIndexSubmenuItem("Output level", names,
				[=]() { int best = 0; for (int i = 0; i < 7; i++) if (std::abs(dbs[i] - m->outGainDb) < std::abs(dbs[best] - m->outGainDb)) best = i; return best; },
				[=](int i) { m->outGainDb = dbs[i]; }));
		}
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("V/OCT, GATE and SUSTAIN jacks"));
		menu->addChild(createSubmenuItem("MIDI channel", std::to_string(m->cvMidi.channel + 1), [=](Menu* sub) {
			for (int c = 0; c < 16; c++)
				sub->addChild(createCheckMenuItem(std::to_string(c + 1), "", [=]() { return m->cvMidi.channel == c; },
					[=]() { m->cvMidi.channel = c; }));
		}));
		menu->addChild(createCheckMenuItem("Polyphonic cable channel n plays MIDI channel n", "", [=]() { return m->cvMidi.polyToChannels; },
			[=]() { m->cvMidi.polyToChannels = !m->cvMidi.polyToChannels; }));
		static const float MIN_MS[8] = { 0.f, 25.f, 50.f, 100.f, 250.f, 500.f, 1000.f, 2000.f };
		int minIndex = 0;
		for (int i = 0; i < 8; i++) if (MIN_MS[i] <= m->minNoteMs + 0.5f) minIndex = i;
		menu->addChild(createIndexSubmenuItem("Minimum note length (triggers)",
			{ "Off: the gate's own length", "25 ms", "50 ms", "100 ms", "250 ms", "500 ms", "1 s", "2 s" },
			[=]() { return minIndex; }, [=](int i) { m->minNoteMs = MIN_MS[i]; }));
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("MIDI in"));
		appendMidiMenu(menu, &m->midiInput);
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuItem("Power cycle", "", [=]() { m->boot(); }));
		menu->addChild(createMenuItem("Erase program memory (flash)", "", [=]() { m->eraseFlash(); }));
	}
};

Model* modelNordicBanking = createModel<NordicBanking, NordicBankingWidget>("NordicBanking");

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
#include "Controls.hpp"
#include "../CvMidi.hpp"
#include "KnobSync.hpp"
#include "Presets.hpp"

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
/** The panel-order names of the 32 pots: the CV inputs' target menu and the knobs' tooltips. */
const char* const POT_NAME[32] = { "LFO rate", "Delay/reverb time", "Osc shape", "Wave select / pulse width",
	"Semitone", "Detune 2/3", "FM amount", "Effect intensity", "Osc balance", "Sub osc", "Osc volume", "Noise",
	"Ring mod", "Effect type/mix", "Delay feedback / reverb damping", "Delay/reverb send", "Soft knob 1",
	"Soft knob 2 / value", "Master volume", "Cutoff", "Cutoff 2", "Resonance", "Envelope amount",
	"Filter balance", "Filter attack", "Filter decay", "Filter sustain", "Filter release", "Amp attack",
	"Amp decay", "Amp sustain", "Amp release" };

/** The six selectors (the SELECTED lines on the display): the key that steps the unit through them (or
    the key of each position), the LEDs that show where it is, and the section's EDIT key, which the
    line's name presses. Order = SEL_PARAM. */
const int OSC_KEYS[3] = { 6, 7, 8 };
struct SelDef {
	const char* name;
	int stepKey;
	const int* direct;
	int led0, n;
	bool lastWhenDark;       // the unit shows the last position with no LED lit
	int pushKey;
	std::vector<std::string> labels;
};
const SelDef SELECTORS[6] = {
	{ "LFO select", 1, nullptr, 1, 4, false, 0, { "LFO 1", "LFO 2", "LFO 3", "Mod" } },
	{ "LFO shape", 2, nullptr, 5, 5, true, -1, { "Sine", "Triangle", "Sawtooth", "Square", "Wave" } },
	{ "Oscillator", -1, OSC_KEYS, 12, 3, false, 4, { "Oscillator 1", "Oscillator 2", "Oscillator 3" } },
	{ "Effect", 11, nullptr, 17, 3, false, 10, { "Distortion", "Phaser", "Chorus" } },
	{ "Filter 1 mode", 29, nullptr, 51, 4, false, -1, { "Lowpass", "Highpass", "Bandpass", "Bandstop" } },
	{ "Filter 2 mode", 30, nullptr, 55, 4, false, -1, { "Lowpass", "Highpass", "Bandpass", "Bandstop" } },
};

/** The step buttons: the key DOWN presses (a detent counterclockwise), and clockwise (the fifth, PRESET, presses no
    key: it sends MIDI). Order = ENC_PARAM. */
const char* const ENC_NAME[5] = { "Part", "Parameter", "Value / program", "Transpose", "Preset" };
const int ENC_KEYS[4][2] = { { 22, 23 }, { 24, 25 }, { 26, 27 }, { 33, 34 } };

/** A control that has no end: its tooltip says what the buttons do rather than a number of detents. */
struct EncoderQuantity : ParamQuantity {
	std::string getDisplayValueString() override { return "down / up: - / +"; }
	std::string getUnit() override { return ""; }
};

/** The buttons a gate input presses, as indices into KEY[], in the order of their jacks. */
const int GATE_KEY[8] = { 22, 23, 24, 25, 26, 27, 13, 17 };   // PART -/+, PARAM </>, VALUE -/+, ARP ON, RANDOM

/** Where the eight CV inputs start out: the knobs most worth moving from a patch. */
const int CV_DEFAULT[8] = { 19, 21, 3, 6, 16, 17, 13, 15 };    // cutoff, resonance, PW, FM, soft 1, soft 2, fx mix, send

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
	uint8_t chars[32] = {};     // the LCD as it is now: the parameter screen
	uint8_t cgram[64] = {};
	uint8_t preset[32] = {};    // the last time it showed a program: the preset screen
	bool havePreset = false;
};

} // namespace

struct Contagion : Module {
	enum ParamId { POT_PARAM, KEY_PARAM = POT_PARAM + 32, SEL_PARAM = KEY_PARAM + 35, ENC_PARAM = SEL_PARAM + 6,
		PRESET_PARAM = ENC_PARAM + 4, TEMPO_PARAM, MULTISINGLE_PARAM, PAGE_PARAM, CATEGORY_PARAM, INCAT_PARAM, PARAMS_LEN };
	enum InputId { IN_L_INPUT, IN_R_INPUT,
		NOTE_V_INPUT, NOTE_GATE_INPUT, NOTE_VEL_INPUT, BEND_INPUT, MOD_INPUT, TOUCH_INPUT, SUSTAIN_INPUT,
		CLK_INPUT, RUN_INPUT, RST_INPUT,
		CV_INPUT, G_INPUT = CV_INPUT + 8, INPUTS_LEN = G_INPUT + 8 };
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
	float minNoteMs = 0.f;                      // settings for cvMidi's minimum note length
	vc::CvMidi cvMidi;                          // audio thread; its settings are saved in the patch
	int cvTarget[8];                            // which pot each CV input moves, -1 for none
	float cvAmount[8];                          // its attenuverter, -1..1: 10 V is the pot's full travel
	float cvOff[32] = {};                       // what the CV adds to each pot, audio thread
	vc::KeyPresser keys;                        // presses of the unit's buttons, from any thread
	vc::Selector sels[6];                       // audio thread
	vc::Encoder encs[4], presetEnc, pageEnc, catEnc, inCatEnc;
	vc::Chord chord;                            // the gestures that hold one key while another is pressed
	bool msWas = false;                         // the MULTI+SINGLE button's last state
	vc::Presets presets;                        // guarded by snapMutex: the image's names
	std::atomic<int> presetRequest{-1};         // a sound to select, from the menu (UI thread)
	std::atomic<int> shownPreset{-1};           // the sound the LCD names, -1 if it names none
	int presetIndex = 0, presetPending = 0;     // audio thread: where the PRESET knob is, and turns not yet sent
	int tempoSent = 38, tempoHold = 0;          // audio thread: the knob's last value the unit has, and a rest for the sync
	int potHold[32] = {};                       // ticks a knob the hand is on is left alone by the sync
	int presetQuiet = 0;                        // ticks the LCD needs to show a sound just chosen
	dsp::SchmittTrigger gateTrig[8];
	int editHold = 0, presetStill = 0;          // audio thread: when to take the LCD for a program screen
	bool keyDown[35] = {};
	uint8_t potSent[32];
	int housekeeping = 0, ramCountdown = 0;

	dsp::SampleRateConverter<2> inSrc;
	dsp::SampleRateConverter<6> outSrc;
	dsp::DoubleRingBuffer<dsp::Frame<2>, 256> inBuf;
	dsp::DoubleRingBuffer<dsp::Frame<6>, 256> outBuf;

	Contagion() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		for (int i = 0; i < 32; i++) configParam(POT_PARAM + i, 0.f, 1.f, 0.75f, POT_NAME[i], "%", 0.f, 100.f);
		for (int i = 0; i < 8; i++) { cvTarget[i] = CV_DEFAULT[i]; cvAmount[i] = 1.f; }
		for (int i = 0; i < 6; i++) {
			configSwitch(SEL_PARAM + i, 0.f, float(SELECTORS[i].n - 1), 0.f, SELECTORS[i].name, SELECTORS[i].labels);
			paramQuantities[SEL_PARAM + i]->randomizeEnabled = false;
			resetSelector(i);
		}
		// Gestures of the unit that need two keys at once or one held while another is pressed: a mouse cannot, so each is
		// a control that does it (Chord in Controls.hpp).
		configButton(MULTISINGLE_PARAM, "Multi+Single: MULTI and SINGLE together enter Multi-Single mode; pressed again, SINGLE alone leaves it");
		configParam<EncoderQuantity>(PAGE_PARAM, -INFINITY, INFINITY, 0.f, "Parameter page (hold one PARAMETER button, press the other)");
		configParam<EncoderQuantity>(CATEGORY_PARAM, -INFINITY, INFINITY, 0.f, "Category (hold SINGLE, step with PARAMETER)");
		configParam<EncoderQuantity>(INCAT_PARAM, -INFINITY, INFINITY, 0.f, "Sound in the category (hold SINGLE, step with VALUE)");
		for (int p : { PAGE_PARAM, CATEGORY_PARAM, INCAT_PARAM }) paramQuantities[p]->randomizeEnabled = false;
		configParam(TEMPO_PARAM, 0.f, 127.f, 38.f, "Tempo", " BPM", 0.f, 1.f, float(vc::KnobSync::TEMPO_BPM_AT_ZERO));
		paramQuantities[TEMPO_PARAM]->snapEnabled = true;
		paramQuantities[TEMPO_PARAM]->randomizeEnabled = false;
		for (int i = 0; i < 5; i++) {
			configParam<EncoderQuantity>(ENC_PARAM + i, -INFINITY, INFINITY, 0.f, ENC_NAME[i]);
			paramQuantities[ENC_PARAM + i]->randomizeEnabled = false;
		}
		for (int i = 0; i < 35; i++) configButton(KEY_PARAM + i, KEY_NAME[i]);
		configInput(IN_L_INPUT, "Left");
		configInput(IN_R_INPUT, "Right");
		configInput(NOTE_V_INPUT, "V/Oct (polyphonic)");
		configInput(NOTE_GATE_INPUT, "Gate (polyphonic)");
		configInput(NOTE_VEL_INPUT, "Velocity, 0-10 V (polyphonic)");
		configInput(BEND_INPUT, "Pitch bend, +-5 V");
		configInput(MOD_INPUT, "Mod wheel, 0-10 V");
		configInput(TOUCH_INPUT, "Aftertouch, 0-10 V");
		configInput(SUSTAIN_INPUT, "Sustain pedal (gate)");
		configInput(CLK_INPUT, "Clock");
		configInput(RUN_INPUT, "Run (gate)");
		configInput(RST_INPUT, "Reset");
		for (int i = 0; i < 8; i++) configInput(CV_INPUT + i, string::f("CV %d", i + 1));
		static const char* gates[8] = { "Part -", "Part +", "Parameter <", "Parameter >", "Value -", "Value +", "Arpeggiator on", "Random" };
		for (int i = 0; i < 8; i++) configInput(G_INPUT + i, std::string(gates[i]) + " (gate)");
		static const char* outs[6] = { "Out 1 left", "Out 1 right", "Out 2 left", "Out 2 right", "Out 3 left", "Out 3 right" };
		for (int i = 0; i < 6; i++) configOutput(OUT1L_OUTPUT + i, outs[i]);
		std::memset(potSent, 0xff, sizeof(potSent));
	}

	~Contagion() {
		if (bootThread.joinable()) bootThread.join();
		delete handover.exchange(nullptr);
	}

	/** Make the part play sound `index` (bank * 128 + program): bank select, then program change. */
	void selectPreset(vc::VirusC& v, int index) {
		index = clamp(index, 0, vc::Presets::COUNT - 1);
		for (uint8_t b : vc::Presets::select(cvMidi.channel, index / vc::Presets::PER_BANK, index % vc::Presets::PER_BANK)) v.midi(b);
		presetIndex = index;
		presetQuiet = 20;
	}

	/** A sound's name, from the battery RAM for the user banks and the image otherwise. UI thread. */
	std::string presetName(int bank, int prog) {
		std::lock_guard<std::mutex> lock(snapMutex);
		return presets.name(bank, prog, bankRam);
	}
	bool haveNames() {
		std::lock_guard<std::mutex> lock(snapMutex);
		return presets.loaded();
	}

	void resetSelector(int i) {
		sels[i] = vc::Selector();
		sels[i].steps = SELECTORS[i].n;
		sels[i].stepKey = SELECTORS[i].stepKey;
		sels[i].directKey = SELECTORS[i].direct;
		sels[i].lastWhenDark = SELECTORS[i].lastWhenDark;
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
			const std::vector<uint8_t> image = readFile(path);
			const std::string err = v->load(image);
			if (!err.empty()) { setStatus(err); booting = false; return; }
			vc::Presets names;
			names.loadImage(image);
			v->setRam(g, b);
			for (int i = 0; i < 32; i++) v->setPot(i, pots[i]);   // the knobs where they stand
			if (!v->boot()) { setStatus("DSP DID NOT START"); booting = false; return; }
			INFO("Contagion: Virus OS booted from %s: [%s]", system::getFilename(path).c_str(), v->lcdText().c_str());
			{
				std::lock_guard<std::mutex> lock(snapMutex);
				presets = names;
			}
			delete handover.exchange(v.release());
			setStatus("");
			rememberImage();
			booting = false;
		});
	}

	uint8_t potCode(int i) { return uint8_t(clamp(params[POT_PARAM + i].getValue() + cvOff[i], 0.f, 1.f) * 255.f + 0.5f); }

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
			cvMidi.reset();
			keys.clear();
			for (int i = 0; i < 6; i++) resetSelector(i);    // the knobs take the new unit's word for it
			for (int i = 0; i < 4; i++) encs[i] = vc::Encoder();
			presetEnc = vc::Encoder();
			pageEnc = catEnc = inCatEnc = vc::Encoder();
			chord.clear();
			tempoSent = int(std::floor(params[TEMPO_PARAM].getValue() + 0.5f));
			tempoHold = 0;
			presetPending = 0;
		}
		midi::Message msg;
		if (!unit) {
			while (midiInput.tryPop(&msg, args.frame)) {}
			for (int i = 0; i < OUTPUTS_LEN; i++) outputs[i].setVoltage(0.f);
			return;
		}
		vc::VirusC& v = *unit;

		// Cables: CV moves the knobs, gates press buttons, and the note and controller jacks are
		// turned into the MIDI a keyboard would send.
		for (int i = 0; i < 32; i++) cvOff[i] = 0.f;
		for (int i = 0; i < 8; i++)
			if (cvTarget[i] >= 0 && inputs[CV_INPUT + i].isConnected())
				cvOff[cvTarget[i]] += inputs[CV_INPUT + i].getVoltageSum() / 10.f * cvAmount[i];
		// A gate is one press of its button, paced for the key scan like every other press.
		for (int i = 0; i < 8; i++)
			if (gateTrig[i].process(inputs[G_INPUT + i].getVoltage(), 0.1f, 2.f)) keys.press(GATE_KEY[i]);
		// An endless knob is a press of its - or + key per detent.
		for (int i = 0; i < 4; i++) {
			const int d = encs[i].delta(params[ENC_PARAM + i].getValue());
			if (d > 0) keys.press(ENC_KEYS[i][1], d);
			else if (d < 0) keys.press(ENC_KEYS[i][0], -d);
		}
		{   // The chorded gestures. Each click holds one key and presses another (KEY indices in PanelMap.hpp).
			const int SINGLE = 21, PARAM_DN = 24, PARAM_UP = 25, VALUE_DN = 26, VALUE_UP = 27;
			const int page = pageEnc.delta(params[PAGE_PARAM].getValue());
			for (int k = 0; k < std::abs(page); k++)      // up scrolls forward: the held button is the direction
				page > 0 ? chord.request(PARAM_UP, PARAM_DN, 0.4f) : chord.request(PARAM_DN, PARAM_UP, 0.4f);
			const int cat = catEnc.delta(params[CATEGORY_PARAM].getValue());
			for (int k = 0; k < std::abs(cat); k++) chord.request(SINGLE, cat > 0 ? PARAM_UP : PARAM_DN, 1.5f);
			const int inCat = inCatEnc.delta(params[INCAT_PARAM].getValue());
			for (int k = 0; k < std::abs(inCat); k++) chord.request(SINGLE, inCat > 0 ? VALUE_UP : VALUE_DN, 1.5f);
		}
		{
			vc::CvMidi::In in;
			Input& nv = inputs[NOTE_V_INPUT];
			in.voices = nv.isConnected() && inputs[NOTE_GATE_INPUT].isConnected()
				? std::min(int(vc::CvMidi::VOICES), std::max(1, nv.getChannels())) : 0;
			for (int c = 0; c < in.voices; c++) {
				in.pitch[c] = nv.getPolyVoltage(c);
				in.gate[c] = inputs[NOTE_GATE_INPUT].getPolyVoltage(c);
				in.vel[c] = inputs[NOTE_VEL_INPUT].getPolyVoltage(c);
			}
			in.velConnected = inputs[NOTE_VEL_INPUT].isConnected();
			in.bendConnected = inputs[BEND_INPUT].isConnected();
			in.bend = inputs[BEND_INPUT].getVoltage();
			in.modConnected = inputs[MOD_INPUT].isConnected();
			in.mod = inputs[MOD_INPUT].getVoltage();
			in.atConnected = inputs[TOUCH_INPUT].isConnected();
			in.at = inputs[TOUCH_INPUT].getVoltage();
			in.susConnected = inputs[SUSTAIN_INPUT].isConnected();
			in.sus = inputs[SUSTAIN_INPUT].getVoltage();
			in.clkConnected = inputs[CLK_INPUT].isConnected();
			in.clk = inputs[CLK_INPUT].getVoltage();
			in.runConnected = inputs[RUN_INPUT].isConnected();
			in.run = inputs[RUN_INPUT].getVoltage();
			in.rstConnected = inputs[RST_INPUT].isConnected();
			in.rst = inputs[RST_INPUT].getVoltage();
			cvMidi.minSamples = int(minNoteMs * 0.001f * args.sampleRate);
			cvMidi.process(in, [&](int a, int b, int c, int n) {
				v.midi(uint8_t(a));
				if (n > 1) v.midi(uint8_t(b));
				if (n > 2) v.midi(uint8_t(c));
			});
		}

		// PRESET steps through the 1024 sounds a detent at a time; what it chose is sent a tick later, once.
		presetPending += presetEnc.delta(params[PRESET_PARAM].getValue());
		const int asked = presetRequest.exchange(-1);
		if (asked >= 0) { selectPreset(v, asked); presetPending = 0; }

		// The front panel: pots into the A/D converter, buttons into the key matrix.
		for (int i = 0; i < 32; i++) {
			const uint8_t c = potCode(i);
			if (c != potSent[i]) { v.setPot(POT_INDEX[i], c); potSent[i] = c; potHold[i] = 24; }
		}
		for (int i = 0; i < 35; i++) {
			const bool pressed = keys.process(i, args.sampleRate);
			const bool down = params[KEY_PARAM + i].getValue() > 0.5f || pressed || i == chord.holder();
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
			// The selectors: where the unit's LEDs say each one is, and presses to put it where
			// it was set on the screen.
			const float tick = float(int(args.sampleRate / 40)) / args.sampleRate;
			auto lit = [&](int i) { return g[LED[i][0]][LED[i][1]]; };
			for (int i = 0; i < 6; i++) {
				int ids[5];
				for (int k = 0; k < SELECTORS[i].n; k++) ids[k] = SELECTORS[i].led0 + k;
				const int obs = vc::litPosition(lit, ids, SELECTORS[i].n);
				const int to = sels[i].tick(int(std::floor(params[SEL_PARAM + i].getValue() + 0.5f)), obs, keys, tick);
				if (to >= 0) params[SEL_PARAM + i].setValue(float(to));
			}
			chord.tick(keys, tick);
			{   // MULTI+SINGLE: both keys at once enter Multi-Single (both LEDs lit); SINGLE alone leaves it
				const bool ms = params[MULTISINGLE_PARAM].getValue() > 0.5f;
				if (ms && !msWas) {
					if (lit(48) > 0.5f && lit(49) > 0.5f) keys.press(21);
					else { keys.press(20); keys.press(21); }
				}
				msWas = ms;
			}
			// The knobs follow the sound. Where a knob and the loaded sound disagree it is moved to the
			// sound's value on screen only (KnobSync.hpp): potSent is set to match, so the firmware is
			// never told and nothing is marked edited. A knob the hand is on, or a CV is moving, is left.
			for (int i = 0; i < 32; i++) if (potHold[i] > 0) potHold[i]--;
			if (tempoHold > 0) tempoHold--;
			{   // the BPM knob: a turn is a parameter change to the unit, sent once it settles
				const int t = int(std::floor(params[TEMPO_PARAM].getValue() + 0.5f));
				if (t != tempoSent) {
					uint8_t m[11];
					vc::KnobSync::tempoMessage(t, m);
					for (uint8_t b : m) v.midi(b);
					tempoSent = t;
					tempoHold = 24;
				}
			}
			if (lit(49) > 0.5f) {                                // single mode: the edit buffer is the sound
				uint8_t buf[vc::KnobSync::EDIT_BUFFER];
				v.xram(0, buf, sizeof(buf));
				const vc::KnobSync::Context cx = vc::KnobSync::context(lit);
				bool cvd[32] = {};
				for (int k = 0; k < 8; k++) if (cvTarget[k] >= 0 && inputs[CV_INPUT + k].isConnected()) cvd[cvTarget[k]] = true;
				if (tempoHold == 0 && buf[vc::KnobSync::TEMPO_BYTE] != tempoSent) {    // the sound has another tempo
					tempoSent = buf[vc::KnobSync::TEMPO_BYTE] & 127;
					params[TEMPO_PARAM].setValue(float(tempoSent));
				}
				for (int i = 0; i < 32; i++) {
					if (potHold[i] > 0 || cvd[i]) continue;
					const int c = vc::KnobSync::resync(i, potCode(i), cx, buf);
					if (c < 0) continue;
					params[POT_PARAM + i].setValue(float(c) / 255.f);
					potSent[i] = uint8_t(c);
				}
			}
			// An EDIT lamp lit lately means a menu is up; the program screen is the one with a name on
			// its first line and no menu, held still for half a second. FILTERS EDIT (50) is not here: the
			// firmware leaves it lit on the program screen itself.
			static const int EDIT_LEDS[7] = { 0, 10, 16, 20, 45, 46, 47 };
			bool menu = false;
			for (int e : EDIT_LEDS) if (lit(e) > 0.2f) menu = true;
			editHold = menu ? 40 : std::max(0, editHold - 1);
			if (presetPending != 0) {
				const int n = vc::Presets::COUNT;
				selectPreset(v, ((presetIndex + presetPending) % n + n) % n);
				presetPending = 0;
			}
			if (snapMutex.try_lock()) {
				v.lcd(snap.chars, snap.cgram);
				const int cur = vc::Presets::fromScreen(snap.chars);
				shownPreset = cur;
				if (presetQuiet > 0) presetQuiet--;
				else if (cur >= 0) presetIndex = cur;
				bool named = false;
				for (int k = 2; k < 16; k++) if (snap.chars[k] != ' ' && snap.chars[k] != 0) named = true;
				if (named && cur >= 0 && editHold == 0) {
					if (++presetStill >= 20) { std::memcpy(snap.preset, snap.chars, 32); snap.havePreset = true; }
				} else presetStill = 0;
				// In single mode the PRESET glass is composed from the edit buffer's own name and the sound's
				// label, in the layout the unit writes ("A37 FuturwldSV"), so it does not hang on catching the
				// program screen on the LCD.
				if (lit(49) > 0.5f) {
					uint8_t buf[vc::KnobSync::EDIT_BUFFER];
					v.xram(0, buf, sizeof(buf));
					const std::string label = vc::Presets::label(presetIndex / vc::Presets::PER_BANK, presetIndex % vc::Presets::PER_BANK);
					const uint8_t* name = buf + vc::Presets::NAME_AT;
					bool printable = false;
					for (int k = 0; k < 10; k++) if (name[k] > 32 && name[k] < 127) printable = true;
					if (printable) {
						std::memset(snap.preset, ' ', 32);
						for (size_t k = 0; k < label.size() && k < 4; k++) snap.preset[2 + k] = uint8_t(label[k]);
						for (int k = 0; k < 10; k++) snap.preset[6 + k] = (name[k] >= 32 && name[k] < 127) ? name[k] : ' ';
						snap.havePreset = true;
					}
				}
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
		json_object_set_new(root, "cvMidiChannel", json_integer(cvMidi.channel));
		json_object_set_new(root, "cvPolyToChannels", json_boolean(cvMidi.polyToChannels));
		json_object_set_new(root, "cvClockPpqn", json_integer(cvMidi.ppqn));
		json_object_set_new(root, "cvMinNoteMs", json_real(minNoteMs));
		json_t* cv = json_array();
		for (int i = 0; i < 8; i++) {
			json_t* o = json_object();
			json_object_set_new(o, "target", json_integer(cvTarget[i]));
			json_object_set_new(o, "amount", json_real(cvAmount[i]));
			json_array_append_new(cv, o);
		}
		json_object_set_new(root, "cv", cv);
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
		if (json_t* j = json_object_get(root, "cvMidiChannel")) cvMidi.channel = clamp(int(json_integer_value(j)), 0, 15);
		if (json_t* j = json_object_get(root, "cvPolyToChannels")) cvMidi.polyToChannels = json_is_true(j);
		if (json_t* j = json_object_get(root, "cvClockPpqn")) cvMidi.ppqn = clamp(int(json_integer_value(j)), 1, 24);
		if (json_t* j = json_object_get(root, "cvMinNoteMs")) minNoteMs = clamp(float(json_number_value(j)), 0.f, 4000.f);
		if (json_t* cv = json_object_get(root, "cv"))
			for (int i = 0; i < 8; i++)
				if (json_t* o = json_array_get(cv, i)) {
					if (json_t* j = json_object_get(o, "target")) cvTarget[i] = clamp(int(json_integer_value(j)), -1, 31);
					if (json_t* j = json_object_get(o, "amount")) cvAmount[i] = clamp(float(json_number_value(j)), -1.f, 1.f);
				}
		boot();
	}
};

// --- widgets ------------------------------------------------------------------------------------

namespace {

/** One 2 x 16 LCD screen, dot by dot: the unit's character ROM for the fixed glyphs and the
    controller's CGRAM for the eight the firmware defines. x, y, w, h are in pixels. */
void drawLcd(NVGcontext* vg, const uint8_t chars[32], const uint8_t cgram[64], float x, float y, float w, float h) {
	const float cellW = w / 16.f, cellH = h / 2.f;
	const float dot = std::min(cellW / 6.2f, cellH / 9.2f);
	for (int i = 0; i < 32; i++) {
		const uint8_t code = chars[i];
		const uint8_t* rows = code < 16 ? &cgram[(code & 7) * 8] : vc::lcdGlyph(code);
		const float x0 = x + (i % 16) * cellW + (cellW - 5.6f * dot) / 2, y0 = y + (i / 16) * cellH + (cellH - 8.6f * dot) / 2;
		for (int r = 0; r < 8; r++)
			for (int c = 0; c < 5; c++) {
				const bool on = rows[r] >> (4 - c) & 1;
				nvgBeginPath(vg);
				nvgRect(vg, x0 + c * dot * 1.12f, y0 + r * dot * 1.08f, dot, dot);
				nvgFillColor(vg, on ? panel::LIME : panel::alpha(panel::SAGE, 0.08f));
				nvgFill(vg);
			}
	}
}

/** The read-out well, all of it one piece of glass: the preset screen (the last program the unit
    showed), the parameter screen (the unit's LCD as it is now), and the lamps the unit answers its
    selectors and its AMOUNT destinations with, grouped by what they answer. Everything here is a
    control as well: each thing is drawn in the rectangle of the field that changes it (FIELD_* in
    Panel.hpp), so what you read and what you grab are one place. */
struct DisplayWidget : widget::Widget {
	Contagion* module = nullptr;

	struct Lamp { const char* text; int led; };
	struct Row { const char* label; int n; Lamp lamps[6]; int editLed; };

	// The selectors, one line each in SEL_PARAM order, and what each position is called. LED
	// indices are PanelMap.hpp's; `editLed` is the section's EDIT lamp, for the three whose name
	// is their EDIT key, -1 for the rest.
	static const Row* selectorRows() {
		static const Row rows[6] = {
			{ "LFO", 4, { { "1", 1 }, { "2", 2 }, { "3", 3 }, { "MOD", 4 } }, 0 },
			{ "SHAPE", 5, { { "SIN", 5 }, { "TRI", 6 }, { "SAW", 7 }, { "SQR", 8 }, { "WAV", 9 } }, -1 },
			{ "OSC", 3, { { "1", 12 }, { "2", 13 }, { "3", 14 } }, 10 },
			{ "EFFECT", 3, { { "DIST", 17 }, { "PHA", 18 }, { "CHO", 19 } }, 16 },
			{ "FILT 1", 4, { { "LP", 51 }, { "HP", 52 }, { "BP", 53 }, { "BS", 54 } }, -1 },
			{ "FILT 2", 4, { { "LP", 55 }, { "HP", 56 }, { "BP", 57 }, { "BS", 58 } }, -1 },
		};
		return rows;
	}
	// What AMOUNT steps through for each of the four modulation sources.
	static const Row* amountRows() {
		static const Row rows[4] = {
			{ "LFO 1", 6, { { "OSC 1", 21 }, { "OSC 2", 22 }, { "PW 1+2", 23 }, { "RESO", 24 }, { "F GAIN", 25 }, { "ASSIGN", 26 } }, -1 },
			{ "LFO 2", 6, { { "FILT 1", 27 }, { "FILT 2", 28 }, { "SHAPE", 29 }, { "FM AMT", 30 }, { "PAN", 31 }, { "ASSIGN", 32 } }, -1 },
			{ "LFO 3", 5, { { "OSC 1", 33 }, { "OSC 2", 34 }, { "PW 1", 35 }, { "PW 2", 36 }, { "SYNC PH", 37 } }, -1 },
			{ "MOD", 6, { { "ASGN 1", 38 }, { "ASGN 2", 39 }, { "ASGN 3", 40 }, { "ASGN 4", 41 }, { "ASGN 5", 42 }, { "ASGN 6", 43 } }, -1 },
		};
		return rows;
	}

	float led(int i) { return module ? module->lights[Contagion::LED_LIGHT + i].getBrightness() : 0.f; }
	NVGcolor lampInk(float b) { return b > 0.05f ? panel::alpha(panel::LIME, 0.35f + 0.65f * b) : panel::alpha(panel::SAGE, 0.55f); }

	/** A row of lamps: `x` and `pitch` in pixels, `y` the line's centre. */
	void drawLamps(NVGcontext* vg, const Row& r, float x, float y, float pitch, const panel::TextStyle& st) {
		for (int i = 0; i < r.n; i++)
			panel::text(vg, st.inked(lampInk(led(r.lamps[i].led))), x + i * pitch, y, r.lamps[i].text);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) return;
		NVGcontext* vg = args.vg;
		Snapshot snap;
		std::string status = "ACCESS VIRUS C";
		bool booting = false;
		if (module) {
			std::lock_guard<std::mutex> lock(module->snapMutex);
			snap = module->snap;
			status = module->status;
			booting = module->booting;
		}
		const float s = box.size.x / panel::GLASS_W;                 // pixels per mm
		const panel::TextStyle cap(panel::Face::Mono, 6.0f, panel::SAGE, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE, 0.8f);
		const panel::TextStyle name(panel::Face::Mono, 6.0f, panel::SAGE, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		const panel::TextStyle lamp(panel::Face::Mono, 7.f, panel::SAGE, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		const float capY = 3.1f * s;                                  // row 0 of the glass grid: the captions

		// PRESET: the last program screen (a click on it lists the sounds), and PARAMETER: the unit's
		// LCD as it is. Each has its step pairs beside it, named underneath.
		const Rect pre = panel::inGlass(panel::FIELD_PRESET_LCD);
		const Rect par = panel::inGlass(Rect(Vec(panel::FIELD_TEMPO.pos.x, panel::FIELD_PRESET_LCD.pos.y),
			Vec(panel::FIELD_TEMPO.size.x, panel::FIELD_PRESET_LCD.size.y)));
		panel::text(vg, cap, pre.pos.x, capY, "PRESET");
		panel::text(vg, cap, par.pos.x, capY, "PARAMETER");
		const panel::TextStyle word(panel::Face::Mono, 7.5f, panel::CLAY, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		if (!module || !status.empty()) {
			const float cx = par.getCenter().x;
			panel::text(vg, word, cx, par.pos.y + par.size.y * 0.27f, module ? status : "ACCESS VIRUS C");
			panel::text(vg, word.inked(panel::SAGE), cx, par.pos.y + par.size.y * 0.73f, !module ? "CONTAGION" : booting ? "PLEASE WAIT" : "RIGHT-CLICK: LOAD OS");
		}
		else {
			drawLcd(vg, snap.chars, snap.cgram, par.pos.x, par.pos.y, par.size.x, par.size.y);
			if (snap.havePreset) drawLcd(vg, snap.preset, snap.cgram, pre.pos.x, pre.pos.y, pre.size.x, pre.size.y);
		}
		struct Step { const Rect* f; const char* label; };
		const Step steps[6] = { { &panel::FIELD_PRESET, "PRESET" }, { &panel::FIELD_CATEGORY, "CAT" }, { &panel::FIELD_INCAT, "IN CAT" },
			{ &panel::FIELD_PARAM, "PARAM" }, { &panel::FIELD_VALUE, "VALUE" }, { &panel::FIELD_PAGE, "PAGE" } };
		const Rect lastRow = panel::inGlass(panel::FIELD_FLT2_MODE);   // row 6 of the grid
		for (const Step& st : steps) {
			const Rect r = panel::inGlass(*st.f);
			panel::text(vg, name, r.getCenter().x, lastRow.getCenter().y, st.label);
		}

		// TRANSPOSE, under the preset: its step pair, then the unit's five octave lamps.
		const Rect tr = panel::inGlass(panel::FIELD_TRANS);
		const float trX = tr.pos.x + tr.size.x + 1.5f * s, trY = tr.getCenter().y;
		const float trEnd = panel::text(vg, lamp.inked(panel::PAPER), trX, trY, "TRANSPOSE");
		static const char* const OCT[5] = { "-2", "-1", "0", "+1", "+2" };
		for (int i = 0; i < 5; i++)
			panel::text(vg, lamp.inked(lampInk(led(61 + i))), trEnd + (3.f + i * 6.f) * s, trY, OCT[i]);

		// BPM, under the parameter screen: hold and drag it. The lamp blinks at the unit's tempo.
		const Rect bpm = panel::inGlass(panel::FIELD_TEMPO);
		const float bY = bpm.getCenter().y;
		const float bEnd = panel::text(vg, lamp.inked(panel::PAPER), bpm.pos.x + 1.f * s, bY, "BPM");
		const std::string tempo = module ? module->paramQuantities[Contagion::TEMPO_PARAM]->getDisplayValueString() : "101";
		const panel::TextStyle num(panel::Face::Mono, 9.f, panel::LIME, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		const float nEnd = panel::text(vg, num, bEnd + 2.5f * s, bY, tempo);
		nvgBeginPath(vg);
		nvgCircle(vg, nEnd + 3.f * s, bY, 0.9f * s);
		nvgFillColor(vg, lampInk(led(66)));
		nvgFill(vg);

		// SELECTED: a line per selector. The positions are the selector (click one, or drag);
		// LFO, OSC and EFFECT are their section's EDIT keys, lit while the unit's EDIT lamp is.
		panel::text(vg, cap, panel::inGlass(panel::FIELD_LFO_EDIT).pos.x, capY, "SELECTED");
		const Rect* selField[6] = { &panel::FIELD_LFO_SEL, &panel::FIELD_LFO_SHAPE, &panel::FIELD_OSC_SEL,
			&panel::FIELD_FX_SEL, &panel::FIELD_FLT1_MODE, &panel::FIELD_FLT2_MODE };
		const float labelX = panel::inGlass(panel::FIELD_LFO_EDIT).pos.x + 0.5f * s;
		const Row* sel = selectorRows();
		for (int i = 0; i < 6; i++) {
			const Rect f = panel::inGlass(*selField[i]);
			const float y = f.getCenter().y;
			const NVGcolor ink = sel[i].editLed >= 0 && led(sel[i].editLed) > 0.5f ? panel::LIME : panel::PAPER;
			panel::text(vg, lamp.inked(ink), labelX, y, sel[i].label);
			drawLamps(vg, sel[i], f.pos.x + 0.5f * s, y, f.size.x / 5.f, lamp);
		}

		// AMOUNT: what each source is routed to; the whole list is the AMOUNT key.
		const Rect amt = panel::inGlass(panel::FIELD_LFO_AMOUNT);
		panel::text(vg, cap, amt.pos.x, capY, "AMOUNT");
		const float labelW = 10.5f * s, amtPitch = (amt.size.x - labelW - 0.5f * s) / 6.f;
		const Row* am = amountRows();
		for (int i = 0; i < 4; i++) {
			const float y = amt.pos.y + amt.size.y * (i + 0.5f) / 4.f;
			panel::text(vg, lamp.inked(panel::PAPER), amt.pos.x + 0.5f * s, y, am[i].label);
			drawLamps(vg, am[i], amt.pos.x + 0.5f * s + labelW, y, amtPitch, lamp);
		}
	}
};

} // namespace

/** A CV input's attenuverter, as a slider in the context menu. */
struct CvAmountQuantity : Quantity {
	Contagion* module;
	int index;
	CvAmountQuantity(Contagion* m, int i) : module(m), index(i) {}
	void setValue(float v) override { module->cvAmount[index] = clamp(v, -1.f, 1.f); }
	float getValue() override { return module->cvAmount[index]; }
	float getMinValue() override { return -1.f; }
	float getMaxValue() override { return 1.f; }
	float getDefaultValue() override { return 1.f; }
	float getDisplayValue() override { return getValue() * 100.f; }
	void setDisplayValue(float v) override { setValue(v / 100.f); }
	std::string getLabel() override { return "Amount"; }
	std::string getUnit() override { return "%"; }
};

struct CvAmountSlider : ui::Slider {
	CvAmountSlider(Contagion* m, int i) {
		quantity = new CvAmountQuantity(m, i);
		box.size.x = 220.f;
	}
	~CvAmountSlider() { delete quantity; }
};

/** Bank > sixteen at a time > the sound, named as the unit's display names it. The names are read
    from the user's own image and, for banks A and B, from the unit's battery RAM. */
void appendPresetBanks(Menu* sub, Contagion* m, int shown) {
	static const char* const kind[8] = { "A (user)", "B (user)", "C", "D", "E", "F", "G", "H" };
	for (int bank = 0; bank < 8; bank++) {
		sub->addChild(createSubmenuItem(std::string("Bank ") + kind[bank], shown / 128 == bank && shown >= 0 ? "*" : "", [=](Menu* bm) {
			for (int g = 0; g < 8; g++) {
				const int p0 = g * 16;
				bm->addChild(createSubmenuItem(vc::Presets::label(bank, p0) + " - " + std::to_string(p0 + 15), "", [=](Menu* gm) {
					for (int p = p0; p < p0 + 16; p++) {
						const std::string name = m->presetName(bank, p);
						gm->addChild(createCheckMenuItem(vc::Presets::label(bank, p) + "  " + (name.empty() ? "-" : name), "",
							[=]() { return shown == bank * 128 + p; },
							[=]() { m->presetRequest = bank * 128 + p; }));
					}
				}));
			}
		}));
	}
}

/** Presets > bank > sixteen at a time > the sound: the context menu's way to the sounds. */
void appendPresetMenu(Menu* menu, Contagion* m) {
	const int shown = m->shownPreset.load();
	std::string now = shown >= 0 ? vc::Presets::label(shown / 128, shown % 128) : "";
	if (!m->haveNames() && m->imagePath.empty()) {
		menu->addChild(createMenuLabel("Presets (load an OS image first)"));
		return;
	}
	menu->addChild(createSubmenuItem("Presets", now, [=](Menu* sub) { appendPresetBanks(sub, m, shown); }));
}

struct ContagionWidget : ModuleWidget {
	ContagionWidget(Contagion* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Contagion.svg")));
		panel::addScrews(this);
		panel::addLabels(this);

		DisplayWidget* display = new DisplayWidget;
		display->module = module;
		display->box = panel::mmRect(panel::GLASS_X, panel::GLASS_Y, panel::GLASS_W, panel::GLASS_H);
		addChild(display);

		const Vec pots[32] = { panel::RATE_POS, panel::DLY_TIME_POS, panel::SHAPE_POS, panel::WAVE_POS,
			panel::SEMITONE_POS, panel::DETUNE_POS, panel::FM_POS, panel::FX_INT_POS, panel::OSC_BAL_POS, panel::SUB_POS,
			panel::OSC_VOL_POS, panel::NOISE_POS, panel::RING_POS, panel::FX_MIX_POS, panel::DLY_FB_POS,
			panel::DLY_SEND_POS, panel::SOFT1_POS, panel::SOFT2_POS, panel::VOLUME_POS, panel::CUTOFF_POS,
			panel::CUTOFF2_POS, panel::RESO_POS, panel::ENV_AMT_POS, panel::FLT_BAL_POS, panel::F_ATT_POS,
			panel::F_DEC_POS, panel::F_SUS_POS, panel::F_REL_POS, panel::A_ATT_POS, panel::A_DEC_POS, panel::A_SUS_POS,
			panel::A_REL_POS };
		for (int i = 0; i < 32; i++)
			addParam(createParamCentered<RoundBlackKnob>(panel::mm(pots[i].x, pots[i].y), module, Contagion::POT_PARAM + i));

		// The buttons the panel still has. The rest of the unit's 35 are on the display (the
		// selectors and their EDITs, AMOUNT) or are the step pairs, which press them.
		struct Btn { int key; Vec pos; };
		const Btn buttons[15] = { { 5, panel::SYNC_POS }, { 9, panel::OSC3_ON_POS },
			{ 12, panel::DLY_EDIT_POS }, { 13, panel::ARP_ON_POS }, { 14, panel::ARP_EDIT_POS }, { 15, panel::EDIT_POS },
			{ 16, panel::GLOBAL_POS }, { 17, panel::RANDOM_POS }, { 18, panel::UNDO_POS }, { 19, panel::STORE_POS },
			{ 20, panel::MULTI_POS }, { 21, panel::SINGLE_POS }, { 28, panel::FLT_EDIT_POS }, { 31, panel::FLT_SEL1_POS },
			{ 32, panel::FLT_SEL2_POS } };
		for (const Btn& b : buttons) {
			int light = -1;
			for (auto& z : BEZEL) if (z[0] == b.key) light = z[1];
			if (light >= 0)
				addParam(createLightParamCentered<VCVLightBezel<panel::LimeLight> >(panel::mm(b.pos.x, b.pos.y), module,
					Contagion::KEY_PARAM + b.key, Contagion::LED_LIGHT + light));
			else
				addParam(createParamCentered<VCVButton>(panel::mm(b.pos.x, b.pos.y), module, Contagion::KEY_PARAM + b.key));
		}

		// The selectors, on the screen: each is the SELECTED line of its positions. A click lists them to pick one, a
		// drag steps through them; either way Selector::tick presses the unit's key until its LEDs agree.
		const Rect selField[6] = { panel::FIELD_LFO_SEL, panel::FIELD_LFO_SHAPE, panel::FIELD_OSC_SEL, panel::FIELD_FX_SEL,
			panel::FIELD_FLT1_MODE, panel::FIELD_FLT2_MODE };
		for (int i = 0; i < 6; i++) {
			panel::ScreenSelect* f = panel::createField<panel::ScreenSelect>(selField[i], module, Contagion::SEL_PARAM + i);
			f->wrap = true;          // the unit's key cycles
			addParam(f);
		}
		// A selector's name is its section's EDIT key (what pushing the selector knob pressed before it moved to the screen), and the AMOUNT
		// lamps are the AMOUNT key: held while the mouse is, like the panel's buttons.
		addParam(panel::createField<panel::ScreenButton>(panel::FIELD_LFO_EDIT, module, Contagion::KEY_PARAM + SELECTORS[0].pushKey));
		addParam(panel::createField<panel::ScreenButton>(panel::FIELD_OSC_EDIT, module, Contagion::KEY_PARAM + SELECTORS[2].pushKey));
		addParam(panel::createField<panel::ScreenButton>(panel::FIELD_FX_EDIT, module, Contagion::KEY_PARAM + SELECTORS[3].pushKey));
		addParam(panel::createField<panel::ScreenButton>(panel::FIELD_LFO_AMOUNT, module, Contagion::KEY_PARAM + 3));

		// BPM, dragged on the screen under the parameter LCD: a turn is a parameter change to the unit, sent once it settles.
		addParam(panel::createField<panel::ScreenKnob>(panel::FIELD_TEMPO, module, Contagion::TEMPO_PARAM));

		// The step pairs: UP is one detent clockwise, DOWN one counterclockwise, and each param still counts detents,
		// so the firmware side (vc::Encoder, vc::Chord) is untouched. PART stays on the panel; the rest stand on the
		// screen beside the LCD they walk.
		addParam(createParamCentered<panel::StepPair>(panel::mm(panel::PART_POS.x, panel::PART_POS.y), module, Contagion::ENC_PARAM + 0));
		addParam(panel::createField<panel::StepPair>(panel::FIELD_PARAM, module, Contagion::ENC_PARAM + 1));
		addParam(panel::createField<panel::StepPair>(panel::FIELD_VALUE, module, Contagion::ENC_PARAM + 2));
		addParam(panel::createField<panel::StepPair>(panel::FIELD_TRANS, module, Contagion::ENC_PARAM + 3));
		addParam(panel::createField<panel::StepPair>(panel::FIELD_PRESET, module, Contagion::PRESET_PARAM));
		addParam(panel::createField<panel::StepPair>(panel::FIELD_CATEGORY, module, Contagion::CATEGORY_PARAM));
		addParam(panel::createField<panel::StepPair>(panel::FIELD_INCAT, module, Contagion::INCAT_PARAM));
		addParam(panel::createField<panel::StepPair>(panel::FIELD_PAGE, module, Contagion::PAGE_PARAM));
		// The chorded gesture that stays a button: MULTI and SINGLE pressed together.
		addParam(createParamCentered<VCVButton>(panel::mm(panel::MULTISINGLE_POS.x, panel::MULTISINGLE_POS.y), module, Contagion::MULTISINGLE_PARAM));
		// The preset screen opens the sound list.
		if (module)
			addChild(panel::createMenuField(panel::FIELD_PRESET_LCD, [=](ui::Menu* menu) {
				menu->addChild(createMenuLabel("Presets"));
				if (!module->haveNames() && module->imagePath.empty()) menu->addChild(createMenuLabel("(load an OS image first)"));
				else appendPresetBanks(menu, module, module->shownPreset.load());
			}));

		// The lamps that stay lamps. The unit's other LEDs are drawn on the display.
		struct Lamp { int led; Vec pos; };
		const Lamp lamps[9] = { { 20, panel::DLY_EDIT_LED_POS }, { 45, panel::ARP_EDIT_LED_POS }, { 46, panel::EDIT_LED_POS },
			{ 47, panel::GLOBAL_LED_POS }, { 48, panel::MULTI_LED_POS }, { 49, panel::SINGLE_LED_POS },
			{ 50, panel::FLT_EDIT_LED_POS }, { 59, panel::SEL1_LED_POS }, { 60, panel::SEL2_LED_POS } };
		for (const Lamp& l : lamps)
			addChild(createLightCentered<SmallLight<panel::ClayLight> >(panel::mm(l.pos.x, l.pos.y), module, Contagion::LED_LIGHT + l.led));
		addChild(createLightCentered<SmallLight<panel::ClayLight> >(panel::mm(panel::RATE1_POS.x, panel::RATE1_POS.y), module, Contagion::RATE_LIGHT));
		addChild(createLightCentered<SmallLight<panel::ClayLight> >(panel::mm(panel::RATE23_POS.x, panel::RATE23_POS.y), module, Contagion::RATE_LIGHT + 1));

		addInput(createInputCentered<panel::PortInMain>(panel::mm(panel::IN_L_POS.x, panel::IN_L_POS.y), module, Contagion::IN_L_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::IN_R_POS.x, panel::IN_R_POS.y), module, Contagion::IN_R_INPUT));
		const Vec notes[10] = { panel::NOTE_V_POS, panel::NOTE_GATE_POS, panel::NOTE_VEL_POS, panel::BEND_POS, panel::MOD_POS,
			panel::TOUCH_POS, panel::SUSTAIN_POS, panel::CLK_POS, panel::RUN_POS, panel::RST_POS };
		for (int i = 0; i < 10; i++) {
			const bool gate = i == 1 || i >= 6;   // the gate-like ones get the trigger jack
			if (gate) addInput(createInputCentered<panel::PortTrigIn>(panel::mm(notes[i].x, notes[i].y), module, Contagion::NOTE_V_INPUT + i));
			else addInput(createInputCentered<panel::PortIn>(panel::mm(notes[i].x, notes[i].y), module, Contagion::NOTE_V_INPUT + i));
		}
		const Vec cvs[8] = { panel::CV1_POS, panel::CV2_POS, panel::CV3_POS, panel::CV4_POS, panel::CV5_POS, panel::CV6_POS,
			panel::CV7_POS, panel::CV8_POS };
		for (int i = 0; i < 8; i++)
			addInput(createInputCentered<panel::PortIn>(panel::mm(cvs[i].x, cvs[i].y), module, Contagion::CV_INPUT + i));
		const Vec gates[8] = { panel::G_PART_DN_POS, panel::G_PART_UP_POS, panel::G_PARAM_DN_POS, panel::G_PARAM_UP_POS,
			panel::G_VALUE_DN_POS, panel::G_VALUE_UP_POS, panel::G_ARP_POS, panel::G_RANDOM_POS };
		for (int i = 0; i < 8; i++)
			addInput(createInputCentered<panel::PortTrigIn>(panel::mm(gates[i].x, gates[i].y), module, Contagion::G_INPUT + i));
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
		appendPresetMenu(menu, m);
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Note and clock jacks"));
		menu->addChild(createSubmenuItem("MIDI channel", std::to_string(m->cvMidi.channel + 1), [=](Menu* sub) {
			for (int c = 0; c < 16; c++)
				sub->addChild(createCheckMenuItem(std::to_string(c + 1), "", [=]() { return m->cvMidi.channel == c; },
					[=]() { m->cvMidi.channel = c; }));
		}));
		menu->addChild(createCheckMenuItem("Polyphonic cable channel n plays MIDI channel n", "", [=]() { return m->cvMidi.polyToChannels; },
			[=]() { m->cvMidi.polyToChannels = !m->cvMidi.polyToChannels; }));
		static const int PPQN[5] = { 1, 2, 4, 8, 24 };
		int ppqnIndex = 2;
		for (int i = 0; i < 5; i++) if (PPQN[i] == m->cvMidi.ppqn) ppqnIndex = i;
		menu->addChild(createIndexSubmenuItem("Clock pulses per quarter note",
			{ "1", "2", "4 (sixteenths)", "8", "24" },
			[=]() { return ppqnIndex; }, [=](int i) { m->cvMidi.ppqn = PPQN[i]; }));
		static const float MIN_MS[8] = { 0.f, 25.f, 50.f, 100.f, 250.f, 500.f, 1000.f, 2000.f };
		int minIndex = 0;
		for (int i = 0; i < 8; i++) if (MIN_MS[i] <= m->minNoteMs + 0.5f) minIndex = i;
		menu->addChild(createIndexSubmenuItem("Minimum note length (triggers)",
			{ "Off: the gate's own length", "25 ms", "50 ms", "100 ms", "250 ms", "500 ms", "1 s", "2 s" },
			[=]() { return minIndex; }, [=](int i) { m->minNoteMs = MIN_MS[i]; }));
		menu->addChild(createMenuLabel("(set the unit's Global > Clock to Auto or MIDI)"));
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("CV inputs: knob moved, and by how much"));
		for (int i = 0; i < 8; i++) {
			menu->addChild(createSubmenuItem(string::f("CV %d", i + 1), m->cvTarget[i] < 0 ? "off" : POT_NAME[m->cvTarget[i]], [=](Menu* sub) {
				sub->addChild(createCheckMenuItem("Off", "", [=]() { return m->cvTarget[i] < 0; }, [=]() { m->cvTarget[i] = -1; }));
				for (int k = 0; k < 32; k++)
					sub->addChild(createCheckMenuItem(POT_NAME[k], "", [=]() { return m->cvTarget[i] == k; }, [=]() { m->cvTarget[i] = k; }));
			}));
			menu->addChild(new CvAmountSlider(m, i));
		}
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuItem("Power cycle", "", [=]() { m->boot(); }));
		menu->addChild(createMenuItem("Clear battery RAM (factory state)", "", [=]() { m->factoryReset(); }));
	}
};

Model* modelContagion = createModel<Contagion, ContagionWidget>("Contagion");

#include "../plugin.hpp"
#include "Panel.hpp"
#include "dsp/WindowPermuter.hpp"

using retroactive::WindowPermuter;
using retroactive::ClockSync;


// Window length = clock period * ratio.
static const float kDivRatios[] = {
	1.f / 16, 1.f / 8, 1.f / 4, 1.f / 3, 1.f / 2, 1.f, 2.f, 3.f, 4.f, 8.f, 16.f
};
static const int kNumDivs = 11;
static const int kDivUnity = 5;

static const float kTimeMinLog2 = -6.643856f;   // log2(0.01 s)
static const float kTimeMaxLog2 =  2.f;         // log2(4.00 s)
static const float kTimeDefLog2 = -2.f;         // log2(0.25 s)

static const char* kModeShort[] = {
	"IDENT", "REV", "BLK REV", "BLK INT", "SHUF", "SWAP", "STUT", "SCAT"
};
static const char* kDivShort[kNumDivs] = {
	"x1/16", "x1/8", "x1/4", "x1/3", "x1/2", "x1", "x2", "x3", "x4", "x8", "x16"
};

/** SUBDIV provably cannot change these two: REVERSE composes block-reverse with
    internal-reverse into exactly f(i) = N-1-i for any S, and IDENTITY is f(i) = i. */
static bool subdivMatters(int mode) {
	return mode != WindowPermuter::MODE_IDENTITY && mode != WindowPermuter::MODE_REVERSE;
}


struct Retroactive : Module {
	enum ParamId {
		TIME_PARAM, TIME_CV_PARAM, DIV_PARAM, MODE_PARAM, MODE_CV_PARAM,
		SUBDIV_PARAM, SUBDIV_CV_PARAM, FADE_PARAM, MIX_PARAM, MIX_CV_PARAM,
		CHAR_PARAM, FREEZE_PARAM, PARAMS_LEN
	};
	enum InputId {
		IN_L_INPUT, IN_R_INPUT, CLOCK_INPUT, RESET_INPUT, FREEZE_INPUT,
		TIME_INPUT, MODE_INPUT, SUBDIV_INPUT, MIX_INPUT, INPUTS_LEN
	};
	enum OutputId {
		OUT_L_OUTPUT, OUT_R_OUTPUT, OUTPUTS_LEN
	};
	enum LightId {
		CLOCK_LIGHT, WINDOW_LIGHT, FREEZE_LIGHT, OVERDRAFT_LIGHT, LIGHTS_LEN
	};

	WindowPermuter core;
	ClockSync clock;
	dsp::SchmittTrigger clockTrigger, resetTrigger, freezeTrigger;
	dsp::ClockDivider lightDivider;

	bool hardSync = true;        // realign the window on every clock edge
	bool wasClockConnected = false;
	bool wasLocked = false;

	// Readouts, published for the panel display from the audio thread.
	float dispWindowSec = 0.f;
	float dispLatencySec = 0.f;
	int   dispMode = WindowPermuter::MODE_REVERSE;
	int   dispSubdiv = 1;
	bool  dispClocked = false;
	bool  dispOverdraft = false;

	Retroactive() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

		configParam(TIME_PARAM, kTimeMinLog2, kTimeMaxLog2, kTimeDefLog2,
		            "Window length", " s", 2.f);
		configParam(TIME_CV_PARAM, -1.f, 1.f, 0.f, "Window length CV", "%", 0.f, 100.f);
		getParamQuantity(TIME_CV_PARAM)->randomizeEnabled = false;

		std::vector<std::string> divLabels;
		divLabels.push_back("x1/16"); divLabels.push_back("x1/8");
		divLabels.push_back("x1/4");  divLabels.push_back("x1/3");
		divLabels.push_back("x1/2");  divLabels.push_back("x1");
		divLabels.push_back("x2");    divLabels.push_back("x3");
		divLabels.push_back("x4");    divLabels.push_back("x8");
		divLabels.push_back("x16");
		configSwitch(DIV_PARAM, 0.f, (float)(kNumDivs - 1), (float)kDivUnity,
		             "Clock ratio", divLabels);

		std::vector<std::string> modeLabels;
		modeLabels.push_back("Identity");
		modeLabels.push_back("Reverse");
		modeLabels.push_back("Block reverse");
		modeLabels.push_back("Block internal");
		modeLabels.push_back("Block shuffle");
		modeLabels.push_back("Pairwise swap");
		modeLabels.push_back("Stutter");
		modeLabels.push_back("Scatter");
		configSwitch(MODE_PARAM, 0.f, (float)(WindowPermuter::NUM_MODES - 1),
		             (float)WindowPermuter::MODE_REVERSE, "Mode", modeLabels);
		configParam(MODE_CV_PARAM, -1.f, 1.f, 0.f, "Mode CV", "%", 0.f, 100.f);
		getParamQuantity(MODE_CV_PARAM)->randomizeEnabled = false;

		std::vector<std::string> subLabels;
		subLabels.push_back("1");  subLabels.push_back("2");  subLabels.push_back("4");
		subLabels.push_back("8");  subLabels.push_back("16"); subLabels.push_back("32");
		subLabels.push_back("64");
		configSwitch(SUBDIV_PARAM, 0.f, 6.f, 0.f, "Subdivisions", subLabels);
		configParam(SUBDIV_CV_PARAM, -1.f, 1.f, 0.f, "Subdivisions CV", "%", 0.f, 100.f);
		getParamQuantity(SUBDIV_CV_PARAM)->randomizeEnabled = false;

		configParam(FADE_PARAM, 0.f, 20.f, 3.f, "Declick fade", " ms");

		configParam(MIX_PARAM, 0.f, 1.f, 1.f, "Mix", "%", 0.f, 100.f);
		configParam(MIX_CV_PARAM, -1.f, 1.f, 0.f, "Mix CV", "%", 0.f, 100.f);
		getParamQuantity(MIX_CV_PARAM)->randomizeEnabled = false;

		std::vector<std::string> charLabels;
		charLabels.push_back("Crossfade");
		charLabels.push_back("Overlap");
		configSwitch(CHAR_PARAM, 0.f, 1.f, 0.f, "Character", charLabels);

		configButton(FREEZE_PARAM, "Freeze");

		configInput(IN_L_INPUT, "Left audio");
		configInput(IN_R_INPUT, "Right audio (normalled to left)");
		configInput(CLOCK_INPUT, "Clock");
		configInput(RESET_INPUT, "Reset");
		configInput(FREEZE_INPUT, "Freeze gate");
		configInput(TIME_INPUT, "Window length CV");
		configInput(MODE_INPUT, "Mode CV");
		configInput(SUBDIV_INPUT, "Subdivisions CV");
		configInput(MIX_INPUT, "Mix CV");
		configOutput(OUT_L_OUTPUT, "Left audio");
		configOutput(OUT_R_OUTPUT, "Right audio");

		configBypass(IN_L_INPUT, OUT_L_OUTPUT);
		configBypass(IN_R_INPUT, OUT_R_OUTPUT);

		lightDivider.setDivision(512);
		core.setSampleRate(APP->engine->getSampleRate());
	}

	// Also fires when the module is added, and that is exactly the call that must
	// zero the ring — so it has to be idempotent.
	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		core.setSampleRate(e.sampleRate);
		clock.setSampleRate(e.sampleRate);
		wasLocked = false;
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		core.reset();
		clock.reset();
		wasLocked = false;
	}

	void process(const ProcessArgs& args) override {
		// getVoltageSum so a polyphonic cable is summed rather than silently
		// dropping channels; IN_R normals to IN_L for mono-in dual-mono.
		float inL = inputs[IN_L_INPUT].getVoltageSum();
		float inR = inputs[IN_R_INPUT].isConnected() ? inputs[IN_R_INPUT].getVoltageSum() : inL;
		// Defensive clamp lives here, not in the core, which stays unit-agnostic.
		inL = clamp(inL, -20.f, 20.f);
		inR = clamp(inR, -20.f, 20.f);

		// --- clock -----------------------------------------------------------
		bool clockConnected = inputs[CLOCK_INPUT].isConnected();
		if (clockConnected != wasClockConnected) {
			// Cable pulled: fall back to the TIME knob immediately.
			clock.reset();
			clockTrigger.reset();
			wasLocked = false;
			wasClockConnected = clockConnected;
		}
		bool edge = false;
		if (clockConnected) {
			edge = clockTrigger.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 2.f);
			clock.process(edge);
		}

		if (resetTrigger.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 2.f)) {
			core.reseed();
			core.forceBoundary();
		}

		// --- window length ---------------------------------------------------
		float t = params[TIME_PARAM].getValue();
		if (inputs[TIME_INPUT].isConnected())
			t += inputs[TIME_INPUT].getVoltage() / 10.f * params[TIME_CV_PARAM].getValue()
			     * (kTimeMaxLog2 - kTimeMinLog2);
		t = clamp(t, kTimeMinLog2, kTimeMaxLog2);
		float knobSec = std::pow(2.f, t);

		bool clocked = clockConnected && clock.isLocked();
		if (clocked) {
			int div = (int)clamp(std::round(params[DIV_PARAM].getValue()), 0.f, (float)(kNumDivs - 1));
			core.setWindowSeconds((float)clock.period() * kDivRatios[div] / args.sampleRate);
			if (!wasLocked) {
				// Bootstrap: the second edge is the first that can give a period.
				core.forceBoundary();
			}
			else if (edge && hardSync && core.getWindowPhase() >= 0.5f) {
				// Realign only past halfway, or a slightly-early clock machine-guns
				// the window.
				core.forceBoundary();
			}
		}
		else {
			core.setWindowSeconds(knobSec);
		}
		wasLocked = clocked;

		// --- remaining controls ----------------------------------------------
		float m = params[MODE_PARAM].getValue();
		if (inputs[MODE_INPUT].isConnected())
			m += inputs[MODE_INPUT].getVoltage() / 10.f * params[MODE_CV_PARAM].getValue()
			     * (float)(WindowPermuter::NUM_MODES - 1);
		int mode = (int)clamp(std::round(m), 0.f, (float)(WindowPermuter::NUM_MODES - 1));
		core.setMode(mode);

		float sd = params[SUBDIV_PARAM].getValue();
		if (inputs[SUBDIV_INPUT].isConnected())
			sd += inputs[SUBDIV_INPUT].getVoltage() / 10.f * params[SUBDIV_CV_PARAM].getValue() * 6.f;
		int sdIdx = (int)clamp(std::round(sd), 0.f, 6.f);
		core.setSubdivisions(1 << sdIdx);

		float mix = params[MIX_PARAM].getValue();
		if (inputs[MIX_INPUT].isConnected())
			mix += inputs[MIX_INPUT].getVoltage() / 10.f * params[MIX_CV_PARAM].getValue();
		core.setMix(clamp(mix, 0.f, 1.f));

		core.setFadeSeconds(params[FADE_PARAM].getValue() * 0.001f);
		core.setCharacter((int)std::round(params[CHAR_PARAM].getValue()));

		bool freeze = params[FREEZE_PARAM].getValue() > 0.5f
		              || inputs[FREEZE_INPUT].getVoltage() >= 1.f;
		core.setFreeze(freeze);

		// --- audio -----------------------------------------------------------
		float oL = 0.f, oR = 0.f;
		core.process(inL, inR, oL, oR);
		outputs[OUT_L_OUTPUT].setVoltage(oL);
		outputs[OUT_R_OUTPUT].setVoltage(oR);

		// --- lights and panel readouts ---------------------------------------
		if (lightDivider.process()) {
			float dt = args.sampleTime * lightDivider.getDivision();

			lights[CLOCK_LIGHT].setBrightness(
			    clocked ? (clock.isStale() ? 0.15f : 1.f) : 0.f);
			// Ramps down through the window, so the boundary reads as a flash.
			lights[WINDOW_LIGHT].setBrightness(1.f - core.getWindowPhase());
			lights[FREEZE_LIGHT].setBrightness(freeze ? 1.f : 0.f);

			// Overdraft: FADE is asking for more than half the sub-block, so it is
			// being clamped. A state worth naming, not a fault worth warning about.
			bool overdraft = core.isOverdraft();
			lights[OVERDRAFT_LIGHT].setBrightnessSmooth(overdraft ? 1.f : 0.f, dt);

			dispWindowSec = core.getWindowSeconds();
			dispLatencySec = core.getLatencySeconds();
			dispMode = core.getMode();
			dispSubdiv = core.getSubdivisions();
			dispClocked = clocked;
			dispOverdraft = overdraft;
		}
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "seed", json_integer((json_int_t)core.getSeed()));
		json_object_set_new(root, "dryCompensation", json_boolean(dryComp));
		json_object_set_new(root, "equalPowerFade", json_boolean(equalPower));
		json_object_set_new(root, "hardSync", json_boolean(hardSync));
		return root;
	}

	void dataFromJson(json_t* root) override {
		json_t* j;
		j = json_object_get(root, "seed");
		if (j) core.setSeed((uint32_t)json_integer_value(j));
		j = json_object_get(root, "dryCompensation");
		if (j) { dryComp = json_boolean_value(j); core.setDryCompensation(dryComp); }
		j = json_object_get(root, "equalPowerFade");
		if (j) { equalPower = json_boolean_value(j); core.setEqualPowerFade(equalPower); }
		j = json_object_get(root, "hardSync");
		if (j) hardSync = json_boolean_value(j);
	}

	bool dryComp = false;
	bool equalPower = true;

	void setDryComp(bool v) { dryComp = v; core.setDryCompensation(v); }
	void setEqualPower(bool v) { equalPower = v; core.setEqualPowerFade(v); }
	void newSeed() { core.setSeed((uint32_t)(random::u32() | 1u)); core.reseed(); }
};


// ---------------------------------------------------------------------------
// Look and feel. The palette, the shared hardware and the entire silkscreen come
// from src/PanelTheme.hpp, generated by tools/panel.py -- see ../panelkit/README.md.
// Nothing about the look is written here, which is what keeps this module,
// Uncertainty Policy and PatchAudit on one design language.

// TIME's knob. Custom knob art replaces this alias.
typedef RoundLargeBlackKnob TimingKnob;
typedef RoundBlackKnob      PanelKnob;


// ---------------------------------------------------------------------------
// Panel display: window length and latency, and every setting that cuts the
// window up, each one a control. The cells are the FIELD_* rectangles the spec
// cut the glass into (src/Retroactive/Panel.hpp); the fields that take the mouse
// sit on the same rectangles, so a value is grabbed exactly where it is printed.
// Rack 2 has no latency-reporting API, so this and the manual are the only
// disclosure that the output is N+L early.
//
// Numerals are DSEG7 (seven-segment); words stay monospace. DSEG7's cmap is
// missing most punctuation, and Rack chains NotoSansJP as a fallback onto every
// font, so a stray '+' or '%' would silently render in a proportional Japanese
// sans rather than as a visible tofu box. Everything drawn in the segment face
// is therefore restricted to [0-9 . : -], which the format strings below honour.

struct RetroactiveDisplay : LedDisplay {
	Retroactive* module = NULL;

	/** Splits into a segment-safe numeric part and a unit that stays in the text
	    face -- panel::segValue draws the pair. */
	static void splitTime(float sec, std::string& num, std::string& unit) {
		if (sec < 1.f)       { num = string::f("%.0f", sec * 1000.f); unit = "ms"; }
		else if (sec < 10.f) { num = string::f("%.3f", sec);          unit = "s"; }
		else                 { num = string::f("%.1f", sec);          unit = "s"; }
	}

	static float baseOf(const Rect& c) { return c.pos.y + c.size.y * 0.76f; }

	/** A small caption at the left of a cell and its value at the right. */
	static void pair(NVGcontext* vg, const Rect& c, const char* tag,
	                 const std::string& value, NVGcolor ink) {
		const panel::TextStyle TAG(panel::Face::Mono, 7.5f, panel::SAGE,
			NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
		const panel::TextStyle VAL(panel::Face::Mono, 9.f, ink,
			NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE);
		panel::text(vg, TAG, c.pos.x + 3.f, baseOf(c), tag);
		panel::text(vg, VAL, c.pos.x + c.size.x - 3.f, baseOf(c), value);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) {
			LedDisplay::drawLayer(args, layer);
			return;
		}
		NVGcontext* vg = args.vg;
		float win = module ? module->dispWindowSec : 0.25f;
		float lat = module ? module->dispLatencySec : 0.25f;
		int mode = module ? module->dispMode : WindowPermuter::MODE_REVERSE;
		int sub = module ? module->dispSubdiv : 1;
		bool clocked = module ? module->dispClocked : false;
		bool overdraft = module ? module->dispOverdraft : false;
		auto param = [&](int id, float dflt) {
			return module ? module->params[id].getValue() : dflt;
		};
		const int div = clamp((int) std::round(param(Retroactive::DIV_PARAM, (float) kDivUnity)), 0, kNumDivs - 1);
		const bool overlap = param(Retroactive::CHAR_PARAM, 0.f) > 0.5f;
		const bool latched = param(Retroactive::FREEZE_PARAM, 0.f) > 0.5f;
		const bool frozen = module && module->lights[Retroactive::FREEZE_LIGHT].getBrightness() > 0.5f;
		const float fade = param(Retroactive::FADE_PARAM, 3.f);
		const float mix = param(Retroactive::MIX_PARAM, 1.f);

		std::string num, unit;
		const NVGcolor dim = panel::alpha(panel::LIME, 0.55f);
		// Every run goes through panel::text, so no style a row sets can leak into
		// the row after it -- the segment face in particular used to inherit
		// whatever letter spacing the mode word left behind.
		const panel::TextStyle MODE(panel::Face::Mono, 10.f, panel::MINT,
			NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE, -0.5f);
		const panel::TextStyle TAG(panel::Face::Mono, 7.5f, panel::SAGE,
			NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);

		// Line 1: window length (TIME), and the mode it is running.
		const Rect t = panel::inGlass(panel::FIELD_TIME_FIELD);
		splitTime(win, num, unit);
		panel::segValue(vg, t.pos.x + 3.f, baseOf(t), 11.f, num, unit, panel::LIME);
		const Rect m = panel::inGlass(panel::FIELD_MODE);
		panel::text(vg, MODE, m.pos.x + m.size.x - 3.f, baseOf(m), kModeShort[mode]);

		// Line 2: the inherent latency -- read only, in the cells left of
		// SUBDIV -- then the sub-block count, shown as "--" where SUBDIV provably
		// has no effect, so that is visible, not mysterious, and lime while the
		// fade is overdrawn; then the clock ratio, dim while nothing is clocking.
		const Rect sd = panel::inGlass(panel::FIELD_SUBDIV);
		float x = panel::text(vg, TAG, t.pos.x + 3.f, baseOf(sd), clocked ? "CLK" : "LAT");
		splitTime(clocked ? win : lat, num, unit);
		panel::segValue(vg, x + 2.5f, baseOf(sd), 8.f, num, unit, dim);
		pair(vg, sd, "SUB", subdivMatters(mode) ? string::f("%d", sub) : std::string("--"),
		     overdraft ? panel::LIME : dim);
		pair(vg, panel::inGlass(panel::FIELD_DIV), "DIV", kDivShort[div],
		     clocked ? panel::LIME : dim);

		// Line 3: the character of the seams, and FREEZE -- latched here, or
		// held by the gate at the FREEZE jack.
		pair(vg, panel::inGlass(panel::FIELD_CHAR), "CHAR", overlap ? "OVERLAP" : "XFADE",
		     panel::MINT);
		pair(vg, panel::inGlass(panel::FIELD_FREEZE), "FREEZE",
		     latched ? "ON" : (frozen ? "GATE" : "OFF"), frozen ? panel::PAPER : dim);

		// Line 4: FADE, lime with SUB while it is overdrawn, and MIX.
		pair(vg, panel::inGlass(panel::FIELD_FADE_FIELD), "FADE", string::f("%.1fms", fade),
		     overdraft ? panel::LIME : dim);
		pair(vg, panel::inGlass(panel::FIELD_MIX_FIELD), "MIX", string::f("%.0f%%", mix * 100.f),
		     panel::LIME);
	}
};


struct RetroactiveWidget : ModuleWidget {
	RetroactiveWidget(Retroactive* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Retroactive.svg")));

		panel::addScrews(this);
		panel::addLabels(this);

		RetroactiveDisplay* display = new RetroactiveDisplay;
		display->module = module;
		display->box.pos = panel::mm(panel::GLASS_X, panel::GLASS_Y);
		display->box.size = panel::mm(panel::GLASS_W, panel::GLASS_H);
		addChild(display);

		// The read-out's fields, over the cells it draws them in.
		addParam(panel::createField<panel::ScreenKnob>(panel::FIELD_TIME_FIELD, module, Retroactive::TIME_PARAM));
		addParam(panel::createField<panel::ScreenSelect>(panel::FIELD_MODE, module, Retroactive::MODE_PARAM));
		addParam(panel::createField<panel::ScreenSelect>(panel::FIELD_SUBDIV, module, Retroactive::SUBDIV_PARAM));
		addParam(panel::createField<panel::ScreenSelect>(panel::FIELD_DIV, module, Retroactive::DIV_PARAM));
		addParam(panel::createField<panel::ScreenSwitch>(panel::FIELD_CHAR, module, Retroactive::CHAR_PARAM));
		addParam(panel::createField<panel::ScreenSwitch>(panel::FIELD_FREEZE, module, Retroactive::FREEZE_PARAM));
		addParam(panel::createField<panel::ScreenKnob>(panel::FIELD_FADE_FIELD, module, Retroactive::FADE_PARAM));
		addParam(panel::createField<panel::ScreenKnob>(panel::FIELD_MIX_FIELD, module, Retroactive::MIX_PARAM));

		addParam(createParamCentered<TimingKnob>(panel::mm(panel::TIME_POS.x, panel::TIME_POS.y), module, Retroactive::TIME_PARAM));
		addParam(createParamCentered<PanelKnob>(panel::mm(panel::FADE_POS.x, panel::FADE_POS.y), module, Retroactive::FADE_PARAM));
		addParam(createParamCentered<PanelKnob>(panel::mm(panel::MIX_POS.x, panel::MIX_POS.y), module, Retroactive::MIX_PARAM));

		addParam(createParamCentered<Trimpot>(panel::mm(panel::TIME_CV_POS.x, panel::TIME_CV_POS.y), module, Retroactive::TIME_CV_PARAM));
		addParam(createParamCentered<Trimpot>(panel::mm(panel::MODE_CV_POS.x, panel::MODE_CV_POS.y), module, Retroactive::MODE_CV_PARAM));
		addParam(createParamCentered<Trimpot>(panel::mm(panel::SUBDIV_CV_POS.x, panel::SUBDIV_CV_POS.y), module, Retroactive::SUBDIV_CV_PARAM));
		addParam(createParamCentered<Trimpot>(panel::mm(panel::MIX_CV_POS.x, panel::MIX_CV_POS.y), module, Retroactive::MIX_CV_PARAM));

		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::TIME_IN_POS.x, panel::TIME_IN_POS.y), module, Retroactive::TIME_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::MODE_IN_POS.x, panel::MODE_IN_POS.y), module, Retroactive::MODE_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::SUBDIV_IN_POS.x, panel::SUBDIV_IN_POS.y), module, Retroactive::SUBDIV_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::MIX_IN_POS.x, panel::MIX_IN_POS.y), module, Retroactive::MIX_INPUT));

		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::CLOCK_IN_POS.x, panel::CLOCK_IN_POS.y), module, Retroactive::CLOCK_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::RESET_IN_POS.x, panel::RESET_IN_POS.y), module, Retroactive::RESET_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::FREEZE_IN_POS.x, panel::FREEZE_IN_POS.y), module, Retroactive::FREEZE_INPUT));

		addInput(createInputCentered<panel::PortInMain>(panel::mm(panel::IN_L_POS.x, panel::IN_L_POS.y), module, Retroactive::IN_L_INPUT));
		addInput(createInputCentered<panel::PortInMain>(panel::mm(panel::IN_R_POS.x, panel::IN_R_POS.y), module, Retroactive::IN_R_INPUT));
		addOutput(createOutputCentered<panel::PortOutMain>(panel::mm(panel::OUT_L_POS.x, panel::OUT_L_POS.y), module, Retroactive::OUT_L_OUTPUT));
		addOutput(createOutputCentered<panel::PortOutMain>(panel::mm(panel::OUT_R_POS.x, panel::OUT_R_POS.y), module, Retroactive::OUT_R_OUTPUT));

		addChild(createLightCentered<SmallLight<panel::PaperLight> >(
		             panel::mm(panel::WINDOW_POS.x, panel::WINDOW_POS.y), module, Retroactive::WINDOW_LIGHT));
		addChild(createLightCentered<MediumLight<panel::LimeLight> >(
		             panel::mm(panel::OVERDRAFT_POS.x, panel::OVERDRAFT_POS.y), module, Retroactive::OVERDRAFT_LIGHT));
		addChild(createLightCentered<SmallLight<panel::MintLight> >(
		             panel::mm(panel::CLOCK_LED_POS.x, panel::CLOCK_LED_POS.y), module, Retroactive::CLOCK_LIGHT));
	}

	void appendContextMenu(Menu* menu) override {
		Retroactive* m = dynamic_cast<Retroactive*>(module);
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Retroactive"));

		menu->addChild(createBoolMenuItem("Dry path compensation", "",
			[=]() { return m->dryComp; },
			[=](bool v) { m->setDryComp(v); }));

		menu->addChild(createBoolMenuItem("Equal-power fade", "",
			[=]() { return m->equalPower; },
			[=](bool v) { m->setEqualPower(v); }));

		menu->addChild(createBoolMenuItem("Hard-sync to clock", "",
			[=]() { return m->hardSync; },
			[=](bool v) { m->hardSync = v; }));

		menu->addChild(createMenuItem("New random seed", "",
			[=]() { m->newSeed(); }));
	}
};


Model* modelRetroactive = createModel<Retroactive, RetroactiveWidget>("Retroactive");

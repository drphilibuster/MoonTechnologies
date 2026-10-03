#include "../plugin.hpp"
#include "Panel.hpp"
#include "Matrix.hpp"

struct ObfuscationModule : Module {
	enum ParamId {
		FREQ_PARAM, PINCH_PARAM, STAGES_PARAM,
		FREQ_CV_PARAM, PINCH_CV_PARAM, STAGES_CV_PARAM,
		RANDOM_PARAM, MODE_PARAM, SPREAD_PARAM,
		DRIVE_PARAM, BRIGHT_PARAM, CLIP_PARAM, BOOST_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		IN_INPUT, FREQ_INPUT, PINCH_INPUT, STAGES_INPUT,
		CLOCK_INPUT, GATE_INPUT,
		INPUTS_LEN
	};
	enum OutputId { OUT_OUTPUT, OUTPUTS_LEN };
	enum LightId { FIRE_LIGHT, LIGHTS_LEN };

	obf::Matrix dsp_;
	dsp::SchmittTrigger clockTrig, gateTrig;
	dsp::ClockDivider lightDivider;

	// What the read-out draws, copied at the light rate: every band's stage
	// cutoffs in Hz, how many stages are running, and the effective settings
	// once CV is added.
	float dispF[3][obf::MAX_STAGES] = {};
	float dispStages = 8.f, dispHz = 640.f, dispQ = 1.f;
	bool dispFrozen = false;
	dsp::ClockDivider dispDivider;      // the stage map needs refreshing at a screen's rate, not a light's

	ObfuscationModule() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

		configParam(FREQ_PARAM, 0.f, 1.f, 0.5f, "Frequency", " Hz", 2.f, 40.f);
		configParam(PINCH_PARAM, 0.f, 1.f, 0.3f, "Pinch (allpass Q: concentrates delay around FREQ)", "%", 0.f, 100.f);
		configParam(STAGES_PARAM, 1.f, 96.f, 8.f, "Allpass stages per band");
		paramQuantities[STAGES_PARAM]->snapEnabled = true;
		configParam(FREQ_CV_PARAM, -1.f, 1.f, 0.f, "Frequency CV", "%", 0.f, 100.f);
		configParam(PINCH_CV_PARAM, -1.f, 1.f, 0.f, "Pinch CV", "%", 0.f, 100.f);
		configParam(STAGES_CV_PARAM, -1.f, 1.f, 0.f, "Stages CV", "%", 0.f, 100.f);
		configSwitch(RANDOM_PARAM, 0.f, 1.f, 0.f, "Randomize", {"Off", "On"});
		configSwitch(MODE_PARAM, 0.f, 1.f, 0.f, "Random trigger source",
			{"Clock input", "Input envelope (peaks)"});
		configParam(SPREAD_PARAM, 0.f, 1.f, 0.f, "Spread (stage cutoff / feedback scatter)", "%", 0.f, 100.f);
		configParam(DRIVE_PARAM, 0.f, 1.f, 0.2f, "Drive", "%", 0.f, 100.f);
		configParam(BRIGHT_PARAM, 0.f, 1.f, 0.5f, "Brightness");
		configParam(CLIP_PARAM, 0.f, 1.f, 1.f, "Clip threshold");
		configParam(BOOST_PARAM, 0.f, 1.f, 0.f, "Boost", " dB", 0.f, 24.f);

		configInput(IN_INPUT, "Audio");
		configInput(FREQ_INPUT, "Frequency CV");
		configInput(PINCH_INPUT, "Pinch CV");
		configInput(STAGES_INPUT, "Stages CV");
		configInput(CLOCK_INPUT, "Randomize clock");
		configInput(GATE_INPUT, "Randomize + freeze gate (held = set frozen)");
		configOutput(OUT_OUTPUT, "Audio");
		configLight(FIRE_LIGHT, "Randomized");

		lightDivider.setDivision(32);
		dispDivider.setDivision(512);
		onSampleRateChange({APP->engine->getSampleRate(), 0.f});
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		dsp_.setRate(e.sampleRate);
		dsp_.reset();
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		dsp_.reset();
	}

	float cv(int in, int atten, float scale) {
		if (!inputs[in].isConnected()) return 0.f;
		return inputs[in].getVoltage() * 0.1f * params[atten].getValue() * scale;
	}

	void process(const ProcessArgs& args) override {
		obf::Params p;
		p.freq = clamp(params[FREQ_PARAM].getValue() + cv(FREQ_INPUT, FREQ_CV_PARAM, 1.f), 0.f, 1.f);
		p.pinch = clamp(params[PINCH_PARAM].getValue() + cv(PINCH_INPUT, PINCH_CV_PARAM, 1.f), 0.f, 1.f);
		p.stages = clamp(params[STAGES_PARAM].getValue() + cv(STAGES_INPUT, STAGES_CV_PARAM, 95.f), 1.f, 96.f);
		p.spread = params[SPREAD_PARAM].getValue();
		p.drive = params[DRIVE_PARAM].getValue();
		p.bright = params[BRIGHT_PARAM].getValue();
		p.clip = params[CLIP_PARAM].getValue();
		p.boost = params[BOOST_PARAM].getValue();

		bool clockEdge = clockTrig.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 1.f);
		// A level, not an edge: held high it freezes the matrix. Its rising edge re-rolls.
		bool gate = inputs[GATE_INPUT].getVoltage() >= 1.f;
		bool randSw = params[RANDOM_PARAM].getValue() > 0.5f;
		// No clock patched means the peaks drive the roll, whatever the switch says.
		bool envMode = params[MODE_PARAM].getValue() > 0.5f || !inputs[CLOCK_INPUT].isConnected();

		float x = inputs[IN_INPUT].getVoltage() * 0.2f;
		float y = dsp_.process(x, p, clockEdge, gate, randSw, envMode);
		outputs[OUT_OUTPUT].setVoltage(clamp(y * 5.f, -12.f, 12.f));

		if (lightDivider.process()) {
			lights[FIRE_LIGHT].setBrightness(clamp(dsp_.fired, 0.f, 1.f));
			dispFrozen = gate;
		}
		if (dispDivider.process()) {
			static const float bandMul[3] = {0.25f, 1.f, 4.f};
			const float f0 = obf::Matrix::freqHz(p.freq);
			for (int b = 0; b < 3; b++)
				for (int i = 0; i < obf::MAX_STAGES; i++)
					dispF[b][i] = f0 * bandMul[b] * std::exp2(p.spread * 2.f * dsp_.band[b].off[i]);
			dispStages = p.stages;
			dispHz = f0;
			dispQ = obf::Matrix::pinchQ(p.pinch);
		}
	}
};

/** The read-out: the matrix made visible. One lane per band, every running
 *  stage's cutoff a tick on a log-frequency axis (20 Hz to 20 kHz), the two
 *  crossovers marked -- so SPREAD fans the ticks out and RANDOM throws them
 *  about where you can see it. The top line and the bottom line are fields. */
struct ObfuscationDisplay : LedDisplay {
	ObfuscationModule* module = NULL;

	static void cell(NVGcontext* vg, const Rect& f, const char* tag, const std::string& v, NVGcolor ink) {
		const Rect c = panel::inGlass(f);
		const float base = c.pos.y + c.size.y * 0.74f;
		const panel::TextStyle TAG(panel::Face::Mono, 6.f, panel::SAGE, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
		const panel::TextStyle VAL(panel::Face::Mono, 8.f, ink, NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE);
		panel::text(vg, TAG, c.pos.x + 2.f, base, tag);
		panel::text(vg, VAL, c.pos.x + c.size.x - 2.f, base, v);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) {
			LedDisplay::drawLayer(args, layer);
			return;
		}
		NVGcontext* vg = args.vg;
		using M = ObfuscationModule;
		const float hz = module ? module->dispHz : 640.f;
		const int n = module ? (int)std::round(module->dispStages) : 8;
		cell(vg, panel::FIELD_FREQ_FIELD, "F", hz < 1000.f ? string::f("%.0f", hz) : string::f("%.1fk", hz / 1000.f), panel::LIME);
		cell(vg, panel::FIELD_PINCH, "Q", string::f("%.1f", module ? module->dispQ : 1.f), panel::LIME);
		cell(vg, panel::FIELD_STAGES, "N", string::f("%d", n), panel::LIME);
		const bool rnd = module && module->params[M::RANDOM_PARAM].getValue() > 0.5f;
		const bool env = module && module->params[M::MODE_PARAM].getValue() > 0.5f;
		cell(vg, panel::FIELD_RANDOM, "RND", rnd ? "ON" : "OFF", rnd ? panel::LIME : panel::SAGE);
		cell(vg, panel::FIELD_MODE, "BY", env ? "ENV" : "CLK", panel::LIME);
		if (module) {
			const panel::TextStyle ST(panel::Face::Mono, 7.f, panel::CLAY, NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE);
			const Rect r = panel::inGlass(panel::FIELD_MODE);
			const float fired = module->lights[M::FIRE_LIGHT].getBrightness();
			if (module->dispFrozen)
				panel::text(vg, ST, box.size.x - 4.f, r.pos.y + r.size.y * 0.74f, "FROZEN");
			else if (fired > 0.05f)
				panel::text(vg, ST.inked(panel::alpha(panel::PAPER, fired)), box.size.x - 4.f, r.pos.y + r.size.y * 0.74f, "ROLL");
		}

		// the stage map, between the two lines of fields
		const Rect top = panel::inGlass(panel::FIELD_FREQ_FIELD), bot = panel::inGlass(panel::FIELD_RANDOM);
		const float x0 = top.pos.x + 2.f, x1 = box.size.x - 4.f;
		const float y0 = top.pos.y + top.size.y + 2.f, y1 = bot.pos.y - 2.f;
		const float lane = (y1 - y0) / 3.f;
		auto X = [&](float f) {
			return x0 + (x1 - x0) * clamp(std::log2(f / 20.f) / std::log2(1000.f), 0.f, 1.f);
		};
		// decades, faintly, and the crossovers the bands are split at
		for (float f : {100.f, 1000.f, 10000.f}) {
			nvgBeginPath(vg); nvgRect(vg, X(f), y0, 0.6f, y1 - y0);
			nvgFillColor(vg, panel::alpha(panel::SAGE, 0.15f)); nvgFill(vg);
		}
		for (float f : {250.f, 2500.f}) {
			nvgBeginPath(vg); nvgRect(vg, X(f) - 0.4f, y0, 0.8f, y1 - y0);
			nvgFillColor(vg, panel::alpha(panel::MINT, 0.45f)); nvgFill(vg);
		}
		const char* names[3] = {"LO", "MID", "HI"};
		const panel::TextStyle LBL(panel::Face::Mono, 5.f, panel::alpha(panel::SAGE, 0.7f), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		for (int b = 0; b < 3; b++) {
			const float ly = y0 + lane * (2 - b);          // low band at the bottom, as a spectrum reads
			panel::text(vg, LBL, x0, ly + lane / 2.f, names[b]);
			for (int i = 0; i < n; i++) {
				const float f = module ? module->dispF[b][i] : 160.f * std::pow(4.f, (float)b);
				nvgBeginPath(vg); nvgRect(vg, X(f) - 0.4f, ly + lane * 0.18f, 0.8f, lane * 0.64f);
				nvgFillColor(vg, panel::alpha(panel::LIME, 0.75f)); nvgFill(vg);
			}
		}
	}
};

struct ObfuscationWidget : ModuleWidget {
	ObfuscationWidget(ObfuscationModule* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Obfuscation.svg")));
		panel::addScrews(this);
		panel::addLabels(this);

		using M = ObfuscationModule;
		auto at = [](Vec v) { return panel::mm(v.x, v.y); };

		ObfuscationDisplay* display = new ObfuscationDisplay;
		display->module = module;
		display->box.pos = panel::mm(panel::GLASS_X, panel::GLASS_Y);
		display->box.size = panel::mm(panel::GLASS_W, panel::GLASS_H);
		addChild(display);
		addParam(panel::createField<panel::ScreenKnob>(panel::FIELD_FREQ_FIELD, module, M::FREQ_PARAM));
		addParam(panel::createField<panel::ScreenKnob>(panel::FIELD_PINCH, module, M::PINCH_PARAM));
		addParam(panel::createField<panel::ScreenKnob>(panel::FIELD_STAGES, module, M::STAGES_PARAM));
		addParam(panel::createField<panel::ScreenSwitch>(panel::FIELD_RANDOM, module, M::RANDOM_PARAM));
		addParam(panel::createField<panel::ScreenSwitch>(panel::FIELD_MODE, module, M::MODE_PARAM));

		addParam(createParamCentered<RoundLargeBlackKnob>(at(panel::FREQ_POS), module, M::FREQ_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(panel::SPREAD_POS), module, M::SPREAD_PARAM));
		addChild(createLightCentered<SmallLight<panel::PaperLight> >(at(panel::FIRE_LED_POS), module, M::FIRE_LIGHT));
		addParam(createParamCentered<Trimpot>(at(panel::FREQ_CV_POS), module, M::FREQ_CV_PARAM));
		addParam(createParamCentered<Trimpot>(at(panel::PINCH_CV_POS), module, M::PINCH_CV_PARAM));
		addParam(createParamCentered<Trimpot>(at(panel::STAGES_CV_POS), module, M::STAGES_CV_PARAM));
		addInput(createInputCentered<panel::PortIn>(at(panel::FREQ_IN_POS), module, M::FREQ_INPUT));
		addInput(createInputCentered<panel::PortIn>(at(panel::PINCH_IN_POS), module, M::PINCH_INPUT));
		addInput(createInputCentered<panel::PortIn>(at(panel::STAGES_IN_POS), module, M::STAGES_INPUT));

		addParam(createParamCentered<RoundBlackKnob>(at(panel::DRIVE_POS), module, M::DRIVE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(panel::BRIGHT_POS), module, M::BRIGHT_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(panel::CLIP_POS), module, M::CLIP_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(panel::BOOST_POS), module, M::BOOST_PARAM));

		addInput(createInputCentered<panel::PortIn>(at(panel::IN_POS), module, M::IN_INPUT));
		addInput(createInputCentered<panel::PortTrigIn>(at(panel::CLOCK_POS), module, M::CLOCK_INPUT));
		addInput(createInputCentered<panel::PortTrigIn>(at(panel::GATE_POS), module, M::GATE_INPUT));
		addOutput(createOutputCentered<panel::PortOutMain>(at(panel::OUT_POS), module, M::OUT_OUTPUT));
	}
};

Model* modelObfuscation = createModel<ObfuscationModule, ObfuscationWidget>("Obfuscation");

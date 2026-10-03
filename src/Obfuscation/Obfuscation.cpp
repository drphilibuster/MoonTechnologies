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

		if (lightDivider.process())
			lights[FIRE_LIGHT].setBrightness(clamp(dsp_.fired, 0.f, 1.f));
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

		addInput(createInputCentered<panel::PortIn>(at(panel::IN_POS), module, M::IN_INPUT));
		addParam(createParamCentered<RoundLargeBlackKnob>(at(panel::FREQ_POS), module, M::FREQ_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(panel::PINCH_POS), module, M::PINCH_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(panel::STAGES_POS), module, M::STAGES_PARAM));
		addParam(createParamCentered<Trimpot>(at(panel::FREQ_CV_POS), module, M::FREQ_CV_PARAM));
		addParam(createParamCentered<Trimpot>(at(panel::PINCH_CV_POS), module, M::PINCH_CV_PARAM));
		addParam(createParamCentered<Trimpot>(at(panel::STAGES_CV_POS), module, M::STAGES_CV_PARAM));
		addInput(createInputCentered<panel::PortIn>(at(panel::FREQ_IN_POS), module, M::FREQ_INPUT));
		addInput(createInputCentered<panel::PortIn>(at(panel::PINCH_IN_POS), module, M::PINCH_INPUT));
		addInput(createInputCentered<panel::PortIn>(at(panel::STAGES_IN_POS), module, M::STAGES_INPUT));

		addParam(createParamCentered<CKSS>(at(panel::RANDOM_POS), module, M::RANDOM_PARAM));
		addParam(createParamCentered<CKSS>(at(panel::MODE_POS), module, M::MODE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(panel::SPREAD_POS), module, M::SPREAD_PARAM));
		addChild(createLightCentered<SmallLight<panel::PaperLight> >(at(panel::FIRE_LED_POS), module, M::FIRE_LIGHT));
		addInput(createInputCentered<panel::PortTrigIn>(at(panel::CLOCK_POS), module, M::CLOCK_INPUT));
		addInput(createInputCentered<panel::PortTrigIn>(at(panel::GATE_POS), module, M::GATE_INPUT));

		addParam(createParamCentered<RoundBlackKnob>(at(panel::DRIVE_POS), module, M::DRIVE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(panel::BRIGHT_POS), module, M::BRIGHT_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(panel::CLIP_POS), module, M::CLIP_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(at(panel::BOOST_POS), module, M::BOOST_PARAM));

		addOutput(createOutputCentered<panel::PortOutMain>(at(panel::OUT_POS), module, M::OUT_OUTPUT));
	}
};

Model* modelObfuscation = createModel<ObfuscationModule, ObfuscationWidget>("Obfuscation");

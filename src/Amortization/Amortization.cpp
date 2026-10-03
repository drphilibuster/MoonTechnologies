// Amortization -- the Pittsburgh Modular Verbtronic, filed as PUB 535.
//
// Not a reverb in its image: the module's public-domain schematic, solved. Three
// PT2399 echo chips wired into a recirculating network, an op-amp tone control
// and a zener limiter on the feedback, a make-up amplifier, and a linearised
// SSM2164 VCA for the wet/dry mix. VERB and TRONIC are the same circuit with a
// DG202 switching a second resistor across each chip's clock; the chips then
// run faster and every delay shortens. See Verbtronic.hpp for the circuit and
// docs/Amortization.md for what is the schematic and what is assumed.
#include "../plugin.hpp"
#include "Panel.hpp"
#include "Verbtronic.hpp"

using namespace amortization;


struct Amortization : Module {
	enum ParamId {
		FEEDBACK_PARAM, TILT_PARAM, MODE_PARAM, MIX_PARAM, MIX_CV_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		IN_INPUT, MIX_INPUT, MODE_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		MIX_OUTPUT, VERB_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		LIMIT_LIGHT, TRONIC_LIGHT,
		LIGHTS_LEN
	};

	Verbtronic circuit;
	dsp::SchmittTrigger modeGate;
	dsp::ClockDivider lightDivider;

	// The pots are smoothed so a turned knob is a swept resistance, not a step.
	double fbS = 0.5, tiltS = 0.5, mixS = 0.5, attS = 0.5;
	double smoothK = 0.01;

	// Context menu: which way the TILT pot turns. The schematic puts the tone
	// stage's bass-up, treble-down end at the CW lug; the manual's copy reads
	// "low to flat to high". Unresolved without a unit in hand.
	bool tiltCwBright = false;

	// Read-outs, published for the display from the audio thread.
	float dispDelayMs = 0.f;
	float dispClockMHz = 0.f;
	bool  dispTronic = false;
	bool  dispLimit = false;

	Amortization() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

		configParam(FEEDBACK_PARAM, 0.f, 1.f, 0.5f, "Feedback", "%", 0.f, 100.f);
		configParam(TILT_PARAM, -1.f, 1.f, 0.f, "Tonal tilt", "%", 0.f, 100.f);
		std::vector<std::string> modeLabels;
		modeLabels.push_back("Verb");
		modeLabels.push_back("Tronic");
		configSwitch(MODE_PARAM, 0.f, 1.f, 0.f, "Mode", modeLabels);
		configParam(MIX_PARAM, 0.f, 1.f, 0.5f, "Output mix", "%", 0.f, 100.f);
		configParam(MIX_CV_PARAM, -1.f, 1.f, 0.f, "Mix CV attenuverter", "%", 0.f, 100.f);
		getParamQuantity(MIX_CV_PARAM)->randomizeEnabled = false;

		configInput(IN_INPUT, "Audio");
		configInput(MIX_INPUT, "Mix CV");
		configInput(MODE_INPUT, "Mode gate (Tronic to Verb)");

		configOutput(MIX_OUTPUT, "Mix (dry and wet)");
		configOutput(VERB_OUTPUT, "Verb (wet only)");

		configBypass(IN_INPUT, MIX_OUTPUT);

		lightDivider.setDivision(512);
		onSampleRateChange(SampleRateChangeEvent{APP->engine->getSampleRate(), APP->engine->getSampleTime()});
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		circuit.setSampleRate(e.sampleRate);
		smoothK = 1.0 - std::exp(-1.0 / (0.005 * (double)e.sampleRate));
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		circuit.reset();
	}

	void process(const ProcessArgs& args) override {
		modeGate.process(inputs[MODE_INPUT].getVoltage(), (float)assumed::GATE_LOW, (float)assumed::GATE_HIGH);

		fbS += smoothK * ((double)params[FEEDBACK_PARAM].getValue() - fbS);
		double tilt = 0.5 + 0.5 * (double)params[TILT_PARAM].getValue();     // 0 CCW .. 1 CW
		tiltS += smoothK * (tilt - tiltS);
		mixS += smoothK * ((double)params[MIX_PARAM].getValue() - mixS);
		attS += smoothK * (0.5 + 0.5 * (double)params[MIX_CV_PARAM].getValue() - attS);

		Verbtronic::In in;
		in.vin = clampd((double)inputs[IN_INPUT].getVoltageSum(), -12.0, 12.0);
		in.feedback = fbS;
		in.tilt = tiltCwBright ? 1.0 - tiltS : tiltS;
		in.mix = mixS;
		in.mixAtten = attS;
		in.mixCv = inputs[MIX_INPUT].getVoltageSum();
		// The toggle puts +V (Verb, "shorter") or ground (Tronic) on the DG202s'
		// logic line; the gate pulls it up, so it only ever takes Tronic to Verb.
		in.verb = params[MODE_PARAM].getValue() < 0.5f || modeGate.isHigh();

		Verbtronic::Out out;
		circuit.process(in, out);

		outputs[MIX_OUTPUT].setVoltage((float)out.mix);
		outputs[VERB_OUTPUT].setVoltage((float)out.verb);

		if (lightDivider.process()) {
			float dt = args.sampleTime * lightDivider.getDivision();
			float lim = (float)clampd(out.limiter, 0.0, 1.0);
			if (out.clipped)
				lim = 1.f;
			lights[LIMIT_LIGHT].setBrightnessSmooth(lim, dt);
			lights[TRONIC_LIGHT].setBrightness(in.verb ? 0.f : 1.f);

			dispTronic = !in.verb;
			dispDelayMs = (float)Verbtronic::delayMs(0, in.verb);
			dispClockMHz = (float)(Pt2399::clockHzFor(Verbtronic::pin6(0, in.verb)) * 1e-6);
			dispLimit = lim > 0.5f;
		}
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "tiltCwBright", json_boolean(tiltCwBright));
		return root;
	}

	void dataFromJson(json_t* root) override {
		json_t* j = json_object_get(root, "tiltCwBright");
		if (j) tiltCwBright = json_boolean_value(j);
	}
};


// ---------------------------------------------------------------------------
// Look and feel. The palette, the shared hardware and the entire silkscreen come
// from the generated headers -- see ../../panelkit/README.md.

typedef RoundLargeBlackKnob PrimaryKnob;
typedef RoundBlackKnob      PanelKnob;


// The read-out: which algorithm is live and what its clock is doing, and the
// three pots. The cells are the FIELD_* rectangles the spec cut the glass into
// (src/Amortization/Panel.hpp); the fields that take the mouse sit on the same
// rectangles. Numerals in DSEG7, whose cmap carries only [0-9 . : -], so the
// segment strings below stay inside that; words in the mono face.
struct AmortizationDisplay : LedDisplay {
	Amortization* module = NULL;

	/** A pot's field: its name small at the top, its value under it. */
	static void pot(NVGcontext* vg, const Rect& c, const char* tag, const std::string& value) {
		const panel::TextStyle TAG(panel::Face::Mono, 6.5f, panel::SAGE,
			NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE);
		const panel::TextStyle VAL(panel::Face::Mono, 9.f, panel::LIME,
			NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE);
		const float cx = c.pos.x + c.size.x * 0.5f;
		panel::text(vg, TAG, cx, c.pos.y + c.size.y * 0.40f, tag);
		panel::text(vg, VAL, cx, c.pos.y + c.size.y * 0.88f, value);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) {
			LedDisplay::drawLayer(args, layer);
			return;
		}
		NVGcontext* vg = args.vg;
		float delay = module ? module->dispDelayMs : 60.2f;
		float clock = module ? module->dispClockMHz : 11.4f;
		bool isTronic = module ? module->dispTronic : false;
		bool limit = module ? module->dispLimit : false;
		// The toggle at Tronic while Verb runs: the gate has pulled it down.
		bool gated = module && !isTronic
			&& module->params[Amortization::MODE_PARAM].getValue() > 0.5f;
		float fb = module ? module->params[Amortization::FEEDBACK_PARAM].getValue() : 0.5f;
		float tilt = module ? module->params[Amortization::TILT_PARAM].getValue() : 0.f;
		float mix = module ? module->params[Amortization::MIX_PARAM].getValue() : 0.5f;

		const Rect mode = panel::inGlass(panel::FIELD_MODE);
		const Rect fbC = panel::inGlass(panel::FIELD_FEEDBACK_VAL);
		const Rect mixC = panel::inGlass(panel::FIELD_MIX_VAL);
		const float left = fbC.pos.x + 3.f;
		const float right = mixC.pos.x + mixC.size.x - 3.f;
		const float row0 = mode.pos.y + mode.size.y * 0.72f;
		// The middle line has no field; it sits half way between the two that do.
		const float row1 = row0 + (fbC.pos.y - mode.pos.y) * 0.5f;
		const NVGcolor dim = panel::alpha(panel::LIME, 0.55f);

		const panel::TextStyle MODE(panel::Face::Mono, 10.f, panel::MINT,
			NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE, -0.5f);
		const panel::TextStyle TAG(panel::Face::Mono, 8.f, dim,
			NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
		const panel::TextStyle FLAG(panel::Face::Mono, 8.f, dim,
			NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE);
		const panel::TextStyle NOTE(panel::Face::Mono, 6.f, panel::CLAY,
			NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE);

		// Top line: the first chip's delay, and the algorithm -- a click on it
		// flips the MODE toggle. A small GATE under it while the gate holds Verb.
		std::string num = delay < 100.f ? string::f("%.1f", delay) : string::f("%.0f", delay);
		panel::segValue(vg, left, row0, 11.f, num, "ms", panel::LIME);
		panel::text(vg, MODE, mode.pos.x + mode.size.x - 3.f, row0, isTronic ? "TRONIC" : "VERB");
		if (gated)
			panel::text(vg, NOTE, mode.pos.x + mode.size.x - 3.f, mode.pos.y + mode.size.y - 0.5f, "GATE");

		// Middle line: that chip's PT2399 clock, and whether the limiter is working.
		float x = panel::text(vg, TAG, left, row1, "CLK");
		panel::segValue(vg, x + 2.5f, row1, 8.f, string::f("%.1f", clock), "MHz", dim);
		panel::text(vg, FLAG.inked(limit ? panel::LIME : dim), right, row1,
			limit ? "LIMIT" : "CLEAR");

		// Bottom line: the three pots, each one a field to hold and drag.
		pot(vg, fbC, "FEEDBACK", string::f("%.0f%%", fb * 100.f));
		pot(vg, panel::inGlass(panel::FIELD_TILT_VAL), "TILT", string::f("%+.0f%%", tilt * 100.f));
		pot(vg, mixC, "MIX", string::f("%.0f%%", mix * 100.f));
	}
};


struct AmortizationWidget : ModuleWidget {
	AmortizationWidget(Amortization* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Amortization.svg")));

		panel::addScrews(this);
		panel::addLabels(this);

		AmortizationDisplay* display = new AmortizationDisplay;
		display->module = module;
		display->box.pos = panel::mm(panel::GLASS_X, panel::GLASS_Y);
		display->box.size = panel::mm(panel::GLASS_W, panel::GLASS_H);
		addChild(display);

		// The read-out's fields, over the cells it draws them in. MODE has no
		// switch on the face any more; FEEDBACK, TILT and MIX keep their knobs.
		addParam(panel::createField<panel::ScreenSwitch>(panel::FIELD_MODE, module, Amortization::MODE_PARAM));
		addParam(panel::createField<panel::ScreenKnob>(panel::FIELD_FEEDBACK_VAL, module, Amortization::FEEDBACK_PARAM));
		addParam(panel::createField<panel::ScreenKnob>(panel::FIELD_TILT_VAL, module, Amortization::TILT_PARAM));
		addParam(panel::createField<panel::ScreenKnob>(panel::FIELD_MIX_VAL, module, Amortization::MIX_PARAM));

		addParam(createParamCentered<PrimaryKnob>(panel::mm(panel::FEEDBACK_POS.x, panel::FEEDBACK_POS.y), module, Amortization::FEEDBACK_PARAM));
		addChild(createLightCentered<SmallLight<panel::LimeLight> >(
		             panel::mm(panel::LIMIT_POS.x, panel::LIMIT_POS.y), module, Amortization::LIMIT_LIGHT));

		addParam(createParamCentered<PanelKnob>(panel::mm(panel::TILT_POS.x, panel::TILT_POS.y), module, Amortization::TILT_PARAM));
		addParam(createParamCentered<PanelKnob>(panel::mm(panel::MIX_POS.x, panel::MIX_POS.y), module, Amortization::MIX_PARAM));

		addParam(createParamCentered<Trimpot>(panel::mm(panel::MIX_CV_POS.x, panel::MIX_CV_POS.y), module, Amortization::MIX_CV_PARAM));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::MIX_IN_POS.x, panel::MIX_IN_POS.y), module, Amortization::MIX_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::MODE_IN_POS.x, panel::MODE_IN_POS.y), module, Amortization::MODE_INPUT));

		addInput(createInputCentered<panel::PortInMain>(panel::mm(panel::IN_POS.x, panel::IN_POS.y), module, Amortization::IN_INPUT));
		addOutput(createOutputCentered<panel::PortOutMain>(panel::mm(panel::MIX_OUT_POS.x, panel::MIX_OUT_POS.y), module, Amortization::MIX_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::VERB_OUT_POS.x, panel::VERB_OUT_POS.y), module, Amortization::VERB_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		Amortization* m = dynamic_cast<Amortization*>(module);
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Amortization"));

		menu->addChild(createIndexSubmenuItem("TILT, clockwise is",
			{"Dark (as the schematic draws the pot)", "Bright"},
			[=]() { return m->tiltCwBright ? 1 : 0; },
			[=](int v) { m->tiltCwBright = v != 0; }));
	}
};


Model* modelAmortization = createModel<Amortization, AmortizationWidget>("Amortization");

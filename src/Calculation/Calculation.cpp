#include "../plugin.hpp"
#include "Panel.hpp"
#include "Core.hpp"

using namespace calculation;


// The plate names a ratio in ASCII: the plate's face carries no × or ÷.
static std::string plateName(int step) {
	return step < DIVISORS ? string::f("/%d", stepDen(step)) : string::f("x%d", stepNum(step));
}

static std::vector<std::string> ratioLabels() {
	std::vector<std::string> v;
	for (int s = 0; s < STEPS; s++)
		v.push_back(s < DIVISORS ? string::f("÷%d", stepDen(s)) : string::f("×%d", stepNum(s)));
	return v;
}


struct Calculation : Module {
	enum ParamId {
		N_PARAM, N_DN_PARAM, N_UP_PARAM,
		START_PARAM, STOP_PARAM, RESET_PARAM,
		MULT_PARAM,
		PARAMS_LEN = MULT_PARAM + ROWS
	};
	enum InputId {
		CLOCK_INPUT, START_INPUT, STOP_INPUT, RESET_INPUT,
		CV_INPUT,
		INPUTS_LEN = CV_INPUT + ROWS
	};
	enum OutputId {
		RUN_OUTPUT,
		TRIG_OUTPUT,
		GATE_OUTPUT = TRIG_OUTPUT + ROWS,
		OUTPUTS_LEN = GATE_OUTPUT + ROWS
	};
	enum LightId {
		RUN_LIGHT,
		FIRE_LIGHT,
		LIGHTS_LEN = FIRE_LIGHT + ROWS
	};

	Phrase phrase;
	dsp::SchmittTrigger clockTrig, startTrig, stopTrig, resetTrig;
	dsp::BooleanTrigger startBtn, stopBtn, resetBtn, dnBtn, upBtn;
	dsp::PulseGenerator pulse[ROWS];
	dsp::ClockDivider uiDivider;
	bool firedSince[ROWS] = {};
	float fireLevel[ROWS] = {};

	// What the panel reads. Written on the audio thread, read by the widgets;
	// each is a word or a number the UI only ever draws.
	int len[ROWS] = {};
	long dispRemaining[ROWS] = {};
	std::string dispName[ROWS];
	const char* dispNamePtr[ROWS] = {};
	int dispN = 16;

	Calculation() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

		configParam(N_PARAM, 1.f, 9999.f, 16.f, "Steps (N)")->snapEnabled = true;
		configButton(N_DN_PARAM, "Steps -1");
		configButton(N_UP_PARAM, "Steps +1");
		configButton(START_PARAM, "Start");
		configButton(STOP_PARAM, "Stop (pause)");
		configButton(RESET_PARAM, "Reset");

		std::vector<std::string> labels = ratioLabels();
		for (int i = 0; i < ROWS; i++) {
			std::string n = std::to_string(i + 1);
			configSwitch(MULT_PARAM + i, 0.f, STEPS - 1, timesStep(i + 1),
				"Line " + n + " ratio of N", labels);
			configInput(CV_INPUT + i, "Line " + n + " ratio CV (1 V per step)");
			configOutput(TRIG_OUTPUT + i, "Line " + n + " trigger");
			configOutput(GATE_OUTPUT + i, "Line " + n + " gate");
			configLight(FIRE_LIGHT + i, "Line " + n + " fired");
			dispNamePtr[i] = NULL;
		}
		configInput(CLOCK_INPUT, "Clock");
		configInput(START_INPUT, "Start");
		configInput(STOP_INPUT, "Stop (pause)");
		configInput(RESET_INPUT, "Reset");
		configOutput(RUN_OUTPUT, "Running");
		configLight(RUN_LIGHT, "Running (dim: waiting for the downbeat)");

		uiDivider.setDivision(32);
		onSampleRateChange({APP->engine->getSampleRate(), 0.f});
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		// A millisecond: room for a few cables' delay between a TRIG and the
		// START it drives, and far shorter than any clock worth counting.
		phrase.window = std::max(1, (int)std::lround(e.sampleRate * 0.001f));
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		int window = phrase.window;
		phrase = Phrase();
		phrase.window = window;
		for (int i = 0; i < ROWS; i++) {
			pulse[i].reset();
			fireLevel[i] = 0.f;
		}
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "mode", json_integer(phrase.mode));
		json_object_set_new(root, "retrigger", json_boolean(phrase.retrigger));
		json_object_set_new(root, "running", json_boolean(phrase.running));
		json_object_set_new(root, "armed", json_boolean(phrase.armed));
		json_object_set_new(root, "finished", json_boolean(phrase.finished));
		json_object_set_new(root, "ticks", json_integer(phrase.ticks));
		json_t* rows = json_array();
		for (int i = 0; i < ROWS; i++) {
			json_t* r = json_object();
			json_object_set_new(r, "repeat", json_boolean(phrase.repeat[i]));
			json_object_set_new(r, "rowTicks", json_integer(phrase.rowTicks[i]));
			json_object_set_new(r, "done", json_boolean(phrase.done[i]));
			json_object_set_new(r, "gate", json_boolean(phrase.gate[i]));
			json_array_append_new(rows, r);
		}
		json_object_set_new(root, "rows", rows);
		return root;
	}

	void dataFromJson(json_t* root) override {
		if (json_t* j = json_object_get(root, "mode")) phrase.mode = (int)json_integer_value(j);
		if (json_t* j = json_object_get(root, "retrigger")) phrase.retrigger = json_boolean_value(j);
		if (json_t* j = json_object_get(root, "running")) phrase.running = json_boolean_value(j);
		if (json_t* j = json_object_get(root, "armed")) phrase.armed = json_boolean_value(j);
		if (json_t* j = json_object_get(root, "finished")) phrase.finished = json_boolean_value(j);
		if (json_t* j = json_object_get(root, "ticks")) phrase.ticks = (long)json_integer_value(j);
		if (json_t* rows = json_object_get(root, "rows")) {
			for (int i = 0; i < ROWS && i < (int)json_array_size(rows); i++) {
				json_t* r = json_array_get(rows, i);
				if (json_t* j = json_object_get(r, "repeat")) phrase.repeat[i] = json_boolean_value(j);
				if (json_t* j = json_object_get(r, "rowTicks")) phrase.rowTicks[i] = (int)json_integer_value(j);
				if (json_t* j = json_object_get(r, "done")) phrase.done[i] = json_boolean_value(j);
				if (json_t* j = json_object_get(r, "gate")) phrase.gate[i] = json_boolean_value(j);
			}
		}
	}

	void process(const ProcessArgs& args) override {
		// The -/+ buttons nudge N by one; the knob and typed entry do the rest.
		if (dnBtn.process(params[N_DN_PARAM].getValue() > 0.f))
			params[N_PARAM].setValue(std::max(1.f, params[N_PARAM].getValue() - 1.f));
		if (upBtn.process(params[N_UP_PARAM].getValue() > 0.f))
			params[N_PARAM].setValue(std::min(9999.f, params[N_PARAM].getValue() + 1.f));

		int n = (int)clamp(std::round(params[N_PARAM].getValue()), 1.f, 9999.f);
		int step[ROWS];
		for (int i = 0; i < ROWS; i++) {
			float s = params[MULT_PARAM + i].getValue() + inputs[CV_INPUT + i].getVoltage();
			step[i] = (int)clamp(std::round(s), 0.f, (float)(STEPS - 1));
			len[i] = rowLength(n, step[i]);
		}

		bool clock = clockTrig.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 1.f);
		// Jack and button are read separately so both triggers see every sample.
		bool startJ = startTrig.process(inputs[START_INPUT].getVoltage(), 0.1f, 1.f);
		bool startB = startBtn.process(params[START_PARAM].getValue() > 0.f);
		bool stopJ = stopTrig.process(inputs[STOP_INPUT].getVoltage(), 0.1f, 1.f);
		bool stopB = stopBtn.process(params[STOP_PARAM].getValue() > 0.f);
		bool rstJ = resetTrig.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f);
		bool rstB = resetBtn.process(params[RESET_PARAM].getValue() > 0.f);

		phrase.process(clock, startJ || startB, stopJ || stopB, rstJ || rstB, len);

		for (int i = 0; i < ROWS; i++) {
			if (phrase.fired[i]) {
				pulse[i].trigger(1e-3f);
				firedSince[i] = true;
			}
			outputs[TRIG_OUTPUT + i].setVoltage(pulse[i].process(args.sampleTime) ? 10.f : 0.f);
			outputs[GATE_OUTPUT + i].setVoltage(phrase.gate[i] ? 10.f : 0.f);
		}
		outputs[RUN_OUTPUT].setVoltage(phrase.running ? 10.f : 0.f);

		if (uiDivider.process()) {
			float dt = args.sampleTime * uiDivider.getDivision();
			lights[RUN_LIGHT].setBrightness(phrase.running ? 1.f : phrase.armed ? 0.3f : 0.f);
			for (int i = 0; i < ROWS; i++) {
				fireLevel[i] = firedSince[i] ? 1.f : fireLevel[i] * std::exp(-dt / 0.12f);
				firedSince[i] = false;
				lights[FIRE_LIGHT + i].setBrightness(fireLevel[i]);
				dispRemaining[i] = phrase.remaining(i, len[i]);
			}
			dispN = n;
		}
	}
};


// ---------------------------------------------------------------------------
// Look and feel comes entirely from src/PanelTheme.hpp and
// src/Calculation/Panel.hpp, generated by tools/panels/Calculation.py -- see
// ../../panelkit/README.md.

/** Typing a number for N: Rack's own right-click field, opened from the glass. */
struct StepsField : ui::TextField {
	Module* module = NULL;

	void step() override {
		APP->event->setSelectedWidget(this);
		TextField::step();
	}

	void onSelectKey(const SelectKeyEvent& e) override {
		if (e.action == GLFW_PRESS && (e.isKeyCommand(GLFW_KEY_ENTER) || e.isKeyCommand(GLFW_KEY_KP_ENTER))) {
			engine::ParamQuantity* pq = module->getParamQuantity(Calculation::N_PARAM);
			float oldValue = pq->getValue();
			pq->setDisplayValueString(text);
			float newValue = pq->getValue();
			if (oldValue != newValue) {
				history::ParamChange* h = new history::ParamChange;
				h->name = "set steps";
				h->moduleId = module->id;
				h->paramId = Calculation::N_PARAM;
				h->oldValue = oldValue;
				h->newValue = newValue;
				APP->history->push(h);
			}
			if (ui::MenuOverlay* overlay = getAncestorOfType<ui::MenuOverlay>())
				overlay->requestDelete();
			e.consume(this);
		}
		if (!e.getTarget())
			TextField::onSelectKey(e);
	}
};

/** The glass: N, large, and what the phrase is doing. Click it to type N. */
struct StepsDisplay : LedDisplay {
	Calculation* module = NULL;

	void onButton(const ButtonEvent& e) override {
		if (module && e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			ui::Menu* menu = createMenu();
			menu->addChild(createMenuLabel("Steps (1-9999), Enter to set"));
			StepsField* f = new StepsField;
			f->module = module;
			f->box.size.x = 120.f;
			f->text = module->getParamQuantity(Calculation::N_PARAM)->getDisplayValueString();
			f->selectAll();
			menu->addChild(f);
			e.consume(this);
			return;
		}
		LedDisplay::onButton(e);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) {
			LedDisplay::drawLayer(args, layer);
			return;
		}
		int n = module ? module->dispN : 16;
		const char* state = "READY";
		long ticks = 0;
		if (module) {
			const Phrase& e = module->phrase;
			ticks = e.ticks;
			state = e.running ? "RUN" : e.armed ? "ARMED" : e.finished ? "DONE" : e.ticks ? "HOLD" : "READY";
		}

		const float pad = 5.f;
		const float base = box.size.y * 0.5f + 5.f;
		const NVGcolor dim = panel::alpha(panel::LIME, 0.55f);
		const panel::TextStyle TAG(panel::Face::Mono, 8.f, dim,
			NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
		const panel::TextStyle STATE(panel::Face::Mono, 8.f, panel::MINT,
			NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE, 1.f);

		float x = panel::text(args.vg, TAG, pad, base, "N");
		panel::segValue(args.vg, x + 3.f, base, 12.f, string::f("%4d", n), "", panel::LIME);
		std::string right = module && (module->phrase.running || module->phrase.ticks)
			? string::f("%s %ld", state, ticks) : std::string(state);
		panel::text(args.vg, STATE, box.size.x - pad, base, right);
	}
};

/** A line's counter: clocks until it next falls due, in the segment face. */
struct DueDisplay : widget::Widget {
	const long* value = NULL;

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) return;
		std::string s = value ? std::to_string(*value) : std::string("--");
		float size = 7.f;
		panel::TextStyle seg(panel::Face::Seg, size, panel::LIME, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
		float w = panel::textWidth(args.vg, seg, s);
		float room = box.size.x - 2.f;
		if (w > room) {
			size *= room / w;
			w = room;
		}
		panel::segValue(args.vg, (box.size.x - w) / 2.f, box.size.y / 2.f + size / 2.f, size, s, "", panel::LIME);
	}
};

struct StepsKnob : RoundBlackKnob {
	// 9999 detents on one knob: about four pixels a step, so it nudges, and
	// the glass takes typed numbers for the long jumps.
	StepsKnob() { speed = 0.02f; }
};


struct CalculationWidget : ModuleWidget {
	CalculationWidget(Calculation* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Calculation.svg")));

		panel::addScrews(this);
		panel::addLabels(this);

		StepsDisplay* display = new StepsDisplay;
		display->module = module;
		display->box.pos = panel::mm(panel::GLASS_X, panel::GLASS_Y);
		display->box.size = panel::mm(panel::GLASS_W, panel::GLASS_H);
		addChild(display);

		addParam(createParamCentered<VCVButton>(panel::mm(panel::START_BTN_POS.x, panel::START_BTN_POS.y), module, Calculation::START_PARAM));
		addParam(createParamCentered<VCVButton>(panel::mm(panel::STOP_BTN_POS.x, panel::STOP_BTN_POS.y), module, Calculation::STOP_PARAM));
		addParam(createParamCentered<VCVButton>(panel::mm(panel::RESET_BTN_POS.x, panel::RESET_BTN_POS.y), module, Calculation::RESET_PARAM));
		addParam(createParamCentered<VCVButton>(panel::mm(panel::N_DN_POS.x, panel::N_DN_POS.y), module, Calculation::N_DN_PARAM));
		addParam(createParamCentered<StepsKnob>(panel::mm(panel::N_POS.x, panel::N_POS.y), module, Calculation::N_PARAM));
		addParam(createParamCentered<VCVButton>(panel::mm(panel::N_UP_POS.x, panel::N_UP_POS.y), module, Calculation::N_UP_PARAM));
		addChild(createLightCentered<SmallLight<panel::LimeLight> >(panel::mm(panel::RUN_LED_POS.x, panel::RUN_LED_POS.y), module, Calculation::RUN_LIGHT));

		// One table per column, so the widget cannot disagree with the enum
		// about which line is which.
		static const Vec* const MULT[ROWS] = { &panel::MULT1_POS, &panel::MULT2_POS, &panel::MULT3_POS, &panel::MULT4_POS, &panel::MULT5_POS, &panel::MULT6_POS };
		static const Vec* const NAME[ROWS] = { &panel::MULT1_NAME_POS, &panel::MULT2_NAME_POS, &panel::MULT3_NAME_POS, &panel::MULT4_NAME_POS, &panel::MULT5_NAME_POS, &panel::MULT6_NAME_POS };
		static const Vec* const CV[ROWS] = { &panel::CV1_POS, &panel::CV2_POS, &panel::CV3_POS, &panel::CV4_POS, &panel::CV5_POS, &panel::CV6_POS };
		static const Vec* const COUNT[ROWS] = { &panel::COUNT1_POS, &panel::COUNT2_POS, &panel::COUNT3_POS, &panel::COUNT4_POS, &panel::COUNT5_POS, &panel::COUNT6_POS };
		static const Vec* const TRIG[ROWS] = { &panel::TRIG1_POS, &panel::TRIG2_POS, &panel::TRIG3_POS, &panel::TRIG4_POS, &panel::TRIG5_POS, &panel::TRIG6_POS };
		static const Vec* const LED[ROWS] = { &panel::FIRE1_LED_POS, &panel::FIRE2_LED_POS, &panel::FIRE3_LED_POS, &panel::FIRE4_LED_POS, &panel::FIRE5_LED_POS, &panel::FIRE6_LED_POS };
		static const Vec* const GATE[ROWS] = { &panel::GATE1_POS, &panel::GATE2_POS, &panel::GATE3_POS, &panel::GATE4_POS, &panel::GATE5_POS, &panel::GATE6_POS };

		for (int i = 0; i < ROWS; i++) {
			addParam(createParamCentered<Trimpot>(panel::mm(MULT[i]->x, MULT[i]->y), module, Calculation::MULT_PARAM + i));

			panel::MiniDisplay* name = new panel::MiniDisplay;
			name->box.size = panel::mm(panel::READOUT_W, panel::READOUT_H);
			name->box.pos = panel::mm(NAME[i]->x, NAME[i]->y).minus(name->box.size.div(2.f));
			name->name = module ? &module->dispNamePtr[i] : NULL;
			addChild(name);

			addInput(createInputCentered<panel::PortIn>(panel::mm(CV[i]->x, CV[i]->y), module, Calculation::CV_INPUT + i));

			DueDisplay* due = new DueDisplay;
			due->box.size = panel::mm(panel::READOUT_W, panel::READOUT_H);
			due->box.pos = panel::mm(COUNT[i]->x, COUNT[i]->y).minus(due->box.size.div(2.f));
			due->value = module ? &module->dispRemaining[i] : NULL;
			addChild(due);

			addOutput(createOutputCentered<panel::PortTrigOut>(panel::mm(TRIG[i]->x, TRIG[i]->y), module, Calculation::TRIG_OUTPUT + i));
			addChild(createLightCentered<SmallLight<panel::MintLight> >(panel::mm(LED[i]->x, LED[i]->y), module, Calculation::FIRE_LIGHT + i));
			addOutput(createOutputCentered<panel::PortTrigOut>(panel::mm(GATE[i]->x, GATE[i]->y), module, Calculation::GATE_OUTPUT + i));
		}

		addInput(createInputCentered<panel::PortTrigIn>(panel::mm(panel::CLOCK_POS.x, panel::CLOCK_POS.y), module, Calculation::CLOCK_INPUT));
		addInput(createInputCentered<panel::PortTrigIn>(panel::mm(panel::START_POS.x, panel::START_POS.y), module, Calculation::START_INPUT));
		addInput(createInputCentered<panel::PortTrigIn>(panel::mm(panel::STOP_POS.x, panel::STOP_POS.y), module, Calculation::STOP_INPUT));
		addInput(createInputCentered<panel::PortTrigIn>(panel::mm(panel::RESET_POS.x, panel::RESET_POS.y), module, Calculation::RESET_INPUT));
		addOutput(createOutputCentered<panel::PortTrigOut>(panel::mm(panel::RUN_POS.x, panel::RUN_POS.y), module, Calculation::RUN_OUTPUT));
	}

	void step() override {
		// The ratio plates follow the knob plus its CV, worked out here on the
		// UI thread, which is the only one that touches the strings.
		if (Calculation* m = dynamic_cast<Calculation*>(module)) {
			for (int i = 0; i < ROWS; i++) {
				float s = m->params[Calculation::MULT_PARAM + i].getValue()
					+ m->inputs[Calculation::CV_INPUT + i].getVoltage();
				int st = (int)clamp(std::round(s), 0.f, (float)(STEPS - 1));
				std::string want = plateName(st);
				if (m->dispName[i] != want)
					m->dispName[i] = want;
				m->dispNamePtr[i] = m->dispName[i].c_str();
			}
		}
		ModuleWidget::step();
	}

	void appendContextMenu(Menu* menu) override {
		Calculation* m = dynamic_cast<Calculation*>(module);
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Calculation"));
		menu->addChild(createIndexSubmenuItem("Start",
			{"On the downbeat (a START up to 1 ms after a clock claims it)",
			 "Countdown-compatible (Count Modula Event Timer rules)"},
			[=]() { return m->phrase.mode; },
			[=](int v) { m->phrase.mode = v; }));
		menu->addChild(createBoolPtrMenuItem<bool>(
			"Retrigger (START while running restarts the phrase)", "", &m->phrase.retrigger));
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Repeat (fall due every N x ratio; GATE toggles)"));
		for (int i = 0; i < ROWS; i++)
			menu->addChild(createBoolPtrMenuItem<bool>(
				string::f("Line %d", i + 1), "", &m->phrase.repeat[i]));
	}
};


Model* modelCalculation = createModel<Calculation, CalculationWidget>("Calculation");

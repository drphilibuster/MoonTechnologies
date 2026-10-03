#include "../plugin.hpp"
#include "Panel.hpp"
#include "Core.hpp"

using namespace calculation;


// The glass names a ratio in ASCII: its face carries no × or ÷.
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
	int dispStep[ROWS] = {};
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
			dispStep[i] = timesStep(i + 1);
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
		// The -/+ buttons are gone from the face (N is a field on the glass now);
		// the params stay so a patch that saved them loads unchanged.
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
				dispStep[i] = step[i];
			}
			dispN = n;
		}
	}
};


// ---------------------------------------------------------------------------
// Look and feel comes entirely from src/PanelTheme.hpp and
// src/Calculation/Panel.hpp, generated by tools/panels/Calculation.py -- see
// ../../panelkit/README.md.

/** The glass is the worksheet, and every value on it is a control: N (hold and
    drag; right-click for the param menu, which takes a typed number), the
    transport words, and each line's ratio in the table below them. The cells are
    the FIELD_* rectangles the spec cut the glass into (src/Calculation/Panel.hpp),
    and the fields that take the mouse sit on the same rectangles. A line's count
    -- clocks until it next falls due -- is drawn in the cell beside its ratio and
    is not a control. */
struct WorksheetDisplay : LedDisplay {
	Calculation* module = NULL;
	panel::FittedText fitState;

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) {
			LedDisplay::drawLayer(args, layer);
			return;
		}
		NVGcontext* vg = args.vg;
		const float gap = panel::mm(0.5f, 0.f).x;
		const float pad = 2.f;

		// First line: N, and what the phrase is doing.
		int n = module ? module->dispN : 16;
		const char* state = "READY";
		long ticks = 0;
		bool running = false, armed = false;
		if (module) {
			const Phrase& e = module->phrase;
			ticks = e.ticks;
			running = e.running;
			armed = e.armed;
			state = e.running ? "RUN" : e.armed ? "ARMED" : e.finished ? "DONE" : e.ticks ? "HOLD" : "READY";
		}
		const Rect nf = panel::inGlass(panel::FIELD_N);
		const float nb = nf.pos.y + nf.size.y * 0.78f;
		const panel::TextStyle TAG(panel::Face::Mono, 7.f, panel::SAGE,
			NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
		float x = panel::text(vg, TAG, nf.pos.x + pad, nb, "N");
		panel::segValue(vg, x + 3.f, nb, 10.f, string::f("%4d", n), "", panel::LIME);

		const float sx = nf.pos.x + nf.size.x + gap;
		const float sr = box.size.x - panel::mm(0.8f, 0.f).x - pad;
		const panel::TextStyle STATE(panel::Face::Mono, 8.f, panel::MINT,
			NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE);
		std::string right = module && (running || ticks)
			? string::f("%s %ld", state, ticks) : std::string(state);
		panel::text(vg, STATE, sr, nb, fitState.get(vg, STATE, right, sr - sx));

		// Second line: the transport, a word each. START is lit while the phrase
		// runs (dim while it waits for the downbeat); a word brightens while held.
		auto word = [&](const Rect& f, const char* w, int param, NVGcolor ink) {
			const Rect c = panel::inGlass(f);
			if (module && module->params[param].getValue() > 0.f) ink = panel::PAPER;
			const panel::TextStyle W(panel::Face::Mono, 8.f, ink,
				NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE, 0.5f);
			panel::text(vg, W, c.pos.x + c.size.x * 0.5f, c.pos.y + c.size.y * 0.74f, w);
		};
		word(panel::FIELD_START_BTN, "START", Calculation::START_PARAM,
		     running ? panel::MINT : armed ? panel::alpha(panel::MINT, 0.6f) : panel::LIME);
		word(panel::FIELD_STOP_BTN, "STOP", Calculation::STOP_PARAM, panel::LIME);
		word(panel::FIELD_RESET_BTN, "RESET", Calculation::RESET_PARAM, panel::LIME);

		// The table: each line's number and ratio (knob plus CV), and beside it
		// the clocks until it falls due, in the segment face, shrunk to fit.
		static const Rect* const RATIO[ROWS] = {
			&panel::FIELD_MULT1, &panel::FIELD_MULT2, &panel::FIELD_MULT3,
			&panel::FIELD_MULT4, &panel::FIELD_MULT5, &panel::FIELD_MULT6 };
		const panel::TextStyle LINE(panel::Face::Mono, 7.f, panel::SAGE,
			NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
		const panel::TextStyle VAL(panel::Face::Mono, 8.5f, panel::LIME,
			NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE);
		for (int i = 0; i < ROWS; i++) {
			const Rect r = panel::inGlass(*RATIO[i]);
			const float base = r.pos.y + r.size.y * 0.74f;
			int st = module ? module->dispStep[i] : timesStep(i + 1);
			panel::text(vg, LINE, r.pos.x + pad, base, std::to_string(i + 1));
			panel::text(vg, VAL, r.pos.x + r.size.x - pad, base, plateName(st));

			std::string due = module ? std::to_string(module->dispRemaining[i]) : std::string("--");
			float size = 7.f;
			const panel::TextStyle seg(panel::Face::Seg, size, panel::LIME,
				NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
			float w = panel::textWidth(vg, seg, due);
			const float room = r.size.x - 2.f * pad;
			if (w > room) {
				size *= room / w;
				w = room;
			}
			const float dr = r.pos.x + 2.f * r.size.x + gap - pad;
			panel::segValue(vg, dr - w, base, size, due, "", panel::LIME);
		}
	}
};


struct CalculationWidget : ModuleWidget {
	CalculationWidget(Calculation* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Calculation.svg")));

		panel::addScrews(this);
		panel::addLabels(this);

		WorksheetDisplay* display = new WorksheetDisplay;
		display->module = module;
		display->box.pos = panel::mm(panel::GLASS_X, panel::GLASS_Y);
		display->box.size = panel::mm(panel::GLASS_W, panel::GLASS_H);
		addChild(display);

		// The worksheet's fields, over the cells it draws them in. N has 9999
		// steps, so it drags slowly -- about four pixels a step -- and takes
		// typed numbers from its right-click menu for the long jumps.
		panel::ScreenKnob* nField = panel::createField<panel::ScreenKnob>(
			panel::FIELD_N, module, Calculation::N_PARAM);
		nField->speed = 0.02f;
		addParam(nField);
		addParam(panel::createField<panel::ScreenButton>(panel::FIELD_START_BTN, module, Calculation::START_PARAM));
		addParam(panel::createField<panel::ScreenButton>(panel::FIELD_STOP_BTN, module, Calculation::STOP_PARAM));
		addParam(panel::createField<panel::ScreenButton>(panel::FIELD_RESET_BTN, module, Calculation::RESET_PARAM));
		static const Rect* const RATIO[ROWS] = {
			&panel::FIELD_MULT1, &panel::FIELD_MULT2, &panel::FIELD_MULT3,
			&panel::FIELD_MULT4, &panel::FIELD_MULT5, &panel::FIELD_MULT6 };
		for (int i = 0; i < ROWS; i++)
			addParam(panel::createField<panel::ScreenSelect>(*RATIO[i], module, Calculation::MULT_PARAM + i));

		// One table per column, so the widget cannot disagree with the enum
		// about which line is which.
		static const Vec* const CV[ROWS] = { &panel::CV1_POS, &panel::CV2_POS, &panel::CV3_POS, &panel::CV4_POS, &panel::CV5_POS, &panel::CV6_POS };
		static const Vec* const TRIG[ROWS] = { &panel::TRIG1_POS, &panel::TRIG2_POS, &panel::TRIG3_POS, &panel::TRIG4_POS, &panel::TRIG5_POS, &panel::TRIG6_POS };
		static const Vec* const LED[ROWS] = { &panel::FIRE1_LED_POS, &panel::FIRE2_LED_POS, &panel::FIRE3_LED_POS, &panel::FIRE4_LED_POS, &panel::FIRE5_LED_POS, &panel::FIRE6_LED_POS };
		static const Vec* const GATE[ROWS] = { &panel::GATE1_POS, &panel::GATE2_POS, &panel::GATE3_POS, &panel::GATE4_POS, &panel::GATE5_POS, &panel::GATE6_POS };

		for (int i = 0; i < ROWS; i++) {
			addInput(createInputCentered<panel::PortIn>(panel::mm(CV[i]->x, CV[i]->y), module, Calculation::CV_INPUT + i));

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

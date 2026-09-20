#include "../plugin.hpp"
#include "../Quantizer.hpp"
#include "Panel.hpp"

// Payment Schedule fuses three Modular in a Week Day 10 circuits into one
// counter: a Baby8 (a 4017 decade counter reading out eight CV/gate steps),
// the same counter read the other way as a sequential switch (a HEF4516 +
// CD4051 in the original, collapsed here into direct routing since the
// counter already lives in software), and a 4031-based tap looper sharing
// the same eight slots as the steps rather than a separately-sized buffer.
// The varimode quantizer (chromatic, major, minor and their pentatonics,
// shared with Dependents' root -- see ../Quantizer.hpp) sits on the CV path
// leaving the module. See docs/PaymentSchedule.md for the source schematics
// and exactly what was kept, changed or left out.

static const int NUM_STEPS = 8;

struct PaymentSchedule : Module {
	enum ParamId {
		ENUMS(STEP_PARAM, NUM_STEPS), ENUMS(GATE_PARAM, NUM_STEPS),
		DIR_PARAM, STEPS_PARAM, SCALE_PARAM, ROOT_PARAM,
		QUANT_PARAM, CV_RANGE_PARAM, ATTEN_PARAM,
		TAP_PARAM, RECORD_PARAM, CLEAR_PARAM, RUN_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		ENUMS(B_IN_INPUT, NUM_STEPS),
		CLOCK_INPUT, RESET_INPUT, DIR_CV_INPUT,
		TAP_GATE_INPUT, A_IN_INPUT,
		RUN_INPUT, CYCLE_TRIG_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		ENUMS(B_OUT_OUTPUT, NUM_STEPS),
		EOC_OUTPUT, LOOP_GATE_OUTPUT, TRIG_OUTPUT, A_OUT_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		ENUMS(GATE_LIGHT, NUM_STEPS),
		UP_LIGHT, DN_LIGHT, CLOCK_LIGHT, TAP_LIGHT, RECORD_LIGHT, CLEAR_LIGHT, RUN_LIGHT,
		LIGHTS_LEN
	};

	dsp::SchmittTrigger clockTrigger, resetTrigger, tapGateTrigger, runCvTrigger, cycleTrigTrigger;
	dsp::PulseGenerator eocPulse, trigOutPulse;
	dsp::ClockDivider lightDivider;

	int step = 0;
	bool running = true;       // RUN button / RUN CV toggle this; CYCLE sets it
	bool armCycleStop = false; // and arms an automatic stop at the next wrap
	bool runWasDown = false, tapWasDown = false, clearWasDown = false;
	float lastQuantVolt = 0.f;
	float heldCv = 0.f;

	// The tap loop shares the eight step slots rather than a buffer of its
	// own -- Payment Schedule used to let this run to 32 or 64 while STEPS
	// topped out at 8, which meant a "32-step loop" was really eight looping
	// slots played four times over with no way to see or reach the rest.
	// Tying it to the same counter is what actually fixes that, not a bigger
	// number.
	bool loopBuf[NUM_STEPS] = {};

	// Published for the light-bezel flashes; decayed in the light block.
	float tapFlash = 0.f, clearFlash = 0.f;

	PaymentSchedule() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

		for (int i = 0; i < NUM_STEPS; i++) {
			configParam(STEP_PARAM + i, 0.f, 1.f, 0.f,
			            string::f("Step %d level", i + 1), "%", 0.f, 100.f);
			configButton(GATE_PARAM + i, string::f("Step %d gate on/off", i + 1));
			getParamQuantity(GATE_PARAM + i)->defaultValue = 1.f;
			params[GATE_PARAM + i].setValue(1.f);
			configInput(B_IN_INPUT + i, string::f("Step %d CV in", i + 1));
			configOutput(B_OUT_OUTPUT + i, string::f("Step %d gate out", i + 1));
		}

		configSwitch(DIR_PARAM, 0.f, 1.f, 1.f, "Direction", {"Down", "Up"});
		configParam(STEPS_PARAM, 1.f, (float) NUM_STEPS, (float) NUM_STEPS, "Sequence length");
		getParamQuantity(STEPS_PARAM)->snapEnabled = true;

		std::vector<std::string> scaleNames;
		for (int i = 0; i < quant::NUM_SCALES; i++) scaleNames.push_back(quant::SCALE_NAMES[i]);
		configSwitch(SCALE_PARAM, 0.f, (float) (quant::NUM_SCALES - 1), 0.f, "Scale", scaleNames);
		configSwitch(ROOT_PARAM, 0.f, 11.f, 0.f, "Root",
		             {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"});
		configSwitch(QUANT_PARAM, 0.f, 1.f, 1.f, "Quantize", {"Off", "On"});
		configSwitch(CV_RANGE_PARAM, 0.f, 1.f, 0.f, "CV range", {"1 V", "5 V"});
		configParam(ATTEN_PARAM, -1.f, 1.f, 1.f, "Attenuverter", "%", 0.f, 100.f);

		configButton(TAP_PARAM, "Tap");
		configButton(RECORD_PARAM, "Record (overdub loop)");
		configButton(CLEAR_PARAM, "Clear loop");
		configButton(RUN_PARAM, "Run/stop (toggle)");

		configInput(CLOCK_INPUT, "Clock");
		configInput(RESET_INPUT, "Reset");
		configInput(DIR_CV_INPUT, "Direction CV");
		configInput(TAP_GATE_INPUT, "Tap gate");
		configInput(A_IN_INPUT, "Switch input (normalled to 10 V)");
		configInput(RUN_INPUT, "Run/stop (rising edge toggles)");
		configInput(CYCLE_TRIG_INPUT, "Run one cycle (trigger)");

		configOutput(EOC_OUTPUT, "End of cycle (fires on wrap)");
		configOutput(LOOP_GATE_OUTPUT, "Loop gate (tap-recorded pattern, current step)");
		configOutput(TRIG_OUTPUT, "Quantizer trigger (on note change)");
		configOutput(A_OUT_OUTPUT, "CV / V-oct out");

		lightDivider.setDivision(32);
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		step = 0;
		running = true;
		armCycleStop = false;
		for (int i = 0; i < NUM_STEPS; i++)
			loopBuf[i] = false;
		lastQuantVolt = 0.f;
		heldCv = 0.f;
	}

	int numSteps() {
		return clamp((int) std::round(params[STEPS_PARAM].getValue()), 1, NUM_STEPS);
	}

	bool directionUp() {
		bool up = params[DIR_PARAM].getValue() > 0.5f;
		if (inputs[DIR_CV_INPUT].isConnected() && inputs[DIR_CV_INPUT].getVoltage() >= 1.f)
			up = !up;
		return up;
	}

	void process(const ProcessArgs& args) override {
		int n = numSteps();

		// --- transport: the RUN button and RUN CV each flip running on their
		// own rising edge, so either one starts or stops the count; CYCLE
		// starts a single pass from step 1 and arms an automatic stop at the
		// next wrap, so patching this module's own EOC into the next one's
		// CYCLE chains a one-shot run from one into the other. -------------
		bool runDown = params[RUN_PARAM].getValue() > 0.5f;
		if (runDown && !runWasDown)
			running = !running;
		runWasDown = runDown;
		if (runCvTrigger.process(inputs[RUN_INPUT].getVoltage(), 0.1f, 2.f))
			running = !running;
		if (cycleTrigTrigger.process(inputs[CYCLE_TRIG_INPUT].getVoltage(), 0.1f, 2.f)) {
			running = true;
			armCycleStop = true;
			step = 0;
		}

		bool wrapped = false;
		if (clockTrigger.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 2.f) && running) {
			bool up = directionUp();
			int prev = step;
			step = up ? (step + 1) % n : (step - 1 + n) % n;
			wrapped = up ? (prev == n - 1) : (prev == 0);
		}
		if (wrapped && armCycleStop) {
			running = false;
			armCycleStop = false;
		}

		bool doReset = resetTrigger.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 2.f);
		if (doReset)
			step = 0;
		if (step >= n)
			step = 0;

		if (wrapped)
			eocPulse.trigger(1e-3f);
		outputs[EOC_OUTPUT].setVoltage(eocPulse.process(args.sampleTime) ? 10.f : 0.f);

		// --- the tap looper: a second gate track sharing the same eight
		// slots as the steps, its write head the same counter. -------------
		bool tapDown = params[TAP_PARAM].getValue() > 0.5f;
		bool tapButtonEdge = tapDown && !tapWasDown;
		bool tapGateEdge = tapGateTrigger.process(inputs[TAP_GATE_INPUT].getVoltage(), 0.1f, 2.f);
		bool tapEdge = tapButtonEdge || tapGateEdge;
		tapWasDown = tapDown;
		if (tapEdge)
			tapFlash = 1.f;

		bool clearDown = params[CLEAR_PARAM].getValue() > 0.5f;
		bool clearEdge = clearDown && !clearWasDown;
		clearWasDown = clearDown;
		if (clearEdge) {
			for (int i = 0; i < NUM_STEPS; i++)
				loopBuf[i] = false;
			clearFlash = 1.f;
		}

		bool recording = params[RECORD_PARAM].getValue() > 0.5f;
		if (recording && tapEdge)
			loopBuf[step] = true;   // overdub: taps OR into the pattern

		outputs[LOOP_GATE_OUTPUT].setVoltage(loopBuf[step] ? 10.f : 0.f);

		// --- the sequential switch: SWITCH IN routes to whichever step's
		// GATE OUT the count has landed on, and that step's CV IN (or its
		// knob) routes out CV OUT instead. A muted step (its GATE button
		// off) is a rest: GATE OUT stays low and CV OUT holds its last value
		// rather than jumping to silence. -----------------------------------
		float switchIn = inputs[A_IN_INPUT].isConnected() ? inputs[A_IN_INPUT].getVoltageSum() : 10.f;
		switchIn = clamp(switchIn, -12.f, 12.f);

		float rangeVolts[2] = {1.f, 5.f};
		float range = rangeVolts[clamp((int) std::round(params[CV_RANGE_PARAM].getValue()), 0, 1)];
		float atten = params[ATTEN_PARAM].getValue();

		bool quantOn = params[QUANT_PARAM].getValue() > 0.5f;
		int scaleIdx = (int) std::round(params[SCALE_PARAM].getValue());
		int rootSemi = (int) std::round(params[ROOT_PARAM].getValue());

		for (int i = 0; i < NUM_STEPS; i++) {
			bool active = (i == step);
			bool gateOn = params[GATE_PARAM + i].getValue() > 0.5f;
			outputs[B_OUT_OUTPUT + i].setVoltage((active && gateOn) ? switchIn : 0.f);
			lights[GATE_LIGHT + i].setBrightness(!gateOn ? 0.f : (active ? 1.f : 0.28f));
		}

		if (step < NUM_STEPS && params[GATE_PARAM + step].getValue() > 0.5f) {
			float raw = inputs[B_IN_INPUT + step].isConnected()
			                ? inputs[B_IN_INPUT + step].getVoltageSum()
			                : params[STEP_PARAM + step].getValue() * range;
			raw *= atten;
			float out = quantOn ? quant::quantize(raw, scaleIdx, rootSemi) : raw;
			heldCv = out;
		}
		outputs[A_OUT_OUTPUT].setVoltage(clamp(heldCv, -12.f, 12.f));

		if (std::fabs(heldCv - lastQuantVolt) > 1e-4f) {
			trigOutPulse.trigger(1e-3f);
			lastQuantVolt = heldCv;
		}
		outputs[TRIG_OUTPUT].setVoltage(trigOutPulse.process(args.sampleTime) ? 10.f : 0.f);

		// --- lights, at a fraction of sample rate -------------------------
		if (lightDivider.process()) {
			float dt = args.sampleTime * lightDivider.getDivision();
			bool up = directionUp();
			lights[UP_LIGHT].setBrightness(up ? 1.f : 0.f);
			lights[DN_LIGHT].setBrightness(up ? 0.f : 1.f);
			lights[CLOCK_LIGHT].setBrightnessSmooth(
			    inputs[CLOCK_INPUT].getVoltage() >= 1.f ? 1.f : 0.f, dt);
			tapFlash = std::max(0.f, tapFlash - dt * 4.f);
			clearFlash = std::max(0.f, clearFlash - dt * 4.f);
			lights[TAP_LIGHT].setBrightness(tapFlash);
			lights[RECORD_LIGHT].setBrightness(recording ? 1.f : 0.f);
			lights[CLEAR_LIGHT].setBrightness(clearFlash);
			lights[RUN_LIGHT].setBrightness(running ? 1.f : 0.f);
		}
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "step", json_integer(step));
		json_object_set_new(root, "running", json_boolean(running));
		json_object_set_new(root, "armCycleStop", json_boolean(armCycleStop));
		json_t* buf = json_array();
		for (int i = 0; i < NUM_STEPS; i++)
			json_array_append_new(buf, json_boolean(loopBuf[i]));
		json_object_set_new(root, "loopBuf", buf);
		return root;
	}

	void dataFromJson(json_t* root) override {
		json_t* j;
		if ((j = json_object_get(root, "step")))
			step = clamp((int) json_integer_value(j), 0, NUM_STEPS - 1);
		if ((j = json_object_get(root, "running")))
			running = json_boolean_value(j);
		if ((j = json_object_get(root, "armCycleStop")))
			armCycleStop = json_boolean_value(j);
		if ((j = json_object_get(root, "loopBuf")) && json_is_array(j)) {
			size_t n = json_array_size(j);
			for (size_t i = 0; i < n && i < (size_t) NUM_STEPS; i++)
				loopBuf[i] = json_boolean_value(json_array_get(j, i));
		}
	}
};


// ---------------------------------------------------------------------------
// Look and feel comes entirely from src/PanelTheme.hpp and this panel's own
// Panel.hpp, generated by tools/panels/PaymentSchedule.py -- see
// ../../panelkit/README.md. Nothing about the layout is written here.

struct PaymentScheduleWidget : ModuleWidget {
	PaymentScheduleWidget(PaymentSchedule* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/PaymentSchedule.svg")));

		panel::addScrews(this);
		panel::addLabels(this);

		static const Vec* const B_IN_POS[NUM_STEPS] = {
			&panel::B_IN1_POS, &panel::B_IN2_POS, &panel::B_IN3_POS, &panel::B_IN4_POS,
			&panel::B_IN5_POS, &panel::B_IN6_POS, &panel::B_IN7_POS, &panel::B_IN8_POS,
		};
		static const Vec* const STEP_POS[NUM_STEPS] = {
			&panel::STEP1_POS, &panel::STEP2_POS, &panel::STEP3_POS, &panel::STEP4_POS,
			&panel::STEP5_POS, &panel::STEP6_POS, &panel::STEP7_POS, &panel::STEP8_POS,
		};
		static const Vec* const GATE_POS[NUM_STEPS] = {
			&panel::GATE1_POS, &panel::GATE2_POS, &panel::GATE3_POS, &panel::GATE4_POS,
			&panel::GATE5_POS, &panel::GATE6_POS, &panel::GATE7_POS, &panel::GATE8_POS,
		};
		static const Vec* const B_OUT_POS[NUM_STEPS] = {
			&panel::B_OUT1_POS, &panel::B_OUT2_POS, &panel::B_OUT3_POS, &panel::B_OUT4_POS,
			&panel::B_OUT5_POS, &panel::B_OUT6_POS, &panel::B_OUT7_POS, &panel::B_OUT8_POS,
		};

		for (int i = 0; i < NUM_STEPS; i++) {
			addInput(createInputCentered<panel::PortIn>(panel::mm(B_IN_POS[i]->x, B_IN_POS[i]->y),
				module, PaymentSchedule::B_IN_INPUT + i));
			addParam(createParamCentered<RoundBlackKnob>(panel::mm(STEP_POS[i]->x, STEP_POS[i]->y),
				module, PaymentSchedule::STEP_PARAM + i));
			addParam(createLightParamCentered<VCVLightBezelLatch<panel::LimeLight> >(
				panel::mm(GATE_POS[i]->x, GATE_POS[i]->y),
				module, PaymentSchedule::GATE_PARAM + i, PaymentSchedule::GATE_LIGHT + i));
			addOutput(createOutputCentered<panel::PortOut>(panel::mm(B_OUT_POS[i]->x, B_OUT_POS[i]->y),
				module, PaymentSchedule::B_OUT_OUTPUT + i));
		}

		addChild(createLightCentered<SmallLight<panel::LimeLight> >(
			panel::mm(panel::UP_LIT_POS.x, panel::UP_LIT_POS.y), module, PaymentSchedule::UP_LIGHT));
		addParam(createParamCentered<CKSS>(
			panel::mm(panel::DIR_POS.x, panel::DIR_POS.y), module, PaymentSchedule::DIR_PARAM));
		addChild(createLightCentered<SmallLight<panel::LimeLight> >(
			panel::mm(panel::DN_LIT_POS.x, panel::DN_LIT_POS.y), module, PaymentSchedule::DN_LIGHT));

		addParam(createParamCentered<RoundBlackKnob>(
			panel::mm(panel::STEPS_POS.x, panel::STEPS_POS.y), module, PaymentSchedule::STEPS_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(
			panel::mm(panel::SCALE_POS.x, panel::SCALE_POS.y), module, PaymentSchedule::SCALE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(
			panel::mm(panel::ROOT_POS.x, panel::ROOT_POS.y), module, PaymentSchedule::ROOT_PARAM));

		addParam(createParamCentered<CKSS>(
			panel::mm(panel::QUANT_POS.x, panel::QUANT_POS.y), module, PaymentSchedule::QUANT_PARAM));
		addParam(createParamCentered<CKSS>(
			panel::mm(panel::CV_RANGE_POS.x, panel::CV_RANGE_POS.y), module, PaymentSchedule::CV_RANGE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(
			panel::mm(panel::ATTEN_POS.x, panel::ATTEN_POS.y), module, PaymentSchedule::ATTEN_PARAM));

		addParam(createLightParamCentered<VCVLightBezel<panel::LimeLight> >(
			panel::mm(panel::TAP_POS.x, panel::TAP_POS.y),
			module, PaymentSchedule::TAP_PARAM, PaymentSchedule::TAP_LIGHT));
		addParam(createLightParamCentered<VCVLightBezelLatch<panel::LimeLight> >(
			panel::mm(panel::RECORD_POS.x, panel::RECORD_POS.y),
			module, PaymentSchedule::RECORD_PARAM, PaymentSchedule::RECORD_LIGHT));
		addParam(createLightParamCentered<VCVLightBezel<panel::ClayLight> >(
			panel::mm(panel::CLEAR_POS.x, panel::CLEAR_POS.y),
			module, PaymentSchedule::CLEAR_PARAM, PaymentSchedule::CLEAR_LIGHT));
		addParam(createLightParamCentered<VCVLightBezel<panel::LimeLight> >(
			panel::mm(panel::RUN_POS.x, panel::RUN_POS.y),
			module, PaymentSchedule::RUN_PARAM, PaymentSchedule::RUN_LIGHT));

		addInput(createInputCentered<panel::PortIn>(
			panel::mm(panel::DIR_CV_IN_POS.x, panel::DIR_CV_IN_POS.y), module, PaymentSchedule::DIR_CV_INPUT));
		addInput(createInputCentered<panel::PortIn>(
			panel::mm(panel::TAP_GATE_IN_POS.x, panel::TAP_GATE_IN_POS.y), module, PaymentSchedule::TAP_GATE_INPUT));
		addInput(createInputCentered<panel::PortIn>(
			panel::mm(panel::RUN_IN_POS.x, panel::RUN_IN_POS.y), module, PaymentSchedule::RUN_INPUT));
		addInput(createInputCentered<panel::PortIn>(
			panel::mm(panel::CYCLE_IN_POS.x, panel::CYCLE_IN_POS.y), module, PaymentSchedule::CYCLE_TRIG_INPUT));
		addOutput(createOutputCentered<panel::PortOut>(
			panel::mm(panel::LOOP_GATE_OUT_POS.x, panel::LOOP_GATE_OUT_POS.y), module, PaymentSchedule::LOOP_GATE_OUTPUT));

		addInput(createInputCentered<panel::PortIn>(
			panel::mm(panel::CLOCK_IN_POS.x, panel::CLOCK_IN_POS.y), module, PaymentSchedule::CLOCK_INPUT));
		addInput(createInputCentered<panel::PortIn>(
			panel::mm(panel::RESET_IN_POS.x, panel::RESET_IN_POS.y), module, PaymentSchedule::RESET_INPUT));
		addChild(createLightCentered<SmallLight<panel::MintLight> >(
			panel::mm(panel::CLOCK_LIT_POS.x, panel::CLOCK_LIT_POS.y), module, PaymentSchedule::CLOCK_LIGHT));
		addInput(createInputCentered<panel::PortIn>(
			panel::mm(panel::A_IN_POS.x, panel::A_IN_POS.y), module, PaymentSchedule::A_IN_INPUT));
		addOutput(createOutputCentered<panel::PortOut>(
			panel::mm(panel::A_OUT_POS.x, panel::A_OUT_POS.y), module, PaymentSchedule::A_OUT_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(
			panel::mm(panel::TRIG_OUT_POS.x, panel::TRIG_OUT_POS.y), module, PaymentSchedule::TRIG_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(
			panel::mm(panel::EOC_OUT_POS.x, panel::EOC_OUT_POS.y), module, PaymentSchedule::EOC_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		PaymentSchedule* m = dynamic_cast<PaymentSchedule*>(module);
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Payment Schedule"));
		menu->addChild(createMenuItem("Reset all step gates on", "", [=]() {
			for (int i = 0; i < NUM_STEPS; i++)
				m->params[PaymentSchedule::GATE_PARAM + i].setValue(1.f);
		}));
	}
};


Model* modelPaymentSchedule = createModel<PaymentSchedule, PaymentScheduleWidget>("PaymentSchedule");

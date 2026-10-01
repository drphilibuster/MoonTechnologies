#include "../plugin.hpp"
#include "../Quantizer.hpp"
#include "Panel.hpp"
#include "Sequencer.hpp"
#include "Varimode.hpp"
#include "VarimodeFirmware.hpp"

#include <osdialog.h>

#include <atomic>
#include <fstream>
#include <memory>
#include <sstream>

// Payment Schedule fuses three Modular in a Week Day 10 circuits onto one clock:
// a Baby8 (a 74HC4017 decade counter reading out eight CV/gate steps), the same
// counter read the other way as a sequential switch (a HEF4516 + CD4051 in the
// original, collapsed here into direct routing since the counter already lives in
// software), and a 4031 tap recorder/looper (a 64-stage CD4031B recirculating
// through a diode OR). The counter and the loop are the chips, in Sequencer.hpp,
// where they can be tested; this file keeps the jacks, the quantizer and the
// panel. The varimode quantizer is a PIC16F684 running firmware (Varimode.hpp,
// Pic16f684.hpp): this project's own, embedded, or any other that keeps the board's pin
// contract, loaded from the context menu. It sits on the CV path leaving the module. See docs/PaymentSchedule.md for the source schematics and
// exactly what was kept, changed or left out.

static const int NUM_STEPS = paysched::kSteps;

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

	paysched::Sequencer seq;   // the 74HC4017 counter, the CD4031B loop, RUN
	bool runWasDown = false, tapWasDown = false;
	float lastQuantVolt = 0.f;
	float heldRaw = 0.f;       // the CV the quantizer is looking at: the last live step's, after ATTEN

	// The quantizer: a PIC16F684 and its firmware. The audio thread owns `pic`; the UI thread
	// builds a replacement and hands it over, so loading a file never stalls the engine.
	std::unique_ptr<paysched::varimode::Varimode> pic;
	std::atomic<paysched::varimode::Varimode*> handover{nullptr};
	std::atomic<bool> powerCycle{false};
	std::atomic<bool> firmwareLeftModel{false};
	double picRate = 0.0;
	std::string firmwarePath;                  // empty: the built-in firmware. UI thread only.
	std::string firmwareStatus = "built-in firmware";   // ditto

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
		configOutput(LOOP_GATE_OUTPUT, "Loop gate (the 4031 Q: tap-recorded pattern, 64 clocks long)");
		configOutput(TRIG_OUTPUT, "Quantizer trigger (on note change)");
		configOutput(A_OUT_OUTPUT, "CV / V-oct out");

		lightDivider.setDivision(32);
		pic.reset(newBuiltIn());
	}

	~PaymentSchedule() { delete handover.exchange(nullptr); }

	static paysched::varimode::Varimode* newBuiltIn() {
		paysched::varimode::Varimode* v = new paysched::varimode::Varimode();
		v->loadWords(paysched::firmware::kVarimodeFixed, paysched::firmware::kVarimodeFixedWords);
		return v;
	}

	/** Loads the firmware from a .HEX the player chose. UI thread. */
	bool loadFirmwareFile(const std::string& path) {
		std::ifstream f(path);
		if (!f) { firmwareStatus = "cannot open " + system::getFilename(path); return false; }
		std::stringstream ss;
		ss << f.rdbuf();
		std::unique_ptr<paysched::varimode::Varimode> v(new paysched::varimode::Varimode());
		if (!v->loadHex(ss.str())) { firmwareStatus = system::getFilename(path) + " is not a valid Intel HEX file"; return false; }
		delete handover.exchange(v.release());
		firmwarePath = path;
		firmwareStatus = "custom firmware: " + system::getFilename(path);
		firmwareLeftModel = false;
		return true;
	}

	void useBuiltInFirmware() {
		delete handover.exchange(newBuiltIn());
		firmwarePath.clear();
		firmwareStatus = "built-in firmware";
		firmwareLeftModel = false;
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		seq.reset();
		lastQuantVolt = 0.f;
		heldRaw = 0.f;
		powerCycle = true;
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
			seq.running = !seq.running;
		runWasDown = runDown;
		if (runCvTrigger.process(inputs[RUN_INPUT].getVoltage(), 0.1f, 2.f))
			seq.running = !seq.running;
		if (cycleTrigTrigger.process(inputs[CYCLE_TRIG_INPUT].getVoltage(), 0.1f, 2.f))
			seq.startCycle();

		// --- the chips. The counter sees the clock and RESET as levels, as the
		// pins do; the loop sees the tap as a level too, read when the clock
		// rises. RECORD gates the tap (the hardware's tap is always live), and
		// CLEAR, held, opens the loop. -----------------------------------------
		clockTrigger.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 2.f);
		resetTrigger.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 2.f);
		bool tapDown = params[TAP_PARAM].getValue() > 0.5f;
		bool tapButtonEdge = tapDown && !tapWasDown;
		tapWasDown = tapDown;
		bool tapGateEdge = tapGateTrigger.process(inputs[TAP_GATE_INPUT].getVoltage(), 0.1f, 2.f);
		if (tapButtonEdge || tapGateEdge)
			tapFlash = 1.f;
		bool recording = params[RECORD_PARAM].getValue() > 0.5f;
		bool tapHigh = recording && (tapDown || tapGateTrigger.isHigh());
		bool clearHeld = params[CLEAR_PARAM].getValue() > 0.5f;
		if (clearHeld)
			clearFlash = 1.f;

		bool wrapped = seq.process(clockTrigger.isHigh(), resetTrigger.isHigh(),
		                           directionUp(), n, tapHigh, clearHeld);
		int step = seq.step();

		if (wrapped)
			eocPulse.trigger(1e-3f);
		outputs[EOC_OUTPUT].setVoltage(eocPulse.process(args.sampleTime) ? 10.f : 0.f);

		outputs[LOOP_GATE_OUTPUT].setVoltage(seq.loopGate() ? 10.f : 0.f);

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
			heldRaw = raw * atten;
		}

		// The quantizer chip. A new firmware, a power cycle and a sample-rate change are all
		// taken here, on the audio thread, which is the one that owns the chip.
		if (paysched::varimode::Varimode* fresh = handover.exchange(nullptr)) {
			pic.reset(fresh);
			picRate = 0.0;
		}
		if (picRate != (double) args.sampleRate) {
			pic->setSampleRate((double) args.sampleRate);
			picRate = (double) args.sampleRate;
		}
		if (powerCycle.exchange(false))
			pic->powerOn();

		float cvOut;
		bool noteChanged;
		if (quantOn) {
			// ROOT is this module's addition: the board has none, so the scale is rooted by
			// moving the input down and the output back up by the same amount.
			float rootV = (float) rootSemi / 12.f;
			static const paysched::varimode::Mode modeOf[quant::NUM_SCALES] = {
				paysched::varimode::CHROMATIC, paysched::varimode::MAJOR, paysched::varimode::MINOR,
				paysched::varimode::MAJOR_PENT, paysched::varimode::MINOR_PENT };
			cvOut = (float) pic->process((double) (heldRaw - rootV),
			                             modeOf[clamp(scaleIdx, 0, quant::NUM_SCALES - 1)]) + rootV;
			noteChanged = pic->noteChanged();
		}
		else {
			cvOut = heldRaw;
			noteChanged = std::fabs(cvOut - lastQuantVolt) > 1e-4f;
			lastQuantVolt = cvOut;
		}
		outputs[A_OUT_OUTPUT].setVoltage(clamp(cvOut, -12.f, 12.f));

		if (noteChanged)
			trigOutPulse.trigger(1e-3f);
		outputs[TRIG_OUTPUT].setVoltage(trigOutPulse.process(args.sampleTime) ? 10.f : 0.f);

		// --- lights, at a fraction of sample rate -------------------------
		if (lightDivider.process()) {
			firmwareLeftModel = pic->unsupported();
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
			lights[RUN_LIGHT].setBrightness(seq.running ? 1.f : 0.f);
		}
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "step", json_integer(seq.step()));
		json_object_set_new(root, "running", json_boolean(seq.running));
		json_object_set_new(root, "armCycleStop", json_boolean(seq.armCycleStop));
		// The 4031's 64 stages as one number, stage 1 in bit 0 (a bit pattern, so the
		// sign of the integer means nothing).
		json_object_set_new(root, "loopStages", json_integer((json_int_t) seq.loop.stages));
		// The firmware, if it is not the built-in one: a path, never the image.
		json_object_set_new(root, "firmwarePath", json_string(firmwarePath.c_str()));
		return root;
	}

	void dataFromJson(json_t* root) override {
		json_t* j;
		if ((j = json_object_get(root, "step")))
			seq.counter.setIndex(clamp((int) json_integer_value(j), 0, NUM_STEPS - 1));
		if ((j = json_object_get(root, "running")))
			seq.running = json_boolean_value(j);
		if ((j = json_object_get(root, "armCycleStop")))
			seq.armCycleStop = json_boolean_value(j);
		if ((j = json_object_get(root, "firmwarePath")) && json_is_string(j) && *json_string_value(j))
			loadFirmwareFile(json_string_value(j));
		if ((j = json_object_get(root, "loopStages"))) {
			seq.loop.stages = (uint64_t) json_integer_value(j);
		}
		else if ((j = json_object_get(root, "loopBuf")) && json_is_array(j)) {
			// A patch from before the loop was a 64-stage register: eight slots, one
			// per step. Tile them along the register so the gates land where they were.
			bool slots[NUM_STEPS] = {};
			size_t n = json_array_size(j);
			for (size_t i = 0; i < n && i < (size_t) NUM_STEPS; i++)
				slots[i] = json_boolean_value(json_array_get(j, i));
			seq.importSlots(slots, numSteps(), seq.step());
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

		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Quantizer: PIC16F684"));
		menu->addChild(createMenuLabel(m->firmwareStatus));
		if (m->firmwareLeftModel)
			menu->addChild(createMenuLabel("(the firmware used something the emulator does not model)"));
		menu->addChild(createMenuItem("Load PIC firmware (.HEX)...", "", [=]() {
			char* p = osdialog_file(OSDIALOG_OPEN, NULL, NULL, NULL);
			if (p) {
				m->loadFirmwareFile(p);
				std::free(p);
			}
		}));
		menu->addChild(createMenuItem("Use the built-in firmware", "", [=]() { m->useBuiltInFirmware(); }));
	}
};


Model* modelPaymentSchedule = createModel<PaymentSchedule, PaymentScheduleWidget>("PaymentSchedule");

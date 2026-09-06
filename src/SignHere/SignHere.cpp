#include "../plugin.hpp"
#include "Panel.hpp"
#include <atomic>

// Sign Here fuses three Modular in a Week Day 11 controllers into one panel:
// the offset-scaler joystick, four touch pads built from the same touch-drum
// circuit, and the button-and-pedal. None of the three source circuits are
// audio- or CV-processing DSP -- they are all "read a gesture, output a
// voltage" -- so unlike the family's other modules the interesting code here
// is in the widgets, not in process(). See docs/SignHere.md for what each
// circuit actually did and what changed.

static const int NUM_PADS = 4;


struct SignHere : Module {
	enum ParamId {
		X_SCALE_PARAM, X_OFFSET_PARAM, Y_SCALE_PARAM, Y_OFFSET_PARAM,
		GLIDE_PARAM, AMOUNT_PARAM,
		BUTTON_PARAM, LEVEL_PARAM, INV_PARAM,
		X_POS_PARAM, Y_POS_PARAM,   // hidden: the joystick's own position
		PARAMS_LEN
	};
	enum InputId {
		UTIL_IN_INPUT, PEDAL_IN_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		X_CV_OUT_OUTPUT, Y_CV_OUT_OUTPUT, TOUCH_GATE_OUT_OUTPUT, UTIL_OUT_OUTPUT,
		BTN_GATE_OUT_OUTPUT, BTN_TRIG_OUT_OUTPUT, BTN_FLIP_OUT_OUTPUT, BTN_CV_OUT_OUTPUT,
		ENUMS(PAD_GATE_OUTPUT, NUM_PADS), ENUMS(PAD_TRIG_OUTPUT, NUM_PADS),
		ENUMS(PAD_CV_OUTPUT, NUM_PADS),
		OUTPUTS_LEN
	};
	enum LightId {
		BUTTON_LIGHT,
		LIGHTS_LEN
	};

	// Written by the JoystickPad widget (the UI thread), read here -- the same
	// contract every stock ParamWidget already has with a param's value, which
	// is what X_POS_PARAM/Y_POS_PARAM actually are. `touching` and the pads'
	// press edges cross threads the other way, so those are atomic.
	std::atomic<bool> joyTouching{false};
	std::atomic<bool> padTouching[NUM_PADS];
	std::atomic<bool> padPressEdge[NUM_PADS];

	bool springReturn = false;
	bool flipState = false;
	bool wasPressed = false;

	float xSmoothed = 0.f, ySmoothed = 0.f;
	float padPressure[NUM_PADS] = {};
	dsp::PulseGenerator btnTrigPulse;
	dsp::PulseGenerator padTrigPulse[NUM_PADS];

	SignHere() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

		configParam(X_SCALE_PARAM, 0.f, 2.f, 1.f, "X scale", "%", 0.f, 100.f);
		configParam(X_OFFSET_PARAM, -5.f, 5.f, 0.f, "X offset", " V");
		configParam(Y_SCALE_PARAM, 0.f, 2.f, 1.f, "Y scale", "%", 0.f, 100.f);
		configParam(Y_OFFSET_PARAM, -5.f, 5.f, 0.f, "Y offset", " V");
		configParam(GLIDE_PARAM, 0.f, 500.f, 0.f, "Glide", " ms");
		configParam(AMOUNT_PARAM, -1.f, 1.f, 0.f, "In-to-out amount", "%", 0.f, 100.f);

		configButton(BUTTON_PARAM, "Button");
		configParam(LEVEL_PARAM, 0.f, 10.f, 10.f, "CV level", " V");
		configSwitch(INV_PARAM, 0.f, 1.f, 0.f, "Polarity", {"Non-inverted", "Inverted"});

		configParam(X_POS_PARAM, -1.f, 1.f, 0.f, "Joystick X position");
		getParamQuantity(X_POS_PARAM)->randomizeEnabled = false;
		configParam(Y_POS_PARAM, -1.f, 1.f, 0.f, "Joystick Y position");
		getParamQuantity(Y_POS_PARAM)->randomizeEnabled = false;

		configInput(UTIL_IN_INPUT, "Utility in");
		configInput(PEDAL_IN_INPUT, "Pedal gate");

		configOutput(X_CV_OUT_OUTPUT, "Joystick X CV");
		configOutput(Y_CV_OUT_OUTPUT, "Joystick Y CV");
		configOutput(TOUCH_GATE_OUT_OUTPUT, "Joystick touch gate");
		configOutput(UTIL_OUT_OUTPUT, "Utility out (in x amount)");
		configOutput(BTN_GATE_OUT_OUTPUT, "Button gate");
		configOutput(BTN_TRIG_OUT_OUTPUT, "Button trigger");
		configOutput(BTN_FLIP_OUT_OUTPUT, "Button flip (latch)");
		configOutput(BTN_CV_OUT_OUTPUT, "Button CV");
		for (int i = 0; i < NUM_PADS; i++) {
			configOutput(PAD_GATE_OUTPUT + i, string::f("Pad %d gate", i + 1));
			configOutput(PAD_TRIG_OUTPUT + i, string::f("Pad %d trigger", i + 1));
			configOutput(PAD_CV_OUTPUT + i, string::f("Pad %d pressure CV", i + 1));
			padTouching[i].store(false);
			padPressEdge[i].store(false);
		}
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		flipState = false;
		wasPressed = false;
		xSmoothed = ySmoothed = 0.f;
		for (int i = 0; i < NUM_PADS; i++) {
			padPressure[i] = 0.f;
			padTouching[i].store(false);
			padPressEdge[i].store(false);
		}
		joyTouching.store(false);
	}

	/** GATE/FLIP/CV all share the same idea: two levels, swapped by INV. */
	float polarized(bool state, float lo, float hi) {
		if (params[INV_PARAM].getValue() > 0.5f)
			std::swap(lo, hi);
		return state ? hi : lo;
	}

	void process(const ProcessArgs& args) override {
		// --- the joystick: scale, offset, glide, spring return -------------
		bool touching = joyTouching.load(std::memory_order_relaxed);
		if (springReturn && !touching) {
			float pull = 1.f - std::exp(-args.sampleTime / 0.12f);
			params[X_POS_PARAM].setValue(params[X_POS_PARAM].getValue() * (1.f - pull));
			params[Y_POS_PARAM].setValue(params[Y_POS_PARAM].getValue() * (1.f - pull));
		}

		float xTarget = clamp(params[X_POS_PARAM].getValue() * params[X_SCALE_PARAM].getValue()
		                       * 5.f + params[X_OFFSET_PARAM].getValue(), -12.f, 12.f);
		float yTarget = clamp(params[Y_POS_PARAM].getValue() * params[Y_SCALE_PARAM].getValue()
		                       * 5.f + params[Y_OFFSET_PARAM].getValue(), -12.f, 12.f);

		float glideMs = params[GLIDE_PARAM].getValue();
		if (glideMs < 0.01f) {
			xSmoothed = xTarget;
			ySmoothed = yTarget;
		}
		else {
			float coeff = 1.f - std::exp(-args.sampleTime / (glideMs * 0.001f));
			xSmoothed += (xTarget - xSmoothed) * coeff;
			ySmoothed += (yTarget - ySmoothed) * coeff;
		}
		outputs[X_CV_OUT_OUTPUT].setVoltage(xSmoothed);
		outputs[Y_CV_OUT_OUTPUT].setVoltage(ySmoothed);
		outputs[TOUCH_GATE_OUT_OUTPUT].setVoltage(touching ? 10.f : 0.f);

		// --- the schematic's own IN-to-OUT path, as a bipolar attenuverter --
		float in = inputs[UTIL_IN_INPUT].getVoltageSum();
		outputs[UTIL_OUT_OUTPUT].setVoltage(clamp(in * params[AMOUNT_PARAM].getValue(), -12.f, 12.f));

		// --- the button and pedal --------------------------------------------
		bool pressed = params[BUTTON_PARAM].getValue() > 0.5f
		               || inputs[PEDAL_IN_INPUT].getVoltage() >= 1.f;
		bool edge = pressed && !wasPressed;
		wasPressed = pressed;
		if (edge) {
			flipState = !flipState;
			btnTrigPulse.trigger(1e-3f);
		}
		outputs[BTN_GATE_OUT_OUTPUT].setVoltage(polarized(pressed, 0.f, 10.f));
		outputs[BTN_FLIP_OUT_OUTPUT].setVoltage(polarized(flipState, 0.f, 10.f));
		outputs[BTN_CV_OUT_OUTPUT].setVoltage(polarized(pressed, 0.f, params[LEVEL_PARAM].getValue()));
		outputs[BTN_TRIG_OUT_OUTPUT].setVoltage(btnTrigPulse.process(args.sampleTime) ? 10.f : 0.f);
		lights[BUTTON_LIGHT].setBrightness(pressed ? 1.f : 0.f);

		// --- the four touch pads: gate, trigger, and a pressure CV that ------
		// ramps up while held and settles back down when released -- the RC
		// charge/discharge the touch schematic's own comparator bias runs on.
		for (int i = 0; i < NUM_PADS; i++) {
			bool held = padTouching[i].load(std::memory_order_relaxed);
			if (padPressEdge[i].exchange(false, std::memory_order_relaxed))
				padTrigPulse[i].trigger(1e-3f);

			float target = held ? 10.f : 0.f;
			float tau = held ? 0.15f : 0.08f;
			float coeff = 1.f - std::exp(-args.sampleTime / tau);
			padPressure[i] += (target - padPressure[i]) * coeff;

			outputs[PAD_GATE_OUTPUT + i].setVoltage(held ? 10.f : 0.f);
			outputs[PAD_CV_OUTPUT + i].setVoltage(padPressure[i]);
			outputs[PAD_TRIG_OUTPUT + i].setVoltage(padTrigPulse[i].process(args.sampleTime) ? 10.f : 0.f);
		}
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "springReturn", json_boolean(springReturn));
		json_object_set_new(root, "flipState", json_boolean(flipState));
		return root;
	}

	void dataFromJson(json_t* root) override {
		json_t* j;
		if ((j = json_object_get(root, "springReturn")))
			springReturn = json_boolean_value(j);
		if ((j = json_object_get(root, "flipState")))
			flipState = json_boolean_value(j);
	}
};


// ---------------------------------------------------------------------------
// The joystick pad and the four touch pads: custom widgets drawn with nvg
// geometry and panel:: colours over the recessed GLASS wells the spec laid
// out as Plates (see the family idiom in src/Repossession/Repossession.cpp's
// TimelineStrip). Everything else about the panel's look comes from
// src/PanelTheme.hpp and this panel's own Panel.hpp -- nothing is hand-drawn
// here except these two widgets' own live content.

struct JoystickPad : widget::OpaqueWidget {
	SignHere* module = NULL;

	Vec toNorm(Vec local) const {
		float x = clamp(local.x / box.size.x * 2.f - 1.f, -1.f, 1.f);
		float y = clamp(1.f - local.y / box.size.y * 2.f, -1.f, 1.f);
		return Vec(x, y);
	}

	void setFromLocal(Vec local) {
		if (!module)
			return;
		Vec n = toNorm(local);
		module->params[SignHere::X_POS_PARAM].setValue(n.x);
		module->params[SignHere::Y_POS_PARAM].setValue(n.y);
	}

	void onButton(const ButtonEvent& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			e.consume(this);
			if (module)
				module->joyTouching.store(true, std::memory_order_relaxed);
			setFromLocal(e.pos);
		}
		else {
			OpaqueWidget::onButton(e);
		}
	}

	void onDragMove(const DragMoveEvent& e) override {
		OpaqueWidget::onDragMove(e);
		if (!module)
			return;
		float zoom = getAbsoluteZoom();
		if (zoom <= 0.f)
			zoom = 1.f;
		float x = clamp(module->params[SignHere::X_POS_PARAM].getValue() + e.mouseDelta.x / zoom / (box.size.x / 2.f), -1.f, 1.f);
		float y = clamp(module->params[SignHere::Y_POS_PARAM].getValue() - e.mouseDelta.y / zoom / (box.size.y / 2.f), -1.f, 1.f);
		module->params[SignHere::X_POS_PARAM].setValue(x);
		module->params[SignHere::Y_POS_PARAM].setValue(y);
	}

	void onDragEnd(const DragEndEvent& e) override {
		OpaqueWidget::onDragEnd(e);
		if (module)
			module->joyTouching.store(false, std::memory_order_relaxed);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) {
			Widget::drawLayer(args, layer);
			return;
		}
		float w = box.size.x, h = box.size.y;
		float cx = w / 2.f, cy = h / 2.f;

		// Crosshair through the centre -- the pad's own rest position.
		nvgBeginPath(args.vg);
		nvgMoveTo(args.vg, cx, 2.f);
		nvgLineTo(args.vg, cx, h - 2.f);
		nvgMoveTo(args.vg, 2.f, cy);
		nvgLineTo(args.vg, w - 2.f, cy);
		nvgStrokeColor(args.vg, panel::alpha(panel::SAGE, 0.35f));
		nvgStrokeWidth(args.vg, 0.7f);
		nvgStroke(args.vg);

		float nx = module ? module->params[SignHere::X_POS_PARAM].getValue() : 0.f;
		float ny = module ? module->params[SignHere::Y_POS_PARAM].getValue() : 0.f;
		float px = cx + nx * (cx - 3.f);
		float py = cy - ny * (cy - 3.f);
		bool touching = module && module->joyTouching.load(std::memory_order_relaxed);

		nvgBeginPath(args.vg);
		nvgCircle(args.vg, px, py, touching ? 3.4f : 2.6f);
		nvgFillColor(args.vg, touching ? panel::LIME : panel::alpha(panel::LIME, 0.8f));
		nvgFill(args.vg);
		nvgStrokeColor(args.vg, panel::PAPER);
		nvgStrokeWidth(args.vg, 0.6f);
		nvgStroke(args.vg);

		Widget::drawLayer(args, layer);
	}
};


struct TouchPad : widget::OpaqueWidget {
	SignHere* module = NULL;
	int index = 0;

	void onButton(const ButtonEvent& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			e.consume(this);
			if (module) {
				module->padTouching[index].store(true, std::memory_order_relaxed);
				module->padPressEdge[index].store(true, std::memory_order_relaxed);
			}
		}
		else {
			OpaqueWidget::onButton(e);
		}
	}

	void onDragEnd(const DragEndEvent& e) override {
		OpaqueWidget::onDragEnd(e);
		if (module)
			module->padTouching[index].store(false, std::memory_order_relaxed);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) {
			Widget::drawLayer(args, layer);
			return;
		}
		float w = box.size.x, h = box.size.y;
		bool held = module && module->padTouching[index].load(std::memory_order_relaxed);
		float pressure = module ? module->padPressure[index] / 10.f : 0.f;

		if (pressure > 0.001f) {
			nvgBeginPath(args.vg);
			nvgRoundedRect(args.vg, 1.f, 1.f, w - 2.f, h - 2.f, 1.0f);
			nvgFillColor(args.vg, panel::alpha(panel::MINT, 0.15f + 0.35f * pressure));
			nvgFill(args.vg);
		}

		panel::TextStyle st(panel::Face::Ui, 8.5f,
			held ? panel::MINT : panel::alpha(panel::SAGE, 0.7f),
			NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		panel::text(args.vg, st, w / 2.f, h / 2.f, string::f("%d", index + 1));

		Widget::drawLayer(args, layer);
	}
};


struct SignHereWidget : ModuleWidget {
	SignHereWidget(SignHere* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/SignHere.svg")));

		panel::addScrews(this);
		panel::addLabels(this);

		JoystickPad* joy = new JoystickPad;
		joy->module = module;
		joy->box.pos = panel::mm(panel::JOY_X, panel::JOY_Y);
		joy->box.size = panel::mm(panel::JOY_W, panel::JOY_H);
		addChild(joy);

		static const float PAD_X[4] = {panel::TP_X0, panel::TP_X1, panel::TP_X0, panel::TP_X1};
		static const float PAD_Y[4] = {panel::TP_Y0, panel::TP_Y0, panel::TP_Y1, panel::TP_Y1};
		for (int i = 0; i < NUM_PADS; i++) {
			TouchPad* pad = new TouchPad;
			pad->module = module;
			pad->index = i;
			pad->box.pos = panel::mm(PAD_X[i], PAD_Y[i]);
			pad->box.size = panel::mm(panel::TP_W, panel::TP_H);
			addChild(pad);
		}

		addParam(createParamCentered<Trimpot>(panel::mm(panel::X_SCALE_POS.x, panel::X_SCALE_POS.y), module, SignHere::X_SCALE_PARAM));
		addParam(createParamCentered<Trimpot>(panel::mm(panel::X_OFFSET_POS.x, panel::X_OFFSET_POS.y), module, SignHere::X_OFFSET_PARAM));
		addParam(createParamCentered<Trimpot>(panel::mm(panel::Y_SCALE_POS.x, panel::Y_SCALE_POS.y), module, SignHere::Y_SCALE_PARAM));
		addParam(createParamCentered<Trimpot>(panel::mm(panel::Y_OFFSET_POS.x, panel::Y_OFFSET_POS.y), module, SignHere::Y_OFFSET_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(panel::mm(panel::GLIDE_POS.x, panel::GLIDE_POS.y), module, SignHere::GLIDE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(panel::mm(panel::AMOUNT_POS.x, panel::AMOUNT_POS.y), module, SignHere::AMOUNT_PARAM));

		addParam(createLightParamCentered<VCVLightBezel<panel::LimeLight> >(
			panel::mm(panel::BUTTON_POS.x, panel::BUTTON_POS.y), module, SignHere::BUTTON_PARAM, SignHere::BUTTON_LIGHT));
		addParam(createParamCentered<RoundBlackKnob>(panel::mm(panel::LEVEL_POS.x, panel::LEVEL_POS.y), module, SignHere::LEVEL_PARAM));
		addParam(createParamCentered<CKSS>(panel::mm(panel::INV_POS.x, panel::INV_POS.y), module, SignHere::INV_PARAM));

		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::X_CV_OUT_POS.x, panel::X_CV_OUT_POS.y), module, SignHere::X_CV_OUT_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::Y_CV_OUT_POS.x, panel::Y_CV_OUT_POS.y), module, SignHere::Y_CV_OUT_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::TOUCH_GATE_OUT_POS.x, panel::TOUCH_GATE_OUT_POS.y), module, SignHere::TOUCH_GATE_OUT_OUTPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::PEDAL_IN_POS.x, panel::PEDAL_IN_POS.y), module, SignHere::PEDAL_IN_INPUT));
		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::BTN_GATE_OUT_POS.x, panel::BTN_GATE_OUT_POS.y), module, SignHere::BTN_GATE_OUT_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::BTN_TRIG_OUT_POS.x, panel::BTN_TRIG_OUT_POS.y), module, SignHere::BTN_TRIG_OUT_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::BTN_FLIP_OUT_POS.x, panel::BTN_FLIP_OUT_POS.y), module, SignHere::BTN_FLIP_OUT_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::BTN_CV_OUT_POS.x, panel::BTN_CV_OUT_POS.y), module, SignHere::BTN_CV_OUT_OUTPUT));

		static const Vec* const GATE_POS[4] = {&panel::GATE1_POS, &panel::GATE2_POS, &panel::GATE3_POS, &panel::GATE4_POS};
		static const Vec* const TRIG_POS[4] = {&panel::TRIG1_POS, &panel::TRIG2_POS, &panel::TRIG3_POS, &panel::TRIG4_POS};
		static const Vec* const CV_POS[4]   = {&panel::CV1_POS, &panel::CV2_POS, &panel::CV3_POS, &panel::CV4_POS};
		for (int i = 0; i < NUM_PADS; i++) {
			addOutput(createOutputCentered<panel::PortOut>(panel::mm(GATE_POS[i]->x, GATE_POS[i]->y), module, SignHere::PAD_GATE_OUTPUT + i));
			addOutput(createOutputCentered<panel::PortOut>(panel::mm(TRIG_POS[i]->x, TRIG_POS[i]->y), module, SignHere::PAD_TRIG_OUTPUT + i));
			addOutput(createOutputCentered<panel::PortOut>(panel::mm(CV_POS[i]->x, CV_POS[i]->y), module, SignHere::PAD_CV_OUTPUT + i));
		}

		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::UTIL_IN_POS.x, panel::UTIL_IN_POS.y), module, SignHere::UTIL_IN_INPUT));
		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::UTIL_OUT_POS.x, panel::UTIL_OUT_POS.y), module, SignHere::UTIL_OUT_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		SignHere* m = dynamic_cast<SignHere*>(module);
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Sign Here"));
		menu->addChild(createBoolMenuItem("Joystick spring return", "",
			[=]() { return m->springReturn; },
			[=](bool v) { m->springReturn = v; }));
	}
};


Model* modelSignHere = createModel<SignHere, SignHereWidget>("SignHere");

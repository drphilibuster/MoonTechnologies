#include "../plugin.hpp"
#include "Panel.hpp"
#include "Bus.hpp"

#include <cmath>


// ---------------------------------------------------------------------------
// Bailout -- Consolidation at twice the size, and not quite the same shape.
//
// The strips are the same circuit: a 10k pot into a 10k input resistor feeding
// a unity-gain inverting summer, from "A Simple Mixer Right?" v1.4 (Kristian
// Blasol), with a second inverting stage after it. Eight of them instead of
// four, and the difference that matters is how they are grouped -- two banks of
// four, each with its own MIX output, and a MAIN that sums whichever banks are
// not already spoken for. Consolidation spends its second inverting stage on an
// INV output; Bailout spends it per bank, so MIX A and MIX B leave the module
// the same way round as MAIN.
//
// Every jack on the footer band steals rather than copies. A channel with its
// direct out patched leaves its bank; a bank with its MIX patched leaves MAIN.
// So the same panel is one 8-into-1 mixer, two independent 4s, eight VCAs, or
// any mixture, with no switch anywhere to say which it is being. The routing
// lives in Bus.hpp, where it can be tested -- the failure it invites is double
// counting, which still makes a sound and so does not announce itself.
//
// MULTIPLES is the same rework of "Buffured Multiple 3x1:2" that Consolidation
// carries, widened to what this panel's eight columns already pay for: two
// 1-in-7-out multiples, B normalled to A, so one cable is 1:7 and two are 1:14.
// ---------------------------------------------------------------------------

static const int NUM_CH = bailout::kChannels;   // 8
static const int NUM_BANK = bailout::kBanks;    // 2
static const int MULT_LEGS = 7;


struct Bailout : Module {
	enum ParamId {
		LVL1_PARAM, LVL2_PARAM, LVL3_PARAM, LVL4_PARAM,
		LVL5_PARAM, LVL6_PARAM, LVL7_PARAM, LVL8_PARAM,
		MUTE1_PARAM, MUTE2_PARAM, MUTE3_PARAM, MUTE4_PARAM,
		MUTE5_PARAM, MUTE6_PARAM, MUTE7_PARAM, MUTE8_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		IN1_INPUT, IN2_INPUT, IN3_INPUT, IN4_INPUT,
		IN5_INPUT, IN6_INPUT, IN7_INPUT, IN8_INPUT,
		CV1_INPUT, CV2_INPUT, CV3_INPUT, CV4_INPUT,
		CV5_INPUT, CV6_INPUT, CV7_INPUT, CV8_INPUT,
		MULT_A_IN_INPUT, MULT_B_IN_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		MIX_A_OUT_OUTPUT, MIX_B_OUT_OUTPUT, MAIN_OUT_OUTPUT,
		CH1_OUTPUT, CH2_OUTPUT, CH3_OUTPUT, CH4_OUTPUT,
		CH5_OUTPUT, CH6_OUTPUT, CH7_OUTPUT, CH8_OUTPUT,
		MULT_A_OUT1_OUTPUT,
		MULT_B_OUT1_OUTPUT = MULT_A_OUT1_OUTPUT + MULT_LEGS,
		OUTPUTS_LEN = MULT_B_OUT1_OUTPUT + MULT_LEGS
	};
	enum LightId {
		MUTE1_LIGHT, MUTE2_LIGHT, MUTE3_LIGHT, MUTE4_LIGHT,
		MUTE5_LIGHT, MUTE6_LIGHT, MUTE7_LIGHT, MUTE8_LIGHT,
		LIGHTS_LEN
	};

	//: What each channel is currently passing, for the arc drawn in the seat
	//: ring around its level knob. Peak-ish rather than RMS, fast up and slow
	//: down: a meter on a mixer is there to say "this one is the loud one".
	float meter[NUM_CH] = {};

	// Options: neither schematic had a front-panel switch for either.
	bool softClip = false;     // MIXER: soft-clamp at the op-amp rails, ~11 V
	bool passiveMult = false;  // MULTIPLES: sag a little as more legs are patched

	Bailout() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

		static const char* kBankName[NUM_BANK] = { "A", "B" };
		for (int i = 0; i < NUM_CH; i++) {
			const char* bank = kBankName[i / bailout::kPerBank];
			// Unity, not zero. A mixer that defaults to silence makes a patched
			// module look broken, and Rack's convention is the opposite --
			// every Fundamental level defaults to unity (VCMixer.cpp:38,
			// VCA.cpp:30, Mixer.cpp:29). It also matches the topology: 10k in
			// against 10k feedback is unity gain per channel.
			configParam(LVL1_PARAM + i, 0.f, 1.f, 1.f,
			            string::f("Channel %d level", i + 1), "%", 0.f, 100.f);
			configInput(IN1_INPUT + i, string::f("Channel %d", i + 1));
			configInput(CV1_INPUT + i, string::f("Channel %d level CV", i + 1));
			// Patched, a direct output takes that channel out of its bank. The
			// name says so, because a jack that quietly removes a channel from
			// somewhere else is the sort of thing a manual gets read for.
			configOutput(CH1_OUTPUT + i,
			             string::f("Channel %d direct (removes it from mix %s)",
			                       i + 1, bank));
			configSwitch(MUTE1_PARAM + i, 0.f, 1.f, 0.f,
			             string::f("Channel %d mute", i + 1),
			             {"Passing", "Muted"});
		}
		configOutput(MIX_A_OUT_OUTPUT, "Mix A, channels 1-4 (removes them from Main)");
		configOutput(MIX_B_OUT_OUTPUT, "Mix B, channels 5-8 (removes them from Main)");
		configOutput(MAIN_OUT_OUTPUT, "Main, whichever banks are unpatched");

		configInput(MULT_A_IN_INPUT, "Multiple A");
		configInput(MULT_B_IN_INPUT, "Multiple B (normalled to A)");
		for (int i = 0; i < MULT_LEGS; i++) {
			configOutput(MULT_A_OUT1_OUTPUT + i, string::f("Multiple A, leg %d", i + 1));
			configOutput(MULT_B_OUT1_OUTPUT + i, string::f("Multiple B, leg %d", i + 1));
		}

		// Rack allows each output to be bypass-routed exactly once
		// (Rack-SDK/include/engine/Module.hpp:240 asserts it), so Main can only
		// carry one channel through: channel 1, the same choice every
		// hardware-style mixer bypass makes. The multiples fan one input out to
		// distinct outputs, which the rule permits.
		configBypass(IN1_INPUT, MAIN_OUT_OUTPUT);
		for (int i = 0; i < MULT_LEGS; i++) {
			configBypass(MULT_A_IN_INPUT, MULT_A_OUT1_OUTPUT + i);
			configBypass(MULT_B_IN_INPUT, MULT_B_OUT1_OUTPUT + i);
		}
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		for (int i = 0; i < NUM_CH; i++)
			meter[i] = 0.f;
	}

	void processMixer() {
		int channels = 1;
		for (int i = 0; i < NUM_CH; i++)
			channels = std::max(channels, inputs[IN1_INPUT + i].getChannels());

		outputs[MIX_A_OUT_OUTPUT].setChannels(channels);
		outputs[MIX_B_OUT_OUTPUT].setChannels(channels);
		outputs[MAIN_OUT_OUTPUT].setChannels(channels);

		float level[NUM_CH];
		bool direct[NUM_CH];
		for (int i = 0; i < NUM_CH; i++) {
			bool muted = params[MUTE1_PARAM + i].getValue() > 0.5f;
			Input& cv = inputs[CV1_INPUT + i];
			level[i] = bailout::levelOf(params[LVL1_PARAM + i].getValue(),
			                            cv.isConnected(), cv.getVoltage(), muted);
			lights[MUTE1_LIGHT + i].setBrightness(muted ? 1.f : 0.f);
			direct[i] = outputs[CH1_OUTPUT + i].isConnected();
			if (direct[i])
				outputs[CH1_OUTPUT + i].setChannels(channels);
		}

		bool mixPatched[NUM_BANK] = {
			outputs[MIX_A_OUT_OUTPUT].isConnected(),
			outputs[MIX_B_OUT_OUTPUT].isConnected() };

		for (int c = 0; c < channels; c++) {
			float v[NUM_CH];
			for (int i = 0; i < NUM_CH; i++) {
				v[i] = inputs[IN1_INPUT + i].getPolyVoltage(c) * level[i];
				// Fast up, slow down. Rise is immediate so a transient is not
				// missed between two frames of the panel being drawn.
				float mag = std::fabs(v[i]) / 10.f;
				if (mag > meter[i])
					meter[i] = mag;
			}

			bailout::Routing r;
			bailout::route(v, direct, mixPatched, softClip, r);

			for (int i = 0; i < NUM_CH; i++)
				if (direct[i])
					outputs[CH1_OUTPUT + i].setVoltage(clamp(r.direct[i], -12.f, 12.f), c);
			outputs[MIX_A_OUT_OUTPUT].setVoltage(clamp(r.mix[0], -12.f, 12.f), c);
			outputs[MIX_B_OUT_OUTPUT].setVoltage(clamp(r.mix[1], -12.f, 12.f), c);
			outputs[MAIN_OUT_OUTPUT].setVoltage(clamp(r.main, -12.f, 12.f), c);
		}
	}

	/** One 1-in-7-out multiple. `first` is the id of its first leg. */
	void processMult(Input& in, int first) {
		int channels = std::max(1, in.getChannels());

		// A bare-wire multiple has no source impedance of its own to sag under
		// load, but the folklore about a passive mult "loading down" as you
		// patch more of it is too good a joke for a joke plugin to leave out --
		// so this mode knocks a percent or so off per patched leg, faithful to
		// the story rather than to the physics.
		float droop = 1.f;
		if (passiveMult) {
			int patched = 0;
			for (int i = 0; i < MULT_LEGS; i++)
				if (outputs[first + i].isConnected())
					patched++;
			droop = 1.f - 0.01f * patched;
		}

		for (int i = 0; i < MULT_LEGS; i++) {
			outputs[first + i].setChannels(channels);
			for (int c = 0; c < channels; c++)
				outputs[first + i].setVoltage(
					clamp(in.getPolyVoltage(c) * droop, -12.f, 12.f), c);
		}
	}

	void process(const ProcessArgs& args) override {
		processMixer();

		// The fall, once per sample rather than once per polyphony channel.
		for (int i = 0; i < NUM_CH; i++)
			meter[i] = std::fmax(0.f, meter[i] - args.sampleTime * 1.6f);

		processMult(inputs[MULT_A_IN_INPUT], MULT_A_OUT1_OUTPUT);

		// B normals from A's raw input, not from A's already-multed legs, so an
		// unpatched B is indistinguishable from more legs on A.
		Input& bSource = inputs[MULT_B_IN_INPUT].isConnected()
		                 ? inputs[MULT_B_IN_INPUT] : inputs[MULT_A_IN_INPUT];
		processMult(bSource, MULT_B_OUT1_OUTPUT);
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "softClip", json_boolean(softClip));
		json_object_set_new(root, "passiveMult", json_boolean(passiveMult));
		return root;
	}

	void dataFromJson(json_t* root) override {
		json_t* j;
		j = json_object_get(root, "softClip");
		if (j) softClip = json_boolean_value(j);
		j = json_object_get(root, "passiveMult");
		if (j) passiveMult = json_boolean_value(j);
	}
};


// ---------------------------------------------------------------------------
// Look and feel comes entirely from src/PanelTheme.hpp and src/Bailout/Panel.hpp,
// generated by tools/panels/Bailout.py -- see ../../panelkit/README.md.

typedef RoundBlackKnob BailoutKnob;


struct BailoutWidget : ModuleWidget {
	BailoutWidget(Bailout* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Bailout.svg")));

		panel::addScrews(this);
		panel::addLabels(this);

		static const Vec* lvlPos[NUM_CH] = {
			&panel::LVL1_POS, &panel::LVL2_POS, &panel::LVL3_POS, &panel::LVL4_POS,
			&panel::LVL5_POS, &panel::LVL6_POS, &panel::LVL7_POS, &panel::LVL8_POS };
		static const Vec* mutePos[NUM_CH] = {
			&panel::MUTE1_POS, &panel::MUTE2_POS, &panel::MUTE3_POS, &panel::MUTE4_POS,
			&panel::MUTE5_POS, &panel::MUTE6_POS, &panel::MUTE7_POS, &panel::MUTE8_POS };
		static const Vec* cvPos[NUM_CH] = {
			&panel::CV1_POS, &panel::CV2_POS, &panel::CV3_POS, &panel::CV4_POS,
			&panel::CV5_POS, &panel::CV6_POS, &panel::CV7_POS, &panel::CV8_POS };
		static const Vec* inPos[NUM_CH] = {
			&panel::IN1_POS, &panel::IN2_POS, &panel::IN3_POS, &panel::IN4_POS,
			&panel::IN5_POS, &panel::IN6_POS, &panel::IN7_POS, &panel::IN8_POS };
		static const Vec* chOutPos[NUM_CH] = {
			&panel::OUT1_POS, &panel::OUT2_POS, &panel::OUT3_POS, &panel::OUT4_POS,
			&panel::OUT5_POS, &panel::OUT6_POS, &panel::OUT7_POS, &panel::OUT8_POS };

		for (int i = 0; i < NUM_CH; i++) {
			addParam(createParamCentered<BailoutKnob>(
				panel::mm(lvlPos[i]->x, lvlPos[i]->y), module, Bailout::LVL1_PARAM + i));

			// The meter, struck into the seat ring around its own level knob.
			// Placed on the knob's centre and sized to the well, so the arc
			// sweeps the same travel the pointer does.
			panel::MeterArc* m = new panel::MeterArc;
			m->box.size = math::Vec(36.f, 36.f);   // must contain the arc
			m->box.pos = panel::mm(lvlPos[i]->x, lvlPos[i]->y).minus(m->box.size.div(2.f));
			m->value = module ? &module->meter[i] : NULL;
			addChild(m);

			addParam(createLightParamCentered<VCVLightLatch<panel::LimeLight> >(
				panel::mm(mutePos[i]->x, mutePos[i]->y), module,
				Bailout::MUTE1_PARAM + i, Bailout::MUTE1_LIGHT + i));
			addInput(createInputCentered<panel::PortIn>(
				panel::mm(cvPos[i]->x, cvPos[i]->y), module, Bailout::CV1_INPUT + i));
			addInput(createInputCentered<panel::PortIn>(
				panel::mm(inPos[i]->x, inPos[i]->y), module, Bailout::IN1_INPUT + i));
			addOutput(createOutputCentered<panel::PortOut>(
				panel::mm(chOutPos[i]->x, chOutPos[i]->y), module, Bailout::CH1_OUTPUT + i));
		}

		static const Vec* multAPos[MULT_LEGS] = {
			&panel::MULT_A_OUT1_POS, &panel::MULT_A_OUT2_POS, &panel::MULT_A_OUT3_POS,
			&panel::MULT_A_OUT4_POS, &panel::MULT_A_OUT5_POS, &panel::MULT_A_OUT6_POS,
			&panel::MULT_A_OUT7_POS };
		static const Vec* multBPos[MULT_LEGS] = {
			&panel::MULT_B_OUT1_POS, &panel::MULT_B_OUT2_POS, &panel::MULT_B_OUT3_POS,
			&panel::MULT_B_OUT4_POS, &panel::MULT_B_OUT5_POS, &panel::MULT_B_OUT6_POS,
			&panel::MULT_B_OUT7_POS };

		addInput(createInputCentered<panel::PortIn>(
			panel::mm(panel::MULT_A_IN_POS.x, panel::MULT_A_IN_POS.y), module, Bailout::MULT_A_IN_INPUT));
		addInput(createInputCentered<panel::PortIn>(
			panel::mm(panel::MULT_B_IN_POS.x, panel::MULT_B_IN_POS.y), module, Bailout::MULT_B_IN_INPUT));
		for (int i = 0; i < MULT_LEGS; i++) {
			addOutput(createOutputCentered<panel::PortOut>(
				panel::mm(multAPos[i]->x, multAPos[i]->y), module, Bailout::MULT_A_OUT1_OUTPUT + i));
			addOutput(createOutputCentered<panel::PortOut>(
				panel::mm(multBPos[i]->x, multBPos[i]->y), module, Bailout::MULT_B_OUT1_OUTPUT + i));
		}

		addOutput(createOutputCentered<panel::PortOut>(
			panel::mm(panel::MIX_A_OUT_POS.x, panel::MIX_A_OUT_POS.y), module, Bailout::MIX_A_OUT_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(
			panel::mm(panel::MIX_B_OUT_POS.x, panel::MIX_B_OUT_POS.y), module, Bailout::MIX_B_OUT_OUTPUT));
		addOutput(createOutputCentered<panel::PortOutMain>(
			panel::mm(panel::MAIN_OUT_POS.x, panel::MAIN_OUT_POS.y), module, Bailout::MAIN_OUT_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		Bailout* m = dynamic_cast<Bailout*>(module);
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Bailout"));

		menu->addChild(createBoolMenuItem("Mixer: soft-clip at the op-amp rails", "",
			[=]() { return m->softClip; },
			[=](bool v) { m->softClip = v; }));

		menu->addChild(createBoolMenuItem("Multiples: passive-style loading droop", "",
			[=]() { return m->passiveMult; },
			[=](bool v) { m->passiveMult = v; }));
	}
};


Model* modelBailout = createModel<Bailout, BailoutWidget>("Bailout");

#include "../plugin.hpp"
#include "Panel.hpp"

#include <cmath>


// ---------------------------------------------------------------------------
// Consolidation -- a Modular in a Week mixer and multiples on one panel.
//
// MIXER models "A Simple Mixer Right?" v1.4 (Kristian Blasol) exactly: each
// channel is a 10k pot into a 10k input resistor feeding a unity-gain
// inverting summer, whose output feeds a second unity-gain inverting stage --
// so the module has both polarities of the sum available, the way the sibling
// "Simple Unbuffered Mixer" schematic breaks its own first stage's output out
// as InvertedOutput. The compensation caps across both feedback resistors
// (27 pF, 47 pF) and the output RC (100 ohm / 10 uF) are HF-stability and
// DC-blocking details with no audible effect at audio rates, and are not
// modeled.
//
// MULTIPLES reworks "Buffured Multiple 3x1:2" (three independent 1-in-2-out
// buffers) into two 1-in-3-out multiples, B normalled to A -- one cable into A
// gives 1:3, a second into B gives 1:6, and patching B directly gives two
// independent 1:3s. Each leg is a unity buffer, matching the schematic's TL074
// voltage followers.
// ---------------------------------------------------------------------------

static const int NUM_CH = 4;


struct Consolidation : Module {
	enum ParamId {
		LVL1_PARAM, LVL2_PARAM, LVL3_PARAM, LVL4_PARAM,
		MUTE1_PARAM, MUTE2_PARAM, MUTE3_PARAM, MUTE4_PARAM, PARAMS_LEN
	};
	enum InputId {
		IN1_INPUT, IN2_INPUT, IN3_INPUT, IN4_INPUT,
		CV1_INPUT, CV2_INPUT, CV3_INPUT, CV4_INPUT,
		MULT_A_IN_INPUT, MULT_B_IN_INPUT, INPUTS_LEN
	};
	enum OutputId {
		OUT_OUTPUT, INV_OUT_OUTPUT,
		CH1_OUTPUT, CH2_OUTPUT, CH3_OUTPUT, CH4_OUTPUT,
		MULT_A_OUT1_OUTPUT, MULT_A_OUT2_OUTPUT, MULT_A_OUT3_OUTPUT,
		MULT_B_OUT1_OUTPUT, MULT_B_OUT2_OUTPUT, MULT_B_OUT3_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		MUTE1_LIGHT, MUTE2_LIGHT, MUTE3_LIGHT, MUTE4_LIGHT,
		LIGHTS_LEN
	};

	//: What each channel is currently passing, smoothed, for the arc drawn in
	//: the seat ring around its level knob. Peak-ish rather than RMS: a meter
	//: on a mixer is there to say "this one is the loud one", and a fast rise
	//: with a slow fall is what reads as that.
	float meter[NUM_CH] = {};

	// Options: neither schematic had a front-panel switch for either.
	bool softClip = false;     // MIXER: soft-clamp at the op-amp rails, ~11 V
	bool passiveMult = false;  // MULTIPLES: sag a little as more legs are patched

	Consolidation() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

		for (int i = 0; i < NUM_CH; i++) {
			// Unity, not zero. A mixer that defaults to silence makes a patched
			// module look broken, and Rack's convention is the opposite --
			// every Fundamental level defaults to unity (VCMixer.cpp:38,
			// VCA.cpp:30, Mixer.cpp:29). It also matches the topology above:
			// 10k in against 10k feedback is unity gain per channel.
			configParam(LVL1_PARAM + i, 0.f, 1.f, 1.f,
			            string::f("Channel %d level", i + 1), "%", 0.f, 100.f);
			configInput(IN1_INPUT + i, string::f("Channel %d", i + 1));
			configInput(CV1_INPUT + i, string::f("Channel %d level CV", i + 1));
			// Patched, a direct output takes that channel out of the mix. The
			// name says so, because a jack that quietly removes a channel from
			// somewhere else is the sort of thing a manual gets read for.
			configOutput(CH1_OUTPUT + i,
			             string::f("Channel %d direct (removes it from the mix)", i + 1));
			configSwitch(MUTE1_PARAM + i, 0.f, 1.f, 0.f,
			             string::f("Channel %d mute", i + 1),
			             {"Passing", "Muted"});
		}
		configOutput(OUT_OUTPUT, "Mix");
		configOutput(INV_OUT_OUTPUT, "Inverted mix");

		configInput(MULT_A_IN_INPUT, "Multiple A");
		configInput(MULT_B_IN_INPUT, "Multiple B");
		configOutput(MULT_A_OUT1_OUTPUT, "Multiple A, leg 1");
		configOutput(MULT_A_OUT2_OUTPUT, "Multiple A, leg 2");
		configOutput(MULT_A_OUT3_OUTPUT, "Multiple A, leg 3");
		configOutput(MULT_B_OUT1_OUTPUT, "Multiple B, leg 1");
		configOutput(MULT_B_OUT2_OUTPUT, "Multiple B, leg 2");
		configOutput(MULT_B_OUT3_OUTPUT, "Multiple B, leg 3");

		// Rack allows each output to be bypass-routed exactly once
		// (Rack-SDK/include/engine/Module.hpp:240 asserts it), so the mix
		// output can only carry one channel through: channel 1, the same
		// choice every hardware-style mixer bypass makes. The multiples fan
		// one input out to distinct outputs, which the rule permits.
		configBypass(IN1_INPUT, OUT_OUTPUT);
		for (int i = 0; i < 3; i++) {
			configBypass(MULT_A_IN_INPUT, MULT_A_OUT1_OUTPUT + i);
			configBypass(MULT_B_IN_INPUT, MULT_B_OUT1_OUTPUT + i);
		}
	}

	void processMixer() {
		int channels = 1;
		for (int i = 0; i < NUM_CH; i++)
			channels = std::max(channels, inputs[IN1_INPUT + i].getChannels());

		outputs[OUT_OUTPUT].setChannels(channels);
		outputs[INV_OUT_OUTPUT].setChannels(channels);

		float level[NUM_CH];
		bool direct[NUM_CH];
		for (int i = 0; i < NUM_CH; i++) {
			level[i] = params[LVL1_PARAM + i].getValue();
			// CV multiplies the knob rather than adding to it, so the knob is
			// the depth and an unpatched jack leaves the channel alone. That is
			// what makes the same strip a mixer channel and a VCA without a
			// mode switch to say which it is being.
			if (inputs[CV1_INPUT + i].isConnected())
				level[i] *= clamp(inputs[CV1_INPUT + i].getVoltage() / 10.f, 0.f, 1.f);
			if (params[MUTE1_PARAM + i].getValue() > 0.5f)
				level[i] = 0.f;
			lights[MUTE1_LIGHT + i].setBrightness(
				params[MUTE1_PARAM + i].getValue() > 0.5f ? 1.f : 0.f);
			direct[i] = outputs[CH1_OUTPUT + i].isConnected();
			if (direct[i]) outputs[CH1_OUTPUT + i].setChannels(channels);
		}

		for (int c = 0; c < channels; c++) {
			// Unity gain per channel (10k in, 10k feedback), attenuated by
			// each channel's own level pot before it reaches the summing node
			// -- the ASMR topology, with a knob standing in for the pot.
			float sum = 0.f;
			for (int i = 0; i < NUM_CH; i++) {
				float v = inputs[IN1_INPUT + i].getPolyVoltage(c) * level[i];
				if (direct[i]) outputs[CH1_OUTPUT + i].setVoltage(clamp(v, -12.f, 12.f), c);
				else sum += v;
				float mag = std::fabs(v) / 10.f;
				// Fast up, slow down: a meter on a mixer answers "which one is
				// the loud one", and a peak that falls slowly is what reads as
				// that. Rise is immediate so a transient is not missed between
				// two frames of the panel being drawn.
				if (mag > meter[i]) meter[i] = mag;
			}

			if (softClip) {
				// The op-amps' own supply rails: TL07x output swing tops out
				// a volt or so shy of a +-12 V rail. tanh gives that a soft
				// knee rather than the hard clip a starved rail would not
				// actually produce.
				const float RAIL = 11.f;
				sum = RAIL * std::tanh(sum / RAIL);
			}
			sum = clamp(sum, -12.f, 12.f);

			// The second inverting stage is unity gain too, so INV OUT is
			// simply the mix negated -- both polarities of the same sum.
			outputs[OUT_OUTPUT].setVoltage(sum, c);
			outputs[INV_OUT_OUTPUT].setVoltage(-sum, c);
		}
	}

	/** One 1-in-3-out multiple. `legs` are the three output ids, in order. */
	void processMult(Input& in, const int legs[3]) {
		int channels = std::max(1, in.getChannels());

		// A bare-wire multiple has no source impedance of its own to sag
		// under load, but the folklore about a passive mult "loading down"
		// as you patch more of it is too good a joke for a joke plugin to
		// leave out -- so this mode knocks a percent or so off the signal
		// per patched leg, faithful to the story rather than to the physics.
		float droop = 1.f;
		if (passiveMult) {
			int patched = 0;
			for (int i = 0; i < 3; i++)
				if (outputs[legs[i]].isConnected())
					patched++;
			droop = 1.f - 0.01f * patched;
		}

		for (int i = 0; i < 3; i++) {
			outputs[legs[i]].setChannels(channels);
			for (int c = 0; c < channels; c++)
				outputs[legs[i]].setVoltage(clamp(in.getPolyVoltage(c) * droop, -12.f, 12.f), c);
		}
	}

	void process(const ProcessArgs& args) override {
		processMixer();

		// The fall, once per sample rather than once per channel.
		for (int i = 0; i < NUM_CH; i++)
			meter[i] = std::fmax(0.f, meter[i] - args.sampleTime * 1.6f);

		static const int legsA[3] = {MULT_A_OUT1_OUTPUT, MULT_A_OUT2_OUTPUT, MULT_A_OUT3_OUTPUT};
		static const int legsB[3] = {MULT_B_OUT1_OUTPUT, MULT_B_OUT2_OUTPUT, MULT_B_OUT3_OUTPUT};

		processMult(inputs[MULT_A_IN_INPUT], legsA);

		// B normals from A's raw input, not from A's already-multed legs, so
		// an unpatched B is indistinguishable from a third leg on A.
		Input& bSource = inputs[MULT_B_IN_INPUT].isConnected()
		                 ? inputs[MULT_B_IN_INPUT] : inputs[MULT_A_IN_INPUT];
		processMult(bSource, legsB);
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
// Look and feel comes entirely from src/PanelTheme.hpp and
// src/Consolidation/Panel.hpp, generated by tools/panels/Consolidation.py --
// see ../../panelkit/README.md.

typedef RoundBlackKnob ConsolidationKnob;


struct ConsolidationWidget : ModuleWidget {
	ConsolidationWidget(Consolidation* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Consolidation.svg")));

		panel::addScrews(this);
		panel::addLabels(this);

		addParam(createParamCentered<ConsolidationKnob>(panel::mm(panel::LVL1_POS.x, panel::LVL1_POS.y), module, Consolidation::LVL1_PARAM));
		addParam(createParamCentered<ConsolidationKnob>(panel::mm(panel::LVL2_POS.x, panel::LVL2_POS.y), module, Consolidation::LVL2_PARAM));
		addParam(createParamCentered<ConsolidationKnob>(panel::mm(panel::LVL3_POS.x, panel::LVL3_POS.y), module, Consolidation::LVL3_PARAM));
		addParam(createParamCentered<ConsolidationKnob>(panel::mm(panel::LVL4_POS.x, panel::LVL4_POS.y), module, Consolidation::LVL4_PARAM));

		// Each channel's meter, struck into the seat ring around its own level
		// knob. Placed on the knob's centre and sized to the well, so the arc
		// sweeps the same travel the pointer does.
		static const Vec* lvlPos[NUM_CH] = {
			&panel::LVL1_POS, &panel::LVL2_POS, &panel::LVL3_POS, &panel::LVL4_POS };
		for (int i = 0; i < NUM_CH; i++) {
			panel::MeterArc* m = new panel::MeterArc;
			m->box.size = math::Vec(36.f, 36.f);   // must contain the arc
			m->box.pos = panel::mm(lvlPos[i]->x, lvlPos[i]->y).minus(m->box.size.div(2.f));
			m->value = module ? &module->meter[i] : NULL;
			addChild(m);
		}

		static const Vec* mutePos[NUM_CH] = {
			&panel::MUTE1_POS, &panel::MUTE2_POS, &panel::MUTE3_POS, &panel::MUTE4_POS };
		static const Vec* cvPos[NUM_CH] = {
			&panel::CV1_POS, &panel::CV2_POS, &panel::CV3_POS, &panel::CV4_POS };
		static const Vec* chOutPos[NUM_CH] = {
			&panel::OUT1_POS, &panel::OUT2_POS, &panel::OUT3_POS, &panel::OUT4_POS };
		for (int i = 0; i < NUM_CH; i++) {
			addParam(createLightParamCentered<VCVLightLatch<panel::LimeLight> >(
				panel::mm(mutePos[i]->x, mutePos[i]->y), module,
				Consolidation::MUTE1_PARAM + i, Consolidation::MUTE1_LIGHT + i));
			addInput(createInputCentered<panel::PortIn>(
				panel::mm(cvPos[i]->x, cvPos[i]->y), module, Consolidation::CV1_INPUT + i));
			addOutput(createOutputCentered<panel::PortOut>(
				panel::mm(chOutPos[i]->x, chOutPos[i]->y), module, Consolidation::CH1_OUTPUT + i));
		}

		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::IN1_POS.x, panel::IN1_POS.y), module, Consolidation::IN1_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::IN2_POS.x, panel::IN2_POS.y), module, Consolidation::IN2_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::IN3_POS.x, panel::IN3_POS.y), module, Consolidation::IN3_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::IN4_POS.x, panel::IN4_POS.y), module, Consolidation::IN4_INPUT));

		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::MULT_A_IN_POS.x, panel::MULT_A_IN_POS.y), module, Consolidation::MULT_A_IN_INPUT));
		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::MULT_A_OUT1_POS.x, panel::MULT_A_OUT1_POS.y), module, Consolidation::MULT_A_OUT1_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::MULT_A_OUT2_POS.x, panel::MULT_A_OUT2_POS.y), module, Consolidation::MULT_A_OUT2_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::MULT_A_OUT3_POS.x, panel::MULT_A_OUT3_POS.y), module, Consolidation::MULT_A_OUT3_OUTPUT));

		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::MULT_B_IN_POS.x, panel::MULT_B_IN_POS.y), module, Consolidation::MULT_B_IN_INPUT));
		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::MULT_B_OUT1_POS.x, panel::MULT_B_OUT1_POS.y), module, Consolidation::MULT_B_OUT1_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::MULT_B_OUT2_POS.x, panel::MULT_B_OUT2_POS.y), module, Consolidation::MULT_B_OUT2_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::MULT_B_OUT3_POS.x, panel::MULT_B_OUT3_POS.y), module, Consolidation::MULT_B_OUT3_OUTPUT));

		addOutput(createOutputCentered<panel::PortOutMain>(panel::mm(panel::OUT_POS.x, panel::OUT_POS.y), module, Consolidation::OUT_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::INV_OUT_POS.x, panel::INV_OUT_POS.y), module, Consolidation::INV_OUT_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		Consolidation* m = dynamic_cast<Consolidation*>(module);
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Consolidation"));

		menu->addChild(createBoolMenuItem("Mixer: soft-clip at the op-amp rails", "",
			[=]() { return m->softClip; },
			[=](bool v) { m->softClip = v; }));

		menu->addChild(createBoolMenuItem("Multiples: passive-style loading droop", "",
			[=]() { return m->passiveMult; },
			[=](bool v) { m->passiveMult = v; }));
	}
};


Model* modelConsolidation = createModel<Consolidation, ConsolidationWidget>("Consolidation");

#include "../plugin.hpp"
#include "Panel.hpp"

// Audit Logic consolidates three Modular in a Week boards behind one panel:
//
//   FINDINGS    the Quad Inverter (Day 8), the Hex Inverter (Day 8) and the
//               Quad Logic Module (Day 8, a 4071 quad-OR plus diode steering
//               and two TL074 comparator stages) -- four channels, each a
//               selectable two-input gate rather than the fixed function the
//               diode network etched into copper. The default per channel
//               follows the registered plugin description's own listing --
//               inverter, AND, OR, XOR -- which is the closest thing the
//               surviving schematics offer to a documented assignment; NAND,
//               NOR and XNOR are additional selections this module adds, not
//               a claim about the original board. Polyphonic: sixteen audits
//               at once. Unpatched A/B inputs read the panel's REF V jumper
//               (0 V or 12 V) rather than floating, exactly as the Quad
//               Inverter's own "Trigger Voltage" switch did for its unpatched
//               "in a" pins. The comparator thresholds -- rising 2 V, falling
//               1 V -- are the CD40106 Schmitt inverter's own, since that is
//               literally what the Hex Inverter board built this out of.
//
//   REFERRAL    the 4066 Quad Gated Switch (Day 10). The original ganged four
//               channels off one quad-bilateral-switch IC; this keeps two, so
//               the panel has room for the rest of the family. Each channel
//               passes A to B while its gate satisfies HI ON or LO ON
//               (shared by both channels, as the original's jumper was one
//               setting for the whole board); ROUTE's second position, A-C,
//               turns the same channel into a two-way router by also feeding
//               A to C whenever the gate does *not* satisfy that condition --
//               a complement the plain 4066 pass-gate does not need but a
//               1-to-2 CV router does. The 1 ms declick crossfade is this
//               module's own addition over the original's hard bilateral
//               switch, off by default so the panel's native character -- an
//               instant, clickable switch -- is what a patch gets until asked
//               otherwise. Mono: an analogue switch keeps no per-channel state
//               worth duplicating sixteen times.
//
//   INSTALLMENTS the Emiz Instruments CV2 clock divider (Day 8): one
//               free-running binary counter, six taps, each a 50 % duty gate
//               (BINARY: /2 /4 /8 /16 /32 /64 -- every tap is a single bit of
//               the counter, which is exactly why the duty cycle is always
//               50 % in that mode). MUSICAL reinterprets the same six jacks as
//               thirds -- /3 /6 /12 /24 /48 /96 -- rather than doubling the
//               panel with a second set of outputs; the panel silkscreen keeps
//               reading /2../64 in both modes, which is disclosed in the
//               manual the way Retroactive discloses its own latency. /3 and
//               /6's odd tap count means their gate is high 2 of every 3
//               (respectively 6) counts rather than an exact half -- documented,
//               not hidden. Mono, like the original board.
using rack::dsp::SchmittTrigger;


// --- shared level-sensing (not edge-triggered): both FINDINGS and REFERRAL
// need to know whether an input is *currently* above or below a threshold,
// with hysteresis so a noisy signal near the line does not chatter. A plain
// SchmittTrigger only reports the rising edge; this remembers the state.
struct LevelGate {
	bool state = false;
	bool sense(float v, float lo, float hi) {
		if (state) { if (v < lo) state = false; }
		else       { if (v >= hi) state = true; }
		return state;
	}
};

struct PolyLevelGate {
	bool state[16] = {};
	bool sense(int c, float v, float lo, float hi) {
		if (state[c]) { if (v < lo) state[c] = false; }
		else          { if (v >= hi) state[c] = true; }
		return state[c];
	}
	void reset() { for (int c = 0; c < 16; c++) state[c] = false; }
};


static const int kFnNotA = 0, kFnAnd = 1, kFnOr = 2, kFnXor = 3,
                  kFnNand = 4, kFnNor = 5, kFnXnor = 6, kNumFn = 7;

static bool evalGate(int fn, bool a, bool b) {
	switch (fn) {
		case kFnNotA: return !a;
		case kFnAnd:  return a && b;
		case kFnOr:   return a || b;
		case kFnXor:  return a != b;
		case kFnNand: return !(a && b);
		case kFnNor:  return !(a || b);
		case kFnXnor: default: return a == b;
	}
}

static const int kRouteAB = 0, kRouteAC = 1;
static const int kPolarityHiOn = 0;   // the only value compared against; Lo On is "not this"
static const int kDivBinary = 0, kDivMusical = 1;
static const int kBinaryTaps[6]  = { 2, 4, 8, 16, 32, 64 };
static const int kMusicalTaps[6] = { 3, 6, 12, 24, 48, 96 };


struct AuditLogic : Module {
	enum ParamId {
		FN1_PARAM, FN2_PARAM, FN3_PARAM, FN4_PARAM,
		POLARITY_PARAM, ROUTE_PARAM,
		DIVMODE_PARAM, REFV_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		A1_INPUT, B1_INPUT, A2_INPUT, B2_INPUT,
		A3_INPUT, B3_INPUT, A4_INPUT, B4_INPUT,
		SW1_A_INPUT, SW1_GATE_INPUT, SW2_A_INPUT, SW2_GATE_INPUT,
		CLOCK_INPUT, RESET_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		OUT1_OUTPUT, OUT2_OUTPUT, OUT3_OUTPUT, OUT4_OUTPUT,
		SW1_B_OUTPUT, SW1_C_OUTPUT, SW2_B_OUTPUT, SW2_C_OUTPUT,
		DIV2_OUTPUT, DIV4_OUTPUT, DIV8_OUTPUT, DIV16_OUTPUT, DIV32_OUTPUT, DIV64_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		VERDICT_LIGHT,
		OUT1_LIGHT, OUT2_LIGHT, OUT3_LIGHT, OUT4_LIGHT,
		ACTIVE_LIGHT,
		SW1_GATE_LIGHT, SW2_GATE_LIGHT,
		CLOCKED_LIGHT,
		DIV2_LIGHT, DIV4_LIGHT, DIV8_LIGHT, DIV16_LIGHT, DIV32_LIGHT, DIV64_LIGHT,
		LIGHTS_LEN
	};

	// FINDINGS -- polyphonic per gate.
	PolyLevelGate aGate[4], bGate[4];
	bool dispGateOut[4] = {};   // channel 0, for the lights

	// REFERRAL -- mono.
	LevelGate swGate[2];
	float onGain[2] = { 0.f, 0.f };
	bool declick = false;

	// INSTALLMENTS -- mono.
	SchmittTrigger clockTrig, resetTrig;
	uint32_t divCounter = 0;
	bool dispDivGate[6] = {};
	bool dispClocked = false;

	dsp::ClockDivider lightDivider;

	AuditLogic() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

		std::vector<std::string> fnLabels;
		fnLabels.push_back("Invert A"); fnLabels.push_back("AND");
		fnLabels.push_back("OR");       fnLabels.push_back("XOR");
		fnLabels.push_back("NAND");     fnLabels.push_back("NOR");
		fnLabels.push_back("XNOR");
		configSwitch(FN1_PARAM, 0.f, (float)(kNumFn - 1), (float)kFnNotA, "Gate 1 function", fnLabels);
		configSwitch(FN2_PARAM, 0.f, (float)(kNumFn - 1), (float)kFnAnd,  "Gate 2 function", fnLabels);
		configSwitch(FN3_PARAM, 0.f, (float)(kNumFn - 1), (float)kFnOr,   "Gate 3 function", fnLabels);
		configSwitch(FN4_PARAM, 0.f, (float)(kNumFn - 1), (float)kFnXor,  "Gate 4 function", fnLabels);

		std::vector<std::string> polarityLabels;
		polarityLabels.push_back("Hi On"); polarityLabels.push_back("Lo On");
		configSwitch(POLARITY_PARAM, 0.f, 1.f, (float)kPolarityHiOn, "Gate polarity", polarityLabels);

		std::vector<std::string> routeLabels;
		routeLabels.push_back("A-B"); routeLabels.push_back("A-B/A-C");
		configSwitch(ROUTE_PARAM, 0.f, 1.f, (float)kRouteAB, "Routing", routeLabels);

		std::vector<std::string> divModeLabels;
		divModeLabels.push_back("Binary");  divModeLabels.push_back("Musical");
		configSwitch(DIVMODE_PARAM, 0.f, 1.f, (float)kDivBinary, "Division set", divModeLabels);

		std::vector<std::string> refvLabels;
		refvLabels.push_back("0 V"); refvLabels.push_back("12 V");
		configSwitch(REFV_PARAM, 0.f, 1.f, 0.f, "Unpatched input reference", refvLabels);

		for (int i = 0; i < 4; i++) {
			configInput(A1_INPUT + 2 * i, string::f("Gate %d, A", i + 1));
			configInput(B1_INPUT + 2 * i, string::f("Gate %d, B", i + 1));
			configOutput(OUT1_OUTPUT + i, string::f("Gate %d out", i + 1));
		}

		configInput(SW1_A_INPUT, "Switch 1, A");
		configInput(SW1_GATE_INPUT, "Switch 1 gate");
		configOutput(SW1_B_OUTPUT, "Switch 1, B");
		configOutput(SW1_C_OUTPUT, "Switch 1, C");
		configInput(SW2_A_INPUT, "Switch 2, A");
		configInput(SW2_GATE_INPUT, "Switch 2 gate");
		configOutput(SW2_B_OUTPUT, "Switch 2, B");
		configOutput(SW2_C_OUTPUT, "Switch 2, C");

		configInput(CLOCK_INPUT, "Clock");
		configInput(RESET_INPUT, "Reset");
		configOutput(DIV2_OUTPUT, "/2");
		configOutput(DIV4_OUTPUT, "/4");
		configOutput(DIV8_OUTPUT, "/8");
		configOutput(DIV16_OUTPUT, "/16");
		configOutput(DIV32_OUTPUT, "/32");
		configOutput(DIV64_OUTPUT, "/64");

		lightDivider.setDivision(512);
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		for (int i = 0; i < 4; i++) { aGate[i].reset(); bGate[i].reset(); }
		swGate[0] = LevelGate(); swGate[1] = LevelGate();
		onGain[0] = onGain[1] = 0.f;
		clockTrig.reset();
		resetTrig.reset();
		divCounter = 0;
	}

	void process(const ProcessArgs& args) override {
		// --- FINDINGS: four polyphonic two-input gates -----------------------
		float refV = params[REFV_PARAM].getValue() > 0.5f ? 12.f : 0.f;
		bool anyTrue = false;
		for (int i = 0; i < 4; i++) {
			Input& ai = inputs[A1_INPUT + 2 * i];
			Input& bi = inputs[B1_INPUT + 2 * i];
			int channels = std::max(ai.getChannels(), bi.getChannels());
			if (channels < 1)
				channels = 1;
			int fn = (int)std::round(params[FN1_PARAM + i].getValue());
			fn = clamp(fn, 0, kNumFn - 1);

			for (int c = 0; c < channels; c++) {
				float av = ai.isConnected() ? ai.getPolyVoltage(c) : refV;
				float bv = bi.isConnected() ? bi.getPolyVoltage(c) : refV;
				bool a = aGate[i].sense(c, av, 1.f, 2.f);
				bool b = bGate[i].sense(c, bv, 1.f, 2.f);
				bool r = evalGate(fn, a, b);
				outputs[OUT1_OUTPUT + i].setVoltage(r ? 10.f : 0.f, c);
				if (c == 0)
					dispGateOut[i] = r;
			}
			outputs[OUT1_OUTPUT + i].setChannels(channels);
			anyTrue = anyTrue || dispGateOut[i];
		}

		// --- REFERRAL: two mono gated analogue switches -----------------------
		bool hiOn = (int)std::round(params[POLARITY_PARAM].getValue()) == kPolarityHiOn;
		bool routeAC = (int)std::round(params[ROUTE_PARAM].getValue()) == kRouteAC;
		bool anyActive = false;
		{
			int gateInputs[2]  = { SW1_GATE_INPUT, SW2_GATE_INPUT };
			int aInputs[2]     = { SW1_A_INPUT, SW2_A_INPUT };
			int bOutputs[2]    = { SW1_B_OUTPUT, SW2_B_OUTPUT };
			int cOutputs[2]    = { SW1_C_OUTPUT, SW2_C_OUTPUT };
			for (int n = 0; n < 2; n++) {
				bool gateHigh = swGate[n].sense(inputs[gateInputs[n]].getVoltage(), 0.1f, 1.f);
				bool on = hiOn ? gateHigh : !gateHigh;
				float target = on ? 1.f : 0.f;
				if (declick) {
					float step = args.sampleTime / 0.001f;   // full swing in ~1 ms
					if (onGain[n] < target)      onGain[n] = std::min(target, onGain[n] + step);
					else if (onGain[n] > target)  onGain[n] = std::max(target, onGain[n] - step);
				}
				else {
					onGain[n] = target;
				}
				float a = clamp(inputs[aInputs[n]].getVoltage(), -12.f, 12.f);
				outputs[bOutputs[n]].setVoltage(clamp(a * onGain[n], -12.f, 12.f));
				outputs[cOutputs[n]].setVoltage(clamp(routeAC ? a * (1.f - onGain[n]) : 0.f, -12.f, 12.f));
				anyActive = anyActive || onGain[n] > 0.01f;
			}
		}

		// --- INSTALLMENTS: one free-running binary counter, six taps ---------
		bool clockConnected = inputs[CLOCK_INPUT].isConnected();
		if (resetTrig.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 2.f))
			divCounter = 0;
		if (clockConnected && clockTrig.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 2.f))
			divCounter++;
		bool musical = (int)std::round(params[DIVMODE_PARAM].getValue()) == kDivMusical;
		const int* taps = musical ? kMusicalTaps : kBinaryTaps;
		int divOutputs[6] = { DIV2_OUTPUT, DIV4_OUTPUT, DIV8_OUTPUT, DIV16_OUTPUT, DIV32_OUTPUT, DIV64_OUTPUT };
		for (int i = 0; i < 6; i++) {
			int n = taps[i];
			int phase = (int)(divCounter % (uint32_t)n);
			int high = (n + 1) / 2;
			bool g = phase < high;
			outputs[divOutputs[i]].setVoltage(g ? 10.f : 0.f);
			dispDivGate[i] = g;
		}
		dispClocked = clockConnected;

		// --- lights, at a reduced rate ----------------------------------------
		if (lightDivider.process()) {
			float dt = args.sampleTime * lightDivider.getDivision();
			lights[VERDICT_LIGHT].setBrightnessSmooth(anyTrue ? 1.f : 0.f, dt);
			for (int i = 0; i < 4; i++)
				lights[OUT1_LIGHT + i].setBrightnessSmooth(dispGateOut[i] ? 1.f : 0.f, dt);
			lights[ACTIVE_LIGHT].setBrightnessSmooth(anyActive ? 1.f : 0.f, dt);
			lights[SW1_GATE_LIGHT].setBrightness(onGain[0]);
			lights[SW2_GATE_LIGHT].setBrightness(onGain[1]);
			lights[CLOCKED_LIGHT].setBrightnessSmooth(dispClocked ? 1.f : 0.f, dt);
			for (int i = 0; i < 6; i++)
				lights[DIV2_LIGHT + i].setBrightnessSmooth(dispDivGate[i] ? 1.f : 0.f, dt);
		}
	}
};


// ---------------------------------------------------------------------------
// Look and feel comes entirely from src/PanelTheme.hpp and this panel's own
// generated Panel.hpp -- see ../../panelkit/README.md. Nothing about the
// widget art is written here.
typedef RoundBlackKnob AuditKnob;


struct AuditLogicWidget : ModuleWidget {
	AuditLogicWidget(AuditLogic* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/AuditLogic.svg")));

		panel::addScrews(this);
		panel::addLabels(this);

		// FINDINGS
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::A1_POS.x, panel::A1_POS.y), module, AuditLogic::A1_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::B1_POS.x, panel::B1_POS.y), module, AuditLogic::B1_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::A2_POS.x, panel::A2_POS.y), module, AuditLogic::A2_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::B2_POS.x, panel::B2_POS.y), module, AuditLogic::B2_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::A3_POS.x, panel::A3_POS.y), module, AuditLogic::A3_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::B3_POS.x, panel::B3_POS.y), module, AuditLogic::B3_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::A4_POS.x, panel::A4_POS.y), module, AuditLogic::A4_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::B4_POS.x, panel::B4_POS.y), module, AuditLogic::B4_INPUT));

		addParam(createParamCentered<AuditKnob>(panel::mm(panel::FN1_POS.x, panel::FN1_POS.y), module, AuditLogic::FN1_PARAM));
		addParam(createParamCentered<AuditKnob>(panel::mm(panel::FN2_POS.x, panel::FN2_POS.y), module, AuditLogic::FN2_PARAM));
		addParam(createParamCentered<AuditKnob>(panel::mm(panel::FN3_POS.x, panel::FN3_POS.y), module, AuditLogic::FN3_PARAM));
		addParam(createParamCentered<AuditKnob>(panel::mm(panel::FN4_POS.x, panel::FN4_POS.y), module, AuditLogic::FN4_PARAM));

		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::OUT1_POS.x, panel::OUT1_POS.y), module, AuditLogic::OUT1_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::OUT2_POS.x, panel::OUT2_POS.y), module, AuditLogic::OUT2_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::OUT3_POS.x, panel::OUT3_POS.y), module, AuditLogic::OUT3_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::OUT4_POS.x, panel::OUT4_POS.y), module, AuditLogic::OUT4_OUTPUT));

		addChild(createLightCentered<SmallLight<panel::PaperLight> >(panel::mm(panel::VERDICT_POS.x, panel::VERDICT_POS.y), module, AuditLogic::VERDICT_LIGHT));
		addChild(createLightCentered<SmallLight<panel::MintLight> >(panel::mm(panel::OUT1_LED_POS.x, panel::OUT1_LED_POS.y), module, AuditLogic::OUT1_LIGHT));
		addChild(createLightCentered<SmallLight<panel::MintLight> >(panel::mm(panel::OUT2_LED_POS.x, panel::OUT2_LED_POS.y), module, AuditLogic::OUT2_LIGHT));
		addChild(createLightCentered<SmallLight<panel::MintLight> >(panel::mm(panel::OUT3_LED_POS.x, panel::OUT3_LED_POS.y), module, AuditLogic::OUT3_LIGHT));
		addChild(createLightCentered<SmallLight<panel::MintLight> >(panel::mm(panel::OUT4_LED_POS.x, panel::OUT4_LED_POS.y), module, AuditLogic::OUT4_LIGHT));

		// REFERRAL
		addParam(createParamCentered<CKSS>(panel::mm(panel::POLARITY_POS.x, panel::POLARITY_POS.y), module, AuditLogic::POLARITY_PARAM));
		addParam(createParamCentered<CKSS>(panel::mm(panel::ROUTE_POS.x, panel::ROUTE_POS.y), module, AuditLogic::ROUTE_PARAM));

		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::SW1_A_POS.x, panel::SW1_A_POS.y), module, AuditLogic::SW1_A_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::SW1_GATE_POS.x, panel::SW1_GATE_POS.y), module, AuditLogic::SW1_GATE_INPUT));
		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::SW1_B_POS.x, panel::SW1_B_POS.y), module, AuditLogic::SW1_B_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::SW1_C_POS.x, panel::SW1_C_POS.y), module, AuditLogic::SW1_C_OUTPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::SW2_A_POS.x, panel::SW2_A_POS.y), module, AuditLogic::SW2_A_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::SW2_GATE_POS.x, panel::SW2_GATE_POS.y), module, AuditLogic::SW2_GATE_INPUT));
		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::SW2_B_POS.x, panel::SW2_B_POS.y), module, AuditLogic::SW2_B_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::SW2_C_POS.x, panel::SW2_C_POS.y), module, AuditLogic::SW2_C_OUTPUT));

		addChild(createLightCentered<SmallLight<panel::PaperLight> >(panel::mm(panel::ACTIVE_POS.x, panel::ACTIVE_POS.y), module, AuditLogic::ACTIVE_LIGHT));
		addChild(createLightCentered<SmallLight<panel::LimeLight> >(panel::mm(panel::SW1_GATE_LED_POS.x, panel::SW1_GATE_LED_POS.y), module, AuditLogic::SW1_GATE_LIGHT));
		addChild(createLightCentered<SmallLight<panel::LimeLight> >(panel::mm(panel::SW2_GATE_LED_POS.x, panel::SW2_GATE_LED_POS.y), module, AuditLogic::SW2_GATE_LIGHT));

		// INSTALLMENTS
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::CLOCK_IN_POS.x, panel::CLOCK_IN_POS.y), module, AuditLogic::CLOCK_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::RESET_IN_POS.x, panel::RESET_IN_POS.y), module, AuditLogic::RESET_INPUT));
		addParam(createParamCentered<CKSS>(panel::mm(panel::DIVMODE_POS.x, panel::DIVMODE_POS.y), module, AuditLogic::DIVMODE_PARAM));
		addParam(createParamCentered<CKSS>(panel::mm(panel::REFV_POS.x, panel::REFV_POS.y), module, AuditLogic::REFV_PARAM));

		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::DIV2_POS.x, panel::DIV2_POS.y), module, AuditLogic::DIV2_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::DIV4_POS.x, panel::DIV4_POS.y), module, AuditLogic::DIV4_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::DIV8_POS.x, panel::DIV8_POS.y), module, AuditLogic::DIV8_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::DIV16_POS.x, panel::DIV16_POS.y), module, AuditLogic::DIV16_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::DIV32_POS.x, panel::DIV32_POS.y), module, AuditLogic::DIV32_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::DIV64_POS.x, panel::DIV64_POS.y), module, AuditLogic::DIV64_OUTPUT));

		addChild(createLightCentered<SmallLight<panel::PaperLight> >(panel::mm(panel::CLOCKED_POS.x, panel::CLOCKED_POS.y), module, AuditLogic::CLOCKED_LIGHT));
		addChild(createLightCentered<SmallLight<panel::MintLight> >(panel::mm(panel::DIV2_LED_POS.x, panel::DIV2_LED_POS.y), module, AuditLogic::DIV2_LIGHT));
		addChild(createLightCentered<SmallLight<panel::MintLight> >(panel::mm(panel::DIV4_LED_POS.x, panel::DIV4_LED_POS.y), module, AuditLogic::DIV4_LIGHT));
		addChild(createLightCentered<SmallLight<panel::MintLight> >(panel::mm(panel::DIV8_LED_POS.x, panel::DIV8_LED_POS.y), module, AuditLogic::DIV8_LIGHT));
		addChild(createLightCentered<SmallLight<panel::MintLight> >(panel::mm(panel::DIV16_LED_POS.x, panel::DIV16_LED_POS.y), module, AuditLogic::DIV16_LIGHT));
		addChild(createLightCentered<SmallLight<panel::MintLight> >(panel::mm(panel::DIV32_LED_POS.x, panel::DIV32_LED_POS.y), module, AuditLogic::DIV32_LIGHT));
		addChild(createLightCentered<SmallLight<panel::MintLight> >(panel::mm(panel::DIV64_LED_POS.x, panel::DIV64_LED_POS.y), module, AuditLogic::DIV64_LIGHT));
	}

	void appendContextMenu(Menu* menu) override {
		AuditLogic* m = dynamic_cast<AuditLogic*>(module);
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Audit Logic"));

		menu->addChild(createBoolMenuItem("REFERRAL declick (1 ms crossfade)", "",
			[=]() { return m->declick; },
			[=](bool v) { m->declick = v; }));
	}
};


Model* modelAuditLogic = createModel<AuditLogic, AuditLogicWidget>("AuditLogic");

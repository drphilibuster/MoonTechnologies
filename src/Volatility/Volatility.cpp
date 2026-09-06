#include "../plugin.hpp"
#include "Panel.hpp"
#include <cmath>

// ---------------------------------------------------------------------------
// Volatility -- the Modular in a Week random trio, consolidated: the 4006
// shift-register noise generator, Rene Schmitz's YASH sample and hold, and
// the PHObos random gate. All three want a clock, so they share one: RATE and
// CLOCK IN live in NOISE (its own character is what actually depends on
// tempo), and SAMPLE & HOLD's TRIG and RND GATE's own draw both fall back to
// that same edge when unpatched. CLOCK OUT mirrors whichever is active.
//
// NOISE is an 18-stage CD4006-style LFSR -- a real static shift register IC
// wired with two XOR feedback taps for a maximal-length sequence, per the
// two-tap table for n=18 (Xilinx XAPP 052): taps at bit 18 and bit 11,
// 1-indexed from the input end. Clocked at audio rates the bitstream reads as
// white-ish digital noise; clocked at a crawl, the same bit held between
// edges reads as a random gate -- one circuit, two characters, purely a
// function of RATE. DAC OUT reads an 8-bit window of the 18-bit register as a
// stepped random CV; BITS picks where that window starts.
//
// SAMPLE & HOLD's SRC normals to the module's own continuous white noise (the
// same signal NOISE OUT carries) and TRIG to the shared clock, so it does
// something useful unpatched; patching either overrides it. RND GATE throws
// PROBABILITY odds on every shared clock edge, either against its own RNG or
// (SRC: Ext) a voltage compared to the same odds -- the classic noise-and-
// comparator random gate, given a patchable source instead of a fixed one.

namespace volatility {

static const uint32_t kLfsrMask = 0x0003FFFFu;    // 18 bits
static const uint32_t kLfsrSeed = 0x00015555u;    // any nonzero 18-bit value

static const float kRateMinHz = 0.05f;
static const float kRateMaxHz = 4000.f;
static const float kMaxSlewSec = 0.25f;
static const float kDroopTauSec = 4.f;

} // namespace volatility


struct Volatility : Module {
	enum ParamId {
		RATE_PARAM, BITS_PARAM, SH_SLEW_PARAM, PROBABILITY_PARAM, RND_SRC_PARAM,
		PROB_CV_AMT_PARAM, PARAMS_LEN
	};
	enum InputId {
		CLOCK_IN_INPUT, SH_SRC_IN_INPUT, SH_TRIG_IN_INPUT, RND_SRC_IN_INPUT,
		PROB_CV_IN_INPUT, INPUTS_LEN
	};
	enum OutputId {
		RND_OUT_OUTPUT, DAC_OUT_OUTPUT, SH_OUT_OUTPUT, GATE_OUT_OUTPUT,
		NOISE_OUT_OUTPUT, CLOCK_OUT_OUTPUT, OUTPUTS_LEN
	};
	enum LightId { LIGHTS_LEN };

	// --- shared clock ---
	float internalPhase = 0.f;
	dsp::SchmittTrigger clockInTrig;
	dsp::PulseGenerator clockOutPulse;

	// --- NOISE: the 4006 LFSR ---
	uint32_t lfsrReg = volatility::kLfsrSeed;
	float rndOutV = -5.f;
	float dacOutV = 0.f;

	// --- SAMPLE & HOLD, polyphonic on SRC IN ---
	dsp::SchmittTrigger shTrigSchmitt;
	float shHeld[PORT_MAX_CHANNELS] = {};
	float shSlewState[PORT_MAX_CHANNELS] = {};
	bool droop = false;

	// --- RND GATE ---
	bool rndGateState = false;
	bool toggleMode = false;

	Volatility() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

		configParam(RATE_PARAM, 0.f, 1.f, 0.5f, "Clock rate");
		configParam(BITS_PARAM, 0.f, 1.f, 0.f, "DAC window", "", 0.f, 10.f);
		configParam(SH_SLEW_PARAM, 0.f, 1.f, 0.f, "Sample & hold slew", "%", 0.f, 100.f);
		configParam(PROBABILITY_PARAM, 0.f, 1.f, 0.5f, "Probability", "%", 0.f, 100.f);
		std::vector<std::string> srcLabels;
		srcLabels.push_back("Int");
		srcLabels.push_back("Ext");
		configSwitch(RND_SRC_PARAM, 0.f, 1.f, 0.f, "Random gate source", srcLabels);
		configParam(PROB_CV_AMT_PARAM, -1.f, 1.f, 0.f, "Probability CV amount", "%", 0.f, 100.f);
		getParamQuantity(PROB_CV_AMT_PARAM)->randomizeEnabled = false;

		configInput(CLOCK_IN_INPUT, "Clock (overrides RATE)");
		configInput(SH_SRC_IN_INPUT, "Sample & hold source (normalled to noise)");
		configInput(SH_TRIG_IN_INPUT, "Sample & hold trigger (normalled to clock)");
		configInput(RND_SRC_IN_INPUT, "Random gate external source");
		configInput(PROB_CV_IN_INPUT, "Probability CV");

		configOutput(RND_OUT_OUTPUT, "LFSR bitstream, +/-5 V");
		configOutput(DAC_OUT_OUTPUT, "LFSR stepped random CV");
		configOutput(SH_OUT_OUTPUT, "Sample & hold");
		configOutput(GATE_OUT_OUTPUT, "Random gate");
		configOutput(NOISE_OUT_OUTPUT, "White noise");
		configOutput(CLOCK_OUT_OUTPUT, "Clock (mirrors active source)");
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		internalPhase = 0.f;
		clockInTrig.reset();
		clockOutPulse.reset();
		lfsrReg = volatility::kLfsrSeed;
		rndOutV = -5.f;
		dacOutV = 0.f;
		shTrigSchmitt.reset();
		for (int c = 0; c < PORT_MAX_CHANNELS; c++) {
			shHeld[c] = 0.f;
			shSlewState[c] = 0.f;
		}
		rndGateState = false;
	}

	void process(const ProcessArgs& args) override {
		float sampleTime = args.sampleTime;

		// --- the shared clock --------------------------------------------------
		bool clockEdge;
		float clockOutV;
		if (inputs[CLOCK_IN_INPUT].isConnected()) {
			clockEdge = clockInTrig.process(inputs[CLOCK_IN_INPUT].getVoltage(), 0.1f, 2.f);
			if (clockEdge)
				clockOutPulse.trigger(1e-3f);
			clockOutV = clockOutPulse.process(sampleTime) ? 10.f : 0.f;
		}
		else {
			float freqHz = volatility::kRateMinHz
			               * std::pow(volatility::kRateMaxHz / volatility::kRateMinHz,
			                          params[RATE_PARAM].getValue());
			internalPhase += freqHz * sampleTime;
			clockEdge = false;
			if (internalPhase >= 1.f) {
				internalPhase -= 1.f;
				clockEdge = true;
			}
			clockOutV = (internalPhase < 0.5f) ? 10.f : 0.f;
		}
		outputs[CLOCK_OUT_OUTPUT].setVoltage(clockOutV);

		// A single continuous white noise source, shared by NOISE OUT and
		// SAMPLE & HOLD's normalled SRC.
		float whiteNoise = (random::uniform() * 2.f - 1.f) * 5.f;
		outputs[NOISE_OUT_OUTPUT].setVoltage(whiteNoise);

		// --- NOISE: the 4006 LFSR, clocked on every shared edge -----------------
		if (clockEdge) {
			// Two-tap maximal-length feedback for an 18-bit register: bit 18
			// XOR bit 11 (1-indexed from the input end, i.e. bit indices 17
			// and 10 here), fed back into the input.
			uint32_t fb = ((lfsrReg >> 17) ^ (lfsrReg >> 10)) & 1u;
			lfsrReg = ((lfsrReg << 1) | fb) & volatility::kLfsrMask;
			rndOutV = fb ? 5.f : -5.f;

			int offset = clamp((int) std::round(params[BITS_PARAM].getValue() * 10.f), 0, 10);
			uint32_t window = (lfsrReg >> offset) & 0xFFu;
			dacOutV = (window / 255.f) * 10.f;
		}
		outputs[RND_OUT_OUTPUT].setVoltage(rndOutV);
		outputs[DAC_OUT_OUTPUT].setVoltage(dacOutV);

		// --- SAMPLE & HOLD, polyphonic on its own SRC IN ------------------------
		bool srcConnected = inputs[SH_SRC_IN_INPUT].isConnected();
		int shChannels = std::max(srcConnected ? inputs[SH_SRC_IN_INPUT].getChannels() : 1, 1);

		bool trigConnected = inputs[SH_TRIG_IN_INPUT].isConnected();
		bool shTrigEdge = trigConnected
			? shTrigSchmitt.process(inputs[SH_TRIG_IN_INPUT].getVoltage(), 0.1f, 1.f)
			: clockEdge;

		float slewSec = params[SH_SLEW_PARAM].getValue() * volatility::kMaxSlewSec;
		float slewCoef = (slewSec > 1e-4f) ? clamp(sampleTime / slewSec, 0.f, 1.f) : 1.f;
		float droopCoef = sampleTime / volatility::kDroopTauSec;

		for (int c = 0; c < shChannels; c++) {
			float src = srcConnected ? inputs[SH_SRC_IN_INPUT].getPolyVoltage(c) : whiteNoise;
			if (shTrigEdge)
				shHeld[c] = src;
			// The FET S&H leaks: held voltage creeps back toward 0 V.
			if (droop)
				shHeld[c] += (0.f - shHeld[c]) * droopCoef;
			shSlewState[c] += slewCoef * (shHeld[c] - shSlewState[c]);
			outputs[SH_OUT_OUTPUT].setVoltage(clamp(shSlewState[c], -12.f, 12.f), c);
		}
		outputs[SH_OUT_OUTPUT].setChannels(shChannels);

		// --- RND GATE: a coin flip on every shared clock edge -------------------
		float prob = params[PROBABILITY_PARAM].getValue();
		if (inputs[PROB_CV_IN_INPUT].isConnected())
			prob += params[PROB_CV_AMT_PARAM].getValue() * (inputs[PROB_CV_IN_INPUT].getVoltage() / 10.f);
		prob = clamp(prob, 0.f, 1.f);

		bool extSrc = params[RND_SRC_PARAM].getValue() > 0.5f;
		if (clockEdge) {
			float draw;
			if (extSrc) {
				float srcV = inputs[RND_SRC_IN_INPUT].isConnected()
					? inputs[RND_SRC_IN_INPUT].getVoltage() : whiteNoise;
				draw = clamp(srcV / 10.f + 0.5f, 0.f, 1.f);
			}
			else {
				draw = random::uniform();
			}
			bool success = draw < prob;
			if (toggleMode) {
				if (success)
					rndGateState = !rndGateState;
			}
			else {
				rndGateState = success;
			}
		}
		outputs[GATE_OUT_OUTPUT].setVoltage(rndGateState ? 10.f : 0.f);
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "droop", json_boolean(droop));
		json_object_set_new(root, "toggleMode", json_boolean(toggleMode));
		return root;
	}

	void dataFromJson(json_t* root) override {
		json_t* j;
		j = json_object_get(root, "droop");
		if (j) droop = json_boolean_value(j);
		j = json_object_get(root, "toggleMode");
		if (j) toggleMode = json_boolean_value(j);
	}
};


// ---------------------------------------------------------------------------
// Look and feel. The palette, the shared hardware and the entire silkscreen
// come from src/PanelTheme.hpp, generated by tools/panels/Volatility.py --
// see ../../panelkit/README.md. Nothing about the look is written here.

struct VolatilityWidget : ModuleWidget {
	VolatilityWidget(Volatility* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Volatility.svg")));

		panel::addScrews(this);
		panel::addLabels(this);

		addParam(createParamCentered<RoundBlackKnob>(panel::mm(panel::RATE_POS.x, panel::RATE_POS.y), module, Volatility::RATE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(panel::mm(panel::BITS_POS.x, panel::BITS_POS.y), module, Volatility::BITS_PARAM));
		addParam(createParamCentered<Trimpot>(panel::mm(panel::SH_SLEW_POS.x, panel::SH_SLEW_POS.y), module, Volatility::SH_SLEW_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(panel::mm(panel::PROBABILITY_POS.x, panel::PROBABILITY_POS.y), module, Volatility::PROBABILITY_PARAM));
		addParam(createParamCentered<CKSS>(panel::mm(panel::RND_SRC_POS.x, panel::RND_SRC_POS.y), module, Volatility::RND_SRC_PARAM));
		addParam(createParamCentered<Trimpot>(panel::mm(panel::PROB_CV_AMT_POS.x, panel::PROB_CV_AMT_POS.y), module, Volatility::PROB_CV_AMT_PARAM));

		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::CLOCK_IN_POS.x, panel::CLOCK_IN_POS.y), module, Volatility::CLOCK_IN_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::SH_SRC_IN_POS.x, panel::SH_SRC_IN_POS.y), module, Volatility::SH_SRC_IN_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::SH_TRIG_IN_POS.x, panel::SH_TRIG_IN_POS.y), module, Volatility::SH_TRIG_IN_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::RND_SRC_IN_POS.x, panel::RND_SRC_IN_POS.y), module, Volatility::RND_SRC_IN_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::PROB_CV_IN_POS.x, panel::PROB_CV_IN_POS.y), module, Volatility::PROB_CV_IN_INPUT));

		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::RND_OUT_POS.x, panel::RND_OUT_POS.y), module, Volatility::RND_OUT_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::DAC_OUT_POS.x, panel::DAC_OUT_POS.y), module, Volatility::DAC_OUT_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::SH_OUT_POS.x, panel::SH_OUT_POS.y), module, Volatility::SH_OUT_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::GATE_OUT_POS.x, panel::GATE_OUT_POS.y), module, Volatility::GATE_OUT_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::NOISE_OUT_POS.x, panel::NOISE_OUT_POS.y), module, Volatility::NOISE_OUT_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::CLOCK_OUT_POS.x, panel::CLOCK_OUT_POS.y), module, Volatility::CLOCK_OUT_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		Volatility* m = dynamic_cast<Volatility*>(module);
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Volatility"));

		menu->addChild(createBoolMenuItem("Sample & hold droop", "",
			[=]() { return m->droop; },
			[=](bool v) { m->droop = v; }));

		menu->addChild(createBoolMenuItem("Random gate: toggle mode", "",
			[=]() { return m->toggleMode; },
			[=](bool v) { m->toggleMode = v; }));
	}
};


Model* modelVolatility = createModel<Volatility, VolatilityWidget>("Volatility");

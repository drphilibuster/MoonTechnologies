#include "../plugin.hpp"
#include "Panel.hpp"
#include "Noise.hpp"
#include "Yash.hpp"
#include "../Lf398.hpp"
#include <cmath>

// ---------------------------------------------------------------------------
// Volatility -- the Modular in a Week random trio, consolidated: the 4006
// shift-register noise generator, Rene Schmitz's YASH sample and hold (an LF398
// with its trigger-pulse circuit; see below), and the PHObos random gate. All
// three want a clock, so they share one: RATE and
// CLOCK IN live in NOISE (its own character is what actually depends on
// tempo), and SAMPLE & HOLD's TRIG and RND GATE's own draw both fall back to
// that same edge when unpatched. CLOCK OUT mirrors whichever is active.
//
// NOISE is one CD4006B wired as an 18-stage shift register closed through two XOR
// gates and an inverter -- the noise source of Yusynth's Random Eight Pole Gate
// Switch: new bit = NOT(stage 18 XOR stage 5) XOR stage 9, on the chip's pins 13,
// 9 and 10. All 18 stages are in play and the period is 2^18 - 4 (see
// src/Volatility/Noise.hpp for the trace). It shifts on the negative-going clock edge, as the
// datasheet has it. Clocked at audio rates the bitstream reads as white-ish
// digital noise; clocked at a crawl, the same bit held between edges reads as a
// random gate -- one circuit, and the clock rate is the only thing that decides
// which. DAC OUT reads an 8-bit window of the register as a stepped random CV
// (the window's stages are inside the chip with no pins: that read is this
// module's addition); BITS picks where it starts.

// SAMPLE & HOLD is Schmitz's YASH (1999): an LF398 with a 1 nF hold capacitor,
// sampled by a ~3 us pulse that a BC548 inverter, three CD4093 Schmitt gates and
// a 470 pF / 10k differentiator make from the trigger (src/Volatility/Yash.hpp,
// src/Lf398.hpp, src/Cd4093.hpp). Each sample is therefore the input a few
// microseconds after the trigger, run through the chip's acquisition lag, not an
// ideal instant read; the held value droops by the chip's leakage, steps by its
// hold step, and carries its offset and feedthrough. With nothing in TRIG the
// circuit's own free-running 4093 oscillator can be the trigger (menu).
//
// SAMPLE & HOLD's SRC normals to the module's own continuous white noise (the
// same signal NOISE OUT carries) and TRIG to the shared clock, so it does
// something useful unpatched; patching either overrides it. RND GATE throws
// PROBABILITY odds on every shared clock edge, either against its own RNG or
// (SRC: Ext) a voltage compared to the same odds -- the classic noise-and-
// comparator random gate, given a patchable source instead of a fixed one.

namespace volatility {

static const float kRateMinHz = 0.05f;
static const float kRateMaxHz = 4000.f;
static const float kMaxSlewSec = 0.25f;

} // namespace volatility


struct Volatility : Module {
	enum ParamId {
		RATE_PARAM, BITS_PARAM, SH_SLEW_PARAM, PROBABILITY_PARAM, RND_SRC_PARAM,
		PROB_CV_AMT_PARAM,
		COLOR_PARAM, LENGTH_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		CLOCK_IN_INPUT, SH_SRC_IN_INPUT, SH_TRIG_IN_INPUT, RND_SRC_IN_INPUT,
		PROB_CV_IN_INPUT,
		RATE_CV_IN_INPUT, BITS_CV_IN_INPUT,
		INPUTS_LEN
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
	volatility::Register4006 lfsr;
	volatility::FallingEdge clockFall;
	dsp::SchmittTrigger clockInFallTrig;
	float rndOutV = -5.f;
	float dacOutV = 0.f;

	// --- SAMPLE & HOLD, polyphonic on SRC IN ---
	lf398::Channel shChip[PORT_MAX_CHANNELS];
	float shSlewState[PORT_MAX_CHANNELS] = {};
	float shPrevSrc[PORT_MAX_CHANNELS] = {};
	int shChannelsPrev = 0;
	// The YASH trigger path and its LF398. "droop" is the old menu option and now means the
	// chip's leakage current (off = an ideal hold); the rest of the chip's imperfections are its
	// datasheet's and always present.
	bool droop = false;
	int chipGrade = 0;            // 0 = LF398 typical, 1 = LF398 worst case (datasheet maxima)
	bool droopDown = false;       // leakage direction; the datasheet gives only a magnitude
	bool yashOsc = false;         // TRIG unpatched: false = shared clock, true = YASH's 4093 oscillator
	yash::TrigInput shTrigIn;     // the BC548 stage, on the TRIG jack
	yash::TrigInput shClockIn;    // the same stage seeing CLOCK IN, when TRIG is normalled to it
	float shPrevClockV = 0.f;
	yash::PulseChain shChain;
	yash::Oscillator shOsc;
	lf398::Spec shSpec;
	int shTrigKind = -1;          // which source drove the chain last step, to reset on a change
	float shPrevTrigV = 0.f;

	// --- RND GATE ---
	bool rndGateState = false;
	bool toggleMode = false;
	volatility::GateStretcher gate;

	// --- the noise colours, shared by NOISE OUT and both normalled sources ---
	volatility::NoiseColours colours;

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
		configParam(COLOR_PARAM, 0.f, 1.f, 0.f, "Noise colour", "%", 0.f, 100.f);
		configParam(LENGTH_PARAM, 0.f, 1.f, 1.f, "Random gate length", "%", 0.f, 100.f);
		getParamQuantity(PROB_CV_AMT_PARAM)->randomizeEnabled = false;

		configInput(CLOCK_IN_INPUT, "Clock (overrides RATE)");
		configInput(SH_SRC_IN_INPUT, "Sample & hold source (normalled to noise)");
		configInput(SH_TRIG_IN_INPUT, "Sample & hold trigger (normalled to clock)");
		configInput(RND_SRC_IN_INPUT, "Random gate external source");
		configInput(PROB_CV_IN_INPUT, "Probability CV");
		configInput(RATE_CV_IN_INPUT, "Clock rate CV, 1 V/octave");
		configInput(BITS_CV_IN_INPUT, "DAC window CV");

		configOutput(RND_OUT_OUTPUT, "LFSR bitstream, +/-5 V");
		configOutput(DAC_OUT_OUTPUT, "LFSR stepped random CV");
		configOutput(SH_OUT_OUTPUT, "Sample & hold");
		configOutput(GATE_OUT_OUTPUT, "Random gate");
		configOutput(NOISE_OUT_OUTPUT, "Noise, white through pink to red");
		configOutput(CLOCK_OUT_OUTPUT, "Clock (mirrors active source)");

		colours.build(APP->engine->getSampleRate());
		refreshChip();
	}

	/** The LF398's datasheet column for the chosen grade and leakage direction. The hold
	    capacitor is the YASH's 1 nF; the junction sits above a 25 C ambient by the chip's own
	    dissipation (src/Lf398.hpp). */
	void refreshChip() {
		shSpec = lf398::spec(chipGrade != 0, 1e-9, droop, droopDown ? -1.0 : 1.0, 25.0, true);
		shChain.corner = shOsc.corner = chipGrade != 0 ? cd4093::CORNER_MAX : cd4093::CORNER_TYP;
		shTrigIn.th = shClockIn.th = yash::trigThresholds(shChain.corner);
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		colours.build(e.sampleRate);
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		internalPhase = 0.f;
		clockInTrig.reset();
		clockOutPulse.reset();
		lfsr.reset();
		clockFall.reset();
		clockInFallTrig.reset();
		rndOutV = -5.f;
		dacOutV = 0.f;
		for (int c = 0; c < PORT_MAX_CHANNELS; c++) {
			shChip[c].reset();
			shSlewState[c] = 0.f;
			shPrevSrc[c] = 0.f;
		}
		shChannelsPrev = 0;
		shTrigIn.reset();
		shClockIn.reset();
		shPrevClockV = 0.f;
		shChain.reset();
		shOsc.reset();
		shTrigKind = -1;
		shPrevTrigV = 0.f;
		rndGateState = false;
		gate.reset();
		colours.reset();
	}

	void process(const ProcessArgs& args) override {
		float sampleTime = args.sampleTime;

		// --- the shared clock --------------------------------------------------
		bool clockEdge;
		bool clockFell;
		float clockOutV;
		// Where inside this sample the clock's edges fell, for the sample and hold's
		// trigger path (seconds from the start of the sample; -1 = none). The internal
		// clock knows exactly; a clock from the jack is only known at the samples.
		float clockRiseAt = -1.f;
		float clockFallAt = -1.f;
		bool clockExt = false;
		float clockVExt = 0.f;
		if (inputs[CLOCK_IN_INPUT].isConnected()) {
			float clockV = inputs[CLOCK_IN_INPUT].getVoltage();
			clockExt = true;
			clockVExt = clockV;
			clockEdge = clockInTrig.process(clockV, 0.1f, 2.f);
			// The CD4006 moves on the negative-going edge: the same thresholds,
			// seen from the other side.
			clockFell = clockInFallTrig.process(-clockV, -2.f, -0.1f);
			if (clockEdge)
				clockOutPulse.trigger(1e-3f);
			clockOutV = clockOutPulse.process(sampleTime) ? 10.f : 0.f;
		}
		else {
			float freqHz = volatility::kRateMinHz
			               * std::pow(volatility::kRateMaxHz / volatility::kRateMinHz,
			                          params[RATE_PARAM].getValue());
			// RATE CV is 1 V/octave, the convention the knob's own log taper
			// already follows, so a pitch source drives the clock as a pitch.
			if (inputs[RATE_CV_IN_INPUT].isConnected())
				freqHz *= dsp::exp2_taylor5(clamp(inputs[RATE_CV_IN_INPUT].getVoltage(),
				                                  -10.f, 10.f));
			freqHz = clamp(freqHz, volatility::kRateMinHz * 0.1f,
			               volatility::kRateMaxHz * 2.f);
			internalPhase += freqHz * sampleTime;
			clockEdge = false;
			if (internalPhase >= 1.f) {
				internalPhase -= 1.f;
				clockEdge = true;
				clockRiseAt = clamp(sampleTime - internalPhase / freqHz, 0.f, sampleTime);
			}
			clockOutV = (internalPhase < 0.5f) ? 10.f : 0.f;
			clockFell = clockFall.process(internalPhase < 0.5f);
			if (clockFell)
				clockFallAt = clamp(sampleTime - (internalPhase - 0.5f) / freqHz, 0.f, sampleTime);
		}
		outputs[CLOCK_OUT_OUTPUT].setVoltage(clockOutV);

		// A single continuous noise source, shared by NOISE OUT and by both
		// normalled inputs -- SAMPLE & HOLD's SRC and RND GATE's SRC IN. COLOUR
		// moves all three together, which is the point: sampling red noise is a
		// smooth random walk where sampling white is a jump, and neither is
		// reachable if the colour only applied to the jack on the footer.
		float white = (random::uniform() * 2.f - 1.f) * 5.f;
		float pink, red;
		colours.step(white, pink, red);
		float noise = volatility::colourMix(white, pink, red,
		                                    params[COLOR_PARAM].getValue());
		// Matched by RMS rather than by peak: pink and red have the higher
		// crest factor, so they reach past +/-10 V now and then and are held
		// to Rack's rail.
		noise = clamp(noise, -12.f, 12.f);
		outputs[NOISE_OUT_OUTPUT].setVoltage(noise);

		// --- NOISE: the 4006 LFSR, clocked on every shared falling edge ---------
		// The chip shifts on the negative-going transition of its clock, half a
		// cycle after the rising edge the sample & hold and the random gate use.
		if (clockFell) {
			bool fb = lfsr.clockFalling();
			rndOutV = fb ? 5.f : -5.f;

			float bitsN = params[BITS_PARAM].getValue();
			if (inputs[BITS_CV_IN_INPUT].isConnected())
				bitsN += inputs[BITS_CV_IN_INPUT].getVoltage() / 10.f;
			int offset = clamp((int) std::round(clamp(bitsN, 0.f, 1.f) * 10.f), 0, 10);
			uint32_t window = (lfsr.stages() >> offset) & 0xFFu;
			dacOutV = (window / 255.f) * 10.f;
		}
		outputs[RND_OUT_OUTPUT].setVoltage(rndOutV);
		outputs[DAC_OUT_OUTPUT].setVoltage(dacOutV);

		// --- SAMPLE & HOLD, polyphonic on its own SRC IN ------------------------
		bool srcConnected = inputs[SH_SRC_IN_INPUT].isConnected();
		int shChannels = std::max(srcConnected ? inputs[SH_SRC_IN_INPUT].getChannels() : 1, 1);

		// The trigger path is the YASH's: a BC548 stage, three 4093 gates and a
		// differentiator that turn the trigger's rising edge into a ~3 us pulse on
		// the LF398's logic pin. Where the edge fell inside this sample is kept
		// (straight-line between the two samples for a patched jack, exactly for
		// the internal clock and the oscillator), so the pulse lands at its true
		// time and the chip reads the input then, not at the sample boundary.
		bool trigConnected = inputs[SH_TRIG_IN_INPUT].isConnected();
		int trigKind = trigConnected ? 0 : (yashOsc ? 2 : 1);
		double dt = sampleTime;
		double trigV = trigConnected ? inputs[SH_TRIG_IN_INPUT].getVoltage() : 0.f;
		if (trigKind != shTrigKind) {
			// A source change must not read as an edge.
			shTrigKind = trigKind;
			shTrigIn.on = trigV >= shTrigIn.th.on;
			shClockIn.on = clockVExt >= shClockIn.th.on;
			shPrevTrigV = trigV;
			shPrevClockV = clockVExt;
			shOsc.reset();
		}
		{
			double et[2];
			int ed[2];
			int ne = 0;
			if (trigKind == 0) {
				ne = shTrigIn.process(shPrevTrigV, trigV, dt, et, ed);
			}
			else if (trigKind == 2) {
				// Pot: RATE sets the 1 Meg pot (clockwise = less resistance = faster); RATE CV
				// is 1 V/octave on the loop resistance. A module addition: the circuit has
				// neither. The loop resistance never goes below the pot's own 2k2.
				double potFrac = 1.0 - params[RATE_PARAM].getValue();
				double r = yash::Oscillator::seriesR(potFrac);
				if (inputs[RATE_CV_IN_INPUT].isConnected())
					r /= std::pow(2.0, clamp(inputs[RATE_CV_IN_INPUT].getVoltage(), -10.f, 10.f));
				r = std::min(std::max(r, (double) yash::kRoscFixed), (double) (yash::kRoscFixed + yash::kPot));
				int n = shOsc.step(dt, r);
				for (int k = 0; k < n && k < 2; k++) { et[ne] = shOsc.a.edges[k].at; ed[ne] = shOsc.a.edges[k].dir; ne++; }
			}
			else if (clockExt) {
				ne = shClockIn.process(shPrevClockV, clockVExt, dt, et, ed);
			}
			else {
				if (clockRiseAt >= 0.f) { et[ne] = clockRiseAt; ed[ne] = 1; ne++; }
				if (clockFallAt >= 0.f) { et[ne] = clockFallAt; ed[ne] = -1; ne++; }
			}
			for (int k = 0; k < ne; k++)
				shChain.pushTrigger(et[k], ed[k]);
			shPrevTrigV = trigV;
			shPrevClockV = clockVExt;
		}
		yash::Pulse pulses[4];
		lf398::Window win[4];
		int np = shChain.advance(dt, pulses, 4);
		for (int k = 0; k < np; k++) { win[k].a = pulses[k].a; win[k].b = pulses[k].b; }

		float slewSec = params[SH_SLEW_PARAM].getValue() * volatility::kMaxSlewSec;
		float slewCoef = (slewSec > 1e-4f) ? clamp(sampleTime / slewSec, 0.f, 1.f) : 1.f;

		for (int c = 0; c < shChannels; c++) {
			float src = srcConnected ? inputs[SH_SRC_IN_INPUT].getPolyVoltage(c) : noise;
			if (c >= shChannelsPrev)
				shPrevSrc[c] = src;   // a channel just switched on has no earlier sample
			float held = (float) shChip[c].step(shSpec, dt, shPrevSrc[c], src, win, np);
			shPrevSrc[c] = src;
			shSlewState[c] += slewCoef * (held - shSlewState[c]);
			outputs[SH_OUT_OUTPUT].setVoltage(clamp(shSlewState[c], -12.f, 12.f), c);
		}
		shChannelsPrev = shChannels;
		outputs[SH_OUT_OUTPUT].setChannels(shChannels);

		// --- RND GATE: a coin flip on every shared clock edge -------------------
		float prob = params[PROBABILITY_PARAM].getValue();
		if (inputs[PROB_CV_IN_INPUT].isConnected())
			prob += params[PROB_CV_AMT_PARAM].getValue() * (inputs[PROB_CV_IN_INPUT].getVoltage() / 10.f);
		prob = clamp(prob, 0.f, 1.f);

		bool extSrc = params[RND_SRC_PARAM].getValue() > 0.5f;
		gate.tick(sampleTime, clockEdge);
		if (clockEdge) {
			float draw;
			if (extSrc) {
				float srcV = inputs[RND_SRC_IN_INPUT].isConnected()
					? inputs[RND_SRC_IN_INPUT].getVoltage() : noise;
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
			else if (success) {
				gate.fire(params[LENGTH_PARAM].getValue());
			}
		}
		// TOGGLE holds a state between successes, so LENGTH has nothing to
		// shorten there and is bypassed rather than quietly ignored.
		bool gateHigh = toggleMode ? rndGateState : gate.high();
		outputs[GATE_OUT_OUTPUT].setVoltage(gateHigh ? 10.f : 0.f);
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "droop", json_boolean(droop));
		json_object_set_new(root, "chipGrade", json_integer(chipGrade));
		json_object_set_new(root, "droopDown", json_boolean(droopDown));
		json_object_set_new(root, "yashOsc", json_boolean(yashOsc));
		json_object_set_new(root, "toggleMode", json_boolean(toggleMode));
		return root;
	}

	void dataFromJson(json_t* root) override {
		json_t* j;
		j = json_object_get(root, "droop");
		if (j) droop = json_boolean_value(j);
		j = json_object_get(root, "chipGrade");
		if (j) chipGrade = json_integer_value(j) != 0 ? 1 : 0;
		j = json_object_get(root, "droopDown");
		if (j) droopDown = json_boolean_value(j);
		j = json_object_get(root, "yashOsc");
		if (j) yashOsc = json_boolean_value(j);
		refreshChip();
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
		addParam(createParamCentered<RoundBlackKnob>(panel::mm(panel::COLOR_POS.x, panel::COLOR_POS.y), module, Volatility::COLOR_PARAM));
		addParam(createParamCentered<Trimpot>(panel::mm(panel::SH_SLEW_POS.x, panel::SH_SLEW_POS.y), module, Volatility::SH_SLEW_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(panel::mm(panel::PROBABILITY_POS.x, panel::PROBABILITY_POS.y), module, Volatility::PROBABILITY_PARAM));
		addParam(createParamCentered<CKSS>(panel::mm(panel::RND_SRC_POS.x, panel::RND_SRC_POS.y), module, Volatility::RND_SRC_PARAM));
		addParam(createParamCentered<Trimpot>(panel::mm(panel::PROB_CV_AMT_POS.x, panel::PROB_CV_AMT_POS.y), module, Volatility::PROB_CV_AMT_PARAM));
		addParam(createParamCentered<Trimpot>(panel::mm(panel::LENGTH_POS.x, panel::LENGTH_POS.y), module, Volatility::LENGTH_PARAM));

		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::CLOCK_IN_POS.x, panel::CLOCK_IN_POS.y), module, Volatility::CLOCK_IN_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::RATE_CV_IN_POS.x, panel::RATE_CV_IN_POS.y), module, Volatility::RATE_CV_IN_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::BITS_CV_IN_POS.x, panel::BITS_CV_IN_POS.y), module, Volatility::BITS_CV_IN_INPUT));
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

		menu->addChild(createBoolMenuItem("Sample & hold droop (LF398 leakage)", "",
			[=]() { return m->droop; },
			[=](bool v) { m->droop = v; m->refreshChip(); }));

		menu->addChild(createIndexSubmenuItem("Sample & hold chip",
			{"LF398 typical", "LF398 worst case (datasheet maxima)"},
			[=]() { return (size_t) m->chipGrade; },
			[=](size_t i) { m->chipGrade = (int) i; m->refreshChip(); }));

		menu->addChild(createIndexSubmenuItem("Droop direction",
			{"Up (leakage into the hold capacitor)", "Down"},
			[=]() { return (size_t) (m->droopDown ? 1 : 0); },
			[=](size_t i) { m->droopDown = i == 1; m->refreshChip(); }));

		menu->addChild(createIndexSubmenuItem("Sample & hold trigger when TRIG is empty",
			{"Shared clock", "YASH oscillator (RATE is its pot)"},
			[=]() { return (size_t) (m->yashOsc ? 1 : 0); },
			[=](size_t i) { m->yashOsc = i == 1; }));

		menu->addChild(createBoolMenuItem("Random gate: toggle mode", "",
			[=]() { return m->toggleMode; },
			[=](bool v) { m->toggleMode = v; }));
	}
};


Model* modelVolatility = createModel<Volatility, VolatilityWidget>("Volatility");

#include "../plugin.hpp"
#include "Panel.hpp"
#include "Curve.hpp"
#include <cmath>

// ---------------------------------------------------------------------------
// Installment -- the Modular in a Week dual function generator, consolidated.
//
// Two identical channels. Each is one circuit at a time, picked by MODE:
//   LFO  the Simple LFO / 13700 VCLFO's OTA-integrator triangle -- a linear
//        ramp up over ATTACK seconds, down over RELEASE seconds, free-running.
//   AR   Niklas Roennberg's diode-steered RC dual AR -- GATE rises it over
//        ATTACK, holds at the peak while the gate stays up, falls over
//        RELEASE when it drops.
//   AD   PHObos's AD -- GATE triggers a rise over ATTACK and an automatic
//        fall over RELEASE, gate duration irrelevant once it has fired.
// LOOP makes an AR retrigger itself the instant it reaches zero (so it never
// needs a second gate) and makes an AD do the same (so it becomes another
// LFO, exactly as the PHObos original could). ATTACK and RELEASE are the same
// two knobs in every mode -- what they set just changes meaning with MODE,
// which is the point: one pair of rise/fall times is a function generator,
// and LFO/AR/AD are three ways of asking it to run.
//
// BIAS and the CV jack both nudge the same axis: a bias/CV octave shift that
// speeds up or slows down ATTACK and RELEASE together, preserving whatever
// skew the two knobs set -- the CV convention the 13700 VCLFO's Bias control
// suggested once it had a panel to share with an envelope.
//
// GATE/RESET IN is one jack wearing two hats: a level-sensitive gate in AR,
// an edge trigger in AD, and a phase reset in LFO (there being no gate to
// speak of once a function generator is free-running).
//
// MINIMUM DUE is the Day 12 Falstad sketch: a triangle compared against a
// duty threshold makes a PWM drive signal for something like a tape motor.
// It taps channel one's core directly, so it always has a carrier -- a
// triangle in LFO mode, an attack/release envelope shape in AR or AD.

namespace installment {

enum Mode { MODE_LFO = 0, MODE_AR = 1, MODE_AD = 2 };

// RANGE bounds, in log2(seconds) so a CV/bias octave shift is a plain add.
static const float kLoMinLog2 = -6.643856f;   // log2(0.01 s)
static const float kLoMaxLog2 =  3.321928f;   // log2(10 s)
static const float kHiMinLog2 = -9.965784f;   // log2(0.001 s)
static const float kHiMaxLog2 =  0.f;         // log2(1 s)
static const float kTimeAbsMin = 0.0003f;     // headroom past HI's floor
static const float kTimeAbsMax = 30.f;        // headroom past LO's ceiling
static const float kBiasOctaves = 3.f;        // BIAS at full deflection
static const float kCvOctaves = 5.f;          // CV_AMT at full CW, 10 V

/** Two-sample-wide polyBLEP correction for a hard step at fractional phase
    `t` (0..1 within the period), where `dt` is the phase advanced per sample.
    Used to band-limit both the LFO's square/inverted output and the PWM
    comparator, so neither aliases when RANGE:HI pushes a cycle into audio
    rates. */
static inline float polyBlep(float t, float dt) {
	if (dt <= 0.f)
		return 0.f;
	if (t < dt) {
		t /= dt;
		return t + t - t * t - 1.f;
	}
	if (t > 1.f - dt) {
		t = (t - 1.f) / dt;
		return t * t + t + t + 1.f;
	}
	return 0.f;
}

/** One channel's core, replicated once per polyphonic voice. Runs whichever
    of the three circuits MODE selects; the caller supplies the already
    CV/bias-modulated attack and release times so this stays free of panel
    concerns. */
struct FuncGenVoice {
	enum Phase { PH_IDLE, PH_RISE, PH_SUSTAIN, PH_FALL };
	Phase phase = PH_IDLE;
	float t = 0.f;             // elapsed seconds in the current AR/AD phase
	float value = 0.f;         // AR/AD output, 0..10 V
	float fallStart = 0.f;     // value the FALL phase decays from
	float cyclePhase = 0.f;    // LFO's own 0..1 position in its cycle
	dsp::SchmittTrigger gateTrig;
	dsp::PulseGenerator eocPulse;

	void reset() {
		phase = PH_IDLE;
		t = 0.f;
		value = 0.f;
		fallStart = 0.f;
		cyclePhase = 0.f;
		gateTrig.reset();
		eocPulse.reset();
	}

	/** Roennberg's AR and PHObos's AD are the same machine with one
	    difference: whether reaching the top waits for the gate to drop
	    (`sustain`, AR) or falls immediately (AD). RC exponentials throughout,
	    time-constant = phase-time / 5 so each phase lands within about 1% of
	    its target at the nominal time -- diode-steered RC charging into a
	    reservoir, not a linear ramp. */
	void runEnvelope(bool sustain, bool loop, float attackSec, float releaseSec,
	                 bool gateEdge, bool gateHigh, float dt,
	                 float& mainOut, float& invOut) {
		switch (phase) {
			case PH_IDLE:
				if (gateEdge) {
					phase = PH_RISE;
					t = 0.f;
				}
				break;

			case PH_RISE:
				// AR only: releasing the gate mid-attack falls from wherever
				// the rise had gotten to, rather than snapping to the peak.
				if (sustain && !gateHigh) {
					phase = PH_FALL;
					fallStart = value;
					t = 0.f;
					break;
				}
				t += dt;
				value = 10.f * (1.f - std::exp(-t / std::fmax(attackSec, 1e-4f) * 5.f));
				if (t >= attackSec || value >= 9.99f) {
					value = 10.f;
					if (sustain && gateHigh) {
						phase = PH_SUSTAIN;
					}
					else {
						phase = PH_FALL;
						fallStart = 10.f;
						t = 0.f;
					}
				}
				break;

			case PH_SUSTAIN:
				value = 10.f;
				if (!gateHigh) {
					phase = PH_FALL;
					fallStart = 10.f;
					t = 0.f;
				}
				break;

			case PH_FALL:
				t += dt;
				value = fallStart * std::exp(-t / std::fmax(releaseSec, 1e-4f) * 5.f);
				if (t >= releaseSec || value <= 0.01f) {
					value = 0.f;
					phase = PH_IDLE;
					eocPulse.trigger(1e-3f);
					// AD's loop and AR's latched retrigger are the same move:
					// go straight back to RISE without waiting on the gate.
					if (loop) {
						phase = PH_RISE;
						t = 0.f;
					}
				}
				break;
		}
		mainOut = value;
		invOut = 10.f - value;
	}

	/** The Simple LFO / 13700 VCLFO core: an OTA integrator, i.e. a linear
	    ramp for ATTACK seconds and back down for RELEASE seconds, which is a
	    triangle when the two match and a asymmetric ramp when they do not.
	    `sine` runs it through a cheap odd waveshaper for the menu's
	    triangle-to-sine option. The companion output is a band-limited pulse
	    at the same duty the triangle keeps, standing in for the comparator
	    squarer the OTA core drives on the original panel. */
	void runLfo(bool sine, float attackSec, float releaseSec, bool resetEdge,
	           float dt, float& mainOut, float& invOut) {
		if (resetEdge)
			cyclePhase = 0.f;

		float period = std::fmax(attackSec + releaseSec, 1e-4f);
		float duty = clamp(attackSec / period, 0.001f, 0.999f);
		float dtN = dt / period;

		cyclePhase += dtN;
		bool wrapped = false;
		if (cyclePhase >= 1.f) {
			cyclePhase -= 1.f;
			wrapped = true;
		}

		float tri = (cyclePhase < duty)
			? (cyclePhase / duty) * 10.f
			: (1.f - (cyclePhase - duty) / (1.f - duty)) * 10.f;
		if (sine) {
			float n = tri * 0.2f - 1.f;               // -1..1
			tri = (std::sin(n * (float)M_PI * 0.5f) * 0.5f + 0.5f) * 10.f;
		}

		float sq = (cyclePhase < duty) ? 1.f : -1.f;
		sq += polyBlep(cyclePhase, dtN);
		sq -= polyBlep(std::fmod(cyclePhase - duty + 1.f, 1.f), dtN);

		mainOut = tri;
		invOut = (sq * 0.5f + 0.5f) * 10.f;
		if (wrapped)
			eocPulse.trigger(1e-3f);
	}
};

} // namespace installment

using installment::FuncGenVoice;
using installment::Mode;


struct Installment : Module {
	enum ParamId {
		MODE1_PARAM, LOOP1_PARAM, ATTACK1_PARAM, RELEASE1_PARAM, RANGE1_PARAM,
		BIAS1_PARAM, CV1_AMT_PARAM,
		MODE2_PARAM, LOOP2_PARAM, ATTACK2_PARAM, RELEASE2_PARAM, RANGE2_PARAM,
		BIAS2_PARAM, CV2_AMT_PARAM,
		PWM_DUTY_PARAM,
		CURVE1_PARAM, CURVE2_PARAM, PWM_CV_AMT_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		CV1_IN_INPUT, GATE1_IN_INPUT, CV2_IN_INPUT, GATE2_IN_INPUT,
		PWM_CV_IN_INPUT, INPUTS_LEN
	};
	enum OutputId {
		ENV1_OUT_OUTPUT, INV1_OUT_OUTPUT, EOC1_OUT_OUTPUT,
		ENV2_OUT_OUTPUT, INV2_OUT_OUTPUT, EOC2_OUT_OUTPUT,
		PWM_OUT_OUTPUT, OUTPUTS_LEN
	};
	enum LightId {
		LOOP1_LIGHT, LOOP2_LIGHT, LIGHTS_LEN
	};

	FuncGenVoice voiceA[PORT_MAX_CHANNELS];
	FuncGenVoice voiceB[PORT_MAX_CHANNELS];

	dsp::MinBlepGenerator<16, 16> pwmBlep;
	float pwmPrevDiff = 0.f;
	float pwmPrevState = 0.f;

	bool sineShape[2] = {false, false};
	bool tapeMotorLimit = false;

	Installment() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

		std::vector<std::string> modeLabels;
		modeLabels.push_back("LFO");
		modeLabels.push_back("AR");
		modeLabels.push_back("AD");
		std::vector<std::string> rangeLabels;
		rangeLabels.push_back("Lo (slow)");
		rangeLabels.push_back("Hi (fast)");

		configSwitch(MODE1_PARAM, 0.f, 2.f, 0.f, "Mode, channel 1", modeLabels);
		configButton(LOOP1_PARAM, "Loop, channel 1");
		configParam(ATTACK1_PARAM, 0.f, 1.f, 0.4f, "Attack, channel 1", "%", 0.f, 100.f);
		configParam(RELEASE1_PARAM, 0.f, 1.f, 0.4f, "Release, channel 1", "%", 0.f, 100.f);
		configSwitch(RANGE1_PARAM, 0.f, 1.f, 0.f, "Range, channel 1", rangeLabels);
		configParam(BIAS1_PARAM, -1.f, 1.f, 0.f, "Bias, channel 1", "%", 0.f, 100.f);
		configParam(CV1_AMT_PARAM, -1.f, 1.f, 0.f, "CV amount, channel 1", "%", 0.f, 100.f);
		configParam(CURVE1_PARAM, -1.f, 1.f, 0.f, "Curve, channel 1", "%", 0.f, 100.f);
		getParamQuantity(CV1_AMT_PARAM)->randomizeEnabled = false;

		configSwitch(MODE2_PARAM, 0.f, 2.f, 0.f, "Mode, channel 2", modeLabels);
		configButton(LOOP2_PARAM, "Loop, channel 2");
		configParam(ATTACK2_PARAM, 0.f, 1.f, 0.4f, "Attack, channel 2", "%", 0.f, 100.f);
		configParam(RELEASE2_PARAM, 0.f, 1.f, 0.4f, "Release, channel 2", "%", 0.f, 100.f);
		configSwitch(RANGE2_PARAM, 0.f, 1.f, 0.f, "Range, channel 2", rangeLabels);
		configParam(BIAS2_PARAM, -1.f, 1.f, 0.f, "Bias, channel 2", "%", 0.f, 100.f);
		configParam(CV2_AMT_PARAM, -1.f, 1.f, 0.f, "CV amount, channel 2", "%", 0.f, 100.f);
		configParam(CURVE2_PARAM, -1.f, 1.f, 0.f, "Curve, channel 2", "%", 0.f, 100.f);
		getParamQuantity(CV2_AMT_PARAM)->randomizeEnabled = false;

		configParam(PWM_DUTY_PARAM, 0.f, 1.f, 0.5f, "PWM duty", "%", 0.f, 100.f);
		configParam(PWM_CV_AMT_PARAM, -1.f, 1.f, 0.f, "PWM duty CV amount", "%", 0.f, 100.f);

		configInput(CV1_IN_INPUT, "Channel 1 CV");
		configInput(GATE1_IN_INPUT, "Channel 1 gate/reset");
		configInput(CV2_IN_INPUT, "Channel 2 CV");
		configInput(GATE2_IN_INPUT, "Channel 2 gate/reset");
		configInput(PWM_CV_IN_INPUT, "PWM duty CV");

		configOutput(ENV1_OUT_OUTPUT, "Channel 1 triangle/envelope");
		configOutput(INV1_OUT_OUTPUT, "Channel 1 square/inverted envelope");
		configOutput(EOC1_OUT_OUTPUT, "Channel 1 end of cycle trigger");
		configOutput(ENV2_OUT_OUTPUT, "Channel 2 triangle/envelope");
		configOutput(INV2_OUT_OUTPUT, "Channel 2 square/inverted envelope");
		configOutput(EOC2_OUT_OUTPUT, "Channel 2 end of cycle trigger");
		configOutput(PWM_OUT_OUTPUT, "PWM");
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		for (int c = 0; c < PORT_MAX_CHANNELS; c++) {
			voiceA[c].reset();
			voiceB[c].reset();
		}
		pwmBlep = dsp::MinBlepGenerator<16, 16>();
		pwmPrevDiff = 0.f;
		pwmPrevState = 0.f;
	}

	/** One channel's worth of processing, shared by channel 1 and channel 2.
	    `ch1Main` receives channel 1's voice-0 core value for MINIMUM DUE to
	    tap downstream; it is only written when `captureForPwm` is set. */
	void processChannel(FuncGenVoice* voices, int mode, bool loop, float attackKnob,
	                    float releaseKnob, int range, float bias, float cvAmt,
	                    float curve,
	                    bool sine, Input& cvIn, Input& gateIn,
	                    Output& mainOut, Output& invOut, Output& eocOut,
	                    float sampleTime, bool captureForPwm, float& ch1Main) {
		int channels = std::max(cvIn.getChannels(), gateIn.getChannels());
		channels = std::max(channels, 1);
		bool cvConnected = cvIn.isConnected();

		float rMin = (range != 0) ? installment::kHiMinLog2 : installment::kLoMinLog2;
		float rMax = (range != 0) ? installment::kHiMaxLog2 : installment::kLoMaxLog2;
		float attackBaseLog2 = rMin + attackKnob * (rMax - rMin);
		float releaseBaseLog2 = rMin + releaseKnob * (rMax - rMin);

		for (int c = 0; c < channels; c++) {
			float modOffset = bias * installment::kBiasOctaves;
			if (cvConnected)
				modOffset += cvAmt * (cvIn.getPolyVoltage(c) / 10.f) * installment::kCvOctaves;

			float attackSec = clamp(dsp::exp2_taylor5(attackBaseLog2 + modOffset),
			                        installment::kTimeAbsMin, installment::kTimeAbsMax);
			float releaseSec = clamp(dsp::exp2_taylor5(releaseBaseLog2 + modOffset),
			                         installment::kTimeAbsMin, installment::kTimeAbsMax);

			float gateV = gateIn.getPolyVoltage(c);
			float main, inv;

			if (mode == installment::MODE_LFO) {
				bool resetEdge = voices[c].gateTrig.process(gateV, 0.1f, 1.f);
				voices[c].runLfo(sine, attackSec, releaseSec, resetEdge, sampleTime, main, inv);
			}
			else {
				bool edge = voices[c].gateTrig.process(gateV, 0.1f, 1.f);
				bool high = voices[c].gateTrig.isHigh();
				voices[c].runEnvelope(mode == installment::MODE_AR, loop, attackSec,
				                      releaseSec, edge, high, sampleTime, main, inv);
			}

			// CURVE bends the finished shape. In LFO mode the companion
			// output is a band-limited pulse, whose only two levels are the
			// shaper's fixed points -- bending it would do nothing but
			// distort the BLEP's overshoot, so it is left alone. In AR and
			// AD the companion is the true inverse of the main output, so it
			// is re-derived from the bent value rather than bent itself.
			float shaped = installment::curveShape(main, curve);
			if (mode != installment::MODE_LFO)
				inv = 10.f - shaped;
			main = shaped;

			mainOut.setVoltage(clamp(main, -12.f, 12.f), c);
			invOut.setVoltage(clamp(inv, -12.f, 12.f), c);
			bool eocHigh = voices[c].eocPulse.process(sampleTime);
			eocOut.setVoltage(eocHigh ? 10.f : 0.f, c);

			if (captureForPwm && c == 0)
				ch1Main = main;
		}
		mainOut.setChannels(channels);
		invOut.setChannels(channels);
		eocOut.setChannels(channels);
	}

	void process(const ProcessArgs& args) override {
		float ch1Main = 0.f;

		processChannel(voiceA, (int)std::round(params[MODE1_PARAM].getValue()),
		               params[LOOP1_PARAM].getValue() > 0.5f,
		               params[ATTACK1_PARAM].getValue(), params[RELEASE1_PARAM].getValue(),
		               (int)std::round(params[RANGE1_PARAM].getValue()),
		               params[BIAS1_PARAM].getValue(), params[CV1_AMT_PARAM].getValue(),
		               params[CURVE1_PARAM].getValue(),
		               sineShape[0], inputs[CV1_IN_INPUT], inputs[GATE1_IN_INPUT],
		               outputs[ENV1_OUT_OUTPUT], outputs[INV1_OUT_OUTPUT],
		               outputs[EOC1_OUT_OUTPUT], args.sampleTime, true, ch1Main);

		float unused = 0.f;
		processChannel(voiceB, (int)std::round(params[MODE2_PARAM].getValue()),
		               params[LOOP2_PARAM].getValue() > 0.5f,
		               params[ATTACK2_PARAM].getValue(), params[RELEASE2_PARAM].getValue(),
		               (int)std::round(params[RANGE2_PARAM].getValue()),
		               params[BIAS2_PARAM].getValue(), params[CV2_AMT_PARAM].getValue(),
		               params[CURVE2_PARAM].getValue(),
		               sineShape[1], inputs[CV2_IN_INPUT], inputs[GATE2_IN_INPUT],
		               outputs[ENV2_OUT_OUTPUT], outputs[INV2_OUT_OUTPUT],
		               outputs[EOC2_OUT_OUTPUT], args.sampleTime, false, unused);

		lights[LOOP1_LIGHT].setBrightness(params[LOOP1_PARAM].getValue() > 0.5f ? 1.f : 0.f);
		lights[LOOP2_LIGHT].setBrightness(params[LOOP2_PARAM].getValue() > 0.5f ? 1.f : 0.f);

		// --- MINIMUM DUE: the Day 12 tape-motor PWM driver -------------------
		// A comparator against channel one's core, whatever MODE has it doing.
		// The crossing is band-limited by inserting a MinBLEP discontinuity at
		// the sub-sample position the carrier actually crossed the threshold,
		// found by linear interpolation between this sample and the last --
		// exact for a comparator against any carrier, not just a triangle.
		float dutyPct = params[PWM_DUTY_PARAM].getValue();
		if (inputs[PWM_CV_IN_INPUT].isConnected())
			dutyPct += params[PWM_CV_AMT_PARAM].getValue()
			         * (inputs[PWM_CV_IN_INPUT].getVoltage() / 10.f);
		dutyPct = tapeMotorLimit ? clamp(dutyPct, 0.1f, 0.9f) : clamp(dutyPct, 0.f, 1.f);

		float diff = ch1Main - dutyPct * 10.f;
		float stateNorm = (diff < 0.f) ? 1.f : 0.f;
		if (stateNorm != pwmPrevState) {
			float delta = stateNorm - pwmPrevState;
			float denom = pwmPrevDiff - diff;
			float frac = (std::fabs(denom) > 1e-9f) ? clamp(pwmPrevDiff / denom, 0.f, 1.f) : 0.f;
			pwmBlep.insertDiscontinuity(-(1.f - frac), delta);
		}
		pwmPrevDiff = diff;
		pwmPrevState = stateNorm;
		float pwmOut = clamp(stateNorm + pwmBlep.process(), 0.f, 1.f) * 10.f;
		outputs[PWM_OUT_OUTPUT].setVoltage(pwmOut);
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "sineShape1", json_boolean(sineShape[0]));
		json_object_set_new(root, "sineShape2", json_boolean(sineShape[1]));
		json_object_set_new(root, "tapeMotorLimit", json_boolean(tapeMotorLimit));
		return root;
	}

	void dataFromJson(json_t* root) override {
		json_t* j;
		j = json_object_get(root, "sineShape1");
		if (j) sineShape[0] = json_boolean_value(j);
		j = json_object_get(root, "sineShape2");
		if (j) sineShape[1] = json_boolean_value(j);
		j = json_object_get(root, "tapeMotorLimit");
		if (j) tapeMotorLimit = json_boolean_value(j);
	}
};


// ---------------------------------------------------------------------------
// Look and feel. The palette, the shared hardware and the entire silkscreen
// come from src/PanelTheme.hpp, generated by tools/panels/Installment.py --
// see ../../panelkit/README.md. Nothing about the look is written here.

struct InstallmentWidget : ModuleWidget {
	InstallmentWidget(Installment* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Installment.svg")));

		panel::addScrews(this);
		panel::addLabels(this);

		addParam(createParamCentered<CKSSThree>(panel::mm(panel::MODE1_POS.x, panel::MODE1_POS.y), module, Installment::MODE1_PARAM));
		addParam(createLightParamCentered<VCVLightBezelLatch<panel::LimeLight> >(
		             panel::mm(panel::LOOP1_POS.x, panel::LOOP1_POS.y), module, Installment::LOOP1_PARAM, Installment::LOOP1_LIGHT));
		addParam(createParamCentered<RoundBlackKnob>(panel::mm(panel::ATTACK1_POS.x, panel::ATTACK1_POS.y), module, Installment::ATTACK1_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(panel::mm(panel::RELEASE1_POS.x, panel::RELEASE1_POS.y), module, Installment::RELEASE1_PARAM));
		addParam(createParamCentered<CKSS>(panel::mm(panel::RANGE1_POS.x, panel::RANGE1_POS.y), module, Installment::RANGE1_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(panel::mm(panel::BIAS1_POS.x, panel::BIAS1_POS.y), module, Installment::BIAS1_PARAM));
		addParam(createParamCentered<Trimpot>(panel::mm(panel::CV1_AMT_POS.x, panel::CV1_AMT_POS.y), module, Installment::CV1_AMT_PARAM));
		addParam(createParamCentered<Trimpot>(panel::mm(panel::CURVE1_POS.x, panel::CURVE1_POS.y), module, Installment::CURVE1_PARAM));

		addParam(createParamCentered<CKSSThree>(panel::mm(panel::MODE2_POS.x, panel::MODE2_POS.y), module, Installment::MODE2_PARAM));
		addParam(createLightParamCentered<VCVLightBezelLatch<panel::LimeLight> >(
		             panel::mm(panel::LOOP2_POS.x, panel::LOOP2_POS.y), module, Installment::LOOP2_PARAM, Installment::LOOP2_LIGHT));
		addParam(createParamCentered<RoundBlackKnob>(panel::mm(panel::ATTACK2_POS.x, panel::ATTACK2_POS.y), module, Installment::ATTACK2_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(panel::mm(panel::RELEASE2_POS.x, panel::RELEASE2_POS.y), module, Installment::RELEASE2_PARAM));
		addParam(createParamCentered<CKSS>(panel::mm(panel::RANGE2_POS.x, panel::RANGE2_POS.y), module, Installment::RANGE2_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(panel::mm(panel::BIAS2_POS.x, panel::BIAS2_POS.y), module, Installment::BIAS2_PARAM));
		addParam(createParamCentered<Trimpot>(panel::mm(panel::CV2_AMT_POS.x, panel::CV2_AMT_POS.y), module, Installment::CV2_AMT_PARAM));
		addParam(createParamCentered<Trimpot>(panel::mm(panel::CURVE2_POS.x, panel::CURVE2_POS.y), module, Installment::CURVE2_PARAM));

		addParam(createParamCentered<Trimpot>(panel::mm(panel::PWM_CV_AMT_POS.x, panel::PWM_CV_AMT_POS.y), module, Installment::PWM_CV_AMT_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(panel::mm(panel::PWM_DUTY_POS.x, panel::PWM_DUTY_POS.y), module, Installment::PWM_DUTY_PARAM));

		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::CV1_IN_POS.x, panel::CV1_IN_POS.y), module, Installment::CV1_IN_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::GATE1_IN_POS.x, panel::GATE1_IN_POS.y), module, Installment::GATE1_IN_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::CV2_IN_POS.x, panel::CV2_IN_POS.y), module, Installment::CV2_IN_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::GATE2_IN_POS.x, panel::GATE2_IN_POS.y), module, Installment::GATE2_IN_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::PWM_CV_IN_POS.x, panel::PWM_CV_IN_POS.y), module, Installment::PWM_CV_IN_INPUT));

		addOutput(createOutputCentered<panel::PortOutMain>(panel::mm(panel::ENV1_OUT_POS.x, panel::ENV1_OUT_POS.y), module, Installment::ENV1_OUT_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::INV1_OUT_POS.x, panel::INV1_OUT_POS.y), module, Installment::INV1_OUT_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::EOC1_OUT_POS.x, panel::EOC1_OUT_POS.y), module, Installment::EOC1_OUT_OUTPUT));
		addOutput(createOutputCentered<panel::PortOutMain>(panel::mm(panel::ENV2_OUT_POS.x, panel::ENV2_OUT_POS.y), module, Installment::ENV2_OUT_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::INV2_OUT_POS.x, panel::INV2_OUT_POS.y), module, Installment::INV2_OUT_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::EOC2_OUT_POS.x, panel::EOC2_OUT_POS.y), module, Installment::EOC2_OUT_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::PWM_OUT_POS.x, panel::PWM_OUT_POS.y), module, Installment::PWM_OUT_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		Installment* m = dynamic_cast<Installment*>(module);
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Installment"));

		menu->addChild(createBoolMenuItem("Channel 1: sine-shaped LFO", "",
			[=]() { return m->sineShape[0]; },
			[=](bool v) { m->sineShape[0] = v; }));

		menu->addChild(createBoolMenuItem("Channel 2: sine-shaped LFO", "",
			[=]() { return m->sineShape[1]; },
			[=](bool v) { m->sineShape[1] = v; }));

		menu->addChild(createBoolMenuItem("Tape-motor duty limit (10-90%)", "",
			[=]() { return m->tapeMotorLimit; },
			[=](bool v) { m->tapeMotorLimit = v; }));
	}
};


Model* modelInstallment = createModel<Installment, InstallmentWidget>("Installment");

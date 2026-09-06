#include "../plugin.hpp"
#include "Panel.hpp"
#include "Voices.hpp"

using namespace kickback;


struct Kickback : Module {
	enum ParamId {
		ACCENT_PARAM, LEVEL_PARAM,
		KICK_PITCH_PARAM, SNARE_PITCH_PARAM, HAT_TONE_PARAM, SMURF_PITCH_PARAM,
		TOM_PITCH_PARAM, BELL_PITCH_PARAM, NOISE_TONE_PARAM, DAZZLER_KIT_PARAM,
		KICK_DECAY_PARAM, SNARE_DECAY_PARAM, HAT_DECAY_PARAM, SMURF_DECAY_PARAM,
		TOM_DECAY_PARAM, BELL_DECAY_PARAM, NOISE_DECAY_PARAM, DAZZLER_DECAY_PARAM,
		KICK_DRIVE_PARAM, SNARE_SNAP_PARAM, SMURF_SWEEP_PARAM, TOM_RANGE_PARAM,
		BELL_TIMBRE_PARAM, DAZZLER_CRACKLE_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		KICK_TRIG_INPUT, SNARE_TRIG_INPUT, HAT_TRIG_INPUT, SMURF_TRIG_INPUT,
		TOM_TRIG_INPUT, BELL_TRIG_INPUT, NOISE_TRIG_INPUT, DAZZLER_TRIG_INPUT,
		NOISE_CV_INPUT, ACCENT_IN_INPUT, INPUTS_LEN
	};
	enum OutputId {
		KICK_OUT_OUTPUT, SNARE_OUT_OUTPUT, HAT_OUT_OUTPUT, SMURF_OUT_OUTPUT,
		TOM_OUT_OUTPUT, BELL_OUT_OUTPUT, NOISE_OUT_OUTPUT, DAZZLER_OUT_OUTPUT,
		MIX_OUT_OUTPUT, OUTPUTS_LEN
	};
	enum LightId {
		KICK_LED_LIGHT, SNARE_LED_LIGHT, HAT_LED_LIGHT, SMURF_LED_LIGHT,
		TOM_LED_LIGHT, BELL_LED_LIGHT, NOISE_LED_LIGHT, DAZZLER_LED_LIGHT,
		LIGHTS_LEN
	};

	Kick kick;
	Snare snare;
	Hat hat;
	Smurf smurf;
	Tom tom;
	Bell bell;
	NoiseVoice noise;
	Dazzler dazzler;

	dsp::SchmittTrigger kickTrig, snareTrig, hatTrig, smurfTrig, tomTrig, bellTrig,
	                     noiseTrig, dazzlerTrig;
	dsp::ClockDivider lightDivider;

	// One flag per voice, latched between light updates so a strike landing
	// mid-division is never missed the way sampling the trigger only on the
	// divider's own tick would miss it.
	bool litSince[8] = {};
	float ledLevel[8] = {};

	Kickback() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

		configParam(ACCENT_PARAM, 0.f, 1.f, 0.5f, "Accent CV amount", "%", 0.f, 100.f);
		configParam(LEVEL_PARAM, 0.f, 1.f, 0.7f, "Mix level", "%", 0.f, 100.f);

		configParam(KICK_PITCH_PARAM, 0.f, 1.f, 0.4f, "Kick pitch");
		configParam(SNARE_PITCH_PARAM, 0.f, 1.f, 0.4f, "Snare pitch");
		configParam(HAT_TONE_PARAM, 0.f, 1.f, 0.6f, "Hat tone");
		configParam(SMURF_PITCH_PARAM, 0.f, 1.f, 0.4f, "Smurf pitch");
		configParam(TOM_PITCH_PARAM, 0.f, 1.f, 0.5f, "Tom pitch");
		configParam(BELL_PITCH_PARAM, 0.f, 1.f, 0.3f, "Bell pitch");
		configParam(NOISE_TONE_PARAM, 0.f, 1.f, 0.5f, "Noise tone");

		std::vector<std::string> kitLabels;
		kitLabels.push_back("Snare");
		kitLabels.push_back("Hi-hat");
		configSwitch(DAZZLER_KIT_PARAM, 0.f, 1.f, 0.f, "Dazzler kit", kitLabels);

		configParam(KICK_DECAY_PARAM, 0.f, 1.f, 0.4f, "Kick decay");
		configParam(SNARE_DECAY_PARAM, 0.f, 1.f, 0.4f, "Snare decay");
		configParam(HAT_DECAY_PARAM, 0.f, 1.f, 0.3f, "Hat decay");
		configParam(SMURF_DECAY_PARAM, 0.f, 1.f, 0.4f, "Smurf decay");
		configParam(TOM_DECAY_PARAM, 0.f, 1.f, 0.5f, "Tom decay");
		configParam(BELL_DECAY_PARAM, 0.f, 1.f, 0.4f, "Bell decay");
		configParam(NOISE_DECAY_PARAM, 0.f, 1.f, 0.35f, "Noise decay");
		configParam(DAZZLER_DECAY_PARAM, 0.f, 1.f, 0.35f, "Dazzler decay");

		configParam(KICK_DRIVE_PARAM, 0.f, 1.f, 0.3f, "Kick drive", "%", 0.f, 100.f);
		configParam(SNARE_SNAP_PARAM, 0.f, 1.f, 0.4f, "Snare snap", "%", 0.f, 100.f);
		configParam(SMURF_SWEEP_PARAM, 0.f, 1.f, 0.6f, "Smurf pitch sweep", "%", 0.f, 100.f);

		std::vector<std::string> rangeLabels;
		rangeLabels.push_back("Low");
		rangeLabels.push_back("Mid");
		rangeLabels.push_back("High");
		configSwitch(TOM_RANGE_PARAM, 0.f, 2.f, 1.f, "Tom range", rangeLabels);

		configParam(BELL_TIMBRE_PARAM, 0.f, 1.f, 0.4f, "Bell timbre", "%", 0.f, 100.f);
		configParam(DAZZLER_CRACKLE_PARAM, 0.f, 1.f, 0.3f, "Dazzler crackle", "%", 0.f, 100.f);

		configInput(KICK_TRIG_INPUT, "Kick trigger");
		configInput(SNARE_TRIG_INPUT, "Snare trigger");
		configInput(HAT_TRIG_INPUT, "Hat trigger");
		configInput(SMURF_TRIG_INPUT, "Smurf trigger");
		configInput(TOM_TRIG_INPUT, "Tom trigger");
		configInput(BELL_TRIG_INPUT, "Bell trigger");
		configInput(NOISE_TRIG_INPUT, "Noise trigger");
		configInput(DAZZLER_TRIG_INPUT, "Dazzler trigger");
		configInput(NOISE_CV_INPUT, "Noise decay CV");
		configInput(ACCENT_IN_INPUT, "Accent CV");

		configOutput(KICK_OUT_OUTPUT, "Kick");
		configOutput(SNARE_OUT_OUTPUT, "Snare");
		configOutput(HAT_OUT_OUTPUT, "Hat");
		configOutput(SMURF_OUT_OUTPUT, "Smurf");
		configOutput(TOM_OUT_OUTPUT, "Tom");
		configOutput(BELL_OUT_OUTPUT, "Bell");
		configOutput(NOISE_OUT_OUTPUT, "Noise");
		configOutput(DAZZLER_OUT_OUTPUT, "Dazzler");
		configOutput(MIX_OUT_OUTPUT, "Mix");

		lightDivider.setDivision(32);
		onSampleRateChange({APP->engine->getSampleRate(), 0.f});
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		float fs = e.sampleRate;
		kick.reset();    kick.setRate(fs);
		snare.reset();   snare.setRate(fs);
		hat.reset();     hat.setRate(fs);
		smurf.reset();   smurf.setRate(fs);
		tom.reset();     tom.setRate(fs);
		bell.reset();    bell.setRate(fs);
		noise.reset();   noise.setRate(fs);
		dazzler.reset(); dazzler.setRate(fs);
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		kick.reset(); snare.reset(); hat.reset(); smurf.reset();
		tom.reset(); bell.reset(); noise.reset(); dazzler.reset();
		for (int i = 0; i < 8; i++) { litSince[i] = false; ledLevel[i] = 0.f; }
	}

	void process(const ProcessArgs& args) override {
		float fs = args.sampleRate;

		// ACCENT: the knob is how much the CV can raise a strike above unity,
		// not a level control by itself -- with nothing patched every strike
		// lands at its full, unaccented velocity.
		float accentCv = inputs[ACCENT_IN_INPUT].isConnected() ? inputs[ACCENT_IN_INPUT].getVoltage() : 0.f;
		float vel = clamp(1.f + accentCv / 10.f * params[ACCENT_PARAM].getValue(), 0.f, 2.f);

		bool kStrike = kickTrig.process(inputs[KICK_TRIG_INPUT].getVoltage(), 0.1f, 1.f);
		bool sStrike = snareTrig.process(inputs[SNARE_TRIG_INPUT].getVoltage(), 0.1f, 1.f);
		bool hStrike = hatTrig.process(inputs[HAT_TRIG_INPUT].getVoltage(), 0.1f, 1.f);
		bool mStrike = smurfTrig.process(inputs[SMURF_TRIG_INPUT].getVoltage(), 0.1f, 1.f);
		bool tStrike = tomTrig.process(inputs[TOM_TRIG_INPUT].getVoltage(), 0.1f, 1.f);
		bool bStrike = bellTrig.process(inputs[BELL_TRIG_INPUT].getVoltage(), 0.1f, 1.f);
		bool nStrike = noiseTrig.process(inputs[NOISE_TRIG_INPUT].getVoltage(), 0.1f, 1.f);
		bool dStrike = dazzlerTrig.process(inputs[DAZZLER_TRIG_INPUT].getVoltage(), 0.1f, 1.f);

		if (kStrike) litSince[0] = true;
		if (sStrike) litSince[1] = true;
		if (hStrike) litSince[2] = true;
		if (mStrike) litSince[3] = true;
		if (tStrike) litSince[4] = true;
		if (bStrike) litSince[5] = true;
		if (nStrike) litSince[6] = true;
		if (dStrike) litSince[7] = true;

		float kOut = kick.process(kStrike, vel, params[KICK_PITCH_PARAM].getValue(),
		                          params[KICK_DECAY_PARAM].getValue(), params[KICK_DRIVE_PARAM].getValue(), fs);
		float sOut = snare.process(sStrike, vel, params[SNARE_PITCH_PARAM].getValue(),
		                           params[SNARE_DECAY_PARAM].getValue(), params[SNARE_SNAP_PARAM].getValue(), fs);
		float hOut = hat.process(hStrike, vel, params[HAT_TONE_PARAM].getValue(), params[HAT_DECAY_PARAM].getValue());
		float mOut = smurf.process(mStrike, vel, params[SMURF_PITCH_PARAM].getValue(),
		                           params[SMURF_DECAY_PARAM].getValue(), params[SMURF_SWEEP_PARAM].getValue());

		int range = (int)clamp(std::round(params[TOM_RANGE_PARAM].getValue()), 0.f, 2.f);
		float tOut = tom.process(tStrike, vel, params[TOM_PITCH_PARAM].getValue(),
		                         params[TOM_DECAY_PARAM].getValue(), range, fs);
		float bOut = bell.process(bStrike, vel, params[BELL_PITCH_PARAM].getValue(),
		                          params[BELL_DECAY_PARAM].getValue(), params[BELL_TIMBRE_PARAM].getValue());

		float decayCv = inputs[NOISE_CV_INPUT].isConnected() ? inputs[NOISE_CV_INPUT].getVoltage() / 10.f * 1.f : 0.f;
		float nOut = noise.process(nStrike, vel, params[NOISE_TONE_PARAM].getValue(),
		                           params[NOISE_DECAY_PARAM].getValue(), decayCv);

		int kit = params[DAZZLER_KIT_PARAM].getValue() > 0.5f ? 1 : 0;
		float dOut = dazzler.process(dStrike, vel, kit, params[DAZZLER_DECAY_PARAM].getValue(),
		                             params[DAZZLER_CRACKLE_PARAM].getValue());

		outputs[KICK_OUT_OUTPUT].setVoltage(clamp(kOut, -12.f, 12.f));
		outputs[SNARE_OUT_OUTPUT].setVoltage(clamp(sOut, -12.f, 12.f));
		outputs[HAT_OUT_OUTPUT].setVoltage(clamp(hOut, -12.f, 12.f));
		outputs[SMURF_OUT_OUTPUT].setVoltage(clamp(mOut, -12.f, 12.f));
		outputs[TOM_OUT_OUTPUT].setVoltage(clamp(tOut, -12.f, 12.f));
		outputs[BELL_OUT_OUTPUT].setVoltage(clamp(bOut, -12.f, 12.f));
		outputs[NOISE_OUT_OUTPUT].setVoltage(clamp(nOut, -12.f, 12.f));
		outputs[DAZZLER_OUT_OUTPUT].setVoltage(clamp(dOut, -12.f, 12.f));

		float mix = (kOut + sOut + hOut + mOut + tOut + bOut + nOut + dOut) * params[LEVEL_PARAM].getValue() * 0.3f;
		outputs[MIX_OUT_OUTPUT].setVoltage(clamp(mix, -12.f, 12.f));

		if (lightDivider.process()) {
			float lightDt = args.sampleTime * lightDivider.getDivision();
			float lambda = std::exp(-lightDt / 0.15f);   // ~150 ms flash, independent of DECAY
			for (int i = 0; i < 8; i++) {
				ledLevel[i] = litSince[i] ? 1.f : ledLevel[i] * lambda;
				litSince[i] = false;
			}
			lights[KICK_LED_LIGHT].setBrightness(ledLevel[0]);
			lights[SNARE_LED_LIGHT].setBrightness(ledLevel[1]);
			lights[HAT_LED_LIGHT].setBrightness(ledLevel[2]);
			lights[SMURF_LED_LIGHT].setBrightness(ledLevel[3]);
			lights[TOM_LED_LIGHT].setBrightness(ledLevel[4]);
			lights[BELL_LED_LIGHT].setBrightness(ledLevel[5]);
			lights[NOISE_LED_LIGHT].setBrightness(ledLevel[6]);
			lights[DAZZLER_LED_LIGHT].setBrightness(ledLevel[7]);
		}
	}
};


// ---------------------------------------------------------------------------
// Look and feel comes entirely from src/PanelTheme.hpp and src/Kickback/Panel.hpp,
// generated by tools/panels/Kickback.py -- see ../../panelkit/README.md. Nothing
// about the look is written here.

typedef RoundLargeBlackKnob VoiceKnob;
typedef RoundBlackKnob      SmallKnob;


struct KickbackWidget : ModuleWidget {
	KickbackWidget(Kickback* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Kickback.svg")));

		panel::addScrews(this);
		panel::addLabels(this);

		// --- strike row: one TRIG per voice, ACCENT and LEVEL at the ends -----
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::KICK_TRIG_POS.x, panel::KICK_TRIG_POS.y), module, Kickback::KICK_TRIG_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::SNARE_TRIG_POS.x, panel::SNARE_TRIG_POS.y), module, Kickback::SNARE_TRIG_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::HAT_TRIG_POS.x, panel::HAT_TRIG_POS.y), module, Kickback::HAT_TRIG_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::SMURF_TRIG_POS.x, panel::SMURF_TRIG_POS.y), module, Kickback::SMURF_TRIG_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::TOM_TRIG_POS.x, panel::TOM_TRIG_POS.y), module, Kickback::TOM_TRIG_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::BELL_TRIG_POS.x, panel::BELL_TRIG_POS.y), module, Kickback::BELL_TRIG_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::NOISE_TRIG_POS.x, panel::NOISE_TRIG_POS.y), module, Kickback::NOISE_TRIG_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::DAZZLER_TRIG_POS.x, panel::DAZZLER_TRIG_POS.y), module, Kickback::DAZZLER_TRIG_INPUT));
		addParam(createParamCentered<SmallKnob>(panel::mm(panel::ACCENT_POS.x, panel::ACCENT_POS.y), module, Kickback::ACCENT_PARAM));
		addParam(createParamCentered<SmallKnob>(panel::mm(panel::LEVEL_POS.x, panel::LEVEL_POS.y), module, Kickback::LEVEL_PARAM));

		addChild(createLightCentered<SmallLight<panel::PaperLight> >(panel::mm(panel::KICK_LED_POS.x, panel::KICK_LED_POS.y), module, Kickback::KICK_LED_LIGHT));
		addChild(createLightCentered<SmallLight<panel::PaperLight> >(panel::mm(panel::SNARE_LED_POS.x, panel::SNARE_LED_POS.y), module, Kickback::SNARE_LED_LIGHT));
		addChild(createLightCentered<SmallLight<panel::PaperLight> >(panel::mm(panel::HAT_LED_POS.x, panel::HAT_LED_POS.y), module, Kickback::HAT_LED_LIGHT));
		addChild(createLightCentered<SmallLight<panel::PaperLight> >(panel::mm(panel::SMURF_LED_POS.x, panel::SMURF_LED_POS.y), module, Kickback::SMURF_LED_LIGHT));
		addChild(createLightCentered<SmallLight<panel::PaperLight> >(panel::mm(panel::TOM_LED_POS.x, panel::TOM_LED_POS.y), module, Kickback::TOM_LED_LIGHT));
		addChild(createLightCentered<SmallLight<panel::PaperLight> >(panel::mm(panel::BELL_LED_POS.x, panel::BELL_LED_POS.y), module, Kickback::BELL_LED_LIGHT));
		addChild(createLightCentered<SmallLight<panel::PaperLight> >(panel::mm(panel::NOISE_LED_POS.x, panel::NOISE_LED_POS.y), module, Kickback::NOISE_LED_LIGHT));
		addChild(createLightCentered<SmallLight<panel::PaperLight> >(panel::mm(panel::DAZZLER_LED_POS.x, panel::DAZZLER_LED_POS.y), module, Kickback::DAZZLER_LED_LIGHT));

		// --- row 1: pitch/tone, or the switch a voice has instead -------------
		addParam(createParamCentered<VoiceKnob>(panel::mm(panel::KICK_PITCH_POS.x, panel::KICK_PITCH_POS.y), module, Kickback::KICK_PITCH_PARAM));
		addParam(createParamCentered<VoiceKnob>(panel::mm(panel::SNARE_PITCH_POS.x, panel::SNARE_PITCH_POS.y), module, Kickback::SNARE_PITCH_PARAM));
		addParam(createParamCentered<VoiceKnob>(panel::mm(panel::HAT_TONE_POS.x, panel::HAT_TONE_POS.y), module, Kickback::HAT_TONE_PARAM));
		addParam(createParamCentered<VoiceKnob>(panel::mm(panel::SMURF_PITCH_POS.x, panel::SMURF_PITCH_POS.y), module, Kickback::SMURF_PITCH_PARAM));
		addParam(createParamCentered<VoiceKnob>(panel::mm(panel::TOM_PITCH_POS.x, panel::TOM_PITCH_POS.y), module, Kickback::TOM_PITCH_PARAM));
		addParam(createParamCentered<VoiceKnob>(panel::mm(panel::BELL_PITCH_POS.x, panel::BELL_PITCH_POS.y), module, Kickback::BELL_PITCH_PARAM));
		addParam(createParamCentered<VoiceKnob>(panel::mm(panel::NOISE_TONE_POS.x, panel::NOISE_TONE_POS.y), module, Kickback::NOISE_TONE_PARAM));
		addParam(createParamCentered<CKSS>(panel::mm(panel::DAZZLER_KIT_POS.x, panel::DAZZLER_KIT_POS.y), module, Kickback::DAZZLER_KIT_PARAM));

		// --- row 2: every voice's DECAY ---------------------------------------
		addParam(createParamCentered<SmallKnob>(panel::mm(panel::KICK_DECAY_POS.x, panel::KICK_DECAY_POS.y), module, Kickback::KICK_DECAY_PARAM));
		addParam(createParamCentered<SmallKnob>(panel::mm(panel::SNARE_DECAY_POS.x, panel::SNARE_DECAY_POS.y), module, Kickback::SNARE_DECAY_PARAM));
		addParam(createParamCentered<SmallKnob>(panel::mm(panel::HAT_DECAY_POS.x, panel::HAT_DECAY_POS.y), module, Kickback::HAT_DECAY_PARAM));
		addParam(createParamCentered<SmallKnob>(panel::mm(panel::SMURF_DECAY_POS.x, panel::SMURF_DECAY_POS.y), module, Kickback::SMURF_DECAY_PARAM));
		addParam(createParamCentered<SmallKnob>(panel::mm(panel::TOM_DECAY_POS.x, panel::TOM_DECAY_POS.y), module, Kickback::TOM_DECAY_PARAM));
		addParam(createParamCentered<SmallKnob>(panel::mm(panel::BELL_DECAY_POS.x, panel::BELL_DECAY_POS.y), module, Kickback::BELL_DECAY_PARAM));
		addParam(createParamCentered<SmallKnob>(panel::mm(panel::NOISE_DECAY_POS.x, panel::NOISE_DECAY_POS.y), module, Kickback::NOISE_DECAY_PARAM));
		addParam(createParamCentered<SmallKnob>(panel::mm(panel::DAZZLER_DECAY_POS.x, panel::DAZZLER_DECAY_POS.y), module, Kickback::DAZZLER_DECAY_PARAM));

		// --- row 3: the third pot each circuit has, where it has one ---------
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::NOISE_CV_POS.x, panel::NOISE_CV_POS.y), module, Kickback::NOISE_CV_INPUT));
		addParam(createParamCentered<SmallKnob>(panel::mm(panel::KICK_DRIVE_POS.x, panel::KICK_DRIVE_POS.y), module, Kickback::KICK_DRIVE_PARAM));
		addParam(createParamCentered<SmallKnob>(panel::mm(panel::SNARE_SNAP_POS.x, panel::SNARE_SNAP_POS.y), module, Kickback::SNARE_SNAP_PARAM));
		addParam(createParamCentered<SmallKnob>(panel::mm(panel::SMURF_SWEEP_POS.x, panel::SMURF_SWEEP_POS.y), module, Kickback::SMURF_SWEEP_PARAM));
		addParam(createParamCentered<CKSSThree>(panel::mm(panel::TOM_RANGE_POS.x, panel::TOM_RANGE_POS.y), module, Kickback::TOM_RANGE_PARAM));
		addParam(createParamCentered<SmallKnob>(panel::mm(panel::BELL_TIMBRE_POS.x, panel::BELL_TIMBRE_POS.y), module, Kickback::BELL_TIMBRE_PARAM));
		addParam(createParamCentered<SmallKnob>(panel::mm(panel::DAZZLER_CRACKLE_POS.x, panel::DAZZLER_CRACKLE_POS.y), module, Kickback::DAZZLER_CRACKLE_PARAM));

		// --- footer: ACCENT CV in, every voice's OUT, MIX ---------------------
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::ACCENT_IN_POS.x, panel::ACCENT_IN_POS.y), module, Kickback::ACCENT_IN_INPUT));
		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::KICK_OUT_POS.x, panel::KICK_OUT_POS.y), module, Kickback::KICK_OUT_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::SNARE_OUT_POS.x, panel::SNARE_OUT_POS.y), module, Kickback::SNARE_OUT_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::HAT_OUT_POS.x, panel::HAT_OUT_POS.y), module, Kickback::HAT_OUT_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::SMURF_OUT_POS.x, panel::SMURF_OUT_POS.y), module, Kickback::SMURF_OUT_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::TOM_OUT_POS.x, panel::TOM_OUT_POS.y), module, Kickback::TOM_OUT_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::BELL_OUT_POS.x, panel::BELL_OUT_POS.y), module, Kickback::BELL_OUT_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::NOISE_OUT_POS.x, panel::NOISE_OUT_POS.y), module, Kickback::NOISE_OUT_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::DAZZLER_OUT_POS.x, panel::DAZZLER_OUT_POS.y), module, Kickback::DAZZLER_OUT_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::MIX_OUT_POS.x, panel::MIX_OUT_POS.y), module, Kickback::MIX_OUT_OUTPUT));
	}
};


Model* modelKickback = createModel<Kickback, KickbackWidget>("Kickback");

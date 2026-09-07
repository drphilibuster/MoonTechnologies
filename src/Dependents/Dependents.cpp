// Dependents -- FORM 8812. A chord claimed as harmonics of a note you cannot
// hear.
//
// After Astrobear Music (Aspen Instruments), "This distortion plays chords
// using Chebyshev harmonic exciters" (https://youtu.be/O0QLnR406pQ), and the
// per-harmonic shaping in their Black Diamond Distortion. Their demonstration
// is the reason this module exists; the mathematics under it is public and
// this is that mathematics as a Rack voice, not a copy of their plugin.
//
// The identity is T_n(cos x) = cos(n x): a unit-amplitude sine through the nth
// Chebyshev polynomial comes out as exactly the nth harmonic. Choose the
// harmonic numbers to be a chord's just ratios -- 4:5:6 major, 10:12:15 minor
// -- and a waveshaper plays a chord. Put the root two octaves below hearing and
// the only thing audible is the chord.
#include "../plugin.hpp"
#include "Panel.hpp"
#include "Chebyshev.hpp"

using namespace dependents;

static const int kOversample = 4;

struct Dependents : Module {
	enum ParamId {
		ROOT_PARAM, CHORD_A_PARAM, CHORD_B_PARAM, MORPH_PARAM,
		TILT_PARAM, DRIVE_PARAM, MIX_PARAM, LEVEL_PARAM, NORM_PARAM,
		MORPH_CV_PARAM, TILT_CV_PARAM,
		HARM_PARAM,                                   // + kCustomHarmonics
		PARAMS_LEN = HARM_PARAM + kCustomHarmonics
	};
	enum InputId { IN_INPUT, VOCT_INPUT, MORPH_INPUT, TILT_INPUT, INPUTS_LEN };
	enum OutputId { OUT_OUTPUT, SUB_OUTPUT, OUTPUTS_LEN };
	enum LightId { LIGHTS_LEN };

	Unity unity;
	DcBlock dc;
	double phase = 0.0;
	float dcR = 0.999f;
	dsp::Upsampler<kOversample, 8> up;
	dsp::Decimator<kOversample, 8> down;

	Dependents() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		// Two octaves below A0 up to the top of the bass clef. The useful
		// setting is the bottom of it: the point of the module is a root you
		// cannot hear, so the chord above it has nothing to sit against.
		configParam(ROOT_PARAM, -4.f, 3.f, -2.f, "Root", " Hz", 2.f, dsp::FREQ_C4);

		std::vector<std::string> names;
		for (int i = 0; i < kChordCount; i++) names.push_back(kChords[i].name);
		configSwitch(CHORD_A_PARAM, 0.f, (float)(kChordCount - 1), 3.f, "Chord A", names);
		configSwitch(CHORD_B_PARAM, 0.f, (float)(kChordCount - 1), 4.f, "Chord B", names);
		paramQuantities[CHORD_A_PARAM]->snapEnabled = true;
		paramQuantities[CHORD_B_PARAM]->snapEnabled = true;
		// Not a switch between the two: the weights interpolate, so every
		// position is a real spectrum. Major to minor is the third fading down
		// while the flat third comes up, and everything between is a chord that
		// has no name.
		configParam(MORPH_PARAM, 0.f, 1.f, 0.f, "Morph A to B", "%", 0.f, 100.f);

		configParam(TILT_PARAM, 0.f, 1.f, 0.35f, "Voicing tilt", "%", 0.f, 100.f);
		configParam(DRIVE_PARAM, 0.f, 1.f, 1.f, "Drive", "%", 0.f, 100.f);
		configParam(MIX_PARAM, 0.f, 1.f, 1.f, "Dry / wet", "%", 0.f, 100.f);
		configParam(LEVEL_PARAM, 0.f, 1.f, 0.7f, "Output level", "%", 0.f, 100.f);
		configSwitch(NORM_PARAM, 0.f, 1.f, 1.f, "Hold at unity",
			{"Off -- the chord dissolves as the input quietens", "On -- the chord holds"});
		configParam(MORPH_CV_PARAM, -1.f, 1.f, 0.f, "Morph CV", "%", 0.f, 100.f);
		configParam(TILT_CV_PARAM, -1.f, 1.f, 0.f, "Tilt CV", "%", 0.f, 100.f);
		for (int n = 0; n < kCustomHarmonics; n++)
			configParam(HARM_PARAM + n, 0.f, 1.f, n == 0 ? 1.f : 0.f,
			            string::f("Harmonic %d", n + 1), "%", 0.f, 100.f);

		configInput(IN_INPUT, "Audio (unpatched, the internal root is used)");
		configInput(VOCT_INPUT, "Root 1 V/oct");
		configInput(MORPH_INPUT, "Morph CV");
		configInput(TILT_INPUT, "Voicing tilt CV");
		configOutput(OUT_OUTPUT, "Chord");
		configOutput(SUB_OUTPUT, "The root on its own");
	}

	void onSampleRateChange() override {
		unity.setRate(APP->engine->getSampleRate());
		dcR = std::exp(-2.f * (float)M_PI * 12.f / APP->engine->getSampleRate());
	}
	void onReset() override { unity.reset(); dc.reset(); phase = 0.0; }

	/** One side of the morph. A preset goes through voicing(); CUSTOM reads the
	    panel's twelve trims instead, so the same machinery carries both. */
	void fill(int idx, float tilt, float* w) {
		if (kChords[idx].count > 0) {
			voicing(kChords[idx], tilt, w);
			return;
		}
		for (int n = 0; n <= kMaxHarmonic; n++) w[n] = 0.f;
		for (int n = 0; n < kCustomHarmonics; n++)
			w[n + 1] = params[HARM_PARAM + n].getValue();
	}

	void process(const ProcessArgs& args) override {
		float tilt = clamp(params[TILT_PARAM].getValue()
		                   + inputs[TILT_INPUT].getVoltage() / 10.f
		                     * params[TILT_CV_PARAM].getValue(), 0.f, 1.f);
		float t = clamp(params[MORPH_PARAM].getValue()
		                + inputs[MORPH_INPUT].getVoltage() / 10.f
		                  * params[MORPH_CV_PARAM].getValue(), 0.f, 1.f);

		int ia = clamp((int)std::round(params[CHORD_A_PARAM].getValue()), 0, kChordCount - 1);
		int ib = clamp((int)std::round(params[CHORD_B_PARAM].getValue()), 0, kChordCount - 1);
		float wa[kMaxHarmonic + 1], wb[kMaxHarmonic + 1], w[kMaxHarmonic + 1];
		fill(ia, tilt, wa);
		fill(ib, tilt, wb);
		morph(wa, wb, t, w);
		float drive = params[DRIVE_PARAM].getValue();

		// The root, always running: it is the SUB output whether or not it is
		// what the exciter is being fed.
		float pitch = params[ROOT_PARAM].getValue() + inputs[VOCT_INPUT].getVoltage();
		float freq = dsp::FREQ_C4 * std::pow(2.f, pitch);
		phase += (double)freq * (double)args.sampleTime;
		if (phase >= 1.0) phase -= std::floor(phase);
		float root = std::cos(2.f * (float)M_PI * (float)phase);

		float dry, wet;
		if (inputs[IN_INPUT].isConnected()) {
			// A signal from outside: the exciter really is a waveshaper, so it
			// is run at 4x and filtered back down. A chord's top member is the
			// 20th harmonic, and the 20th harmonic of anything much above a few
			// hundred hertz is past Nyquist without this.
			dry = inputs[IN_INPUT].getVoltage() / 5.f;
			float g = params[NORM_PARAM].getValue() > 0.5f ? unity.gain(dry) : 1.f;
			float x = clamp(dry * g * drive, -1.f, 1.f);
			float buf[kOversample];
			up.process(x, buf);
			for (int i = 0; i < kOversample; i++)
				buf[i] = excite(clamp(buf[i], -1.f, 1.f), w);
			wet = down.process(buf);
		}
		else {
			// The internal root, and here the waveshaper is not needed at all.
			// T_n(cos p) *is* cos(n p), so the chord can be summed directly from
			// the phase -- exact, and with every member above Nyquist simply
			// left out rather than folded back down as an alias. Oversampling
			// cannot do better than not making the alias in the first place.
			dry = root;
			wet = drive * exciteFromPhase(phase, freq, 0.5f / args.sampleTime, w);
		}

		wet = dc.process(wet, dcR);
		float mix = params[MIX_PARAM].getValue();
		float y = dry + (wet - dry) * mix;
		y *= params[LEVEL_PARAM].getValue() * 5.f;

		outputs[OUT_OUTPUT].setVoltage(clamp(y, -12.f, 12.f));
		outputs[SUB_OUTPUT].setVoltage(root * 5.f);
	}
};

struct DependentsWidget : ModuleWidget {
	DependentsWidget(Dependents* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Dependents.svg")));
		panel::addScrews(this);
		panel::addLabels(this);

		addParam(createParamCentered<RoundLargeBlackKnob>(panel::mm(panel::ROOT_POS.x, panel::ROOT_POS.y), module, Dependents::ROOT_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(panel::mm(panel::CHORD_A_POS.x, panel::CHORD_A_POS.y), module, Dependents::CHORD_A_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(panel::mm(panel::CHORD_B_POS.x, panel::CHORD_B_POS.y), module, Dependents::CHORD_B_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(panel::mm(panel::MORPH_POS.x, panel::MORPH_POS.y), module, Dependents::MORPH_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(panel::mm(panel::TILT_POS.x, panel::TILT_POS.y), module, Dependents::TILT_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(panel::mm(panel::DRIVE_POS.x, panel::DRIVE_POS.y), module, Dependents::DRIVE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(panel::mm(panel::MIX_POS.x, panel::MIX_POS.y), module, Dependents::MIX_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(panel::mm(panel::LEVEL_POS.x, panel::LEVEL_POS.y), module, Dependents::LEVEL_PARAM));
		addParam(createParamCentered<CKSS>(panel::mm(panel::NORM_POS.x, panel::NORM_POS.y), module, Dependents::NORM_PARAM));

		addParam(createParamCentered<Trimpot>(panel::mm(panel::MORPH_CV_POS.x, panel::MORPH_CV_POS.y), module, Dependents::MORPH_CV_PARAM));
		addParam(createParamCentered<Trimpot>(panel::mm(panel::TILT_CV_POS.x, panel::TILT_CV_POS.y), module, Dependents::TILT_CV_PARAM));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::MORPH_IN_POS.x, panel::MORPH_IN_POS.y), module, Dependents::MORPH_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::TILT_IN_POS.x, panel::TILT_IN_POS.y), module, Dependents::TILT_INPUT));

		static const Vec* harmPos[kCustomHarmonics] = {
			&panel::H1_POS, &panel::H2_POS, &panel::H3_POS, &panel::H4_POS,
			&panel::H5_POS, &panel::H6_POS, &panel::H7_POS, &panel::H8_POS,
			&panel::H9_POS, &panel::H10_POS, &panel::H11_POS, &panel::H12_POS };
		for (int n = 0; n < kCustomHarmonics; n++)
			addParam(createParamCentered<Trimpot>(
				panel::mm(harmPos[n]->x, harmPos[n]->y), module, Dependents::HARM_PARAM + n));

		addInput(createInputCentered<panel::PortInMain>(panel::mm(panel::IN_POS.x, panel::IN_POS.y), module, Dependents::IN_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::VOCT_POS.x, panel::VOCT_POS.y), module, Dependents::VOCT_INPUT));
		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::SUB_POS.x, panel::SUB_POS.y), module, Dependents::SUB_OUTPUT));
		addOutput(createOutputCentered<panel::PortOutMain>(panel::mm(panel::OUT_POS.x, panel::OUT_POS.y), module, Dependents::OUT_OUTPUT));
	}
};

Model* modelDependents = createModel<Dependents, DependentsWidget>("Dependents");

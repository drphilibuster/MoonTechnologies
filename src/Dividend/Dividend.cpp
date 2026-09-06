#include "../plugin.hpp"
#include "Panel.hpp"
#include "Pulsar.hpp"

using dividend::PulsarCore;


// The FORMANT knob is one parameter with two meanings: a ratio to the
// fundamental when TRACK is up, a frequency when it is down. The tooltip says
// which, in the unit that applies, rather than reporting octaves either way.
struct FormantQuantity : ParamQuantity {
	int trackParam = -1;

	bool tracking() {
		return module && trackParam >= 0 && module->params[trackParam].getValue() > 0.5f;
	}
	float getDisplayValue() override {
		float v = getValue();
		return tracking() ? std::pow(2.f, v) : dsp::FREQ_C4 * std::pow(2.f, v);
	}
	void setDisplayValue(float d) override {
		if (!(d > 0.f))
			return;
		setValue(tracking() ? std::log2(d) : std::log2(d / dsp::FREQ_C4));
	}
	std::string getUnit() override {
		return tracking() ? "x fundamental" : " Hz";
	}
};


struct Dividend : Module {
	enum ParamId {
		FREQ_PARAM, FINE_PARAM, FORMANT_PARAM, TRACK_PARAM, FM_MODE_PARAM,
		WAVE_PARAM, WINDOW_PARAM, CYCLES_PARAM, STEREO_PARAM,
		BURST_ON_PARAM, BURST_OFF_PARAM, PROB_PARAM,
		FM_CV_PARAM, FMT_CV_PARAM, PROB_CV_PARAM, BURST_CV_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		VOCT_INPUT, SYNC_INPUT, FM_INPUT, FMT_INPUT, PROB_INPUT, BURST_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		OUT_L_OUTPUT, OUT_R_OUTPUT, TRIG_OUTPUT, ENV_OUTPUT, OUTPUTS_LEN
	};
	enum LightId {
		HELD_LIGHT, LIGHTS_LEN
	};

	// Band-limiting: the pulsaret boundaries (and the saw, square and
	// rectangular-window steps inside them) alias, so the core runs at a
	// multiple of the sample rate and the result is decimated through Rack's
	// windowed-sinc FIR (dsp::Decimator, 16 taps per phase).
	enum Oversample { OS_1, OS_2, OS_4, NUM_OS };
	int oversample = OS_2;
	bool overlap = false;
	bool dcBlock = true;

	PulsarCore core;
	dsp::Decimator<2, 16> dec2L, dec2R;
	dsp::Decimator<4, 16> dec4L, dec4R;
	dsp::TRCFilter<float> dcL, dcR;
	dsp::SchmittTrigger syncTrigger;
	dsp::ClockDivider lightDivider;
	float sampleRate = 44100.f;
	float heldLevel = 0.f;

	// Read-outs, published for the panel display from the audio thread.
	float dispF0 = dsp::FREQ_C4;
	float dispFormant = 4.f * dsp::FREQ_C4;
	float dispDuty = 0.25f;
	bool dispTrack = true;
	bool dispStereo = false;

	Dividend() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

		configParam(FREQ_PARAM, -4.f, 4.f, 0.f, "Fundamental", " Hz", 2.f, dsp::FREQ_C4);
		configParam(FINE_PARAM, -1.f, 1.f, 0.f, "Fine tune", " semitones");
		FormantQuantity* fq = configParam<FormantQuantity>(FORMANT_PARAM, -2.f, 6.f, 2.f, "Formant");
		fq->trackParam = TRACK_PARAM;

		std::vector<std::string> trackLabels;
		trackLabels.push_back("Absolute (Hz)");
		trackLabels.push_back("Tracks the fundamental");
		configSwitch(TRACK_PARAM, 0.f, 1.f, 1.f, "Formant", trackLabels);

		std::vector<std::string> fmLabels;
		fmLabels.push_back("Exponential (V/oct)");
		fmLabels.push_back("Linear through-zero");
		configSwitch(FM_MODE_PARAM, 0.f, 1.f, 0.f, "FM law", fmLabels);

		std::vector<std::string> waveLabels;
		waveLabels.push_back("Sine");
		waveLabels.push_back("Sinc");
		waveLabels.push_back("Saw");
		waveLabels.push_back("Square");
		waveLabels.push_back("Triangle");
		waveLabels.push_back("Cosine burst");
		configSwitch(WAVE_PARAM, 0.f, (float) (dividend::NUM_WAVES - 1), (float) dividend::WAVE_SINE,
		             "Pulsaret waveform", waveLabels);

		std::vector<std::string> winLabels;
		winLabels.push_back("Rectangular");
		winLabels.push_back("Gaussian");
		winLabels.push_back("Hann");
		winLabels.push_back("Exponential decay");
		winLabels.push_back("Reverse exponential");
		winLabels.push_back("Linear decay");
		configSwitch(WINDOW_PARAM, 0.f, (float) (dividend::NUM_WINDOWS - 1), (float) dividend::WIN_GAUSS,
		             "Pulsaret envelope", winLabels);

		configParam(CYCLES_PARAM, 1.f, 8.f, 1.f, "Cycles per pulsaret");

		std::vector<std::string> stereoLabels;
		stereoLabels.push_back("Off (R follows L)");
		stereoLabels.push_back("Alternate pulsarets L / R");
		configSwitch(STEREO_PARAM, 0.f, 1.f, 0.f, "Channel masking", stereoLabels);

		configParam(BURST_ON_PARAM, 1.f, 8.f, 1.f, "Burst: pulsars paid");
		getParamQuantity(BURST_ON_PARAM)->snapEnabled = true;
		configParam(BURST_OFF_PARAM, 0.f, 8.f, 0.f, "Burst: pulsars withheld");
		getParamQuantity(BURST_OFF_PARAM)->snapEnabled = true;
		configParam(PROB_PARAM, 0.f, 1.f, 1.f, "Payout probability", "%", 0.f, 100.f);

		configParam(FM_CV_PARAM, -1.f, 1.f, 0.f, "FM amount", "%", 0.f, 100.f);
		configParam(FMT_CV_PARAM, -1.f, 1.f, 0.f, "Formant CV amount", "%", 0.f, 100.f);
		configParam(PROB_CV_PARAM, -1.f, 1.f, 0.f, "Probability CV amount", "%", 0.f, 100.f);
		configParam(BURST_CV_PARAM, -1.f, 1.f, 0.f, "Burst CV amount", "%", 0.f, 100.f);
		getParamQuantity(FM_CV_PARAM)->randomizeEnabled = false;
		getParamQuantity(FMT_CV_PARAM)->randomizeEnabled = false;
		getParamQuantity(PROB_CV_PARAM)->randomizeEnabled = false;
		getParamQuantity(BURST_CV_PARAM)->randomizeEnabled = false;

		configInput(VOCT_INPUT, "1V/oct pitch");
		configInput(SYNC_INPUT, "Sync");
		configInput(FM_INPUT, "Frequency modulation");
		configInput(FMT_INPUT, "Formant CV (1V/oct)");
		configInput(PROB_INPUT, "Payout probability CV");
		configInput(BURST_INPUT, "Burst length CV");
		configOutput(OUT_L_OUTPUT, "Left / mono");
		configOutput(OUT_R_OUTPUT, "Right");
		configOutput(TRIG_OUTPUT, "Pulsaret gate");
		configOutput(ENV_OUTPUT, "Pulsaret envelope");
		configLight(HELD_LIGHT, "Pulsar withheld");

		lightDivider.setDivision(256);
		core.seed(random::u32() | 1u);
		setSampleRate(APP->engine->getSampleRate());
	}

	void setSampleRate(float sr) {
		sampleRate = sr;
		dcL.setCutoffFreq(5.f / sr);
		dcR.setCutoffFreq(5.f / sr);
		dcL.reset();
		dcR.reset();
		dec2L.reset(); dec2R.reset();
		dec4L.reset(); dec4R.reset();
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		setSampleRate(e.sampleRate);
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		core.reset();
		heldLevel = 0.f;
		dcL.reset();
		dcR.reset();
	}

	static int factor(int os) {
		return os == OS_4 ? 4 : os == OS_2 ? 2 : 1;
	}

	void process(const ProcessArgs& args) override {
		// --- the fundamental ---------------------------------------------------
		float pitch = params[FREQ_PARAM].getValue() + params[FINE_PARAM].getValue() / 12.f
		              + inputs[VOCT_INPUT].getVoltage();
		float fm = inputs[FM_INPUT].getVoltage() * params[FM_CV_PARAM].getValue();
		bool linear = params[FM_MODE_PARAM].getValue() > 0.5f;
		if (!linear)
			pitch += fm;
		pitch = clamp(pitch, -14.f, 8.f);
		float f0 = dsp::FREQ_C4 * std::pow(2.f, pitch);
		if (linear) {
			// Index 1 at +/-5 V with the attenuverter fully open: the train
			// runs through zero and out the other side, backwards.
			f0 *= 1.f + fm / 5.f;
		}
		float fmax = 0.45f * args.sampleRate;
		f0 = clamp(f0, -fmax, fmax);

		// --- the formant -------------------------------------------------------
		bool track = params[TRACK_PARAM].getValue() > 0.5f;
		float fv = params[FORMANT_PARAM].getValue()
		           + inputs[FMT_INPUT].getVoltage() * params[FMT_CV_PARAM].getValue();
		fv = clamp(fv, -8.f, 10.f);
		float formant = track ? std::fabs(f0) * std::pow(2.f, fv)
		                      : dsp::FREQ_C4 * std::pow(2.f, fv);
		int os = factor(oversample);
		formant = clamp(formant, 1.f, 0.45f * args.sampleRate * (float) os);

		float cycles = clamp(params[CYCLES_PARAM].getValue(), 1.f, 8.f);

		// --- masking -----------------------------------------------------------
		float on = params[BURST_ON_PARAM].getValue();
		if (inputs[BURST_INPUT].isConnected())
			on += inputs[BURST_INPUT].getVoltage() / 10.f * params[BURST_CV_PARAM].getValue() * 8.f;
		int burstOn = (int) clamp(std::round(on), 1.f, 8.f);
		int burstOff = (int) clamp(std::round(params[BURST_OFF_PARAM].getValue()), 0.f, 8.f);

		float prob = params[PROB_PARAM].getValue();
		if (inputs[PROB_INPUT].isConnected())
			prob += inputs[PROB_INPUT].getVoltage() / 10.f * params[PROB_CV_PARAM].getValue();
		prob = clamp(prob, 0.f, 1.f);

		bool stereo = params[STEREO_PARAM].getValue() > 0.5f;

		core.freq = f0;
		core.formant = formant;
		core.cycles = cycles;
		core.wave = (int) clamp(std::round(params[WAVE_PARAM].getValue()), 0.f, (float) (dividend::NUM_WAVES - 1));
		core.window = (int) clamp(std::round(params[WINDOW_PARAM].getValue()), 0.f, (float) (dividend::NUM_WINDOWS - 1));
		core.burstOn = burstOn;
		core.burstOff = burstOff;
		core.prob = prob;
		core.stereo = stereo;
		core.overlap = overlap;

		if (syncTrigger.process(inputs[SYNC_INPUT].getVoltage(), 0.1f, 2.f))
			core.sync();

		// --- the train, oversampled ------------------------------------------
		float dt = args.sampleTime / (float) os;
		float bufL[4], bufR[4];
		PulsarCore::Frame fr;
		bool masked = false;
		for (int i = 0; i < os; i++) {
			core.tick(dt, fr);
			bufL[i] = fr.l;
			bufR[i] = fr.r;
			masked |= fr.masked;
		}
		float l, r;
		if (os == 4) {
			l = dec4L.process(bufL);
			r = dec4R.process(bufR);
		}
		else if (os == 2) {
			l = dec2L.process(bufL);
			r = dec2R.process(bufR);
		}
		else {
			l = bufL[0];
			r = bufR[0];
		}

		if (dcBlock) {
			dcL.process(l);
			dcR.process(r);
			l = dcL.highpass();
			r = dcR.highpass();
		}

		outputs[OUT_L_OUTPUT].setVoltage(clamp(5.f * l, -12.f, 12.f));
		outputs[OUT_R_OUTPUT].setVoltage(clamp(5.f * r, -12.f, 12.f));
		outputs[TRIG_OUTPUT].setVoltage(fr.gate ? 10.f : 0.f);
		outputs[ENV_OUTPUT].setVoltage(10.f * clamp(fr.env, 0.f, 1.f));

		if (masked)
			heldLevel = 1.f;

		// --- lights and panel read-outs ----------------------------------------
		if (lightDivider.process()) {
			float dtl = args.sampleTime * lightDivider.getDivision();
			lights[HELD_LIGHT].setBrightness(heldLevel);
			// Decays over ~150 ms, so a withheld pulsar at LFO rates reads as a
			// flash and steady masking at audio rates reads as a glow.
			heldLevel *= std::exp(-dtl / 0.15f);
			if (heldLevel < 1e-3f) heldLevel = 0.f;

			dispF0 = std::fabs(f0);
			dispFormant = formant;
			dispDuty = std::fabs(f0) * cycles / formant;
			dispTrack = track;
			dispStereo = stereo;
		}
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "oversample", json_integer(oversample));
		json_object_set_new(root, "overlap", json_boolean(overlap));
		json_object_set_new(root, "dcBlock", json_boolean(dcBlock));
		return root;
	}

	void dataFromJson(json_t* root) override {
		json_t* j;
		j = json_object_get(root, "oversample");
		if (j) oversample = clamp((int) json_integer_value(j), 0, NUM_OS - 1);
		j = json_object_get(root, "overlap");
		if (j) overlap = json_boolean_value(j);
		j = json_object_get(root, "dcBlock");
		if (j) dcBlock = json_boolean_value(j);
	}
};


// ---------------------------------------------------------------------------
// Look and feel. The palette, the shared hardware and the entire silkscreen come
// from src/PanelTheme.hpp and Panel.hpp, generated from tools/panels/Dividend.py
// -- see ../../panelkit/README.md. Nothing about the look is written here.

typedef RoundLargeBlackKnob PayoutKnob;
typedef RoundBlackKnob      PanelKnob;


// Panel read-out: the fundamental and the formant, in Hz, plus the duty ratio
// d/p that the two of them and CYCLES add up to. Numerals are DSEG7 (restricted
// to [0-9 . : -]), words stay in the mono face -- see panel::segValue.
struct DividendDisplay : LedDisplay {
	Dividend* module = NULL;

	static void splitHz(float hz, std::string& num, std::string& unit) {
		if (hz < 1000.f) { num = string::f("%.1f", hz);          unit = "Hz"; }
		else             { num = string::f("%.3f", hz / 1000.f); unit = "kHz"; }
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) {
			LedDisplay::drawLayer(args, layer);
			return;
		}
		float f0 = module ? module->dispF0 : dsp::FREQ_C4;
		float fmt = module ? module->dispFormant : 4.f * dsp::FREQ_C4;
		float duty = module ? module->dispDuty : 0.25f;
		bool track = module ? module->dispTrack : true;
		bool stereo = module ? module->dispStereo : false;

		const float pad = 5.f;
		const float rightX = box.size.x - pad;
		const NVGcolor dim = panel::alpha(panel::LIME, 0.55f);
		const panel::TextStyle TAG(panel::Face::Mono, 8.f, dim,
			NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
		const panel::TextStyle NOTE(panel::Face::Mono, 8.f, panel::MINT,
			NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE);

		std::string num, unit;

		// Row 1: the fundamental, and whether the formant rides on it.
		float x = panel::text(args.vg, TAG, pad, 12.f, "FUND");
		splitHz(f0, num, unit);
		panel::segValue(args.vg, x + 3.f, 12.f, 11.f, num, unit, panel::LIME);
		panel::text(args.vg, NOTE, rightX, 12.f, track ? "TRACK" : "ABS");

		// Row 2: the formant, and the duty ratio it makes against the period.
		x = panel::text(args.vg, TAG, pad, 24.f, "FMT ");
		splitHz(fmt, num, unit);
		x = panel::segValue(args.vg, x + 3.f, 24.f, 8.f, num, unit, dim);
		x = panel::text(args.vg, TAG, x + 8.f, 24.f, "D/P");
		panel::segValue(args.vg, x + 3.f, 24.f, 8.f, string::f("%.2f", std::min(duty, 99.99f)),
		                std::string(), dim);
		panel::text(args.vg, NOTE.inked(dim), rightX, 24.f, stereo ? "L/R" : "MONO");
	}
};


struct DividendWidget : ModuleWidget {
	DividendWidget(Dividend* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Dividend.svg")));

		panel::addScrews(this);
		panel::addLabels(this);

		DividendDisplay* display = new DividendDisplay;
		display->module = module;
		display->box.pos = panel::mm(4.2f, 10.2f);
		display->box.size = panel::mm(panel::W - 8.4f, 9.2f);
		addChild(display);

		addParam(createParamCentered<PayoutKnob>(panel::mm(panel::FREQ_POS.x, panel::FREQ_POS.y), module, Dividend::FREQ_PARAM));
		addParam(createParamCentered<PanelKnob>(panel::mm(panel::FINE_POS.x, panel::FINE_POS.y), module, Dividend::FINE_PARAM));
		addParam(createParamCentered<PayoutKnob>(panel::mm(panel::FORMANT_POS.x, panel::FORMANT_POS.y), module, Dividend::FORMANT_PARAM));
		addParam(createParamCentered<CKSS>(panel::mm(panel::TRACK_POS.x, panel::TRACK_POS.y), module, Dividend::TRACK_PARAM));
		addParam(createParamCentered<CKSS>(panel::mm(panel::FM_MODE_POS.x, panel::FM_MODE_POS.y), module, Dividend::FM_MODE_PARAM));

		addParam(createParamCentered<PanelKnob>(panel::mm(panel::WAVE_POS.x, panel::WAVE_POS.y), module, Dividend::WAVE_PARAM));
		addParam(createParamCentered<PanelKnob>(panel::mm(panel::WINDOW_POS.x, panel::WINDOW_POS.y), module, Dividend::WINDOW_PARAM));
		addParam(createParamCentered<PanelKnob>(panel::mm(panel::CYCLES_POS.x, panel::CYCLES_POS.y), module, Dividend::CYCLES_PARAM));
		addParam(createParamCentered<CKSS>(panel::mm(panel::STEREO_POS.x, panel::STEREO_POS.y), module, Dividend::STEREO_PARAM));

		addParam(createParamCentered<PanelKnob>(panel::mm(panel::BURST_ON_POS.x, panel::BURST_ON_POS.y), module, Dividend::BURST_ON_PARAM));
		addParam(createParamCentered<PanelKnob>(panel::mm(panel::BURST_OFF_POS.x, panel::BURST_OFF_POS.y), module, Dividend::BURST_OFF_PARAM));
		addParam(createParamCentered<PanelKnob>(panel::mm(panel::PROB_POS.x, panel::PROB_POS.y), module, Dividend::PROB_PARAM));
		addChild(createLightCentered<MediumLight<panel::LimeLight> >(
		             panel::mm(panel::HELD_POS.x, panel::HELD_POS.y), module, Dividend::HELD_LIGHT));

		addParam(createParamCentered<Trimpot>(panel::mm(panel::FM_CV_POS.x, panel::FM_CV_POS.y), module, Dividend::FM_CV_PARAM));
		addParam(createParamCentered<Trimpot>(panel::mm(panel::FMT_CV_POS.x, panel::FMT_CV_POS.y), module, Dividend::FMT_CV_PARAM));
		addParam(createParamCentered<Trimpot>(panel::mm(panel::PROB_CV_POS.x, panel::PROB_CV_POS.y), module, Dividend::PROB_CV_PARAM));
		addParam(createParamCentered<Trimpot>(panel::mm(panel::BURST_CV_POS.x, panel::BURST_CV_POS.y), module, Dividend::BURST_CV_PARAM));

		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::FM_IN_POS.x, panel::FM_IN_POS.y), module, Dividend::FM_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::FMT_IN_POS.x, panel::FMT_IN_POS.y), module, Dividend::FMT_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::PROB_IN_POS.x, panel::PROB_IN_POS.y), module, Dividend::PROB_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::BURST_IN_POS.x, panel::BURST_IN_POS.y), module, Dividend::BURST_INPUT));

		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::VOCT_POS.x, panel::VOCT_POS.y), module, Dividend::VOCT_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::SYNC_POS.x, panel::SYNC_POS.y), module, Dividend::SYNC_INPUT));
		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::OUT_L_POS.x, panel::OUT_L_POS.y), module, Dividend::OUT_L_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::OUT_R_POS.x, panel::OUT_R_POS.y), module, Dividend::OUT_R_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::TRIG_POS.x, panel::TRIG_POS.y), module, Dividend::TRIG_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::ENV_POS.x, panel::ENV_POS.y), module, Dividend::ENV_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		Dividend* m = dynamic_cast<Dividend*>(module);
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Dividend"));

		std::vector<std::string> osLabels;
		osLabels.push_back("Off (1x)");
		osLabels.push_back("2x oversampling");
		osLabels.push_back("4x oversampling");
		menu->addChild(createIndexSubmenuItem("Band-limiting", osLabels,
			[=]() { return (size_t) m->oversample; },
			[=](size_t v) { m->oversample = (int) v; }));

		std::vector<std::string> ovLabels;
		ovLabels.push_back("Truncate at the period");
		ovLabels.push_back("Overlap (8 voices)");
		menu->addChild(createIndexSubmenuItem("Pulsarets longer than the period", ovLabels,
			[=]() { return (size_t) (m->overlap ? 1 : 0); },
			[=](size_t v) { m->overlap = v == 1; }));

		menu->addChild(createBoolMenuItem("DC blocker", "",
			[=]() { return m->dcBlock; },
			[=](bool v) { m->dcBlock = v; }));
	}
};


Model* modelDividend = createModel<Dividend, DividendWidget>("Dividend");

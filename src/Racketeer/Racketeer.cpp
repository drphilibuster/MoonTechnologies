#include "../plugin.hpp"
#include "../DspCache.hpp"
#include "Panel.hpp"
#include "Pt2399Loop.hpp"

using racketeer::Pt2399Loop;


// The two RANGE settings: the datasheet's range, and how far the pot on the
// PB701 actually pushes the chip.
static const float kShortMax = 0.340f;
static const float kLongMax  = Pt2399Loop::kMaxDelay;

static const float kCutMinLog2 = 5.321928f;    // log2(40 Hz)
static const float kCutMaxLog2 = 14.135709f;   // log2(18 kHz)
static const float kCutDefLog2 = 11.550747f;   // log2(3 kHz)

static const float kRateMinLog2 = -3.321928f;  // log2(0.1 Hz)
static const float kRateMaxLog2 = 5.906891f;   // log2(60 Hz)
static const float kRateDefLog2 = 2.f;         // log2(4 Hz)

static const float kChipNoiseAmount[] = { 0.f, 0.25f, 0.5f, 0.75f, 1.f };
static const int kNumChipNoise = 5;


struct Racketeer;

/** TIME reads in milliseconds, for the range the switch is on. */
struct TimeQuantity : ParamQuantity {
	std::string getDisplayValueString() override;
};


struct Racketeer : Module {
	enum ParamId {
		TIME_PARAM, ECHO_PARAM, CUTOFF_PARAM,
		LAG_PARAM, DRIVE_PARAM, SEED_PARAM, RES_PARAM, THRESH_PARAM,
		POL_PARAM, FILT_PARAM, RANGE_PARAM, RATE_PARAM, CHOP_PARAM,
		TIME_CV_PARAM, ECHO_CV_PARAM, CUTOFF_CV_PARAM, RATE_CV_PARAM,
		NOISE_PARAM, BOOST_PARAM, MUTE_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		IN_INPUT, TIME_INPUT, ECHO_INPUT, CUTOFF_INPUT, RATE_INPUT,
		NOISE_INPUT, BOOST_INPUT, MUTE_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		OUT_OUTPUT, DIRTY_OUTPUT, ENV_OUTPUT, GATE_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		LOOP_LIGHT, CHOP_LIGHT, NOISE_LIGHT, BOOST_LIGHT, MUTE_LIGHT,
		LIGHTS_LEN
	};

	Pt2399Loop loop;
	racketeer::Noise seedNoise;

	// Oversampling: the loop runs at fs * os. The loop's ring is sized for 2x
	// at any engine rate, so flipping this never allocates.
	int oversample = 1;
	int oversampleRequest = 1;
	dsp::Upsampler<2, 8> up;
	dsp::Decimator<2, 8> downOut, downDirty;

	int chipNoiseIdx = 2;
	bool noiseKills = false;     // NOISE button: inject (default) or kill

	dsp::SchmittTrigger noiseTrig, boostTrig, muteTrig;
	dsp::ClockDivider lightDivider;

	// Knob-rate coefficients kept out of the per-sample path; see DspCache.hpp.
	mt::Cache lagSlowC, lagFastC, driveC;

	float timeSlew = 0.5f;       // the optocoupler's state, 0..1 of the range
	float chopPhase = 0.f;
	float chopAmt = 1.f;         // declicked chopper gate in the loop
	float muteAmt = 1.f;         // declicked output gate
	float env = 0.f;
	bool gateHigh = false;

	// Read-outs, published for the panel display from the audio thread.
	float dispDelaySec = 0.3f;
	float dispFsInt = 4550.f;
	float dispBits = 14.f;
	bool  dispLong = true;

	Racketeer() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

		configParam<TimeQuantity>(TIME_PARAM, 0.f, 1.f, 0.5f, "Delay time");
		configParam(ECHO_PARAM, 0.f, 1.5f, 0.7f, "Echo (feedback)", "%", 0.f, 100.f);
		configParam(CUTOFF_PARAM, kCutMinLog2, kCutMaxLog2, kCutDefLog2, "Cutoff", " Hz", 2.f);

		configParam(LAG_PARAM, 0.f, 1.f, 0.3f, "Time lag (optocoupler)", "%", 0.f, 100.f);
		configParam(DRIVE_PARAM, 0.f, 1.f, 0.f, "Input drive", " dB", 0.f, 24.f);
		configParam(SEED_PARAM, 0.f, 1.f, 0.15f, "Seed noise", "%", 0.f, 100.f);
		configParam(RES_PARAM, 0.f, 1.f, 0.f, "Resonance", "%", 0.f, 100.f);
		configParam(THRESH_PARAM, 0.f, 1.f, 0.5f, "Gate threshold", " V", 0.f, 10.f);

		std::vector<std::string> polLabels;
		polLabels.push_back("Inverted");
		polLabels.push_back("Normal");
		configSwitch(POL_PARAM, 0.f, 1.f, 1.f, "Feedback polarity", polLabels);

		std::vector<std::string> filtLabels;
		filtLabels.push_back("After the loop");
		filtLabels.push_back("In the loop");
		configSwitch(FILT_PARAM, 0.f, 1.f, 1.f, "Filter position", filtLabels);

		std::vector<std::string> rangeLabels;
		rangeLabels.push_back("Short (30-340 ms)");
		rangeLabels.push_back("Long (30 ms-1.2 s)");
		configSwitch(RANGE_PARAM, 0.f, 1.f, 1.f, "Delay range", rangeLabels);

		configParam(RATE_PARAM, kRateMinLog2, kRateMaxLog2, kRateDefLog2, "Chopper rate", " Hz", 2.f);

		std::vector<std::string> chopLabels;
		chopLabels.push_back("Off");
		chopLabels.push_back("On");
		configSwitch(CHOP_PARAM, 0.f, 1.f, 0.f, "Chopper", chopLabels);

		configParam(TIME_CV_PARAM, -1.f, 1.f, 0.f, "Time CV", "%", 0.f, 100.f);
		configParam(ECHO_CV_PARAM, -1.f, 1.f, 0.f, "Echo CV", "%", 0.f, 100.f);
		configParam(CUTOFF_CV_PARAM, -1.f, 1.f, 0.f, "Cutoff CV", "%", 0.f, 100.f);
		configParam(RATE_CV_PARAM, -1.f, 1.f, 0.f, "Chopper rate CV", "%", 0.f, 100.f);
		getParamQuantity(TIME_CV_PARAM)->randomizeEnabled = false;
		getParamQuantity(ECHO_CV_PARAM)->randomizeEnabled = false;
		getParamQuantity(CUTOFF_CV_PARAM)->randomizeEnabled = false;
		getParamQuantity(RATE_CV_PARAM)->randomizeEnabled = false;

		configButton(NOISE_PARAM, "Noise");
		configButton(BOOST_PARAM, "Boost");
		configButton(MUTE_PARAM, "Mute");

		configInput(IN_INPUT, "Audio");
		configInput(TIME_INPUT, "Delay time CV");
		configInput(ECHO_INPUT, "Echo CV");
		configInput(CUTOFF_INPUT, "Cutoff CV (1 V/oct)");
		configInput(RATE_INPUT, "Chopper rate CV (1 V/oct)");
		configInput(NOISE_INPUT, "Noise gate");
		configInput(BOOST_INPUT, "Boost gate");
		configInput(MUTE_INPUT, "Mute gate");

		configOutput(OUT_OUTPUT, "Audio");
		configOutput(DIRTY_OUTPUT, "Pre-filter tap");
		configOutput(ENV_OUTPUT, "Loop envelope");
		configOutput(GATE_OUTPUT, "Threshold gate");

		configBypass(IN_INPUT, OUT_OUTPUT);

		lightDivider.setDivision(256);
		loop.setSampleRate(APP->engine->getSampleRate(), oversample);
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		loop.setSampleRate(e.sampleRate, oversample);
		up.reset();
		downOut.reset();
		downDirty.reset();
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		loop.reset();
		up.reset();
		downOut.reset();
		downDirty.reset();
		env = 0.f;
		gateHigh = false;
		chopPhase = 0.f;
		chopAmt = 1.f;
		muteAmt = 1.f;
	}

	/** The delay the TIME knob asks for, before the optocoupler. */
	float delayFor(float t, bool longRange) const {
		float dmax = longRange ? kLongMax : kShortMax;
		return Pt2399Loop::kMinDelay * std::pow(dmax / Pt2399Loop::kMinDelay, clamp(t, 0.f, 1.f));
	}

	void process(const ProcessArgs& args) override {
		// Oversampling changes are applied here, on the audio thread, so the
		// loop's rate and its filters are never set from the UI mid-sample.
		if (oversampleRequest != oversample) {
			oversample = oversampleRequest;
			loop.setSampleRate(args.sampleRate, oversample);
			up.reset();
			downOut.reset();
			downDirty.reset();
		}

		// --- buttons and their gates ---------------------------------------
		// The triggers run every sample, unconditionally, so a gate edge that
		// arrives while the button is held is not lost behind the OR.
		noiseTrig.process(inputs[NOISE_INPUT].getVoltage(), 0.1f, 1.f);
		boostTrig.process(inputs[BOOST_INPUT].getVoltage(), 0.1f, 1.f);
		muteTrig.process(inputs[MUTE_INPUT].getVoltage(), 0.1f, 1.f);
		bool noiseOn = params[NOISE_PARAM].getValue() > 0.5f || noiseTrig.isHigh();
		bool boostOn = params[BOOST_PARAM].getValue() > 0.5f || boostTrig.isHigh();
		bool muteOn  = params[MUTE_PARAM].getValue() > 0.5f  || muteTrig.isHigh();

		// --- delay time: knob + CV, through the optocoupler ------------------
		bool longRange = params[RANGE_PARAM].getValue() > 0.5f;
		float t = params[TIME_PARAM].getValue();
		if (inputs[TIME_INPUT].isConnected())
			t += inputs[TIME_INPUT].getVoltage() / 10.f * params[TIME_CV_PARAM].getValue();
		t = clamp(t, 0.f, 1.f);

		// The TLP521 lights an LED into a phototransistor that stands in for
		// the R pot: more light, less R, shorter delay. Light comes on faster
		// than it goes off, so a jump to shorter is quicker than one to longer.
		float lag = params[LAG_PARAM].getValue();
		if (lag <= 0.001f) {
			timeSlew = t;
		}
		else {
			// LAG is a knob, so both coefficients hold still between moves.
			// Caching each separately means the rise/fall branch never forces a
			// recompute the way one cache keyed on the chosen tau would.
			float key = lag * args.sampleRate;   // covers a rate change too
			float aSlow = lagSlowC.get(key, [&](float) {
				return 1.f - std::exp(-args.sampleTime / (0.002f * std::pow(1000.f, lag)));
			});
			float aFast = lagFastC.get(key, [&](float) {
				return 1.f - std::exp(-args.sampleTime / (0.002f * std::pow(1000.f, lag) * 0.35f));
			});
			timeSlew += (t < timeSlew ? aFast : aSlow) * (t - timeSlew);
		}
		float delaySec = delayFor(timeSlew, longRange);
		loop.delaySec = delaySec;

		// --- the rest of the loop's settings --------------------------------
		float echo = params[ECHO_PARAM].getValue();
		if (inputs[ECHO_INPUT].isConnected())
			echo += inputs[ECHO_INPUT].getVoltage() / 10.f * params[ECHO_CV_PARAM].getValue() * 1.5f;
		loop.echo = clamp(echo, 0.f, 1.5f);
		loop.polarity = params[POL_PARAM].getValue() > 0.5f ? 1.f : -1.f;
		loop.loopGain = boostOn ? 2.f : 1.f;
		loop.filterInLoop = params[FILT_PARAM].getValue() > 0.5f;
		loop.kill = noiseKills && noiseOn;
		loop.chipNoise = kChipNoiseAmount[chipNoiseIdx];

		float cut = params[CUTOFF_PARAM].getValue();
		if (inputs[CUTOFF_INPUT].isConnected())
			cut += inputs[CUTOFF_INPUT].getVoltage() * params[CUTOFF_CV_PARAM].getValue();
		cut = clamp(cut, kCutMinLog2, kCutMaxLog2);
		// CUTOFF is already log2, so this is exp2 -- no need for the general pow.
		loop.setFilter(dsp::exp2_taylor5(cut), params[RES_PARAM].getValue());

		// --- the chopper: the mute button pressed by a square LFO -----------
		bool chopOn = params[CHOP_PARAM].getValue() > 0.5f;
		float rate = params[RATE_PARAM].getValue();
		if (inputs[RATE_INPUT].isConnected())
			rate += inputs[RATE_INPUT].getVoltage() * params[RATE_CV_PARAM].getValue();
		rate = clamp(rate, kRateMinLog2, kRateMaxLog2);
		chopPhase += dsp::exp2_taylor5(rate) * args.sampleTime;
		if (chopPhase >= 1.f) chopPhase -= 1.f;
		bool chopGate = chopPhase < 0.5f;
		float chopTarget = (chopOn && !chopGate) ? 0.f : 1.f;
		// 0.3 ms: enough to take the digital edge off, not enough to soften the click.
		chopAmt += (chopTarget - chopAmt) * std::min(1.f, args.sampleTime / 0.0003f);
		loop.chop = chopAmt;

		// --- what goes into the loop ----------------------------------------
		float in = clamp(inputs[IN_INPUT].getVoltageSum(), -12.f, 12.f) / 5.f;
		// DRIVE has no CV, so this dB-to-linear is a knob-rate value.
		in *= driveC.get(params[DRIVE_PARAM].getValue(),
			[](float k) { return std::pow(10.f, k * 24.f / 20.f); });
		float seed = params[SEED_PARAM].getValue();
		float seedAmp = seed * seed;                 // audible floor at the top, nothing at zero
		float injectAmp = (noiseOn && !noiseKills) ? 0.7f : 0.f;

		// --- run the loop, oversampled or not --------------------------------
		float out, dirty;
		if (oversample == 2) {
			float xs[2], os[2], ds[2];
			up.process(in, xs);
			for (int i = 0; i < 2; i++) {
				float x = xs[i] + seedAmp * seedNoise.white() + injectAmp * seedNoise.white();
				os[i] = loop.process(x, ds[i]);
			}
			out = downOut.process(os);
			dirty = downDirty.process(ds);
		}
		else {
			float x = in + seedAmp * seedNoise.white() + injectAmp * seedNoise.white();
			out = loop.process(x, dirty);
		}
		// --- envelope and threshold gate: the loop's own loudness ---------
		float level = std::fabs(out);
		float envCoef = level > env ? args.sampleTime / 0.005f : args.sampleTime / 0.100f;
		env += (level - env) * std::min(1.f, envCoef);
		float thresh = params[THRESH_PARAM].getValue();
		if (gateHigh) {
			if (env < thresh * 0.8f) gateHigh = false;
		}
		else if (env > thresh && thresh > 0.001f) {
			gateHigh = true;
		}

		// --- outputs -----------------------------------------------------------
		float outGain = boostOn ? 2.f : 1.f;
		muteAmt += ((muteOn ? 0.f : 1.f) - muteAmt) * std::min(1.f, args.sampleTime / 0.002f);
		outputs[OUT_OUTPUT].setVoltage(clamp(out * 5.f * outGain * muteAmt, -12.f, 12.f));
		outputs[DIRTY_OUTPUT].setVoltage(clamp(dirty * 5.f * outGain * muteAmt, -12.f, 12.f));
		outputs[ENV_OUTPUT].setVoltage(clamp(env * 10.f, 0.f, 10.f));
		outputs[GATE_OUTPUT].setVoltage(gateHigh ? 10.f : 0.f);

		// --- lights and read-outs ----------------------------------------------
		if (lightDivider.process()) {
			float dt = args.sampleTime * lightDivider.getDivision();
			lights[LOOP_LIGHT].setBrightnessSmooth(clamp(env * 1.5f, 0.f, 1.f), dt);
			lights[CHOP_LIGHT].setBrightness(chopOn ? (chopGate ? 1.f : 0.f) : 0.f);
			lights[NOISE_LIGHT].setBrightness(noiseOn ? 1.f : 0.f);
			lights[BOOST_LIGHT].setBrightness(boostOn ? 1.f : 0.f);
			lights[MUTE_LIGHT].setBrightness(muteOn ? 1.f : 0.f);

			dispDelaySec = delaySec;
			dispFsInt = loop.fsInternal;
			dispBits = loop.bits;
			dispLong = longRange;
		}
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "oversample", json_integer(oversampleRequest));
		json_object_set_new(root, "chipNoise", json_integer(chipNoiseIdx));
		json_object_set_new(root, "noiseKills", json_boolean(noiseKills));
		return root;
	}

	void dataFromJson(json_t* root) override {
		json_t* j;
		j = json_object_get(root, "oversample");
		if (j) oversampleRequest = json_integer_value(j) >= 2 ? 2 : 1;
		j = json_object_get(root, "chipNoise");
		if (j) chipNoiseIdx = (int)clamp((int)json_integer_value(j), 0, kNumChipNoise - 1);
		j = json_object_get(root, "noiseKills");
		if (j) noiseKills = json_boolean_value(j);
	}
};


std::string TimeQuantity::getDisplayValueString() {
	Racketeer* m = dynamic_cast<Racketeer*>(module);
	bool longRange = m ? m->params[Racketeer::RANGE_PARAM].getValue() > 0.5f : true;
	float sec = m ? m->delayFor(getValue(), longRange)
	              : Pt2399Loop::kMinDelay * std::pow(kLongMax / Pt2399Loop::kMinDelay, getValue());
	if (sec < 1.f)
		return string::f("%.0f ms", sec * 1000.f);
	return string::f("%.2f s", sec);
}


// ---------------------------------------------------------------------------
// Look and feel. The palette, the shared hardware and the entire silkscreen come
// from src/PanelTheme.hpp and Panel.hpp, generated by tools/panels/Racketeer.py.

typedef RoundLargeBlackKnob LoopKnob;
typedef RoundBlackKnob      PanelKnob;


// The chip's condition: the delay it is running, and the sample rate it has
// dropped to in order to run it. Numerals in the segment face, which carries
// only [0-9 . : -]; units and words stay in the mono face.
struct RacketeerDisplay : LedDisplay {
	Racketeer* module = NULL;

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) {
			LedDisplay::drawLayer(args, layer);
			return;
		}
		float sec = module ? module->dispDelaySec : 0.3f;
		float fsInt = module ? module->dispFsInt : 4550.f;
		float bits = module ? module->dispBits : 14.f;
		bool longRange = module ? module->dispLong : true;

		const float pad = 5.f;
		const float rightX = box.size.x - pad;
		const NVGcolor dim = panel::alpha(panel::LIME, 0.55f);
		const panel::TextStyle TAG(panel::Face::Mono, 8.f, dim,
			NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
		const panel::TextStyle RIGHT(panel::Face::Mono, 8.f, panel::MINT,
			NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE);

		// Row 1: the delay, and which range the switch is on.
		std::string num, unit;
		if (sec < 1.f) { num = string::f("%.0f", sec * 1000.f); unit = "ms"; }
		else           { num = string::f("%.2f", sec);          unit = "s"; }
		panel::segValue(args.vg, pad, 12.f, 11.f, num, unit, panel::LIME);
		panel::text(args.vg, RIGHT, rightX, 12.f, longRange ? "LONG" : "SHORT");

		// Row 2: the chip's sample rate and the bits it has left.
		float x = panel::text(args.vg, TAG, pad, 24.f, "FS");
		if (fsInt >= 10000.f) { num = string::f("%.1f", fsInt / 1000.f); unit = "kHz"; }
		else if (fsInt >= 1000.f) { num = string::f("%.2f", fsInt / 1000.f); unit = "kHz"; }
		else { num = string::f("%.0f", fsInt); unit = "Hz"; }
		panel::segValue(args.vg, x + 2.5f, 24.f, 8.f, num, unit, dim);
		panel::text(args.vg, RIGHT.inked(dim), rightX, 24.f, string::f("%.0f BIT", bits));
	}
};


struct RacketeerWidget : ModuleWidget {
	RacketeerWidget(Racketeer* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Racketeer.svg")));

		panel::addScrews(this);
		panel::addLabels(this);

		RacketeerDisplay* display = new RacketeerDisplay;
		display->module = module;
		display->box.pos = panel::mm(panel::GLASS_X, panel::GLASS_Y);
		display->box.size = panel::mm(panel::GLASS_W, panel::GLASS_H);
		addChild(display);

		addParam(createParamCentered<LoopKnob>(panel::mm(panel::TIME_POS.x, panel::TIME_POS.y), module, Racketeer::TIME_PARAM));
		addParam(createParamCentered<LoopKnob>(panel::mm(panel::ECHO_POS.x, panel::ECHO_POS.y), module, Racketeer::ECHO_PARAM));
		addParam(createParamCentered<LoopKnob>(panel::mm(panel::CUTOFF_POS.x, panel::CUTOFF_POS.y), module, Racketeer::CUTOFF_PARAM));

		addParam(createParamCentered<Trimpot>(panel::mm(panel::LAG_POS.x, panel::LAG_POS.y), module, Racketeer::LAG_PARAM));
		addParam(createParamCentered<Trimpot>(panel::mm(panel::DRIVE_POS.x, panel::DRIVE_POS.y), module, Racketeer::DRIVE_PARAM));
		addParam(createParamCentered<Trimpot>(panel::mm(panel::SEED_POS.x, panel::SEED_POS.y), module, Racketeer::SEED_PARAM));
		addParam(createParamCentered<Trimpot>(panel::mm(panel::RES_POS.x, panel::RES_POS.y), module, Racketeer::RES_PARAM));
		addParam(createParamCentered<Trimpot>(panel::mm(panel::THRESH_POS.x, panel::THRESH_POS.y), module, Racketeer::THRESH_PARAM));

		addParam(createParamCentered<CKSS>(panel::mm(panel::POL_POS.x, panel::POL_POS.y), module, Racketeer::POL_PARAM));
		addParam(createParamCentered<CKSS>(panel::mm(panel::FILT_POS.x, panel::FILT_POS.y), module, Racketeer::FILT_PARAM));
		addParam(createParamCentered<CKSS>(panel::mm(panel::RANGE_POS.x, panel::RANGE_POS.y), module, Racketeer::RANGE_PARAM));
		addParam(createParamCentered<Trimpot>(panel::mm(panel::RATE_POS.x, panel::RATE_POS.y), module, Racketeer::RATE_PARAM));
		addParam(createParamCentered<CKSS>(panel::mm(panel::CHOP_POS.x, panel::CHOP_POS.y), module, Racketeer::CHOP_PARAM));

		addParam(createParamCentered<Trimpot>(panel::mm(panel::TIME_CV_POS.x, panel::TIME_CV_POS.y), module, Racketeer::TIME_CV_PARAM));
		addParam(createParamCentered<Trimpot>(panel::mm(panel::ECHO_CV_POS.x, panel::ECHO_CV_POS.y), module, Racketeer::ECHO_CV_PARAM));
		addParam(createParamCentered<Trimpot>(panel::mm(panel::CUTOFF_CV_POS.x, panel::CUTOFF_CV_POS.y), module, Racketeer::CUTOFF_CV_PARAM));
		addParam(createParamCentered<Trimpot>(panel::mm(panel::RATE_CV_POS.x, panel::RATE_CV_POS.y), module, Racketeer::RATE_CV_PARAM));

		addParam(createLightParamCentered<VCVLightBezel<panel::PaperLight> >(
		             panel::mm(panel::NOISE_POS.x, panel::NOISE_POS.y), module, Racketeer::NOISE_PARAM, Racketeer::NOISE_LIGHT));
		addParam(createLightParamCentered<VCVLightBezel<panel::ClayLight> >(
		             panel::mm(panel::BOOST_POS.x, panel::BOOST_POS.y), module, Racketeer::BOOST_PARAM, Racketeer::BOOST_LIGHT));
		addParam(createLightParamCentered<VCVLightBezel<panel::PaperLight> >(
		             panel::mm(panel::MUTE_POS.x, panel::MUTE_POS.y), module, Racketeer::MUTE_PARAM, Racketeer::MUTE_LIGHT));

		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::TIME_IN_POS.x, panel::TIME_IN_POS.y), module, Racketeer::TIME_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::ECHO_IN_POS.x, panel::ECHO_IN_POS.y), module, Racketeer::ECHO_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::CUTOFF_IN_POS.x, panel::CUTOFF_IN_POS.y), module, Racketeer::CUTOFF_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::RATE_IN_POS.x, panel::RATE_IN_POS.y), module, Racketeer::RATE_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::NOISE_IN_POS.x, panel::NOISE_IN_POS.y), module, Racketeer::NOISE_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::BOOST_IN_POS.x, panel::BOOST_IN_POS.y), module, Racketeer::BOOST_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::MUTE_IN_POS.x, panel::MUTE_IN_POS.y), module, Racketeer::MUTE_INPUT));

		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::IN_POS.x, panel::IN_POS.y), module, Racketeer::IN_INPUT));
		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::ENV_OUT_POS.x, panel::ENV_OUT_POS.y), module, Racketeer::ENV_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::GATE_OUT_POS.x, panel::GATE_OUT_POS.y), module, Racketeer::GATE_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::DIRTY_OUT_POS.x, panel::DIRTY_OUT_POS.y), module, Racketeer::DIRTY_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::OUT_POS.x, panel::OUT_POS.y), module, Racketeer::OUT_OUTPUT));

		addChild(createLightCentered<SmallLight<panel::LimeLight> >(
		             panel::mm(panel::LOOP_POS.x, panel::LOOP_POS.y), module, Racketeer::LOOP_LIGHT));
		addChild(createLightCentered<SmallLight<panel::MintLight> >(
		             panel::mm(panel::CHOP_LED_POS.x, panel::CHOP_LED_POS.y), module, Racketeer::CHOP_LIGHT));
	}

	void appendContextMenu(Menu* menu) override {
		Racketeer* m = dynamic_cast<Racketeer*>(module);
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Racketeer"));

		menu->addChild(createIndexSubmenuItem("Oversampling",
			{"Off", "2x"},
			[=]() { return m->oversampleRequest == 2 ? 1 : 0; },
			[=](int v) { m->oversampleRequest = v ? 2 : 1; }));

		menu->addChild(createIndexSubmenuItem("Chip noise",
			{"None", "Subtle", "Stock", "Filthy", "Ruined"},
			[=]() { return m->chipNoiseIdx; },
			[=](int v) { m->chipNoiseIdx = clamp(v, 0, kNumChipNoise - 1); }));

		menu->addChild(createIndexSubmenuItem("NOISE button",
			{"Injects noise", "Kills the loop"},
			[=]() { return m->noiseKills ? 1 : 0; },
			[=](int v) { m->noiseKills = (v == 1); }));
	}
};


Model* modelRacketeer = createModel<Racketeer, RacketeerWidget>("Racketeer");

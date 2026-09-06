#include "../plugin.hpp"
#include "Panel.hpp"
#include "Filters.hpp"

using deduction::Voice;
using deduction::Coeffs;


// Long names for the MODEL switch's tooltip/menu; short ones for the read-out,
// which has room for about seven characters beside the frequency.
static const char* kModelLong[] = {
	"PAiA 2720-3L", "Escobedo Q&D", "Korg35", "MS-20 OTA",
	"EFM Moog-type high-pass", "Synthrotek DIRT"
};
static const char* kModelShort[] = {
	"PAIA", "Q&D", "KORG35", "MS-20", "EFM HP", "DIRT"
};

static const float kFreqMinLog2 = 4.321928f;    // log2(20)
static const float kFreqMaxLog2 = 14.287712f;   // log2(20000)
static const float kFreqDefLog2 = 9.965784f;    // log2(1000)


struct Deduction : Module {
	enum ParamId {
		FREQ_PARAM, FREQ_CV_PARAM, MODEL_PARAM, MODEL_CV_PARAM,
		RES_PARAM, RES_CV_PARAM, DRIVE_PARAM, DRIVE_CV_PARAM,
		RESPONSE_PARAM, PARAMS_LEN
	};
	enum InputId {
		FREQ_CV_INPUT, RES_CV_INPUT, DRIVE_CV_INPUT, MODEL_CV_INPUT,
		LP_INPUT, HP_INPUT, INPUTS_LEN
	};
	enum OutputId {
		OUT_OUTPUT, OUTPUTS_LEN
	};
	enum LightId {
		SAT_LIGHT, LIGHTS_LEN
	};

	// One voice, one oversampler pair, per polyphony channel.
	Voice voice[PORT_MAX_CHANNELS];
	dsp::Upsampler<2, 8> upLp[PORT_MAX_CHANNELS], upHp[PORT_MAX_CHANNELS];
	dsp::Decimator<2, 8> dn[PORT_MAX_CHANNELS];
	float freqSmooth[PORT_MAX_CHANNELS];   // log2 Hz, slewed so CV steps don't zipper

	dsp::ClockDivider lightDivider;
	float freqSlewCoef = 1.f;              // recomputed on sample-rate change
	float fadeIncPerSample = 1.f;          // 1 / (0.02 s crossfade * sampleRate)

	bool oversample = false;
	bool oversampleRequest = false;

	// Read-outs, published for the panel display from the audio thread.
	int dispModel = deduction::KORG35;
	float dispFreqHz = 1000.f;
	float dispRes = 0.f;
	float dispDrive = 0.5f;

	Deduction() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

		configParam(FREQ_PARAM, kFreqMinLog2, kFreqMaxLog2, kFreqDefLog2,
		            "Cutoff frequency", " Hz", 2.f);
		configParam(FREQ_CV_PARAM, -1.f, 1.f, 0.f, "Cutoff CV amount", "%", 0.f, 100.f);
		getParamQuantity(FREQ_CV_PARAM)->randomizeEnabled = false;

		std::vector<std::string> modelLabels;
		for (int i = 0; i < deduction::NUM_MODELS; i++)
			modelLabels.push_back(kModelLong[i]);
		configSwitch(MODEL_PARAM, 0.f, (float)(deduction::NUM_MODELS - 1),
		             (float)deduction::KORG35, "Model", modelLabels);
		configParam(MODEL_CV_PARAM, -1.f, 1.f, 0.f, "Model CV amount", "%", 0.f, 100.f);
		getParamQuantity(MODEL_CV_PARAM)->randomizeEnabled = false;

		configParam(RES_PARAM, 0.f, 1.f, 0.f, "Resonance", "%", 0.f, 100.f);
		configParam(RES_CV_PARAM, -1.f, 1.f, 0.f, "Resonance CV amount", "%", 0.f, 100.f);
		getParamQuantity(RES_CV_PARAM)->randomizeEnabled = false;

		configParam(DRIVE_PARAM, 0.f, 1.f, 0.5f, "Drive", "%", 0.f, 100.f);
		configParam(DRIVE_CV_PARAM, -1.f, 1.f, 0.f, "Drive CV amount", "%", 0.f, 100.f);
		getParamQuantity(DRIVE_CV_PARAM)->randomizeEnabled = false;

		std::vector<std::string> respLabels;
		respLabels.push_back("Normal");
		respLabels.push_back("Inverted");
		configSwitch(RESPONSE_PARAM, 0.f, 1.f, 0.f, "Cutoff CV response", respLabels);

		configInput(FREQ_CV_INPUT, "Cutoff CV");
		configInput(RES_CV_INPUT, "Resonance CV");
		configInput(DRIVE_CV_INPUT, "Drive CV");
		configInput(MODEL_CV_INPUT, "Model CV");
		configInput(LP_INPUT, "Low-pass path");
		configInput(HP_INPUT, "High-pass path");
		configOutput(OUT_OUTPUT, "Filtered audio");

		configBypass(LP_INPUT, OUT_OUTPUT);

		lightDivider.setDivision(512);
		for (int c = 0; c < PORT_MAX_CHANNELS; c++)
			freqSmooth[c] = kFreqDefLog2;
		setRate(APP->engine->getSampleRate());
	}

	void setRate(float sr) {
		// ~5 ms one-pole for the cutoff, so audio-rate CV cannot zipper it.
		freqSlewCoef = 1.f - std::exp(-1.f / (0.005f * sr));
		fadeIncPerSample = 1.f / (0.02f * sr);
		for (int c = 0; c < PORT_MAX_CHANNELS; c++)
			voice[c].setRate(sr);
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		setRate(e.sampleRate);
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		for (int c = 0; c < PORT_MAX_CHANNELS; c++) {
			voice[c].reset();
			freqSmooth[c] = kFreqDefLog2;
			upLp[c].reset();
			upHp[c].reset();
			dn[c].reset();
		}
	}

	void process(const ProcessArgs& args) override {
		// Oversampling changes land here, on the audio thread, so a resampler is
		// never reset under a sample it is producing.
		if (oversampleRequest != oversample) {
			oversample = oversampleRequest;
			for (int c = 0; c < PORT_MAX_CHANNELS; c++) {
				upLp[c].reset();
				upHp[c].reset();
				dn[c].reset();
			}
		}

		int channels = std::max(inputs[LP_INPUT].getChannels(), inputs[HP_INPUT].getChannels());
		channels = std::max(channels, 1);

		float freqBase = params[FREQ_PARAM].getValue();
		float freqCvAmt = params[FREQ_CV_PARAM].getValue();
		float resBase = params[RES_PARAM].getValue();
		float resCvAmt = params[RES_CV_PARAM].getValue();
		float driveBase = params[DRIVE_PARAM].getValue();
		float driveCvAmt = params[DRIVE_CV_PARAM].getValue();
		float modelBase = params[MODEL_PARAM].getValue();
		float modelCvAmt = params[MODEL_CV_PARAM].getValue();
		// The Q&D's CV Response switch: NRM tracks CUTOFF CV upward, INV downward.
		float respSign = params[RESPONSE_PARAM].getValue() > 0.5f ? -1.f : 1.f;

		bool useHp = inputs[HP_INPUT].isConnected();
		bool freqCvConnected = inputs[FREQ_CV_INPUT].isConnected();
		bool resCvConnected = inputs[RES_CV_INPUT].isConnected();
		bool driveCvConnected = inputs[DRIVE_CV_INPUT].isConnected();
		bool modelCvConnected = inputs[MODEL_CV_INPUT].isConnected();

		float nyquist = args.sampleRate * 0.49f;
		int factor = oversample ? 2 : 1;
		float fadeInc = fadeIncPerSample / (float)factor;

		float maxHot = 0.f;
		int showModel = deduction::KORG35;
		float showFreqHz = 1000.f, showRes = 0.f, showDrive = 0.5f;

		for (int c = 0; c < channels; c++) {
			// --- cutoff: 1 V/oct at the CUTOFF trim's full clockwise ------------
			float freqLog2 = freqBase;
			if (freqCvConnected)
				freqLog2 += respSign * freqCvAmt * inputs[FREQ_CV_INPUT].getPolyVoltage(c);
			freqLog2 = clamp(freqLog2, kFreqMinLog2, kFreqMaxLog2);
			freqSmooth[c] += freqSlewCoef * (freqLog2 - freqSmooth[c]);
			float fc = std::fmin(std::pow(2.f, freqSmooth[c]), nyquist);
			float g = std::tan(deduction::kPi * fc / args.sampleRate);
			float G = g / (1.f + g);

			// --- resonance and drive: attenuverters over 10 V -------------------
			float res = resBase;
			if (resCvConnected)
				res += resCvAmt * inputs[RES_CV_INPUT].getPolyVoltage(c) * 0.1f;
			res = clamp(res, 0.f, 1.f);

			float drive = driveBase;
			if (driveCvConnected)
				drive += driveCvAmt * inputs[DRIVE_CV_INPUT].getPolyVoltage(c) * 0.1f;
			drive = clamp(drive, 0.f, 1.f);

			// --- model: the same attenuverter convention, over the full 6 slots -
			float modelF = modelBase;
			if (modelCvConnected)
				modelF += modelCvAmt * inputs[MODEL_CV_INPUT].getPolyVoltage(c) * 0.1f
				          * (float)(deduction::NUM_MODELS - 1);
			int m = (int)clamp(std::round(modelF), 0.f, (float)(deduction::NUM_MODELS - 1));
			voice[c].select(m);

			Coeffs co;
			co.g = g;
			co.G = G;
			co.res = res;
			co.drive = drive;

			// 1.0 inside the filter cores is 5 V, so the knees land where the
			// original circuits' diodes and transistors have theirs.
			float lpIn = inputs[LP_INPUT].getPolyVoltage(c) * 0.2f;
			float hpIn = useHp ? inputs[HP_INPUT].getPolyVoltage(c) * 0.2f : 0.f;

			float y;
			if (oversample) {
				float lpBuf[2], hpBuf[2], outBuf[2];
				upLp[c].process(lpIn, lpBuf);
				upHp[c].process(hpIn, hpBuf);
				outBuf[0] = voice[c].process(lpBuf[0], hpBuf[0], useHp, co, fadeInc);
				outBuf[1] = voice[c].process(lpBuf[1], hpBuf[1], useHp, co, fadeInc);
				y = dn[c].process(outBuf);
			}
			else {
				y = voice[c].process(lpIn, hpIn, useHp, co, fadeInc);
			}

			outputs[OUT_OUTPUT].setVoltage(clamp(y * 5.f, -12.f, 12.f), c);

			maxHot = std::fmax(maxHot, voice[c].hot);
			if (c == 0) {
				showModel = m;
				showFreqHz = fc;
				showRes = res;
				showDrive = drive;
			}
		}
		outputs[OUT_OUTPUT].setChannels(channels);

		if (lightDivider.process()) {
			float dt = args.sampleTime * lightDivider.getDivision();
			// The stages clip smoothly from about 1x their normalised knee to 3x,
			// where the Pade tanh reaches its hard ceiling -- see Filters.hpp.
			lights[SAT_LIGHT].setBrightnessSmooth(clamp((maxHot - 1.f) / 2.f, 0.f, 1.f), dt);
			dispModel = showModel;
			dispFreqHz = showFreqHz;
			dispRes = showRes;
			dispDrive = showDrive;
		}
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "oversample", json_boolean(oversampleRequest));
		return root;
	}

	void dataFromJson(json_t* root) override {
		json_t* j = json_object_get(root, "oversample");
		if (j) oversampleRequest = json_boolean_value(j);
	}
};


// ---------------------------------------------------------------------------
// Look and feel. The palette, the shared hardware and the entire silkscreen
// come from src/PanelTheme.hpp, generated by tools/panels/Deduction.py -- see
// ../../panelkit/README.md. Nothing about the look is written here.

typedef RoundLargeBlackKnob BigKnob;
typedef RoundBlackKnob      PanelKnob;


// ---------------------------------------------------------------------------
// Panel display: which model is loaded, its cutoff, and where RES/DRIVE sit.
// Numerals are DSEG7; words stay in Share Tech Mono -- see PanelTheme.hpp's
// segValue for why the two faces can never mix in one run.

struct DeductionDisplay : LedDisplay {
	Deduction* module = NULL;

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) {
			LedDisplay::drawLayer(args, layer);
			return;
		}
		int model = module ? module->dispModel : (int)deduction::KORG35;
		float hz = module ? module->dispFreqHz : 1000.f;
		float res = module ? module->dispRes : 0.f;
		float drive = module ? module->dispDrive : 0.5f;

		const float pad = 5.f;
		const float rightX = box.size.x - pad;

		const panel::TextStyle NAME(panel::Face::Mono, 10.f, panel::MINT,
			NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE, -0.5f);
		const panel::TextStyle TAG(panel::Face::Mono, 8.f, panel::alpha(panel::LIME, 0.55f),
			NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);

		// Row 1: cutoff, and which of the six models is loaded.
		std::string num, unit;
		if (hz < 1000.f) { num = string::f("%.0f", hz);        unit = "Hz"; }
		else             { num = string::f("%.2f", hz * 0.001f); unit = "kHz"; }
		panel::segValue(args.vg, pad, 12.f, 11.f, num, unit, panel::LIME);
		panel::text(args.vg, NAME, rightX, 12.f, kModelShort[model]);

		// Row 2: where RES and DRIVE stand once their own CV is added in.
		panel::text(args.vg, TAG, pad, 24.f,
			string::f("RES %.0f%%", res * 100.f));
		panel::text(args.vg, TAG.aligned(NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE), rightX, 24.f,
			string::f("DRV %.0f%%", drive * 100.f));
	}
};


struct DeductionWidget : ModuleWidget {
	DeductionWidget(Deduction* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Deduction.svg")));

		panel::addScrews(this);
		panel::addLabels(this);

		DeductionDisplay* display = new DeductionDisplay;
		display->module = module;
		display->box.pos = panel::mm(panel::GLASS_X, panel::GLASS_Y);
		display->box.size = panel::mm(panel::GLASS_W, panel::GLASS_H);
		addChild(display);

		addParam(createParamCentered<BigKnob>(panel::mm(panel::FREQ_POS.x, panel::FREQ_POS.y), module, Deduction::FREQ_PARAM));
		addParam(createParamCentered<BigKnob>(panel::mm(panel::MODEL_POS.x, panel::MODEL_POS.y), module, Deduction::MODEL_PARAM));

		addParam(createParamCentered<PanelKnob>(panel::mm(panel::RES_POS.x, panel::RES_POS.y), module, Deduction::RES_PARAM));
		addParam(createParamCentered<PanelKnob>(panel::mm(panel::DRIVE_POS.x, panel::DRIVE_POS.y), module, Deduction::DRIVE_PARAM));
		addParam(createParamCentered<CKSS>(panel::mm(panel::RESPONSE_POS.x, panel::RESPONSE_POS.y), module, Deduction::RESPONSE_PARAM));

		addParam(createParamCentered<Trimpot>(panel::mm(panel::CV_AMT_POS.x, panel::CV_AMT_POS.y), module, Deduction::FREQ_CV_PARAM));
		addParam(createParamCentered<Trimpot>(panel::mm(panel::RES_CV_POS.x, panel::RES_CV_POS.y), module, Deduction::RES_CV_PARAM));
		addParam(createParamCentered<Trimpot>(panel::mm(panel::DRIVE_CV_POS.x, panel::DRIVE_CV_POS.y), module, Deduction::DRIVE_CV_PARAM));
		addParam(createParamCentered<Trimpot>(panel::mm(panel::MODEL_CV_POS.x, panel::MODEL_CV_POS.y), module, Deduction::MODEL_CV_PARAM));

		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::CV_IN_POS.x, panel::CV_IN_POS.y), module, Deduction::FREQ_CV_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::RES_IN_POS.x, panel::RES_IN_POS.y), module, Deduction::RES_CV_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::DRIVE_IN_POS.x, panel::DRIVE_IN_POS.y), module, Deduction::DRIVE_CV_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::MODEL_IN_POS.x, panel::MODEL_IN_POS.y), module, Deduction::MODEL_CV_INPUT));

		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::LP_IN_POS.x, panel::LP_IN_POS.y), module, Deduction::LP_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::HP_IN_POS.x, panel::HP_IN_POS.y), module, Deduction::HP_INPUT));
		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::OUT_POS.x, panel::OUT_POS.y), module, Deduction::OUT_OUTPUT));

		addChild(createLightCentered<SmallLight<panel::ClayLight> >(
		             panel::mm(panel::SAT_POS.x, panel::SAT_POS.y), module, Deduction::SAT_LIGHT));
	}

	void appendContextMenu(Menu* menu) override {
		Deduction* m = dynamic_cast<Deduction*>(module);
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Deduction"));

		menu->addChild(createBoolMenuItem("2x oversampling", "",
			[=]() { return m->oversampleRequest; },
			[=](bool v) { m->oversampleRequest = v; }));
	}
};


Model* modelDeduction = createModel<Deduction, DeductionWidget>("Deduction");

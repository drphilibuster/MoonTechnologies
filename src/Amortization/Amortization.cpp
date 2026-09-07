// Amortization -- a Verbtronic-style digital reverb, filed as PUB 535.
//
// After the Pittsburgh Modular Verbtronic (Richard Nicol), a 10 HP digital
// reverb "designed for modular synthesis rather than realism", released as
// public domain after it was discontinued. Two algorithms behind one MODE
// switch: VERB, the shorter natural one, and TRONIC, the large synthetic one;
// a TONAL TILT, a FEEDBACK, an OUTPUT MIX with CV, a MODE GATE that flips
// Tronic to Verb, and a wet-only VERB OUT beside the MIX OUT. Everything the
// original had is here with its behaviour; what VCV allows on top -- stereo,
// SIZE, PREDELAY, FREEZE, CV over everything, a read-out -- is added around it.
#include "../plugin.hpp"
#include "Panel.hpp"
#include "Tanks.hpp"

using namespace amortization;


// Feedback ceilings. "Original ranges" keeps the Verb tail finite and the
// Tronic loop just past unity, which is where the hardware lived; extended
// lets Verb hold indefinitely and Tronic run well into its limiter.
static const float kVerbMaxOriginal   = 0.97f;
static const float kVerbMaxExtended   = 1.00f;
static const float kTronicMaxOriginal = 1.10f;
static const float kTronicMaxExtended = 1.35f;

static const float kPredelayMaxMs = 250.f;
static const float kLoopTiltDb = 6.f;     // the tilt inside the tanks, cut only
static const float kOutTiltDb  = 6.f;     // the tilt on the wet output, boost and cut
static const float kLimitKnee  = 1.f;     // Tronic limiter knee, in +-5 V units
// A mode change is not a crossfade on the hardware, so this is not really a
// fade time -- it is how long the two structures are driven into each other on
// the way past. Short enough to be a change rather than a morph, long enough
// for the collision to put something in the tank that then rings on its own.
static const float kXfadeSec   = 0.11f;
//: How hard each tank is driven into the other at the middle of a change,
//: before the feedback setting scales it. Past about 1.2 the pair will not
//: settle at high feedback -- which is arguably correct for this box, but it is
//: the difference between a module that screams when asked and one that cannot
//: be turned down again.
static const float kCollide    = 0.95f;

static const float kRt60Ln = -6.907755f;  // ln(0.001)


struct Amortization : Module {
	enum ParamId {
		FEEDBACK_PARAM, SIZE_PARAM, PREDELAY_PARAM, MOD_PARAM,
		TILT_PARAM, MODE_PARAM, MIX_PARAM, FREEZE_PARAM,
		FB_CV_PARAM, TILT_CV_PARAM, MIX_CV_PARAM, SIZE_CV_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		IN_L_INPUT, IN_R_INPUT,
		FB_INPUT, TILT_INPUT, MIX_INPUT, SIZE_INPUT,
		MODE_INPUT, FREEZE_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		MIX_L_OUTPUT, MIX_R_OUTPUT, VERB_L_OUTPUT, VERB_R_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		LIMIT_LIGHT, TRONIC_LIGHT, FREEZE_LIGHT,
		LIGHTS_LEN
	};

	// --- the signal path -------------------------------------------------
	DelayLine preL, preR;
	Diffuser diffMono;        // feeds VERB, which is a mono-in tank
	Diffuser diffL, diffR;    // feed TRONIC, which takes a stereo pair
	VerbTank verb;
	TronicTank tronic;
	Tilt outTiltL, outTiltR;
	Lfo lfo[4];

	dsp::SchmittTrigger modeGate, freezeGate;
	dsp::ClockDivider controlDivider, lightDivider;

	// --- state ---------------------------------------------------------------
	amortization::ModeCollider collider;
	//: last sample out of each tank, for the cross-feed during a mode change
	float lastVL = 0.f, lastVR = 0.f, lastTL = 0.f, lastTR = 0.f;
	float sizeSmooth = 1.f;
	float preSmooth = 0.f;      // predelay, samples
	float outTiltK = 0.1f;

	// Control-rate values, recomputed every controlDivider tick.
	float cDecay = 0.f, cGain = 0.f;
	float cLoopLo = 1.f, cLoopHi = 1.f;
	float cOutLo = 1.f, cOutHi = 1.f;
	float cSizeTarget = 1.f;
	float cPreTarget = 0.f;
	float cModVerb = 0.f, cModTronic = 0.f;
	bool  cFrozen = false;
	bool  cTronic = false;

	// Options.
	Interp interp = INTERP_LINEAR;
	bool originalRanges = true;

	// Read-outs, published for the display from the audio thread.
	float dispRt60 = 0.f;       // seconds; < 0 means the loop is at or past unity
	float dispPreMs = 0.f;
	bool  dispTronic = false;
	bool  dispFrozen = false;
	bool  dispLimit = false;

	Amortization() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

		configParam(FEEDBACK_PARAM, 0.f, 1.f, 0.5f, "Feedback", "%", 0.f, 100.f);
		configParam(SIZE_PARAM, -1.f, 1.f, 0.f, "Size", "x", 2.f);
		configParam(PREDELAY_PARAM, 0.f, kPredelayMaxMs, 0.f, "Pre-delay", " ms");
		configParam(MOD_PARAM, 0.f, 1.f, 0.35f, "Modulation depth", "%", 0.f, 100.f);

		configParam(TILT_PARAM, -1.f, 1.f, 0.f, "Tonal tilt", " dB", 0.f, kOutTiltDb);
		std::vector<std::string> modeLabels;
		modeLabels.push_back("Verb");
		modeLabels.push_back("Tronic");
		configSwitch(MODE_PARAM, 0.f, 1.f, 0.f, "Mode", modeLabels);
		configParam(MIX_PARAM, 0.f, 1.f, 0.5f, "Output mix", "%", 0.f, 100.f);
		configButton(FREEZE_PARAM, "Freeze");

		configParam(FB_CV_PARAM, -1.f, 1.f, 0.f, "Feedback CV", "%", 0.f, 100.f);
		configParam(TILT_CV_PARAM, -1.f, 1.f, 0.f, "Tonal tilt CV", "%", 0.f, 100.f);
		configParam(MIX_CV_PARAM, -1.f, 1.f, 0.f, "Mix CV", "%", 0.f, 100.f);
		configParam(SIZE_CV_PARAM, -1.f, 1.f, 0.f, "Size CV", "%", 0.f, 100.f);
		getParamQuantity(FB_CV_PARAM)->randomizeEnabled = false;
		getParamQuantity(TILT_CV_PARAM)->randomizeEnabled = false;
		getParamQuantity(MIX_CV_PARAM)->randomizeEnabled = false;
		getParamQuantity(SIZE_CV_PARAM)->randomizeEnabled = false;

		configInput(IN_L_INPUT, "Left audio");
		configInput(IN_R_INPUT, "Right audio (normalled to left)");
		configInput(FB_INPUT, "Feedback CV");
		configInput(TILT_INPUT, "Tonal tilt CV");
		configInput(MIX_INPUT, "Mix CV");
		configInput(SIZE_INPUT, "Size CV");
		configInput(MODE_INPUT, "Mode gate (Tronic to Verb)");
		configInput(FREEZE_INPUT, "Freeze gate");

		configOutput(MIX_L_OUTPUT, "Left mix");
		configOutput(MIX_R_OUTPUT, "Right mix");
		configOutput(VERB_L_OUTPUT, "Left wet");
		configOutput(VERB_R_OUTPUT, "Right wet");

		configBypass(IN_L_INPUT, MIX_L_OUTPUT);
		configBypass(IN_R_INPUT, MIX_R_OUTPUT);

		controlDivider.setDivision(16);
		lightDivider.setDivision(512);
		setSampleRate(APP->engine->getSampleRate());
	}

	/** Allocates every buffer for `sr`. Called from the constructor and from
	    onSampleRateChange, both of which run while the engine is not calling
	    process() on this module. */
	void setSampleRate(float sr) {
		preL.resize((size_t)(kPredelayMaxMs * 0.001f * sr) + 16);
		preR.resize((size_t)(kPredelayMaxMs * 0.001f * sr) + 16);
		diffMono.setSampleRate(sr, 1.f);
		diffL.setSampleRate(sr, 1.f);
		diffR.setSampleRate(sr, 1.13f);
		verb.setSampleRate(sr);
		tronic.setSampleRate(sr);
		outTiltK = onePoleK(1000.f, sr);
		resetState();
	}

	void resetState() {
		outTiltL.reset();
		outTiltR.reset();
		for (int i = 0; i < 4; i++)
			lfo[i].reset();
		preSmooth = 0.f;
		sizeSmooth = 1.f;
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		setSampleRate(e.sampleRate);
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		preL.clear();
		preR.clear();
		diffMono.reset();
		diffL.reset();
		diffR.reset();
		verb.reset();
		tronic.reset();
		resetState();
		collider.reset();
		lastVL = lastVR = lastTL = lastTR = 0.f;
	}

	/** Attenuverted CV: `cv` volts scaled by a +-1 trimmer onto `range`. */
	float cvAmount(InputId in, ParamId trim, float range) {
		if (!inputs[in].isConnected())
			return 0.f;
		return inputs[in].getVoltage() / 10.f * params[trim].getValue() * range;
	}

	void updateControls() {
		// FEEDBACK: the Verb tail's per-half-loop gain, and the Tronic loop gain,
		// which is allowed past unity.
		float fb = clamp(params[FEEDBACK_PARAM].getValue() + cvAmount(FB_INPUT, FB_CV_PARAM, 1.f), 0.f, 1.f);
		float verbMax = originalRanges ? kVerbMaxOriginal : kVerbMaxExtended;
		float tronicMax = originalRanges ? kTronicMaxOriginal : kTronicMaxExtended;
		// Verb runs linear so the knob reads as a tail length; Tronic bows up,
		// reaching unity about 70 % of the way and spending the rest of the
		// travel past it -- the never-ending wash the original was sold on.
		float s = 1.f - fb;
		cDecay = verbMax * fb;
		cGain = tronicMax * (1.f - s * s);

		// TILT: inside the loop it only ever cuts the side it leans away from, so
		// the loop gain never rises above FEEDBACK. On the wet output it is a
		// proper see-saw, boost one side and cut the other.
		float tilt = clamp(params[TILT_PARAM].getValue() + cvAmount(TILT_INPUT, TILT_CV_PARAM, 2.f), -1.f, 1.f);
		float cut = std::pow(10.f, -std::fabs(tilt) * kLoopTiltDb / 20.f);
		cLoopLo = tilt > 0.f ? cut : 1.f;
		cLoopHi = tilt < 0.f ? cut : 1.f;
		cOutLo = std::pow(10.f, -tilt * kOutTiltDb / 20.f);
		cOutHi = 1.f / cOutLo;

		// SIZE is logarithmic, half to double.
		float sz = clamp(params[SIZE_PARAM].getValue() + cvAmount(SIZE_INPUT, SIZE_CV_PARAM, 2.f), -1.f, 1.f);
		cSizeTarget = std::pow(2.f, sz);

		cPreTarget = params[PREDELAY_PARAM].getValue();

		// MOD: excursion of the modulated allpasses, in each tank's reference
		// samples. Dattorro's figure was 16 at 29761 Hz.
		float mod = params[MOD_PARAM].getValue();
		cModVerb = 16.f * mod;
		cModTronic = 24.f * mod;

		// FREEZE: the button latches, the gate holds.
		cFrozen = params[FREEZE_PARAM].getValue() > 0.5f || freezeGate.isHigh();

		// MODE: the switch, unless the gate is holding it down to Verb.
		bool tronicSwitch = params[MODE_PARAM].getValue() > 0.5f;
		cTronic = tronicSwitch && !modeGate.isHigh();
	}

	void process(const ProcessArgs& args) override {
		// Gates first, so a control tick sees this sample's edge.
		modeGate.process(inputs[MODE_INPUT].getVoltage(), 0.1f, 1.f);
		freezeGate.process(inputs[FREEZE_INPUT].getVoltage(), 0.1f, 1.f);

		if (controlDivider.process())
			updateControls();

		// --- input -------------------------------------------------------------
		// getVoltageSum so a polyphonic cable is summed rather than dropped;
		// IN R normals to IN L. Everything past here is in +-1 == +-5 V.
		float dryL = inputs[IN_L_INPUT].getVoltageSum();
		float dryR = inputs[IN_R_INPUT].isConnected() ? inputs[IN_R_INPUT].getVoltageSum() : dryL;
		dryL = clamp(dryL, -20.f, 20.f);
		dryR = clamp(dryR, -20.f, 20.f);
		float inL = dryL * 0.2f;
		float inR = dryR * 0.2f;

		// --- pre-delay, then diffusion -------------------------------------------
		// Both slewed, so a moving knob bends rather than clicks.
		preSmooth += 0.002f * (cPreTarget * 0.001f * args.sampleRate - preSmooth);
		preL.write(inL);
		preR.write(inR);
		float pL = preL.read(preSmooth, interp);
		float pR = preR.read(preSmooth, interp);

		sizeSmooth += 0.001f * (cSizeTarget - sizeSmooth);

		// The tanks hold when frozen: nothing new goes in, nothing leaks out.
		float feed = cFrozen ? 0.f : 1.f;
		float xMono = diffMono.process(0.5f * (pL + pR), interp) * feed;
		float xL = diffL.process(pL, interp) * feed;
		float xR = diffR.process(pR, interp) * feed;

		// --- the two tanks -------------------------------------------------------
		// Both always run, so a mode change is a crossfade between two live
		// tails rather than a cut into a cold one, and the tail you left keeps
		// ringing down for when the gate brings you back.
		collider.step(cTronic, args.sampleTime, kXfadeSec);

		float decay = cFrozen ? 1.f : cDecay;
		float gain = cFrozen ? 1.f : cGain;
		float loopLo = cFrozen ? 1.f : cLoopLo;
		float loopHi = cFrozen ? 1.f : cLoopHi;

		float m0 = lfo[0].step(0.93f, args.sampleTime);
		float m1 = lfo[1].step(1.27f, args.sampleTime);
		float m2 = lfo[2].step(0.71f, args.sampleTime);
		float m3 = lfo[3].step(1.09f, args.sampleTime);

		// The collision. Each tank is fed the whole input at all times -- so the
		// one being arrived at is already ringing rather than starting cold --
		// plus, while a change is in progress, the other tank's last output.
		// Scaled by the feedback setting, because that is the condition the
		// hardware does this under: run the loop up until it is ringing, change
		// mode, and what was in one structure arrives in the other.
		float hot = std::fmax(decay, gain);
		float k = collider.collide() * kCollide * hot;

		float vL, vR;
		verb.process(xMono + k * lastTL, decay, sizeSmooth,
		             cModVerb * m0, cModVerb * m1, loopLo, loopHi, interp, vL, vR);

		float tMod[4] = {cModTronic * m0, cModTronic * m2, cModTronic * m1, cModTronic * m3};
		float tL, tR;
		tronic.process(xL + k * lastVL, xR + k * lastVR, gain, sizeSmooth, tMod,
		               loopLo, loopHi, kLimitKnee, interp, tL, tR);

		lastVL = vL; lastVR = vR; lastTL = tL; lastTR = tR;

		// Equal power, so the middle of a change is not a hole.
		float gv = collider.gVerb(), gt = collider.gTronic();
		float wetL = vL * gv + tL * gt;
		float wetR = vR * gv + tR * gt;

		// --- tonal tilt on the way out --------------------------------------------
		wetL = outTiltL.process(wetL, outTiltK, cOutLo, cOutHi);
		wetR = outTiltR.process(wetR, outTiltK, cOutLo, cOutHi);
		// The wet is quiet against the dry, and the hardware is not: measured
		// through tests/Amortization, the tanks return between 0.23 and 0.50 of
		// what goes in across the FEEDBACK range. At the old gain of 5 that came
		// out at unity overall -- the input is scaled by 0.2 on the way in -- so
		// a fully wet setting was always softer than the signal it replaced.
		// Doubled: the wet now roughly matches the dry at high feedback and is
		// still under it at low, which is the way round it should be. The clamp
		// is the only thing between this and the rails, and it is meant to be.
		wetL = clamp(wetL * 10.f, -12.f, 12.f);
		wetR = clamp(wetR * 10.f, -12.f, 12.f);

		// --- outputs -------------------------------------------------------------
		float mix = clamp(params[MIX_PARAM].getValue() + cvAmount(MIX_INPUT, MIX_CV_PARAM, 1.f), 0.f, 1.f);
		outputs[VERB_L_OUTPUT].setVoltage(wetL);
		outputs[VERB_R_OUTPUT].setVoltage(wetR);
		outputs[MIX_L_OUTPUT].setVoltage(clamp(dryL + (wetL - dryL) * mix, -12.f, 12.f));
		outputs[MIX_R_OUTPUT].setVoltage(clamp(dryR + (wetR - dryR) * mix, -12.f, 12.f));

		// --- lights and read-outs -------------------------------------------------
		if (lightDivider.process()) {
			float dt = args.sampleTime * lightDivider.getDivision();

			// The loop limiter working: the live tank's level against its knee.
			float peak = cTronic ? tronic.peak() / kLimitKnee : verb.peak() / 3.f;
			float lim = clamp((peak - 0.6f) / 0.4f, 0.f, 1.f);
			lights[LIMIT_LIGHT].setBrightnessSmooth(lim, dt);
			lights[TRONIC_LIGHT].setBrightness(cTronic ? 1.f : 0.f);
			lights[FREEZE_LIGHT].setBrightness(cFrozen ? 1.f : 0.f);

			dispRt60 = rt60(cTronic, sizeSmooth);
			dispPreMs = cPreTarget;
			dispTronic = cTronic;
			dispFrozen = cFrozen;
			dispLimit = lim > 0.5f;
		}
	}

	/** Time for the tail to fall 60 dB, from the loop gain per mean loop
	    length. Negative when the loop is at or past unity: it does not fall. */
	float rt60(bool isTronic, float size) const {
		if (cFrozen)
			return -1.f;
		float g = isTronic ? cGain : cDecay;
		if (g >= 0.9999f)
			return -1.f;
		if (g <= 0.f)
			return 0.f;
		float loop = isTronic ? tronic.loopSeconds(size) : verb.loopSeconds(size);
		return loop * kRt60Ln / std::log(g);
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "quality", json_integer(interp == INTERP_CUBIC ? 1 : 0));
		json_object_set_new(root, "originalRanges", json_boolean(originalRanges));
		return root;
	}

	void dataFromJson(json_t* root) override {
		json_t* j;
		j = json_object_get(root, "quality");
		if (j) interp = json_integer_value(j) ? INTERP_CUBIC : INTERP_LINEAR;
		j = json_object_get(root, "originalRanges");
		if (j) originalRanges = json_boolean_value(j);
	}
};


// ---------------------------------------------------------------------------
// Look and feel. The palette, the shared hardware and the entire silkscreen come
// from the generated headers -- see ../../panelkit/README.md.

typedef RoundLargeBlackKnob PrimaryKnob;
typedef RoundBlackKnob      PanelKnob;


// The read-out: which algorithm is live and how long its tail runs, then the
// pre-delay and the loop's condition. Numerals in DSEG7, whose cmap carries
// only [0-9 . : -], so the segment strings below stay inside that; words in
// the mono face.
struct AmortizationDisplay : LedDisplay {
	Amortization* module = NULL;

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) {
			LedDisplay::drawLayer(args, layer);
			return;
		}
		float rt = module ? module->dispRt60 : 2.5f;
		float pre = module ? module->dispPreMs : 0.f;
		bool isTronic = module ? module->dispTronic : false;
		bool frozen = module ? module->dispFrozen : false;
		bool limit = module ? module->dispLimit : false;

		const float pad = 5.f;
		const float rightX = box.size.x - pad;
		const NVGcolor dim = panel::alpha(panel::LIME, 0.55f);

		const panel::TextStyle MODE(panel::Face::Mono, 10.f, panel::MINT,
			NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE, -0.5f);
		const panel::TextStyle TAG(panel::Face::Mono, 8.f, dim,
			NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
		const panel::TextStyle FLAG(panel::Face::Mono, 8.f, dim,
			NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE);
		const panel::TextStyle HOLD(panel::Face::Mono, 11.f, panel::LIME,
			NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);

		// Row 1: the tail, and the algorithm carrying it.
		if (frozen)
			panel::text(args.vg, HOLD, pad, 12.f, "HELD");
		else if (rt < 0.f)
			panel::text(args.vg, HOLD, pad, 12.f, "OVER");
		else {
			std::string num = rt < 10.f ? string::f("%.2f", rt)
			                : rt < 100.f ? string::f("%.1f", rt)
			                : std::string("99.9");
			panel::segValue(args.vg, pad, 12.f, 11.f, num, "s", panel::LIME);
		}
		panel::text(args.vg, MODE, rightX, 12.f, isTronic ? "TRONIC" : "VERB");

		// Row 2: the pre-delay, and whether the limiter is working.
		float x = panel::text(args.vg, TAG, pad, 24.f, "PRE");
		panel::segValue(args.vg, x + 2.5f, 24.f, 8.f, string::f("%.0f", pre), "ms", dim);
		panel::text(args.vg, FLAG.inked(limit ? panel::LIME : dim), rightX, 24.f,
			limit ? "LIMIT" : "CLEAR");
	}
};


struct AmortizationWidget : ModuleWidget {
	AmortizationWidget(Amortization* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Amortization.svg")));

		panel::addScrews(this);
		panel::addLabels(this);

		AmortizationDisplay* display = new AmortizationDisplay;
		display->module = module;
		display->box.pos = panel::mm(panel::GLASS_X, panel::GLASS_Y);
		display->box.size = panel::mm(panel::GLASS_W, panel::GLASS_H);
		addChild(display);

		// TERM
		addParam(createParamCentered<PrimaryKnob>(panel::mm(panel::FEEDBACK_POS.x, panel::FEEDBACK_POS.y), module, Amortization::FEEDBACK_PARAM));
		addParam(createParamCentered<PanelKnob>(panel::mm(panel::SIZE_POS.x, panel::SIZE_POS.y), module, Amortization::SIZE_PARAM));
		addParam(createParamCentered<PanelKnob>(panel::mm(panel::PREDELAY_POS.x, panel::PREDELAY_POS.y), module, Amortization::PREDELAY_PARAM));
		addParam(createParamCentered<Trimpot>(panel::mm(panel::MOD_POS.x, panel::MOD_POS.y), module, Amortization::MOD_PARAM));
		addChild(createLightCentered<SmallLight<panel::LimeLight> >(
		             panel::mm(panel::LIMIT_POS.x, panel::LIMIT_POS.y), module, Amortization::LIMIT_LIGHT));

		// SCHEDULE
		addParam(createParamCentered<PanelKnob>(panel::mm(panel::TILT_POS.x, panel::TILT_POS.y), module, Amortization::TILT_PARAM));
		addParam(createParamCentered<CKSS>(panel::mm(panel::MODE_POS.x, panel::MODE_POS.y), module, Amortization::MODE_PARAM));
		addChild(createLightCentered<SmallLight<panel::MintLight> >(
		             panel::mm(panel::TRONIC_LED_POS.x, panel::TRONIC_LED_POS.y), module, Amortization::TRONIC_LIGHT));
		addParam(createParamCentered<PanelKnob>(panel::mm(panel::MIX_POS.x, panel::MIX_POS.y), module, Amortization::MIX_PARAM));
		addParam(createLightParamCentered<VCVLightBezelLatch<panel::PaperLight> >(
		             panel::mm(panel::FREEZE_POS.x, panel::FREEZE_POS.y), module, Amortization::FREEZE_PARAM, Amortization::FREEZE_LIGHT));

		// PAYMENTS
		addParam(createParamCentered<Trimpot>(panel::mm(panel::FB_CV_POS.x, panel::FB_CV_POS.y), module, Amortization::FB_CV_PARAM));
		addParam(createParamCentered<Trimpot>(panel::mm(panel::TILT_CV_POS.x, panel::TILT_CV_POS.y), module, Amortization::TILT_CV_PARAM));
		addParam(createParamCentered<Trimpot>(panel::mm(panel::MIX_CV_POS.x, panel::MIX_CV_POS.y), module, Amortization::MIX_CV_PARAM));
		addParam(createParamCentered<Trimpot>(panel::mm(panel::SIZE_CV_POS.x, panel::SIZE_CV_POS.y), module, Amortization::SIZE_CV_PARAM));

		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::FB_IN_POS.x, panel::FB_IN_POS.y), module, Amortization::FB_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::TILT_IN_POS.x, panel::TILT_IN_POS.y), module, Amortization::TILT_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::MIX_IN_POS.x, panel::MIX_IN_POS.y), module, Amortization::MIX_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::SIZE_IN_POS.x, panel::SIZE_IN_POS.y), module, Amortization::SIZE_INPUT));

		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::MODE_IN_POS.x, panel::MODE_IN_POS.y), module, Amortization::MODE_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::FREEZE_IN_POS.x, panel::FREEZE_IN_POS.y), module, Amortization::FREEZE_INPUT));

		// Footer
		addInput(createInputCentered<panel::PortInMain>(panel::mm(panel::IN_L_POS.x, panel::IN_L_POS.y), module, Amortization::IN_L_INPUT));
		addInput(createInputCentered<panel::PortInMain>(panel::mm(panel::IN_R_POS.x, panel::IN_R_POS.y), module, Amortization::IN_R_INPUT));
		addOutput(createOutputCentered<panel::PortOutMain>(panel::mm(panel::MIX_L_POS.x, panel::MIX_L_POS.y), module, Amortization::MIX_L_OUTPUT));
		addOutput(createOutputCentered<panel::PortOutMain>(panel::mm(panel::MIX_R_POS.x, panel::MIX_R_POS.y), module, Amortization::MIX_R_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::VERB_L_POS.x, panel::VERB_L_POS.y), module, Amortization::VERB_L_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::VERB_R_POS.x, panel::VERB_R_POS.y), module, Amortization::VERB_R_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		Amortization* m = dynamic_cast<Amortization*>(module);
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Amortization"));

		menu->addChild(createIndexSubmenuItem("Quality",
			{"Standard (linear)", "High (cubic)"},
			[=]() { return m->interp == INTERP_CUBIC ? 1 : 0; },
			[=](int v) { m->interp = v ? INTERP_CUBIC : INTERP_LINEAR; }));

		menu->addChild(createBoolMenuItem("Original ranges", "",
			[=]() { return m->originalRanges; },
			[=](bool v) { m->originalRanges = v; }));
	}
};


Model* modelAmortization = createModel<Amortization, AmortizationWidget>("Amortization");

#include "../plugin.hpp"
#include "Panel.hpp"
#include "Tuning.hpp"

using tuning::Ratio;
using tuning::Scale;
using tuning::Choice;
using tuning::DissonanceCurve;

static const char* kSetNames[tuning::NUM_SETS] = {
	"DIAMOND", "43-TONE", "HEXAD", "HEXANY", "EIKOSANY", "SERIES",
};
static const char* kSetLongNames[tuning::NUM_SETS] = {
	"Diamond -- Partch's 11-limit tonality diamond, 29 pitches",
	"43-tone -- Partch's complete scale",
	"Hexad -- one Otonality or Utonality, 6 pitches",
	"Hexany -- Wilson's 2)4 combination product set on 1-3-5-7",
	"Eikosany -- Wilson's 3)6 set on 1-3-5-7-9-11, 20 pitches",
	"Series -- harmonics 8 to 15, or the same undertones",
};
static const char* kRuleNames[tuning::NUM_RULES] = {
	"NEAREST", "TENNEY", "BARLOW", "EULER", "SETHARES", "ADAPTIVE",
};
static const char* kRuleLongNames[tuning::NUM_RULES] = {
	"Nearest -- plain distance in cents, the control",
	"Tenney -- lowest harmonic distance, log2(n*d)",
	"Barlow -- highest harmonicity, from the indigestibility of the primes",
	"Euler -- lowest gradus suavitatis (1739)",
	"Sethares -- least sensory dissonance against the assumed timbre",
	"Adaptive -- purest interval from the last note, not from the root",
};

//! The four prime limits PRIME steps through.
static const uint32_t kPrimes[4] = {3u, 5u, 7u, 11u};
//! WINDOW's full travel, in cents. Wider than the largest gap in any set here,
//! so at full window every rule can see the whole neighbourhood.
static const float WINDOW_CENTS = 150.f;
//! HYST's full travel, as a fraction of the gap between the two pitches in
//! question. Relative rather than absolute because these sets are wildly
//! uneven -- a hexad steps by two hundred cents and Partch's 43 by fourteen --
//! and a half is the most it can be and still leave every degree reachable.
static const float MAX_HYST = 0.5f;
//! SLEW's full travel, in seconds per octave.
static const float MAX_SLEW_SEC = 1.f;
//! How far ADAPTIVE's tonal centre may wander from the root before it is held.
//! A comma pump has no fixed point -- it is supposed to walk -- but it must not
//! walk out of hearing while your back is turned.
static const float MAX_DRIFT_V = 1.f;


struct Reconciliation : Module {
	enum ParamId {
		SET_PARAM, NEXUS_PARAM, UTONAL_PARAM, PRIME_PARAM,
		RULE_PARAM, BIAS_PARAM, WINDOW_PARAM,
		DEGREE_PARAM, HYST_PARAM, SLEW_PARAM,
		NEXUS_CV_PARAM, WINDOW_CV_PARAM, BIAS_CV_PARAM, DEGREE_CV_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		NEXUS_INPUT, WINDOW_INPUT, BIAS_INPUT, DEGREE_INPUT,
		PITCH_INPUT, ROOT_INPUT, RESET_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		PITCH_OUTPUT, TRIG_OUTPUT, PURITY_OUTPUT, DRIFT_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		TRIG_LIGHT, DRIFT_LIGHT,
		LIGHTS_LEN
	};

	Scale scale;
	DissonanceCurve curve;

	// What the current `scale` was built and ranked for. Rebuilding is cheap but
	// not free -- the eikosany is twenty ratios, each octave-folded and reduced --
	// and none of it can change between samples unless a knob moves.
	int builtSet = -1, builtRule = -1;
	bool builtUtonal = false;
	uint32_t builtPrime = 0u;
	int partials = 7;          //!< the assumed timbre, from the menu
	int curvePartials = -1;
	float curveHz = 0.f;

	// Per channel: the last input we quantized, and what it came out as. A
	// quantizer that re-searches an unchanged input every sample is doing the
	// same work at 48 kHz that it needed to do once.
	float lastIn[16] = {};
	float lastOut[16] = {};       //!< the quantized pitch, before slew
	float slewed[16] = {};        //!< what actually leaves the jack
	float lastPickCents[16] = {}; //!< the chosen degree's cents, for hysteresis
	int lastIndex[16] = {};
	int lastOctave[16] = {};
	bool primed[16] = {};
	dsp::PulseGenerator trigPulse[16];

	float centre = 0.f;        //!< ADAPTIVE's drifting tonal centre, in volts
	dsp::SchmittTrigger resetTrigger;
	dsp::ClockDivider curveDivider;
	dsp::ClockDivider lightDivider;

	// For the read-out. Written on the light divider, read by the widget.
	Ratio dispRatio;
	float dispCents = 0.f;
	float dispDetune = 0.f;    //!< cents away from the nearest 12-TET pitch
	int dispSet = 0, dispRule = 0, dispNexus = 0, dispCount = 0;
	bool dispUtonal = false;

	Reconciliation() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

		std::vector<std::string> setLabels;
		for (int i = 0; i < tuning::NUM_SETS; i++)
			setLabels.push_back(kSetLongNames[i]);
		configSwitch(SET_PARAM, 0.f, (float) (tuning::NUM_SETS - 1), 0.f, "Set", setLabels);
		getParamQuantity(SET_PARAM)->snapEnabled = true;

		std::vector<std::string> nexusLabels;
		for (int i = 0; i < tuning::NUM_IDENTITIES; i++)
			nexusLabels.push_back(string::f("Identity %u", tuning::IDENTITIES[i]));
		configSwitch(NEXUS_PARAM, 0.f, (float) (tuning::NUM_IDENTITIES - 1), 0.f,
		             "Nexus", nexusLabels);
		getParamQuantity(NEXUS_PARAM)->snapEnabled = true;

		configSwitch(UTONAL_PARAM, 0.f, 1.f, 0.f, "Polarity",
		             {"Otonal (over a fundamental)", "Utonal (under a guide tone)"});

		std::vector<std::string> primeLabels;
		for (int i = 0; i < 4; i++)
			primeLabels.push_back(string::f("%u limit", kPrimes[i]));
		configSwitch(PRIME_PARAM, 0.f, 3.f, 3.f, "Prime limit", primeLabels);
		getParamQuantity(PRIME_PARAM)->snapEnabled = true;

		std::vector<std::string> ruleLabels;
		for (int i = 0; i < tuning::NUM_RULES; i++)
			ruleLabels.push_back(kRuleLongNames[i]);
		configSwitch(RULE_PARAM, 0.f, (float) (tuning::NUM_RULES - 1), 0.f, "Rule", ruleLabels);
		getParamQuantity(RULE_PARAM)->snapEnabled = true;

		configParam(BIAS_PARAM, 0.f, 1.f, 0.f,
		            "Bias (how hard the rule pulls against plain distance)", "%", 0.f, 100.f);
		configParam(WINDOW_PARAM, 0.f, 1.f, 0.4f,
		            "Window (how far the rule may reach)", " cents", 0.f, WINDOW_CENTS);

		// Degrees of the set, not semitones: at DIAMOND one step is whatever
		// the next ratio happens to be, which is the point of transposing
		// inside a structure rather than across it.
		configParam(DEGREE_PARAM, -12.f, 12.f, 0.f, "Degree (transpose within the set)",
		            " steps");
		getParamQuantity(DEGREE_PARAM)->snapEnabled = true;
		configParam(HYST_PARAM, 0.f, 1.f, 0.3f,
		            "Hysteresis (dead band, as a fraction of the step)",
		            "%", 0.f, 100.f * MAX_HYST);
		configParam(SLEW_PARAM, 0.f, 1.f, 0.f, "Slew", " s/oct", 0.f, MAX_SLEW_SEC);

		configParam(NEXUS_CV_PARAM, -1.f, 1.f, 0.f, "Nexus CV depth", "%", 0.f, 100.f);
		configParam(WINDOW_CV_PARAM, -1.f, 1.f, 0.f, "Window CV depth", "%", 0.f, 100.f);
		configParam(BIAS_CV_PARAM, -1.f, 1.f, 0.f, "Bias CV depth", "%", 0.f, 100.f);
		configParam(DEGREE_CV_PARAM, -1.f, 1.f, 0.f, "Degree CV depth", "%", 0.f, 100.f);
		for (int p = NEXUS_CV_PARAM; p <= DEGREE_CV_PARAM; p++)
			getParamQuantity(p)->randomizeEnabled = false;

		configInput(NEXUS_INPUT, "Nexus CV");
		configInput(WINDOW_INPUT, "Window CV");
		configInput(BIAS_INPUT, "Bias CV");
		configInput(DEGREE_INPUT, "Degree CV (1 V = 5 degrees)");
		configInput(PITCH_INPUT, "1 V/oct (polyphonic)");
		configInput(ROOT_INPUT, "Root -- the 1/1 every ratio is measured from");
		configInput(RESET_INPUT, "Reset (returns Adaptive's tonal centre to the root)");

		configOutput(PITCH_OUTPUT, "Quantized 1 V/oct");
		configOutput(TRIG_OUTPUT, "Trigger on every change of pitch");
		configOutput(PURITY_OUTPUT, "Purity (0-10 V: how consonant the chosen ratio is)");
		configOutput(DRIFT_OUTPUT, "Drift (Adaptive's tonal centre, relative to the root)");

		configBypass(PITCH_INPUT, PITCH_OUTPUT);

		curveDivider.setDivision(16384);
		lightDivider.setDivision(256);
		rebuild(true);
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		centre = 0.f;
		for (int c = 0; c < 16; c++)
			primed[c] = false;
		rebuild(true);
	}

	/** Rebuild the pitch set and its ranking if anything they depend on moved.
	    Returns true if it did, which is what tells the per-channel cache that
	    its answers are stale. */
	bool rebuild(bool force) {
		const int set = (int) std::round(params[SET_PARAM].getValue());
		const int rule = (int) std::round(params[RULE_PARAM].getValue());
		const bool utonal = params[UTONAL_PARAM].getValue() > 0.5f;
		const uint32_t prime = kPrimes[clamp((int) std::round(params[PRIME_PARAM].getValue()), 0, 3)];

		const bool setChanged = force || set != builtSet || utonal != builtUtonal
		                        || prime != builtPrime;
		const bool rankChanged = setChanged || rule != builtRule;
		if (!rankChanged)
			return false;

		if (setChanged)
			scale.build(set, utonal, prime);
		scale.rankBy(rule, curve);
		builtSet = set;
		builtRule = rule;
		builtUtonal = utonal;
		builtPrime = prime;
		return true;
	}

	/** The dissonance curve depends on the root's frequency, because the
	    critical band does. Rebuilding it costs partials^2 exponentials a cent,
	    so it happens only for the one rule that reads it, only a few times a
	    second, and only when the root has actually moved a useful distance --
	    the curve's shape barely changes over a semitone. */
	void maybeRebuildCurve(float rootV) {
		const int rule = (int) std::round(params[RULE_PARAM].getValue());
		if (rule != tuning::RULE_SETHARES)
			return;
		if (!curveDivider.process() && curvePartials == partials && curveHz > 0.f)
			return;
		const float hz = dsp::FREQ_C4 * std::pow(2.f, clamp(rootV, -5.f, 5.f));
		const bool moved = (curveHz <= 0.f) || (std::fabs(std::log2(hz / curveHz)) > 0.25f);
		if (!moved && curvePartials == partials)
			return;
		curve.build(hz, partials);
		curveHz = hz;
		curvePartials = partials;
		scale.rankBy(rule, curve);
	}

	float modulated(int knobParam, int cvParam, int input, float lo, float hi) {
		float v = params[knobParam].getValue();
		if (inputs[input].isConnected())
			v += inputs[input].getVoltage() * 0.1f * params[cvParam].getValue();
		return clamp(v, lo, hi);
	}

	void process(const ProcessArgs& args) override {
		const int channels = std::max(1, inputs[PITCH_INPUT].getChannels());
		const float rootV = inputs[ROOT_INPUT].getVoltage();

		bool stale = rebuild(false);
		maybeRebuildCurve(rootV);

		const int rule = (int) std::round(params[RULE_PARAM].getValue());
		const float window = modulated(WINDOW_PARAM, WINDOW_CV_PARAM, WINDOW_INPUT, 0.f, 1.f)
		                     * WINDOW_CENTS;
		// NEAREST means nearest. Its ranking is still computed -- PURITY reads
		// it -- but it never gets to move the answer.
		const float bias = (rule == tuning::RULE_NEAREST)
			? 0.f
			: modulated(BIAS_PARAM, BIAS_CV_PARAM, BIAS_INPUT, 0.f, 1.f);

		// NEXUS transposes the structure onto one of Partch's identities. An
		// Otonality on 7 is the Otonality on 1 with its root moved down by 7/4;
		// a Utonality on 7 is the same move upward. Doing it as a shift of the
		// root rather than by rebuilding the set is not a shortcut -- it is what
		// the numerary nexus *is* -- and it keeps the ratios on the read-out
		// small enough to be worth reading.
		int nexusIdx = (int) std::round(params[NEXUS_PARAM].getValue());
		if (inputs[NEXUS_INPUT].isConnected()) {
			nexusIdx += (int) std::round(inputs[NEXUS_INPUT].getVoltage()
			                             * params[NEXUS_CV_PARAM].getValue()
			                             * 0.1f * (tuning::NUM_IDENTITIES - 1));
		}
		nexusIdx = clamp(nexusIdx, 0, tuning::NUM_IDENTITIES - 1);
		const bool utonal = params[UTONAL_PARAM].getValue() > 0.5f;
		const Ratio nexusRatio = tuning::octaveReduce(
			Ratio(tuning::IDENTITIES[nexusIdx], 1u));
		const float nexusShift = (utonal ? 1.f : -1.f) * tuning::cents(nexusRatio);

		if (resetTrigger.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 2.f))
			centre = rootV;

		// The reference every ratio is measured from. Four of the five rules use
		// the root; ADAPTIVE uses its own drifting centre, and the difference
		// between those two cases is the whole of what ADAPTIVE is.
		const bool adaptive = (rule == tuning::RULE_ADAPTIVE);
		if (!adaptive)
			centre = rootV;
		centre = clamp(centre, rootV - MAX_DRIFT_V, rootV + MAX_DRIFT_V);

		outputs[PITCH_OUTPUT].setChannels(channels);
		outputs[TRIG_OUTPUT].setChannels(channels);
		outputs[PURITY_OUTPUT].setChannels(channels);

		// Anything that moves the answer for every channel at once invalidates
		// the whole cache, not just the channels whose input happened to change.
		//
		// The reference is deliberately *not* on this list in ADAPTIVE mode.
		// There, the centre moving is the module's own doing -- it steps to
		// wherever the last note landed -- and re-quantizing the current note
		// against it would find that note already sitting exactly on the new
		// reference and collapse every interval to 1/1. Which is what happened
		// the first time this was written.
		if (window != cachedWindow || bias != cachedBias || nexusShift != cachedShift
		    || (!adaptive && centre != cachedCentre)) {
			cachedWindow = window;
			cachedBias = bias;
			cachedShift = nexusShift;
			cachedCentre = centre;
			stale = true;
		}

		// DEGREE walks along the set rather than across the keyboard: one step is
		// whatever the next ratio in this structure happens to be, which at
		// DIAMOND is anything from 14 cents to a whole tone.
		int degree = (int) std::round(params[DEGREE_PARAM].getValue());
		if (inputs[DEGREE_INPUT].isConnected())
			degree += (int) std::round(inputs[DEGREE_INPUT].getVoltage()
			                           * params[DEGREE_CV_PARAM].getValue() * 5.f);
		if (degree != cachedDegree) {
			cachedDegree = degree;
			stale = true;
		}
		const float hyst = params[HYST_PARAM].getValue() * MAX_HYST;
		const float slewSec = params[SLEW_PARAM].getValue() * MAX_SLEW_SEC;

		bool anyTrig = false;
		for (int ch = 0; ch < channels; ch++) {
			const float in = inputs[PITCH_INPUT].getVoltage(ch);
			if (stale || !primed[ch] || std::fabs(in - lastIn[ch]) > 1e-6f) {
				const float x = (in - centre) * 1200.f - nexusShift;
				Choice pick = tuning::quantise(scale, x, window, bias);

				// Hysteresis. The note only changes once the input is closer to
				// the new degree than to the one it is already on by more than
				// HYST -- so a ramp crossing a boundary crosses it once, rather
				// than sitting on it and dithering. Applied before DEGREE,
				// because it is about the input's own neighbourhood.
				Choice prev;
				prev.index = lastIndex[ch];
				prev.octave = lastOctave[ch];
				prev.cents = lastPickCents[ch];
				pick = tuning::withHysteresis(pick, prev, primed[ch], x, hyst);

				const int picked = pick.index;
				const int pickedOct = pick.octave;
				const float pickedCents = pick.cents;

				// DEGREE, applied to whatever was chosen.
				const float outCents = tuning::transposeDegrees(scale, pick, degree).cents;
				const float out = centre + (outCents + nexusShift) / 1200.f;

				const bool changed = !primed[ch] || picked != lastIndex[ch]
				                     || pickedOct != lastOctave[ch];
				if (changed) {
					trigPulse[ch].trigger(1e-3f);
					anyTrig = true;
				}
				lastIn[ch] = in;
				lastOut[ch] = out;
				lastPickCents[ch] = pickedCents;
				lastIndex[ch] = picked;
				lastOctave[ch] = pickedOct;
				if (!primed[ch])
					slewed[ch] = out;
				primed[ch] = true;

				// ADAPTIVE measures the next interval from where this one landed,
				// not from the root: that is what keeps every interval pure, and
				// it is also why the tonal centre walks. Play I-IV-V-I this way
				// and you come home a syntonic comma flat -- not a bug in the
				// module but the oldest known problem with just intonation, and
				// DRIFT is it as a voltage.
				//
				// Stepping only when the chosen *degree* changes, rather than on
				// any movement of the input, is what keeps a portamento from
				// dragging the centre with it one sample at a time.
				if (adaptive && ch == 0 && changed) {
					// The pre-DEGREE pitch, deliberately: the next interval is
					// measured from the note the rule actually chose, so that
					// turning DEGREE transposes the line without also changing
					// where the tonal centre walks to.
					const float anchor = centre + (pickedCents + nexusShift) / 1200.f;
					centre = clamp(anchor, rootV - MAX_DRIFT_V, rootV + MAX_DRIFT_V);
					// Channels above this one are tuned against the note the
					// bass just landed on, so a chord comes out pure within
					// itself rather than each voice pure against the root.
				}
			}
			// SLEW, in seconds per octave, so a wide leap takes proportionally
			// longer than a narrow one -- which is what a portamento does and
			// what a fixed time constant does not.
			if (slewSec > 1e-4f) {
				const float step = args.sampleTime / slewSec;
				const float d = lastOut[ch] - slewed[ch];
				slewed[ch] += clamp(d, -step, step);
			}
			else {
				slewed[ch] = lastOut[ch];
			}

			outputs[PITCH_OUTPUT].setVoltage(slewed[ch], ch);
			outputs[TRIG_OUTPUT].setVoltage(trigPulse[ch].process(args.sampleTime) ? 10.f : 0.f, ch);
			const int idx = clamp(lastIndex[ch], 0, std::max(0, scale.n - 1));
			outputs[PURITY_OUTPUT].setVoltage(scale.quality[idx] * 10.f, ch);
		}

		outputs[DRIFT_OUTPUT].setVoltage(adaptive ? clamp(centre - rootV, -MAX_DRIFT_V, MAX_DRIFT_V) : 0.f);

		if (lightDivider.process()) {
			const float dt = args.sampleTime * lightDivider.getDivision();
			lights[TRIG_LIGHT].setBrightnessSmooth(anyTrig ? 1.f : 0.f, dt);
			lights[DRIFT_LIGHT].setBrightnessSmooth(
				adaptive ? std::fabs(centre - rootV) / MAX_DRIFT_V : 0.f, dt);

			// The read-out. The ratio shown is the one against the *root*, not
			// against the transposed structure, because that is the interval you
			// are actually hearing.
			const int idx = clamp(lastIndex[0], 0, std::max(0, scale.n - 1));
			const Ratio degree = (scale.n > 0) ? scale.r[idx] : Ratio(1u, 1u);
			dispRatio = utonal ? tuning::octaveReduce(tuning::mul(degree, nexusRatio))
			                   : tuning::octaveReduce(tuning::div(degree, nexusRatio));
			dispCents = tuning::cents(dispRatio);
			dispDetune = dispCents - 100.f * std::round(dispCents / 100.f);
			dispSet = builtSet;
			dispRule = builtRule;
			dispNexus = nexusIdx;
			dispUtonal = utonal;
			dispCount = scale.n;
		}
	}

	float cachedWindow = -1.f, cachedBias = -1.f, cachedShift = 1e9f, cachedCentre = 1e9f;
	int cachedDegree = 0;

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "partials", json_integer(partials));
		return root;
	}

	void dataFromJson(json_t* root) override {
		if (json_t* p = json_object_get(root, "partials"))
			partials = clamp((int) json_integer_value(p), 1, DissonanceCurve::MAX_PARTIALS);
	}
};


// ---------------------------------------------------------------------------
// Look and feel. The palette, the shared hardware and the entire silkscreen come
// from src/PanelTheme.hpp, generated by tools/panels/Reconciliation.py -- see
// ../../panelkit/README.md. Nothing about the look is written here.

typedef RoundLargeBlackKnob BigKnob;
typedef RoundBlackKnob      PanelKnob;


/** The read-out. A just-intonation quantizer whose display shows cents is
    telling you the least interesting true thing about itself: the ratio is the
    fact, and "11/8" is a different statement from "551 cents".

    Five lines on the grid the spec cut the glass into (src/Reconciliation/
    Panel.hpp). The middle three are FIELD_* cells, and the module places a
    field over each, so a setting is grabbed exactly where it is printed. The
    top line (the ratio) and the bottom one (the pitch count and the two lamps)
    are only read; they are the grid's first and last rows, found from the
    fields' own pitch so they cannot drift from them. */
struct ReconciliationDisplay : LedDisplay {
	Reconciliation* module = NULL;

	/** A small caption at the left of a cell and its value at the right. */
	static void pair(NVGcontext* vg, const Rect& c, const char* tag,
	                 const std::string& value) {
		const float base = c.pos.y + c.size.y * 0.78f;
		const panel::TextStyle TAG(panel::Face::Mono, 7.f, panel::SAGE,
			NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
		const panel::TextStyle VAL(panel::Face::Mono, 8.5f, panel::LIME,
			NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE);
		panel::text(vg, TAG, c.pos.x + 2.f, base, tag);
		panel::text(vg, VAL, c.pos.x + c.size.x - 2.f, base, value);
	}

	/** A lamp drawn on the glass: a dot that lights, and its name before it. */
	static float lamp(NVGcontext* vg, float right, float base, float mid,
	                  const char* name, float bright) {
		const float r = 1.9f;
		nvgBeginPath(vg);
		nvgCircle(vg, right - r, mid, r);
		nvgFillColor(vg, panel::alpha(panel::LIME, 0.15f + 0.85f * clamp(bright, 0.f, 1.f)));
		nvgFill(vg);
		const panel::TextStyle TAG(panel::Face::Mono, 7.f, panel::SAGE,
			NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE);
		panel::text(vg, TAG, right - 2.f * r - 2.f, base, name);
		return right - 2.f * r - 2.f - panel::textWidth(vg, TAG, name);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) {
			LedDisplay::drawLayer(args, layer);
			return;
		}
		NVGcontext* vg = args.vg;
		const Ratio r = module ? module->dispRatio : Ratio(1u, 1u);
		const float c = module ? module->dispCents : 0.f;
		const float dev = module ? module->dispDetune : 0.f;
		const int set = clamp(module ? module->dispSet : 0, 0, tuning::NUM_SETS - 1);
		const int rule = clamp(module ? module->dispRule : 0, 0, tuning::NUM_RULES - 1);
		const int nexus = clamp(module ? module->dispNexus : 0, 0, tuning::NUM_IDENTITIES - 1);
		const bool utonal = module ? module->dispUtonal : false;
		const int count = module ? module->dispCount : 29;
		auto param = [&](int id, float dflt) {
			return module ? module->params[id].getValue() : dflt;
		};
		const int prime = clamp((int) std::round(param(Reconciliation::PRIME_PARAM, 3.f)), 0, 3);
		const int degree = (int) std::round(param(Reconciliation::DEGREE_PARAM, 0.f));
		const float hyst = param(Reconciliation::HYST_PARAM, 0.3f) * MAX_HYST;
		const float slew = param(Reconciliation::SLEW_PARAM, 0.f) * MAX_SLEW_SEC;

		// The grid's rows: the fields give rows 1 to 3, and their pitch gives
		// the read-only lines above and below.
		const Rect set1 = panel::inGlass(panel::FIELD_SET);
		const Rect rule1 = panel::inGlass(panel::FIELD_RULE);
		const float pitch = panel::inGlass(panel::FIELD_PRIME).pos.y - set1.pos.y;
		const float left = set1.pos.x + 2.f;
		const float right = rule1.pos.x + rule1.size.x - 2.f;
		const float h = set1.size.y;
		const float top = set1.pos.y - pitch;
		const float foot = panel::inGlass(panel::FIELD_HYST).pos.y + pitch;

		// Top line: the ratio, its size in cents, and how far that is from the
		// twelve-tone pitch it is nearest -- the number that says what this
		// module is for.
		const panel::TextStyle RATIO(panel::Face::Mono, 11.f, panel::LIME,
			NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
		const panel::TextStyle NOTE(panel::Face::Mono, 8.f, panel::MINT,
			NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE);
		const panel::TextStyle TAG(panel::Face::Mono, 7.f, panel::SAGE,
			NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE);
		const float b0 = top + h * 0.85f;
		float x = panel::text(vg, RATIO, left, b0, string::f("%u/%u", r.n, r.d));
		panel::segValue(vg, x + 4.f, b0, 9.f, string::f("%.1f", c), "c", panel::LIME);
		const float tagX = right - panel::textWidth(vg, NOTE, string::f("%+.1f", dev)) - 2.f;
		panel::text(vg, NOTE, right, b0, string::f("%+.1f", dev));
		panel::text(vg, TAG, tagX, b0, "ET");

		// Line 2: BASIS and the rule -- which structure, its polarity, which
		// identity it stands on, and which rule is choosing.
		const panel::TextStyle WORD(panel::Face::Mono, 8.5f, panel::LIME,
			NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
		const float b1 = set1.pos.y + h * 0.78f;
		panel::text(vg, WORD, set1.pos.x + 2.f, b1, kSetNames[set]);
		const Rect u = panel::inGlass(panel::FIELD_UTONAL);
		panel::text(vg, WORD.inked(panel::MINT).aligned(NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE),
		            u.pos.x + u.size.x * 0.5f, b1, utonal ? "U" : "O");
		const Rect n = panel::inGlass(panel::FIELD_NEXUS);
		panel::text(vg, WORD.inked(panel::MINT).aligned(NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE),
		            n.pos.x + n.size.x * 0.5f, b1,
		            string::f("%u", tuning::IDENTITIES[nexus]));
		panel::text(vg, WORD.aligned(NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE),
		            rule1.pos.x + rule1.size.x - 2.f, b1, kRuleNames[rule]);

		// Lines 3 and 4: what prunes and tunes it.
		pair(vg, panel::inGlass(panel::FIELD_PRIME), "LIMIT", string::f("%u", kPrimes[prime]));
		pair(vg, panel::inGlass(panel::FIELD_DEGREE), "DEGREE", string::f("%+d", degree));
		pair(vg, panel::inGlass(panel::FIELD_HYST), "HYST", string::f("%.0f%%", hyst * 100.f));
		pair(vg, panel::inGlass(panel::FIELD_SLEW), "SLEW",
		     slew <= 0.f ? std::string("OFF") : string::f("%.2fs", slew));

		// Bottom line: how many pitches survived the prime filter, and the
		// lamps that used to sit by the TRIG and DRIFT jacks -- the note
		// changing, and the tonal centre wandering off the root.
		const float b4 = foot + h * 0.78f;
		const panel::TextStyle COUNT(panel::Face::Mono, 7.f, panel::SAGE,
			NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
		panel::text(vg, COUNT, left, b4, string::f("%d PITCHES", count));
		const float trig = module ? module->lights[Reconciliation::TRIG_LIGHT].getBrightness() : 0.f;
		const float drift = module ? module->lights[Reconciliation::DRIFT_LIGHT].getBrightness() : 0.f;
		x = lamp(vg, right, b4, foot + h * 0.5f, "DRIFT", drift);
		lamp(vg, x - 5.f, b4, foot + h * 0.5f, "TRIG", trig);
	}
};


struct ReconciliationWidget : ModuleWidget {
	ReconciliationWidget(Reconciliation* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Reconciliation.svg")));

		panel::addScrews(this);
		panel::addLabels(this);

		ReconciliationDisplay* display = new ReconciliationDisplay;
		display->module = module;
		display->box.pos = panel::mm(panel::GLASS_X, panel::GLASS_Y);
		display->box.size = panel::mm(panel::GLASS_W, panel::GLASS_H);
		addChild(display);

		// The read-out's fields, over the cells it draws them in: BASIS and
		// the rule on the second line, the settings that tune it below.
		addParam(panel::createField<panel::ScreenSelect>(panel::FIELD_SET, module, Reconciliation::SET_PARAM));
		addParam(panel::createField<panel::ScreenSwitch>(panel::FIELD_UTONAL, module, Reconciliation::UTONAL_PARAM));
		addParam(panel::createField<panel::ScreenSelect>(panel::FIELD_NEXUS, module, Reconciliation::NEXUS_PARAM));
		addParam(panel::createField<panel::ScreenSelect>(panel::FIELD_RULE, module, Reconciliation::RULE_PARAM));
		addParam(panel::createField<panel::ScreenSelect>(panel::FIELD_PRIME, module, Reconciliation::PRIME_PARAM));
		addParam(panel::createField<panel::ScreenKnob>(panel::FIELD_DEGREE, module, Reconciliation::DEGREE_PARAM));
		addParam(panel::createField<panel::ScreenKnob>(panel::FIELD_HYST, module, Reconciliation::HYST_PARAM));
		addParam(panel::createField<panel::ScreenKnob>(panel::FIELD_SLEW, module, Reconciliation::SLEW_PARAM));

		// RECONCILE: the two you play.
		addParam(createParamCentered<BigKnob>(
		             panel::mm(panel::BIAS_POS.x, panel::BIAS_POS.y), module, Reconciliation::BIAS_PARAM));
		addParam(createParamCentered<PanelKnob>(
		             panel::mm(panel::WINDOW_POS.x, panel::WINDOW_POS.y), module, Reconciliation::WINDOW_PARAM));

		// ALLOWANCES: a trimpot directly over its jack.
#define RECONCILIATION_CV(TRIM, JACK, PARAM, INPUT) \
		addParam(createParamCentered<Trimpot>( \
		             panel::mm(panel::TRIM.x, panel::TRIM.y), module, Reconciliation::PARAM)); \
		addInput(createInputCentered<panel::PortIn>( \
		             panel::mm(panel::JACK.x, panel::JACK.y), module, Reconciliation::INPUT));

		RECONCILIATION_CV(NEXUS_CV_POS,  NEXUS_IN_POS,  NEXUS_CV_PARAM,  NEXUS_INPUT)
		RECONCILIATION_CV(WINDOW_CV_POS, WINDOW_IN_POS, WINDOW_CV_PARAM, WINDOW_INPUT)
		RECONCILIATION_CV(BIAS_CV_POS,   BIAS_IN_POS,   BIAS_CV_PARAM,   BIAS_INPUT)
		RECONCILIATION_CV(DEGREE_CV_POS, DEGREE_IN_POS, DEGREE_CV_PARAM, DEGREE_INPUT)
#undef RECONCILIATION_CV

		// The footer band: the inputs.
		addInput(createInputCentered<panel::PortInMain>(
		             panel::mm(panel::PITCH_IN_POS.x, panel::PITCH_IN_POS.y), module, Reconciliation::PITCH_INPUT));
		addInput(createInputCentered<panel::PortIn>(
		             panel::mm(panel::ROOT_IN_POS.x, panel::ROOT_IN_POS.y), module, Reconciliation::ROOT_INPUT));
		addInput(createInputCentered<panel::PortIn>(
		             panel::mm(panel::RESET_IN_POS.x, panel::RESET_IN_POS.y), module, Reconciliation::RESET_INPUT));

		// The rail: the outputs. Their lamps are drawn on the read-out.
		addOutput(createOutputCentered<panel::PortOutMain>(
		             panel::mm(panel::PITCH_OUT_POS.x, panel::PITCH_OUT_POS.y), module, Reconciliation::PITCH_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(
		             panel::mm(panel::TRIG_OUT_POS.x, panel::TRIG_OUT_POS.y), module, Reconciliation::TRIG_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(
		             panel::mm(panel::PURITY_OUT_POS.x, panel::PURITY_OUT_POS.y), module, Reconciliation::PURITY_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(
		             panel::mm(panel::DRIFT_OUT_POS.x, panel::DRIFT_OUT_POS.y), module, Reconciliation::DRIFT_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		Reconciliation* m = dynamic_cast<Reconciliation*>(module);
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Reconciliation"));

		// The one setting that only matters to SETHARES, and the one that makes
		// his argument audible: which intervals are consonant is a fact about
		// the spectrum, so changing the assumed timbre changes the answer.
		menu->addChild(createIndexSubmenuItem("Assumed timbre (Sethares)",
			{"4 partials (dull)", "7 partials (harmonic)", "12 partials (bright)"},
			[=]() { return m->partials <= 4 ? 0 : (m->partials <= 7 ? 1 : 2); },
			[=](int i) {
				m->partials = (i == 0) ? 4 : ((i == 1) ? 7 : 12);
				m->curvePartials = -1;      // force the curve to be rebuilt
				m->curveHz = 0.f;
			}));
	}
};


Model* modelReconciliation = createModel<Reconciliation, ReconciliationWidget>("Reconciliation");

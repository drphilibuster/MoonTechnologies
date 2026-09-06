#include <cstring>

#include "../plugin.hpp"
#include "Panel.hpp"
#include "Mapping.hpp"

using gross::Shape;

// ---------------------------------------------------------------------------
// Gross: Schedule C. A Wiener-Hammerstein distortion -- input EQ, pre-gain,
// operating-point shift, a parametric mapping, dry/wet, post gain, output EQ --
// after Eichas & Zölzer's extended Wiener model (DAFx-16) and the gray-box
// chain of Comunità, Steinmetz & Reiss (2025). The nonlinear block runs
// oversampled; everything linear runs at the engine rate.
//
// Units inside the block: the audio is divided by 5 V so the mapping's knees
// and the OFFSET / DYN shifts are in the same units the papers plot -- a knee
// of 1.0 sits where a 5 V input lands with DRIVE at 0 dB.

static const char* kCurveShort[] = { "TANH", "RATIO", "HARD", "DIODE", "CUBIC" };

static const float kLowShelfHz  = 100.f;
static const float kHighShelfHz = 3000.f;
static const float kTiltHz      = 1000.f;
static const float kDcBlockHz   = 5.f;
static const float kMidQ        = 0.8f;

static inline float dbToLin(float db) { return std::pow(10.f, db / 20.f); }

/** A preset is every knob on the panel. The three circuit presets are read off
    Fig. 7 of Eichas & Zölzer: the paper reports the model but not the fitted
    parameter vectors, so these reproduce the plotted curves rather than quote
    numbers. */
struct Preset {
	const char* name;
	int curve;
	float driveDb, offset, dyn, attackMs, releaseMs;
	float kp, kn, gp, gn;
	float wet, postDb;
	float low, mid, midfHz, high, tone, locutHz;
};

static const Preset kPresets[] = {
	// Fig. 7a: symmetric, vertical at the origin, dead flat past the knee,
	// no dry. The RC low-pass before the diodes (7.2 kHz) is the HIGH shelf.
	{ "Diode Clipper", gross::CURVE_TANH, 30.f, 0.f, 0.f, 32.f, 32.f,
	  3.f, 3.f, 1.f, 1.f, 1.f, -5.f, 0.f, 0.f, 800.f, -6.f, 0.f, 20.f },
	// Fig. 7b: steep centre to about +/-0.3, then the slope of the dry path out
	// to +/-0.5 at 1 V. The knee is 0.375 in wet units (0.3 / 0.8).
	{ "Big Muff", gross::CURVE_TANH, 30.f, 0.f, 0.f, 32.f, 32.f,
	  0.39f, 0.39f, 8.f, 8.f, 0.8f, 0.f, -6.f, 0.f, 800.f, 0.f, 0.f, 80.f },
	// Fig. 7c: a lot of dry, and a negative knee that turns over more gently
	// than the positive one. The mid hump is the pedal's 720 Hz feedback corner.
	{ "Tube Screamer", gross::CURVE_TANH, 32.f, 0.f, 0.f, 32.f, 32.f,
	  1.1f, 0.9f, 6.f, 1.5f, 0.25f, 3.f, -6.f, 4.f, 720.f, 0.f, 0.f, 40.f },
	// Comunità et al. Fig. 2 (3a, 4a): the operating point rides the envelope,
	// fast up and slow down, and the curve is symmetric and hard.
	{ "Fuzz", gross::CURVE_TANH, 34.f, 0.f, 0.7f, 2.f, 300.f,
	  0.5f, 0.5f, 3.f, 3.f, 1.f, -3.f, 0.f, 0.f, 800.f, -4.f, -0.3f, 40.f },
	// Unity: a plain tanh reached only by a 5 V signal, everything else off.
	{ "Flat", gross::CURVE_TANH, 0.f, 0.f, 0.f, 32.f, 32.f,
	  3.f, 3.f, 1.f, 1.f, 1.f, 0.f, 0.f, 0.f, 800.f, 0.f, 0.f, 10.f },
};
static const int kNumPresets = sizeof(kPresets) / sizeof(kPresets[0]);


struct Gross : Module {
	enum ParamId {
		LOW_PARAM, MID_PARAM, MIDF_PARAM, HIGH_PARAM, DRIVE_PARAM,
		OFFSET_PARAM, DYN_PARAM, ATTACK_PARAM, RELEASE_PARAM, CURVE_PARAM,
		KP_PARAM, KN_PARAM, GP_PARAM, GN_PARAM,
		WET_PARAM, POST_PARAM, TONE_PARAM, LOCUT_PARAM,
		DRIVE_CV_PARAM, BIAS_CV_PARAM, WET_CV_PARAM, TONE_CV_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		DRIVE_INPUT, BIAS_INPUT, WET_INPUT, TONE_INPUT,
		IN_L_INPUT, IN_R_INPUT, INPUTS_LEN
	};
	enum OutputId {
		ENV_OUTPUT, OUT_L_OUTPUT, OUT_R_OUTPUT, OUTPUTS_LEN
	};
	enum LightId {
		ENV_LIGHT, LIGHTS_LEN
	};

	enum Oversample { OS_1, OS_2, OS_4, NUM_OS };
	static int osFactor(int os) { return os == OS_4 ? 4 : os == OS_2 ? 2 : 1; }

	/** One side of the stereo pair: the linear blocks and the resamplers round
	    the nonlinear one. */
	struct Channel {
		dsp::BiquadFilter eqLow, eqMid, eqHigh;      // input EQ
		dsp::BiquadFilter tiltLow, tiltHigh, loCut;  // output EQ
		dsp::RCFilter dcBlock;
		dsp::Upsampler<2, 8> up2;
		dsp::Decimator<2, 8> dn2;
		dsp::Upsampler<4, 8> up4;
		dsp::Decimator<4, 8> dn4;
		float env = 0.f;

		void reset() {
			eqLow.reset(); eqMid.reset(); eqHigh.reset();
			tiltLow.reset(); tiltHigh.reset(); loCut.reset();
			dcBlock.reset();
			up2.reset(); dn2.reset(); up4.reset(); dn4.reset();
			env = 0.f;
		}
		void resetResamplers() {
			up2.reset(); dn2.reset(); up4.reset(); dn4.reset();
		}
	};

	Channel ch[2];
	Shape shape;
	dsp::ClockDivider ctrlDivider, lightDivider;

	// Menu state
	int oversample = OS_2;
	int oversampleRequest = OS_2;
	bool dryAfterDrive = false;   // Eichas fig. 6 taps the dry path after g_pre

	// Control-rate targets and their per-sample smoothed values. The gains are
	// slewed so a knob or a CV step never lands as a click.
	float driveTgt = 1.f, offsetTgt = 0.f, dynTgt = 0.f, wetTgt = 1.f, postTgt = 1.f;
	float drive = 1.f, offset = 0.f, dyn = 0.f, wet = 1.f, post = 1.f;
	float atkCoef = 0.f, relCoef = 0.f;
	bool controlsReady = false;   // has updateControls() run at least once?

	// Last values the filters were designed for, so the coefficients are only
	// recomputed when something moved.
	float fLow = 1e9f, fMid = 1e9f, fMidf = 1e9f, fHigh = 1e9f, fTone = 1e9f, fLocut = 1e9f;
	float fRate = 0.f;

	// Read-outs, published for the panel display from the audio thread.
	float dispDriveDb = 0.f;
	float dispBias = 0.f;
	float dispEnv = 0.f;
	float dispWet = 1.f;
	Shape dispShape;

	Gross() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

		configParam(LOW_PARAM, -12.f, 12.f, 0.f, "Low shelf (100 Hz)", " dB");
		configParam(MID_PARAM, -12.f, 12.f, 0.f, "Mid peak", " dB");
		configParam(MIDF_PARAM, 2.f, std::log10(5000.f), std::log10(800.f), "Mid frequency", " Hz", 10.f);
		configParam(HIGH_PARAM, -12.f, 12.f, 0.f, "High shelf (3 kHz)", " dB");
		configParam(DRIVE_PARAM, -12.f, 36.f, 12.f, "Drive", " dB");

		configParam(OFFSET_PARAM, -1.f, 1.f, 0.f, "Offset (static bias)");
		configParam(DYN_PARAM, -1.f, 1.f, 0.f, "Dynamic bias", "%", 0.f, 100.f);
		configParam(ATTACK_PARAM, 0.f, 3.f, std::log10(32.f), "Envelope attack", " ms", 10.f);
		configParam(RELEASE_PARAM, 0.f, std::log10(3000.f), std::log10(32.f), "Envelope release", " ms", 10.f);

		std::vector<std::string> curveLabels;
		curveLabels.push_back("Eichas tanh");
		curveLabels.push_back("Rational");
		curveLabels.push_back("Hard clip");
		curveLabels.push_back("Diode (exponential)");
		curveLabels.push_back("Cubic soft");
		configSwitch(CURVE_PARAM, 0.f, (float)(gross::NUM_CURVES - 1), 0.f, "Curve", curveLabels);

		configParam(KP_PARAM, 0.05f, 3.f, 1.f, "Positive knee");
		configParam(KN_PARAM, 0.05f, 3.f, 1.f, "Negative knee");
		configParam(GP_PARAM, -1.f, 1.f, 0.f, "Positive shape", "x", 10.f);
		configParam(GN_PARAM, -1.f, 1.f, 0.f, "Negative shape", "x", 10.f);

		configParam(WET_PARAM, 0.f, 1.f, 1.f, "Wet", "%", 0.f, 100.f);
		configParam(POST_PARAM, -24.f, 24.f, 0.f, "Post gain", " dB");
		configParam(TONE_PARAM, -1.f, 1.f, 0.f, "Tone (tilt about 1 kHz)", " dB", 0.f, 6.f);
		configParam(LOCUT_PARAM, 1.f, 3.f, std::log10(20.f), "Low cut", " Hz", 10.f);

		configParam(DRIVE_CV_PARAM, -1.f, 1.f, 0.f, "Drive CV", "%", 0.f, 100.f);
		configParam(BIAS_CV_PARAM, -1.f, 1.f, 0.f, "Bias CV", "%", 0.f, 100.f);
		configParam(WET_CV_PARAM, -1.f, 1.f, 0.f, "Wet CV", "%", 0.f, 100.f);
		configParam(TONE_CV_PARAM, -1.f, 1.f, 0.f, "Tone CV", "%", 0.f, 100.f);
		getParamQuantity(DRIVE_CV_PARAM)->randomizeEnabled = false;
		getParamQuantity(BIAS_CV_PARAM)->randomizeEnabled = false;
		getParamQuantity(WET_CV_PARAM)->randomizeEnabled = false;
		getParamQuantity(TONE_CV_PARAM)->randomizeEnabled = false;

		configInput(DRIVE_INPUT, "Drive CV (24 dB per 10 V at full trim)");
		configInput(BIAS_INPUT, "Bias CV (1 knee unit per 10 V at full trim)");
		configInput(WET_INPUT, "Wet CV");
		configInput(TONE_INPUT, "Tone CV");
		configInput(IN_L_INPUT, "Left audio");
		configInput(IN_R_INPUT, "Right audio (normalled to left)");
		configOutput(ENV_OUTPUT, "Envelope (0-10 V)");
		configOutput(OUT_L_OUTPUT, "Left audio");
		configOutput(OUT_R_OUTPUT, "Right audio");

		configBypass(IN_L_INPUT, OUT_L_OUTPUT);
		configBypass(IN_R_INPUT, OUT_R_OUTPUT);

		ctrlDivider.setDivision(8);
		lightDivider.setDivision(256);
		shape.set(gross::CURVE_TANH, 1.f, 1.f, 1.f, 1.f);
		dispShape = shape;
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		fRate = 0.f;   // forces every filter to be redesigned on the next block
		(void) e;
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		ch[0].reset();
		ch[1].reset();
		fRate = 0.f;
	}

	/** Designs one biquad and copies its coefficients to the other channel, so
	    the trigonometry is done once per pair. */
	static void design(dsp::BiquadFilter& l, dsp::BiquadFilter& r,
	                   dsp::BiquadFilter::Type type, float f, float Q, float V) {
		l.setParameters(type, f, Q, V);
		std::memcpy(r.b, l.b, sizeof(l.b));
		std::memcpy(r.a, l.a, sizeof(l.a));
	}

	/** Everything read from the knobs and CV jacks, once per control block. */
	void updateControls(const ProcessArgs& args) {
		const float sr = args.sampleRate;
		bool rateChanged = (sr != fRate);
		fRate = sr;

		// --- gains ------------------------------------------------------------
		float driveDb = params[DRIVE_PARAM].getValue();
		if (inputs[DRIVE_INPUT].isConnected())
			driveDb += inputs[DRIVE_INPUT].getVoltage() / 10.f * params[DRIVE_CV_PARAM].getValue() * 24.f;
		driveDb = clamp(driveDb, -24.f, 48.f);
		driveTgt = dbToLin(driveDb);

		float off = params[OFFSET_PARAM].getValue();
		if (inputs[BIAS_INPUT].isConnected())
			off += inputs[BIAS_INPUT].getVoltage() / 10.f * params[BIAS_CV_PARAM].getValue();
		offsetTgt = clamp(off, -2.f, 2.f);

		dynTgt = params[DYN_PARAM].getValue();

		float w = params[WET_PARAM].getValue();
		if (inputs[WET_INPUT].isConnected())
			w += inputs[WET_INPUT].getVoltage() / 10.f * params[WET_CV_PARAM].getValue();
		wetTgt = clamp(w, 0.f, 1.f);

		postTgt = dbToLin(params[POST_PARAM].getValue());

		// --- envelope ballistics --------------------------------------------
		float atkMs = std::pow(10.f, params[ATTACK_PARAM].getValue());
		float relMs = std::pow(10.f, params[RELEASE_PARAM].getValue());
		atkCoef = 1.f - std::exp(-1000.f / (atkMs * sr));
		relCoef = 1.f - std::exp(-1000.f / (relMs * sr));

		// --- the mapping ------------------------------------------------------
		int curve = (int) clamp(std::round(params[CURVE_PARAM].getValue()), 0.f, (float)(gross::NUM_CURVES - 1));
		shape.set(curve,
		          params[KP_PARAM].getValue(), params[KN_PARAM].getValue(),
		          std::pow(10.f, params[GP_PARAM].getValue()),
		          std::pow(10.f, params[GN_PARAM].getValue()));

		// --- filters, only when something moved -----------------------------
		float low = params[LOW_PARAM].getValue();
		float mid = params[MID_PARAM].getValue();
		float midf = params[MIDF_PARAM].getValue();
		float high = params[HIGH_PARAM].getValue();
		if (rateChanged || low != fLow) {
			design(ch[0].eqLow, ch[1].eqLow, dsp::BiquadFilter::LOWSHELF, kLowShelfHz / sr, 0.f, dbToLin(low));
			fLow = low;
		}
		if (rateChanged || mid != fMid || midf != fMidf) {
			float hz = clamp(std::pow(10.f, midf), 20.f, sr * 0.45f);
			design(ch[0].eqMid, ch[1].eqMid, dsp::BiquadFilter::PEAK, hz / sr, kMidQ, dbToLin(mid));
			fMid = mid;
			fMidf = midf;
		}
		if (rateChanged || high != fHigh) {
			design(ch[0].eqHigh, ch[1].eqHigh, dsp::BiquadFilter::HIGHSHELF, kHighShelfHz / sr, 0.f, dbToLin(high));
			fHigh = high;
		}

		float tone = params[TONE_PARAM].getValue();
		if (inputs[TONE_INPUT].isConnected())
			tone += inputs[TONE_INPUT].getVoltage() / 10.f * params[TONE_CV_PARAM].getValue();
		tone = clamp(tone, -1.f, 1.f);
		if (rateChanged || tone != fTone) {
			// A tilt: -6 dB on one shelf and +6 dB on the other, both at 1 kHz.
			design(ch[0].tiltLow, ch[1].tiltLow, dsp::BiquadFilter::LOWSHELF, kTiltHz / sr, 0.f, dbToLin(-6.f * tone));
			design(ch[0].tiltHigh, ch[1].tiltHigh, dsp::BiquadFilter::HIGHSHELF, kTiltHz / sr, 0.f, dbToLin(6.f * tone));
			fTone = tone;
		}

		float locut = params[LOCUT_PARAM].getValue();
		if (rateChanged || locut != fLocut) {
			float hz = clamp(std::pow(10.f, locut), 5.f, sr * 0.45f);
			design(ch[0].loCut, ch[1].loCut, dsp::BiquadFilter::HIGHPASS, hz / sr, M_SQRT1_2, 1.f);
			fLocut = locut;
		}
		if (rateChanged) {
			ch[0].dcBlock.setCutoffFreq(kDcBlockHz / sr);
			ch[1].dcBlock.setCutoffFreq(kDcBlockHz / sr);
		}

		dispDriveDb = driveDb;
	}

	/** One channel, one sample. `in` is volts; returns volts. */
	float processChannel(Channel& c, float in) {
		float x = in * 0.2f;   // +/-5 V -> +/-1
		x = c.eqLow.process(x);
		x = c.eqMid.process(x);
		x = c.eqHigh.process(x);

		// Envelope of the equalised input, so DYN is in the same units as
		// OFFSET: 1.0 shifts the operating point by one knee unit at 5 V in.
		float a = std::fabs(x);
		c.env += (a > c.env ? atkCoef : relCoef) * (a - c.env);

		float u = x * drive;
		float dry = dryAfterDrive ? u : x;
		u += offset + dyn * c.env;

		float y;
		switch (oversample) {
			case OS_2: {
				float buf[2];
				c.up2.process(u, buf);
				for (int i = 0; i < 2; i++)
					buf[i] = gross::map(shape, buf[i]);
				y = c.dn2.process(buf);
			} break;
			case OS_4: {
				float buf[4];
				c.up4.process(u, buf);
				for (int i = 0; i < 4; i++)
					buf[i] = gross::map(shape, buf[i]);
				y = c.dn4.process(buf);
			} break;
			default:
				y = gross::map(shape, u);
		}

		y = wet * y + (1.f - wet) * dry;
		y *= post;

		y = c.tiltLow.process(y);
		y = c.tiltHigh.process(y);
		y = c.loCut.process(y);
		c.dcBlock.process(y);
		y = c.dcBlock.highpass();

		return y * 5.f;
	}

	void process(const ProcessArgs& args) override {
		// Oversampling changes land here, on the audio thread, so a resampler is
		// never reset under a sample it is producing.
		if (oversampleRequest != oversample) {
			oversample = oversampleRequest;
			ch[0].resetResamplers();
			ch[1].resetResamplers();
		}

		// The EQ and tilt biquads must be designed before a single sample goes
		// through them. Rack default-constructs a BiquadFilter by calling
		// setParameters(LOWPASS, f=0, Q=0, V=1), whose LOWPASS branch computes
		// 1/(1 + K/Q + K*K) -- with K=0 and Q=0 that is 0/0, so every
		// coefficient starts as NaN (Rack-SDK/include/dsp/filter.hpp:307,335).
		// updateControls() is what replaces them, and it runs behind
		// ctrlDivider, which does not fire until its eighth call. Those seven
		// samples were enough: NaN lodges in the filters' state history and
		// never washes out, so the module sat at a constant 12 V forever --
		// clamp() returns its upper bound for NaN -- with ENV pinned at 10 V,
		// whatever was patched in.
		if (!controlsReady) {
			controlsReady = true;
			updateControls(args);
		}
		else if (ctrlDivider.process()) {
			updateControls(args);
		}

		// Slew the gains toward their targets: ~1.5 ms at 48 kHz.
		const float k = 0.015f;
		drive  += k * (driveTgt - drive);
		offset += k * (offsetTgt - offset);
		dyn    += k * (dynTgt - dyn);
		wet    += k * (wetTgt - wet);
		post   += k * (postTgt - post);

		// getVoltageSum so a polyphonic cable is summed rather than silently
		// dropping channels; IN R normals to IN L for mono-in dual-mono.
		float inL = inputs[IN_L_INPUT].getVoltageSum();
		float inR = inputs[IN_R_INPUT].isConnected() ? inputs[IN_R_INPUT].getVoltageSum() : inL;
		inL = clamp(inL, -20.f, 20.f);
		inR = clamp(inR, -20.f, 20.f);

		float outL = processChannel(ch[0], inL);
		float outR = processChannel(ch[1], inR);
		outputs[OUT_L_OUTPUT].setVoltage(clamp(outL, -12.f, 12.f));
		outputs[OUT_R_OUTPUT].setVoltage(clamp(outR, -12.f, 12.f));

		float env = std::max(ch[0].env, ch[1].env);
		outputs[ENV_OUTPUT].setVoltage(clamp(env * 10.f, 0.f, 10.f));

		if (lightDivider.process()) {
			lights[ENV_LIGHT].setBrightness(clamp(env, 0.f, 1.f));
			dispEnv = env;
			dispBias = offset + dyn * ch[0].env;
			dispWet = wet;
			dispShape = shape;
		}
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "oversample", json_integer(oversampleRequest));
		json_object_set_new(root, "dryAfterDrive", json_boolean(dryAfterDrive));
		return root;
	}

	void dataFromJson(json_t* root) override {
		json_t* j;
		j = json_object_get(root, "oversample");
		if (j) oversampleRequest = clamp((int) json_integer_value(j), 0, NUM_OS - 1);
		j = json_object_get(root, "dryAfterDrive");
		if (j) dryAfterDrive = json_boolean_value(j);
	}

	// --- presets ------------------------------------------------------------
	void applyPreset(const Preset& p) {
		getParamQuantity(CURVE_PARAM)->setValue((float) p.curve);
		getParamQuantity(DRIVE_PARAM)->setValue(p.driveDb);
		getParamQuantity(OFFSET_PARAM)->setValue(p.offset);
		getParamQuantity(DYN_PARAM)->setValue(p.dyn);
		getParamQuantity(ATTACK_PARAM)->setValue(std::log10(p.attackMs));
		getParamQuantity(RELEASE_PARAM)->setValue(std::log10(p.releaseMs));
		getParamQuantity(KP_PARAM)->setValue(p.kp);
		getParamQuantity(KN_PARAM)->setValue(p.kn);
		getParamQuantity(GP_PARAM)->setValue(std::log10(p.gp));
		getParamQuantity(GN_PARAM)->setValue(std::log10(p.gn));
		getParamQuantity(WET_PARAM)->setValue(p.wet);
		getParamQuantity(POST_PARAM)->setValue(p.postDb);
		getParamQuantity(LOW_PARAM)->setValue(p.low);
		getParamQuantity(MID_PARAM)->setValue(p.mid);
		getParamQuantity(MIDF_PARAM)->setValue(std::log10(p.midfHz));
		getParamQuantity(HIGH_PARAM)->setValue(p.high);
		getParamQuantity(TONE_PARAM)->setValue(p.tone);
		getParamQuantity(LOCUT_PARAM)->setValue(std::log10(p.locutHz));
	}

	static bool near(float a, float b, float tol = 0.02f) {
		return std::fabs(a - b) <= tol;
	}

	/** True when every panel knob still sits where the preset put it. */
	bool matchesPreset(const Preset& p) {
		return near(params[CURVE_PARAM].getValue(), (float) p.curve, 0.4f)
		    && near(params[DRIVE_PARAM].getValue(), p.driveDb, 0.3f)
		    && near(params[OFFSET_PARAM].getValue(), p.offset)
		    && near(params[DYN_PARAM].getValue(), p.dyn)
		    && near(params[ATTACK_PARAM].getValue(), std::log10(p.attackMs))
		    && near(params[RELEASE_PARAM].getValue(), std::log10(p.releaseMs))
		    && near(params[KP_PARAM].getValue(), p.kp)
		    && near(params[KN_PARAM].getValue(), p.kn)
		    && near(params[GP_PARAM].getValue(), std::log10(p.gp))
		    && near(params[GN_PARAM].getValue(), std::log10(p.gn))
		    && near(params[WET_PARAM].getValue(), p.wet)
		    && near(params[POST_PARAM].getValue(), p.postDb, 0.3f)
		    && near(params[LOW_PARAM].getValue(), p.low, 0.3f)
		    && near(params[MID_PARAM].getValue(), p.mid, 0.3f)
		    && near(params[MIDF_PARAM].getValue(), std::log10(p.midfHz))
		    && near(params[HIGH_PARAM].getValue(), p.high, 0.3f)
		    && near(params[TONE_PARAM].getValue(), p.tone)
		    && near(params[LOCUT_PARAM].getValue(), std::log10(p.locutHz));
	}

	/** The name the read-out shows: a preset while it is untouched, else CUSTOM. */
	const char* presetName() {
		for (int i = 0; i < kNumPresets; i++)
			if (matchesPreset(kPresets[i]))
				return kPresets[i].name;
		return "Custom";
	}
};


// ---------------------------------------------------------------------------
// Look and feel. The palette, the shared hardware and the entire silkscreen come
// from src/PanelTheme.hpp and this panel's generated Panel.hpp -- see
// ../panelkit/README.md. Nothing about the look is written here.

typedef RoundBlackKnob PanelKnob;


// ---------------------------------------------------------------------------
// Panel display: the transfer curve as it stands -- drive, offset and the live
// dynamic bias included, wet/dry included, before POST and the output EQ -- with
// the preset name, the curve, and the numbers behind the picture. The curve is
// nanovg geometry; every word goes through panel::text.

struct GrossDisplay : LedDisplay {
	Gross* module = NULL;

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) {
			LedDisplay::drawLayer(args, layer);
			return;
		}
		NVGcontext* vg = args.vg;

		Shape shape;
		float drive = dbToLin(12.f), bias = 0.f, wet = 1.f, driveDb = 12.f, env = 0.f;
		bool dryAfter = false;
		const char* name = "Custom";
		if (module) {
			shape = module->dispShape;
			driveDb = module->dispDriveDb;
			drive = dbToLin(driveDb);
			bias = module->dispBias;
			wet = module->dispWet;
			env = module->dispEnv;
			dryAfter = module->dryAfterDrive;
			name = module->presetName();
		}
		else {
			shape.set(gross::CURVE_TANH, 1.f, 1.f, 1.f, 1.f);
		}

		// --- the curve --------------------------------------------------------
		// Input +/-1 (5 V) across the plot; output +/-1.25 up it.
		const float px0 = 4.f, pw = 38.f;
		const float py0 = 2.5f, ph = box.size.y - 5.f;
		const float cx = px0 + pw / 2.f, cy = py0 + ph / 2.f;
		const float yScale = ph / 2.5f;

		nvgSave(vg);
		nvgScissor(vg, px0, py0, pw, ph);

		// Axes, faint.
		nvgBeginPath(vg);
		nvgMoveTo(vg, px0, cy); nvgLineTo(vg, px0 + pw, cy);
		nvgMoveTo(vg, cx, py0); nvgLineTo(vg, cx, py0 + ph);
		nvgStrokeColor(vg, panel::alpha(panel::SAGE, 0.35f));
		nvgStrokeWidth(vg, 0.6f);
		nvgStroke(vg);

		// The curve itself, dense enough that a 36 dB step still reads as a line.
		const int N = 96;
		nvgBeginPath(vg);
		for (int i = 0; i <= N; i++) {
			float x = -1.f + 2.f * i / N;
			float u = x * drive + bias;
			float y = wet * gross::map(shape, u) + (1.f - wet) * (dryAfter ? x * drive : x);
			y = clamp(y, -1.25f, 1.25f);
			float sx = px0 + (x + 1.f) * 0.5f * pw;
			float sy = cy - y * yScale;
			if (i == 0) nvgMoveTo(vg, sx, sy); else nvgLineTo(vg, sx, sy);
		}
		nvgStrokeColor(vg, panel::LIME);
		nvgStrokeWidth(vg, 1.2f);
		nvgLineJoin(vg, NVG_ROUND);
		nvgStroke(vg);
		nvgRestore(vg);

		// --- the words ----------------------------------------------------------
		const float col = px0 + pw + 8.f;
		const float rightX = box.size.x - 5.f;
		const NVGcolor dim = panel::alpha(panel::LIME, 0.55f);
		const panel::TextStyle NAME(panel::Face::Mono, 10.f, panel::MINT,
			NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE, -0.5f);
		const panel::TextStyle CURVE(panel::Face::Mono, 9.f, panel::SAGE,
			NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE);
		const panel::TextStyle TAG(panel::Face::Mono, 8.f, dim,
			NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
		const panel::TextStyle RTAG(panel::Face::Mono, 8.f, dim,
			NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE);

		// Row 1: the preset, and which curve family is running.
		panel::text(vg, NAME, col, 12.f, name);
		panel::text(vg, CURVE, rightX, 12.f, kCurveShort[shape.curve]);

		// Row 2: drive, the operating point as it stands, and the envelope.
		// DSEG7 carries [0-9 . : -] only, so the numerals are formatted without a
		// '+' and the units stay in the text face.
		float x = panel::text(vg, TAG, col, 24.f, "DRV");
		x = panel::segValue(vg, x + 2.5f, 24.f, 8.f, string::f("%.0f", driveDb), "dB", dim);
		x = panel::text(vg, TAG, x + 7.f, 24.f, "BIAS");
		panel::segValue(vg, x + 2.5f, 24.f, 8.f, string::f("%.2f", bias), "", dim);

		panel::text(vg, RTAG, rightX, 24.f,
			string::f("ENV %3.0f%%  WET %3.0f%%", clamp(env, 0.f, 9.99f) * 100.f, wet * 100.f));
	}
};


struct GrossWidget : ModuleWidget {
	static Vec at(const Vec& p) { return panel::mm(p.x, p.y); }

	GrossWidget(Gross* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Gross.svg")));

		panel::addScrews(this);
		panel::addLabels(this);

		GrossDisplay* display = new GrossDisplay;
		display->module = module;
		display->box.pos = panel::mm(panel::GLASS_X, panel::GLASS_Y);
		display->box.size = panel::mm(panel::GLASS_W, panel::GLASS_H);
		addChild(display);

		addParam(createParamCentered<PanelKnob>(at(panel::LOW_POS), module, Gross::LOW_PARAM));
		addParam(createParamCentered<PanelKnob>(at(panel::MID_POS), module, Gross::MID_PARAM));
		addParam(createParamCentered<PanelKnob>(at(panel::MIDF_POS), module, Gross::MIDF_PARAM));
		addParam(createParamCentered<PanelKnob>(at(panel::HIGH_POS), module, Gross::HIGH_PARAM));
		addParam(createParamCentered<PanelKnob>(at(panel::DRIVE_POS), module, Gross::DRIVE_PARAM));

		addParam(createParamCentered<PanelKnob>(at(panel::OFFSET_POS), module, Gross::OFFSET_PARAM));
		addParam(createParamCentered<PanelKnob>(at(panel::DYN_POS), module, Gross::DYN_PARAM));
		addParam(createParamCentered<PanelKnob>(at(panel::ATTACK_POS), module, Gross::ATTACK_PARAM));
		addParam(createParamCentered<PanelKnob>(at(panel::RELEASE_POS), module, Gross::RELEASE_PARAM));
		addParam(createParamCentered<PanelKnob>(at(panel::CURVE_POS), module, Gross::CURVE_PARAM));

		addParam(createParamCentered<Trimpot>(at(panel::KP_POS), module, Gross::KP_PARAM));
		addParam(createParamCentered<Trimpot>(at(panel::KN_POS), module, Gross::KN_PARAM));
		addParam(createParamCentered<Trimpot>(at(panel::GP_POS), module, Gross::GP_PARAM));
		addParam(createParamCentered<Trimpot>(at(panel::GN_POS), module, Gross::GN_PARAM));

		addParam(createParamCentered<PanelKnob>(at(panel::WET_POS), module, Gross::WET_PARAM));
		addParam(createParamCentered<PanelKnob>(at(panel::POST_POS), module, Gross::POST_PARAM));
		addParam(createParamCentered<PanelKnob>(at(panel::TONE_POS), module, Gross::TONE_PARAM));
		addParam(createParamCentered<PanelKnob>(at(panel::LOCUT_POS), module, Gross::LOCUT_PARAM));

		addParam(createParamCentered<Trimpot>(at(panel::DRIVE_CV_POS), module, Gross::DRIVE_CV_PARAM));
		addParam(createParamCentered<Trimpot>(at(panel::BIAS_CV_POS), module, Gross::BIAS_CV_PARAM));
		addParam(createParamCentered<Trimpot>(at(panel::WET_CV_POS), module, Gross::WET_CV_PARAM));
		addParam(createParamCentered<Trimpot>(at(panel::TONE_CV_POS), module, Gross::TONE_CV_PARAM));

		addInput(createInputCentered<panel::PortIn>(at(panel::DRIVE_IN_POS), module, Gross::DRIVE_INPUT));
		addInput(createInputCentered<panel::PortIn>(at(panel::BIAS_IN_POS), module, Gross::BIAS_INPUT));
		addInput(createInputCentered<panel::PortIn>(at(panel::WET_IN_POS), module, Gross::WET_INPUT));
		addInput(createInputCentered<panel::PortIn>(at(panel::TONE_IN_POS), module, Gross::TONE_INPUT));
		addOutput(createOutputCentered<panel::PortOut>(at(panel::ENV_OUT_POS), module, Gross::ENV_OUTPUT));
		addInput(createInputCentered<panel::PortIn>(at(panel::IN_L_POS), module, Gross::IN_L_INPUT));
		addInput(createInputCentered<panel::PortIn>(at(panel::IN_R_POS), module, Gross::IN_R_INPUT));
		addOutput(createOutputCentered<panel::PortOut>(at(panel::OUT_L_POS), module, Gross::OUT_L_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(at(panel::OUT_R_POS), module, Gross::OUT_R_OUTPUT));

		addChild(createLightCentered<SmallLight<panel::LimeLight> >(
		             at(panel::ENV_LED_POS), module, Gross::ENV_LIGHT));
	}

	void appendContextMenu(Menu* menu) override {
		Gross* m = dynamic_cast<Gross*>(module);
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Gross"));

		// A preset is every knob at once; the read-out names it while it holds.
		menu->addChild(createSubmenuItem("Filing status", m->presetName(), [=](Menu* sub) {
			for (int i = 0; i < kNumPresets; i++) {
				const Preset* p = &kPresets[i];
				sub->addChild(createCheckMenuItem(p->name, "",
					[=]() { return m->matchesPreset(*p); },
					[=]() { m->applyPreset(*p); }));
			}
		}));

		std::vector<std::string> osLabels;
		osLabels.push_back("Off (1x)");
		osLabels.push_back("2x");
		osLabels.push_back("4x");
		menu->addChild(createIndexSubmenuItem("Oversampling", osLabels,
			[=]() { return (size_t) m->oversampleRequest; },
			[=](size_t v) { m->oversampleRequest = (int) v; }));

		menu->addChild(createBoolMenuItem("Dry path taps after DRIVE", "",
			[=]() { return m->dryAfterDrive; },
			[=](bool v) { m->dryAfterDrive = v; }));
	}
};


Model* modelGross = createModel<Gross, GrossWidget>("Gross");

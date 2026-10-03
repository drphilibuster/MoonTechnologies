#include "../plugin.hpp"
#include "Panel.hpp"
#include "Cores.hpp"
#include "Cd4046.hpp"

using sixfigures::Voice;
using sixfigures::Core;

static const char* kCoreNames[] = {
	"40106 Schmitt (square)", "4069 AAC (triangle)", "4046 PLL (square)", "Avalanche (saw)"
};


struct SixFigures : Module {
	float dispHz[6] = {};      // what the read-out prints: each voice's running frequency
	enum ParamId {
		CORE1_PARAM, CORE2_PARAM, CORE3_PARAM, CORE4_PARAM, CORE5_PARAM, CORE6_PARAM,
		RANGE_PARAM,
		RATE1_PARAM, RATE2_PARAM, RATE3_PARAM, RATE4_PARAM, RATE5_PARAM, RATE6_PARAM,
		CAPTURE_PARAM,
		CV1_PARAM, CV2_PARAM, CV3_PARAM, CV4_PARAM, CV5_PARAM, CV6_PARAM,
		DRIFT_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		CV1_IN_INPUT, CV2_IN_INPUT, CV3_IN_INPUT, CV4_IN_INPUT, CV5_IN_INPUT, CV6_IN_INPUT,
		SYNC_INPUT, SIGNAL_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		OUT1_OUTPUT, OUT2_OUTPUT, OUT3_OUTPUT, OUT4_OUTPUT, OUT5_OUTPUT, OUT6_OUTPUT,
		AUX1_OUTPUT, AUX2_OUTPUT, AUX3_OUTPUT, AUX4_OUTPUT, AUX5_OUTPUT, AUX6_OUTPUT,
		MIX_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		LED1_LIGHT, LED2_LIGHT, LED3_LIGHT, LED4_LIGHT, LED5_LIGHT, LED6_LIGHT,
		LOCK_LIGHT,
		LIGHTS_LEN
	};

	Voice voices[6];
	dsp::SchmittTrigger syncTrigger;
	dsp::SchmittTrigger signalTrigger;
	dsp::ClockDivider lightDivider;

	// Non-panel state: the CV response mode. Off (default) is the "crude", pot-like
	// response every RC core in the family actually has; on is a proper 1 V/oct
	// exponential converter, which none of the originals had but Rack makes cheap.
	bool voltPerOct = false;
	// The board's SIGIN/Sync/RingMod jack goes to the 4046's R1 pin (11). Off (default): SIGNAL is only the
	// PLL reference, as before. On: SIGNAL is also patched onto pin 11 of every 4046 voice (docs/SixFigures.md).
	bool signalOnR1 = false;
	// The 40106 core's supply (VDD = VCC on the board). Its thresholds are a function of it (Cd40106.hpp).
	float vdd = (float) sixfigures::schmitt::kVddDefault;
	// The avalanche core as the board (AvalancheBoard.hpp, docs/SixFigures.md). Off (default): the core is the
	// abstract one, RATE and CV asking for a frequency. On: RATE is the board's R2 pot, the range switch picks C1/C2,
	// CV drives the vactrol's LED through R8 330 and R9 (CV AMOUNT), and `avalancheTap` picks what OUT carries.
	bool avalancheBoard = false;
	int avalancheTap = sixfigures::avalanche::board::TAP_SAW;
	bool avalancheLed3 = false;          // the board's optional CV LED, in series with the vactrol's

	SixFigures() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

		std::vector<std::string> coreLabels;
		for (int c = 0; c < sixfigures::NUM_CORES; c++)
			coreLabels.push_back(kCoreNames[c]);

		for (int i = 0; i < 6; i++) {
			configSwitch(CORE1_PARAM + i, 0.f, (float)(sixfigures::NUM_CORES - 1), 0.f,
			             string::f("Voice %d core", i + 1), coreLabels);
			getParamQuantity(CORE1_PARAM + i)->snapEnabled = true;
			voices[i].strike = sixfigures::avalanche::Strike(0x57121CEu + 7919u * (uint32_t)i);   // each voice's own jitter

			configParam(RATE1_PARAM + i, 0.f, 1.f, 0.5f, string::f("Voice %d rate", i + 1));

			configParam(CV1_PARAM + i, -1.f, 1.f, 0.f,
			            string::f("Voice %d CV amount", i + 1), "%", 0.f, 100.f);
			getParamQuantity(CV1_PARAM + i)->randomizeEnabled = false;

			configInput(CV1_IN_INPUT + i, string::f("Voice %d CV", i + 1));
			configOutput(OUT1_OUTPUT + i, string::f("Voice %d main", i + 1));
			configOutput(AUX1_OUTPUT + i, string::f("Voice %d auxiliary", i + 1));
		}

		std::vector<std::string> rangeLabels;
		rangeLabels.push_back("LO (LFO)");
		rangeLabels.push_back("HI (audio)");
		configSwitch(RANGE_PARAM, 0.f, 1.f, 1.f, "Range", rangeLabels);

		configParam(CAPTURE_PARAM, 0.f, 1.f, 0.3f, "PLL capture bandwidth", "%", 0.f, 100.f);
		configParam(DRIFT_PARAM, 0.f, 1.f, 0.2f, "Avalanche drift", "%", 0.f, 100.f);

		configInput(SYNC_INPUT, "Sync (hard reset, all voices)");
		configInput(SIGNAL_INPUT, "Signal (PLL comparator reference)");
		configOutput(MIX_OUTPUT, "Mix");

		lightDivider.setDivision(256);
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		for (int i = 0; i < 6; i++)
			voices[i].reset();
		syncTrigger.reset();
		signalTrigger.reset();
	}

	// One-pole coefficients that depend only on the sample rate, and (for the
	// PLL loop filter) on CAPTURE. Evaluated inline they cost three std::exp()
	// per voice per sample -- eighteen per sample for six voices -- to produce
	// values that change only when the rate or a knob does.
	float coeffSr = 0.f;       // sample rate the cached coefficients are for
	float coeffCapture = -1.f; // CAPTURE the cached pllCoeff is for
	float driftCoeff = 0.f;
	float slowCoeff = 0.f;
	float pllCoeff = 0.f;

	void updateCoeffs(float sampleRate, float sampleTime, float capture) {
		if (sampleRate != coeffSr) {
			coeffSr = sampleRate;
			const float k = -sampleTime * 2.f * (float) M_PI;
			driftCoeff = 1.f - std::exp(k * 0.3f);
			slowCoeff = 1.f - std::exp(k * 0.5f);
			coeffCapture = -1.f;  // rate change invalidates the capture coeff too
		}
		if (capture != coeffCapture) {
			coeffCapture = capture;
			pllCoeff = 1.f - std::exp(-sampleTime * 2.f * (float) M_PI
			                          * (0.2f + capture * 8.f));
		}
	}

	void process(const ProcessArgs& args) override {
		bool lfoRange = params[RANGE_PARAM].getValue() < 0.5f;
		float lo, hi;
		sixfigures::rangeHz(lfoRange, lo, hi);

		bool sync = syncTrigger.process(inputs[SYNC_INPUT].getVoltage(), 0.1f, 2.f);
		if (sync) {
			for (int i = 0; i < 6; i++)
				voices[i].reset();
		}

		bool signalConnected = inputs[SIGNAL_INPUT].isConnected();
		signalTrigger.process(inputs[SIGNAL_INPUT].getVoltage(), -0.1f, 0.1f);
		bool signalHigh = signalTrigger.isHigh();

		float capture = params[CAPTURE_PARAM].getValue();
		float driftAmount = params[DRIFT_PARAM].getValue();
		updateCoeffs(args.sampleRate, args.sampleTime, capture);

		float mixSum = 0.f;
		bool lightUpdate = lightDivider.process();
		float lightDt = args.sampleTime * lightDivider.getDivision();
		bool anyLocked = false;

		for (int i = 0; i < 6; i++) {
			Voice& v = voices[i];
			int core = (int)std::round(params[CORE1_PARAM + i].getValue());
			core = clamp(core, 0, sixfigures::NUM_CORES - 1);

			float knob = params[RATE1_PARAM + i].getValue();
			float cvAmt = params[CV1_PARAM + i].getValue();
			float cv = inputs[CV1_IN_INPUT + i].isConnected()
			               ? inputs[CV1_IN_INPUT + i].getVoltage() : 0.f;

			float freq = voltPerOct ? sixfigures::voltOctFreq(knob, cv, cvAmt, lo, hi)
			                        : sixfigures::potFreq(knob, cv, cvAmt, lo, hi);

			// 4046: the CD4046B's own VCO law (Cd4046.hpp). RATE and CV make VCOIN (the board feeds it from
			// an LM358 stage); the range switch picks C1; R1 is the board's 100k. The old exponential
			// RATE taper does not apply: the chip's response is linear in VCOIN, zero at 0 V.
			if (core == sixfigures::CORE_PLL) {
				namespace cd = sixfigures::cd4046;
				const double c1 = lfoRange ? cd::kC1Lfo : cd::kC1Audio;
				double vcoin;
				if (voltPerOct)
					vcoin = cd::vcoinFor(freq, cd::kVdd, cd::kR1, c1);  // an exponential converter ahead of VCOIN
				else
					vcoin = cd::kVdd * clamp(knob + cv * 0.1f * cvAmt, 0.f, 1.f);
				cd::Jack jack;
				if (signalOnR1 && signalConnected)
					jack = cd::Jack(inputs[SIGNAL_INPUT].getVoltage(), 1000.0);  // a module output: 1k, assumed
				freq = (float) cd::freq(vcoin, cd::kVdd, cd::kR1, c1, jack);
			}

			// Reverse avalanche: a slow random wander on top of the RATE knob,
			// modelling the vactrol's LDR never quite settling. One-pole low-pass
			// of white noise, so the wander is smooth rather than stepped.
			if (core == sixfigures::CORE_AVALANCHE) {
				float target = random::uniform() * 2.f - 1.f;
				v.drift += (target - v.drift) * driftCoeff;
				freq *= dsp::exp2_taylor5(v.drift * driftAmount * 0.5f);
			}

			// 4046 PLL: an XOR phase detector between this voice's own square and
			// SIGNAL (itself squared by a Schmitt comparator), low-passed by a
			// CAPTURE-controlled loop filter into a control voltage that pulls the
			// free-running frequency above towards SIGNAL's.
			bool pllHasError = false;
			float pllErrorBit = 0.f;
			if (core == sixfigures::CORE_PLL) {
				if (signalConnected) {
					bool ownHigh = v.phase < 0.5f;
					pllErrorBit = (ownHigh != signalHigh) ? 1.f : 0.f;
					pllHasError = true;
					v.loopFilter += (pllErrorBit - 0.5f - v.loopFilter) * pllCoeff;
					v.loopFilterSlow += (v.loopFilter - v.loopFilterSlow) * slowCoeff;
					float maxPullOct = 0.2f + capture * 3.5f;
					freq *= dsp::exp2_taylor5(clamp(v.loopFilter * 2.f, -1.f, 1.f) * maxPullOct);

					bool settled = std::fabs(v.loopFilter - v.loopFilterSlow) < 0.04f;
					float lockCoeff = args.sampleTime * (settled ? 2.f : 8.f);
					v.lockEnv += ((settled ? 1.f : 0.f) - v.lockEnv) * lockCoeff;
					v.locked = v.lockEnv > 0.5f;
				}
				else {
					v.loopFilter *= 0.999f;
					v.loopFilterSlow *= 0.999f;
					v.lockEnv = 0.f;
					v.locked = false;
				}
				anyLocked = anyLocked || v.locked;
			}

			freq = clamp(freq, 0.001f, args.sampleRate * 0.45f);
			dispHz[i] = freq;
			float dt = freq * args.sampleTime;

			// The board's avalanche core runs its own circuit; everything above (the abstract RATE/CV frequency, DRIFT's
			// wander) is shared with the default core, so DRIFT still moves the pitch the same way, as a speed factor.
			sixfigures::avalanche::board::Board::Out boardOut = {};
			const bool boardMode = avalancheBoard && core == sixfigures::CORE_AVALANCHE;
			if (boardMode) {
				if (v.boardSr != args.sampleRate) {
					v.board.setRate(args.sampleRate);
					v.boardSr = args.sampleRate;
				}
				const double speed = dsp::exp2_taylor5(v.drift * driftAmount * 0.5f);
				boardOut = v.board.process(knob, lfoRange, cv, std::max(cvAmt, 0.f), avalancheLed3, speed);
				dispHz[i] = (float) v.board.hz;
			}

			float prevPhase = v.phase;
			v.phase += dt;
			// the avalanche core's cycle ends where this cycle's junction struck (Avalanche.hpp `Strike`)
			float wrapAt = core == sixfigures::CORE_AVALANCHE ? v.strike.theta : 1.f;
			if (v.phase >= wrapAt) {
				v.phase -= wrapAt;
				if (core == sixfigures::CORE_AVALANCHE)
					v.strike.draw(freq);
			}
			bool wrapped = v.phase < prevPhase;

			float outSample = 0.f, auxSample = 0.f;
			float outVolts = 0.f;          // the board's chosen tap, when OUT carries one
			bool haveVolts = false;
			switch (core) {
				case sixfigures::CORE_SCHMITT: {
					// The astable itself (Cd40106.hpp): RATE is the pot, the CV the In jack's 1N4448 + 1k
					// (or, with 1 V/oct on, an exponential converter scaling the pot), the range the cap.
					const sixfigures::schmitt::Setting st =
						sixfigures::schmitt::setting(knob, lfoRange, lo, hi, voltPerOct, cv * cvAmt);
					outSample = v.schmitt.process(args.sampleTime, st.r, st.c, vdd, cd40106::CORNER_TYP, st.vin);
					dispHz[i] = (float) v.schmitt.a.frequency();
					v.phase = v.schmitt.a.high ? 0.25f : 0.75f;       // the LED follows the output
					// AUX: the capacitor itself, centred on the hysteresis window and scaled to +-5 V at the thresholds.
					const cd40106::Thresholds th = cd40106::thresholds(vdd);
					auxSample = clamp((float) ((v.schmitt.a.v - 0.5 * (th.vp + th.vn)) / (0.5 * (th.vp - th.vn))), -1.5f, 1.5f) * 5.f;
					break;
				}
				case sixfigures::CORE_AAC: {
					outSample = sixfigures::blepSquare(v.phase, dt);
					float tri = v.tri.process(outSample, freq, args.sampleTime);
					auxSample = clamp(tri, -1.f, 1.f) * 5.f;
					break;
				}
				case sixfigures::CORE_PLL: {
					outSample = sixfigures::blepSquare(v.phase, dt);
					auxSample = pllHasError ? pllErrorBit * 10.f : 0.f;
					break;
				}
				case sixfigures::CORE_AVALANCHE:
				default: {
					if (boardMode) {
						// The board: OUT carries the saw (as ever, +-5 V), or a node of the circuit in the volts it is at.
						// MIX always sums the saw: the nodes sit on DC (N, P2) or are not the module's level (P3).
						outSample = (float) boardOut.saw;
						switch (avalancheTap) {
							case sixfigures::avalanche::board::TAP_TL072: outVolts = (float) boardOut.p3; break;
							case sixfigures::avalanche::board::TAP_NODE:  outVolts = (float) boardOut.p2; break;
							case sixfigures::avalanche::board::TAP_CAP:   outVolts = (float) boardOut.node; break;
							default: break;
						}
						if (avalancheTap != sixfigures::avalanche::board::TAP_SAW)
							haveVolts = true;
						if (v.board.wrapped)
							v.avalanchePulse.trigger(0.001f);
						v.phase = clamp((float) (v.board.tCycle * v.board.hz), 0.f, 1.f);   // the LED follows the cycle
					}
					else {
						outSample = sixfigures::avalancheSaw(v.phase, dt, v.strike.theta, v.strike.warp);
						if (wrapped)
							v.avalanchePulse.trigger(0.001f);
					}
					auxSample = v.avalanchePulse.process(args.sampleTime) ? 10.f : 0.f;
					break;
				}
			}

			outputs[OUT1_OUTPUT + i].setVoltage(haveVolts ? outVolts : outSample * 5.f);
			outputs[AUX1_OUTPUT + i].setVoltage(auxSample);
			mixSum += outSample;

			if (lightUpdate) {
				bool blink = lfoRange && v.phase < 0.5f;
				lights[LED1_LIGHT + i].setBrightnessSmooth(blink ? 1.f : 0.f, lightDt);
			}
		}

		outputs[MIX_OUTPUT].setVoltage(std::tanh(mixSum * 0.5f) * 5.f);

		if (lightUpdate)
			lights[LOCK_LIGHT].setBrightnessSmooth(anyLocked ? 1.f : 0.f, lightDt);
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "voltPerOct", json_boolean(voltPerOct));
		json_object_set_new(root, "signalOnR1", json_boolean(signalOnR1));
		json_object_set_new(root, "vdd", json_real(vdd));
		// the board option is written only when it is on, so a default patch saves what it always did
		if (avalancheBoard)
			json_object_set_new(root, "avalancheBoard", json_boolean(true));
		if (avalancheTap != sixfigures::avalanche::board::TAP_SAW)
			json_object_set_new(root, "avalancheTap", json_integer(avalancheTap));
		if (avalancheLed3)
			json_object_set_new(root, "avalancheLed3", json_boolean(true));
		return root;
	}

	void dataFromJson(json_t* root) override {
		json_t* j = json_object_get(root, "voltPerOct");
		if (j)
			voltPerOct = json_boolean_value(j);
		j = json_object_get(root, "signalOnR1");
		if (j)
			signalOnR1 = json_boolean_value(j);
		j = json_object_get(root, "vdd");
		if (j && json_is_number(j))
			vdd = clamp((float) json_number_value(j), 3.f, 18.f);
		j = json_object_get(root, "avalancheBoard");
		avalancheBoard = j && json_boolean_value(j);
		j = json_object_get(root, "avalancheTap");
		avalancheTap = (j && json_is_integer(j)) ? clamp((int) json_integer_value(j), 0, sixfigures::avalanche::board::NUM_TAPS - 1)
		                                         : sixfigures::avalanche::board::TAP_SAW;
		j = json_object_get(root, "avalancheLed3");
		avalancheLed3 = j && json_boolean_value(j);
	}
};


// ---------------------------------------------------------------------------
// Look and feel. The palette, the shared hardware and the entire silkscreen come
// from src/PanelTheme.hpp, generated by tools/panels/SixFigures.py -- see
// ../../panelkit/README.md. Nothing about the look is written here.

typedef RoundBlackKnob      PanelKnob;    // CORE and CAPTURE
typedef RoundLargeBlackKnob RateKnob;     // RATE, one per voice


/** The read-out: each voice's core by name over the frequency it is running at,
 *  and RANGE at the end. The names and RANGE are fields. */
struct SixFiguresDisplay : LedDisplay {
	SixFigures* module = NULL;

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) {
			LedDisplay::drawLayer(args, layer);
			return;
		}
		NVGcontext* vg = args.vg;
		static const Rect CORE[6] = { panel::FIELD_CORE1, panel::FIELD_CORE2, panel::FIELD_CORE3,
		                             panel::FIELD_CORE4, panel::FIELD_CORE5, panel::FIELD_CORE6 };
		static const char* const SHORT[] = {"40106", "4069", "4046", "AVAL"};
		const panel::TextStyle NAME(panel::Face::Mono, 8.f, panel::MINT, NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE);
		const panel::TextStyle HZ(panel::Face::Mono, 7.f, panel::LIME, NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE);
		for (int i = 0; i < 6; i++) {
			const int core = module ? clamp((int)std::lround(module->params[SixFigures::CORE1_PARAM + i].getValue()), 0, 3) : 0;
			const Rect c = panel::inGlass(CORE[i]);
			const float cx = c.pos.x + c.size.x / 2.f;
			panel::text(vg, NAME, cx, c.pos.y + c.size.y * 0.75f, SHORT[core]);
			const float hz = module ? module->dispHz[i] : 0.f;
			std::string f = !module ? "--" : hz < 1.f ? string::f("%.2fHz", hz)
			              : hz < 1000.f ? string::f("%.0fHz", hz) : string::f("%.2fk", hz / 1000.f);
			panel::text(vg, HZ, cx, c.pos.y + c.size.y * 1.95f, f);
		}
		const bool audio = !module || module->params[SixFigures::RANGE_PARAM].getValue() > 0.5f;
		const Rect r = panel::inGlass(panel::FIELD_RANGE);
		const panel::TextStyle TAG(panel::Face::Mono, 6.f, panel::SAGE, NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE);
		panel::text(vg, TAG, r.pos.x + r.size.x / 2.f, r.pos.y + r.size.y * 0.38f, "RANGE");
		panel::text(vg, NAME.inked(panel::LIME), r.pos.x + r.size.x / 2.f, r.pos.y + r.size.y * 0.78f, audio ? "AUDIO" : "LFO");
	}
};

struct SixFiguresWidget : ModuleWidget {
	SixFiguresWidget(SixFigures* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/SixFigures.svg")));

		panel::addScrews(this);
		panel::addLabels(this);

		// Six identical voice columns. A macro rather than a loop because each
		// column's positions are distinct compile-time constants (CORE3_POS is not
		// CORE1_POS plus an offset) -- see src/SixFigures/Panel.hpp.
		SixFiguresDisplay* display = new SixFiguresDisplay;
		display->module = module;
		display->box.pos = panel::mm(panel::GLASS_X, panel::GLASS_Y);
		display->box.size = panel::mm(panel::GLASS_W, panel::GLASS_H);
		addChild(display);

#define SIXFIGURES_VOICE(N) \
		addParam(panel::createField<panel::ScreenSelect>(panel::FIELD_CORE##N, \
		             module, SixFigures::CORE1_PARAM + (N - 1))); \
		addParam(createParamCentered<RateKnob>( \
		             panel::mm(panel::RATE##N##_POS.x, panel::RATE##N##_POS.y), \
		             module, SixFigures::RATE1_PARAM + (N - 1))); \
		addParam(createParamCentered<Trimpot>( \
		             panel::mm(panel::CV##N##_POS.x, panel::CV##N##_POS.y), \
		             module, SixFigures::CV1_PARAM + (N - 1))); \
		addInput(createInputCentered<panel::PortIn>( \
		             panel::mm(panel::CV##N##_IN_POS.x, panel::CV##N##_IN_POS.y), \
		             module, SixFigures::CV1_IN_INPUT + (N - 1))); \
		addOutput(createOutputCentered<panel::PortOut>( \
		             panel::mm(panel::OUT##N##_POS.x, panel::OUT##N##_POS.y), \
		             module, SixFigures::OUT1_OUTPUT + (N - 1))); \
		addOutput(createOutputCentered<panel::PortOut>( \
		             panel::mm(panel::AUX##N##_POS.x, panel::AUX##N##_POS.y), \
		             module, SixFigures::AUX1_OUTPUT + (N - 1))); \
		addChild(createLightCentered<SmallLight<panel::MintLight> >( \
		             panel::mm(panel::LED##N##_POS.x, panel::LED##N##_POS.y), \
		             module, SixFigures::LED1_LIGHT + (N - 1)));

		SIXFIGURES_VOICE(1)
		SIXFIGURES_VOICE(2)
		SIXFIGURES_VOICE(3)
		SIXFIGURES_VOICE(4)
		SIXFIGURES_VOICE(5)
		SIXFIGURES_VOICE(6)
#undef SIXFIGURES_VOICE

		// The totals column.
		addParam(panel::createField<panel::ScreenSwitch>(panel::FIELD_RANGE, module, SixFigures::RANGE_PARAM));
		addParam(createParamCentered<PanelKnob>(
		             panel::mm(panel::CAPTURE_POS.x, panel::CAPTURE_POS.y), module, SixFigures::CAPTURE_PARAM));
		addChild(createLightCentered<SmallLight<panel::LimeLight> >(
		             panel::mm(panel::LOCK_POS.x, panel::LOCK_POS.y), module, SixFigures::LOCK_LIGHT));
		addInput(createInputCentered<panel::PortIn>(
		             panel::mm(panel::SYNC_POS.x, panel::SYNC_POS.y), module, SixFigures::SYNC_INPUT));
		addParam(createParamCentered<Trimpot>(
		             panel::mm(panel::DRIFT_POS.x, panel::DRIFT_POS.y), module, SixFigures::DRIFT_PARAM));
		addInput(createInputCentered<panel::PortIn>(
		             panel::mm(panel::SIGNAL_POS.x, panel::SIGNAL_POS.y), module, SixFigures::SIGNAL_INPUT));
		addOutput(createOutputCentered<panel::PortOutMain>(
		             panel::mm(panel::MIX_POS.x, panel::MIX_POS.y), module, SixFigures::MIX_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		SixFigures* m = dynamic_cast<SixFigures*>(module);
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Six Figures"));

		menu->addChild(createBoolMenuItem("1 V/oct tracking (all voices)", "",
			[=]() { return m->voltPerOct; },
			[=](bool v) { m->voltPerOct = v; }));
		menu->addChild(createBoolMenuItem("SIGNAL also on the 4046 R1 pin (board's RingMod jack)", "",
			[=]() { return m->signalOnR1; },
			[=](bool v) { m->signalOnR1 = v; }));
		menu->addChild(createBoolMenuItem("Avalanche core: board (R2 pot, C1/C2, vactrol CV, TL072)", "",
			[=]() { return m->avalancheBoard; },
			[=](bool v) { m->avalancheBoard = v; }));
		static const char* const TAPS[] = {"saw (module)", "TL072 x221 amplified (P3)", "raw oscillator node (P2)", "capacitor node (N, before R4)"};
		menu->addChild(createSubmenuItem("Avalanche board: OUT carries", TAPS[clamp(m->avalancheTap, 0, 3)], [=](Menu* sub) {
			for (int t = 0; t < sixfigures::avalanche::board::NUM_TAPS; t++)
				sub->addChild(createCheckMenuItem(TAPS[t], "",
					[=]() { return m->avalancheTap == t; },
					[=]() { m->avalancheTap = t; }));
		}));
		menu->addChild(createBoolMenuItem("Avalanche board: CV LED (LED3) fitted", "",
			[=]() { return m->avalancheLed3; },
			[=](bool v) { m->avalancheLed3 = v; }));
		menu->addChild(createSubmenuItem("40106 supply (VDD)", string::f("%.0f V", m->vdd), [=](Menu* sub) {
			static const float VDDS[] = {5.f, 9.f, 12.f, 15.f};
			for (float vv : VDDS)
				sub->addChild(createCheckMenuItem(string::f("%.0f V", vv), "",
					[=]() { return m->vdd == vv; },
					[=]() { m->vdd = vv; }));
		}));
	}
};


Model* modelSixFigures = createModel<SixFigures, SixFiguresWidget>("SixFigures");

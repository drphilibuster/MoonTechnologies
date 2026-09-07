#include "../plugin.hpp"
#include "Panel.hpp"
#include "Payroll.hpp"

using namespace kickback;


//: RATIO's tooltip should say "/12" or "x7", not "index 7". Built once and
//: handed to configSwitch.
static std::vector<std::string> ratioLabels() {
	std::vector<std::string> v;
	for (int i = 0; i < kRatioCount; i++) {
		float r = kClockRatio[i];
		char b[16];
		if (i == kUnity) snprintf(b, sizeof b, "x1");
		else if (r < 1.f)  snprintf(b, sizeof b, "/%g", (double)(1.f / r));
		else               snprintf(b, sizeof b, "x%g", (double)r);
		v.push_back(b);
	}
	return v;
}


struct Kickback : Module {
	// Nine voices, five controls each, in the order VoiceId has them -- so the
	// process loop is a loop and not nine near-identical paragraphs.
	enum ParamId {
		RUN_PARAM, LEVEL_PARAM, RATE_PARAM, ACCENT_PARAM,
		DIV_PARAM, FILL_PARAM, SWING_PARAM, SEED_PARAM, HUMAN_PARAM, GATELEN_PARAM,
		BURST_PARAM,
		TUNE_PARAM,                                   // + V_COUNT
		DECAY_PARAM = TUNE_PARAM + V_COUNT,           // + V_COUNT
		BEND_PARAM = DECAY_PARAM + V_COUNT,           // + V_COUNT
		COLOUR_PARAM = BEND_PARAM + V_COUNT,          // + V_COUNT
		RATIO_PARAM = COLOUR_PARAM + V_COUNT,         // + V_COUNT
		PARAMS_LEN = RATIO_PARAM + V_COUNT
	};
	enum InputId {
		CLK_INPUT, RST_INPUT, ACC_INPUT,
		TRIG_INPUT,                                   // + V_COUNT
		INPUTS_LEN = TRIG_INPUT + V_COUNT
	};
	enum OutputId {
		CLK_OUTPUT, MIX_OUTPUT,
		GATE_OUTPUT,                                  // + V_COUNT
		VOICE_OUTPUT = GATE_OUTPUT + V_COUNT,         // + V_COUNT
		OUTPUTS_LEN = VOICE_OUTPUT + V_COUNT
	};
	enum LightId {
		RUN_LIGHT,
		VOICE_LIGHT,                                  // + V_COUNT
		LIGHTS_LEN = VOICE_LIGHT + V_COUNT
	};

	Kick kick;
	Snare snare;
	Hat hat;
	Tom tom[3];

	Payroll payroll;

	dsp::SchmittTrigger trig[V_COUNT], clkTrig, rstTrig;
	dsp::ClockDivider lightDivider;

	// One flag per voice, latched between light updates so a strike landing
	// mid-division is never missed the way sampling the trigger only on the
	// divider's own tick would miss it.
	bool litSince[V_COUNT] = {};
	float ledLevel[V_COUNT] = {};

	//: The gate bus: how long each channel has left to run, and at what height.
	float gateLeft[V_COUNT] = {};
	float gateVel[V_COUNT] = {};

	//: Three toms need three noise streams, or their attack layers are the
	//: same sample-for-sample and a tom fill sounds like one drum through a
	//: pitch shifter -- and three sizes, or they are one drum transposed:
	//: the size sets the tuning range, the ring time, how fast the upper
	//: modes die and how hard the beater is.
	Kickback() : tom{Tom(0x70111u, 0), Tom(0x70222u, 1), Tom(0x70333u, 2)} {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

		configSwitch(RUN_PARAM, 0.f, 1.f, 0.f, "Run", {"Stopped", "Running"});
		configParam(LEVEL_PARAM, 0.f, 1.f, 0.7f, "Mix level", "%", 0.f, 100.f);
		configParam(RATE_PARAM, 0.f, 1.f, 0.42f, "Tempo", " BPM", 10.f, 30.f);
		configParam(ACCENT_PARAM, 0.f, 1.f, 0.5f, "Accent CV amount", "%", 0.f, 100.f);

		configSwitch(DIV_PARAM, 0.f, (float)(kDivCount - 1), 3.f, "Clock division",
			{"1/4", "1/8", "1/8 triplet", "1/16", "1/16 triplet", "1/32"});
		configParam(FILL_PARAM, 0.f, 1.f, 0.45f, "Pattern fill", "%", 0.f, 100.f);
		configSwitch(BURST_PARAM, 0.f, 1.f, 0.f, "Ratios drive the steps",
			{"Off -- one hit per step", "Burst -- each step runs at its voice's ratio"});
		configParam(SWING_PARAM, 0.f, 1.f, 0.f, "Swing", "%", 0.f, 100.f);
		configParam(SEED_PARAM, 0.f, 15.f, 0.f, "Pattern seed");
		paramQuantities[SEED_PARAM]->snapEnabled = true;
		configParam(HUMAN_PARAM, 0.f, 1.f, 0.25f, "Humanise", "%", 0.f, 100.f);
		configParam(GATELEN_PARAM, 0.f, 1.f, 0.3f, "Gate length", " ms", 0.f, 1.f);
		paramQuantities[GATELEN_PARAM]->displayMultiplier = 95.f;
		paramQuantities[GATELEN_PARAM]->displayOffset = 5.f;

		static const char* vname[V_COUNT] = {
			"Kick", "Snare", "Hat", "Tom I", "Tom II", "Tom III"
		};
		// TUNE is TONE on the two voices whose "pitch" is a filter corner, and
		// the tooltip should say what the silkscreen says.
		static const char* tuneName[V_COUNT] = {
			"tune", "tune", "tone", "tune", "tune", "tune"
		};
		static const char* colourName[V_COUNT] = {
			"model", "mode", "rattle", "strike", "strike", "strike"
		};
		// Each tom is tuned a fourth or so apart out of the box, so three toms
		// are a set of three drums rather than three copies of one.
		// The three toms sit a fourth or so apart *within their own ranges*,
		// which already differ, so the kit is spread out of the box.
		static const float tuneDefault[V_COUNT] = {
			0.40f, 0.45f, 0.60f, 0.42f, 0.40f, 0.38f
		};
		static const float decayDefault[V_COUNT] = {
			0.50f, 0.45f, 0.26f, 0.55f, 0.50f, 0.45f
		};
		static const float bendDefault[V_COUNT] = {
			0.60f, 0.40f, 0.30f, 0.55f, 0.55f, 0.55f
		};
		static const float colourDefault[V_COUNT] = {
			0.00f, 0.00f, 0.25f, 0.30f, 0.42f, 0.55f
		};

		for (int v = 0; v < V_COUNT; v++) {
			std::string n = vname[v];
			configParam(TUNE_PARAM + v, 0.f, 1.f, tuneDefault[v], n + " " + tuneName[v]);
			configParam(DECAY_PARAM + v, 0.f, 1.f, decayDefault[v], n + " decay");
			configParam(BEND_PARAM + v, 0.f, 1.f, bendDefault[v], n + " bend",
			            "%", 0.f, 100.f);
			configInput(TRIG_INPUT + v, n + " trigger");
			// RATIO reads out as the ratio itself -- "/12", "x7" -- rather than
			// as an index, because an index is not a thing anybody wants to see.
			configSwitch(RATIO_PARAM + v, 0.f, (float)(kRatioCount - 1), (float)kUnity,
			             n + " clock ratio", ratioLabels());
			configOutput(VOICE_OUTPUT + v, n);
			configOutput(GATE_OUTPUT + v, n + " gate");
			configLight(VOICE_LIGHT + v, n);
		}

		// The two voices whose fourth control selects a model rather than
		// shaping one get configured as switches, so the tooltip names the
		// model and the knob snaps to its detents.
		configSwitch(COLOUR_PARAM + V_KICK, 0.f, 1.f, 0.f, "Kick model",
			{"Bridge (BaSnaHi)", "Smurf (astable)"});
		configSwitch(COLOUR_PARAM + V_SNARE, 0.f, 2.f, 0.f, "Snare mode",
			{"XOR (XORbell)", "Vactrol (noise voice)", "Dazzle (Karplus-Strong)"});
		for (int v = 0; v < V_COUNT; v++) {
			if (v == V_KICK || v == V_SNARE) continue;
			configParam(COLOUR_PARAM + v, 0.f, 1.f, colourDefault[v],
			            std::string(vname[v]) + " " + colourName[v], "%", 0.f, 100.f);
		}

		configInput(CLK_INPUT, "Clock");
		configInput(RST_INPUT, "Reset");
		configInput(ACC_INPUT, "Accent CV");
		configOutput(CLK_OUTPUT, "Clock");
		configOutput(MIX_OUTPUT, "Mix");
		configLight(RUN_LIGHT, "Running");

		lightDivider.setDivision(32);
		onSampleRateChange({APP->engine->getSampleRate(), 0.f});
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		float fs = e.sampleRate;
		kick.reset();  kick.setRate(fs);
		snare.reset(); snare.setRate(fs);
		hat.reset();   hat.setRate(fs);
		for (int i = 0; i < 3; i++) { tom[i].reset(); tom[i].setRate(fs); }
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		kick.reset(); snare.reset(); hat.reset();
		for (int i = 0; i < 3; i++) tom[i].reset();
		payroll.reset();
		for (int v = 0; v < V_COUNT; v++) {
			litSince[v] = false; ledLevel[v] = 0.f;
			gateLeft[v] = 0.f; gateVel[v] = 0.f;
		}
	}

	//: The transport itself is a param (the latching RUN bezel), which Rack
	//: saves for us. Only where the grid had got to needs saving by hand, so a
	//: patch reopens mid-bar rather than snapping to the top of it.
	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "step", json_integer(payroll.step));
		return root;
	}
	void dataFromJson(json_t* root) override {
		json_t* j = json_object_get(root, "step");
		if (j) payroll.step = clamp((int)json_integer_value(j), 0, kSteps - 1);
	}

	void process(const ProcessArgs& args) override {
		// --- the payroll -----------------------------------------------------
		// RUN is a latching lit bezel, so the param itself is the transport
		// state and survives a patch save without any mirror of our own.
		payroll.running = params[RUN_PARAM].getValue() > 0.5f;

		// FILL at its bottom stop is grid mode: no patterns, every voice on
		// every step, each at its own RATIO. It goes here rather than on a
		// switch of its own because the panel has no slot for one -- and
		// because FILL at zero used to mean "every pattern is empty", which is
		// a setting nobody wants and the right place for the mode that
		// replaces it.
		float fill = params[FILL_PARAM].getValue();
		payroll.gridMode = (fill <= 0.001f);
		// BURST is what makes the top of the ratio range worth reaching. On its
		// own a fast ratio is only useful at FILL 0, where the six voices become
		// six drones; handed the pattern to run against, it is a ratchet on the
		// steps that voice already plays. Grid mode wins if both are asked for
		// -- there is no pattern left to burst against down there.
		payroll.burstMode = !payroll.gridMode
		                    && params[BURST_PARAM].getValue() > 0.5f;
		for (int v = 0; v < V_COUNT; v++)
			payroll.ratioIndex[v] = (int)clamp(std::round(params[RATIO_PARAM + v].getValue()),
			                                   0.f, (float)(kRatioCount - 1));
		payroll.build(fill, (int)std::round(params[SEED_PARAM].getValue()),
		              params[HUMAN_PARAM].getValue());

		// 30 to 300 BPM, log-spaced, so the useful half of the range is the
		// useful half of the knob.
		float bpm = expMap(params[RATE_PARAM].getValue(), 30.f, 300.f);
		int div = (int)clamp(std::round(params[DIV_PARAM].getValue()), 0.f, (float)(kDivCount - 1));

		bool extConnected = inputs[CLK_INPUT].isConnected();
		bool extEdge = clkTrig.process(inputs[CLK_INPUT].getVoltage(), 0.1f, 1.f);
		bool rstEdge = rstTrig.process(inputs[RST_INPUT].getVoltage(), 0.1f, 1.f);

		// The normalling rule: a voice with something in its TRIG is played by
		// that and nothing else; a voice with an empty TRIG is played by the
		// engine. Nothing has to be switched to move between the two.
		bool patched[V_COUNT];
		for (int v = 0; v < V_COUNT; v++) patched[v] = inputs[TRIG_INPUT + v].isConnected();

		payroll.process(args.sampleTime, extConnected, extEdge, rstEdge,
		                bpm, div, params[SWING_PARAM].getValue(), patched);

		// --- who is struck this sample ---------------------------------------
		// ACCENT: the knob is how much the CV can raise a strike above unity,
		// not a level control by itself -- with nothing patched every external
		// strike lands at its full, unaccented velocity.
		float accCv = inputs[ACC_INPUT].isConnected() ? inputs[ACC_INPUT].getVoltage() : 0.f;
		float accent = clamp(1.f + accCv / 10.f * params[ACCENT_PARAM].getValue(), 0.f, 2.f);

		bool hit[V_COUNT];
		float vel[V_COUNT];
		for (int v = 0; v < V_COUNT; v++) {
			if (patched[v]) {
				hit[v] = trig[v].process(inputs[TRIG_INPUT + v].getVoltage(), 0.1f, 1.f);
				vel[v] = accent;
			}
			else {
				hit[v] = payroll.fired[v];
				// The engine's own per-step velocity, then the accent CV on top.
				vel[v] = clamp(payroll.vel[v] * accent, 0.f, 2.f);
			}
			if (hit[v]) litSince[v] = true;
		}

		// --- the voices -------------------------------------------------------
		float out[V_COUNT];
		#define KNOB(base, v) params[base##_PARAM + (v)].getValue()
		// V/oct left this panel when the RATIO row took its row; the voices
		// still take a volts argument, so pass them nothing.
		#define VOCT(v) (0.f)

		out[V_KICK] = kick.process(hit[V_KICK], vel[V_KICK],
			KNOB(COLOUR, V_KICK) > 0.5f ? 1 : 0,
			KNOB(TUNE, V_KICK), VOCT(V_KICK), KNOB(DECAY, V_KICK), KNOB(BEND, V_KICK),
			// The model switch has taken the colour knob, so DRIVE follows BEND:
			// a kick wound up for a long dive wants the stage pushed harder.
			0.25f + 0.5f * KNOB(BEND, V_KICK));

		out[V_SNARE] = snare.process(hit[V_SNARE], vel[V_SNARE],
			(int)clamp(std::round(KNOB(COLOUR, V_SNARE)), 0.f, 2.f),
			KNOB(TUNE, V_SNARE), VOCT(V_SNARE), KNOB(DECAY, V_SNARE),
			KNOB(BEND, V_SNARE));

		out[V_HAT] = hat.process(hit[V_HAT], vel[V_HAT],
			KNOB(TUNE, V_HAT), VOCT(V_HAT), KNOB(DECAY, V_HAT),
			KNOB(BEND, V_HAT), KNOB(COLOUR, V_HAT));

		for (int i = 0; i < 3; i++) {
			int v = V_TOM1 + i;
			out[v] = tom[i].process(hit[v], vel[v], KNOB(TUNE, v), VOCT(v),
			                        KNOB(DECAY, v), KNOB(BEND, v), KNOB(COLOUR, v));
		}

		#undef KNOB
		#undef VOCT

		float mix = 0.f;
		for (int v = 0; v < V_COUNT; v++) {
			outputs[VOICE_OUTPUT + v].setVoltage(clamp(out[v], -12.f, 12.f));
			mix += out[v];
		}
		// 0.95, not 0.34. Six drum voices rarely peak together -- their
		// transients land on different samples -- so summing them and dividing
		// by six is not the right sum at all, it is the sum for six sine waves
		// in phase. At 0.34 the MIX came out ten decibels under a single
		// voice's own OUT, which is the wrong way round for the jack you are
		// meant to be able to use on its own: patching MIX should not be
		// quieter than patching the kick.
		//
		// This puts a full kit within a couple of decibels of its loudest
		// single voice at the default LEVEL, with the knob's top end above it.
		// Six full-velocity strikes landing on the same sample still clip --
		// they would at any setting worth using -- and the +-12 V clamp is what
		// catches that.
		mix *= params[LEVEL_PARAM].getValue() * 0.95f;
		outputs[MIX_OUTPUT].setVoltage(clamp(mix, -12.f, 12.f));
		outputs[CLK_OUTPUT].setVoltage(payroll.clockHigh() ? 10.f : 0.f);

		// --- the gates -------------------------------------------------------
		// One jack per voice, not one polyphonic bus. A poly bus would need a
		// split module to be useful, and a drum machine that cannot drive six
		// external voices without one is not self-contained -- polyphony is for
		// things that are tonal, where the channels are notes of one voice
		// rather than six separate voices.
		//
		// Each gate is held high for GATE and scaled by that hit's velocity, so
		// the accent travels with the trigger instead of on a second cable. A
		// ghost note still clears a 1 V trigger threshold.
		float gateSec = 0.005f + 0.095f * params[GATELEN_PARAM].getValue();
		for (int v = 0; v < V_COUNT; v++) {
			if (hit[v]) { gateLeft[v] = gateSec; gateVel[v] = vel[v]; }
			float g = 0.f;
			if (gateLeft[v] > 0.f) {
				gateLeft[v] -= args.sampleTime;
				g = clamp(gateVel[v], 0.f, 1.f) * 10.f;
			}
			outputs[GATE_OUTPUT + v].setVoltage(g);
		}

		if (lightDivider.process()) {
			float lightDt = args.sampleTime * lightDivider.getDivision();
			float lambda = std::exp(-lightDt / 0.12f);   // ~120 ms, independent of DECAY
			for (int v = 0; v < V_COUNT; v++) {
				ledLevel[v] = litSince[v] ? 1.f : ledLevel[v] * lambda;
				litSince[v] = false;
				lights[VOICE_LIGHT + v].setBrightness(ledLevel[v]);
			}
			lights[RUN_LIGHT].setBrightness(payroll.running || extConnected ? 1.f : 0.f);
		}
	}
};


// ---------------------------------------------------------------------------
// Look and feel comes entirely from src/PanelTheme.hpp and src/Kickback/Panel.hpp,
// generated by tools/panels/Kickback.py -- see ../../panelkit/README.md. Nothing
// about the look is written here.

typedef RoundLargeBlackKnob VoiceKnob;
typedef RoundBlackKnob      MidKnob;
typedef Trimpot             SmallKnob;


struct KickbackWidget : ModuleWidget {
	KickbackWidget(Kickback* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Kickback.svg")));

		panel::addScrews(this);
		panel::addLabels(this);

		// The six voice columns, in the order VoiceId has them, so a column is
		// a loop iteration rather than six copied lines.
		//
		// Two port arts, on two axes: the throat band says which way a jack
		// goes, and a thin lime line inside it says the jack carries timing
		// rather than a level. So the TRIGs, the gates, CLK both ways and RST
		// wear the line; the audio OUTs, MIX and the ACC input do not.
		static const Vec* trigPos[V_COUNT] = {
			&panel::KICK_TRIG_POS, &panel::SNARE_TRIG_POS, &panel::HAT_TRIG_POS,
			&panel::TOM1_TRIG_POS, &panel::TOM2_TRIG_POS, &panel::TOM3_TRIG_POS };
		static const Vec* ledPos[V_COUNT] = {
			&panel::KICK_LED_POS, &panel::SNARE_LED_POS, &panel::HAT_LED_POS,
			&panel::TOM1_LED_POS, &panel::TOM2_LED_POS, &panel::TOM3_LED_POS };
		static const Vec* tunePos[V_COUNT] = {
			&panel::KICK_TUNE_POS, &panel::SNARE_TUNE_POS, &panel::HAT_TUNE_POS,
			&panel::TOM1_TUNE_POS, &panel::TOM2_TUNE_POS, &panel::TOM3_TUNE_POS };
		static const Vec* ratioPos[V_COUNT] = {
			&panel::KICK_RATIO_POS, &panel::SNARE_RATIO_POS, &panel::HAT_RATIO_POS,
			&panel::TOM1_RATIO_POS, &panel::TOM2_RATIO_POS, &panel::TOM3_RATIO_POS };
		static const Vec* decayPos[V_COUNT] = {
			&panel::KICK_DECAY_POS, &panel::SNARE_DECAY_POS, &panel::HAT_DECAY_POS,
			&panel::TOM1_DECAY_POS, &panel::TOM2_DECAY_POS, &panel::TOM3_DECAY_POS };
		static const Vec* bendPos[V_COUNT] = {
			&panel::KICK_BEND_POS, &panel::SNARE_BEND_POS, &panel::HAT_BEND_POS,
			&panel::TOM1_BEND_POS, &panel::TOM2_BEND_POS, &panel::TOM3_BEND_POS };
		static const Vec* colourPos[V_COUNT] = {
			&panel::KICK_MODEL_POS, &panel::SNARE_MODE_POS, &panel::HAT_RATTLE_POS,
			&panel::TOM1_STRIKE_POS, &panel::TOM2_STRIKE_POS, &panel::TOM3_STRIKE_POS };
		static const Vec* gatePos[V_COUNT] = {
			&panel::KICK_GATE_POS, &panel::SNARE_GATE_POS, &panel::HAT_GATE_POS,
			&panel::TOM1_GATE_POS, &panel::TOM2_GATE_POS, &panel::TOM3_GATE_POS };
		static const Vec* outPos[V_COUNT] = {
			&panel::KICK_OUT_POS, &panel::SNARE_OUT_POS, &panel::HAT_OUT_POS,
			&panel::TOM1_OUT_POS, &panel::TOM2_OUT_POS, &panel::TOM3_OUT_POS };

		for (int v = 0; v < V_COUNT; v++) {
			addInput(createInputCentered<panel::PortTrigIn>(
				panel::mm(trigPos[v]->x, trigPos[v]->y), module, Kickback::TRIG_INPUT + v));
			addChild(createLightCentered<SmallLight<panel::PaperLight> >(
				panel::mm(ledPos[v]->x, ledPos[v]->y), module, Kickback::VOICE_LIGHT + v));
			addParam(createParamCentered<VoiceKnob>(
				panel::mm(tunePos[v]->x, tunePos[v]->y), module, Kickback::TUNE_PARAM + v));
			addParam(createParamCentered<SmallKnob>(
				panel::mm(ratioPos[v]->x, ratioPos[v]->y), module, Kickback::RATIO_PARAM + v));
			addParam(createParamCentered<SmallKnob>(
				panel::mm(decayPos[v]->x, decayPos[v]->y), module, Kickback::DECAY_PARAM + v));
			addParam(createParamCentered<SmallKnob>(
				panel::mm(bendPos[v]->x, bendPos[v]->y), module, Kickback::BEND_PARAM + v));
			// KICK and SNARE select a model here where the others shape one,
			// and they get real toggles for it. A detented trim was smaller,
			// but you cannot see where a trim is standing from across the room
			// and you cannot flick it -- and a control whose whole job is to
			// say which of two or three circuits is running should look like
			// the switch it is.
			if (v == V_KICK) {
				addParam(createParamCentered<CKSS>(
					panel::mm(colourPos[v]->x, colourPos[v]->y), module, Kickback::COLOUR_PARAM + v));
			}
			else if (v == V_SNARE) {
				addParam(createParamCentered<CKSSThree>(
					panel::mm(colourPos[v]->x, colourPos[v]->y), module, Kickback::COLOUR_PARAM + v));
			}
			else {
				addParam(createParamCentered<SmallKnob>(
					panel::mm(colourPos[v]->x, colourPos[v]->y), module, Kickback::COLOUR_PARAM + v));
			}
			addOutput(createOutputCentered<panel::PortOut>(
				panel::mm(outPos[v]->x, outPos[v]->y), module, Kickback::VOICE_OUTPUT + v));
			addOutput(createOutputCentered<panel::PortTrigOut>(
				panel::mm(gatePos[v]->x, gatePos[v]->y), module, Kickback::GATE_OUTPUT + v));
		}

		// BURST sits in the gutter between the ratio knobs and the gate column,
		// which is the only part of that gap the panel was not already using.
		addParam(createParamCentered<CKSS>(
			panel::mm(panel::BURST_POS.x, panel::BURST_POS.y), module, Kickback::BURST_PARAM));

		// --- the payroll strip -------------------------------------------------
		addParam(createLightParamCentered<VCVLightBezelLatch<panel::LimeLight> >(
			panel::mm(panel::RUN_POS.x, panel::RUN_POS.y), module,
			Kickback::RUN_PARAM, Kickback::RUN_LIGHT));
		addParam(createParamCentered<SmallKnob>(panel::mm(panel::LEVEL_POS.x, panel::LEVEL_POS.y), module, Kickback::LEVEL_PARAM));
		addParam(createParamCentered<VoiceKnob>(panel::mm(panel::RATE_POS.x, panel::RATE_POS.y), module, Kickback::RATE_PARAM));
		addParam(createParamCentered<MidKnob>(panel::mm(panel::ACCENT_POS.x, panel::ACCENT_POS.y), module, Kickback::ACCENT_PARAM));
		addInput(createInputCentered<panel::PortTrigIn>(panel::mm(panel::CLK_IN_POS.x, panel::CLK_IN_POS.y), module, Kickback::CLK_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::ACC_IN_POS.x, panel::ACC_IN_POS.y), module, Kickback::ACC_INPUT));
		addParam(createParamCentered<SmallKnob>(panel::mm(panel::DIV_POS.x, panel::DIV_POS.y), module, Kickback::DIV_PARAM));
		addParam(createParamCentered<SmallKnob>(panel::mm(panel::FILL_POS.x, panel::FILL_POS.y), module, Kickback::FILL_PARAM));
		addParam(createParamCentered<SmallKnob>(panel::mm(panel::SWING_POS.x, panel::SWING_POS.y), module, Kickback::SWING_PARAM));
		addParam(createParamCentered<SmallKnob>(panel::mm(panel::SEED_POS.x, panel::SEED_POS.y), module, Kickback::SEED_PARAM));
		addParam(createParamCentered<SmallKnob>(panel::mm(panel::HUMAN_POS.x, panel::HUMAN_POS.y), module, Kickback::HUMAN_PARAM));
		addParam(createParamCentered<SmallKnob>(panel::mm(panel::GATELEN_POS.x, panel::GATELEN_POS.y), module, Kickback::GATELEN_PARAM));
		addInput(createInputCentered<panel::PortTrigIn>(panel::mm(panel::RST_IN_POS.x, panel::RST_IN_POS.y), module, Kickback::RST_INPUT));
		addOutput(createOutputCentered<panel::PortTrigOut>(panel::mm(panel::CLK_OUT_POS.x, panel::CLK_OUT_POS.y), module, Kickback::CLK_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::MIX_OUT_POS.x, panel::MIX_OUT_POS.y), module, Kickback::MIX_OUTPUT));

	}
};


Model* modelKickback = createModel<Kickback, KickbackWidget>("Kickback");

#include "../plugin.hpp"
#include "Panel.hpp"

#include <cmath>


// ---------------------------------------------------------------------------
// Garnishment -- a dual VCA / low-pass gate. Each of the two identical channels
// is, per its MODE switch, one of three Modular-in-a-Week circuits:
//
//   OTA      Simple 13700 Dual VCA (Kristian Blasol): an LM13700 whose control
//            current sets its transconductance -- a clean linear VCA.
//   VACTROL  Vactrol VCA (Kristian Blasol): an LED driving a photoresistor in
//            series with the audio -- the classic slow, characterful LPG cell.
//   JFET AM  "I AM O" (Quincas): a 2N5457 used as a voltage-controlled resistor,
//            dividing a carrier against its own channel resistance -- a crude
//            two-signal multiplier riding the JFET's square-law asymmetry.
//
// One physical control set is reused for all three rather than the panel
// switching what it shows: BIAS and CV IN (through CV AMOUNT) form the control
// voltage that opens the OTA, drives the vactrol's LED, or sets the JFET's gate
// bias; LAG is the vactrol's own asymmetric attack/decay, applied to all three
// as a shared "how fast does this respond" knob. Channel 2's CV normals from
// channel 1's, so one CV cable can drive both.
// ---------------------------------------------------------------------------

enum GarnishmentMode { MODE_OTA = 0, MODE_VACTROL = 1, MODE_JFET_AM = 2 };

static const int MAX_POLY = 16;

/** Asymmetric exponential slew -- separate time constants rising and falling.
    This is the vactrol's own fast-attack/slow-decay behaviour (~2 ms up, 50-200
    ms down), reused as the shared LAG control for the other two circuits. */
static inline float slewTo(float prev, float target, float attackTau, float decayTau, float dt) {
	float tau = (target > prev) ? attackTau : decayTau;
	float coef = 1.f - std::exp(-dt / std::max(tau, 1e-6f));
	return prev + (target - prev) * coef;
}

/** One-pole TPT lowpass. Used only in the vactrol's LPG mode, where the filter
    closes along with the gain -- a low-pass gate rolls off treble as it darkens,
    which a plain multiply-by-gain VCA does not. */
struct OnePoleLP {
	float state = 0.f;
	float process(float x, float cutoffHz, float sampleTime) {
		float g = std::tan((float) M_PI * cutoffHz * sampleTime);
		float G = g / (1.f + g);
		float v = (x - state) * G;
		float y = v + state;
		state = y + v;
		return y;
	}
	void reset() { state = 0.f; }
};

/** DC blocker for the JFET path's output cap (C1 in the schematic): the drain
    divider sits on a DC operating point that must not reach the output. */
struct OnePoleHP {
	float x1 = 0.f, y1 = 0.f;
	float process(float x, float r) {
		float y = x - x1 + r * y1;
		x1 = x;
		y1 = y;
		return y;
	}
	void reset() { x1 = y1 = 0.f; }
};

/** Everything one VCA channel needs to remember between samples, per
    polyphonic voice. Two of these live in the module, one per channel. */
struct VcaBus {
	float ctrl[MAX_POLY] = {};   // slewed control voltage, 0..1 -- all three modes
	OnePoleLP lpg[MAX_POLY];
	OnePoleHP dcBlock[MAX_POLY];

	void reset() {
		for (int i = 0; i < MAX_POLY; i++) {
			ctrl[i] = 0.f;
			lpg[i].reset();
			dcBlock[i].reset();
		}
	}
};


struct Garnishment : Module {
	//: Channels. Six rather than the two the original pair of boards had: the
	//: three circuits are the same either way, and one channel per column
	//: instead of one per section is what made room. Six because the rest of
	//: the family runs on six -- SixFigures' oscillators, Kickback's drum
	//: voices, Collusion's LFOs -- so this bank serves any of them one to one.
	static const int N = 6;

	enum ParamId {
		// Channel c occupies four consecutive slots, which is the layout the
		// dual version already had: channels 1 and 2 keep their indices exactly,
		// so a patch saved against that version restores its settings.
		BIAS_PARAM, MODE_PARAM, LAG_PARAM, CVAMT_PARAM,
		PARAMS_LEN = 4 * N
	};
	enum InputId {
		// Same reasoning, and it is why the new channels' jacks are appended
		// rather than interleaved: interleaving would have moved IN 1.
		CVIN1_INPUT, CVIN2_INPUT, IN1_INPUT, IN2_INPUT,
		CVIN_MORE_INPUT,
		IN_MORE_INPUT = CVIN_MORE_INPUT + (N - 2),
		INPUTS_LEN = IN_MORE_INPUT + (N - 2)
	};
	enum OutputId {
		OUT_OUTPUT,                     // channel 1; the rest follow in order
		OUTPUTS_LEN = N
	};
	enum LightId { LIGHTS_LEN };

	/** Ids for channel `c`, zero-based. */
	static int pid(int c, int which) { return 4 * c + which; }
	static int cvInId(int c) { return c < 2 ? CVIN1_INPUT + c : CVIN_MORE_INPUT + c - 2; }
	static int audioInId(int c) { return c < 2 ? IN1_INPUT + c : IN_MORE_INPUT + c - 2; }
	static int outId(int c) { return OUT_OUTPUT + c; }

	VcaBus bus[N];

	// Non-parameter options: neither circuit had a front-panel switch for
	// these in the original, so they live in the menu rather than crowding a
	// 10 HP panel that already repeats twice.
	bool expCv = false;     // OTA: bend the (linear, in the original) CV response
	bool lpgMode = false;   // VACTROL: close a one-pole LPF along with the gain

	Garnishment() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		for (int c = 0; c < N; c++)
			configChannel(c);
	}

	void configChannel(int c) {
		int ch     = c + 1;
		int biasP  = pid(c, BIAS_PARAM);
		int modeP  = pid(c, MODE_PARAM);
		int lagP   = pid(c, LAG_PARAM);
		int cvAmtP = pid(c, CVAMT_PARAM);
		int cvI    = cvInId(c);
		int inI    = audioInId(c);
		int outO   = outId(c);

		configParam(biasP, 0.f, 10.f, 0.f, string::f("Channel %d bias", ch), " V");
		configSwitch(modeP, 0.f, 2.f, 0.f, string::f("Channel %d mode", ch),
		             {"OTA", "Vactrol", "JFET AM"});
		configParam(lagP, 0.f, 1.f, 0.f, string::f("Channel %d lag", ch), "%", 0.f, 100.f);
		configParam(cvAmtP, -1.f, 1.f, 1.f, string::f("Channel %d CV amount", ch), "%", 0.f, 100.f);

		configInput(cvI, string::f("Channel %d CV", ch));
		configInput(inI, string::f("Channel %d audio", ch));
		configOutput(outO, string::f("Channel %d audio", ch));
		configBypass(inI, outO);
	}

	/** One channel, one sample block. `cvFallback` is channel 1's own CV port,
	    passed only for channel 2 -- the normalling. */
	void processBus(const ProcessArgs& args, VcaBus& bus,
	                 int inId, int cvId, int outId,
	                 int biasId, int modeId, int lagId, int cvAmtId,
	                 Input* cvFallback) {
		Input& inPort = inputs[inId];
		Input& cvPort = (cvFallback && !inputs[cvId].isConnected()) ? *cvFallback : inputs[cvId];

		int channels = std::max(1, inPort.getChannels());
		outputs[outId].setChannels(channels);

		float bias = params[biasId].getValue();
		float cvAmt = params[cvAmtId].getValue();
		float lagKnob = params[lagId].getValue();
		int mode = (int) std::lround(params[modeId].getValue());

		const float attackTau = 0.002f;                 // ~2 ms, fixed
		const float decayTau = 0.05f + lagKnob * 0.15f;  // 50..200 ms

		for (int c = 0; c < channels; c++) {
			float audioIn = clamp(inPort.getPolyVoltage(c), -12.f, 12.f);
			float cv = cvPort.isConnected() ? cvPort.getPolyVoltage(c) : 0.f;

			// Unipolar 0-10 V CV convention: bias plus an attenuverted CV,
			// normalized to the 0..1 the three circuits share as "how open".
			float target = clamp((bias + cvAmt * cv) / 10.f, 0.f, 1.f);
			bus.ctrl[c] = slewTo(bus.ctrl[c], target, attackTau, decayTau, args.sampleTime);
			float ctrl = bus.ctrl[c];

			float out = 0.f;
			switch (mode) {
				case MODE_OTA: {
					// Linear gain, soft-clipped by a fixed-drive tanh: the
					// unlinearized LM13700 diff pair saturating as either the
					// signal or the control level rises. Unity small-signal
					// gain at ctrl = 1 falls out of the 5/1 scaling.
					float g = expCv ? ctrl * ctrl : ctrl;
					out = 5.f * std::tanh((audioIn / 5.f) * g);
					break;
				}
				case MODE_VACTROL: {
					// LED brightness (= ctrl) sets the LDR's resistance on a
					// log curve between dark and lit, dividing against an
					// assumed ~100 kOhm downstream input impedance -- R12 in
					// the schematic has no fixed partner, so whatever it
					// feeds supplies the other half of the divider.
					const float RLDR_DARK = 4.7e6f, RLDR_BRIGHT = 150.f, RLOAD = 100e3f;
					float rldr = RLDR_DARK * std::pow(RLDR_BRIGHT / RLDR_DARK, ctrl);
					float g = RLOAD / (RLOAD + rldr);
					float sig = audioIn;
					if (lpgMode) {
						float cutoff = 20.f + g * g * 15000.f;
						sig = bus.lpg[c].process(sig, cutoff, args.sampleTime);
					}
					out = sig * g;
					break;
				}
				case MODE_JFET_AM: {
					// The schematic's "In" pin (our BIAS/CV) sets the JFET's
					// channel resistance; "AM" (our audio IN) is divided
					// against it through R2. Id ~ (1 - Vgs/Vp)^2 gives the
					// divider its square-law asymmetry. A passive divider
					// never reaches unity, hence the x2 makeup.
					float g = ctrl * ctrl;
					float divGain = g / (g + 1.f);
					float raw = audioIn * divGain * 2.f;
					float r = std::exp(-2.f * (float) M_PI * 20.f * args.sampleTime);
					out = bus.dcBlock[c].process(raw, r);
					break;
				}
			}
			outputs[outId].setVoltage(clamp(out, -12.f, 12.f), c);
		}
	}

	void process(const ProcessArgs& args) override {
		// CV normals down the bank: channel 2 falls back to channel 1's port,
		// 3 to 2's, and so on. The dual did this between its two channels; a
		// chain is the same idea and means one CV can open all six.
		for (int c = 0; c < N; c++)
			processBus(args, bus[c], audioInId(c), cvInId(c), outId(c),
			           pid(c, BIAS_PARAM), pid(c, MODE_PARAM),
			           pid(c, LAG_PARAM), pid(c, CVAMT_PARAM),
			           c == 0 ? nullptr : &inputs[cvInId(c - 1)]);
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		for (int c = 0; c < N; c++) bus[c].reset();
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		for (int c = 0; c < N; c++) bus[c].reset();
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "expCv", json_boolean(expCv));
		json_object_set_new(root, "lpgMode", json_boolean(lpgMode));
		return root;
	}

	void dataFromJson(json_t* root) override {
		json_t* j;
		j = json_object_get(root, "expCv");
		if (j) expCv = json_boolean_value(j);
		j = json_object_get(root, "lpgMode");
		if (j) lpgMode = json_boolean_value(j);
	}
};


// ---------------------------------------------------------------------------
// Look and feel comes entirely from src/PanelTheme.hpp and src/Garnishment/Panel.hpp,
// generated by tools/panels/Garnishment.py -- see ../../panelkit/README.md.

typedef RoundBlackKnob GarnishmentKnob;


struct GarnishmentWidget : ModuleWidget {
	GarnishmentWidget(Garnishment* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Garnishment.svg")));

		panel::addScrews(this);
		panel::addLabels(this);

		static const Vec* biasPos[Garnishment::N] = {
			&panel::BIAS1_POS, &panel::BIAS2_POS, &panel::BIAS3_POS,
			&panel::BIAS4_POS, &panel::BIAS5_POS, &panel::BIAS6_POS };
		static const Vec* modePos[Garnishment::N] = {
			&panel::MODE1_POS, &panel::MODE2_POS, &panel::MODE3_POS,
			&panel::MODE4_POS, &panel::MODE5_POS, &panel::MODE6_POS };
		static const Vec* lagPos[Garnishment::N] = {
			&panel::LAG1_POS, &panel::LAG2_POS, &panel::LAG3_POS,
			&panel::LAG4_POS, &panel::LAG5_POS, &panel::LAG6_POS };
		static const Vec* cvAmtPos[Garnishment::N] = {
			&panel::CVAMT1_POS, &panel::CVAMT2_POS, &panel::CVAMT3_POS,
			&panel::CVAMT4_POS, &panel::CVAMT5_POS, &panel::CVAMT6_POS };
		static const Vec* cvInPos[Garnishment::N] = {
			&panel::CVIN1_POS, &panel::CVIN2_POS, &panel::CVIN3_POS,
			&panel::CVIN4_POS, &panel::CVIN5_POS, &panel::CVIN6_POS };
		static const Vec* inPos[Garnishment::N] = {
			&panel::IN1_POS, &panel::IN2_POS, &panel::IN3_POS,
			&panel::IN4_POS, &panel::IN5_POS, &panel::IN6_POS };
		static const Vec* outPos[Garnishment::N] = {
			&panel::OUT1_POS, &panel::OUT2_POS, &panel::OUT3_POS,
			&panel::OUT4_POS, &panel::OUT5_POS, &panel::OUT6_POS };

		for (int c = 0; c < Garnishment::N; c++) {
			addParam(createParamCentered<GarnishmentKnob>(
				panel::mm(biasPos[c]->x, biasPos[c]->y), module,
				Garnishment::pid(c, Garnishment::BIAS_PARAM)));
			addParam(createParamCentered<CKSSThree>(
				panel::mm(modePos[c]->x, modePos[c]->y), module,
				Garnishment::pid(c, Garnishment::MODE_PARAM)));
			addParam(createParamCentered<GarnishmentKnob>(
				panel::mm(lagPos[c]->x, lagPos[c]->y), module,
				Garnishment::pid(c, Garnishment::LAG_PARAM)));
			addParam(createParamCentered<Trimpot>(
				panel::mm(cvAmtPos[c]->x, cvAmtPos[c]->y), module,
				Garnishment::pid(c, Garnishment::CVAMT_PARAM)));
			addInput(createInputCentered<panel::PortIn>(
				panel::mm(cvInPos[c]->x, cvInPos[c]->y), module, Garnishment::cvInId(c)));
			addInput(createInputCentered<panel::PortInMain>(
				panel::mm(inPos[c]->x, inPos[c]->y), module, Garnishment::audioInId(c)));
			addOutput(createOutputCentered<panel::PortOutMain>(
				panel::mm(outPos[c]->x, outPos[c]->y), module, Garnishment::outId(c)));
		}
	}

	void appendContextMenu(Menu* menu) override {
		Garnishment* m = dynamic_cast<Garnishment*>(module);
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Garnishment"));

		menu->addChild(createBoolMenuItem("OTA: exponential CV response", "",
			[=]() { return m->expCv; },
			[=](bool v) { m->expCv = v; }));

		menu->addChild(createBoolMenuItem("Vactrol: low-pass gate (filter tracks gain)", "",
			[=]() { return m->lpgMode; },
			[=](bool v) { m->lpgMode = v; }));
	}
};


Model* modelGarnishment = createModel<Garnishment, GarnishmentWidget>("Garnishment");

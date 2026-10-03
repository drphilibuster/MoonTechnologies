#include "../plugin.hpp"
#include "Panel.hpp"

#include <cmath>


// ---------------------------------------------------------------------------
// Garnishment -- a dual VCA / low-pass gate. Each of the two identical channels
// is, per its MODE switch, one of three Modular-in-a-Week circuits:
//
//   OTA      Simple 13700 Dual VCA (Kristian Blasol): an LM13700 whose control
//            current sets its transconductance, solved as the board (OtaVca.hpp on
//            Lm13700.hpp): linearizing diodes, the trimmer, 22k into Iabc, 33k and
//            the buffer. CV is volts at the 22k, not a 0-10 V "how open".
//   VACTROL  Vactrol VCA (Kristian Blasol): an LED driving a photoresistor in
//            series with the audio -- the classic slow, characterful LPG cell.
//   JFET AM  "I AM O" (Quincas): a 2N5457 used as a voltage-controlled resistor,
//            dividing a carrier against its own channel resistance -- solved as
//            the circuit (IAmO.hpp), so it saturates and self-pinches like it.
//
// One physical control set is reused for all three rather than the panel
// switching what it shows: BIAS and CV IN (through CV AMOUNT) form the control
// voltage that opens the OTA, drives the vactrol's LED, or sets the JFET's gate
// bias; LAG is the vactrol's own asymmetric attack/decay, applied to all three
// as a shared "how fast does this respond" knob. Channel 2's CV normals from
// channel 1's, so one CV cable can drive both.
// ---------------------------------------------------------------------------

#include "Vca.hpp"


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
	float otaTrim = 0.5f;   // OTA: the 1k BIAS trimmer's wiper (0.5 balances the pair)
	bool otaRemoveDc = false; // OTA: take the buffer's -1.4 V rest level off Out
	bool lpgMode = false;   // VACTROL: close a one-pole LPF along with the gain
	int vactrolPart = 0;    // VACTROL: which part (vactrol::PartId; 0 = the VTL5C3, the default)

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

	/** One channel, one sample block. `cvFallback`, when not null, is the
	    nearest connected CV port among the earlier channels -- the normalling. */
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
			// normalized to the 0..1 the vactrol and JFET circuits share as "how open".
			float volts = bias + cvAmt * cv;
			float target = clamp(volts / 10.f, 0.f, 1.f);
			bus.ctrl[c] = slewTo(bus.ctrl[c], target, attackTau, decayTau, args.sampleTime);
			float ctrl = bus.ctrl[c];
			// The OTA board has no such convention: the CV goes through 22k into the chip's
			// bias pin, in volts, and the control current is whatever that makes of it. The
			// same lag is applied to those volts.
			bus.cvv[c] = slewTo(bus.cvv[c], clamp(volts, -12.f, 12.f), attackTau, decayTau, args.sampleTime);
			bus.open[c] = ctrl;

			float out = 0.f;
			switch (mode) {
				case MODE_OTA: {
					// Solved as the Day 2 board (OtaVca.hpp): the audio through 470 nF and
					// 27k into the 13700's linearizing-diode input, the trimmer, Iabc from the
					// CV through 22k, 33k and the buffer. DC coupled, as drawn: Out rests at
					// about -1.4 V unless the menu takes it off.
					garnishment::OtaVca& o = bus.ota[c];
					o.trim = otaTrim;
					o.squareIabc = expCv;
					out = (float) o.process(audioIn, bus.cvv[c], args.sampleRate);
					bus.open[c] = clamp((float)(o.iabc / 0.933e-3), 0.f, 1.f);   // 1 at +10 V CV
					if (otaRemoveDc) {
						float k = 1.f - std::exp(-2.f * (float) M_PI * 2.f * args.sampleTime);   // ~2 Hz
						bus.dcLp[c] += (out - bus.dcLp[c]) * k;
						out -= bus.dcLp[c];
					}
					break;
				}
				case MODE_VACTROL: {
					// The Day 2 schematic: CV In -> 330 ohm -> LED, the LDR in series with the
					// audio. The LED sees BIAS + CV AMOUNT * CV volts through 330 ohm (the
					// schematic's 100k pot is the CV AMOUNT), the cell is a
					// PerkinElmer VTL5C3 (Vactrol.hpp: resistance against current, attack, the two decays
					// and the memory), dividing against an assumed ~100 kOhm downstream input
					// impedance -- R12 has no fixed partner on the sheet. LAG does not act here:
					// the lag is the part's own, and slewing the drive on top would count it twice.
					float drive = clamp(volts, 0.f, 15.f);
					bus.ldr[c].setPart(vactrolPart);
					float g = vactrolGain(bus.ldr[c], drive, args.sampleTime);
					bus.open[c] = g;      // the read-out shows what the cell is actually passing
					float sig = audioIn;
					if (lpgMode) {
						float cutoff = 20.f + g * g * 15000.f;
						sig = bus.lpg[c].process(sig, cutoff, args.sampleTime);
					}
					out = sig * g;
					break;
				}
				case MODE_JFET_AM: {
					// The schematic's "In" pin (our BIAS/CV) is the JFET's gate, "AM"
					// (our audio IN) reaches the drain through 100 ohms; the circuit is
					// solved (IAmO.hpp). The control's 0..1 spans the gate from 0 V (the
					// channel fully on, 2.3 dB down) to -4 V (past the 2N5457's typical
					// -2 V pinch-off, no division at all).
					out = (float) bus.jfet[c].process(-4.0 * ctrl, audioIn, args.sampleRate);
					break;
				}
			}
			outputs[outId].setVoltage(clamp(out, -12.f, 12.f), c);
		}
	}

	void process(const ProcessArgs& args) override {
		// CV normals down the bank: a channel with nothing in its own CV jack
		// falls back to the nearest connected CV port at or before it, not just
		// its immediate neighbour's raw jack -- otherwise the fallback chain
		// breaks the moment one channel in the middle is itself relying on the
		// normal, and everything past it goes dark. This is what lets one CV
		// patched into channel 1 open all six.
		Input* lastConnected = nullptr;
		for (int c = 0; c < N; c++) {
			processBus(args, bus[c], audioInId(c), cvInId(c), outId(c),
			           pid(c, BIAS_PARAM), pid(c, MODE_PARAM),
			           pid(c, LAG_PARAM), pid(c, CVAMT_PARAM),
			           lastConnected);
			if (inputs[cvInId(c)].isConnected())
				lastConnected = &inputs[cvInId(c)];
		}
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
		json_object_set_new(root, "otaTrim", json_real(otaTrim));
		json_object_set_new(root, "otaRemoveDc", json_boolean(otaRemoveDc));
		json_object_set_new(root, "vactrolPart", json_integer(vactrolPart));
		return root;
	}

	void dataFromJson(json_t* root) override {
		json_t* j;
		j = json_object_get(root, "expCv");
		if (j) expCv = json_boolean_value(j);
		j = json_object_get(root, "lpgMode");
		if (j) lpgMode = json_boolean_value(j);
		j = json_object_get(root, "otaTrim");
		if (j) otaTrim = clamp((float) json_number_value(j), 0.f, 1.f);
		j = json_object_get(root, "otaRemoveDc");
		if (j) otaRemoveDc = json_boolean_value(j);
		j = json_object_get(root, "vactrolPart");
		vactrolPart = j ? clamp((int) json_integer_value(j), 0, ::vactrol::PART_COUNT - 1) : 0;
	}
};


// ---------------------------------------------------------------------------
// Look and feel comes entirely from src/PanelTheme.hpp and src/Garnishment/Panel.hpp,
// generated by tools/panels/Garnishment.py -- see ../../panelkit/README.md.

typedef RoundBlackKnob GarnishmentKnob;


/** The read-out: each channel's circuit by name over a bar of how open it is
 *  right now -- bias and CV together, through the lag, which is the thing the
 *  three circuits share. The names and the bars are fields: click a name for
 *  the circuit, drag a bar for BIAS. */
struct GarnishmentDisplay : LedDisplay {
	Garnishment* module = NULL;

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) {
			LedDisplay::drawLayer(args, layer);
			return;
		}
		NVGcontext* vg = args.vg;
		static const Rect MODE[Garnishment::N] = { panel::FIELD_MODE1, panel::FIELD_MODE2, panel::FIELD_MODE3,
		                                          panel::FIELD_MODE4, panel::FIELD_MODE5, panel::FIELD_MODE6 };
		static const Rect BAR[Garnishment::N] = { panel::FIELD_BAR1, panel::FIELD_BAR2, panel::FIELD_BAR3,
		                                         panel::FIELD_BAR4, panel::FIELD_BAR5, panel::FIELD_BAR6 };
		static const char* const NAMES[3] = {"OTA", "LPG", "JFET"};
		const panel::TextStyle NAME(panel::Face::Mono, 8.f, panel::MINT, NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE);
		const panel::TextStyle NUM(panel::Face::Mono, 6.f, panel::SAGE, NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE);
		for (int c = 0; c < Garnishment::N; c++) {
			const int mode = module ? clamp((int)std::lround(module->params[Garnishment::pid(c, Garnishment::MODE_PARAM)].getValue()), 0, 2) : 0;
			const Rect m = panel::inGlass(MODE[c]);
			panel::text(vg, NAME, m.pos.x + m.size.x / 2.f, m.pos.y + m.size.y * 0.75f, NAMES[mode]);

			const Rect b = panel::inGlass(BAR[c]);
			const float x = b.pos.x + b.size.x * 0.25f, w = b.size.x * 0.5f;
			const float top = b.pos.y + 1.f, bot = b.pos.y + b.size.y - 8.f, h = bot - top;
			nvgBeginPath(vg); nvgRect(vg, x, top, w, h);
			nvgFillColor(vg, panel::alpha(panel::SAGE, 0.10f)); nvgFill(vg);
			// how open: the channel's control level, its first voice
			const float open = module ? clamp(module->bus[c].open[0], 0.f, 1.f) : 0.f;
			nvgBeginPath(vg); nvgRect(vg, x, bot - h * open, w, h * open);
			nvgFillColor(vg, panel::alpha(panel::LIME, 0.75f)); nvgFill(vg);
			// where BIAS alone sits, as a tick across the bar
			const float bias = module ? module->params[Garnishment::pid(c, Garnishment::BIAS_PARAM)].getValue() / 10.f : 0.f;
			nvgBeginPath(vg); nvgRect(vg, x - 1.5f, bot - h * bias - 0.6f, w + 3.f, 1.2f);
			nvgFillColor(vg, panel::PAPER); nvgFill(vg);
			panel::text(vg, NUM, b.pos.x + b.size.x / 2.f, b.pos.y + b.size.y - 1.5f, std::to_string(c + 1));
		}
	}
};


/** The OTA board's 1k BIAS trimmer, as a slider in the context menu. */
struct OtaTrimQuantity : Quantity {
	Garnishment* module;
	OtaTrimQuantity(Garnishment* m) : module(m) {}
	void setValue(float v) override { module->otaTrim = clamp(v, 0.f, 1.f); }
	float getValue() override { return module->otaTrim; }
	float getMinValue() override { return 0.f; }
	float getMaxValue() override { return 1.f; }
	float getDefaultValue() override { return 0.5f; }
	float getDisplayValue() override { return getValue() * 100.f; }
	void setDisplayValue(float v) override { setValue(v / 100.f); }
	std::string getLabel() override { return "OTA BIAS trimmer"; }
	std::string getUnit() override { return "%"; }
};

struct OtaTrimSlider : ui::Slider {
	OtaTrimSlider(Garnishment* m) {
		quantity = new OtaTrimQuantity(m);
		box.size.x = 220.f;
	}
	~OtaTrimSlider() { delete quantity; }
};


struct GarnishmentWidget : ModuleWidget {
	GarnishmentWidget(Garnishment* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Garnishment.svg")));

		panel::addScrews(this);
		panel::addLabels(this);

		static const Vec* biasPos[Garnishment::N] = {
			&panel::BIAS1_POS, &panel::BIAS2_POS, &panel::BIAS3_POS,
			&panel::BIAS4_POS, &panel::BIAS5_POS, &panel::BIAS6_POS };
		GarnishmentDisplay* display = new GarnishmentDisplay;
		display->module = module;
		display->box.pos = panel::mm(panel::GLASS_X, panel::GLASS_Y);
		display->box.size = panel::mm(panel::GLASS_W, panel::GLASS_H);
		addChild(display);
		static const Rect MODE[Garnishment::N] = { panel::FIELD_MODE1, panel::FIELD_MODE2, panel::FIELD_MODE3,
		                                          panel::FIELD_MODE4, panel::FIELD_MODE5, panel::FIELD_MODE6 };
		static const Rect BAR[Garnishment::N] = { panel::FIELD_BAR1, panel::FIELD_BAR2, panel::FIELD_BAR3,
		                                         panel::FIELD_BAR4, panel::FIELD_BAR5, panel::FIELD_BAR6 };
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
			addParam(panel::createField<panel::ScreenSelect>(MODE[c], module,
				Garnishment::pid(c, Garnishment::MODE_PARAM)));
			panel::ScreenKnob* bar = panel::createField<panel::ScreenKnob>(BAR[c], module,
				Garnishment::pid(c, Garnishment::BIAS_PARAM));
			bar->speed = 2.5f;          // a bar travels its own height
			addParam(bar);
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

		menu->addChild(new OtaTrimSlider(m));

		menu->addChild(createBoolMenuItem("OTA: remove the output's DC rest level (-1.4 V)", "",
			[=]() { return m->otaRemoveDc; },
			[=](bool v) { m->otaRemoveDc = v; }));

		menu->addChild(createBoolMenuItem("OTA: exponential CV response (Iabc squared)", "",
			[=]() { return m->expCv; },
			[=](bool v) { m->expCv = v; }));

		menu->addChild(createBoolMenuItem("Vactrol: low-pass gate (filter tracks gain)", "",
			[=]() { return m->lpgMode; },
			[=](bool v) { m->lpgMode = v; }));

		std::vector<std::string> parts;
		for (int i = 0; i < ::vactrol::PART_COUNT; i++) parts.push_back(::vactrol::part(i).name);
		menu->addChild(createIndexSubmenuItem("Vactrol part", parts,
			[=]() { return (size_t) m->vactrolPart; },
			[=](size_t i) { m->vactrolPart = (int) i; }));
	}
};


Model* modelGarnishment = createModel<Garnishment, GarnishmentWidget>("Garnishment");

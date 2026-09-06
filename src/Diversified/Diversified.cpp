#include "../plugin.hpp"
#include "Panel.hpp"
#include "Dsp99.hpp"

using divfx::Patch;
using divfx::BankCtx;
using divfx::MiawCtx;
using divfx::NUM_PROGRAMS;


/** One complete effect engine: the DSP99 bank and the seven MiaW boards.
 *
 * Two of these exist. A program change starts a 30 ms crossfade from the one
 * that is running to the one that is not, so the tail of a reverb survives the
 * change instead of being cut off -- which is the whole reason a multi-effect
 * with a program knob is worth patching a CV into. */
struct Voice {
	divfx::Dsp99 bank;
	divfx::MiawRack miaw;
	MiawCtx mctx;

	void init() {
		bank.init();
		miaw.init();
	}
	void setSampleRate(float sr) {
		bank.setSampleRate(sr);
		miaw.setSampleRate(sr);
	}
	void clearAll() {
		bank.clearAll();
		miaw.clearAll();
	}
	/** Zeroes only what the incoming program will use, so a crossfade never
	    starts from another algorithm's leftover state. */
	void prepare(const Patch& p) {
		if (p.algA == divfx::A_MIAW) {
			miaw.clear(p.chrA);
		}
		else {
			bank.clear(p.algA);
			if (p.algB != divfx::A_NONE)
				bank.clear(p.algB);
		}
	}

	void setParams(const Patch& p, const BankCtx& bc, const MiawCtx& tmpl) {
		if (p.algA == divfx::A_MIAW) {
			mctx = tmpl;
			for (int i = 0; i < 3; i++)
				mctx.p[i] = p.a[i];
			miaw.setParams(p.chrA, mctx);
		}
		else {
			bank.setParams(p, bc);
		}
	}

	void process(const Patch& p, const BankCtx& bc, const MiawCtx& tmpl,
	             float inL, float inR, float& outL, float& outR) {
		if (p.algA == divfx::A_MIAW) {
			// Only the per-sample fields; the macros were folded in at control
			// rate and must not be overwritten by the template's.
			mctx.aux = tmpl.aux;
			mctx.auxConnected = tmpl.auxConnected;
			mctx.auxGate = tmpl.auxGate;
			mctx.ret = tmpl.ret;
			mctx.retConnected = tmpl.retConnected;
			mctx.inRConnected = tmpl.inRConnected;
			mctx.sr = tmpl.sr;
			miaw.process(p.chrA, mctx, inL, inR, outL, outR);
		}
		else {
			mctx.send = 0.f;
			bank.process(p, bc, inL, inR, outL, outR);
		}
	}
};


/** A macro's tooltip has to say what the macro currently is, because that
    changes with every program. */
struct MacroQuantity : ParamQuantity {
	int slot;
	MacroQuantity() : slot(0) {}
	std::string getLabel() override;
};


struct Diversified : Module {
	enum ParamId {
		PROGRAM_PARAM, P1_PARAM, P2_PARAM, P3_PARAM, MIX_PARAM,
		PROGRAM_CV_PARAM, P1_CV_PARAM, P2_CV_PARAM, P3_CV_PARAM, MIX_CV_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		IN_L_INPUT, IN_R_INPUT, RET_INPUT,
		PROGRAM_INPUT, P1_INPUT, P2_INPUT, P3_INPUT, MIX_INPUT,
		AUX_INPUT, TAP_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		OUT_L_OUTPUT, OUT_R_OUTPUT, SEND_OUTPUT, OUTPUTS_LEN
	};
	enum LightId {
		ACTIVE_LIGHT, TAP_LIGHT, LIGHTS_LEN
	};

	Voice voice[2];
	Patch base[2];      // what each voice was handed
	Patch work[2];      // ... with the macros folded in
	int activeVoice;
	bool fading;
	float fadePos;
	int pendingProgram;
	int currentProgram;

	BankCtx bctx;
	MiawCtx tmpl;

	dsp::SchmittTrigger tapTrigger, auxTrigger;
	dsp::ClockDivider ctrlDivider, lightDivider;
	int tapCount;
	float tapSec;
	float sinceTap;

	float wetMeter;

	// Menu options, all program-specific but stored for the module.
	bool crushSwap;
	bool crushLpf;
	bool ringSmooth;

	// Published for the read-out. Written from the audio thread at control rate,
	// read by the UI: all scalars and pointers into the static program table.
	int dispProgram;
	const char* dispName;
	const char* dispMac[3];
	bool dispClocked;

	Diversified() : activeVoice(0), fading(false), fadePos(0.f),
	                pendingProgram(-1), currentProgram(0),
	                tapCount(0), tapSec(0.f), sinceTap(1e6f), wetMeter(0.f),
	                crushSwap(false), crushLpf(true), ringSmooth(false),
	                dispProgram(0), dispName("SMALL HALL"), dispClocked(false) {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

		// The program knob carries the whole sheet, so its tooltip is the sheet.
		std::vector<std::string> progLabels;
		progLabels.reserve(NUM_PROGRAMS);
		for (int i = 0; i < NUM_PROGRAMS; i++) {
			Patch p;
			divfx::programAt(i, p);
			progLabels.push_back(string::f("%03d  %s", i, p.name));
		}
		configSwitch(PROGRAM_PARAM, 0.f, (float) (NUM_PROGRAMS - 1), 0.f,
		             "Program", progLabels);
		configParam(PROGRAM_CV_PARAM, -1.f, 1.f, 0.f, "Program CV", "%", 0.f, 100.f);
		getParamQuantity(PROGRAM_CV_PARAM)->randomizeEnabled = false;

		for (int i = 0; i < 3; i++) {
			MacroQuantity* q = configParam<MacroQuantity>(
			    P1_PARAM + i, 0.f, 1.f, 0.5f,
			    string::f("Macro %d", i + 1), "%", 0.f, 100.f);
			q->slot = i;
			configParam(P1_CV_PARAM + i, -1.f, 1.f, 0.f,
			            string::f("Macro %d CV", i + 1), "%", 0.f, 100.f);
			getParamQuantity(P1_CV_PARAM + i)->randomizeEnabled = false;
		}

		configParam(MIX_PARAM, 0.f, 1.f, 0.5f, "Mix", "%", 0.f, 100.f);
		configParam(MIX_CV_PARAM, -1.f, 1.f, 0.f, "Mix CV", "%", 0.f, 100.f);
		getParamQuantity(MIX_CV_PARAM)->randomizeEnabled = false;

		configInput(IN_L_INPUT, "Left audio");
		configInput(IN_R_INPUT, "Right audio (normalled to left)");
		configInput(RET_INPUT, "Effects loop return (normalled to send)");
		configInput(PROGRAM_INPUT, "Program CV");
		configInput(P1_INPUT, "Macro 1 CV");
		configInput(P2_INPUT, "Macro 2 CV");
		configInput(P3_INPUT, "Macro 3 CV");
		configInput(MIX_INPUT, "Mix CV");
		configInput(AUX_INPUT, "Aux gate / CV");
		configInput(TAP_INPUT, "Tap / clock");
		configOutput(OUT_L_OUTPUT, "Left audio");
		configOutput(OUT_R_OUTPUT, "Right audio");
		configOutput(SEND_OUTPUT, "Effects loop send");

		configBypass(IN_L_INPUT, OUT_L_OUTPUT);
		configBypass(IN_R_INPUT, OUT_R_OUTPUT);

		ctrlDivider.setDivision(16);
		lightDivider.setDivision(512);

		float sr = APP->engine->getSampleRate();
		for (int v = 0; v < 2; v++) {
			voice[v].init();
			voice[v].setSampleRate(sr);
		}
		bctx.sr = sr;
		bctx.sampleTime = 1.f / sr;
		tmpl.sr = sr;

		divfx::programAt(0, base[0]);
		divfx::programAt(0, base[1]);
		work[0] = base[0];
		work[1] = base[1];
		publish(base[0]);
	}

	void publish(const Patch& p) {
		dispProgram = p.index;
		dispName = p.name;
		for (int i = 0; i < 3; i++)
			dispMac[i] = p.mac[i];
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		bctx.sr = e.sampleRate;
		bctx.sampleTime = 1.f / e.sampleRate;
		tmpl.sr = e.sampleRate;
		for (int v = 0; v < 2; v++) {
			voice[v].setSampleRate(e.sampleRate);
			voice[v].clearAll();
		}
		tapSec = 0.f;
		tapCount = 0;
		sinceTap = 1e6f;
		fading = false;
		fadePos = 0.f;
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		for (int v = 0; v < 2; v++)
			voice[v].clearAll();
		activeVoice = 0;
		fading = false;
		fadePos = 0.f;
		pendingProgram = -1;
		currentProgram = 0;
		tapSec = 0.f;
		tapCount = 0;
		sinceTap = 1e6f;
		wetMeter = 0.f;
		divfx::programAt(0, base[0]);
		work[0] = base[0];
		publish(base[0]);
	}

	/** knob + CV * attenuverter, clamped to the control's own range. */
	float macro(int i) {
		float v = params[P1_PARAM + i].getValue();
		if (inputs[P1_INPUT + i].isConnected())
			v += inputs[P1_INPUT + i].getVoltage() * 0.1f
			     * params[P1_CV_PARAM + i].getValue();
		return clamp(v, 0.f, 1.f);
	}

	void process(const ProcessArgs& args) override {
		// --- input -----------------------------------------------------------
		float inL = clamp(inputs[IN_L_INPUT].getVoltageSum(), -20.f, 20.f);
		bool rConnected = inputs[IN_R_INPUT].isConnected();
		float inR = rConnected
		            ? clamp(inputs[IN_R_INPUT].getVoltageSum(), -20.f, 20.f)
		            : inL;

		// --- tap clock -------------------------------------------------------
		sinceTap += args.sampleTime;
		if (tapTrigger.process(inputs[TAP_INPUT].getVoltage(), 0.1f, 2.f)) {
			if (tapCount > 0 && sinceTap > 0.005f && sinceTap < 2.5f)
				tapSec = sinceTap;
			else
				tapCount = 0;
			tapCount++;
			sinceTap = 0.f;
		}
		if (!inputs[TAP_INPUT].isConnected() || sinceTap > 3.f) {
			tapSec = 0.f;
			tapCount = 0;
		}

		bool auxGate = auxTrigger.process(inputs[AUX_INPUT].getVoltage(), 0.1f, 1.f);

		// --- control rate ----------------------------------------------------
		if (ctrlDivider.process()) {
			float pv = params[PROGRAM_PARAM].getValue();
			if (inputs[PROGRAM_INPUT].isConnected())
				pv += inputs[PROGRAM_INPUT].getVoltage() * 0.1f
				      * params[PROGRAM_CV_PARAM].getValue() * (float) (NUM_PROGRAMS - 1);
			int want = (int) clamp(std::round(pv), 0.f, (float) (NUM_PROGRAMS - 1));

			if (want != currentProgram) {
				if (fading) {
					// Mid-crossfade: remember it and take it when this one lands,
					// so sweeping the knob is a sequence of clean 30 ms fades and
					// not a stutter of half-finished ones.
					pendingProgram = want;
				}
				else {
					startFade(want);
				}
			}

			bctx.aux = inputs[AUX_INPUT].getVoltage();
			bctx.auxConnected = inputs[AUX_INPUT].isConnected();
			bctx.auxGate = auxGate;
			bctx.tapSec = tapSec;

			tmpl.aux = bctx.aux;
			tmpl.auxConnected = bctx.auxConnected;
			tmpl.auxGate = auxGate;
			tmpl.tapSec = tapSec;
			tmpl.retConnected = inputs[RET_INPUT].isConnected();
			tmpl.inRConnected = rConnected;
			tmpl.crushSwap = crushSwap;
			tmpl.crushLpf = crushLpf;
			tmpl.ringSmooth = ringSmooth;

			float m[3];
			for (int i = 0; i < 3; i++)
				m[i] = macro(i);

			int last = fading ? 2 : 1;
			for (int k = 0; k < last; k++) {
				int v = (k == 0) ? activeVoice : (1 - activeVoice);
				work[v] = base[v];
				for (int i = 0; i < 3; i++)
					work[v].setMacro(i, m[i]);
				voice[v].setParams(work[v], bctx, tmpl);
			}
		}

		// per-sample context that the circuits read directly
		bctx.auxGate = auxGate;
		tmpl.auxGate = auxGate;
		tmpl.aux = inputs[AUX_INPUT].getVoltage();
		tmpl.ret = inputs[RET_INPUT].getVoltage();
		tmpl.retConnected = inputs[RET_INPUT].isConnected();
		tmpl.inRConnected = rConnected;

		// --- engines ---------------------------------------------------------
		int other = 1 - activeVoice;
		float wetL = 0.f, wetR = 0.f;
		voice[activeVoice].process(work[activeVoice], bctx, tmpl, inL, inR, wetL, wetR);

		if (fading) {
			float bL = 0.f, bR = 0.f;
			voice[other].process(work[other], bctx, tmpl, inL, inR, bL, bR);
			float x = fadePos;
			wetL = wetL * (1.f - x) + bL * x;
			wetR = wetR * (1.f - x) + bR * x;

			fadePos += args.sampleTime / 0.03f;
			if (fadePos >= 1.f) {
				fadePos = 0.f;
				fading = false;
				activeVoice = other;
				publish(base[activeVoice]);
				if (pendingProgram >= 0) {
					int p = pendingProgram;
					pendingProgram = -1;
					if (p != currentProgram)
						startFade(p);
				}
			}
		}

		// --- the effects loop ------------------------------------------------
		// In the Echomatic the loop is inside the feedback path and the circuit
		// has already used it, so SEND comes from there. Everywhere else it is a
		// mono insert across the wet path.
		const Patch& target = fading ? base[other] : base[activeVoice];
		bool echoLoop = (target.algA == divfx::A_MIAW
		                 && target.chrA == divfx::MW_ECHOMATIC);
		float sendVal;
		if (echoLoop) {
			sendVal = voice[fading ? other : activeVoice].mctx.send;
		}
		else {
			sendVal = 0.5f * (wetL + wetR);
			if (inputs[RET_INPUT].isConnected()) {
				float r = clamp(inputs[RET_INPUT].getVoltage(), -20.f, 20.f);
				wetL = r;
				wetR = r;
			}
		}
		outputs[SEND_OUTPUT].setVoltage(clamp(sendVal, -12.f, 12.f));

		// --- mix -------------------------------------------------------------
		float mix = params[MIX_PARAM].getValue();
		if (inputs[MIX_INPUT].isConnected())
			mix += inputs[MIX_INPUT].getVoltage() * 0.1f
			       * params[MIX_CV_PARAM].getValue();
		mix = clamp(mix, 0.f, 1.f);

		float outL = clamp(inL * (1.f - mix) + wetL * mix, -12.f, 12.f);
		float outR = clamp(inR * (1.f - mix) + wetR * mix, -12.f, 12.f);
		outputs[OUT_L_OUTPUT].setVoltage(outL);
		outputs[OUT_R_OUTPUT].setVoltage(outR);

		// --- lights and read-out ---------------------------------------------
		float w = 0.5f * (std::fabs(wetL) + std::fabs(wetR));
		wetMeter += (w > wetMeter ? 0.02f : 0.0008f) * (w - wetMeter);
		if (lightDivider.process()) {
			float dt = args.sampleTime * lightDivider.getDivision();
			lights[ACTIVE_LIGHT].setBrightnessSmooth(clamp(wetMeter * 0.25f, 0.f, 1.f), dt);
			lights[TAP_LIGHT].setBrightnessSmooth(tapSec > 0.f ? 1.f : 0.f, dt);
			dispClocked = (tapSec > 0.f);
		}
	}

	void startFade(int want) {
		int other = 1 - activeVoice;
		divfx::programAt(want, base[other]);
		work[other] = base[other];
		for (int i = 0; i < 3; i++)
			work[other].setMacro(i, macro(i));
		voice[other].prepare(base[other]);
		voice[other].setParams(work[other], bctx, tmpl);
		currentProgram = want;
		fading = true;
		fadePos = 0.f;
		publish(base[other]);
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "bitSwap", json_boolean(crushSwap));
		json_object_set_new(root, "reconstructionFilter", json_boolean(crushLpf));
		json_object_set_new(root, "smoothRing", json_boolean(ringSmooth));
		return root;
	}

	void dataFromJson(json_t* root) override {
		json_t* j;
		j = json_object_get(root, "bitSwap");
		if (j) crushSwap = json_boolean_value(j);
		j = json_object_get(root, "reconstructionFilter");
		if (j) crushLpf = json_boolean_value(j);
		j = json_object_get(root, "smoothRing");
		if (j) ringSmooth = json_boolean_value(j);
	}
};


std::string MacroQuantity::getLabel() {
	Diversified* m = dynamic_cast<Diversified*>(module);
	if (m && m->dispMac[slot])
		return string::f("%s", m->dispMac[slot]);
	return ParamQuantity::getLabel();
}


// ---------------------------------------------------------------------------
// Look and feel. The palette, the shared hardware and the whole silkscreen come
// from src/PanelTheme.hpp and src/Diversified/Panel.hpp, generated by
// tools/panels/Diversified.py -- see ../../panelkit/README.md. Nothing about the
// look is decided here.

typedef RoundLargeBlackKnob ProgramKnob;
typedef RoundBlackKnob      PanelKnob;


/** The read-out: which of the hundred and six holdings is running, and what the
 * three macro knobs are doing for it.
 *
 * The number is in the segment face and the words are not, because DSEG7 only
 * carries [0-9 . : -] and Rack chains a Japanese sans onto every font as a
 * fallback -- a letter drawn in the segment face would silently come out in it. */
struct DiversifiedDisplay : LedDisplay {
	Diversified* module;

	DiversifiedDisplay() : module(NULL) {}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) {
			LedDisplay::drawLayer(args, layer);
			return;
		}
		int idx = module ? module->dispProgram : 0;
		const char* name = module ? module->dispName : "SMALL HALL";
		bool clocked = module ? module->dispClocked : false;

		const float pad = 5.f;
		const float rightX = box.size.x - pad;
		const NVGcolor dim = panel::alpha(panel::LIME, 0.55f);

		const panel::TextStyle NAME(panel::Face::Mono, 10.f, panel::MINT,
			NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE, -0.5f);
		const panel::TextStyle TAG(panel::Face::Mono, 7.5f, dim,
			NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
		const panel::TextStyle MAC(panel::Face::Mono, 7.5f, dim,
			NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE);

		panel::segValue(args.vg, pad, 12.f, 11.f, string::f("%03d", idx), "",
		                panel::LIME);
		panel::text(args.vg, NAME, rightX, 12.f, name);

		if (clocked)
			panel::text(args.vg, TAG, pad, 24.f, "CLK");

		// Each macro's name sits over the knob it belongs to: the display starts
		// 4.2 mm from the left edge, so a knob at x mm is at x - 4.2 mm here.
		static const float knobX[3] = {34.48f, 56.96f, 79.44f};
		for (int i = 0; i < 3; i++) {
			const char* m = module ? module->dispMac[i] : "--";
			panel::text(args.vg, MAC, panel::mm(knobX[i] - 4.2f, 0.f).x, 24.f, m);
		}
	}
};


struct DiversifiedWidget : ModuleWidget {
	DiversifiedWidget(Diversified* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Diversified.svg")));

		panel::addScrews(this);
		panel::addLabels(this);

		DiversifiedDisplay* display = new DiversifiedDisplay;
		display->module = module;
		display->box.pos = panel::mm(4.2f, 10.2f);
		display->box.size = panel::mm(panel::W - 8.4f, 9.2f);
		addChild(display);

		addParam(createParamCentered<ProgramKnob>(panel::mm(panel::PROGRAM_POS.x, panel::PROGRAM_POS.y), module, Diversified::PROGRAM_PARAM));
		addParam(createParamCentered<PanelKnob>(panel::mm(panel::P1_POS.x, panel::P1_POS.y), module, Diversified::P1_PARAM));
		addParam(createParamCentered<PanelKnob>(panel::mm(panel::P2_POS.x, panel::P2_POS.y), module, Diversified::P2_PARAM));
		addParam(createParamCentered<PanelKnob>(panel::mm(panel::P3_POS.x, panel::P3_POS.y), module, Diversified::P3_PARAM));
		addParam(createParamCentered<PanelKnob>(panel::mm(panel::MIX_POS.x, panel::MIX_POS.y), module, Diversified::MIX_PARAM));

		addParam(createParamCentered<Trimpot>(panel::mm(panel::PROGRAM_CV_POS.x, panel::PROGRAM_CV_POS.y), module, Diversified::PROGRAM_CV_PARAM));
		addParam(createParamCentered<Trimpot>(panel::mm(panel::P1_CV_POS.x, panel::P1_CV_POS.y), module, Diversified::P1_CV_PARAM));
		addParam(createParamCentered<Trimpot>(panel::mm(panel::P2_CV_POS.x, panel::P2_CV_POS.y), module, Diversified::P2_CV_PARAM));
		addParam(createParamCentered<Trimpot>(panel::mm(panel::P3_CV_POS.x, panel::P3_CV_POS.y), module, Diversified::P3_CV_PARAM));
		addParam(createParamCentered<Trimpot>(panel::mm(panel::MIX_CV_POS.x, panel::MIX_CV_POS.y), module, Diversified::MIX_CV_PARAM));

		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::PROGRAM_IN_POS.x, panel::PROGRAM_IN_POS.y), module, Diversified::PROGRAM_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::P1_IN_POS.x, panel::P1_IN_POS.y), module, Diversified::P1_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::P2_IN_POS.x, panel::P2_IN_POS.y), module, Diversified::P2_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::P3_IN_POS.x, panel::P3_IN_POS.y), module, Diversified::P3_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::MIX_IN_POS.x, panel::MIX_IN_POS.y), module, Diversified::MIX_INPUT));

		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::AUX_IN_POS.x, panel::AUX_IN_POS.y), module, Diversified::AUX_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::TAP_IN_POS.x, panel::TAP_IN_POS.y), module, Diversified::TAP_INPUT));

		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::IN_L_POS.x, panel::IN_L_POS.y), module, Diversified::IN_L_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::IN_R_POS.x, panel::IN_R_POS.y), module, Diversified::IN_R_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::RET_POS.x, panel::RET_POS.y), module, Diversified::RET_INPUT));
		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::SEND_POS.x, panel::SEND_POS.y), module, Diversified::SEND_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::OUT_L_POS.x, panel::OUT_L_POS.y), module, Diversified::OUT_L_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::OUT_R_POS.x, panel::OUT_R_POS.y), module, Diversified::OUT_R_OUTPUT));

		addChild(createLightCentered<SmallLight<panel::LimeLight> >(
		             panel::mm(panel::ACTIVE_POS.x, panel::ACTIVE_POS.y), module, Diversified::ACTIVE_LIGHT));
		addChild(createLightCentered<SmallLight<panel::MintLight> >(
		             panel::mm(panel::TAP_LED_POS.x, panel::TAP_LED_POS.y), module, Diversified::TAP_LIGHT));
	}

	void appendContextMenu(Menu* menu) override {
		Diversified* m = dynamic_cast<Diversified*>(module);
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Diversified"));

		menu->addChild(createMenuLabel("104 Bitcrusher"));
		menu->addChild(createBoolMenuItem("Swap MSB and LSB", "",
			[=]() { return m->crushSwap; },
			[=](bool v) { m->crushSwap = v; }));
		menu->addChild(createBoolMenuItem("Reconstruction filter", "",
			[=]() { return m->crushLpf; },
			[=](bool v) { m->crushLpf = v; }));

		menu->addChild(createMenuLabel("105 Ring modulator"));
		menu->addChild(createBoolMenuItem("Smooth (analogue product)", "",
			[=]() { return m->ringSmooth; },
			[=](bool v) { m->ringSmooth = v; }));
	}
};


Model* modelDiversified = createModel<Diversified, DiversifiedWidget>("Diversified");

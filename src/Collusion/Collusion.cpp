#include "../plugin.hpp"
#include "Panel.hpp"
#include "Swarm.hpp"

using collusion::Swarm;
using collusion::Ledger;
using collusion::Controls;
using collusion::N_FILERS;

static const char* kSchemeNames[collusion::NUM_SCHEMES] = {
	"Cartel (all to all)",
	"Ring (each to its neighbours)",
	"Pyramid (one-way cascade)",
	"Firefly (pulse coupled)",
};

//! RANGE's two bands. The LFO band reaches down to a cycle a minute and up
//! through the low audio; the audio band starts where the ear stops counting.
static const float LO_MIN = 0.02f, LO_MAX = 40.f;
static const float HI_MIN = 20.f,  HI_MAX = 4000.f;

//! SPREAD's full travel, in octaves either side of RATE. Kuramoto's critical
//! coupling for a uniform spread of natural frequencies is about 0.9 times the
//! half-width, so a knob that reaches 1.5 octaves and a COUPLING that reaches
//! 2.2 put the whole phase transition inside the panel at every SPREAD setting.
static const float SPREAD_OCT = 1.5f;
static const float COUPLE_MAX = 2.2f;
//! LEVERAGE's full travel, also in octaves.
static const float LEVERAGE_OCT = 1.5f;


struct Collusion : Module {
	enum ParamId {
		RATE_PARAM, SPREAD_PARAM, SHAPE_PARAM, RANGE_PARAM, DEAL_PARAM,
		COUPLE_PARAM, EVADE_PARAM, SCHEME_PARAM, LEVERAGE_PARAM,
		TERM_PARAM, AUDIT_PARAM,
		RATE_CV_PARAM, COUPLE_CV_PARAM, EVADE_CV_PARAM,
		SHAPE_CV_PARAM, SPREAD_CV_PARAM, AUDIT_CV_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		RATE_INPUT, COUPLE_INPUT, EVADE_INPUT,
		SHAPE_INPUT, SPREAD_INPUT, AUDIT_INPUT,
		SYNC_INPUT, CLK_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		OUT1_OUTPUT, OUT2_OUTPUT, OUT3_OUTPUT,
		OUT4_OUTPUT, OUT5_OUTPUT, OUT6_OUTPUT,
		CONSENSUS_OUTPUT, ORDER_OUTPUT, LEDGER_OUTPUT, PULSE_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		LED1_LIGHT, LED2_LIGHT, LED3_LIGHT,
		LED4_LIGHT, LED5_LIGHT, LED6_LIGHT,
		ORDER_LIGHT, LEDGER_LIGHT, DEAL_LIGHT,
		LIGHTS_LEN
	};

	Swarm swarm;
	Ledger ledger;
	dsp::SchmittTrigger syncTrigger;
	dsp::SchmittTrigger clockTrigger;
	dsp::BooleanTrigger dealTrigger;
	dsp::PulseGenerator ledgerPulse;
	dsp::PulseGenerator dealPulse;
	dsp::ClockDivider lightDivider;

	Collusion() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

		configParam(RATE_PARAM, 0.f, 1.f, 0.5f, "Rate");
		configParam(SPREAD_PARAM, 0.f, 1.f, 0.25f, "Spread", " oct", 0.f, SPREAD_OCT);
		configParam(SHAPE_PARAM, 0.f, 1.f, 0.f, "Shape (sine to relaxation spike)",
		            "%", 0.f, 100.f);

		configSwitch(RANGE_PARAM, 0.f, 1.f, 0.f, "Range",
		             {"LO (0.02 - 40 Hz)", "HI (20 Hz - 4 kHz)"});
		configButton(DEAL_PARAM, "Deal a new fan of natural rates");

		// The one knob the module is about. Bipolar: below zero the filers push
		// each other apart into maximal disagreement, above it they pull together.
		configParam(COUPLE_PARAM, -1.f, 1.f, 0.2f, "Coupling", "", 0.f, COUPLE_MAX);
		configParam(EVADE_PARAM, 0.f, 1.f, 0.f, "Evasion (coupling phase lag)",
		            " turns", 0.f, collusion::MAX_ALPHA);

		std::vector<std::string> schemeLabels;
		for (int i = 0; i < collusion::NUM_SCHEMES; i++)
			schemeLabels.push_back(kSchemeNames[i]);
		configSwitch(SCHEME_PARAM, 0.f, (float) (collusion::NUM_SCHEMES - 1), 0.f,
		             "Scheme", schemeLabels);
		getParamQuantity(SCHEME_PARAM)->snapEnabled = true;

		configParam(LEVERAGE_PARAM, -1.f, 1.f, 0.f, "Leverage (ledger back into the rates)",
		            " oct", 0.f, LEVERAGE_OCT);

		std::vector<std::string> termLabels;
		for (int i = 0; i < Ledger::NUM_TERMS; i++)
			termLabels.push_back(string::f("%d steps", Ledger::termLength(i)));
		configSwitch(TERM_PARAM, 0.f, (float) (Ledger::NUM_TERMS - 1), 5.f, "Term", termLabels);
		getParamQuantity(TERM_PARAM)->snapEnabled = true;

		configParam(AUDIT_PARAM, 0.f, 1.f, 0.5f,
		            "Audit (chance a step is rewritten, not recirculated)", "%", 0.f, 100.f);

		// The V/oct depth starts at full: a swarm that ignores the pitch input
		// until told otherwise is a swarm nobody patches a keyboard into. The
		// rest start at zero, which is the only safe default for an attenuverter.
		configParam(RATE_CV_PARAM, -1.f, 1.f, 1.f, "V/oct depth", "%", 0.f, 100.f);
		configParam(COUPLE_CV_PARAM, -1.f, 1.f, 0.f, "Coupling CV depth", "%", 0.f, 100.f);
		configParam(EVADE_CV_PARAM, -1.f, 1.f, 0.f, "Evasion CV depth", "%", 0.f, 100.f);
		configParam(SHAPE_CV_PARAM, -1.f, 1.f, 0.f, "Shape CV depth", "%", 0.f, 100.f);
		configParam(SPREAD_CV_PARAM, -1.f, 1.f, 0.f, "Spread CV depth", "%", 0.f, 100.f);
		configParam(AUDIT_CV_PARAM, -1.f, 1.f, 0.f, "Audit CV depth", "%", 0.f, 100.f);
		for (int p = RATE_CV_PARAM; p <= AUDIT_CV_PARAM; p++)
			getParamQuantity(p)->randomizeEnabled = false;

		configInput(RATE_INPUT, "1 V/oct");
		configInput(COUPLE_INPUT, "Coupling CV");
		configInput(EVADE_INPUT, "Evasion CV");
		configInput(SHAPE_INPUT, "Shape CV");
		configInput(SPREAD_INPUT, "Spread CV");
		configInput(AUDIT_INPUT, "Audit CV");
		configInput(SYNC_INPUT, "Sync (aligns every filer to phase zero)");
		configInput(CLK_INPUT, "Ledger clock (unpatched, the ledger follows filer 1)");

		for (int i = 0; i < N_FILERS; i++)
			configOutput(OUT1_OUTPUT + i, string::f("Filer %d", i + 1));
		configOutput(CONSENSUS_OUTPUT, "Consensus (the population's mean wave)");
		configOutput(ORDER_OUTPUT, "Order (0-10 V: how much they agree)");
		configOutput(LEDGER_OUTPUT, "Ledger (stepped CV)");
		configOutput(PULSE_OUTPUT, "Pulse (a gate every time a filer fires)");

		lightDivider.setDivision(64);
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		swarm.fan();
		swarm.scatter();
		ledger.reset();
		syncTrigger.reset();
		clockTrigger.reset();
	}

	void onRandomize(const RandomizeEvent& e) override {
		Module::onRandomize(e);
		swarm.deal([]() { return random::uniform(); });
	}

	/** A knob plus its own attenuverted CV, clamped to the knob's own travel.
	    Rack's convention is 10 V full scale for a unipolar modulation input, so
	    a tenth of a volt is one percent of the knob. */
	float modulated(int knobParam, int cvParam, int input, float lo, float hi) {
		float v = params[knobParam].getValue();
		if (inputs[input].isConnected())
			v += inputs[input].getVoltage() * 0.1f * params[cvParam].getValue();
		return clamp(v, lo, hi);
	}

	void process(const ProcessArgs& args) override {
		// --- what each filer would do alone ---------------------------------
		const bool audio = params[RANGE_PARAM].getValue() > 0.5f;
		const float lo = audio ? HI_MIN : LO_MIN;
		const float hi = audio ? HI_MAX : LO_MAX;

		float oct = 0.f;
		if (inputs[RATE_INPUT].isConnected())
			oct = inputs[RATE_INPUT].getVoltage() * params[RATE_CV_PARAM].getValue();
		float baseFreq = lo * std::pow(hi / lo, params[RATE_PARAM].getValue())
		                 * dsp::exp2_taylor5(clamp(oct, -12.f, 12.f));
		baseFreq = clamp(baseFreq, 0.005f, args.sampleRate * 0.45f);

		// --- what they do about each other ----------------------------------
		float couple = params[COUPLE_PARAM].getValue();
		if (inputs[COUPLE_INPUT].isConnected())
			couple += inputs[COUPLE_INPUT].getVoltage() * 0.2f * params[COUPLE_CV_PARAM].getValue();
		couple = clamp(couple, -1.2f, 1.2f) * COUPLE_MAX;

		if (dealTrigger.process(params[DEAL_PARAM].getValue() > 0.5f)) {
			swarm.deal([]() { return random::uniform(); });
			dealPulse.trigger(0.12f);
		}
		if (syncTrigger.process(inputs[SYNC_INPUT].getVoltage(), 0.1f, 2.f))
			swarm.align();

		Controls c;
		c.baseFreq   = baseFreq;
		c.spread     = modulated(SPREAD_PARAM, SPREAD_CV_PARAM, SPREAD_INPUT, 0.f, 1.f) * SPREAD_OCT;
		c.shape      = modulated(SHAPE_PARAM, SHAPE_CV_PARAM, SHAPE_INPUT, 0.f, 1.f);
		c.coupling   = couple;
		c.evasion    = modulated(EVADE_PARAM, EVADE_CV_PARAM, EVADE_INPUT, 0.f, 1.f);
		c.scheme     = (int) std::round(params[SCHEME_PARAM].getValue());
		c.leverage   = params[LEVERAGE_PARAM].getValue() * LEVERAGE_OCT;
		c.ledger     = ledger.value();
		c.sampleTime = args.sampleTime;
		c.sampleRate = args.sampleRate;

		swarm.process(c);

		// --- the record they keep of it -------------------------------------
		// Unpatched, the register runs off filer 1, which is the Benjolin's own
		// arrangement: one oscillator clocks, the population supplies the bit.
		// Patched, CLK takes it off the swarm entirely, and the same knobs then
		// write a melody in your own time rather than in the swarm's.
		ledger.setLength(Ledger::termLength((int) std::round(params[TERM_PARAM].getValue())));
		const bool clocked = inputs[CLK_INPUT].isConnected()
			? clockTrigger.process(inputs[CLK_INPUT].getVoltage(), 0.1f, 2.f)
			: swarm.fired[0];
		if (clocked) {
			const float audit = modulated(AUDIT_PARAM, AUDIT_CV_PARAM, AUDIT_INPUT, 0.f, 1.f);
			ledger.clock(swarm.consensus > 0.f, random::uniform() < audit);
			ledgerPulse.trigger(0.03f);
		}

		// --- out ------------------------------------------------------------
		for (int i = 0; i < N_FILERS; i++)
			outputs[OUT1_OUTPUT + i].setVoltage(swarm.wave[i] * 5.f);
		outputs[CONSENSUS_OUTPUT].setVoltage(swarm.consensus * 5.f);
		outputs[ORDER_OUTPUT].setVoltage(swarm.order * 10.f);
		outputs[LEDGER_OUTPUT].setVoltage(ledger.value() * 5.f);
		outputs[PULSE_OUTPUT].setVoltage(swarm.pulse ? 10.f : 0.f);

		if (lightDivider.process()) {
			const float dt = args.sampleTime * lightDivider.getDivision();
			for (int i = 0; i < N_FILERS; i++)
				lights[LED1_LIGHT + i].setBrightnessSmooth(swarm.wave[i] * 0.5f + 0.5f, dt);
			lights[ORDER_LIGHT].setBrightnessSmooth(swarm.order, dt);
			lights[LEDGER_LIGHT].setBrightnessSmooth(ledgerPulse.process(dt) ? 1.f : 0.f, dt);
			lights[DEAL_LIGHT].setBrightnessSmooth(dealPulse.process(dt) ? 1.f : 0.f, dt);
		}
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		// The fan is state, not a parameter: DEAL rolls it, and a patch that
		// does not carry it back comes up sounding like a different patch.
		json_t* fan = json_array();
		for (int i = 0; i < N_FILERS; i++)
			json_array_append_new(fan, json_real(swarm.detune[i]));
		json_object_set_new(root, "fan", fan);
		json_object_set_new(root, "ledgerBits", json_integer((json_int_t) ledger.bits));
		json_object_set_new(root, "ledgerWidth", json_integer(ledger.width));
		return root;
	}

	void dataFromJson(json_t* root) override {
		if (json_t* fan = json_object_get(root, "fan")) {
			for (int i = 0; i < N_FILERS && i < (int) json_array_size(fan); i++)
				swarm.detune[i] = clamp((float) json_number_value(json_array_get(fan, i)), -1.f, 1.f);
		}
		if (json_t* b = json_object_get(root, "ledgerBits"))
			ledger.bits = (uint32_t) json_integer_value(b);
		if (json_t* w = json_object_get(root, "ledgerWidth"))
			ledger.setWidth((int) json_integer_value(w));
	}
};


// ---------------------------------------------------------------------------
// Look and feel. The palette, the shared hardware and the entire silkscreen come
// from src/PanelTheme.hpp, generated by tools/panels/Collusion.py -- see
// ../../panelkit/README.md. Nothing about the look is written here.

typedef RoundLargeBlackKnob BigKnob;    // RATE and COUPLING
typedef RoundBlackKnob      PanelKnob;  // everything else


struct CollusionWidget : ModuleWidget {
	CollusionWidget(Collusion* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Collusion.svg")));

		panel::addScrews(this);
		panel::addLabels(this);

		// FILINGS.
		addParam(createParamCentered<BigKnob>(
		             panel::mm(panel::RATE_POS.x, panel::RATE_POS.y), module, Collusion::RATE_PARAM));
		addParam(createParamCentered<PanelKnob>(
		             panel::mm(panel::SPREAD_POS.x, panel::SPREAD_POS.y), module, Collusion::SPREAD_PARAM));
		addParam(createParamCentered<PanelKnob>(
		             panel::mm(panel::SHAPE_POS.x, panel::SHAPE_POS.y), module, Collusion::SHAPE_PARAM));
		addParam(createParamCentered<CKSS>(
		             panel::mm(panel::RANGE_POS.x, panel::RANGE_POS.y), module, Collusion::RANGE_PARAM));
		addParam(createLightParamCentered<VCVLightBezel<panel::LimeLight> >(
		             panel::mm(panel::DEAL_POS.x, panel::DEAL_POS.y), module,
		             Collusion::DEAL_PARAM, Collusion::DEAL_LIGHT));

		// AGREEMENT.
		addParam(createParamCentered<BigKnob>(
		             panel::mm(panel::COUPLE_POS.x, panel::COUPLE_POS.y), module, Collusion::COUPLE_PARAM));
		addParam(createParamCentered<PanelKnob>(
		             panel::mm(panel::EVADE_POS.x, panel::EVADE_POS.y), module, Collusion::EVADE_PARAM));
		addParam(createParamCentered<PanelKnob>(
		             panel::mm(panel::SCHEME_POS.x, panel::SCHEME_POS.y), module, Collusion::SCHEME_PARAM));
		addParam(createParamCentered<PanelKnob>(
		             panel::mm(panel::LEVERAGE_POS.x, panel::LEVERAGE_POS.y), module, Collusion::LEVERAGE_PARAM));
		addParam(createParamCentered<PanelKnob>(
		             panel::mm(panel::TERM_POS.x, panel::TERM_POS.y), module, Collusion::TERM_PARAM));
		addParam(createParamCentered<PanelKnob>(
		             panel::mm(panel::AUDIT_POS.x, panel::AUDIT_POS.y), module, Collusion::AUDIT_PARAM));
		addChild(createLightCentered<SmallLight<panel::LimeLight> >(
		             panel::mm(panel::ORDER_LED_POS.x, panel::ORDER_LED_POS.y), module, Collusion::ORDER_LIGHT));

		// The six CV pairs: a trimpot directly over its jack, in the order the
		// silkscreen names them.
#define COLLUSION_CV(TRIM, JACK, PARAM, INPUT) \
		addParam(createParamCentered<Trimpot>( \
		             panel::mm(panel::TRIM.x, panel::TRIM.y), module, Collusion::PARAM)); \
		addInput(createInputCentered<panel::PortIn>( \
		             panel::mm(panel::JACK.x, panel::JACK.y), module, Collusion::INPUT));

		COLLUSION_CV(RATE_CV_POS,   RATE_IN_POS,   RATE_CV_PARAM,   RATE_INPUT)
		COLLUSION_CV(COUPLE_CV_POS, COUPLE_IN_POS, COUPLE_CV_PARAM, COUPLE_INPUT)
		COLLUSION_CV(EVADE_CV_POS,  EVADE_IN_POS,  EVADE_CV_PARAM,  EVADE_INPUT)
		COLLUSION_CV(SHAPE_CV_POS,  SHAPE_IN_POS,  SHAPE_CV_PARAM,  SHAPE_INPUT)
		COLLUSION_CV(SPREAD_CV_POS, SPREAD_IN_POS, SPREAD_CV_PARAM, SPREAD_INPUT)
		COLLUSION_CV(AUDIT_CV_POS,  AUDIT_IN_POS,  AUDIT_CV_PARAM,  AUDIT_INPUT)
#undef COLLUSION_CV

		// PARTIES: six identical columns, each a jack and the lamp on its label.
#define COLLUSION_FILER(N) \
		addOutput(createOutputCentered<panel::PortOut>( \
		             panel::mm(panel::OUT##N##_POS.x, panel::OUT##N##_POS.y), \
		             module, Collusion::OUT1_OUTPUT + (N - 1))); \
		addChild(createLightCentered<SmallLight<panel::MintLight> >( \
		             panel::mm(panel::LED##N##_POS.x, panel::LED##N##_POS.y), \
		             module, Collusion::LED1_LIGHT + (N - 1)));

		COLLUSION_FILER(1)
		COLLUSION_FILER(2)
		COLLUSION_FILER(3)
		COLLUSION_FILER(4)
		COLLUSION_FILER(5)
		COLLUSION_FILER(6)
#undef COLLUSION_FILER

		// The footer band.
		addInput(createInputCentered<panel::PortIn>(
		             panel::mm(panel::SYNC_IN_POS.x, panel::SYNC_IN_POS.y), module, Collusion::SYNC_INPUT));
		addInput(createInputCentered<panel::PortIn>(
		             panel::mm(panel::CLK_IN_POS.x, panel::CLK_IN_POS.y), module, Collusion::CLK_INPUT));
		addChild(createLightCentered<SmallLight<panel::LimeLight> >(
		             panel::mm(panel::LEDGER_LED_POS.x, panel::LEDGER_LED_POS.y), module, Collusion::LEDGER_LIGHT));
		addOutput(createOutputCentered<panel::PortOut>(
		             panel::mm(panel::CONSENSUS_POS.x, panel::CONSENSUS_POS.y), module, Collusion::CONSENSUS_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(
		             panel::mm(panel::ORDER_OUT_POS.x, panel::ORDER_OUT_POS.y), module, Collusion::ORDER_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(
		             panel::mm(panel::LEDGER_OUT_POS.x, panel::LEDGER_OUT_POS.y), module, Collusion::LEDGER_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(
		             panel::mm(panel::PULSE_OUT_POS.x, panel::PULSE_OUT_POS.y), module, Collusion::PULSE_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		Collusion* m = dynamic_cast<Collusion*>(module);
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Collusion"));

		// Three bits is the Benjolin's ladder: eight levels, chunky enough that
		// the ledger reads as a tune rather than as a wander. Wider is smoother,
		// and further from what a rungler sounds like.
		menu->addChild(createIndexSubmenuItem("Ledger resolution",
			{"3 bits (8 steps)", "5 bits (32 steps)", "8 bits (256 steps)"},
			[=]() { return m->ledger.width == 3 ? 0 : (m->ledger.width == 5 ? 1 : 2); },
			[=](int i) { m->ledger.setWidth(i == 0 ? 3 : (i == 1 ? 5 : 8)); }));

		menu->addChild(createMenuItem("Deal a new fan", "",
			[=]() { m->swarm.deal([]() { return random::uniform(); }); }));

		menu->addChild(createMenuItem("Restore the even fan", "",
			[=]() { m->swarm.fan(); }));
	}
};


Model* modelCollusion = createModel<Collusion, CollusionWidget>("Collusion");

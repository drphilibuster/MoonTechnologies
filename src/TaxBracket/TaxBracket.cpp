#include "../plugin.hpp"
#include "Panel.hpp"
#include "Ladder.hpp"

using taxbracket::Ladder;


// Tax Bracket: the Olegtron R2R. A passive 8-bit R-2R ladder whose every jack
// is both an input and an output -- a DAC, a weighted mixer, a programmable
// attenuator and a "labile" multiple, depending only on what is plugged where.
// Rack ports are one-directional, so each hardware jack is a pair here: the
// input is the jack being driven, the output is the same jack being read. The
// network itself lives in Ladder.hpp and is solved as the resistor string it is.

struct TaxBracket : Module {
	enum ParamId {
		GROUND_PARAM,
		SCALE_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		ENUMS(BIT_INPUTS, 8),       // jacks 1, 2, 4 .. 128
		IO_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		ENUMS(BIT_OUTPUTS, 8),
		IO_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		LIGHTS_LEN
	};

	static const int JACKS = Ladder::JACKS;

	Ladder ladder;
	dsp::ClockDivider displayDivider;

	/** The manual's further mod: ground the I/O jack too when nothing is in it.
	    Off by default, because I/O is the ladder's top node -- grounding it pins
	    the whole string's far end and attenuates everything. */
	bool groundIO = false;

	// Read-outs for the panel display, written from the audio thread at the
	// display divider's rate.
	float dispVolts = 0.f;
	char  dispWord[9];      // MSB first: '1' / '0' driven, '0' grounded, '-' floating

	TaxBracket() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

		std::vector<std::string> groundLabels;
		groundLabels.push_back("Floating");
		groundLabels.push_back("Grounded");
		configSwitch(GROUND_PARAM, 0.f, 1.f, 0.f, "Unplugged jacks", groundLabels);
		configParam(SCALE_PARAM, 0.f, 2.f, 1.f, "Output scale", "%", 0.f, 100.f);
		getParamQuantity(SCALE_PARAM)->randomizeEnabled = false;

		for (int k = 0; k < 8; k++) {
			std::string name = string::f("Jack %d", 1 << k);
			configInput(BIT_INPUTS + k, name);
			configOutput(BIT_OUTPUTS + k, name);
			configBypass(BIT_INPUTS + k, BIT_OUTPUTS + k);
		}
		configInput(IO_INPUT, "I/O jack");
		configOutput(IO_OUTPUT, "I/O jack");
		configBypass(IO_INPUT, IO_OUTPUT);

		for (int i = 0; i < 8; i++)
			dispWord[i] = '-';
		dispWord[8] = '\0';

		displayDivider.setDivision(256);
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		ladder.invalidate();
	}

	void process(const ProcessArgs& args) override {
		// Which jacks are sources, which have anything in them at all, and how
		// many channels the widest source carries. A jack with a cable in either
		// of its ports counts as plugged -- on the hardware, an output cable
		// breaks the ground normalization just as an input one does.
		unsigned driven = 0, plugged = 0;
		int channels = 1;
		for (int j = 0; j < JACKS; j++) {
			bool in = inputs[j].isConnected();
			bool out = outputs[j].isConnected();
			if (in) {
				driven |= 1u << j;
				channels = std::max(channels, inputs[j].getChannels());
			}
			if (in || out)
				plugged |= 1u << j;
		}

		unsigned grounded = 0;
		if (params[GROUND_PARAM].getValue() > 0.5f)
			grounded |= ~plugged & 0xFFu;
		if (groundIO && !((plugged >> Ladder::IO) & 1u))
			grounded |= 1u << Ladder::IO;

		// Re-solves only when the pattern changes; per sample this is a lookup.
		ladder.configure(driven, grounded);

		float scale = params[SCALE_PARAM].getValue();
		for (int j = 0; j < JACKS; j++)
			outputs[j].setChannels(channels);

		float src[Ladder::JACKS];
		float v[Ladder::NODES];
		float vIO0 = 0.f;
		for (int c = 0; c < channels; c++) {
			for (int j = 0; j < JACKS; j++)
				src[j] = ((driven >> j) & 1u) ? inputs[j].getPolyVoltage(c) : 0.f;
			ladder.evaluate(src, v);
			for (int j = 0; j < JACKS; j++)
				outputs[j].setVoltage(clamp(v[Ladder::nodeOf(j)] * scale, -12.f, 12.f), c);
			if (c == 0)
				vIO0 = v[Ladder::NODES - 1];
		}

		if (displayDivider.process()) {
			dispVolts = vIO0 * scale;
			for (int k = 0; k < 8; k++) {
				char ch = '-';
				if ((driven >> k) & 1u)
					ch = inputs[k].getPolyVoltage(0) >= 1.f ? '1' : '0';
				else if ((grounded >> k) & 1u)
					ch = '0';
				dispWord[7 - k] = ch;
			}
		}
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "groundIO", json_boolean(groundIO));
		return root;
	}

	void dataFromJson(json_t* root) override {
		json_t* j = json_object_get(root, "groundIO");
		if (j)
			groundIO = json_boolean_value(j);
	}
};


// ---------------------------------------------------------------------------
// Look and feel. Palette, hardware and the whole silkscreen come from
// src/PanelTheme.hpp and this panel's generated Panel.hpp -- see
// ../panelkit/README.md. Nothing about the look is written here.

/** The read-out: what the ladder adds up to at I/O, and the 8-bit word it is
    being handed (MSB first; '-' is a floating jack). */
struct TaxBracketDisplay : LedDisplay {
	TaxBracket* module = NULL;

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) {
			LedDisplay::drawLayer(args, layer);
			return;
		}
		float volts = module ? module->dispVolts : 0.f;
		const char* word = module ? module->dispWord : "--------";

		const float pad = 5.f;
		const float base = 13.f;
		const NVGcolor dim = panel::alpha(panel::LIME, 0.55f);
		const panel::TextStyle TAG(panel::Face::Mono, 8.f, dim,
			NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
		const panel::TextStyle WORD(panel::Face::Mono, 9.f, panel::MINT,
			NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE, 1.f);

		// The segment face carries only [0-9 . : -], which "%.2f" honours.
		float x = panel::text(args.vg, TAG, pad, base, "DUE");
		panel::segValue(args.vg, x + 2.5f, base, 10.f, string::f("%.2f", volts), "V",
		                panel::LIME);
		panel::text(args.vg, WORD, box.size.x - pad, base, word);
	}
};


struct TaxBracketWidget : ModuleWidget {
	TaxBracketWidget(TaxBracket* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/TaxBracket.svg")));

		panel::addScrews(this);
		panel::addLabels(this);

		TaxBracketDisplay* display = new TaxBracketDisplay;
		display->module = module;
		display->box.pos = panel::mm(panel::GLASS_X, panel::GLASS_Y);
		display->box.size = panel::mm(panel::GLASS_W, panel::GLASS_H);
		addChild(display);

		// One table for the eight bit pairs, so the widget cannot disagree with
		// the enum about which jack is which.
		static const Vec* const IN_POS[8] = {
			&panel::IN1_POS, &panel::IN2_POS, &panel::IN4_POS, &panel::IN8_POS,
			&panel::IN16_POS, &panel::IN32_POS, &panel::IN64_POS, &panel::IN128_POS
		};
		static const Vec* const OUT_POS[8] = {
			&panel::OUT1_POS, &panel::OUT2_POS, &panel::OUT4_POS, &panel::OUT8_POS,
			&panel::OUT16_POS, &panel::OUT32_POS, &panel::OUT64_POS, &panel::OUT128_POS
		};
		for (int k = 0; k < 8; k++) {
			addInput(createInputCentered<panel::PortIn>(
				panel::mm(IN_POS[k]->x, IN_POS[k]->y), module, TaxBracket::BIT_INPUTS + k));
			addOutput(createOutputCentered<panel::PortOut>(
				panel::mm(OUT_POS[k]->x, OUT_POS[k]->y), module, TaxBracket::BIT_OUTPUTS + k));
		}
		addInput(createInputCentered<panel::PortIn>(
			panel::mm(panel::IO_IN_POS.x, panel::IO_IN_POS.y), module, TaxBracket::IO_INPUT));
		addOutput(createOutputCentered<panel::PortOut>(
			panel::mm(panel::IO_OUT_POS.x, panel::IO_OUT_POS.y), module, TaxBracket::IO_OUTPUT));

		addParam(createParamCentered<CKSS>(
			panel::mm(panel::GROUND_POS.x, panel::GROUND_POS.y), module, TaxBracket::GROUND_PARAM));
		addParam(createParamCentered<Trimpot>(
			panel::mm(panel::SCALE_POS.x, panel::SCALE_POS.y), module, TaxBracket::SCALE_PARAM));
	}

	void appendContextMenu(Menu* menu) override {
		TaxBracket* m = dynamic_cast<TaxBracket*>(module);
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Tax Bracket"));
		menu->addChild(createBoolMenuItem("Ground I/O when unplugged", "",
			[=]() { return m->groundIO; },
			[=](bool v) { m->groundIO = v; }));
	}
};


Model* modelTaxBracket = createModel<TaxBracket, TaxBracketWidget>("TaxBracket");

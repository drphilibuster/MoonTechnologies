/** SCHEDULE A -- the itemised attachment to Repossession's FORM 1099-A.

Eight rows, one per seized asset. Each carries the four charges that can be
varied against that step -- SPEED, GAIN, START, LENGTH -- and the audio the step
produces. Repossession carries all of this on four polyphonic jacks already, one
cable with channel N addressing step N; that costs no panel space and patches
badly, and this is the same eight steps with somewhere to plug a cable into.

It attaches to the **left**: put it immediately to Repossession's right. Nothing
here has state, a menu, or an opinion. The whole module is a frame of message
passing:

    -> host   each patched jack's voltage, and the fact that it is patched
    <- host   each step's audio, and the colour its light should wear

The `has` flags are what make the two ways of patching combine rather than
fight. A poly LFO into the host's SPEED drives all eight steps; one envelope
into step 5 here overrides that step and only that step. An empty jack overrides
nothing, which is why this cannot be done by summing.

A lone expander -- no host, or a host that has just been deleted -- shows dark
lights and silent outputs. Rack can dissolve the neighbour between one frame and
the next, so the only safe assumption about it is that it may be gone. */
#include "../plugin.hpp"
#include "../Repossession/Expander.hpp"
#include "Panel.hpp"


struct ScheduleA : Module {
	enum ParamId { PARAMS_LEN };
	/** Four per step, laid out step-major so INPUT_FOR(cv, step) is arithmetic
	    rather than a table. */
	enum InputId {
		STEP_INPUT,
		INPUTS_LEN = STEP_INPUT + rp::NUM_SLOTS * rp::NUM_STEP_CV
	};
	enum OutputId {
		STEP_OUTPUT,
		OUTPUTS_LEN = STEP_OUTPUT + rp::NUM_SLOTS
	};
	enum LightId {
		STEP_LIGHT,
		LIGHTS_LEN = STEP_LIGHT + rp::NUM_SLOTS * 3
	};

	static int inputFor(int cv, int step) {
		return STEP_INPUT + step * rp::NUM_STEP_CV + cv;
	}

	/** Rack's two buffers per side. The host writes into our left producer and
	    flips it; we read our left consumer. */
	rp::SchedMessage msgA, msgB;

	dsp::ClockDivider lightDiv;

	ScheduleA() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

		static const char* CV_NAMES[rp::NUM_STEP_CV] =
			{"speed", "gain", "window start", "window length"};
		for (int i = 0; i < rp::NUM_SLOTS; i++) {
			for (int c = 0; c < rp::NUM_STEP_CV; c++) {
				configInput(inputFor(c, i),
					string::f("Step %d %s CV", i + 1, CV_NAMES[c]));
			}
			configOutput(STEP_OUTPUT + i, string::f("Step %d audio", i + 1));
		}

		leftExpander.producerMessage = &msgA;
		leftExpander.consumerMessage = &msgB;

		lightDiv.setDivision(32);
	}

	/** The host, if there is one and it is the right kind of module. Checked by
	    model pointer rather than by dynamic_cast, which is what Rack's own
	    expanders do and what keeps this file from having to know the host's
	    layout. */
	bool hostAttached() {
		return leftExpander.module
			&& leftExpander.module->model == modelRepossession;
	}

	void process(const ProcessArgs& args) override {
		rp::SchedMessage* fromHost = (rp::SchedMessage*) leftExpander.consumerMessage;
		bool attached = hostAttached();
		bool live = attached && fromHost && fromHost->hostPresent;

		// What we send back up the chain: every patched jack, and only those.
		if (attached) {
			rp::SchedMessage* toHost =
				(rp::SchedMessage*) leftExpander.module->rightExpander.producerMessage;
			if (toHost) {
				for (int i = 0; i < rp::NUM_SLOTS; i++) {
					for (int c = 0; c < rp::NUM_STEP_CV; c++) {
						Input& in = inputs[inputFor(c, i)];
						bool on = in.isConnected();
						toHost->has[c][i] = on;
						toHost->cv[c][i] = on ? in.getVoltage() : 0.f;
					}
				}
				leftExpander.module->rightExpander.requestMessageFlip();
			}
		}

		for (int i = 0; i < rp::NUM_SLOTS; i++) {
			outputs[STEP_OUTPUT + i].setVoltage(
				live ? fromHost->stepOut[i] : 0.f);
		}

		if (lightDiv.process()) {
			float dt = args.sampleTime * lightDiv.getDivision();
			for (int i = 0; i < rp::NUM_SLOTS; i++) {
				for (int k = 0; k < 3; k++) {
					lights[STEP_LIGHT + i * 3 + k].setBrightnessSmooth(
						live ? fromHost->ink[i][k] : 0.f, dt);
				}
			}
		}
	}
};


/** The row lights, which are the only thing naming a row.

    With no module -- the browser, the library thumbnail -- Rack turns every base
    colour of a light fully on, and red plus green plus blue is white. Eight
    white dots say nothing, so the preview wears the same lime-to-mint ramp the
    host's step buttons do. The duplication of that ramp with Repossession's own
    is deliberate: the two modules are separate translation units and each has
    its own Panel.hpp, which may not be included twice. */
struct RowLight : app::ModuleLightWidget {
	int slot = 0;

	RowLight() {
		bgColor = panel::GLASS;
		borderColor = nvgTransRGBA(panel::RULE, 0xa0);
		addBaseColor(nvgRGB(0xff, 0x00, 0x00));
		addBaseColor(nvgRGB(0x00, 0xff, 0x00));
		addBaseColor(nvgRGB(0x00, 0x00, 0xff));
	}

	void step() override {
		app::ModuleLightWidget::step();
		if (!module) {
			NVGcolor c = nvgLerpRGBA(panel::LIME, panel::MINT,
				(rp::NUM_SLOTS > 1)
					? (float) slot / (float) (rp::NUM_SLOTS - 1) : 0.f);
			std::vector<float> b;
			b.push_back(c.r);
			b.push_back(c.g);
			b.push_back(c.b);
			setBrightnesses(b);
		}
	}
};


struct ScheduleAWidget : ModuleWidget {
	ScheduleAWidget(ScheduleA* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/ScheduleA.svg")));

		panel::addScrews(this);
		panel::addLabels(this);

		for (int i = 0; i < rp::NUM_SLOTS; i++) {
			const Vec* r = rowPos(i);

			RowLight* l = createLightCentered<RowLight>(
				panel::mm(r[0].x, r[0].y), module,
				ScheduleA::STEP_LIGHT + i * 3);
			l->slot = i;
			addChild(l);

			for (int c = 0; c < rp::NUM_STEP_CV; c++) {
				addInput(createInputCentered<panel::PortIn>(
					panel::mm(r[1 + c].x, r[1 + c].y), module,
					ScheduleA::inputFor(c, i)));
			}
			addOutput(createOutputCentered<panel::PortOut>(
				panel::mm(r[5].x, r[5].y), module,
				ScheduleA::STEP_OUTPUT + i));
		}
	}

	/** The six positions of row `i`, in the order the columns are declared in
	    tools/panels/ScheduleA.py: light, speed, gain, start, length, out. */
	static const Vec* rowPos(int i) {
		static const Vec p[rp::NUM_SLOTS][6] = {
			{panel::STEP1_POS, panel::SPEED1_POS, panel::GAIN1_POS,
			 panel::START1_POS, panel::LEN1_POS, panel::OUT1_POS},
			{panel::STEP2_POS, panel::SPEED2_POS, panel::GAIN2_POS,
			 panel::START2_POS, panel::LEN2_POS, panel::OUT2_POS},
			{panel::STEP3_POS, panel::SPEED3_POS, panel::GAIN3_POS,
			 panel::START3_POS, panel::LEN3_POS, panel::OUT3_POS},
			{panel::STEP4_POS, panel::SPEED4_POS, panel::GAIN4_POS,
			 panel::START4_POS, panel::LEN4_POS, panel::OUT4_POS},
			{panel::STEP5_POS, panel::SPEED5_POS, panel::GAIN5_POS,
			 panel::START5_POS, panel::LEN5_POS, panel::OUT5_POS},
			{panel::STEP6_POS, panel::SPEED6_POS, panel::GAIN6_POS,
			 panel::START6_POS, panel::LEN6_POS, panel::OUT6_POS},
			{panel::STEP7_POS, panel::SPEED7_POS, panel::GAIN7_POS,
			 panel::START7_POS, panel::LEN7_POS, panel::OUT7_POS},
			{panel::STEP8_POS, panel::SPEED8_POS, panel::GAIN8_POS,
			 panel::START8_POS, panel::LEN8_POS, panel::OUT8_POS},
		};
		return p[i];
	}
};


Model* modelScheduleA = createModel<ScheduleA, ScheduleAWidget>("ScheduleA");

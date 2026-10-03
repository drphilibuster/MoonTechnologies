#include "../plugin.hpp"
#include "Panel.hpp"
#include "Vco.hpp"

#include <map>
#include <memory>
#include <mutex>
#include <atomic>

/** The solved plants, built once per (supply, input capacitance) and kept for the life of the
 *  process: a Plant is a few hundred kilobytes and about a tenth of a second of circuit solving,
 *  so it is built on the thread that asks (the UI, when a menu changes; the loader, when a patch
 *  is read) and the audio thread only ever follows a pointer. */
static const accrual::Plant* plantFor(int vddIdx, int cinIdx) {
	static const double kVdd[4] = { 9.0, 10.0, 12.0, 15.0 };
	static const double kCin[4] = { 5e-12, 10e-12, 15e-12, 20e-12 };
	static std::mutex mu;
	static std::map<int, std::unique_ptr<accrual::Plant>> cache;
	std::lock_guard<std::mutex> lock(mu);
	int key = vddIdx * 8 + cinIdx;
	auto it = cache.find(key);
	if (it != cache.end())
		return it->second.get();
	std::unique_ptr<accrual::Plant> p(new accrual::Plant);
	p->build(kVdd[vddIdx], kCin[cinIdx]);
	if (!p->ok)
		return nullptr;
	const accrual::Plant* out = p.get();
	cache[key] = std::move(p);
	return out;
}

struct AccrualModule : Module {
	enum ParamId { TUNE_PARAM, FINE_PARAM, PW_PARAM, PARAMS_LEN };
	enum InputId { CV1_INPUT, CV2_INPUT, PWM_INPUT, INPUTS_LEN };
	enum OutputId { SAW_OUTPUT, PULSE_OUTPUT, OUTPUTS_LEN };
	enum LightId { LIGHTS_LEN };

	static const int kVoices = 16;
	accrual::Voice voice[kVoices];
	accrual::PwmFront pwm[kVoices];
	double zth[kVoices] = {};
	float lastPwm[kVoices] = {};
	float lastPw = -1.f;
	bool pwmWas[kVoices] = {};

	// the left-click menu, saved in the patch
	int vddIdx = 2;                 // 9, 10, 12, 15 V
	int cinIdx = 1;                 // 5, 10, 15, 20 pF
	int rangeIdx = 0;               // the pots' ends: +-5 V, +-12 V
	float r21 = 0.f;                // the v/oct trimmer, ohms (0 until the first calibration)
	bool r21Set = false;

	std::atomic<const accrual::Plant*> plant{nullptr};
	std::atomic<const accrual::Plant*> pendingPlant{nullptr};   // set by the UI thread, taken by the audio thread
	int channels = 1;

	// what the read-out draws (copied at the screen's rate)
	dsp::ClockDivider dispDivider;
	float scopeZ[96] = {};
	float scopeTh = 0.f, scopePos = 0.f, scopeVdd = 12.f, dispHz = 0.f;

	float potVolts() const { return rangeIdx == 0 ? 5.f : 12.f; }

	struct PotQuantity : ParamQuantity {
		float getDisplayValue() override {
			AccrualModule* m = dynamic_cast<AccrualModule*>(module);
			return getValue() * (m ? m->potVolts() : 5.f);
		}
		void setDisplayValue(float v) override {
			AccrualModule* m = dynamic_cast<AccrualModule*>(module);
			setValue(v / (m ? m->potVolts() : 5.f));
		}
	};

	AccrualModule() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam<PotQuantity>(TUNE_PARAM, -1.f, 1.f, 0.f, "Tune (P1 pot, 100k into the summing node)", " V");
		configParam<PotQuantity>(FINE_PARAM, -1.f, 1.f, 0.f, "Fine (P2 pot, 1M into the summing node)", " V");
		configParam(PW_PARAM, 0.f, 1.f, 0.5f, "Pulse width (PW pot, volts at R13)", "%", 0.f, 100.f);
		configInput(CV1_INPUT, "CV 1 (100k, 1 V/oct once trimmed)");
		configInput(CV2_INPUT, "CV 2 (100k, 1 V/oct once trimmed)");
		configInput(PWM_INPUT, "Pulse-width CV (Q3 follower; open when unpatched, as on the board)");
		configOutput(SAW_OUTPUT, "Saw (C2 220n, 7 Hz high-pass)");
		configOutput(PULSE_OUTPUT, "Pulse (C3 220n, 7 Hz high-pass, 0 to VDD before the coupling)");
		dispDivider.setDivision(1024);
		r21 = (float)accrual::calibrateTrim(true, true);
		r21Set = true;
		plant = plantFor(vddIdx, cinIdx);
		onSampleRateChange({APP->engine->getSampleRate(), 0.f});
	}

	/** Called from the UI (or the patch loader): builds the plant there and hands it over; the audio
	 *  thread swaps it in at the top of its next block, so the voices are never touched from two. */
	void setPlant() {
		const accrual::Plant* p = plantFor(vddIdx, cinIdx);
		if (p)
			pendingPlant = p;
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		(void) e;
		resetVoices();
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		resetVoices();
	}

	void resetVoices() {
		const accrual::Plant* p = plant.load();
		for (int c = 0; c < kVoices; c++) {
			if (p)
				voice[c].setPlant(p);
			pwm[c] = accrual::PwmFront();
			lastPwm[c] = -1000.f;
			pwmWas[c] = false;
		}
		lastPw = -1.f;
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "vdd", json_integer(vddIdx));
		json_object_set_new(root, "cin", json_integer(cinIdx));
		json_object_set_new(root, "range", json_integer(rangeIdx));
		json_object_set_new(root, "r21", json_real(r21));
		return root;
	}

	void dataFromJson(json_t* root) override {
		json_t* j;
		if ((j = json_object_get(root, "vdd"))) vddIdx = clamp((int) json_integer_value(j), 0, 3);
		if ((j = json_object_get(root, "cin"))) cinIdx = clamp((int) json_integer_value(j), 0, 3);
		if ((j = json_object_get(root, "range"))) rangeIdx = clamp((int) json_integer_value(j), 0, 1);
		if ((j = json_object_get(root, "r21"))) r21 = clamp((float) json_number_value(j), 0.f, 1000.f);
		setPlant();
	}

	void process(const ProcessArgs& args) override {
		if (const accrual::Plant* np = pendingPlant.exchange(nullptr)) {
			plant = np;
			resetVoices();
		}
		const accrual::Plant* P = plant.load();
		if (!P) {
			outputs[SAW_OUTPUT].setVoltage(0.f);
			outputs[PULSE_OUTPUT].setVoltage(0.f);
			return;
		}
		using namespace accrual;
		const bool c1 = inputs[CV1_INPUT].isConnected(), c2 = inputs[CV2_INPUT].isConnected();
		const bool pw = inputs[PWM_INPUT].isConnected();
		int n = 1;
		if (c1) n = std::max(n, inputs[CV1_INPUT].getChannels());
		if (c2) n = std::max(n, inputs[CV2_INPUT].getChannels());
		if (pw) n = std::max(n, inputs[PWM_INPUT].getChannels());
		channels = n;
		const double tune = params[TUNE_PARAM].getValue() * potVolts();
		const double fine = params[FINE_PARAM].getValue() * potVolts();
		const float pot = params[PW_PARAM].getValue();
		// the summing node's conductances: the two pots always, the jacks only when patched
		const double g = 1.0 / R1 + 1.0 / R2 + (c1 ? 1.0 / R3 : 0.0) + (c2 ? 1.0 / R4 : 0.0)
			+ 1.0 / (r21 + R6);
		const double base = tune / R1 + fine / R2;
		for (int c = 0; c < n; c++) {
			double s = base;
			if (c1) s += inputs[CV1_INPUT].getPolyVoltage(c) / R3;
			if (c2) s += inputs[CV2_INPUT].getPolyVoltage(c) / R4;
			Voice& v = voice[c];
			v.front.solve(s, g);
			const float vp = pw ? inputs[PWM_INPUT].getPolyVoltage(c) : 0.f;
			if (vp != lastPwm[c] || pot != lastPw || pw != pwmWas[c]) {
				double qe = pwm[c].solve(P->vm, vp, pw);
				zth[c] = pulseThreshold(P->vm, pot, qe);
				lastPwm[c] = vp; pwmWas[c] = pw;
			}
			double saw, pulse;
			v.process(args.sampleTime, v.front.ibase, v.front.vb, zth[c], saw, pulse);
			outputs[SAW_OUTPUT].setVoltage(clamp((float) saw, -12.f, 12.f), c);
			outputs[PULSE_OUTPUT].setVoltage(clamp((float) pulse, -12.f, 12.f), c);
		}
		lastPw = pot;
		outputs[SAW_OUTPUT].setChannels(n);
		outputs[PULSE_OUTPUT].setChannels(n);

		if (dispDivider.process()) {
			const Voice& v = voice[0];
			dispHz = (float) v.frequency();
			double dvb = v.front.vb - P->vb0;
			double vaf = bc550c().vaf;
			double ic = (v.front.ibase + P->leak / P->fLeak) * (1.0 + (P->uTrip - v.front.vb) / vaf);
			double dur, vc, vup; const double* path;
			P->resetAt(ic, dur, vc, vup, path);
			double ph0 = P->psi(vc, dvb), ph1 = P->psi(vup, dvb);
			double dNow = R8 * v.front.ibase * P->fbar;
			for (int k = 0; k < 96; k++) {
				double ph = ph0 + (ph1 - ph0) * k / 95.0;
				scopeZ[k] = (float) P->zOfW(P->wOfPsi(ph, dvb) + dNow);
			}
			scopePos = ph1 > ph0 ? (float) std::max(0.0, std::min(1.0, (v.phi - ph0) / (ph1 - ph0))) : 0.f;
			scopeTh = (float) zth[0];
			scopeVdd = (float) P->vdd;
		}
	}
};

/** The read-out: the board's sawtooth node over one cycle, the pulse comparator's threshold across
 *  it (PULSE is high below the line), a marker where the cycle is now. The pitch, FINE and PW on
 *  its edge are controls. */
struct AccrualDisplay : LedDisplay {
	AccrualModule* module = NULL;

	static void cell(NVGcontext* vg, const Rect& f, const char* tag, const std::string& v, NVGcolor ink) {
		const Rect c = panel::inGlass(f);
		const float base = c.pos.y + c.size.y * 0.74f;
		const panel::TextStyle TAG(panel::Face::Mono, 6.f, panel::SAGE, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
		const panel::TextStyle VAL(panel::Face::Mono, 8.f, ink, NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE);
		panel::text(vg, TAG, c.pos.x + 2.f, base, tag);
		panel::text(vg, VAL, c.pos.x + c.size.x - 2.f, base, v);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) {
			LedDisplay::drawLayer(args, layer);
			return;
		}
		NVGcontext* vg = args.vg;
		using M = AccrualModule;
		const float hz = module ? module->dispHz : 440.f;
		std::string hzs = hz <= 0.f ? "--" : (hz < 1000.f ? string::f("%.1f", hz) : string::f("%.2fk", hz / 1000.f));
		cell(vg, panel::FIELD_TUNE_HZ, "F", hzs + " HZ", panel::LIME);
		const float fine = module ? module->params[M::FINE_PARAM].getValue() * module->potVolts() : 0.f;
		cell(vg, panel::FIELD_FINE_FIELD, "FINE", string::f("%+.2fV", fine), panel::LIME);
		const float pw = module ? module->params[M::PW_PARAM].getValue() : 0.5f;
		cell(vg, panel::FIELD_PW_FIELD, "PW", string::f("%.0f%%", pw * 100.f), panel::LIME);

		const Rect top = panel::inGlass(panel::FIELD_TUNE_HZ), bot = panel::inGlass(panel::FIELD_FINE_FIELD);
		const float x0 = top.pos.x + 2.f, x1 = box.size.x - 4.f;
		const float y0 = top.pos.y + top.size.y + 3.f, y1 = bot.pos.y - 3.f;
		const float vmax = module ? module->scopeVdd : 12.f;
		auto Y = [&](float z) { return y1 - (y1 - y0) * clamp(z / vmax, 0.f, 1.f); };
		// the rails and the mid-line, faintly
		for (float z : {0.f, 0.5f * vmax, vmax}) {
			nvgBeginPath(vg); nvgRect(vg, x0, Y(z) - 0.3f, x1 - x0, 0.6f);
			nvgFillColor(vg, panel::alpha(panel::SAGE, 0.15f)); nvgFill(vg);
		}
		float z[96];
		for (int k = 0; k < 96; k++) {
			if (module) z[k] = module->scopeZ[k];
			else { float t = k / 95.f; z[k] = 3.5f + t * 4.8f; }
		}
		const float th = module ? module->scopeTh : 6.f;
		// PULSE high below the threshold: shade that span of the ramp
		nvgBeginPath(vg);
		bool open = false;
		for (int k = 0; k < 96; k++) {
			float x = x0 + (x1 - x0) * k / 95.f;
			if (z[k] < th) {
				nvgRect(vg, x, Y(th), (x1 - x0) / 95.f + 0.3f, Y(z[k]) - Y(th));
				open = true;
			}
		}
		(void) open;
		nvgFillColor(vg, panel::alpha(panel::MINT, 0.22f)); nvgFill(vg);
		nvgBeginPath(vg); nvgRect(vg, x0, Y(th) - 0.4f, x1 - x0, 0.8f);
		nvgFillColor(vg, panel::alpha(panel::CLAY, 0.8f)); nvgFill(vg);
		// the saw
		nvgBeginPath(vg);
		for (int k = 0; k < 96; k++) {
			float x = x0 + (x1 - x0) * k / 95.f;
			if (k == 0) nvgMoveTo(vg, x, Y(z[k])); else nvgLineTo(vg, x, Y(z[k]));
		}
		nvgStrokeColor(vg, panel::LIME); nvgStrokeWidth(vg, 1.1f); nvgStroke(vg);
		if (module) {
			const float px = x0 + (x1 - x0) * module->scopePos;
			nvgBeginPath(vg); nvgRect(vg, px - 0.4f, y0, 0.8f, y1 - y0);
			nvgFillColor(vg, panel::alpha(panel::PAPER, 0.5f)); nvgFill(vg);
		}
	}
};

struct R21Quantity : Quantity {
	AccrualModule* module;
	R21Quantity(AccrualModule* m) : module(m) {}
	void setValue(float v) override { module->r21 = clamp(v, 0.f, 1000.f); }
	float getValue() override { return module->r21; }
	float getMinValue() override { return 0.f; }
	float getMaxValue() override { return 1000.f; }
	float getDefaultValue() override { return (float) accrual::calibrateTrim(true, true); }
	float getDisplayValue() override { return getValue(); }
	void setDisplayValue(float v) override { setValue(v); }
	std::string getLabel() override { return "R21 v/oct trimmer"; }
	std::string getUnit() override { return " ohm"; }
};

struct R21Slider : ui::Slider {
	R21Slider(AccrualModule* m) {
		quantity = new R21Quantity(m);
		box.size.x = 220.f;
	}
	~R21Slider() { delete quantity; }
};

struct AccrualWidget : ModuleWidget {
	AccrualWidget(AccrualModule* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Accrual.svg")));
		panel::addScrews(this);
		panel::addLabels(this);

		using M = AccrualModule;
		auto at = [](Vec v) { return panel::mm(v.x, v.y); };

		AccrualDisplay* display = new AccrualDisplay;
		display->module = module;
		display->box.pos = panel::mm(panel::GLASS_X, panel::GLASS_Y);
		display->box.size = panel::mm(panel::GLASS_W, panel::GLASS_H);
		addChild(display);
		addParam(panel::createField<panel::ScreenKnob>(panel::FIELD_TUNE_HZ, module, M::TUNE_PARAM));
		addParam(panel::createField<panel::ScreenKnob>(panel::FIELD_FINE_FIELD, module, M::FINE_PARAM));
		addParam(panel::createField<panel::ScreenKnob>(panel::FIELD_PW_FIELD, module, M::PW_PARAM));

		addParam(createParamCentered<RoundLargeBlackKnob>(at(panel::TUNE_POS), module, M::TUNE_PARAM));
		addInput(createInputCentered<panel::PortIn>(at(panel::CV1_POS), module, M::CV1_INPUT));
		addInput(createInputCentered<panel::PortIn>(at(panel::CV2_POS), module, M::CV2_INPUT));
		addInput(createInputCentered<panel::PortIn>(at(panel::PWM_POS), module, M::PWM_INPUT));
		addOutput(createOutputCentered<panel::PortOut>(at(panel::SAW_POS), module, M::SAW_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(at(panel::PULSE_POS), module, M::PULSE_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		AccrualModule* m = dynamic_cast<AccrualModule*>(module);
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Accrual"));
		menu->addChild(new R21Slider(m));
		menu->addChild(createMenuItem("Calibrate R21 to 1 V/oct (CV1 and CV2 patched)", "",
			[=]() { m->r21 = (float) accrual::calibrateTrim(true, true); }));
		static const std::vector<std::string> vdd = { "9 V", "10 V", "12 V (VCC, as drawn)", "15 V" };
		menu->addChild(createIndexSubmenuItem("CD4069 supply VDD", vdd,
			[=]() { return m->vddIdx; },
			[=](int i) { m->vddIdx = i; m->setPlant(); }));
		static const std::vector<std::string> cin = { "5 pF", "10 pF (datasheet typical)", "15 pF (datasheet maximum)", "20 pF" };
		menu->addChild(createIndexSubmenuItem("CD4069 input capacitance", cin,
			[=]() { return m->cinIdx; },
			[=](int i) { m->cinIdx = i; m->setPlant(); }));
		static const std::vector<std::string> range = { "+-5 V", "+-12 V (across the rails)" };
		menu->addChild(createIndexSubmenuItem("Tune and Fine pot range", range,
			[=]() { return m->rangeIdx; },
			[=](int i) { m->rangeIdx = i; }));
	}
};

Model* modelAccrual = createModel<AccrualModule, AccrualWidget>("Accrual");

#include "../plugin.hpp"
#include "Panel.hpp"
#include "DrumSeq.hpp"

#include <atomic>
#include <memory>
#include <sstream>

// Payment Schedule: an eight-track drum sequencer with the grid on its screen (see
// DrumSeq.hpp for the engine, docs/PaymentSchedule.md for the manual). The screen's
// top two rows are fields on params -- the lane keys, RUN, RST, the pattern, the
// selected track's length and rate, the page and the swing -- and the rest of it is
// the grid, drawn and edited here.
//
// Patches saved before 2026-10 are of the Baby8 / 4031 / varimode module this replaced:
// their params mean nothing to this one, so they are set back to defaults on load and
// the patch gets the starter beat.

namespace D = drumseq;

struct PaymentSchedule : Module {
	enum ParamId {
		ENUMS(LANE_PARAM, D::kNumLanes),
		RUN_PARAM, RESET_PARAM,
		PATTERN_PARAM, TRACK_PARAM, LENGTH_PARAM, DIV_PARAM, PAGE_PARAM, SWING_PARAM,
		PARAMS_LEN
	};
	enum InputId { CLOCK_INPUT, RESET_INPUT, RUN_INPUT, PATTERN_INPUT, INPUTS_LEN };
	enum OutputId {
		ENUMS(TRIG_OUTPUT, D::kTracks), ENUMS(VEL_OUTPUT, D::kTracks),
		ALL_OUTPUT, CV_OUTPUT, EOC_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId { LIGHTS_LEN };

	D::DrumSeq seq;
	dsp::SchmittTrigger clockIn, resetIn, runIn;
	dsp::BooleanTrigger laneBtn[D::kNumLanes], runBtn, rstBtn;
	dsp::ClockDivider ctrlDiv;

	// The UI's view: which lane the grid shows. Set by the lane keys.
	std::atomic<int> lane{D::kLaneGate};
	bool running = true;
	bool rstHeld = false;
	// The selected track's settings as last written to the knobs (see syncTrack).
	int trkLast = -1, patLast = -1, lenLast = -1, divLast = -1;

	PaymentSchedule() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		for (int i = 0; i < D::kNumLanes; i++)
			configButton(LANE_PARAM + i, std::string("Show and edit ") + D::laneNames[i]);
		configButton(RUN_PARAM, "Run / stop");
		configButton(RESET_PARAM, "Reset: the next clock plays step 1");
		configParam(PATTERN_PARAM, 0.f, D::kPatterns - 1, 0.f, "Pattern", "", 0.f, 1.f, 1.f)->snapEnabled = true;
		configSwitch(TRACK_PARAM, 0.f, D::kTracks - 1, 0.f, "Track", {"1", "2", "3", "4", "5", "6", "7", "8"});
		configParam(LENGTH_PARAM, 1.f, D::kSteps, D::kPage, "Track length", " steps")->snapEnabled = true;
		std::vector<std::string> divs(D::divNames, D::divNames + D::kNumDivs);
		configSwitch(DIV_PARAM, 0.f, D::kNumDivs - 1, D::kDivOne, "Track rate (steps per clock)", divs);
		configSwitch(PAGE_PARAM, 0.f, 3.f, 0.f, "Steps on screen", {"1-16", "17-32", "33-48", "49-64"});
		configParam(SWING_PARAM, 50.f, 75.f, 50.f, "Swing", "%")->snapEnabled = true;

		configInput(CLOCK_INPUT, "Clock (one step a rising edge, at 1x)");
		configInput(RESET_INPUT, "Reset (the next clock plays step 1)");
		configInput(RUN_INPUT, "Run gate (high runs; overrides RUN)");
		configInput(PATTERN_INPUT, "Pattern select, 0-10 V across 16");
		for (int t = 0; t < D::kTracks; t++) {
			configOutput(TRIG_OUTPUT + t, string::f("Track %d trigger", t + 1));
			configOutput(VEL_OUTPUT + t, string::f("Track %d velocity (0-10 V, held)", t + 1));
		}
		configOutput(ALL_OUTPUT, "Every track's trigger (8 channels)");
		configOutput(CV_OUTPUT, "Every track's CV lane (8 channels, held from each hit)");
		configOutput(EOC_OUTPUT, "End of track 1's loop");
		configBypass(CLOCK_INPUT, EOC_OUTPUT);

		ctrlDiv.setDivision(32);
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override { seq.setSampleRate(e.sampleRate); }

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		seq = D::DrumSeq();
		seq.setSampleRate(APP->engine->getSampleRate());
		running = true;
		trkLast = patLast = -1;
	}

	// The pattern the screen shows and edits: the one asked for (it plays from the
	// next bar), so the next one can be written while this one plays.
	int viewPattern() const { return seq.want; }
	D::Track& selTrack() { return seq.pat[viewPattern()].t[(int)params[TRACK_PARAM].getValue()]; }

	// LEN and RATE are the selected track's: a new track or pattern moves them to it,
	// and moving them writes the track.
	void syncTrack() {
		int t = (int)params[TRACK_PARAM].getValue();
		int p = viewPattern();
		D::Track& tr = seq.pat[p].t[t];
		if (t != trkLast || p != patLast) {
			trkLast = t;
			patLast = p;
			params[LENGTH_PARAM].setValue(tr.length);
			params[DIV_PARAM].setValue(tr.div);
		}
		else {
			int len = (int)params[LENGTH_PARAM].getValue();
			int dv = (int)params[DIV_PARAM].getValue();
			if (len != tr.length && len != lenLast) tr.length = (uint8_t)clamp(len, 1, (int)D::kSteps);
			if (dv != tr.div && dv != divLast) tr.div = (uint8_t)clamp(dv, 0, D::kNumDivs - 1);
			// an edit elsewhere (the menu, undo) moved the track: the knobs follow it
			if (len != tr.length) params[LENGTH_PARAM].setValue(tr.length);
			if (dv != tr.div) params[DIV_PARAM].setValue(tr.div);
		}
		lenLast = (int)params[LENGTH_PARAM].getValue();
		divLast = (int)params[DIV_PARAM].getValue();
	}

	void process(const ProcessArgs& args) override {
		if (ctrlDiv.process()) {
			for (int i = 0; i < D::kNumLanes; i++)
				if (laneBtn[i].process(params[LANE_PARAM + i].getValue() > 0.f)) lane = i;
			if (inputs[RUN_INPUT].isConnected()) {
				runIn.process(inputs[RUN_INPUT].getVoltage(), 0.1f, 1.f);
				running = runIn.isHigh();
			}
			else if (runBtn.process(params[RUN_PARAM].getValue() > 0.f))
				running = !running;
			if (inputs[PATTERN_INPUT].isConnected()) {
				int p = clamp((int)(inputs[PATTERN_INPUT].getVoltage() * (D::kPatterns / 10.f)), 0, D::kPatterns - 1);
				seq.want = p;
				params[PATTERN_PARAM].setValue((float)p);
			}
			else
				seq.want = clamp((int)params[PATTERN_PARAM].getValue(), 0, D::kPatterns - 1);
			seq.swing = params[SWING_PARAM].getValue() * 0.01f;
			syncTrack();
		}
		bool rst = rstBtn.process(params[RESET_PARAM].getValue() > 0.f);
		rstHeld = params[RESET_PARAM].getValue() > 0.f;
		bool clk = clockIn.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 2.f);
		rst = resetIn.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 2.f) || rst;
		seq.process(clk, rst, running);

		outputs[ALL_OUTPUT].setChannels(D::kTracks);
		outputs[CV_OUTPUT].setChannels(D::kTracks);
		for (int t = 0; t < D::kTracks; t++) {
			float g = seq.trig[t] ? 10.f : 0.f;
			outputs[TRIG_OUTPUT + t].setVoltage(g);
			outputs[ALL_OUTPUT].setVoltage(g, t);
			outputs[VEL_OUTPUT + t].setVoltage(seq.ts[t].vel);
			outputs[CV_OUTPUT].setVoltage(seq.ts[t].cv, t);
		}
		outputs[EOC_OUTPUT].setVoltage(seq.eocHigh() ? 10.f : 0.f);
	}

	// ---- patch -------------------------------------------------------------

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "format", json_integer(2));
		json_object_set_new(root, "running", json_boolean(running));
		json_object_set_new(root, "lane", json_integer(lane.load()));
		json_object_set_new(root, "switchOnBar", json_boolean(seq.switchOnBar));
		json_object_set_new(root, "trigMs", json_real(seq.trigSeconds * 1000.f));
		json_t* mutes = json_array();
		for (int t = 0; t < D::kTracks; t++) json_array_append_new(mutes, json_boolean(seq.mute[t]));
		json_object_set_new(root, "mute", mutes);
		json_t* pats = json_array();
		char buf[4 + 12 * D::kSteps + 1];
		for (int p = 0; p < D::kPatterns; p++) {
			if (seq.pat[p].empty()) { json_array_append_new(pats, json_null()); continue; }
			json_t* tr = json_array();
			for (int t = 0; t < D::kTracks; t++) {
				D::trackToText(seq.pat[p].t[t], buf);
				json_array_append_new(tr, json_string(buf));
			}
			json_array_append_new(pats, tr);
		}
		json_object_set_new(root, "patterns", pats);
		return root;
	}

	void dataFromJson(json_t* root) override {
		if (!json_object_get(root, "format")) {
			// the Baby8 module's patch: its params were other controls entirely
			for (int i = 0; i < PARAMS_LEN; i++)
				if (paramQuantities[i]) params[i].setValue(paramQuantities[i]->getDefaultValue());
			trkLast = patLast = -1;
			return;
		}
		if (json_t* j = json_object_get(root, "running")) running = json_boolean_value(j);
		if (json_t* j = json_object_get(root, "lane")) lane = clamp((int)json_integer_value(j), 0, D::kNumLanes - 1);
		if (json_t* j = json_object_get(root, "switchOnBar")) seq.switchOnBar = json_boolean_value(j);
		if (json_t* j = json_object_get(root, "trigMs")) seq.trigSeconds = clamp((float)json_number_value(j), 0.1f, 500.f) * 0.001f;
		if (json_t* m = json_object_get(root, "mute"))
			for (int t = 0; t < D::kTracks && t < (int)json_array_size(m); t++)
				seq.mute[t] = json_boolean_value(json_array_get(m, t));
		if (json_t* pats = json_object_get(root, "patterns")) {
			for (int p = 0; p < D::kPatterns; p++) {
				seq.pat[p].clear();
				json_t* tr = json_array_get(pats, p);
				if (!tr || !json_is_array(tr)) continue;
				for (int t = 0; t < D::kTracks && t < (int)json_array_size(tr); t++) {
					const char* s = json_string_value(json_array_get(tr, t));
					if (s) D::trackFromText(s, seq.pat[p].t[t]);
				}
			}
		}
		trkLast = patLast = -1;
	}
};


// ---------------------------------------------------------------------------
// Undo: a whole pattern, before and after an edit.

struct PatternUndo : history::ModuleAction {
	int pattern;
	D::Pattern before, after;
	PatternUndo(PaymentSchedule* m, int p, const D::Pattern& was, const std::string& what) : pattern(p), before(was) {
		moduleId = m->id;
		name = "Payment Schedule: " + what;
		after = m->seq.pat[p];
	}
	PaymentSchedule* find() {
		return APP && APP->engine ? dynamic_cast<PaymentSchedule*>(APP->engine->getModule(moduleId)) : NULL;
	}
	void undo() override { if (PaymentSchedule* m = find()) m->seq.pat[pattern] = before; }
	void redo() override { if (PaymentSchedule* m = find()) m->seq.pat[pattern] = after; }
};

static bool samePattern(const D::Pattern& a, const D::Pattern& b) {
	for (int t = 0; t < D::kTracks; t++) {
		if (a.t[t].length != b.t[t].length || a.t[t].div != b.t[t].div) return false;
		for (int i = 0; i < D::kSteps; i++) if (!(a.t[t].s[i] == b.t[t].s[i])) return false;
	}
	return true;
}

// The clipboard: the system's, as text, so a pattern goes between instances and patches.
static const char* const kClipTag = "MoonTechnologies.PaymentSchedule.pattern";

static void copyPattern(const D::Pattern& p) {
	std::string s = kClipTag;
	char buf[4 + 12 * D::kSteps + 1];
	for (int t = 0; t < D::kTracks; t++) {
		D::trackToText(p.t[t], buf);
		s += "\n";
		s += buf;
	}
	glfwSetClipboardString(APP->window->win, s.c_str());
}

static bool pastePattern(D::Pattern& p) {
	const char* c = glfwGetClipboardString(APP->window->win);
	if (!c) return false;
	std::istringstream in(c);
	std::string line;
	if (!std::getline(in, line) || line != kClipTag) return false;
	D::Pattern got;
	for (int t = 0; t < D::kTracks; t++) {
		if (!std::getline(in, line) || !D::trackFromText(line.c_str(), got.t[t])) return false;
	}
	p = got;
	return true;
}


// ---------------------------------------------------------------------------
// The screen: the fields' captions and values, and the grid.

struct DrumDisplay : widget::OpaqueWidget {
	PaymentSchedule* module = NULL;
	D::DrumSeq preview;					// what the library browser shows

	// geometry, in this widget's pixels (the glass)
	const float pad = 3.f;
	Rect local(const Rect& f) const {
		Rect r = panel::mmRect(f.pos.x, f.pos.y, f.size.x, f.size.y);
		r.pos = r.pos.minus(box.pos);
		return r;
	}
	float bodyTop() const { Rect r = local(panel::FIELD_PATT); return r.pos.y + r.size.y + 2.f; }
	float labelW() const { return 24.f; }
	float headH() const { return 8.f; }
	float gridX0() const { return pad + labelW(); }
	float gridX1() const { return box.size.x - pad; }
	float cellW() const { return (gridX1() - gridX0()) / D::kPage; }
	float rowY0() const { return bodyTop() + headH(); }
	float rowH() const { return (box.size.y - pad - rowY0()) / D::kTracks; }

	D::DrumSeq& seq() { return module ? module->seq : preview; }
	int lane() const { return module ? module->lane.load() : D::kLaneGate; }
	int page() const { return module ? (int)module->params[PaymentSchedule::PAGE_PARAM].getValue() : 0; }
	int selTrack() const { return module ? (int)module->params[PaymentSchedule::TRACK_PARAM].getValue() : 0; }
	int viewPat() { return seq().want; }

	// The track and step under p; false if it is not on the grid.
	bool cellAt(Vec p, int& t, int& s) const {
		if (p.x < gridX0() || p.x >= gridX1() || p.y < rowY0()) return false;
		t = (int)std::floor((p.y - rowY0()) / rowH());
		s = page() * D::kPage + (int)std::floor((p.x - gridX0()) / cellW());
		return t >= 0 && t < D::kTracks && s >= 0 && s < D::kSteps;
	}
	// A value for `lane` from the height of p in track t's row.
	int valueAt(int ln, int t, float y) const {
		float top = rowY0() + t * rowH() + 1.5f, bot = rowY0() + (t + 1) * rowH() - 1.5f;
		float f = clamp((bot - y) / (bot - top), 0.f, 1.f);
		int lo = D::Step::laneMin(ln), hi = D::Step::laneMax(ln);
		return lo + (int)std::round(f * (hi - lo));
	}

	// ---- editing ------------------------------------------------------------

	enum Drag { NONE, PAINT, VALUE };
	Drag drag = NONE;
	int paintOn = 0;
	int dragTrack = -1;
	Vec dragPos;
	D::Pattern before;
	bool haveBefore = false;

	void begin() {
		before = seq().pat[viewPat()];
		haveBefore = true;
	}
	void finish(const char* what) {
		if (!module || !haveBefore) return;
		haveBefore = false;
		int p = viewPat();
		if (samePattern(before, seq().pat[p])) return;
		APP->history->push(new PatternUndo(module, p, before, what));
	}
	// A one-shot edit from a menu: the whole of it one undo step.
	template <typename F> void edit(const char* what, F f) {
		begin();
		f(seq().pat[viewPat()]);
		finish(what);
	}

	void apply(Vec p) {
		int t, s;
		if (!cellAt(p, t, s)) return;
		D::Step& st = seq().pat[viewPat()].t[t].s[s];
		if (drag == PAINT) st.on = (uint8_t)paintOn;
		else if (drag == VALUE && t == dragTrack) {
			int ln = lane();
			st.set(ln, valueAt(ln, t, p.y));
			if (ln != D::kLaneCv && !st.on) st.on = 1;		// a value given is a hit asked for
		}
	}

	void onButton(const ButtonEvent& e) override {
		OpaqueWidget::onButton(e);
		if (!module || e.action != GLFW_PRESS) return;
		Vec p = e.pos;
		if (p.y < bodyTop()) return;
		e.consume(this);
		// the label column: select a track, its M box mutes it
		if (p.x < gridX0() && p.y >= rowY0()) {
			int t = (int)std::floor((p.y - rowY0()) / rowH());
			if (t < 0 || t >= D::kTracks) return;
			if (e.button == GLFW_MOUSE_BUTTON_RIGHT) { trackMenu(t); return; }
			if (e.button != GLFW_MOUSE_BUTTON_LEFT) return;
			if (p.x >= pad + labelW() - 11.f) module->seq.mute[t] = !module->seq.mute[t];
			else module->params[PaymentSchedule::TRACK_PARAM].setValue((float)t);
			return;
		}
		int t, s;
		if (!cellAt(p, t, s)) return;
		D::Step& st = seq().pat[viewPat()].t[t].s[s];
		if (e.button == GLFW_MOUSE_BUTTON_RIGHT) { stepMenu(t, s); return; }
		if (e.button != GLFW_MOUSE_BUTTON_LEFT) return;
		module->params[PaymentSchedule::TRACK_PARAM].setValue((float)t);
		begin();
		dragPos = p;
		dragTrack = t;
		if (lane() == D::kLaneGate) {
			if ((e.mods & RACK_MOD_MASK) == GLFW_MOD_SHIFT && st.on) {
				st.ratchet = (uint8_t)(st.ratchet % 4 + 1);		// shift-click: the next ratchet
				drag = NONE;
				return;
			}
			drag = PAINT;
			paintOn = !st.on;
		}
		else
			drag = VALUE;
		apply(p);
	}

	void onDragStart(const DragStartEvent& e) override {
		if (e.button != GLFW_MOUSE_BUTTON_LEFT) return;
		OpaqueWidget::onDragStart(e);
	}
	void onDragMove(const DragMoveEvent& e) override {
		if (drag == NONE) return;
		dragPos = dragPos.plus(e.mouseDelta.div(getAbsoluteZoom()));
		apply(dragPos);
	}
	void onDragEnd(const DragEndEvent& e) override {
		const char* what = drag == PAINT ? "paint steps" : "set steps";
		drag = NONE;
		finish(what);
		OpaqueWidget::onDragEnd(e);
	}

	void stepMenu(int t, int s) {
		ui::Menu* menu = createMenu();
		menu->addChild(createMenuLabel(string::f("Track %d, step %d", t + 1, s + 1)));
		auto at = [=]() -> D::Step& { return seq().pat[viewPat()].t[t].s[s]; };
		menu->addChild(createSubmenuItem("Ratchet", string::f("x%d", at().ratchet), [=](ui::Menu* sub) {
			for (int r = 1; r <= 4; r++)
				sub->addChild(createCheckMenuItem(r == 1 ? "Single" : string::f("x%d", r), "",
					[=]() { return at().ratchet == r; },
					[=]() { edit("ratchet", [=](D::Pattern& p) { p.t[t].s[s].ratchet = (uint8_t)r; p.t[t].s[s].on = 1; }); }));
		}));
		menu->addChild(createSubmenuItem("Chance", string::f("%d%%", at().chance), [=](ui::Menu* sub) {
			for (int c : {100, 75, 50, 25, 10, 0})
				sub->addChild(createCheckMenuItem(string::f("%d%%", c), "",
					[=]() { return at().chance == c; },
					[=]() { edit("chance", [=](D::Pattern& p) { p.t[t].s[s].chance = (uint8_t)c; }); }));
		}));
		menu->addChild(createMenuItem("Clear this step", "", [=]() {
			edit("clear step", [=](D::Pattern& p) { p.t[t].s[s] = D::Step(); });
		}));
		menu->addChild(createMenuItem("Set the track's length to end here", "", [=]() {
			edit("length", [=](D::Pattern& p) { p.t[t].length = (uint8_t)(s + 1); });
		}));
	}

	void trackMenu(int t) {
		ui::Menu* menu = createMenu();
		menu->addChild(createMenuLabel(string::f("Track %d", t + 1)));
		menu->addChild(createBoolPtrMenuItem("Mute", "", &module->seq.mute[t]));
		menu->addChild(createMenuItem("Rotate one step later", "", [=]() { edit("rotate", [=](D::Pattern& p) { p.t[t].rotate(1); }); }));
		menu->addChild(createMenuItem("Rotate one step earlier", "", [=]() { edit("rotate", [=](D::Pattern& p) { p.t[t].rotate(-1); }); }));
		menu->addChild(createMenuItem("Randomize", "", [=]() {
			edit("randomize track", [=](D::Pattern& p) { D::randomBeat(p, module->seq.dice, 1u << t); });
		}));
		menu->addChild(createMenuItem("Clear", "", [=]() { edit("clear track", [=](D::Pattern& p) { p.t[t].clear(); }); }));
	}

	// ---- drawing ------------------------------------------------------------

	void fieldBox(NVGcontext* vg, const Rect& f, const char* caption, const std::string& value, float lit) {
		Rect r = local(f);
		nvgBeginPath(vg);
		nvgRoundedRect(vg, r.pos.x + 0.4f, r.pos.y + 0.4f, r.size.x - 0.8f, r.size.y - 0.8f, 1.2f);
		nvgFillColor(vg, lit > 0.02f ? panel::alpha(panel::LIME, 0.10f + 0.32f * std::min(lit, 1.f))
			: panel::alpha(panel::SAGE, 0.10f));
		nvgFill(vg);
		float cy = r.pos.y + r.size.y * 0.5f + 0.3f;
		float sz = std::min(7.f, r.size.y * 0.6f);
		if (value.empty()) {
			const panel::TextStyle NAME(panel::Face::Mono, sz, lit > 0.5f ? panel::PAPER : panel::SAGE,
				NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
			panel::text(vg, NAME, r.pos.x + r.size.x * 0.5f, cy, caption);
			return;
		}
		const panel::TextStyle CAP(panel::Face::Mono, sz * 0.85f, panel::SAGE, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		const panel::TextStyle VAL(panel::Face::Mono, sz, panel::PAPER, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
		panel::text(vg, CAP, r.pos.x + 2.f, cy, caption);
		panel::text(vg, VAL, r.pos.x + r.size.x - 2.f, cy, value.c_str());
	}

	void drawFields(NVGcontext* vg) {
		static const Rect* const lanes[D::kNumLanes] = { &panel::FIELD_LN_GATE, &panel::FIELD_LN_VEL,
			&panel::FIELD_LN_CHNC, &panel::FIELD_LN_RTCH, &panel::FIELD_LN_OFFS, &panel::FIELD_LN_CV };
		for (int i = 0; i < D::kNumLanes; i++)
			fieldBox(vg, *lanes[i], D::laneNames[i], "", i == lane() ? 1.f : 0.f);
		fieldBox(vg, panel::FIELD_RUN, "RUN", "", module ? (module->running ? 1.f : 0.f) : 1.f);
		fieldBox(vg, panel::FIELD_RST, "RST", "", module && module->rstHeld ? 1.f : 0.f);
		D::DrumSeq& q = seq();
		std::string pat = string::f("%d", q.want + 1);
		if (q.want != q.cur) pat = string::f("%d>%d", q.cur + 1, q.want + 1);
		fieldBox(vg, panel::FIELD_PATT, "PAT", pat, 0.f);
		int t = selTrack();
		const D::Track& tr = q.pat[q.want].t[t];
		fieldBox(vg, panel::FIELD_TRK, "TRK", string::f("%d", t + 1), 0.f);
		fieldBox(vg, panel::FIELD_LEN, "LEN", string::f("%d", tr.length), 0.f);
		fieldBox(vg, panel::FIELD_DIV, "RATE", D::divNames[tr.div < D::kNumDivs ? tr.div : D::kDivOne], 0.f);
		int pg = page();
		fieldBox(vg, panel::FIELD_PAGE, "STEP", string::f("%d-%d", pg * 16 + 1, pg * 16 + 16), 0.f);
		fieldBox(vg, panel::FIELD_SWNG, "SWG", string::f("%d%%", (int)std::round(q.swing * 100.f)), 0.f);
	}

	void drawGrid(NVGcontext* vg) {
		D::DrumSeq& q = seq();
		const D::Pattern& pat = q.pat[q.want];
		bool playing = q.want == q.cur;
		int ln = lane();
		int pg = page();
		float cw = cellW(), rh = rowH(), x0 = gridX0(), y0 = rowY0();
		float bw = std::max(1.f, cw - 1.5f);

		// the step numbers, and every other beat shaded down the whole grid
		const panel::TextStyle NUM(panel::Face::Mono, 6.f, panel::SAGE, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		nvgBeginPath(vg);
		for (int b = 1; b < 4; b += 2) nvgRect(vg, x0 + b * 4 * cw, bodyTop(), 4 * cw, box.size.y - pad - bodyTop());
		nvgFillColor(vg, panel::alpha(panel::SAGE, 0.06f));
		nvgFill(vg);
		for (int i = 0; i < D::kPage; i += (cw < 10.f ? 2 : 1))
			panel::text(vg, NUM, x0 + (i + 0.5f) * cw, bodyTop() + headH() * 0.5f, string::f("%d", pg * 16 + i + 1));
		const panel::TextStyle LANE(panel::Face::Mono, 6.f, panel::LIME, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		panel::text(vg, LANE, pad, bodyTop() + headH() * 0.5f, D::laneNames[ln]);

		const panel::TextStyle LBL(panel::Face::Mono, std::min(8.f, rh * 0.6f), panel::PAPER, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		const panel::TextStyle MUTE = LBL.sized(std::min(6.5f, rh * 0.5f)).aligned(NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		int sel = selTrack();
		for (int t = 0; t < D::kTracks; t++) {
			const D::Track& tr = pat.t[t];
			float ry = y0 + t * rh, yc = ry + rh * 0.5f;
			float top = ry + 1.5f, bot = ry + rh - 1.5f, h = bot - top;
			bool muted = q.mute[t];
			int len = D::DrumSeq::stepsOf(tr);
			// label: number (selected: lit), and the mute box
			if (t == sel) {
				nvgBeginPath(vg);
				nvgRect(vg, 0.8f, top, 1.6f, h);
				nvgFillColor(vg, panel::LIME);
				nvgFill(vg);
			}
			panel::text(vg, LBL.inked(t == sel ? panel::PAPER : panel::SAGE), pad + 1.f, yc, string::f("%d", t + 1));
			float mx = pad + labelW() - 11.f;
			nvgBeginPath(vg);
			nvgRoundedRect(vg, mx, top + 0.5f, 9.f, h - 1.f, 1.f);
			nvgFillColor(vg, muted ? panel::alpha(panel::CLAY, 0.7f) : panel::alpha(panel::SAGE, 0.12f));
			nvgFill(vg);
			panel::text(vg, MUTE.inked(muted ? panel::PAPER : panel::SAGE), mx + 4.5f, yc, "M");

			NVGcolor ink = muted ? panel::alpha(panel::SAGE, 0.5f) : panel::PAPER;
			int at = playing ? q.ts[t].step : -1;
			// the playing step's backdrop
			if (at >= pg * 16 && at < pg * 16 + 16) {
				nvgBeginPath(vg);
				nvgRect(vg, x0 + (at - pg * 16) * cw, top - 0.5f, bw, h + 1.f);
				nvgFillColor(vg, panel::alpha(panel::LIME, 0.22f));
				nvgFill(vg);
			}
			// past the track's end: a dot per cell
			nvgBeginPath(vg);
			bool dots = false;
			for (int i = 0; i < D::kPage; i++)
				if (pg * 16 + i >= len) { nvgCircle(vg, x0 + i * cw + bw * 0.5f, yc, 0.6f); dots = true; }
			if (dots) { nvgFillColor(vg, panel::alpha(panel::SAGE, 0.35f)); nvgFill(vg); }

			// the cells: one path for the empty ones, then each hit (its own shade)
			nvgBeginPath(vg);
			for (int i = 0; i < D::kPage; i++) {
				int s = pg * 16 + i;
				if (s >= len) continue;
				bool show = ln == D::kLaneCv || tr.s[s].on;
				if (!show || ln == D::kLaneGate) {
					if (!tr.s[s].on) nvgRect(vg, x0 + i * cw, bot - 0.8f, bw, 0.8f);
				}
			}
			nvgFillColor(vg, panel::alpha(panel::SAGE, 0.4f));
			nvgFill(vg);
			for (int i = 0; i < D::kPage; i++) {
				int s = pg * 16 + i;
				if (s >= len) continue;
				const D::Step& st = tr.s[s];
				float x = x0 + i * cw;
				bool hot = s == at;
				NVGcolor c = hot ? panel::LIME : ink;
				if (ln == D::kLaneGate) {
					if (!st.on) continue;
					// a pad, dimmer as its chance falls; ratchets as notches across it
					nvgBeginPath(vg);
					nvgRoundedRect(vg, x, top, bw, h, 1.f);
					nvgFillColor(vg, panel::alpha(c, 0.35f + 0.65f * st.chance / 100.f));
					nvgFill(vg);
					if (st.ratchet > 1) {
						nvgBeginPath(vg);
						for (int r = 1; r < st.ratchet; r++) nvgRect(vg, x + bw * r / st.ratchet - 0.4f, top + 1.f, 0.8f, h - 2.f);
						nvgFillColor(vg, panel::GLASS);
						nvgFill(vg);
					}
					continue;
				}
				if (ln != D::kLaneCv && !st.on) continue;
				int v = st.get(ln), lo = D::Step::laneMin(ln), hi = D::Step::laneMax(ln);
				nvgBeginPath(vg);
				if (ln == D::kLaneOffset) {
					// early below the line, late above
					float mid = (top + bot) * 0.5f, dy = (float)v / hi * (h * 0.5f);
					nvgRect(vg, x, dy >= 0 ? mid - dy : mid, bw, std::max(0.8f, std::fabs(dy)));
				}
				else {
					float f = hi > lo ? (float)(v - lo) / (hi - lo) : 0.f;
					float bh = std::max(0.8f, h * (ln == D::kLaneRatchet ? (float)v / hi : f));
					nvgRect(vg, x, bot - bh, bw, bh);
				}
				nvgFillColor(vg, c);
				nvgFill(vg);
			}
		}
	}

	void draw(const DrawArgs& args) override {
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 3.f);
		nvgFillColor(args.vg, panel::GLASS);
		nvgFill(args.vg);
		nvgStrokeColor(args.vg, panel::RULE);
		nvgStrokeWidth(args.vg, 0.6f);
		nvgStroke(args.vg);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		OpaqueWidget::drawLayer(args, layer);
		if (layer != 1) return;
		drawFields(args.vg);
		drawGrid(args.vg);
	}
};


struct PaymentScheduleWidget : ModuleWidget {
	PaymentScheduleWidget(PaymentSchedule* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/PaymentSchedule.svg")));
		panel::addScrews(this);
		panel::addLabels(this);

		DrumDisplay* display = new DrumDisplay;
		display->module = module;
		display->box = panel::mmRect(panel::GLASS_X, panel::GLASS_Y, panel::GLASS_W, panel::GLASS_H);
		addChild(display);

		// the fields, over the display: each a param, so undo, MIDI-map and typing work
		struct F { const Rect* f; int param; int kind; };	// 0 button, 1 value, 2 select
		const F fields[] = {
			{ &panel::FIELD_LN_GATE, PaymentSchedule::LANE_PARAM + D::kLaneGate, 0 },
			{ &panel::FIELD_LN_VEL, PaymentSchedule::LANE_PARAM + D::kLaneVel, 0 },
			{ &panel::FIELD_LN_CHNC, PaymentSchedule::LANE_PARAM + D::kLaneChance, 0 },
			{ &panel::FIELD_LN_RTCH, PaymentSchedule::LANE_PARAM + D::kLaneRatchet, 0 },
			{ &panel::FIELD_LN_OFFS, PaymentSchedule::LANE_PARAM + D::kLaneOffset, 0 },
			{ &panel::FIELD_LN_CV, PaymentSchedule::LANE_PARAM + D::kLaneCv, 0 },
			{ &panel::FIELD_RUN, PaymentSchedule::RUN_PARAM, 0 },
			{ &panel::FIELD_RST, PaymentSchedule::RESET_PARAM, 0 },
			{ &panel::FIELD_PATT, PaymentSchedule::PATTERN_PARAM, 1 },
			{ &panel::FIELD_TRK, PaymentSchedule::TRACK_PARAM, 2 },
			{ &panel::FIELD_LEN, PaymentSchedule::LENGTH_PARAM, 1 },
			{ &panel::FIELD_DIV, PaymentSchedule::DIV_PARAM, 2 },
			{ &panel::FIELD_PAGE, PaymentSchedule::PAGE_PARAM, 2 },
			{ &panel::FIELD_SWNG, PaymentSchedule::SWING_PARAM, 1 },
		};
		for (const F& f : fields) {
			if (f.kind == 0) addParam(panel::createField<panel::ScreenButton>(*f.f, module, f.param));
			else if (f.kind == 1) addParam(panel::createField<panel::ScreenKnob>(*f.f, module, f.param));
			else addParam(panel::createField<panel::ScreenSelect>(*f.f, module, f.param));
		}

		static const Vec* const TRIG_POS[D::kTracks] = { &panel::TRIG1_POS, &panel::TRIG2_POS, &panel::TRIG3_POS,
			&panel::TRIG4_POS, &panel::TRIG5_POS, &panel::TRIG6_POS, &panel::TRIG7_POS, &panel::TRIG8_POS };
		static const Vec* const VEL_POS[D::kTracks] = { &panel::VEL1_POS, &panel::VEL2_POS, &panel::VEL3_POS,
			&panel::VEL4_POS, &panel::VEL5_POS, &panel::VEL6_POS, &panel::VEL7_POS, &panel::VEL8_POS };
		for (int t = 0; t < D::kTracks; t++) {
			addOutput(createOutputCentered<panel::PortTrigOut>(panel::mm(TRIG_POS[t]->x, TRIG_POS[t]->y), module, PaymentSchedule::TRIG_OUTPUT + t));
			addOutput(createOutputCentered<panel::PortOut>(panel::mm(VEL_POS[t]->x, VEL_POS[t]->y), module, PaymentSchedule::VEL_OUTPUT + t));
		}
		addInput(createInputCentered<panel::PortTrigIn>(panel::mm(panel::CLOCK_IN_POS.x, panel::CLOCK_IN_POS.y), module, PaymentSchedule::CLOCK_INPUT));
		addInput(createInputCentered<panel::PortTrigIn>(panel::mm(panel::RESET_IN_POS.x, panel::RESET_IN_POS.y), module, PaymentSchedule::RESET_INPUT));
		addInput(createInputCentered<panel::PortTrigIn>(panel::mm(panel::RUN_IN_POS.x, panel::RUN_IN_POS.y), module, PaymentSchedule::RUN_INPUT));
		addInput(createInputCentered<panel::PortIn>(panel::mm(panel::PATT_IN_POS.x, panel::PATT_IN_POS.y), module, PaymentSchedule::PATTERN_INPUT));
		addOutput(createOutputCentered<panel::PortTrigOut>(panel::mm(panel::ALL_OUT_POS.x, panel::ALL_OUT_POS.y), module, PaymentSchedule::ALL_OUTPUT));
		addOutput(createOutputCentered<panel::PortOut>(panel::mm(panel::CV_OUT_POS.x, panel::CV_OUT_POS.y), module, PaymentSchedule::CV_OUTPUT));
		addOutput(createOutputCentered<panel::PortTrigOut>(panel::mm(panel::EOC_OUT_POS.x, panel::EOC_OUT_POS.y), module, PaymentSchedule::EOC_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		PaymentSchedule* m = dynamic_cast<PaymentSchedule*>(module);
		if (!m) return;
		DrumDisplay* d = getFirstDescendantOfType<DrumDisplay>();
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Payment Schedule"));
		int p = m->seq.want;
		menu->addChild(createMenuLabel(string::f("Pattern %d", p + 1)));
		menu->addChild(createMenuItem("Copy pattern", "", [=]() { copyPattern(m->seq.pat[p]); }));
		menu->addChild(createMenuItem("Paste pattern", "", [=]() {
			if (d) d->edit("paste pattern", [](D::Pattern& pat) { pastePattern(pat); });
		}));
		menu->addChild(createMenuItem("Randomize pattern", "", [=]() {
			if (d) d->edit("randomize", [=](D::Pattern& pat) { D::randomBeat(pat, m->seq.dice); });
		}));
		menu->addChild(createMenuItem("Rotate one step later", "", [=]() {
			if (d) d->edit("rotate", [](D::Pattern& pat) { pat.rotate(1); });
		}));
		menu->addChild(createMenuItem("Rotate one step earlier", "", [=]() {
			if (d) d->edit("rotate", [](D::Pattern& pat) { pat.rotate(-1); });
		}));
		menu->addChild(createMenuItem("Clear pattern", "", [=]() {
			if (d) d->edit("clear pattern", [](D::Pattern& pat) { pat.clear(); });
		}));
		menu->addChild(new MenuSeparator);
		menu->addChild(createIndexSubmenuItem("Pattern change", {"At the next bar (16 clocks)", "At the next clock"},
			[=]() { return m->seq.switchOnBar ? 0 : 1; },
			[=](int v) { m->seq.switchOnBar = v == 0; }));
		static const float lens[] = { 1.f, 2.f, 5.f, 10.f, 20.f, 50.f, 100.f, 200.f };
		menu->addChild(createSubmenuItem("Trigger length", string::f("%g ms", m->seq.trigSeconds * 1000.f), [=](ui::Menu* sub) {
			for (float ms : lens)
				sub->addChild(createCheckMenuItem(string::f("%g ms", ms), "",
					[=]() { return std::fabs(m->seq.trigSeconds * 1000.f - ms) < 0.01f; },
					[=]() { m->seq.trigSeconds = ms * 0.001f; }));
		}));
	}
};


Model* modelPaymentSchedule = createModel<PaymentSchedule, PaymentScheduleWidget>("PaymentSchedule");

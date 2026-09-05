#include "../plugin.hpp"

#include "Panel.hpp"
#include "Motion.hpp"
#include "Policy.hpp"
#include "Deviate.hpp"
#include "Graph.hpp"
#include "Labels.hpp"
#include "Roles.hpp"
#include "Signal.hpp"

#include <algorithm>
#include <atomic>
#include <functional>
#include <set>
#include <string>
#include <vector>


using namespace upol;


static const float PROBE_SEC = 0.20f;
static const float AUDITION_SEC = 0.25f;
static const char* FILING_NAME = "Uncertainty Policy filing";

static const float MOTION_SEC = 1.80f;   // ~one bar at 120 BPM
static const int MAX_ATTEMPTS = 8;
static const int MOTION_ATTEMPTS = 4;


/** One output port we are listening to, plus the pointer the audio thread reads.

    Rack takes the engine write lock to add or remove a module, and process() runs
    under that lock, so a Module* captured on the UI thread cannot be freed while
    process() is mid-call. The window where it could go stale is between the UI
    thread publishing this list and the audio thread reading it; we close that by
    clearing probeActive before any structural change we ourselves make, and by
    never holding a list across more than one probe window. */
struct Probe {
	rack::engine::Module* mod = nullptr;
	int port = 0;
	PortStats stats;
	PortProfile profile;
	std::string name;
	int64_t moduleId = -1;
};


struct UncertaintyPolicy : Module {
	enum ParamId {
		KNOB_AMOUNT_PARAM,
		CABLE_COUNT_PARAM,
		ROLL_CONTROLS_PARAM,
		ROLL_CABLES_PARAM,
		ROLL_ALL_PARAM,
		REVERT_PARAM,
		// Appended, never inserted: Rack stores param values by index, so
		// slotting these in beside the controls they sit next to on the panel
		// would silently reassign every knob value in every saved patch.
		SPREAD_PARAM,
		BASIS_PARAM,
		SPINE_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		TRIG_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		OUTPUTS_LEN
	};
	enum LightId {
		OK_LIGHT,
		WARN_LIGHT,
		LIGHTS_LEN
	};

	// --- options, persisted ---
	Policy policy;
	std::set<int64_t> lockedModules;

	// --- shared with the audio thread ---
	std::vector<Probe> probes;
	std::atomic<bool> probeActive{false};
	std::atomic<int64_t> probeSamples{0};

	rack::engine::Module* sink = nullptr;
	std::atomic<bool> meterActive{false};
	std::atomic<int64_t> meterSamples{0};
	SinkMeter meter;
	float hpCoeff = 0.f;
	float hpSampleRate = 0.f;

	std::atomic<bool> trigRequest{false};
	dsp::SchmittTrigger trigTrigger;

	UncertaintyPolicy() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(KNOB_AMOUNT_PARAM, 0.f, 1.f, 0.15f, "Knob variance", "%", 0.f, 100.f);
		configParam(CABLE_COUNT_PARAM, 0.f, 8.f, 2.f, "Wire transfers per filing");
		getParamQuantity(CABLE_COUNT_PARAM)->snapEnabled = true;
		configButton(ROLL_CONTROLS_PARAM, "Amend controls");
		configButton(ROLL_CABLES_PARAM, "Amend wires");
		configButton(ROLL_ALL_PARAM, "Amend everything");
		configButton(REVERT_PARAM, "Rescind last filing");
		configParam(SPREAD_PARAM, 0.f, 1.f, 0.5f, "Lines amended per filing", "%", 0.f, 100.f);
		configSwitch(BASIS_PARAM, 0.f, 2.f, 2.f, "Basis of review",
		             {"Wind-down", "Neutral", "Going concern"});
		configParam(SPINE_PARAM, 0.f, 1.f, 0.75f, "Safe harbor", "%", 0.f, 100.f);
		configInput(TRIG_INPUT, "Filing trigger");

		// Our own controls must never be caught by our own randomizer.
		for (int i = 0; i < PARAMS_LEN; i++)
			getParamQuantity(i)->randomizeEnabled = false;
	}

	Basis basis() {
		const int v = (int) std::lround(params[BASIS_PARAM].getValue());
		return (Basis) rack::math::clamp(v, 0, 2);
	}

	float safeHarbor() {
		return params[SPINE_PARAM].getValue();
	}

	float spread() {
		return params[SPREAD_PARAM].getValue();
	}

	void process(const ProcessArgs& args) override {
		if (inputs[TRIG_INPUT].isConnected() &&
		    trigTrigger.process(inputs[TRIG_INPUT].getVoltage(), 0.1f, 1.f)) {
			trigRequest.store(true, std::memory_order_release);
		}

		if (probeActive.load(std::memory_order_acquire)) {
			for (Probe& p : probes) {
				if (p.mod && p.port < (int) p.mod->outputs.size())
					p.stats.push(p.mod->outputs[p.port].getVoltage());
			}
			probeSamples.fetch_add(1, std::memory_order_relaxed);
		}

		if (args.sampleRate != hpSampleRate) {
			hpSampleRate = args.sampleRate;
			// One pole at roughly 1 kHz. Recomputed only when the rate moves.
			hpCoeff = rack::math::clamp(
				2.f * (float) M_PI * 1000.f / std::max(1.f, args.sampleRate), 0.f, 1.f);
		}

		if (meterActive.load(std::memory_order_acquire) && sink) {
			float sum = 0.f;
			for (size_t i = 0; i < sink->inputs.size(); i++)
				sum += sink->inputs[i].getVoltageSum();
			meter.push(sum, hpCoeff);
			meterSamples.fetch_add(1, std::memory_order_relaxed);
		}
	}

	json_t* dataToJson() override {
		json_t* rootJ = policy.toJson();
		json_t* lockedJ = json_array();
		for (int64_t id : lockedModules)
			json_array_append_new(lockedJ, json_integer(id));
		json_object_set_new(rootJ, "locked", lockedJ);
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		policy.fromJson(rootJ);
		lockedModules.clear();
		if (json_t* lockedJ = json_object_get(rootJ, "locked")) {
			size_t i;
			json_t* v;
			json_array_foreach(lockedJ, i, v)
				lockedModules.insert((int64_t) json_integer_value(v));
		}
	}
};


// ---------------------------------------------------------------------------
// Roll engine. Everything here runs on the UI thread, because adding, removing
// and undoing cables all touch the scene graph.
// ---------------------------------------------------------------------------

/** Nothing worth patching into or out of: a blank panel, a scope, an expander --
    or any module the user has bypassed, whose ports carry nothing meaningful. */
static bool isDeadWeight(rack::engine::Module* m, RoleMask roles, bool skipBypassed) {
	if (!m)
		return true;
	if (skipBypassed && m->isBypassed())
		return true;
	return (roles & ROLE_INERT) != 0;
}


static bool isAudioSink(rack::engine::Module* m) {
	if (!m || !m->model || !m->model->plugin)
		return false;
	return m->model->plugin->slug == "Core" &&
	       m->model->slug.find("Audio") != std::string::npos;
}


/** One key for a (module, port) pair. 4096 ports is far beyond anything Rack
    will ever hand us and keeps the arithmetic obvious. */
static inline int64_t portKey(int64_t modId, int port) {
	return modId * 4096 + (int64_t) port;
}


struct UncertaintyPolicyWidget;


struct RollEngine {
	UncertaintyPolicy* module = nullptr;

	enum class Phase {
		Idle,
		Measure,   // probing every output and taking a baseline level
		Audition,  // change applied, listening to see whether it survived
	};
	Phase phase = Phase::Idle;

	bool wantControls = false;
	bool wantCables = false;
	int attempt = 0;
	MotionStats baseline;
	MotionStats lastTrial;
	bool quickChecked = false;
	history::ComplexAction* pending = nullptr;
	RoleCache roles;
	/** (module, port) -> index into module->probes, so a cable's source
	    kind is a lookup rather than a search. */
	std::unordered_map<int64_t, int> probeIndex;
	int controlsMoved = 0;
	int controlsEligible = 0;
	std::string status = "no filing";
	float statusOkTimer = 0.f;
	float statusWarnTimer = 0.f;

	// ---------------------------------------------------------------

	void request(bool controls, bool cables) {
		if (phase != Phase::Idle)
			return;
		wantControls = controls;
		wantCables = cables;
		attempt = 0;
		beginMeasure();
	}

	void beginMeasure() {
		module->probeActive.store(false, std::memory_order_release);
		module->probes.clear();
		roles.clear();

		module->sink = nullptr;
		for (int64_t id : APP->engine->getModuleIds()) {
			rack::engine::Module* m = APP->engine->getModule(id);
			if (!m || m == module)
				continue;
			if (!module->sink && isAudioSink(m))
				module->sink = m;
			// Probing a bypassed module measures a port that is not running,
			// and classify() would then take that reading seriously.
			if (isDeadWeight(m, roles.get(m), module->policy.skipBypassed))
				continue;
			for (size_t i = 0; i < m->outputs.size(); i++) {
				Probe p;
				p.mod = m;
				p.moduleId = id;
				p.port = (int) i;
				if (i < m->outputInfos.size() && m->outputInfos[i])
					p.name = m->outputInfos[i]->getName();
				module->probes.push_back(p);
			}
		}

		module->probeSamples.store(0, std::memory_order_relaxed);
		module->meterSamples.store(0, std::memory_order_relaxed);
		module->meter.reset();
		module->meter.sizeFor(sampleRate(), meterSec());
		module->probeActive.store(true, std::memory_order_release);
		module->meterActive.store(module->sink != nullptr, std::memory_order_release);
		phase = Phase::Measure;
	}

	float sampleRate() const {
		float sr = APP->engine->getSampleRate();
		return sr > 0.f ? sr : 44100.f;
	}

	/** How long the meter runs. Judging level needs a quarter of a second;
	    judging rhythm does not -- at 120 BPM an eighth note IS a quarter of a
	    second, so a window that short sees at most one event and often lands
	    inside a single sustain. The long window is only paid for when the
	    basis actually asks a question about movement. */
	bool motionArmed() const {
		// The module browser draws this widget with no module behind it, so
		// every accessor reachable from drawLayer() has to survive that.
		return module && module->policy.auditionEnabled && module->sink &&
			   module->basis() != Basis::Neutral;
	}

	float meterSec() const {
		return motionArmed() ? MOTION_SEC : AUDITION_SEC;
	}

	int maxAttempts() const {
		return motionArmed() ? MOTION_ATTEMPTS : MAX_ATTEMPTS;
	}


	/** A filing can be mid-audition when the module is deleted. The trial's
	    cables and knob moves are already applied to a rack that is about to
	    outlive us, so the action gets committed rather than leaked -- undoing
	    here would mean unwinding a scene graph in the middle of being torn
	    down, and dropping it would leave those edits with no way back. */
	~RollEngine() {
		if (module) {
			module->probeActive.store(false, std::memory_order_release);
			module->meterActive.store(false, std::memory_order_release);
		}
		if (pending) {
			APP->history->push(pending);
			pending = nullptr;
		}
	}

	void step(float dt);
	void finishMeasure();
	void applyRoll();
	void evaluate();
	const char* quickReject() const;
	const char* motionReject() const;
	void reject(const char* reason);
	void rollControls(history::ComplexAction* action);
	void rollCables(history::ComplexAction* action);
	Graph buildGraph() const;
	Kind kindOf(rack::engine::Module* m, int port) const;
	/** Is this module off limits to the filing?
	
	    Reads the one chosen list in whichever direction the scope says. The
	    default is the original meaning -- the list is a set of exemptions --
	    so a patch saved before scoping existed behaves exactly as it did. */
	bool locked(int64_t id) const {
		const bool listed = module->lockedModules.count(id) > 0;
		switch (module->policy.scope) {
			case Scope::Everything: return false;
			case Scope::OnlyListed: return !listed;
			default: return listed;
		}
	}
};


void RollEngine::step(float dt) {
	statusOkTimer = std::max(0.f, statusOkTimer - dt);
	statusWarnTimer = std::max(0.f, statusWarnTimer - dt);

	switch (phase) {
		case Phase::Idle:
			break;

		case Phase::Measure: {
			// The two windows no longer end together. Classifying a port's
			// voltage genuinely needs no more than PROBE_SEC, but taking the
			// patch's baseline movement needs a bar of it.
			const float sr = sampleRate();
			if (module->probeActive.load(std::memory_order_acquire) &&
			    module->probeSamples.load(std::memory_order_relaxed) >=
			            (int64_t) (PROBE_SEC * sr))
				module->probeActive.store(false, std::memory_order_release);

			const bool probeDone = !module->probeActive.load(std::memory_order_acquire);
			const bool meterDone =
				!module->meterActive.load(std::memory_order_acquire) ||
				module->meterSamples.load(std::memory_order_relaxed) >=
					    (int64_t) (meterSec() * sr);
			if (probeDone && meterDone)
				finishMeasure();
			break;
		}

		case Phase::Audition: {
			if (!module->sink || !module->policy.auditionEnabled) {
				evaluate();
				break;
			}
			const float sr = sampleRate();
			const int64_t n = module->meterSamples.load(std::memory_order_relaxed);

			// Staged: the level and clipping test is answerable in a quarter of a
			// second and is the common way a trial fails, so a doomed roll costs
			// 0.25 s rather than the full window. Only a trial that has already
			// cleared it earns the long listen.
			if (!quickChecked && n >= (int64_t) (AUDITION_SEC * sr)) {
				quickChecked = true;
				const char* quick = quickReject();
				if (quick) {
					reject(quick);
					break;
				}
			}
			if (n >= (int64_t) (meterSec() * sr))
				evaluate();
			break;
		}
	}
}


void RollEngine::finishMeasure() {
	module->probeActive.store(false, std::memory_order_release);
	module->meterActive.store(false, std::memory_order_release);

	const float sr = sampleRate();
	probeIndex.clear();
	for (size_t i = 0; i < module->probes.size(); i++) {
		Probe& p = module->probes[i];
		p.profile = classify(p.stats, sr, p.name);
		probeIndex[portKey(p.moduleId, p.port)] = (int) i;
	}

	baseline = analyse(module->meter, sr);
	applyRoll();
}


void RollEngine::applyRoll() {
	history::ComplexAction* action = new history::ComplexAction;
	action->name = FILING_NAME;

	if (wantControls)
		rollControls(action);
	if (wantCables)
		rollCables(action);

	if (action->isEmpty()) {
		delete action;
		status = "nothing to file";
		statusWarnTimer = 1.5f;
		phase = Phase::Idle;
		return;
	}

	pending = action;

	if (!module->policy.auditionEnabled || !module->sink) {
		// No audition: keep whatever came out.
		APP->history->push(pending);
		pending = nullptr;
		status = "filed";
		statusOkTimer = 1.f;
		phase = Phase::Idle;
		return;
	}

	module->meterSamples.store(0, std::memory_order_relaxed);
	module->meter.reset();
	module->meter.sizeFor(sampleRate(), meterSec());
	module->meterActive.store(true, std::memory_order_release);
	quickChecked = false;
	phase = Phase::Audition;
}


/** The two failures that can be judged without waiting for the full window:
    the patch went quiet, or it pinned itself to the rails. Reads the snapshot
    the audio thread republishes each block rather than the accumulators it is
    still writing. */
const char* RollEngine::quickReject() const {
	const float rms = module->meter.liveRms.load(std::memory_order_relaxed);
	const float peak = module->meter.livePeak.load(std::memory_order_relaxed);
	const float floorRms = std::max(1e-4f, module->policy.silenceRatio * baseline.rms);
	if (rms < floorRms)
		return "withdrawn: no signal";
	if (peak > 40.f)
		return "withdrawn: overflow";
	return nullptr;
}


/** Did the filing leave the patch in the state the basis of review asks for?

    This is the part the module was missing entirely. A drone is loud and
    unclipped, so it sailed through the old test on the first attempt and the
    retry loop -- which was sitting right there -- never got a chance to reject
    it. */
const char* RollEngine::motionReject() const {
	const Basis b = module->basis();
	if (b == Basis::Neutral)
		return nullptr;

	if (b == Basis::GoingConcern) {
		// A patch that was already still cannot be asked to keep moving. Without
		// this the module would withdraw every attempt on an existing drone and
		// then give up, having done nothing but waste the budget. Going concern
		// preserves the movement that was there; it never imposes movement that
		// was not.
		if (baseline.index <= 0.75f)
			return nullptr;
		if (lastTrial.index < module->policy.motionFloor * baseline.index)
			return "withdrawn: no movement";
		return nullptr;
	}

	// Wind-down, judged on articulation rather than the full index: a drone
	// with an LFO on the filter is still a drone, and a good one. Both tests
	// have to fail before a trial is rejected -- the absolute one asks "is this
	// a drone yet", and the relative one lets a filing through once it has taken
	// enough of the movement out, whatever the patch started at.
	if (lastTrial.articulation > 1.5f &&
	    lastTrial.articulation > module->policy.motionCeil * baseline.articulation)
		return "withdrawn: still ticking";
	return nullptr;
}


void RollEngine::reject(const char* reason) {
	if (!pending) {
		phase = Phase::Idle;
		return;
	}
	pending->undo();
	delete pending;
	pending = nullptr;

	attempt++;
	if (attempt >= maxAttempts()) {
		status = reason;
		statusWarnTimer = 2.5f;
		phase = Phase::Idle;
		return;
	}

	// Re-use the measurements we already have and try another combination.
	applyRoll();
}


void RollEngine::evaluate() {
	module->meterActive.store(false, std::memory_order_release);
	lastTrial = analyse(module->meter, sampleRate());

	// A roll passes if the output is still audible, has not pinned itself to the
	// rails, and has left the patch in the condition the basis asks for.
	// Everything else gets quietly rolled back and tried again, so only survivors
	// are ever heard.
	const float floorRms = std::max(1e-4f, module->policy.silenceRatio * baseline.rms);
	const char* reason = nullptr;
	if (lastTrial.rms < floorRms)
		reason = "withdrawn: no signal";
	else if (lastTrial.peak > 40.f)
		reason = "withdrawn: overflow";
	else
		reason = motionReject();

	if (!reason || !pending) {
		if (pending) {
			APP->history->push(pending);
			pending = nullptr;
		}
		status = attempt > 0
		         ? rack::string::f("filed, %d amended", attempt)
		         : "filed";
		statusOkTimer = 1.f;
		phase = Phase::Idle;
		return;
	}

	reject(reason);
}


Graph RollEngine::buildGraph() const {
	Graph g;
	for (int64_t id : APP->engine->getModuleIds())
		g.addNode(id);
	for (int64_t cid : APP->engine->getCableIds()) {
		rack::engine::Cable* c = APP->engine->getCable(cid);
		if (c && c->outputModule && c->inputModule)
			g.addEdge(c->outputModule->id, c->inputModule->id);
	}
	return g;
}


void RollEngine::rollControls(history::ComplexAction* action) {
	const float amount = module->params[UncertaintyPolicy::KNOB_AMOUNT_PARAM].getValue();
	if (amount <= 0.f)
		return;

	const Policy& pol = module->policy;
	const float discipline = rack::math::clamp(pol.roleDiscipline, 0.f, 1.f);

	/** One knob that could move, and how far it may travel if it does. */
	struct Target {
		int64_t modId = -1;
		int paramId = 0;
		rack::engine::ParamQuantity* pq = nullptr;
		float scale = 1.f;
		bool allowDisabled = false;
	};
	std::vector<Target> eligible;

	for (int64_t id : APP->engine->getModuleIds()) {
		if (locked(id))
			continue;
		rack::engine::Module* m = APP->engine->getModule(id);
		if (!m || m == module || isAudioSink(m))
			continue;
		const RoleMask mRoles = roles.get(m);
		if (isDeadWeight(m, mRoles, module->policy.skipBypassed))
			continue;
		// Fetched once per module rather than once per param: getParam()
		// scans the widget tree, and a matrix module has sixty-four of them.
		rack::app::ModuleWidget* mw = APP->scene->rack->getModule(id);

		for (size_t i = 0; i < m->params.size(); i++) {
			rack::engine::ParamQuantity* pq = m->getParamQuantity((int) i);
			if (!pq)
				continue;
			if (!pq->isBounded())
				continue;

			const ButtonKind bk = buttonKind(mw, (int) i);
			// A momentary button's value is a press, not a setting: app::Switch
			// puts it back on release, so moving one writes a no-op into the
			// filing and nothing is heard.
			if (bk == ButtonKind::Momentary)
				continue;

			// configButton() clears randomizeEnabled for every button it makes,
			// so taking that flag at face value hides every button in Rack --
			// including the grids of latching buttons that ARE the front panel on
			// a patch built around a matrix or a step sequencer. A latch is patch
			// content and is rolled; anything else the author disabled stays that
			// way, which is the convention Rack's own randomize follows.
			const bool latching = (bk == ButtonKind::Latching);
			if (!pq->randomizeEnabled && !latching)
				continue;
			if (latching && rack::random::uniform() >= pol.latchProb)
				continue;
			if (!pol.rollLevels && isLevelParam(pq->name))
				continue;
			// A mapper owns this knob and will put it back. Rolling it looks
			// like the module doing nothing, and quietly spends part of SPREAD
			// to do it. The control worth moving is the one on the surface.
			if (pol.skipMapped && isMapped(id, (int) i))
				continue;

			const ParamClass k = classifyParam(pq, mRoles, bk);
			if (k == ParamClass::Mode && rack::random::uniform() >= pol.modeSwitchProb)
				continue;

			Target t;
			t.modId = id;
			t.paramId = (int) i;
			t.pq = pq;
			t.allowDisabled = latching;
			// Two tables, one dial. At discipline 0 every knob moves by the same
			// share of its range -- exactly what the module did before it knew
			// what anything was -- and at 1 it defers fully to what the param and
			// its module are for.
			t.scale = rack::math::crossfade(1.f, classScale(k) * roleScale(mRoles),
			                                discipline);
			if (t.scale <= 0.f)
				continue;
			eligible.push_back(t);
		}
	}

	controlsEligible = (int) eligible.size();
	controlsMoved = 0;
	if (eligible.empty())
		return;

	// Breadth is how many controls move; VARIANCE is how far each one travels.
	// They were the same number before, which meant "nudge everything slightly"
	// was reachable and "yank three things hard" was not.
	const float breadth = rack::math::clamp(module->spread(), 0.f, 1.f);
	int want = (int) std::lround(breadth * (float) eligible.size());
	want = rack::math::clamp(want, 1, (int) eligible.size());

	// Partial Fisher-Yates: sampling without replacement, so the count is exact
	// and the readout can say honestly how much of the patch was touched.
	for (int i = 0; i < want; i++) {
		const int span = (int) eligible.size() - i;
		const int j = i + (int) (rack::random::uniform() * (float) span) % span;
		std::swap(eligible[i], eligible[j]);
		Target& t = eligible[i];

		const float before = t.pq->getValue();
		Deviation d = deviate(t.pq, amount * t.scale, t.allowDisabled);
		if (!d.changed)
			continue;

		history::ParamChange* h = new history::ParamChange;
		h->name = "roll knob";
		h->moduleId = t.modId;
		h->paramId = t.paramId;
		h->oldValue = before;
		h->newValue = d.newValue;
		t.pq->setValue(d.newValue);
		action->push(h);
		controlsMoved++;
	}
}


/** One existing cable, with everything a removal decision needs already worked
    out. Cheap to build -- there are far fewer cables in a patch than there are
    (source x input) pairs, which is why this can afford a makeSlot() per cable
    where the candidate walk cannot. */
struct CableFacts {
	app::CableWidget* cw = nullptr;
	Kind srcKind = Kind::Unknown;
	RoleMask srcRoles = ROLE_NONE;
	InputSlot dst;
	Move move = Move::Modulation;
	bool onSpine = false;
	float weight = 0.f;
};


Kind RollEngine::kindOf(rack::engine::Module* m, int port) const {
	if (!m)
		return Kind::Unknown;
	auto it = probeIndex.find(portKey(m->id, port));
	if (it == probeIndex.end())
		return Kind::Unknown;
	return module->probes[it->second].profile.kind;
}


void RollEngine::rollCables(history::ComplexAction* action) {
	const int ops = (int) std::lround(module->params[UncertaintyPolicy::CABLE_COUNT_PARAM].getValue());
	if (ops <= 0)
		return;

	const Policy& pol = module->policy;
	const float harbor = rack::math::clamp(module->safeHarbor(), 0.f, 1.f);

	for (int k = 0; k < ops; k++) {
		Graph g = buildGraph();
		std::vector<app::CableWidget*> cables = APP->scene->rack->getCompleteCables();

		// --- every existing cable, described ------------------------------------
		// Done before anything else because both the removal weights and the
		// add path's eviction penalty are read off this.
		std::vector<CableFacts> facts;
		std::unordered_map<int64_t, int> feedCount;    // cables into a module
		std::unordered_map<int64_t, int> classFeed;    // ... of each class
		std::unordered_set<int64_t> spineOccupied;     // inputs holding a spine cable

		for (app::CableWidget* cw : cables) {
			rack::engine::Cable* c = cw ? cw->getCable() : nullptr;
			if (!c || !c->outputModule || !c->inputModule)
				continue;

			CableFacts f;
			f.cw = cw;
			f.srcKind = kindOf(c->outputModule, c->outputId);
			f.srcRoles = roles.get(c->outputModule);
			std::string iname;
			if (c->inputId < (int) c->inputModule->inputInfos.size() &&
			    c->inputModule->inputInfos[c->inputId])
				iname = c->inputModule->inputInfos[c->inputId]->getName();
			f.dst = makeSlot(c->inputModule, c->inputModule->id, c->inputId, iname,
			                 roles.get(c->inputModule));
			f.move = classifyMove(f.srcKind, f.srcRoles, f.dst);
			f.onSpine = onArticulationSpine(f.srcKind, f.srcRoles, f.dst);

			feedCount[f.dst.modId]++;
			classFeed[f.dst.modId * MOVE_COUNT + (int) f.move]++;
			if (f.onSpine)
				spineOccupied.insert(portKey(f.dst.modId, f.dst.port));
			facts.push_back(f);
		}

		// --- candidate removals, weighted ---------------------------------------
		// The old code drew uniformly here, which meant a clock cable was as
		// likely to be cut as a redundant CV cable -- one of the three ways a
		// rhythmic patch used to turn into a drone without anyone deciding it
		// should.
		std::vector<CableFacts> removable;
		float removeTotal = 0.f;
		for (CableFacts& f : facts) {
			rack::engine::Cable* c = f.cw->getCable();
			const int64_t a = c->outputModule->id;
			const int64_t b = c->inputModule->id;
			if (locked(a) || locked(b))
				continue;
			if (c->outputModule == module || c->inputModule == module)
				continue;
			if (module->policy.protectAudioPath && module->sink) {
				// Refuse anything that would leave the output with nothing
				// upstream of it at all -- the cut that silences the patch.
				if (g.feeders(module->sink->id, a, b).empty())
					continue;
			}

			float w = std::max(0.f, pol.moveWeight[(int) f.move]);
			if (f.onSpine)
				w *= (1.f - harbor);
			if (f.srcKind == Kind::Pitch && f.dst.isPitch)
				w *= 0.5f;             // cutting the note stream is a drone too
			if (feedCount[f.dst.modId] <= 1)
				w *= 0.35f;            // the only thing feeding it: kills a branch
			if (classFeed[f.dst.modId * MOVE_COUNT + (int) f.move] >= 2)
				w *= 1.6f;             // one of several: the cheapest edit there is
			if (w <= 0.f)
				continue;

			f.weight = w;
			removeTotal += w;
			removable.push_back(f);
		}

		// --- destination inputs, parsed once ------------------------------------
		// Both of the expensive things -- parsing port names and asking whether a
		// new cable would close a loop -- are hoisted out of the source x input
		// loop. One traversal per module rather than one per pair, and one string
		// parse per input rather than one per pair.
		std::vector<InputSlot> slots;
		std::unordered_map<int64_t, std::unordered_set<int64_t>> downstream;
		for (int64_t did : APP->engine->getModuleIds()) {
			if (locked(did))
				continue;
			rack::engine::Module* dm = APP->engine->getModule(did);
			if (!dm || dm == module)
				continue;
			const RoleMask dstRoles = roles.get(dm);
			if (isDeadWeight(dm, dstRoles, module->policy.skipBypassed))
				continue;
			if (!module->policy.allowFeedback)
				downstream[did] = g.descendants(did);
			for (size_t i = 0; i < dm->inputs.size(); i++) {
				std::string iname;
				if (i < dm->inputInfos.size() && dm->inputInfos[i])
					iname = dm->inputInfos[i]->getName();
				slots.push_back(makeSlot(dm, did, (int) i, iname, dstRoles));
			}
		}

		// Walk the source x input product twice rather than materialising it:
		// once to total the weights, once to land on the pick. On a big patch the
		// product runs to tens of thousands of pairs, and none of them need to be
		// stored to sample one.
		auto eachCandidate = [&](const std::function<void(Probe&, const InputSlot&, Move, float)>& fn) {
			for (Probe& pr : module->probes) {
				if (!pr.mod || locked(pr.moduleId))
					continue;
				if (pr.profile.kind == Kind::Silent || pr.profile.kind == Kind::Unknown)
					continue;
				const RoleMask srcRoles = roles.get(pr.mod);
				for (const InputSlot& sl : slots) {
					if (sl.mod == pr.mod && !module->policy.allowFeedback)
						continue;
					if (!module->policy.allowFeedback) {
						auto it = downstream.find(sl.modId);
						if (it != downstream.end() && it->second.count(pr.moduleId))
							continue;
					}
					const float a = affinity(pr.profile.kind, sl, module->policy.blindPortWeight);
					if (a <= 0.05f)
						continue;
					// Rack allows one cable per input, so landing here evicts
					// whatever is already there. If that incumbent is holding the
					// patch's pulse together, safe harbour makes this a bad place
					// to aim -- the same protection as on the removal side,
					// because an eviction is a removal wearing a hat.
					float w = a * a;   // squared: confident matches dominate
					                   // without ever making the odd left-field
					                   // patch impossible
					if (spineOccupied.count(portKey(sl.modId, sl.port)))
						w *= (1.f - harbor);
					if (w <= 0.f)
						continue;
					fn(pr, sl, classifyMove(pr.profile.kind, srcRoles, sl), w);
				}
			}
		};

		float classTotal[MOVE_COUNT] = {0.f, 0.f, 0.f, 0.f};
		eachCandidate([&](Probe&, const InputSlot&, Move mv, float w) {
			classTotal[(int) mv] += w;
		});

		float scoreTotal = 0.f;
		for (int i = 0; i < MOVE_COUNT; i++)
			scoreTotal += classTotal[i];

		const bool canAdd = scoreTotal > 0.f;
		const bool canRemove = removeTotal > 0.f;
		if (!canAdd && !canRemove)
			return;

		// Lean towards adding in a sparse patch and towards pruning a dense one.
		float addBias = 0.5f;
		if (canAdd && canRemove) {
			const float density = (float) cables.size() /
			                      std::max(1.f, (float) module->probes.size());
			addBias = rack::math::clamp(0.75f - density, 0.2f, 0.8f);
		}
		const bool doAdd = canAdd && (!canRemove || rack::random::uniform() < addBias);

		if (doAdd) {
			// Pick the CLASS first, from the policy weights alone -- never from
			// weight x how many candidates the class happens to contain. That
			// multiplication was the whole bug: a typical patch has five times as
			// many modulation inputs as anything else, so a single flat roulette
			// over every pair was decided by the census and every roll came out
			// as "an LFO landed on a CV input". Choosing the class first makes
			// the appetite for each kind of edit mean what it says, whatever the
			// shape of the patch.
			int chosen = -1;
			float classPickTotal = 0.f;
			for (int i = 0; i < MOVE_COUNT; i++) {
				if (classTotal[i] > 0.f)
					classPickTotal += std::max(0.f, pol.moveWeight[i]);
			}
			if (classPickTotal > 0.f) {
				float r = rack::random::uniform() * classPickTotal;
				for (int i = 0; i < MOVE_COUNT; i++) {
					if (classTotal[i] <= 0.f)
						continue;
					r -= std::max(0.f, pol.moveWeight[i]);
					if (r <= 0.f) {
						chosen = i;
						break;
					}
				}
			}
			if (chosen < 0) {
				// Every class the patch can offer has been weighted to zero.
				// Fall back to an even hand rather than doing nothing at all.
				std::vector<int> live;
				for (int i = 0; i < MOVE_COUNT; i++) {
					if (classTotal[i] > 0.f)
						live.push_back(i);
				}
				if (live.empty())
					continue;
				chosen = live[(size_t) (rack::random::uniform() * live.size()) % live.size()];
			}

			float pick = rack::random::uniform() * classTotal[chosen];
			Probe* srcPick = nullptr;
			InputSlot dstPick;
			eachCandidate([&](Probe& pr, const InputSlot& sl, Move mv, float w) {
				if (srcPick || (int) mv != chosen)
					return;
				pick -= w;
				if (pick <= 0.f) {
					srcPick = &pr;
					dstPick = sl;
				}
			});
			if (!srcPick)
				continue;

			// Rack allows one cable per input, so evict the incumbent first.
			for (app::CableWidget* cw : APP->scene->rack->getCompleteCables()) {
				rack::engine::Cable* c = cw ? cw->getCable() : nullptr;
				if (!c || c->inputModule != dstPick.mod || c->inputId != dstPick.port)
					continue;
				history::CableRemove* hr = new history::CableRemove;
				hr->setCable(cw);
				action->push(hr);
				APP->scene->rack->removeCable(cw);
				delete cw;
				break;
			}

			rack::engine::Cable* cable = new rack::engine::Cable;
			cable->outputModule = srcPick->mod;
			cable->outputId = srcPick->port;
			cable->inputModule = dstPick.mod;
			cable->inputId = dstPick.port;
			try {
				APP->engine->addCable(cable);
			}
			catch (rack::Exception& e) {
				WARN("Uncertainty Policy could not add cable: %s", e.what());
				delete cable;
				continue;
			}

			app::CableWidget* cw = new app::CableWidget;
			cw->setCable(cable);
			if (!settings::cableColors.empty()) {
				cw->color = settings::cableColors[
				        (size_t) (rack::random::uniform() * settings::cableColors.size()) %
				        settings::cableColors.size()];
			}
			APP->scene->rack->addCable(cw);

			history::CableAdd* ha = new history::CableAdd;
			ha->setCable(cw);
			action->push(ha);
		}
		else if (canRemove) {
			float pick = rack::random::uniform() * removeTotal;
			app::CableWidget* victim = nullptr;
			for (const CableFacts& f : removable) {
				pick -= f.weight;
				if (pick <= 0.f) {
					victim = f.cw;
					break;
				}
			}
			if (!victim)
				victim = removable.back().cw;

			history::CableRemove* hr = new history::CableRemove;
			hr->setCable(victim);
			action->push(hr);
			APP->scene->rack->removeCable(victim);
			delete victim;
		}
	}
}
// ---------------------------------------------------------------------------
// Panel. The palette, the shared hardware and the entire silkscreen come from
// src/PanelTheme.hpp, generated by tools/panel.py -- see ../panelkit/README.md.
// Nothing about the look is written here, which is what keeps this module,
// Retroactive and PatchAudit on one design language.
// ---------------------------------------------------------------------------

typedef RoundLargeBlackKnob PolicyKnob;


/** The glass at the top: what the last roll did, in words. Monospace, because it
    reports counts and verdicts rather than prose. */
struct PolicyDisplay : widget::Widget {
	UncertaintyPolicy* module = nullptr;
	RollEngine* engine = nullptr;

	/** The terms in force, for the third line of the read-out. */
	std::string mandate() const {
		if (!module)
			return "no mandate on file";
		const char* b = "neutral";
		switch (module->basis()) {
			case Basis::WindDown: b = "wind-down"; break;
			case Basis::GoingConcern: b = "going concern"; break;
			default: break;
		}
		return rack::string::f("%s / harbor %.0f%%", b, module->safeHarbor() * 100.f);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1)
			return;

		std::string top = "NO FILING";
		std::string bot = "nothing on file";
		NVGcolor col = panel::SAGE;

		if (engine) {
			if (engine->phase == RollEngine::Phase::Measure) {
				top = "ASSESSING";
				bot = "profiling outputs";
				col = panel::LIME;
			}
			else if (engine->phase == RollEngine::Phase::Audition) {
				top = "UNDER REVIEW";
				bot = rack::string::f("filing %d of %d", engine->attempt + 1,
							              engine->maxAttempts());
				col = panel::LIME;
			}
			else {
				top = rack::string::uppercase(engine->status);
				col = engine->statusWarnTimer > 0.f ? panel::CLAY
				      : engine->statusOkTimer > 0.f ? panel::MINT : panel::SAGE;
				// Under a motion basis the movement figure IS the verdict's evidence,
				// so it earns the line ahead of the level it replaced.
				if (engine->motionArmed() && engine->baseline.index > 0.f) {
					bot = rack::string::f("move %.1f -> %.1f",
					                      engine->baseline.index, engine->lastTrial.index);
				}
				else if (engine->wantControls && engine->controlsEligible > 0) {
					bot = rack::string::f("%d of %d lines",
					                      engine->controlsMoved, engine->controlsEligible);
				}
				else if (engine->baseline.rms > 0.f) {
					bot = rack::string::f("out %.2f -> %.2f V",
					                      engine->baseline.rms, engine->lastTrial.rms);
				}
				else {
					bot = "nothing on file";
				}
			}
		}

		// The family read-out idiom: words in the mono face, headline over
		// subline. Both runs go through panel::text so neither can leave state
		// behind for the other.
		static const panel::TextStyle HEAD(panel::Face::Mono, 9.f, panel::SAGE,
			NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
		static const panel::TextStyle SUB(panel::Face::Mono, 6.4f, panel::SAGE,
			NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
		// The mandate line is dimmer than the verdict above it on purpose: it is
		// standing state rather than news, and it is there so the terms the next
		// filing will be made under can be read without decoding a switch
		// position and a trimpot angle.
		static const panel::TextStyle FOOT(panel::Face::Mono, 5.8f, panel::SAGE,
			NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);

		panel::text(args.vg, HEAD.inked(col), mm2px(2.2f), mm2px(5.0f), top);
		panel::text(args.vg, SUB, mm2px(2.2f), mm2px(9.0f), bot);
		panel::text(args.vg, FOOT, mm2px(2.2f), mm2px(13.0f), mandate());

		Widget::drawLayer(args, layer);
	}
};


struct UncertaintyPolicyWidget : ModuleWidget {
	UncertaintyPolicy* mod = nullptr;
	RollEngine engine;
	dsp::BooleanTrigger controlsBtn, cablesBtn, allBtn, revertBtn;

	UncertaintyPolicyWidget(UncertaintyPolicy* module) {
		mod = module;
		engine.module = module;
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/UncertaintyPolicy.svg")));

		panel::addScrews(this);
		panel::addLabels(this);

		PolicyDisplay* display = new PolicyDisplay;
		display->module = module;
		display->engine = &engine;
		display->box.pos = panel::mm(4.2f, 10.2f);
		display->box.size = panel::mm(panel::W - 8.4f, 15.0f);
		addChild(display);

		addParam(createParamCentered<PolicyKnob>(panel::mm(panel::KNOB_AMOUNT_POS.x, panel::KNOB_AMOUNT_POS.y),
		                                        module, UncertaintyPolicy::KNOB_AMOUNT_PARAM));
		addParam(createParamCentered<PolicyKnob>(panel::mm(panel::SPREAD_POS.x, panel::SPREAD_POS.y),
		                                        module, UncertaintyPolicy::SPREAD_PARAM));
		addParam(createParamCentered<PolicyKnob>(panel::mm(panel::CABLE_COUNT_POS.x, panel::CABLE_COUNT_POS.y),
		                                        module, UncertaintyPolicy::CABLE_COUNT_PARAM));

		addParam(createParamCentered<VCVButton>(panel::mm(panel::ROLL_CONTROLS_POS.x, panel::ROLL_CONTROLS_POS.y),
		                                        module, UncertaintyPolicy::ROLL_CONTROLS_PARAM));
		addParam(createParamCentered<VCVButton>(panel::mm(panel::ROLL_CABLES_POS.x, panel::ROLL_CABLES_POS.y),
		                                        module, UncertaintyPolicy::ROLL_CABLES_PARAM));
		addParam(createParamCentered<CKSSThree>(panel::mm(panel::BASIS_POS.x, panel::BASIS_POS.y),
		                                        module, UncertaintyPolicy::BASIS_PARAM));
		addParam(createParamCentered<VCVBezel>(panel::mm(panel::ROLL_ALL_POS.x, panel::ROLL_ALL_POS.y),
		                                        module, UncertaintyPolicy::ROLL_ALL_PARAM));
		addParam(createParamCentered<Trimpot>(panel::mm(panel::SPINE_POS.x, panel::SPINE_POS.y),
		                                        module, UncertaintyPolicy::SPINE_PARAM));
		addParam(createParamCentered<VCVButton>(panel::mm(panel::REVERT_POS.x, panel::REVERT_POS.y),
		                                        module, UncertaintyPolicy::REVERT_PARAM));

		addChild(createLightCentered<MediumLight<panel::VerdictLight> >(
		             panel::mm(panel::VERDICT_POS.x, panel::VERDICT_POS.y),
		             module, UncertaintyPolicy::OK_LIGHT));

		addInput(createInputCentered<panel::PortIn>(
		             panel::mm(panel::TRIG_POS.x, panel::TRIG_POS.y),
		             module, UncertaintyPolicy::TRIG_INPUT));
	}

	void step() override {
		ModuleWidget::step();
		if (!mod)
			return;

		const float dt = APP->window->getLastFrameDuration();
		engine.step(dt > 0.f ? dt : 1.f / 60.f);

		if (controlsBtn.process(mod->params[UncertaintyPolicy::ROLL_CONTROLS_PARAM].getValue() > 0.5f))
			engine.request(true, false);
		if (cablesBtn.process(mod->params[UncertaintyPolicy::ROLL_CABLES_PARAM].getValue() > 0.5f))
			engine.request(false, true);
		if (allBtn.process(mod->params[UncertaintyPolicy::ROLL_ALL_PARAM].getValue() > 0.5f))
			engine.request(true, true);
		if (revertBtn.process(mod->params[UncertaintyPolicy::REVERT_PARAM].getValue() > 0.5f)) {
			if (engine.phase == RollEngine::Phase::Idle) {
				// A bare undo() takes whatever is on top of the global stack, which
				// after any edit of your own is your edit and not our filing.
				// RESCIND withdraws a filing; it is not a second undo key.
				if (APP->history->getUndoName() == FILING_NAME) {
					APP->history->undo();
					engine.status = "rescinded";
					engine.statusOkTimer = 1.f;
				}
				else {
					engine.status = "nothing to rescind";
					engine.statusWarnTimer = 2.f;
				}
			}
		}
		if (mod->trigRequest.exchange(false, std::memory_order_acq_rel))
			engine.request(true, true);

		mod->lights[UncertaintyPolicy::OK_LIGHT].setBrightness(engine.statusOkTimer > 0.f ? 1.f : 0.f);
		mod->lights[UncertaintyPolicy::WARN_LIGHT].setBrightness(engine.statusWarnTimer > 0.f ? 1.f : 0.f);
	}

	/** What the module list currently means, for the menu's right-hand text. */
	std::string scopeName() const {
		if (!mod)
			return "";
		switch (mod->policy.scope) {
			case Scope::Everything: return "the whole patch";
			case Scope::OnlyListed: return rack::string::f("only %d chosen",
			                                               (int) mod->lockedModules.size());
			default: return rack::string::f("all but %d chosen",
			                                (int) mod->lockedModules.size());
		}
	}

	void appendContextMenu(Menu* menu) override {
		if (!mod)
			return;
		menu->addChild(new MenuSeparator);

		// A preset is the whole posture. There are enough settings behind this
		// menu now that offering them one at a time would be a worse product
		// than offering five that are known to work together.
		std::string current = "custom";
		for (const Preset& q : presets()) {
			if (matchesPreset(mod->policy, q, mod->basis(), mod->safeHarbor(), mod->spread())) {
				current = q.name;
				break;
			}
		}
		menu->addChild(createSubmenuItem("Filing posture", current, [=](Menu* sub) {
			for (const Preset& q : presets()) {
				sub->addChild(createCheckMenuItem(q.name, "",
				        [=]() {
					        return matchesPreset(mod->policy, q, mod->basis(),
					                             mod->safeHarbor(), mod->spread());
				        },
				        [=]() {
					        applyPreset(mod->policy, q);
					        // The three panel controls travel with the posture.
					        mod->getParamQuantity(UncertaintyPolicy::BASIS_PARAM)
					                ->setValue((float) (int) q.basis);
					        mod->getParamQuantity(UncertaintyPolicy::SPINE_PARAM)
					                ->setValue(q.safeHarbor);
					        mod->getParamQuantity(UncertaintyPolicy::SPREAD_PARAM)
					                ->setValue(q.spread);
				        }));
			}
		}));

		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Where transfers land"));

		// Read as a distribution over classes of edit, never multiplied by how
		// many candidates a class happens to contain -- that multiplication is
		// what used to make every roll come out as modulation, simply because a
		// patch has more CV inputs than anything else.
		auto pct = [=](const char* label, float* field, const char* unit) {
			return createSubmenuItem(label, rack::string::f("%.0f%%", *field * 100.f),
			                         [=](Menu* sub) {
				static const float opts[] = {0.f, 0.25f, 0.5f, 0.75f, 1.f};
				for (float v : opts) {
					sub->addChild(createCheckMenuItem(
					        rack::string::f("%.0f%% %s", v * 100.f, unit), "",
					        [=]() { return std::fabs(*field - v) < 1e-3f; },
					        [=]() { *field = v; }));
				}
			});
		};

		menu->addChild(pct("Signal path", &mod->policy.moveWeight[(int) Move::Voice], ""));
		menu->addChild(pct("Modulation", &mod->policy.moveWeight[(int) Move::Modulation], ""));
		menu->addChild(pct("Pitch", &mod->policy.moveWeight[(int) Move::Pitch], ""));
		menu->addChild(pct("Timing", &mod->policy.moveWeight[(int) Move::Timing], ""));

		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Policy"));

		menu->addChild(pct("Follow each module's declared function",
		                   &mod->policy.roleDiscipline, ""));
		menu->addChild(pct("Flip waveform and algorithm switches",
		                   &mod->policy.modeSwitchProb, "of the time"));
		// Separate from the above on purpose: flipping an algorithm is a large,
		// categorical event, while flipping a step or a route in a matrix is an
		// ordinary edit -- and on a patch played through a grid of buttons it is
		// the whole point. Set this to 0% for a knobs-only roll.
		menu->addChild(pct("Flip latching buttons",
		                   &mod->policy.latchProb, "of the time"));

		menu->addChild(createSubmenuItem("Patch unlabelled ports", "", [=](Menu* sub) {
			// A port whose module never called configInput() tells us nothing,
			// and the candidate walk rejects anything at or below 0.05 -- so the
			// bottom of this range is a genuine "never" rather than a small
			// number that still fires occasionally.
			struct Opt { const char* name; float v; };
			static const Opt opts[] = {{"Never", 0.04f}, {"Rarely", 0.08f},
			                           {"Sometimes", 0.15f}, {"Freely", 0.30f}};
			for (const Opt& o : opts) {
				sub->addChild(createCheckMenuItem(o.name, "",
				        [=]() { return std::fabs(mod->policy.blindPortWeight - o.v) < 1e-3f; },
				        [=]() { mod->policy.blindPortWeight = o.v; }));
			}
		}));

		menu->addChild(pct("Movement a filing must keep (going concern)",
		                   &mod->policy.motionFloor, "of prior"));
		menu->addChild(pct("Movement a filing must shed (wind-down)",
		                   &mod->policy.motionCeil, "of prior"));

		menu->addChild(createSubmenuItem("Materiality threshold",
		                                 rack::string::f("%.0f%%", mod->policy.silenceRatio * 100.f),
		                                 [=](Menu* sub) {
			static const float opts[] = {0.05f, 0.1f, 0.2f, 0.35f, 0.5f};
			for (float v : opts) {
				sub->addChild(createCheckMenuItem(
				        rack::string::f("%.0f%% of prior level", v * 100.f), "",
				        [=]() { return std::fabs(mod->policy.silenceRatio - v) < 1e-4f; },
				        [=]() { mod->policy.silenceRatio = v; }));
			}
		}));

		menu->addChild(new MenuSeparator);
		menu->addChild(createBoolPtrMenuItem("Review each filing, rescind if it kills the signal", "",
		                                     &mod->policy.auditionEnabled));
		menu->addChild(createBoolPtrMenuItem("Protect the path to the output", "",
		                                     &mod->policy.protectAudioPath));
		menu->addChild(createBoolPtrMenuItem("Allow circular references", "",
		                                     &mod->policy.allowFeedback));
		menu->addChild(createBoolPtrMenuItem("Include level and mix knobs", "",
		                                     &mod->policy.rollLevels));
		menu->addChild(createBoolPtrMenuItem("Skip bypassed modules", "",
		                                     &mod->policy.skipBypassed));
		menu->addChild(createBoolPtrMenuItem("Skip controls a mapper is driving", "",
		                                     &mod->policy.skipMapped));

		menu->addChild(new MenuSeparator);
		// What the module believes each of your modules is for. Tags come from the
		// plugin author; a module that declares none falls back to its own prose,
		// and one showing "-" is invisible to every role-aware rule.
		menu->addChild(createSubmenuItem("What this patch looks like", "", [=](Menu* sub) {
			std::vector<int64_t> ids = APP->engine->getModuleIds();
			PatchLabels labels;
			RoleCache rc;
			labels.scan(rc);
			int tagged = 0, total = 0, named = 0;
			for (int64_t id : ids) {
				rack::engine::Module* m = APP->engine->getModule(id);
				if (!m || m == mod || !m->model)
					continue;
				total++;
				const RoleMask fromTags = rolesFromTags(m->model);
				if (fromTags & ROLE_STRUCTURAL)
					tagged++;
				const RoleMask roles = rolesOf(m);
				const std::string given = labels.nameFor(id);
				if (!given.empty())
					named++;
				// The creator's own name first where there is one: on a patch built
				// as an instrument it says far more than the model name does.
				sub->addChild(createMenuLabel(rack::string::f(
				        "%s  --  %s%s",
				        given.empty() ? m->model->name.c_str() : given.c_str(),
				        roleNames(roles).c_str(),
				        (fromTags & ROLE_STRUCTURAL) ? "" : "  (from description)")));
			}
			if (!total)
				sub->addChild(createMenuLabel("No other modules in the patch"));
			else
				sub->addChild(createMenuLabel(rack::string::f(
				        "%d of %d declare their function, %d named by the creator",
				        tagged, total, named)));
		}));

		// A patch from Patchstorage is somebody's instrument, and the only
		// interesting thing to randomise is usually the front panel they put on
		// it -- so the list has to be readable in both directions.
		menu->addChild(createSubmenuItem("Modules a filing may touch", scopeName(), [=](Menu* sub) {
			struct Opt { const char* name; Scope v; const char* hint; };
			static const Opt opts[] = {
				{"The whole patch", Scope::Everything, ""},
				{"All but the chosen", Scope::AllButListed, "the chosen are exempt"},
				{"Only the chosen", Scope::OnlyListed, "the chosen are the patch"},
			};
			for (const Opt& o : opts) {
				sub->addChild(createCheckMenuItem(o.name, o.hint,
				        [=]() { return mod->policy.scope == o.v; },
				        [=]() { mod->policy.scope = o.v; }));
			}
		}));

		menu->addChild(createSubmenuItem("Choose modules",
		                                 rack::string::f("%d chosen", (int) mod->lockedModules.size()),
		                                 [=](Menu* sub) {
			PatchLabels labels;
			RoleCache rc;
			labels.scan(rc);

			// The two selections worth having as one click each. A patch built
			// around a control surface wants the first; a patch whose creator
			// labelled their front panel wants the second, and those labels are
			// a better description of the instrument than anything we infer.
			sub->addChild(createMenuItem("Select the control surfaces", "", [=]() {
				RoleCache r2;
				mod->lockedModules.clear();
				for (int64_t id : APP->engine->getModuleIds()) {
					rack::engine::Module* m = APP->engine->getModule(id);
					if (!m || m == mod)
						continue;
					if (r2.get(m) & ROLE_SURFACE)
						mod->lockedModules.insert(id);
				}
				mod->policy.scope = Scope::OnlyListed;
			}));
			sub->addChild(createMenuItem("Select what the creator named", "", [=]() {
				PatchLabels l2;
				RoleCache r2;
				l2.scan(r2);
				mod->lockedModules.clear();
				for (int64_t id : APP->engine->getModuleIds()) {
					rack::engine::Module* m = APP->engine->getModule(id);
					if (!m || m == mod)
						continue;
					if (!l2.nameFor(id).empty())
						mod->lockedModules.insert(id);
				}
				mod->policy.scope = Scope::OnlyListed;
			}));
			sub->addChild(createMenuItem("Select none", "", [=]() {
				mod->lockedModules.clear();
			}));
			sub->addChild(new MenuSeparator);

			bool any = false;
			for (int64_t id : APP->engine->getModuleIds()) {
				rack::engine::Module* m = APP->engine->getModule(id);
				if (!m || m == mod || !m->model)
					continue;
				any = true;
				const std::string label = labels.describe(m);
				sub->addChild(createCheckMenuItem(label, "",
				        [=]() { return mod->lockedModules.count(id) > 0; },
				        [=]() {
					        if (mod->lockedModules.count(id))
						        mod->lockedModules.erase(id);
					        else
						        mod->lockedModules.insert(id);
				        }));
			}
			if (!any)
				sub->addChild(createMenuLabel("No other modules in the patch"));
		}));
	}
};


Model* modelUncertaintyPolicy =
        createModel<UncertaintyPolicy, UncertaintyPolicyWidget>("UncertaintyPolicy");

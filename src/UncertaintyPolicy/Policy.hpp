#pragma once
#include <rack.hpp>

#include "Roles.hpp"
#include "Signal.hpp"

#include <cmath>
#include <string>
#include <vector>


namespace upol {


/** The basis on which a filing is reviewed -- what the audition is listening
    *for*, over and above "is there still a signal".

    Going concern is the accountant's judgement that the entity will keep
    operating: the patch must still be moving after the roll. Wind-down is the
    opposite opinion, and it is a target rather than a hazard -- it withdraws any
    filing that leaves the patch still ticking, so successive attempts converge
    on a drone instead of stumbling into one. */
enum class Basis : int {
	WindDown = 0,
	Neutral = 1,
	GoingConcern = 2,
};


/** What a cable operation is *about*.

    Every candidate falls in exactly one of these, which is the point: the old
    code drew from one flat pool, so whichever class had the most ports won by
    census. In a real patch that is modulation by a wide margin -- almost every
    module has a fistful of CV inputs -- which is why every roll used to end up
    being "an LFO landed on a CV input". */
enum class Move : int {
	Voice = 0,        // audio into audio: re-routes the signal path itself
	Modulation = 1,   // anything into a CV input
	Pitch = 2,        // pitch CV into V/Oct
	Timing = 3,       // gates, triggers and clocks into trigger inputs
};
static const int MOVE_COUNT = 4;

inline const char* moveName(Move m) {
	switch (m) {
		case Move::Voice: return "voice";
		case Move::Modulation: return "modulation";
		case Move::Pitch: return "pitch";
		case Move::Timing: return "timing";
	}
	return "?";
}


/** How the chosen module list is read.

    A blocklist answers "leave this alone". A patch downloaded from Patchstorage
    needs the other question: somebody built an instrument out of forty modules
    and put a front panel on it, and the only interesting thing to randomise is
    that front panel. Saying which forty to exclude is not a workable way to
    express that. */
enum class Scope : int {
	Everything = 0,    // the whole patch, exemptions aside
	AllButListed = 1,  // the original behaviour: the list is exempt
	OnlyListed = 2,    // the list IS the patch, as far as a filing is concerned
};


/** Which class a candidate cable operation belongs to.

    First match wins, and every candidate lands in exactly one class -- that
    exclusivity is what makes the class-first draw a genuine normalisation
    rather than just another reweighting. */
inline Move classifyMove(Kind srcKind, RoleMask srcRoles, const InputSlot& dst) {
	(void) srcRoles;
	if (dst.isPitch)
		return Move::Pitch;
	if (dst.isTrig || dst.isClock)
		return Move::Timing;
	if (srcKind == Kind::Audio &&
	    (dst.isAudio || (dst.destRoles & (ROLE_PROCESSOR | ROLE_MIX))))
		return Move::Voice;
	return Move::Modulation;
}


/** Is this cable part of what makes the patch speak in events rather than hold
    a tone?

    Cutting the clock into a sequencer, or the gate into an envelope, is how a
    rhythmic patch silently becomes a drone -- and the old audition could not
    tell, because a drone is loud and unclipped. Decided from the two endpoints
    alone; no graph traversal, because this is asked once per cable per
    operation. */
inline bool onArticulationSpine(Kind srcKind, RoleMask srcRoles, const InputSlot& dst) {
	if (srcRoles & ROLE_CLOCKROOT)
		return true;   // time starts here: everything it feeds is the spine
	if (!(dst.isTrig || dst.isClock))
		return false;
	if (srcRoles & (ROLE_TIMING | ROLE_ENVELOPE))
		return true;
	return srcKind == Kind::Gate || srcKind == Kind::Trigger;
}


/** What a knob is for.

    Heuristics over the param's name and its owning module's role, and they are
    only heuristics: "Frequency" means tuning on an oscillator and cutoff on a
    filter, which is exactly why the module's role has to come into it. */
enum class ParamClass { Tuning, Time, Timbre, Level, Mode, Latch, Other };


/** What kind of button a param is attached to, which only the widget knows.

    Rack's configButton() clears randomizeEnabled for every button it makes
    (Module.hpp), momentary and latching alike -- so honouring that flag on its
    own makes every button in every plugin invisible to a randomiser. That is
    right for a momentary button, whose value is a press and is reset the moment
    it is released, and wrong for a latching one, whose value is patch content:
    a sequencer step, a route in a matrix, a mute. Telling them apart needs
    app::Switch::momentary, which is on the widget rather than the quantity. */
enum class ButtonKind { NotAButton, Momentary, Latching };


inline const char* paramClassName(ParamClass k) {
	switch (k) {
		case ParamClass::Tuning: return "tuning";
		case ParamClass::Time: return "time";
		case ParamClass::Timbre: return "timbre";
		case ParamClass::Level: return "level";
		case ParamClass::Mode: return "mode";
		case ParamClass::Latch: return "latch";
		default: return "other";
	}
}


/** Level-ish controls are the single biggest cause of a roll "killing the
    sound": turn a master fader down and everything downstream goes quiet even
    though the patch is intact. */
inline bool isLevelParam(const std::string& nameRaw) {
	const std::string n = lower(nameRaw);
	return mentions(n, {"level", "volume", "vol ", "gain", "master", "mix",
	                    "output", "amplitude", "dry/wet", "wet"});
}


inline ParamClass classifyParam(const rack::engine::ParamQuantity* pq, RoleMask roles,
                                ButtonKind button = ButtonKind::NotAButton) {
	if (!pq)
		return ParamClass::Other;

	// A latching button is its own thing: not a knob with two positions, and
	// not a mode selector either. Flipping a step on or a route through is an
	// ordinary edit, and the commonest thing worth randomising on a patch whose
	// front panel is a grid of buttons.
	if (button == ButtonKind::Latching)
		return ParamClass::Latch;

	// A labelled discrete selector -- a waveform, a filter type, an algorithm.
	// Rack models these as SwitchQuantity, and the distinction matters: the old
	// code saw only snapEnabled and so treated "sine/saw/square" exactly like a
	// numeric step count, when flipping one is a categorical change that dwarfs
	// any amount of knob travel.
	if (dynamic_cast<const rack::engine::SwitchQuantity*>(pq))
		return ParamClass::Mode;

	const std::string n = lower(pq->name);
	if (isLevelParam(pq->name))
		return ParamClass::Level;

	if (mentions(n, {"cutoff", "reso", "shape", "wave", "fold", "index", "morph",
	                 "timbre", "colour", "color", "tone", "drive", "width", "pw",
	                 "harmonic", "damp", "bright", "symmetry"}))
		return ParamClass::Timbre;

	if (mentions(n, {"tune", "pitch", "octave", "coarse", "fine", "detune",
	                 "transpose", "semitone", "v/oct", "root", "scale", "key"}))
		return ParamClass::Tuning;

	if (mentions(n, {"attack", "decay", "sustain", "release", "rate", "time",
	                 "rise", "fall", "speed", "tempo", "div", "length", "slew",
	                 "glide", "portamento", "delay", "size", "feedback"}))
		return ParamClass::Time;

	// "Frequency" on its own is ambiguous, and the module says which it is.
	if (mentions(n, {"freq"})) {
		if (roles & ROLE_MODULATION)
			return ParamClass::Time;      // an LFO's frequency is its rate
		if (roles & ROLE_PROCESSOR)
			return ParamClass::Timbre;    // a filter's is its cutoff
		if (roles & ROLE_SOURCE)
			return ParamClass::Tuning;    // an oscillator's is its note
	}

	return ParamClass::Other;
}


/** How far a knob of this class may travel, as a share of what was asked for. */
inline float classScale(ParamClass k) {
	switch (k) {
		// Throttled hardest, because a full-range move on a coarse tune takes the
		// voice out of the patch's key -- the commonest way a randomiser ruins
		// something that was working.
		case ParamClass::Tuning: return 0.35f;
		case ParamClass::Time:   return 0.60f;
		case ParamClass::Timbre: return 1.00f;   // where the interest lives
		case ParamClass::Level:  return 0.50f;
		case ParamClass::Mode:   return 0.25f;
		// A latch has two positions and one step between them, so a share of
		// its range is not a meaningful idea -- selecting it at all is the
		// decision, and SPREAD is what governs that.
		case ParamClass::Latch:  return 1.00f;
		default:                 return 0.90f;
	}
}


/** Everything the module believes about how a filing should be made.

    The three controls that live on the panel -- SPREAD, BASIS and SAFE HARBOR --
    are deliberately *not* here: they are params, so Rack persists them in the
    patch's own params array and there is exactly one source of truth for each.
    Presets set them through setValue() alongside these fields. */
struct Policy {
	// --- carried over unchanged, JSON keys included -------------------------
	bool auditionEnabled = true;
	bool allowFeedback = false;
	bool protectAudioPath = true;
	bool rollLevels = false;
	float silenceRatio = 0.2f;      // trial must keep this share of baseline RMS

	// --- targeting ----------------------------------------------------------
	/** Relative appetite for each class of cable operation. Read as a
	    distribution over classes, never multiplied by how many candidates a
	    class happens to contain -- that multiplication *is* the census bug. */
	float moveWeight[MOVE_COUNT] = {0.30f, 0.40f, 0.15f, 0.15f};
	/** How far to trust a module's declared function when sizing a knob move.
	    At 0 every knob moves by the same share of its range, which is what the
	    module did before it knew what anything was. */
	float roleDiscipline = 0.80f;
	/** How often a labelled discrete selector -- a waveform, a filter type, an
	    algorithm -- is eligible at all. Flipping one is a categorical change
	    that dwarfs any amount of knob travel, so it does not ride the same
	    probability as a cutoff nudge. */
	float modeSwitchProb = 0.20f;
	/** How often a latching button is eligible. Full by default: on a patch
	    controlled through a grid of buttons these ARE the controls, and the
	    number that move is already SPREAD's job. */
	float latchProb = 1.00f;
	/** What an input port with no name scores. */
	float blindPortWeight = 0.08f;
	bool skipBypassed = true;
	/** Leave alone any knob a mapping module is already driving. Moving one
	    achieves nothing -- the mapper writes it back on the next frame -- and
	    the budget spent on it is silently wasted. */
	bool skipMapped = true;
	Scope scope = Scope::AllButListed;

	// --- outcome ------------------------------------------------------------
	/** Under going concern, the share of the patch's prior movement a filing
	    must retain. Under wind-down, the share it must fall below. */
	float motionFloor = 0.50f;
	float motionCeil = 0.35f;

	// ---------------------------------------------------------------------

	float moveWeightTotal() const {
		float t = 0.f;
		for (int i = 0; i < MOVE_COUNT; i++)
			t += std::max(0.f, moveWeight[i]);
		return t;
	}

	json_t* toJson() const {
		json_t* rootJ = json_object();
		// Original keys, unchanged, so a patch saved by the previous build
		// loads with its settings intact.
		json_object_set_new(rootJ, "audition", json_boolean(auditionEnabled));
		json_object_set_new(rootJ, "allowFeedback", json_boolean(allowFeedback));
		json_object_set_new(rootJ, "protectAudioPath", json_boolean(protectAudioPath));
		json_object_set_new(rootJ, "rollLevels", json_boolean(rollLevels));
		json_object_set_new(rootJ, "silenceRatio", json_real(silenceRatio));

		json_t* mwJ = json_array();
		for (int i = 0; i < MOVE_COUNT; i++)
			json_array_append_new(mwJ, json_real(moveWeight[i]));
		json_object_set_new(rootJ, "moveWeights", mwJ);
		json_object_set_new(rootJ, "roleDiscipline", json_real(roleDiscipline));
		json_object_set_new(rootJ, "modeSwitchProb", json_real(modeSwitchProb));
		json_object_set_new(rootJ, "latchProb", json_real(latchProb));
		json_object_set_new(rootJ, "blindPortWeight", json_real(blindPortWeight));
		json_object_set_new(rootJ, "skipBypassed", json_boolean(skipBypassed));
		json_object_set_new(rootJ, "skipMapped", json_boolean(skipMapped));
		json_object_set_new(rootJ, "scopeMode", json_integer((int) scope));
		json_object_set_new(rootJ, "motionFloor", json_real(motionFloor));
		json_object_set_new(rootJ, "motionCeil", json_real(motionCeil));
		return rootJ;
	}

	void fromJson(json_t* rootJ) {
		if (!rootJ)
			return;
		// Every read is guarded: an absent key keeps the constructed default, so
		// a patch from the previous build opens with the new settings at their
		// defaults and its own five unchanged.
		if (json_t* j = json_object_get(rootJ, "audition"))
			auditionEnabled = json_boolean_value(j);
		if (json_t* j = json_object_get(rootJ, "allowFeedback"))
			allowFeedback = json_boolean_value(j);
		if (json_t* j = json_object_get(rootJ, "protectAudioPath"))
			protectAudioPath = json_boolean_value(j);
		if (json_t* j = json_object_get(rootJ, "rollLevels"))
			rollLevels = json_boolean_value(j);
		if (json_t* j = json_object_get(rootJ, "silenceRatio"))
			silenceRatio = (float) json_number_value(j);

		if (json_t* j = json_object_get(rootJ, "moveWeights")) {
			for (int i = 0; i < MOVE_COUNT && i < (int) json_array_size(j); i++)
				moveWeight[i] = (float) json_number_value(json_array_get(j, i));
		}
		if (json_t* j = json_object_get(rootJ, "roleDiscipline"))
			roleDiscipline = (float) json_number_value(j);
		if (json_t* j = json_object_get(rootJ, "modeSwitchProb"))
			modeSwitchProb = (float) json_number_value(j);
		if (json_t* j = json_object_get(rootJ, "latchProb"))
			latchProb = (float) json_number_value(j);
		if (json_t* j = json_object_get(rootJ, "blindPortWeight"))
			blindPortWeight = (float) json_number_value(j);
		if (json_t* j = json_object_get(rootJ, "skipBypassed"))
			skipBypassed = json_boolean_value(j);
		if (json_t* j = json_object_get(rootJ, "skipMapped"))
			skipMapped = json_boolean_value(j);
		// Absent in patches from before scoping existed, and the default is
		// AllButListed -- which is exactly what "locked" used to mean.
		if (json_t* j = json_object_get(rootJ, "scopeMode")) {
			const int v = (int) json_integer_value(j);
			scope = (Scope) rack::math::clamp(v, 0, 2);
		}
		if (json_t* j = json_object_get(rootJ, "motionFloor"))
			motionFloor = (float) json_number_value(j);
		if (json_t* j = json_object_get(rootJ, "motionCeil"))
			motionCeil = (float) json_number_value(j);
	}
};


/** A preset is a whole posture, not a knob.

    There are enough settings now that offering them one at a time would be a
    worse product than offering five that are known to work. The panel params
    each preset implies travel alongside it -- see Preset::basis/safeHarbor/
    spread, which the caller writes through setValue(). */
struct Preset {
	const char* name;
	Policy policy;
	Basis basis;
	float safeHarbor;
	float spread;
};


inline std::vector<Preset> presets() {
	std::vector<Preset> out;

	auto add = [&](const char* name, Basis basis, float safeHarbor, float spread,
	               float discipline, float voice, float mod, float pitch, float timing,
	               float blind, float modeProb) {
		Preset p;
		p.name = name;
		p.basis = basis;
		p.safeHarbor = safeHarbor;
		p.spread = spread;
		p.policy.roleDiscipline = discipline;
		p.policy.moveWeight[0] = voice;
		p.policy.moveWeight[1] = mod;
		p.policy.moveWeight[2] = pitch;
		p.policy.moveWeight[3] = timing;
		p.policy.blindPortWeight = blind;
		p.policy.modeSwitchProb = modeProb;
		out.push_back(p);
	};

	//   name                    basis                 harbor spread disc  voice mod  pitch timing blind mode
	add("Conservative filing",   Basis::GoingConcern,  0.95f, 0.25f, 1.00f, .15f, .55f, .10f, .20f, 0.04f, 0.00f);
	add("Standard filing",       Basis::GoingConcern,  0.75f, 0.50f, 0.80f, .30f, .40f, .15f, .15f, 0.08f, 0.20f);
	add("Aggressive filing",     Basis::Neutral,       0.35f, 1.00f, 0.40f, .35f, .30f, .20f, .15f, 0.20f, 0.40f);
	add("Wind-down",             Basis::WindDown,      0.00f, 0.60f, 0.50f, .50f, .40f, .05f, .05f, 0.12f, 0.30f);
	// Everything off: what the module did before it knew what anything was.
	add("Total reconstruction",  Basis::Neutral,       0.00f, 1.00f, 0.00f, .25f, .25f, .25f, .25f, 0.30f, 1.00f);
	return out;
}


/** Apply only the fields a preset actually speaks to.

    Deliberately not a wholesale assignment: auditing, feedback, output
    protection and the materiality threshold are the user's standing choices, and
    a preset is a posture, not a factory reset. The three panel params travel
    with it, but the caller writes those -- they belong to Rack. */
inline void applyPreset(Policy& p, const Preset& q) {
	p.roleDiscipline = q.policy.roleDiscipline;
	p.blindPortWeight = q.policy.blindPortWeight;
	p.modeSwitchProb = q.policy.modeSwitchProb;
	for (int i = 0; i < MOVE_COUNT; i++)
		p.moveWeight[i] = q.policy.moveWeight[i];
}


/** Does `p` still match the preset it came from? Compares only the fields a
    preset actually sets, so toggling an unrelated menu item does not make the
    label lie. */
inline bool matchesPreset(const Policy& p, const Preset& q, Basis basis,
                          float safeHarbor, float spread) {
	auto near = [](float a, float b) { return std::fabs(a - b) < 1e-3f; };
	if (basis != q.basis || !near(safeHarbor, q.safeHarbor) || !near(spread, q.spread))
		return false;
	if (!near(p.roleDiscipline, q.policy.roleDiscipline) ||
	    !near(p.blindPortWeight, q.policy.blindPortWeight) ||
	    !near(p.modeSwitchProb, q.policy.modeSwitchProb))
		return false;
	for (int i = 0; i < MOVE_COUNT; i++) {
		if (!near(p.moveWeight[i], q.policy.moveWeight[i]))
			return false;
	}
	return true;
}


} // namespace upol

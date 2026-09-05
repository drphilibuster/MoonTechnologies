#pragma once
#include <rack.hpp>

#include "LabelParse.hpp"
#include "Policy.hpp"
#include "Roles.hpp"

#include <string>
#include <unordered_map>


namespace upol {


/** What the person who built the patch called things.

    A patch downloaded from Patchstorage is somebody's instrument, and its
    modules have names in that instrument that have nothing to do with what the
    plugin author called them. "VC Frequency Divider MkII" is a module; "Rhythm
    1" is a control on a Subharmonicon. Only the second one is any use when you
    are choosing what a randomiser should be allowed to touch.

    Two sources, neither of which needs the module to be from any particular
    vendor:

      - A ParamHandle's `text`. Any module that maps a param -- PatchMaster,
        a CV mapper, MIDI-Map -- registers a handle with the engine, and the
        handle carries the mapper's label for that control.
      - An annotation module's own saved state. Label modules keep an array of
        {moduleId, text} entries, which is read back through the public
        dataToJson() rather than by knowing anything about the module. */
struct PatchLabels {
	std::unordered_map<int64_t, std::string> byModule;

	void clear() {
		byModule.clear();
	}

	/** Scan the patch. Cheap enough to run when a menu opens; not something to
	    do inside a roll. */
	void scan(RoleCache& roles) {
		clear();
		for (int64_t id : APP->engine->getModuleIds()) {
			rack::engine::Module* m = APP->engine->getModule(id);
			if (!m || !m->model)
				continue;
			// Only ask port-less modules for their state. An annotation module
			// has nothing to patch, and the restriction matters: dataToJson()
			// on a sampler or a recorder can serialise a very large buffer, and
			// this has no business paying for that to look for a label.
			if (!m->inputs.empty() || !m->outputs.empty())
				continue;
			json_t* rootJ = m->dataToJson();
			if (!rootJ)
				continue;
			readLabelsInto(rootJ, byModule);
			json_decref(rootJ);
		}
	}

	/** The creator's name for a module, or an empty string. */
	std::string nameFor(int64_t moduleId) const {
		auto it = byModule.find(moduleId);
		return it == byModule.end() ? std::string() : it->second;
	}

	/** How a module should be listed: the creator's name where there is one,
	    with the plugin's own name behind it so the row is still findable. */
	std::string describe(rack::engine::Module* m) const {
		if (!m || !m->model)
			return "?";
		const std::string plain = m->model->plugin->name + " " + m->model->name;
		const std::string given = nameFor(m->id);
		return given.empty() ? plain : given + "  (" + plain + ")";
	}
};


/** Is this param currently driven by a mapping module?

    A continuous mapper writes its target every frame, so a roll that moves the
    target is undone before anyone hears it -- it looks like the randomiser did
    nothing, and it quietly spends part of the budget doing it. The control worth
    moving is the one on the surface, which is why scoping to the surface is the
    better answer than trying to reach through it. */
inline bool isMapped(int64_t moduleId, int paramId) {
	return APP->engine->getParamHandle(moduleId, paramId) != nullptr;
}


/** Momentary or latching? Only the widget knows, so this is a UI-thread question
    and the answer is NotAButton whenever there is no widget to ask -- the module
    browser, a headless render, a module still being built.

    A momentary button's value is a press: app::Switch resets it on release, so
    writing one achieves nothing and would put a no-op in the undo history. A
    latching one is state the patch saves and reloads. */
inline ButtonKind buttonKind(rack::app::ModuleWidget* mw, int paramId) {
	if (!mw)
		return ButtonKind::NotAButton;
	rack::app::ParamWidget* pw = mw->getParam(paramId);
	if (!pw)
		return ButtonKind::NotAButton;
	rack::app::Switch* sw = dynamic_cast<rack::app::Switch*>(pw);
	if (!sw)
		return ButtonKind::NotAButton;
	return sw->momentary ? ButtonKind::Momentary : ButtonKind::Latching;
}


/** The mapper's own label for a control, if it has one. */
inline std::string mappedName(int64_t moduleId, int paramId) {
	rack::engine::ParamHandle* h = APP->engine->getParamHandle(moduleId, paramId);
	return h ? h->text : std::string();
}


} // namespace upol

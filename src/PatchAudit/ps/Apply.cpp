#include "Apply.hpp"
#include "Json.hpp"
#include "PatchFile.hpp"

#include <osdialog.h>
#include <rack.hpp>
// rack.hpp does not pull in patch.hpp, and APP->patch is only forward-declared
// in context.hpp. Included after rack.hpp so the PRIVATE guard is already armed;
// the only thing used from it is the public `autosavePath` member.
#include <patch.hpp>

#include <map>
#include <set>

using namespace rack;

namespace ps {

/** The id a module claims in the patch it came from.
 *
 * Before Rack 1.0 modules carried no "id" at all -- the id WAS the index in the
 * "modules" array, which is how Rack still reads them
 * (Rack/src/engine/Engine.cpp:1308-1311) and how the "wires" of that era
 * reference their endpoints. Without this an old patch imports nothing at all:
 * every module looks id-less and gets skipped, and every cable then has no
 * endpoint to attach to. */
static int64_t sourceModuleId(json_t* moduleJ, size_t index) {
	json_t* idJ = json_object_get(moduleJ, "id");
	return idJ ? json_integer_value(idJ) : (int64_t) index;
}

/** Builds old id -> id to actually use. Identity for everything that doesn't
    already exist in the rack; a fresh random id for anything that does. */
static std::map<int64_t, int64_t> planIds(json_t* modulesJ, int& remapped) {
	std::map<int64_t, int64_t> idMap;
	std::set<int64_t> taken;

	size_t i;
	json_t* moduleJ;
	json_array_foreach(modulesJ, i, moduleJ) {
		int64_t oldId = sourceModuleId(moduleJ, i);

		int64_t newId = oldId;
		// Collides with the open rack, or the patch repeats an id: renumber.
		while (newId < 0 || APP->scene->rack->getModule(newId) || taken.count(newId)) {
			newId = (int64_t) (random::u64() % (1ull << 53));
			remapped++;
		}
		taken.insert(newId);
		idMap[oldId] = newId;
	}
	return idMap;
}

/** Carries a module's patch storage directory into the current patch's autosave,
    under the id the module will actually have. Rack's paste drops these. */
static bool copyPatchStorage(const std::string& extractDir, int64_t oldId, int64_t newId) {
	std::string src = system::join(extractDir, "modules", std::to_string(oldId));
	if (!system::isDirectory(src))
		return false;
	std::string destParent = system::join(APP->patch->autosavePath, "modules");
	system::createDirectories(destParent);
	std::string dest = system::join(destParent, std::to_string(newId));
	try {
		return system::copy(src, dest);
	}
	catch (Exception& e) {
		WARN("PatchAudit: could not carry over patch storage for module %lld: %s",
			(long long) oldId, e.what());
		return false;
	}
}

ImportResult applyImport(std::string patchJsonPath) {
	ImportResult res;

	json_error_t err;
	JsonRef rootJ(json_load_file(patchJsonPath.c_str(), 0, &err));
	if (!rootJ) {
		WARN("PatchAudit: could not read %s: %s", patchJsonPath.c_str(), err.text);
		return res;
	}
	json_t* modulesJ = json_object_get(rootJ.get(), "modules");
	if (!modulesJ || !json_is_array(modulesJ))
		return res;

	// Everything below this line is era-agnostic except the two places Rack's own
	// loader special-cases: where positions are measured, and what the cable array
	// is called. modelFromJson, Module::fromJson and Cable::fromJson handle the
	// rest (renamed slugs, "disabled" vs "bypass", "paramId" vs "id") themselves.
	PatchEra era = eraFromVersion(jstr(rootJ.get(), "version"));

	app::RackWidget* rack = APP->scene->rack;
	std::string extractDir = system::getDirectory(patchJsonPath);

	std::map<int64_t, int64_t> idMap = planIds(modulesJ, res.remapped);

	history::ComplexAction* complexAction = new history::ComplexAction;
	complexAction->name = "import Patchstorage patch";
	DEFER({
		if (!complexAction->isEmpty())
			APP->history->push(complexAction);
		else
			delete complexAction;
	});

	rack->deselectAll();

	std::map<int64_t, app::ModuleWidget*> newModules;
	math::Vec minPos(INFINITY, INFINITY);
	math::Vec maxPos(-INFINITY, -INFINITY);

	size_t i;
	json_t* moduleJ;
	json_array_foreach(modulesJ, i, moduleJ) {
		int64_t oldId = sourceModuleId(moduleJ, i);
		int64_t newId = idMap.count(oldId) ? idMap[oldId] : oldId;

		// Patch storage has to be in place before the module is added: modules
		// read it in onAdd(), which Engine::addModule dispatches.
		if (copyPatchStorage(extractDir, oldId, newId))
			res.storage++;

		// The id stays in the JSON -- that is the entire point of this function.
		// Expander links go too; they are recomputed from position afterwards,
		// but a module that reads them itself gets a coherent pair.
		//
		// Written unconditionally rather than only when it changed, because a
		// pre-1.0 patch has no "id" to leave alone: Module::fromJson would leave
		// the module at id < 0 and Engine::addModule would hand it a random one
		// (Engine.cpp:767-771), losing the index the "wires" refer to.
		json_object_set_new(moduleJ, "id", json_integer(newId));

		engine::Module* module = NULL;
		app::ModuleWidget* mw = NULL;
		try {
			plugin::Model* model = plugin::modelFromJson(moduleJ);
			module = model->createModule();
			module->fromJson(moduleJ);      // sets id, since a new Module has id < 0
			mw = model->createModuleWidget(module);
		}
		catch (Exception& e) {
			// Almost always "plugin not installed", which the missing-modules
			// dialog has already told the user about.
			WARN("PatchAudit: skipping module: %s", e.what());
			delete module;
			res.skipped++;
			continue;
		}

		APP->engine->addModule(module);
		if (module->id != newId) {
			// Engine renumbered it anyway; keep the cable map honest.
			res.remapped++;
			newId = module->id;
		}

		json_t* posJ = json_object_get(moduleJ, "pos");
		double x = 0.0, y = 0.0;
		json_unpack(posJ, "[F, F]", &x, &y);
		// In <=v0.5 "pos" was already in pixels; from v0.6 on it is in grid units.
		math::Vec pos = math::Vec(x, y);
		if (era != PatchEra::LegacyPixelPos)
			pos = pos.mult(RACK_GRID_SIZE);
		mw->box.pos = pos.plus(RACK_OFFSET);
		minPos = minPos.min(mw->box.getTopLeft());
		maxPos = maxPos.max(mw->box.getBottomRight());

		rack->addModule(mw);
		rack->select(mw);
		newModules[oldId] = mw;
		res.modules++;
	}

	if (newModules.empty())
		return res;

	// Centre the imported patch in the viewport, the way loadSelection does.
	math::Vec selectionCenter = minPos.plus(maxPos).div(2);
	math::Vec viewCenter = rack->getViewport().getCenter();
	math::Vec delta = viewCenter.minus(selectionCenter).div(RACK_GRID_SIZE).round().mult(RACK_GRID_SIZE);
	for (std::map<int64_t, app::ModuleWidget*>::iterator it = newModules.begin();
	     it != newModules.end(); ++it)
		it->second->box.pos = it->second->box.pos.plus(delta);

	// Resolves overlaps with the modules already in the rack, and calls
	// updateExpanders().
	rack->setSelectionPosNearest(math::Vec(0, 0));

	const std::set<app::ModuleWidget*>& selected = rack->getSelected();
	for (std::set<app::ModuleWidget*>::const_iterator it = selected.begin();
	     it != selected.end(); ++it) {
		history::ModuleAdd* h = new history::ModuleAdd;
		h->setModule(*it);
		complexAction->push(h);
	}

	// --- cables -------------------------------------------------------------
	json_t* cablesJ = json_object_get(rootJ.get(), "cables");
	// In <=v0.6 cables were called wires.
	if (!cablesJ)
		cablesJ = json_object_get(rootJ.get(), "wires");
	if (cablesJ && json_is_array(cablesJ)) {
		size_t c;
		json_t* cableJ;
		json_array_foreach(cablesJ, c, cableJ) {
			// Cable::jsonStripIds is PRIVATE to plugins, and all it does is drop
			// the cable's own id so the Engine assigns a fresh one.
			json_object_del(cableJ, "id");

			json_t* inJ = json_object_get(cableJ, "inputModuleId");
			json_t* outJ = json_object_get(cableJ, "outputModuleId");
			if (!inJ || !outJ)
				continue;
			std::map<int64_t, app::ModuleWidget*>::iterator inIt =
				newModules.find(json_integer_value(inJ));
			std::map<int64_t, app::ModuleWidget*>::iterator outIt =
				newModules.find(json_integer_value(outJ));
			// A cable to a module we skipped has nothing to attach to.
			if (inIt == newModules.end() || outIt == newModules.end())
				continue;
			json_object_set_new(cableJ, "inputModuleId", json_integer(inIt->second->module->id));
			json_object_set_new(cableJ, "outputModuleId", json_integer(outIt->second->module->id));

			engine::Cable* cable = new engine::Cable;
			try {
				cable->fromJson(cableJ);
				APP->engine->addCable(cable);
			}
			catch (Exception& e) {
				WARN("PatchAudit: cannot add cable: %s", e.what());
				delete cable;
				continue;
			}

			app::CableWidget* cw = new app::CableWidget;
			cw->setCable(cable);
			cw->fromJson(cableJ);
			rack->addCable(cw);
			res.cables++;

			history::CableAdd* h = new history::CableAdd;
			h->setCable(cw);
			complexAction->push(h);
		}
	}

	res.ok = true;
	return res;
}

void applySaveToDisk(std::string archivePath, std::string suggestedFilename) {
	std::string dir = asset::user("patches");
	system::createDirectories(dir);
	if (suggestedFilename.empty())
		suggestedFilename = "patch.vcv";

	osdialog_filters* filters = osdialog_filters_parse("VCV Rack patch (.vcv):vcv");
	DEFER({ osdialog_filters_free(filters); });
	char* path = osdialog_file(OSDIALOG_SAVE, dir.c_str(), suggestedFilename.c_str(), filters);
	if (!path)
		return;
	std::string dest = path;
	std::free(path);

	if (system::getExtension(dest).empty())
		dest += ".vcv";
	if (!system::copy(archivePath, dest))
		osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK, "Could not save the patch.");
}

} // namespace ps

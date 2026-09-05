#pragma once
#include <string>

namespace ps {

struct ImportResult {
	int modules = 0;
	int cables = 0;
	int skipped = 0;      // modules whose plugin isn't installed
	int remapped = 0;     // modules whose id collided and had to be renumbered
	int storage = 0;      // per-module patch storage directories carried over
	bool ok = false;
};

/** Pastes an extracted patch.json into the current rack, centred in the
 * viewport, as one undoable action.
 *
 * This is a reimplementation of RackWidget::pasteJsonAction rather than a call
 * to it, for one reason: Rack's paste calls Module::jsonStripIds() and lets the
 * Engine assign fresh random ids. Any module that stores a reference to another
 * module *by id* inside its own data blob -- MindMeld PatchMaster's `maps`,
 * stoermelder's MIDI-CAT and uMAP, anything built on ParamHandle -- then points
 * at ids that no longer exist, and every mapping in the patch arrives dead.
 *
 * Nothing requires the renumbering. Module::fromJson only sets the id when it is
 * still unset (Rack/src/engine/Module.cpp:174-180), and Engine::addModule keeps
 * whatever id the module has unless it collides with one already in the rack
 * (Rack/src/engine/Engine.cpp:767-771) -- and it re-points existing ParamHandles
 * at the module as it goes. Ids are 53-bit random, so a collision against the
 * user's open rack is vanishingly unlikely; when one does happen only that
 * module is renumbered, and the result says so.
 *
 * Keeping the ids also lets the per-module patch storage directories in the
 * archive be carried across, which Rack's paste drops.
 *
 * Safe to call from Widget::step(): it only appends to the rack's module list.
 */
ImportResult applyImport(std::string patchJsonPath);

/* There is deliberately no applyReplace().
 *
 * "Open this patch" would mean APP->patch->loadAction(), which calls
 * RackWidget::clear() and deletes every ModuleWidget -- including this browser,
 * which the user would then have to re-add from Rack's module browser before
 * they could look at another patch. It is also the one operation here that is
 * genuinely hazardous to implement: clear() erases our node from
 * moduleContainer->children (Rack/src/app/RackWidget.cpp:256-264) while
 * Widget::step() is iterating that list (Rack/src/widget/Widget.cpp:249-267), so
 * it is undefined behaviour anywhere except a ui::MenuItem::onAction.
 *
 * Import does everything Open did except discard your rack, and "Save to disk"
 * plus File > Open covers the rest. So the feature is cut, and with it the whole
 * failure mode. */

/** Copies the cached archive somewhere the user picks. */
void applySaveToDisk(std::string archivePath, std::string suggestedFilename);

} // namespace ps

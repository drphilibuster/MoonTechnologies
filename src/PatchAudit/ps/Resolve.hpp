#pragma once
#include "VcvLibrary.hpp"

#include <set>
#include <string>

namespace ps {
/** Answering "can this user run this module, and if not, what can they do about
    it" -- in one place, so the badge, the missing-modules dialog and the importer
    cannot disagree. They used to: the badge and the scan asked
    plugin::getModel with the patch's raw slug, while the importer asked
    plugin::modelFromJson, which normalizes the slug and applies Rack's rename
    tables first. The importer was right. */
namespace resolve {

/** The slug as Rack will actually look it up.
 *
 * Rack ≤1.x let a plugin call itself "Aepelzens Modules" or "Autodafe-Drum Kit",
 * spaces and all, and patches from that era still carry those strings.
 * plugin::normalizeSlug strips everything that isn't alphanumeric, '-' or '_',
 * and Rack runs it before every lookup (Rack/src/plugin.cpp:504-512). Skipping it
 * meant 'Autodafe-Drum Kit' never matched Autodafe-DrumKit, which is sitting in
 * the library, available, with a build for this machine.
 *
 * Pure string work with no globals, so it is safe on a worker thread. */
std::string canonical(const std::string& slug);

/** Exactly the predicate Rack's loader uses: normalize, then getModelFallback,
    which applies the plugin and module rename tables (VultModulesFree↔VultModules
    and friends). Normalize first -- those tables are keyed on normalized slugs.
 *
 * MUST run on the UI thread: it walks plugin::plugins. See ps/ModuleScan.hpp. */
bool moduleResolves(const std::string& pluginSlug, const std::string& modelSlug);

/** Why a plugin's modules are missing, and whether the user can do anything. */
enum class Availability {
	/** The manifests haven't loaded (or the fetch failed). Nothing below can be
	    trusted yet, and guessing here is how "not in the library" ends up next to
	    a plugin that is very much in the library. */
	LibraryUnknown,
	LibraryInstallable,   ///< available, builds for this arch -> install it
	RequiresPurchase,     ///< premium, not owned
	UpdateAvailable,      ///< installed, and the library ships something newer
	ModuleRetired,        ///< installed and current; this model no longer exists
	NeedsNewerRack,       ///< minRackVersion is beyond this Rack
	NoBuildForPlatform,   ///< available, but not for this OS/CPU
	DelistedOpenSource,   ///< gone from the library, but the source is published
	Delisted,             ///< gone from the library, no source
	NotInLibrary,         ///< the library has never heard of this slug
};

struct Verdict {
	Availability status = Availability::LibraryUnknown;
	std::string displayName;
	std::string actionUrl;     ///< empty when there is genuinely nothing to open
	std::string actionLabel;   ///< the short right-hand phrase
	std::string detail;        ///< optional second line
	/** No path to these modules on this machine. Drives the badge and the sort;
	    a patch made of these will never run here however much you install. */
	bool deadEnd = false;
};

/** Classifies one plugin the patch needs but this Rack can't fully supply.
 *
 * `rawPluginSlug` is the slug as the patch spells it. `missingModels` are the
 * model slugs (raw) that failed to resolve. UI thread only -- it inspects the
 * installed plugin list to tell "retired" from "not installed". */
Verdict classifyPlugin(const std::string& rawPluginSlug,
                       const std::set<std::string>& missingModels);

} // namespace resolve
} // namespace ps

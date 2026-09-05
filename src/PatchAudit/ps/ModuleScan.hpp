#pragma once
#include "Resolve.hpp"

#include <set>
#include <string>
#include <vector>

namespace ps {

struct MissingEntry {
	/** As the patch spells it, and as Rack would look it up. They differ for
	    every pre-2.0 patch that used a slug with spaces in it. */
	std::string pluginSlug;
	std::string canonicalSlug;
	/** Distinct model slugs of this plugin that the patch wants and this Rack
	    can't supply. */
	std::set<std::string> modelSlugs;
	/** How many module *instances* those account for. A patch using three copies
	    of one missing module is one entry, one model slug, three instances --
	    which is why the row counts never used to add up to the header. */
	int instances = 0;
	/** What the user can actually do about it. */
	resolve::Verdict verdict;
};

struct ScanResult {
	std::vector<MissingEntry> missing;
	int totalModules = 0;
	int missingModules = 0;
	/** Missing instances with no route to them on this machine. */
	int deadEndModules = 0;
	/** False when the VCV Library manifests hadn't loaded, in which case every
	    verdict is provisional and the UI must say so rather than guess. */
	bool libraryKnown = false;

	bool ok() const { return missing.empty(); }
	/** https://library.vcvrack.com/?modules=Plugin/Model,... -- the same link
	    Rack builds in patch::Manager::checkUnavailableModulesJson, but limited to
	    plugins the library can actually offer. Empty when none qualify. */
	std::string combinedLibraryUrl() const;
	bool hasInstallable() const;
};

/** Scans an extracted patch.json for modules this Rack can't instantiate.
 *
 * MUST be called on the UI thread: it walks plugin::plugins through
 * ps::resolve::moduleResolves, and nothing documents that container as
 * thread-safe while the library sync can mutate it.
 *
 * Mirrors Rack/src/patch.cpp:564-604 but returns data instead of throwing up a
 * blocking osdialog modal. */
ScanResult scanPatchFile(const std::string& patchJsonPath);

} // namespace ps

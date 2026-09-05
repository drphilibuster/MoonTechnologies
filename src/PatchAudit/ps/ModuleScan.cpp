#include "ModuleScan.hpp"
#include "Json.hpp"

#include <rack.hpp>

#include <map>

using namespace rack;

namespace ps {

bool ScanResult::hasInstallable() const {
	for (size_t i = 0; i < missing.size(); i++) {
		const resolve::Availability a = missing[i].verdict.status;
		if (a == resolve::Availability::LibraryInstallable
		    || a == resolve::Availability::RequiresPurchase
		    || a == resolve::Availability::UpdateAvailable)
			return true;
	}
	return false;
}

std::string ScanResult::combinedLibraryUrl() const {
	std::string url = "https://library.vcvrack.com/?modules=";
	bool first = true;
	for (size_t i = 0; i < missing.size(); i++) {
		const MissingEntry& m = missing[i];
		// Only plugins the library can actually offer. Including a retired or
		// delisted slug produces a page that helps nobody, and including a raw
		// pre-2.0 slug put literal spaces into the URL.
		const resolve::Availability a = m.verdict.status;
		if (a != resolve::Availability::LibraryInstallable
		    && a != resolve::Availability::RequiresPurchase
		    && a != resolve::Availability::UpdateAvailable)
			continue;
		for (std::set<std::string>::const_iterator it = m.modelSlugs.begin();
		     it != m.modelSlugs.end(); ++it) {
			if (!first)
				url += ",";
			url += m.canonicalSlug + "/" + resolve::canonical(*it);
			first = false;
		}
	}
	return first ? "" : url;
}

ScanResult scanPatchFile(const std::string& patchJsonPath) {
	ScanResult out;
	out.libraryKnown = vcvlib::ready();

	json_error_t err;
	JsonRef rootJ(json_load_file(patchJsonPath.c_str(), 0, &err));
	if (!rootJ)
		return out;
	json_t* modulesJ = json_object_get(rootJ.get(), "modules");
	if (!modulesJ || !json_is_array(modulesJ))
		return out;

	// Keyed on the canonical slug, so a patch that spells the same plugin two
	// ways collapses into one row rather than two.
	std::map<std::string, MissingEntry> byPlugin;
	size_t i;
	json_t* moduleJ;
	json_array_foreach(modulesJ, i, moduleJ) {
		std::string pluginSlug = jstr(moduleJ, "plugin");
		std::string modelSlug = jstr(moduleJ, "model");
		if (pluginSlug.empty() || modelSlug.empty())
			continue;
		out.totalModules++;

		if (resolve::moduleResolves(pluginSlug, modelSlug))
			continue;

		out.missingModules++;
		MissingEntry& e = byPlugin[resolve::canonical(pluginSlug)];
		if (e.pluginSlug.empty()) {
			e.pluginSlug = pluginSlug;
			e.canonicalSlug = resolve::canonical(pluginSlug);
		}
		e.modelSlugs.insert(modelSlug);
		e.instances++;
	}

	for (std::map<std::string, MissingEntry>::iterator it = byPlugin.begin();
	     it != byPlugin.end(); ++it) {
		MissingEntry e = it->second;
		e.verdict = resolve::classifyPlugin(e.pluginSlug, e.modelSlugs);
		if (e.verdict.deadEnd)
			out.deadEndModules += e.instances;
		out.missing.push_back(e);
	}
	return out;
}

} // namespace ps

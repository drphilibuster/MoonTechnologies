#include "Resolve.hpp"

#include <rack.hpp>

#include <cctype>
#include <set>
#include <vector>

using namespace rack;

namespace ps {
namespace resolve {

std::string canonical(const std::string& slug) {
	return plugin::normalizeSlug(slug);
}

bool moduleResolves(const std::string& pluginSlug, const std::string& modelSlug) {
	if (pluginSlug.empty() || modelSlug.empty())
		return false;
	return plugin::getModelFallback(canonical(pluginSlug), canonical(modelSlug)) != NULL;
}

/** The installed plugin, if this Rack has it. */
static plugin::Plugin* installedPlugin(const std::string& canonicalSlug) {
	return plugin::getPluginFallback(canonicalSlug);
}

/** True when the library ships something newer than what's installed.
 *
 * Mirrors the shape of Rack's own update test: only 2.x builds count, and the
 * library version has to actually be newer. string::Version::operator< is
 * declared non-const (Rack-SDK/include/string.hpp:181), so these have to be
 * non-const locals -- a const Version will not compile. */
static bool libraryIsNewer(const std::string& installedVersion,
                           const std::string& libraryVersion) {
	if (libraryVersion.empty() || installedVersion.empty())
		return false;
	if (libraryVersion.compare(0, 2, "2.") != 0)
		return false;
	if (libraryVersion == installedVersion)
		return false;
	string::Version installed(installedVersion);
	string::Version library(libraryVersion);
	return installed < library;
}

/** Lowercased words of a slug or name, splitting camelCase and separators.
    "QuadEuclideanRhythm" -> {quad, euclidean, rhythm}. Words shorter than three
    characters are dropped; they carry no signal and match everything. */
static std::set<std::string> words(const std::string& s) {
	std::set<std::string> out;
	std::string cur;
	for (size_t i = 0; i < s.size(); i++) {
		unsigned char c = (unsigned char) s[i];
		bool sep = (c == '-' || c == '_' || c == ' ');
		bool camel = i > 0 && std::isupper(c)
		    && (std::islower((unsigned char) s[i - 1])
		        || (i + 1 < s.size() && std::islower((unsigned char) s[i + 1])));
		if (sep || camel) {
			if (cur.size() >= 3) out.insert(cur);
			cur.clear();
			if (sep) continue;
		}
		cur += (char) std::tolower(c);
	}
	if (cur.size() >= 3) out.insert(cur);
	return out;
}

/** The installed module whose name most looks like the one the patch wanted.
 *
 * For the case this exists to explain: a patch asks for
 * FrozenWasteland/QuadEuclideanRhythm; the plugin is installed and current, and
 * the module is simply gone -- renamed to QuadAlgorithmicRhythm. Rack's answer to
 * renames is a hand-curated table with one entry in it, so nothing resolves this
 * automatically. Naming the likely successor at least ends the search.
 *
 * Deliberately NOT the SDK's fuzzysearch::Database. That matcher wants every
 * query word to hit, so it scores this exact case 0.000 -- indistinguishable from
 * a query of pure gibberish -- because the middle word is the one that changed.
 * Shared-word overlap is what actually separates a rename from an unrelated
 * module.
 *
 * Answers only when it is sure: at least two words in common, and a single
 * best candidate. A tie means several expanders share the family name, which is
 * a guess not worth making. Advisory in any case -- nothing may substitute this
 * into a patch, which would change how the patch sounds without saying so. */
static std::string suggestReplacement(plugin::Plugin* p, const std::string& missingSlug) {
	if (!p)
		return "";
	std::set<std::string> want = words(missingSlug);
	if (want.size() < 2)
		return "";

	// Strongest signal first: a model whose NAME is the slug the patch asked for.
	// That is what a v1-to-v2 slug tidy-up leaves behind -- Autodafe's
	// "Drums - Snare" became the slug DrumsSnare with the old string kept as the
	// display name -- and it is not a guess at all.
	std::string canonicalMissing = canonical(missingSlug);
	for (std::list<plugin::Model*>::const_iterator it = p->models.begin();
	     it != p->models.end(); ++it) {
		plugin::Model* m = *it;
		if (m && !m->name.empty() && canonical(m->name) == canonicalMissing)
			return m->name;
	}

	std::string best;
	size_t bestScore = 0;
	bool tied = false;
	for (std::list<plugin::Model*>::const_iterator it = p->models.begin();
	     it != p->models.end(); ++it) {
		plugin::Model* m = *it;
		if (!m)
			continue;
		std::set<std::string> have = words(m->slug);
		std::set<std::string> fromName = words(m->name);
		have.insert(fromName.begin(), fromName.end());

		size_t overlap = 0;
		for (std::set<std::string>::const_iterator w = want.begin(); w != want.end(); ++w)
			if (have.count(*w))
				overlap++;
		if (overlap < 2)
			continue;
		if (overlap > bestScore) {
			bestScore = overlap;
			best = m->name.empty() ? m->slug : m->name;
			tied = false;
		}
		else if (overlap == bestScore) {
			tied = true;
		}
	}
	return tied ? "" : best;
}

/** Joins slugs for a detail line: "A, B and C". */
static std::string listOf(const std::set<std::string>& items) {
	std::string out;
	size_t i = 0, n = items.size();
	for (std::set<std::string>::const_iterator it = items.begin(); it != items.end(); ++it, ++i) {
		if (i > 0)
			out += (i + 1 == n) ? " and " : ", ";
		out += *it;
	}
	return out;
}

Verdict classifyPlugin(const std::string& rawPluginSlug,
                       const std::set<std::string>& missingModels) {
	std::string slug = canonical(rawPluginSlug);
	Verdict v;
	v.displayName = slug;
	v.detail = listOf(missingModels);

	// 1. Nothing below can be trusted until the manifests are in.
	if (!vcvlib::ready()) {
		v.status = Availability::LibraryUnknown;
		v.actionLabel = vcvlib::state() == vcvlib::State::Failed
			? "library unreachable" : "checking library...";
		return v;
	}

	ManifestEntry man = vcvlib::lookup(slug);
	if (man.found)
		v.displayName = man.displayName();

	plugin::Plugin* installed = installedPlugin(slug);
	int n = (int) missingModels.size();

	// 2. The library has never heard of it. Genuinely nothing to offer.
	if (!man.found) {
		v.status = Availability::NotInLibrary;
		v.actionLabel = "not in the library";
		v.deadEnd = true;
		return v;
	}

	// 3. Delisted. Checked BEFORE premium: 13 of the delisted plugins are also
	// flagged premium, and pointing someone at a shop for something that is no
	// longer sold is worse than telling them it is gone.
	if (!man.available) {
		v.deadEnd = true;
		if (!man.sourceUrl.empty()) {
			v.status = Availability::DelistedOpenSource;
			v.actionLabel = "delisted -- source on GitHub";
			v.actionUrl = man.sourceUrl;
		}
		else {
			v.status = Availability::Delisted;
			v.actionLabel = "no longer in the library";
		}
		return v;
	}

	// 4. In the library, but not built for this machine.
	if (!man.archOk) {
		v.status = Availability::NoBuildForPlatform;
		v.actionLabel = "no build for this platform";
		v.actionUrl = man.libraryUrl();
		v.deadEnd = true;
		return v;
	}

	// 5. Needs a newer Rack than this one.
	if (!man.minRackVersion.empty()) {
		string::Version mine(APP_VERSION);
		string::Version needed(man.minRackVersion);
		if (mine < needed) {
			v.status = Availability::NeedsNewerRack;
			v.actionLabel = string::f("needs Rack %s", man.minRackVersion.c_str());
			v.actionUrl = man.libraryUrl();
			return v;
		}
	}

	// 6. Already installed, so this is about the module, not the plugin. Checked
	// before premium so an owned premium plugin gets the update path rather than
	// a buy link.
	if (installed) {
		if (libraryIsNewer(installed->version, man.version)) {
			v.status = Availability::UpdateAvailable;
			v.actionLabel = string::f("update to %s", man.version.c_str());
			v.actionUrl = man.libraryUrl();
			return v;
		}
		// Installed, current, and the module still isn't there: it was renamed or
		// removed. Never say "update" here -- there is no update, and the library
		// page will just say "Installed".
		v.status = Availability::ModuleRetired;
		v.actionLabel = string::f("not in %s %s", v.displayName.c_str(),
			installed->version.c_str());
		v.deadEnd = true;
		// A changelog is the one place a rename is actually written down, and 199
		// plugins publish one. It cannot make the patch run, but it is the only
		// honest answer to "where did my module go".
		if (!man.changelogUrl.empty()) {
			v.actionUrl = man.changelogUrl;
			v.actionLabel += " -- changelog";
		}
		// One line per missing model, each with its own suggestion where there is
		// a confident one. A plugin can retire several modules at once -- Autodafe
		// renamed its whole drum kit -- and naming only the first would leave the
		// rest looking unexplained.
		std::string lines;
		for (std::set<std::string>::const_iterator it = missingModels.begin();
		     it != missingModels.end(); ++it) {
			if (!lines.empty())
				lines += "\n";
			std::string guess = suggestReplacement(installed, *it);
			lines += guess.empty() ? *it
			                       : string::f("%s -- now %s?", it->c_str(), guess.c_str());
		}
		v.detail = lines;
		return v;
	}

	// 7. Premium and not owned.
	if (man.premium) {
		v.status = Availability::RequiresPurchase;
		v.actionLabel = "premium";
		v.actionUrl = man.libraryUrl();
		return v;
	}

	// 8. Just install it.
	v.status = Availability::LibraryInstallable;
	v.actionLabel = string::f("%d module%s", n, n == 1 ? "" : "s");
	v.actionUrl = man.libraryUrl();
	return v;
}

} // namespace resolve
} // namespace ps

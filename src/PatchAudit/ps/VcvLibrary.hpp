#pragma once
#include <string>

namespace ps {

/** One plugin as the VCV Library describes it.
    Source: GET https://api.vcvrack.com/library/manifests?version=2 -- the only
    endpoint that exists; per-plugin manifest URLs 404. */
struct ManifestEntry {
	std::string slug;
	std::string name;
	std::string brand;
	std::string author;
	std::string pluginUrl;     ///< homepage or README
	std::string sourceUrl;     ///< repository, when the plugin is open source
	std::string changelogUrl;  ///< where a module rename or removal is recorded
	std::string version;       ///< the version the library currently ships
	std::string minRackVersion;
	bool premium = false;
	bool openSource = false;
	/** The library still ships this plugin. See the parser for why this is key
	    presence rather than a boolean test. */
	bool available = false;
	/** The library ships a build for the architecture Rack is running on. A
	    plugin can be perfectly available and still be uninstallable here. */
	bool archOk = false;
	bool found = false;

	/** Brand if the plugin has one, else its name, else the raw slug. */
	std::string displayName() const;
	std::string libraryUrl() const;
};

namespace vcvlib {

/** Where the one-time manifest fetch has got to.
 *
 * Worth distinguishing, because "the library has never heard of this plugin" and
 * "we haven't asked the library yet" look identical from a lookup() miss, and
 * telling a user their plugin doesn't exist when the truth is that a fetch is
 * still in flight is the worst answer of the three. */
enum class State { Idle, Loading, Ready, Failed };

State state();
inline bool ready() { return state() == State::Ready; }

/** Queues the one-time manifest fetch. Cheap to call repeatedly: it no-ops once
    loaded or in flight, and after a failure it waits out a retry gap rather than
    hammering the endpoint. */
void ensureLoaded();
/** Forgets a failure and refetches now -- for a user-driven "Retry". */
void retry();

/** True once, each time the state changes. Polled by the UI so a page audited
    before the manifests landed can be re-resolved once they do. */
bool takeDirty();

/** Thread-safe. Returns an entry with found == false for an unknown slug.
    The slug must already be normalized -- see ps/Resolve.hpp. */
ManifestEntry lookup(const std::string& pluginSlug);
int count();

} // namespace vcvlib
} // namespace ps

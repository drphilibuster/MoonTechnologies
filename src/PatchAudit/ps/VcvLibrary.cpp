#include "VcvLibrary.hpp"
#include "Api.hpp"
#include "HttpCache.hpp"
#include "JobRunner.hpp"
#include "Json.hpp"

#include <rack.hpp>

#include <atomic>
#include <map>
#include <mutex>

using namespace rack;

namespace ps {

std::string ManifestEntry::displayName() const {
	if (!brand.empty()) return brand;
	if (!name.empty()) return name;
	return slug;
}

std::string ManifestEntry::libraryUrl() const {
	return "https://library.vcvrack.com/" + slug;
}

namespace vcvlib {

static const char* const MANIFESTS_URL = "https://api.vcvrack.com/library/manifests?version=2";

/** How long to wait after a failed fetch before another ensureLoaded() will try
    again. Long enough that a browse session with no network doesn't retry per
    keystroke, short enough that plugging the cable back in fixes it. */
static const double RETRY_GAP = 60.0;

static std::mutex g_mutex;
static std::map<std::string, ManifestEntry> g_manifests;
static std::atomic<int> g_state{(int) State::Idle};
static std::atomic<bool> g_dirty{false};
static std::atomic<bool> g_inFlight{false};
static std::atomic<double> g_lastFailure{0.0};

State state() { return (State) g_state.load(); }

bool takeDirty() { return g_dirty.exchange(false); }

int count() {
	std::lock_guard<std::mutex> lock(g_mutex);
	return (int) g_manifests.size();
}

static void setState(State s) {
	g_state = (int) s;
	g_dirty = true;
}

/** The architecture string the library keys its builds by, e.g. "mac-arm64". */
static std::string thisArch() {
	return std::string(APP_OS) + "-" + APP_CPU;
}

static void fetchJob() {
	ApiResponse resp;
	JsonRef rootJ(getJson(MANIFESTS_URL, cache::TTL_MANIFESTS, &resp, NULL, true));
	// This endpoint returns a bare object, so judge the response by its content.
	json_t* manifestsJ = rootJ ? json_object_get(rootJ.get(), "manifests") : NULL;
	if (!manifestsJ || !json_is_object(manifestsJ)) {
		g_lastFailure = system::getTime();
		g_inFlight = false;
		setState(State::Failed);
		WARN("PatchAudit: could not load VCV Library manifests");
		return;
	}

	std::string arch = thisArch();
	std::map<std::string, ManifestEntry> parsed;
	const char* key;
	json_t* value;
	json_object_foreach(manifestsJ, key, value) {
		ManifestEntry e;
		e.slug = key;
		e.name = jstr(value, "name");
		e.brand = jstr(value, "brand");
		e.author = jstr(value, "author");
		e.pluginUrl = jstr(value, "pluginUrl");
		e.sourceUrl = jstr(value, "sourceUrl");
		e.changelogUrl = jstr(value, "changelogUrl");
		e.version = jstr(value, "version");
		e.minRackVersion = jstr(value, "minRackVersion");

		json_t* premiumJ = json_object_get(value, "premium");
		e.premium = premiumJ && json_is_true(premiumJ);
		json_t* openJ = json_object_get(value, "openSource");
		e.openSource = openJ && json_is_true(openJ);

		// A delisted plugin does not carry `available: false` -- it simply has no
		// `available` key. Measured against the live endpoint: of 558 manifests,
		// 456 have the key and every one of those is true; the other 102 omit it,
		// and those are exactly the plugins the library shows as "Unavailable".
		//
		// This used to fall back to `status == "available"` for entries with no
		// boolean, which reads sensibly and is precisely backwards. Only 67
		// entries carry `status` at all, every one of them is in the delisted 102,
		// and every one says "available" -- the field is vestigial. So the
		// fallback whitelisted 67 delisted plugins, and the default argument
		// whitelisted the remaining 35. All 102 were reported as installable.
		json_t* availableJ = json_object_get(value, "available");
		e.available = availableJ && json_is_true(availableJ);

		// `arches` is present on exactly the same 456 entries as `available`, and
		// says which builds exist. 15 available plugins have no mac-arm64 build,
		// so "in the library" and "installable here" are different questions.
		json_t* archesJ = json_object_get(value, "arches");
		if (archesJ && json_is_object(archesJ)) {
			json_t* mineJ = json_object_get(archesJ, arch.c_str());
			e.archOk = mineJ && json_is_true(mineJ);
		}

		e.found = true;
		parsed[e.slug] = e;
	}

	{
		std::lock_guard<std::mutex> lock(g_mutex);
		g_manifests.swap(parsed);
	}
	g_inFlight = false;
	setState(State::Ready);
	INFO("PatchAudit: loaded %d VCV Library manifests for %s", count(), arch.c_str());
}

static void submit() {
	if (g_inFlight.exchange(true))
		return;
	setState(State::Loading);
	JobRunner::global().submit(Lane::Api, &fetchJob);
}

void ensureLoaded() {
	State s = state();
	if (s == State::Ready || s == State::Loading)
		return;
	if (s == State::Failed && system::getTime() - g_lastFailure.load() < RETRY_GAP)
		return;
	submit();
}

void retry() {
	if (state() == State::Ready)
		return;
	g_lastFailure = 0.0;
	submit();
}

ManifestEntry lookup(const std::string& pluginSlug) {
	std::lock_guard<std::mutex> lock(g_mutex);
	std::map<std::string, ManifestEntry>::const_iterator it = g_manifests.find(pluginSlug);
	if (it == g_manifests.end()) {
		ManifestEntry e;
		e.slug = pluginSlug;
		return e;
	}
	return it->second;
}

} // namespace vcvlib
} // namespace ps

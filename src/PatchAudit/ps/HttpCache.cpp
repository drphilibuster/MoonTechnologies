#include "HttpCache.hpp"
#include "Json.hpp"

#include <rack.hpp>

#include <sys/stat.h>

#include <algorithm>
#include <atomic>
#include <vector>

using namespace rack;

namespace ps {
namespace cache {

static std::atomic<bool> g_enabled{true};

void setEnabled(bool e) { g_enabled = e; }
bool enabled() { return g_enabled.load(); }

/** FNV-1a over the full URL. The SDK ships no hash we can borrow, and the key
    only has to be stable and collision-unlikely; the stored URL is compared on
    read so a collision degrades to a miss rather than to wrong data. */
static std::string hashUrl(const std::string& url) {
	uint64_t h = 1469598103934665603ull;
	for (size_t i = 0; i < url.size(); i++) {
		h ^= (unsigned char) url[i];
		h *= 1099511628211ull;
	}
	return string::f("%016llx", (unsigned long long) h);
}

std::string root()      { return asset::user("PatchAudit"); }
std::string apiDir()    { return root() + "/cache/api"; }
std::string patchDir()  { return root() + "/cache/patches"; }

static std::string apiPath(const std::string& url) {
	return apiDir() + "/" + hashUrl(url) + ".json";
}

json_t* loadJson(const std::string& url, double maxAgeSec) {
	if (!enabled())
		return NULL;
	std::string path = apiPath(url);
	if (!system::isFile(path))
		return NULL;

	json_error_t err;
	JsonRef rootJ(json_load_file(path.c_str(), 0, &err));
	if (!rootJ)
		return NULL;

	// A hash collision would hand us a different URL's body: treat as a miss.
	if (jstr(rootJ.get(), "url") != url)
		return NULL;

	double fetchedAt = (double) jint(rootJ.get(), "fetchedAt", 0);
	if (system::getUnixTime() - fetchedAt > maxAgeSec)
		return NULL;

	json_t* body = json_object_get(rootJ.get(), "body");
	if (!body)
		return NULL;
	return json_incref(body);
}

void storeJson(const std::string& url, json_t* body) {
	if (!enabled() || !body)
		return;
	// createDirectories returns false when the directory already exists, so its
	// return value says nothing useful here; check the directory itself.
	system::createDirectories(apiDir());
	if (!system::isDirectory(apiDir()))
		return;
	JsonRef wrapper(json_object());
	json_object_set_new(wrapper.get(), "url", json_string(url.c_str()));
	json_object_set_new(wrapper.get(), "fetchedAt", json_integer((json_int_t) system::getUnixTime()));
	json_object_set(wrapper.get(), "body", body);
	json_dump_file(wrapper.get(), apiPath(url).c_str(), JSON_COMPACT);
}

/** Keeps only characters that are safe in a filename on every platform. */
static std::string sanitize(const std::string& s, size_t maxLen) {
	std::string out;
	for (size_t i = 0; i < s.size() && out.size() < maxLen; i++) {
		char c = s[i];
		bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
		       || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_';
		out += ok ? c : '_';
	}
	return out;
}

std::string archivePath(int64_t patchId, int64_t fileId, const std::string& updatedAt,
                        const std::string& filename) {
	// updatedAt in the name IS the invalidation policy: a re-uploaded patch gets
	// a different path, misses, and re-downloads.
	std::string stamp = sanitize(updatedAt, 24);
	std::string name = sanitize(system::getFilename(filename), 48);
	if (name.empty())
		name = "patch.vcv";
	return patchDir() + "/" + string::f("%lld-%lld-%s-%s",
		(long long) patchId, (long long) fileId, stamp.c_str(), name.c_str());
}

static std::string sessionDir() {
	return system::getTempDirectory() + "/PatchAudit";
}

std::string extractDir(int64_t patchId) {
	return sessionDir() + "/" + string::f("extract-%lld", (long long) patchId);
}

static uint64_t dirSize(const std::string& dir) {
	if (!system::isDirectory(dir))
		return 0;
	uint64_t total = 0;
	std::vector<std::string> entries = system::getEntries(dir);
	for (size_t i = 0; i < entries.size(); i++) {
		if (system::isDirectory(entries[i]))
			continue;
		try {
			total += (uint64_t) system::getFileSize(entries[i]);
		}
		catch (Exception& e) {}
	}
	return total;
}

uint64_t sizeOnDisk() {
	return dirSize(patchDir()) + dirSize(apiDir());
}

/** Last-modified time in Unix seconds. The SDK exposes file size but not file
    time, and the LRU needs an age, so this drops to stat(). */
static double fileMTime(const std::string& path) {
	struct stat st;
	if (stat(path.c_str(), &st) != 0)
		return 0.0;
	return (double) st.st_mtime;
}

struct Aged {
	std::string path;
	double mtime;
	uint64_t size;
	bool operator<(const Aged& o) const { return mtime < o.mtime; }
};

void pruneOnStartup() {
	// Extractions are scratch: nothing outlives the session that made them.
	try {
		if (system::isDirectory(sessionDir()))
			system::removeRecursively(sessionDir());
	}
	catch (Exception& e) {}

	if (!system::isDirectory(patchDir()))
		return;
	std::vector<Aged> files;
	uint64_t total = 0;
	std::vector<std::string> entries = system::getEntries(patchDir());
	for (size_t i = 0; i < entries.size(); i++) {
		if (system::isDirectory(entries[i]))
			continue;
		Aged a;
		a.path = entries[i];
		a.size = 0;
		a.mtime = 0.0;
		try {
			a.size = (uint64_t) system::getFileSize(entries[i]);
			a.mtime = fileMTime(entries[i]);
		}
		catch (Exception& e) {
			continue;
		}
		total += a.size;
		files.push_back(a);
	}
	if (total <= MAX_ARCHIVE_BYTES)
		return;

	std::sort(files.begin(), files.end());
	for (size_t i = 0; i < files.size() && total > MAX_ARCHIVE_BYTES; i++) {
		try {
			system::remove(files[i].path);
			total -= files[i].size;
		}
		catch (Exception& e) {}
	}
	INFO("PatchAudit: pruned patch cache to %llu bytes", (unsigned long long) total);
}

void clearAll() {
	try {
		if (system::isDirectory(root()))
			system::removeRecursively(root());
	}
	catch (Exception& e) {}
}

} // namespace cache
} // namespace ps

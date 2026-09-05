#pragma once
#include <jansson.h>

#include <stdint.h>
#include <string>

namespace ps {
/** On-disk cache. JSON bodies expire by TTL; downloaded patch archives are keyed
    on the patch's `updated_at` so a re-upload simply misses, and are LRU-pruned
    by size. Every function here is safe to call from a worker thread. */
namespace cache {

// TTLs in seconds
static const double TTL_SEARCH    = 10 * 60;
static const double TTL_DETAIL    = 60 * 60;
static const double TTL_MANIFESTS = 24 * 60 * 60;

static const uint64_t MAX_ARCHIVE_BYTES = 200ull * 1024 * 1024;

std::string root();
std::string apiDir();
std::string patchDir();

/** Returns a new reference, or NULL on a miss, a stale entry, or when caching is
    off. Caller decrefs. */
json_t* loadJson(const std::string& url, double maxAgeSec);
void storeJson(const std::string& url, json_t* body);

std::string archivePath(int64_t patchId, int64_t fileId, const std::string& updatedAt,
                        const std::string& filename);
std::string extractDir(int64_t patchId);

/** Deletes stale extraction dirs and trims the archive cache to
    MAX_ARCHIVE_BYTES, oldest first. Run once per session, on a worker. */
void pruneOnStartup();

uint64_t sizeOnDisk();
void clearAll();

/** Global on/off, mirrored from the module's "Use disk cache" menu item. */
void setEnabled(bool enabled);
bool enabled();

} // namespace cache
} // namespace ps

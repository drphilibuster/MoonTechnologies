#include "Audit.hpp"
#include "Api.hpp"
#include "HttpCache.hpp"
#include "HttpStream.hpp"
#include "Installer.hpp"
#include "PatchFile.hpp"
#include "Json.hpp"
#include "JobRunner.hpp"
#include "PatchstorageClient.hpp"
#include "Zip.hpp"

#include <rack.hpp>

using namespace rack;

namespace ps {

/** Minimum gap between audit downloads. The audit is the one thing here that
    issues a burst of requests, so it pauses between them -- but only after a
    patch that actually went to the network. A page whose every result is already
    in the cache used to spend perPage * AUDIT_GAP seconds asleep for no reason;
    re-visiting an audited page is now bounded by disk, not by this. */
static const double AUDIT_GAP = 0.15;
/** Don't pull an attachment bigger than this just to read its module list.
 *
 * The audit fetches every result on a page, so one 30 MB upload is 30 MB spent
 * to badge one row, and a page of them is most of a gigabyte. Import has its own,
 * much higher ceiling (Installer.cpp) because it fetches one file the user asked
 * for -- a patch too big to audit is not too big to import, and the badge must
 * not imply otherwise. Compared against the size the API declares, so an oversize
 * upload is never actually downloaded. */
static const int64_t AUDIT_MAX_FILESIZE = 16ll * 1024 * 1024;

/** Cache key for a patch's extracted slug list. Not a real URL -- HttpCache only
    needs a stable string -- and it carries updated_at so a re-upload misses.
 *
 * The leading version is bumped whenever this file's idea of what it can open
 * changes, because failures are cached too: without it, every patch written off
 * as "no patch file" before zip support existed would stay written off. */
static const int AUDIT_CACHE_VERSION = 3;

static std::string slugCacheKey(int64_t patchId, const std::string& updatedAt) {
	return string::f("audit%d:%lld:%s", AUDIT_CACHE_VERSION,
		(long long) patchId, updatedAt.c_str());
}

static const double SLUG_TTL = 30 * 24 * 60 * 60;

/** A short badge word for each way opening a patch can fail. */
static const char* badgeNote(FailureKind kind) {
	switch (kind) {
		case FailureKind::NoFile: return "no patch file";
		case FailureKind::UnsupportedFile: return "not a patch";
		case FailureKind::CorruptArchive: return "unreadable";
		case FailureKind::NoPatchJson: return "unreadable";
		default: return "?";
	}
}

/** Reads the plugin/model pairs a patch uses.
 *
 * Deliberately makes no judgement about the patch's `"version"`. Rack 2 opens
 * pre-2.0 patches (see the note in Installer.cpp), so a v1 patch's module list
 * is exactly as meaningful as a v2 one's -- and the reason an old patch usually
 * won't run, its plugins never having been ported, is a missing-module count
 * like any other. */
static bool collectSlugs(const std::string& patchJsonPath, AuditRaw& out) {
	json_error_t err;
	JsonRef rootJ(json_load_file(patchJsonPath.c_str(), 0, &err));
	if (!rootJ)
		return false;
	json_t* modulesJ = json_object_get(rootJ.get(), "modules");
	if (!modulesJ || !json_is_array(modulesJ))
		return false;
	size_t i;
	json_t* moduleJ;
	json_array_foreach(modulesJ, i, moduleJ) {
		std::string p = jstr(moduleJ, "plugin");
		std::string m = jstr(moduleJ, "model");
		if (!p.empty() && !m.empty())
			out.slugs.push_back(std::make_pair(p, m));
	}
	return true;
}

/** How much of an oversize upload to read before giving up on finding the patch.
 *
 * The whole trick only pays when the .vcv sits near the front, which is the
 * usual layout -- a zip tool writes the small file first. If the patch is behind
 * a 40 MB sample we would have to stream the sample to reach it, which is the
 * cost this exists to avoid, so stop and let the row show its size instead. */
static const size_t STREAM_BUDGET = 2 * 1024 * 1024;

/** Pulls the patch out of a large zipped upload without downloading the upload.
 *
 * Reads the response from the front, walking zip local headers as they arrive,
 * and hangs up the moment the .vcv has gone past. Returns false if the upload
 * isn't a front-loaded zip, in which case the caller falls back to the size
 * badge. See ps/HttpStream.hpp for why this is a stream rather than a range. */
static bool auditByStreaming(std::shared_ptr<PatchstorageClient>& self, uint64_t gen,
                             int64_t patchId, const std::string& url, AuditRaw& out) {
	if (!http::available())
		return false;

	std::vector<uint8_t> buf;
	std::vector<uint8_t> patch;
	std::string entryName;
	bool found = false;

	http::get(url, [&](const uint8_t* data, size_t len) -> bool {
		buf.insert(buf.end(), data, data + len);
		PrefixScan r = zipFindPatchInPrefix(buf.empty() ? NULL : &buf[0], buf.size(),
		                                    patch, &entryName, NULL);
		if (r == PrefixScan::Found) {
			found = true;
			return false;                  // got it -- hang up on the rest
		}
		if (r != PrefixScan::NeedMore)
			return false;                  // not a zip this reader can walk
		// Nothing else in this plugin blocks past a cancellation, and this is the
		// one place that could sit on a socket for megabytes after the page it was
		// auditing has gone. Checked per chunk, so hanging up costs one chunk.
		if (self->abandoned || JobRunner::global().stopping())
			return false;
		{
			std::lock_guard<std::mutex> lock(self->mutex);
			if (gen != self->auditGen)
				return false;
		}
		return buf.size() < STREAM_BUDGET;
	}, NULL);

	if (!found || patch.empty())
		return false;

	// The entry we pulled out is a .vcv, which is itself either a Rack 2 zstd
	// archive or a bare pre-2.0 JSON document. Rather than teach this path to tell
	// those apart, write it out and hand it to openPatchFile -- the same unwrapper
	// the normal route uses, so both formats work and there is one place that
	// knows what a .vcv can be.
	std::string dir = system::join(cache::extractDir(patchId), "streamed");
	bool ok = false;
	try {
		system::createDirectories(dir);
		std::string inner = system::join(dir, "patch.vcv");
		FILE* f = fopen(inner.c_str(), "wb");
		if (f) {
			size_t wrote = fwrite(&patch[0], 1, patch.size(), f);
			fclose(f);
			if (wrote == patch.size()) {
				OpenedPatch opened = openPatchFile(inner, system::join(dir, "work"), entryName);
				if (opened.ok())
					ok = collectSlugs(opened.patchJsonPath, out);
			}
		}
	}
	catch (Exception& e) {
		WARN("PatchAudit: streamed audit failed for %lld: %s", (long long) patchId, e.what());
	}
	try {
		system::removeRecursively(dir);
	}
	catch (Exception& e) {}
	return ok;
}

static void storeSlugs(const std::string& key, const AuditRaw& a) {
	JsonRef bodyJ(json_object());
	json_t* arr = json_array();
	for (size_t i = 0; i < a.slugs.size(); i++) {
		json_t* pair = json_array();
		json_array_append_new(pair, json_string(a.slugs[i].first.c_str()));
		json_array_append_new(pair, json_string(a.slugs[i].second.c_str()));
		json_array_append_new(arr, pair);
	}
	json_object_set_new(bodyJ.get(), "slugs", arr);
	if (a.failed) {
		json_object_set_new(bodyJ.get(), "failed", json_true());
		json_object_set_new(bodyJ.get(), "note", json_string(a.note.c_str()));
	}
	cache::storeJson(key, bodyJ.get());
}

static bool loadSlugs(const std::string& key, AuditRaw& out) {
	JsonRef bodyJ(cache::loadJson(key, SLUG_TTL));
	if (!bodyJ)
		return false;
	json_t* failedJ = json_object_get(bodyJ.get(), "failed");
	if (failedJ && json_is_true(failedJ)) {
		out.failed = true;
		out.note = jstr(bodyJ.get(), "note");
		return true;
	}
	json_t* arr = json_object_get(bodyJ.get(), "slugs");
	if (!arr || !json_is_array(arr))
		return false;
	size_t i;
	json_t* pair;
	json_array_foreach(arr, i, pair) {
		if (json_array_size(pair) != 2)
			continue;
		const char* p = json_string_value(json_array_get(pair, 0));
		const char* m = json_string_value(json_array_get(pair, 1));
		if (p && m)
			out.slugs.push_back(std::make_pair(std::string(p), std::string(m)));
	}
	return true;
}

/** Fetches and inspects one patch. Returns false only if we were superseded.
 *
 * `touchedNetwork` says whether this call actually went out to the network, so
 * the caller knows whether it owes Patchstorage a pause. A page served entirely
 * from cache leaves it false throughout. */
static bool auditOne(std::shared_ptr<PatchstorageClient>& self, const PatchSummary& patch,
                     uint64_t gen, AuditRaw& out, bool& touchedNetwork) {
	out.patchId = patch.id;
	touchedNetwork = false;

	// The summary's updated_at is enough for the first cache probe; if it's
	// blank (favourites rows) we fall through to the detail fetch below.
	if (!patch.updatedAt.empty() && loadSlugs(slugCacheKey(patch.id, patch.updatedAt), out))
		return true;

	ApiResponse resp;
	bool detailFromCache = false;
	JsonRef detailJ(getJson(string::f("%s/patches/%lld", API_BASE, (long long) patch.id),
		cache::TTL_DETAIL, &resp, &detailFromCache));
	if (!detailFromCache)
		touchedNetwork = true;
	if (self->abandoned)
		return false;
	if (resp.status != ApiStatus::Ok) {
		out.failed = true;
		out.note = "unreachable";
		return true;
	}

	std::string updatedAt = jstr(detailJ.get(), "updated_at", patch.updatedAt);
	if (loadSlugs(slugCacheKey(patch.id, updatedAt), out))
		return true;

	PatchFile chosen;
	bool have = false;
	json_t* filesJ = json_object_get(detailJ.get(), "files");
	if (filesJ && json_is_array(filesJ)) {
		size_t i;
		json_t* f;
		// Prefer a .vcv, but fall back to the sole attachment -- a zipped
		// upload is often named .zip, and openPatchFile can unwrap it.
		PatchFile only;
		int count = 0;
		json_array_foreach(filesJ, i, f) {
			PatchFile pf;
			pf.id = jint(f, "id");
			pf.url = jstr(f, "url");
			pf.filename = jstr(f, "filename");
			pf.filesize = jint(f, "filesize");
			if (pf.url.empty())
				continue;
			count++;
			only = pf;
			if (isPatchFilename(pf.filename)) {
				chosen = pf;
				have = true;
				break;
			}
		}
		if (!have && count == 1) {
			chosen = only;
			have = true;
		}
	}
	if (!have || chosen.url.empty()) {
		out.failed = true;
		out.note = "no patch file";
		storeSlugs(slugCacheKey(patch.id, updatedAt), out);
		return true;
	}
	std::string archivePath = cache::archivePath(patch.id, chosen.id, updatedAt, chosen.filename);
	bool haveArchive = system::isFile(archivePath) && system::getFileSize(archivePath) > 0;

	// The size cap is about what we would have to *fetch*. An archive already in
	// the patch cache -- because it was imported, saved, or audited before it grew
	// -- costs nothing to read, so its size stops being anyone's business.
	if (!haveArchive && chosen.filesize > AUDIT_MAX_FILESIZE) {
		// An upload is usually only this big because it ships the samples the patch
		// loads. The patch itself is still a few tens of KB, and in a zip it comes
		// before them, so try to read just that much and hang up.
		touchedNetwork = true;
		if (auditByStreaming(self, gen, patch.id, chosen.url, out)) {
			storeSlugs(slugCacheKey(patch.id, updatedAt), out);
			return true;
		}
		out.slugs.clear();
		out.failed = true;
		// Say the size, not "too large": this badge shares a column with a module
		// count, where "too large" reads as "too many modules". It is the download
		// that is big -- Silk Nebulae is 60 modules in 0.4 MB, and the uploads that
		// trip this are zips carrying samples.
		out.note = string::f("%lld MB", (long long) (chosen.filesize / (1024 * 1024)));
		// Deliberately not cached. Unlike every other failure here this one can stop
		// being true without the patch changing: import it once and the archive is
		// local, so the next audit reads it. Caching would pin the row at "29 MB"
		// for the TTL even though the file is now sitting on disk.
		return true;
	}

	if (!haveArchive) {
		system::createDirectories(cache::patchDir());
		std::string partPath = archivePath + ".part";
		touchedNetwork = true;
		bool ok = network::requestDownload(chosen.url, partPath, NULL);
		if (self->abandoned) {
			system::remove(partPath);
			return false;
		}
		if (!ok || !system::isFile(partPath) || system::getFileSize(partPath) == 0) {
			system::remove(partPath);
			out.failed = true;
			out.note = "download failed";
			return true;                    // not cached: worth retrying later
		}
		system::rename(partPath, archivePath);
	}

	std::string dir = cache::extractDir(patch.id);
	OpenedPatch opened = openPatchFile(archivePath, dir, chosen.filename);
	if (!opened.ok()) {
		out.failed = true;
		out.note = badgeNote(opened.failure);
		storeSlugs(slugCacheKey(patch.id, updatedAt), out);
		return true;
	}

	if (!collectSlugs(opened.patchJsonPath, out)) {
		out.failed = true;
		out.note = "unreadable";
	}
	// The archive stays in the cache; the extraction does not.
	try {
		system::removeRecursively(dir);
	}
	catch (Exception& e) {}

	storeSlugs(slugCacheKey(patch.id, updatedAt), out);
	return true;
}

void auditJob(std::shared_ptr<PatchstorageClient> self, std::vector<PatchSummary> page,
              uint64_t gen) {
	{
		std::lock_guard<std::mutex> lock(self->mutex);
		if (gen != self->auditGen)
			return;
		self->auditTotal = (int) page.size();
		self->auditDone = 0;
	}
	self->auditDirty = true;

	for (size_t i = 0; i < page.size(); i++) {
		if (self->abandoned || JobRunner::global().stopping())
			return;
		{
			std::lock_guard<std::mutex> lock(self->mutex);
			if (gen != self->auditGen)
				return;              // the page changed under us
		}

		AuditRaw raw;
		bool touchedNetwork = false;
		if (!auditOne(self, page[i], gen, raw, touchedNetwork))
			return;

		{
			std::lock_guard<std::mutex> lock(self->mutex);
			if (gen != self->auditGen)
				return;
			self->auditInbox.push_back(raw);
			self->auditDone = (int) i + 1;
		}
		self->auditDirty = true;

		if (touchedNetwork && i + 1 < page.size()
		    && !JobRunner::global().interruptibleSleep(AUDIT_GAP))
			return;
	}
}

} // namespace ps

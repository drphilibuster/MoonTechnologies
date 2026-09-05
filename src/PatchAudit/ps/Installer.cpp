#include "Installer.hpp"
#include "Api.hpp"
#include "PatchFile.hpp"
#include "HttpCache.hpp"
#include "Json.hpp"
#include "PatchstorageClient.hpp"

#include <rack.hpp>

#include <stdio.h>

#include <algorithm>

using namespace rack;

namespace ps {

/** Refuse anything larger than this outright. The largest VCV patches on
    Patchstorage are a few megabytes; a 64 MB attachment is a sample pack. */
static const int64_t MAX_FILESIZE = 64ll * 1024 * 1024;

/** True if the file starts with the Zstandard magic 28 B5 2F FD, i.e. it is a
    Rack 2 patch archive rather than a raw-JSON Rack v0/v1 patch. Same test as
    Rack's own patch::Manager::isPatchLegacyV1 (Rack/src/patch.cpp:283-296).
    Worth doing up front: the most-liked patches on Patchstorage are v0.5/v1
    files, and "isn't a valid archive" is a much worse message than naming the
    version. */
bool isZstdArchive(const std::string& path) {
	FILE* f = std::fopen(path.c_str(), "rb");
	if (!f)
		return false;
	unsigned char magic[4] = {0, 0, 0, 0};
	size_t n = std::fread(magic, 1, 4, f);
	std::fclose(f);
	return n == 4 && magic[0] == 0x28 && magic[1] == 0xB5 && magic[2] == 0x2F && magic[3] == 0xFD;
}

static std::string lower(const std::string& s) {
	std::string t = s;
	std::transform(t.begin(), t.end(), t.begin(), ::tolower);
	return t;
}

static bool endsWith(const std::string& s, const std::string& suffix) {
	return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool isPatchFilename(const std::string& filename) {
	return endsWith(lower(filename), ".vcv");
}

bool isSelectionFilename(const std::string& filename) {
	return endsWith(lower(filename), ".vcvs");
}

/** Publishes a phase update. Returns false if this job has been superseded, in
    which case the caller must return immediately without publishing anything. */
static bool publish(std::shared_ptr<PatchstorageClient>& self, uint64_t gen, const InstallResult& r) {
	{
		std::lock_guard<std::mutex> lock(self->mutex);
		if (gen != self->installGen)
			return false;
		self->install = r;
	}
	self->installDirty = true;
	return true;
}

static bool alive(std::shared_ptr<PatchstorageClient>& self, uint64_t gen) {
	if (self->abandoned)
		return false;
	std::lock_guard<std::mutex> lock(self->mutex);
	return gen == self->installGen;
}

static void fail(std::shared_ptr<PatchstorageClient>& self, uint64_t gen, InstallResult r,
                 FailureKind kind, const std::string& message, bool retryable = false) {
	r.phase = InstallPhase::Failed;
	r.failure = kind;
	r.message = message;
	r.retryable = retryable;
	publish(self, gen, r);
}

void installJob(std::shared_ptr<PatchstorageClient> self, PatchSummary patch,
                Intent intent, int64_t explicitFileId, uint64_t gen) {
	if (!alive(self, gen))
		return;

	InstallResult r;
	r.intent = intent;
	r.patchId = patch.id;
	r.title = patch.title;
	r.patchstorageUrl = patch.url;

	// --- 1. detail ----------------------------------------------------------
	r.phase = InstallPhase::FetchingDetail;
	if (!publish(self, gen, r))
		return;

	std::string detailUrl = string::f("%s/patches/%lld", API_BASE, (long long) patch.id);
	ApiResponse resp;
	JsonRef detailJ(getJson(detailUrl, cache::TTL_DETAIL, &resp));
	if (!alive(self, gen))
		return;

	if (resp.status == ApiStatus::Network)
		return fail(self, gen, r, FailureKind::Network,
			"Could not reach patchstorage.com. Check your connection.", true);
	if (resp.status != ApiStatus::Ok) {
		if (resp.httpStatus == 404)
			return fail(self, gen, r, FailureKind::NotFound, "This patch is no longer on Patchstorage.");
		return fail(self, gen, r, FailureKind::ApiError,
			resp.message.empty() ? "Patchstorage returned an error." : resp.message, true);
	}

	// The detail response is authoritative for updated_at, which keys the archive
	// cache. It matters for rows that came from the favourites list, which carry
	// only id/title/author.
	std::string updatedAt = jstr(detailJ.get(), "updated_at", patch.updatedAt);

	// --- 2. pick a file -----------------------------------------------------
	json_t* filesJ = json_object_get(detailJ.get(), "files");
	std::vector<PatchFile> all;
	if (filesJ && json_is_array(filesJ)) {
		size_t i;
		json_t* f;
		json_array_foreach(filesJ, i, f) {
			PatchFile pf;
			pf.id = jint(f, "id");
			pf.url = jstr(f, "url");
			pf.filename = jstr(f, "filename");
			pf.filesize = jint(f, "filesize");
			if (!pf.url.empty())
				all.push_back(pf);
		}
	}
	if (all.empty())
		return fail(self, gen, r, FailureKind::NoFile, "This patch has no file attached.");

	PatchFile chosen;
	bool haveChoice = false;
	if (explicitFileId >= 0) {
		for (size_t i = 0; i < all.size(); i++)
			if (all[i].id == explicitFileId) { chosen = all[i]; haveChoice = true; }
	}
	if (!haveChoice) {
		std::vector<PatchFile> vcvs, sels;
		for (size_t i = 0; i < all.size(); i++) {
			if (isPatchFilename(all[i].filename)) vcvs.push_back(all[i]);
			else if (isSelectionFilename(all[i].filename)) sels.push_back(all[i]);
		}
		if (vcvs.size() == 1) {
			chosen = vcvs[0];
			haveChoice = true;
		}
		else if (vcvs.size() > 1) {
			// Let the user say which one; the UI re-issues with an explicit id.
			r.phase = InstallPhase::NeedsFileChoice;
			r.alternateFiles = vcvs;
			publish(self, gen, r);
			return;
		}
		else if (sels.size() >= 1) {
			chosen = sels[0];
			haveChoice = true;
			r.isSelectionFile = true;
		}
		else if (all.size() == 1) {
			// One unlabelled attachment: let the unarchiver be the judge.
			chosen = all[0];
			haveChoice = true;
		}
	}
	if (!haveChoice)
		return fail(self, gen, r, FailureKind::NoFile,
			"This patch has no .vcv file attached. Open it on Patchstorage to see what it ships.");

	r.isSelectionFile = isSelectionFilename(chosen.filename);
	r.suggestedFilename = chosen.filename;

	if (chosen.filesize > MAX_FILESIZE)
		return fail(self, gen, r, FailureKind::TooLarge,
			string::f("'%s' is %lld MB -- too large to be a patch.",
				chosen.filename.c_str(), (long long) (chosen.filesize / (1024 * 1024))));

	// --- 3. download --------------------------------------------------------
	std::string archivePath = cache::archivePath(patch.id, chosen.id, updatedAt, chosen.filename);
	r.archivePath = archivePath;
	r.phase = InstallPhase::Downloading;
	if (!publish(self, gen, r))
		return;

	bool cached = system::isFile(archivePath) && system::getFileSize(archivePath) > 0;
	if (!cached) {
		// createDirectories returns false when the directory already exists.
		system::createDirectories(cache::patchDir());
		if (!system::isDirectory(cache::patchDir()))
			return fail(self, gen, r, FailureKind::DownloadFailed, "Could not create the cache directory.");
		std::string partPath = archivePath + ".part";
		self->downloadProgress = 0.f;
		bool ok = network::requestDownload(chosen.url, partPath, &self->downloadProgress);
		self->downloadProgress = 0.f;
		if (!alive(self, gen)) {
			system::remove(partPath);
			return;
		}
		if (!ok || !system::isFile(partPath) || system::getFileSize(partPath) == 0) {
			system::remove(partPath);
			return fail(self, gen, r, FailureKind::DownloadFailed, "Download failed.", true);
		}
		if (!system::rename(partPath, archivePath)) {
			system::remove(partPath);
			return fail(self, gen, r, FailureKind::DownloadFailed, "Could not save the download.", true);
		}
	}

	if (intent == Intent::SaveToDisk) {
		r.phase = InstallPhase::ReadyToApply;
		publish(self, gen, r);
		return;
	}

	// --- 4. unwrap ----------------------------------------------------------
	r.phase = InstallPhase::Unarchiving;
	if (!publish(self, gen, r))
		return;

	OpenedPatch opened = openPatchFile(archivePath, cache::extractDir(patch.id), chosen.filename);
	if (!opened.ok())
		return fail(self, gen, r, opened.failure, opened.message);
	std::string patchJsonPath = opened.patchJsonPath;
	r.isSelectionFile = opened.isSelection;
	// For a zipped upload this is the .vcv from inside the zip, so "Save to
	// disk" writes something Rack can actually open.
	r.archivePath = opened.archivePath;

	// --- 5. parse ----------------------------------------------------------
	r.phase = InstallPhase::Parsing;
	if (!publish(self, gen, r))
		return;

	json_error_t err;
	JsonRef patchJ(json_load_file(patchJsonPath.c_str(), 0, &err));
	if (!patchJ)
		return fail(self, gen, r, FailureKind::NoPatchJson,
			string::f("Could not read the patch: %s", err.text));
	if (!json_object_get(patchJ.get(), "modules"))
		return fail(self, gen, r, FailureKind::NoPatchJson, "That file isn't a Rack patch.");

	// There is deliberately no version gate here. Rack 2 opens pre-2.0 patches --
	// patch::Manager::load copies a non-zstd .vcv straight to patch.json
	// (Rack/src/patch.cpp:306-310) and every loader below it carries legacy
	// branches back to v0.3 -- and so does ps::applyImport, which goes through the
	// same modelFromJson / Module::fromJson / Cable::fromJson that Rack uses. The
	// only thing that actually stops an old patch running is its plugins never
	// having been ported, which is exactly what the audit already measures.

	// --- 6. hand off paths, not json ----------------------------------------
	r.patchJsonPath = patchJsonPath;
	r.phase = InstallPhase::ReadyToApply;
	publish(self, gen, r);
}

} // namespace ps

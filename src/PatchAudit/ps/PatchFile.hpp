#pragma once
#include "Types.hpp"

#include <string>

namespace ps {

enum class PatchFormat {
	Rack2Archive,   ///< zstd-compressed tar: a Rack 2 .vcv
	RawJson,        ///< a bare JSON document: a .vcvs selection, or a Rack v1 .vcv
	Zip,            ///< a .zip, whatever the file is named
	Empty,
	Unknown,
};

/** Sniffs the file's leading bytes. The extension is not to be trusted: uploads
    named .vcv turn out to be zips, and pre-2.0 .vcv files are plain JSON. */
PatchFormat detectFormat(const std::string& path);

/** Which serialisation era a patch's `"version"` string belongs to.
 *
 * Rack 2 opens all of them -- `patch::Manager::load` copies a non-zstd .vcv
 * straight to patch.json (Rack/src/patch.cpp:306-310), and the loaders carry
 * explicit branches back to v0.3. The eras differ only in how a few fields are
 * spelled, and this plugin has to know because it reimplements paste rather
 * than calling Rack's. */
enum class PatchEra {
	/** 2.x, and anything unversioned. */
	Modern,
	/** 0.6.x-1.x: grid positions, and (in 0.6) cables spelled "wires". */
	Legacy,
	/** <=0.5.x and "dev": module positions are in pixels, not grid units.
	    Same set RackWidget::fromJson calls legacyV05
	    (Rack/src/app/RackWidget.cpp:343-346). */
	LegacyPixelPos,
};

/** Classifies a patch root's `"version"` string. */
PatchEra eraFromVersion(const std::string& version);

struct OpenedPatch {
	/** The JSON document to read: an extracted patch.json, an extracted zip
	    member, or the downloaded file itself. */
	std::string patchJsonPath;
	bool isSelection = false;
	/** The .vcv actually holding the patch -- the zip member for a zipped
	    upload, otherwise the download. What "Save to disk" should write. */
	std::string archivePath;
	FailureKind failure = FailureKind::None;
	std::string message;
	bool ok() const { return failure == FailureKind::None && !patchJsonPath.empty(); }
};

/** Turns whatever was downloaded into a readable patch JSON, unpacking as many
    layers as it takes -- a zip holding a .vcv holding a patch.json is normal.
    `workDir` is emptied and used for scratch. Safe to call from a worker. */
OpenedPatch openPatchFile(const std::string& downloadedPath, const std::string& workDir,
                          const std::string& originalFilename);

} // namespace ps

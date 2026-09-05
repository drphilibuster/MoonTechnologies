#include "PatchFile.hpp"
#include "Zip.hpp"

#include <rack.hpp>

#include <algorithm>
#include <stdio.h>

using namespace rack;

namespace ps {

static std::string lower(const std::string& s) {
	std::string t = s;
	std::transform(t.begin(), t.end(), t.begin(), ::tolower);
	return t;
}

static bool endsWith(const std::string& s, const std::string& suffix) {
	return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

PatchEra eraFromVersion(const std::string& version) {
	if (version.empty())
		return PatchEra::Modern;
	// Rack's own list, verbatim: 0.3, 0.4, 0.5 and the "dev" builds between them
	// wrote module positions in pixels.
	if (version == "dev"
	    || version.compare(0, 4, "0.3.") == 0
	    || version.compare(0, 4, "0.4.") == 0
	    || version.compare(0, 4, "0.5.") == 0)
		return PatchEra::LegacyPixelPos;
	if (version.compare(0, 2, "0.") == 0 || version.compare(0, 2, "1.") == 0)
		return PatchEra::Legacy;
	return PatchEra::Modern;
}

PatchFormat detectFormat(const std::string& path) {
	FILE* f = fopen(path.c_str(), "rb");
	if (!f)
		return PatchFormat::Unknown;
	unsigned char magic[8] = {0};
	size_t n = fread(magic, 1, sizeof(magic), f);
	fclose(f);
	if (n == 0)
		return PatchFormat::Empty;

	if (n >= 4 && magic[0] == 0x28 && magic[1] == 0xB5 && magic[2] == 0x2F && magic[3] == 0xFD)
		return PatchFormat::Rack2Archive;
	// "PK\3\4", and the empty- and spanned-archive variants.
	if (n >= 4 && magic[0] == 'P' && magic[1] == 'K'
	    && (magic[2] == 3 || magic[2] == 5 || magic[2] == 7))
		return PatchFormat::Zip;
	for (size_t i = 0; i < n; i++) {
		unsigned char c = magic[i];
		if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == 0xEF || c == 0xBB || c == 0xBF)
			continue;   // leading whitespace or a UTF-8 BOM
		return (c == '{' || c == '[') ? PatchFormat::RawJson : PatchFormat::Unknown;
	}
	return PatchFormat::Unknown;
}

/** Picks the entry most likely to be the patch: a .vcv first, then a .vcvs,
    preferring the largest of each -- zipped uploads often carry a small
    "read me" patch beside the real one. */
static bool pickZipEntry(const std::vector<ZipEntry>& entries, ZipEntry& out) {
	const ZipEntry* bestVcv = NULL;
	const ZipEntry* bestSel = NULL;
	for (size_t i = 0; i < entries.size(); i++) {
		std::string n = lower(entries[i].name);
		if (endsWith(n, ".vcv")) {
			if (!bestVcv || entries[i].uncompSize > bestVcv->uncompSize)
				bestVcv = &entries[i];
		}
		else if (endsWith(n, ".vcvs")) {
			if (!bestSel || entries[i].uncompSize > bestSel->uncompSize)
				bestSel = &entries[i];
		}
	}
	const ZipEntry* pick = bestVcv ? bestVcv : bestSel;
	if (!pick)
		return false;
	out = *pick;
	return true;
}

static bool writeFile(const std::string& path, const std::vector<uint8_t>& data) {
	FILE* f = fopen(path.c_str(), "wb");
	if (!f)
		return false;
	size_t wrote = data.empty() ? 0 : fwrite(&data[0], 1, data.size(), f);
	fclose(f);
	return wrote == data.size();
}

static OpenedPatch fail(FailureKind kind, const std::string& message) {
	OpenedPatch r;
	r.failure = kind;
	r.message = message;
	return r;
}

OpenedPatch openPatchFile(const std::string& downloadedPath, const std::string& workDir,
                          const std::string& originalFilename) {
	OpenedPatch r;
	r.archivePath = downloadedPath;
	r.isSelection = endsWith(lower(originalFilename), ".vcvs");

	try {
		if (system::isDirectory(workDir))
			system::removeRecursively(workDir);
		system::createDirectories(workDir);
	}
	catch (Exception& e) {
		return fail(FailureKind::CorruptArchive, "Could not create a working directory.");
	}
	if (!system::isDirectory(workDir))
		return fail(FailureKind::CorruptArchive, "Could not create a working directory.");

	std::string path = downloadedPath;
	PatchFormat format = detectFormat(path);

	// A zip may hold the .vcv, which is itself an archive: unwrap once, then
	// carry on with whatever came out.
	if (format == PatchFormat::Zip) {
		std::vector<ZipEntry> entries;
		std::string err;
		if (!zipList(path, entries, &err))
			return fail(FailureKind::CorruptArchive,
				string::f("This upload is a zip that couldn't be read (%s).", err.c_str()));

		ZipEntry entry;
		if (!pickZipEntry(entries, entry))
			return fail(FailureKind::NoFile,
				string::f("This upload is a zip with no patch inside (%d file%s).",
					(int) entries.size(), entries.size() == 1 ? "" : "s"));

		std::vector<uint8_t> data;
		if (!zipExtract(path, entry, data, &err))
			return fail(FailureKind::CorruptArchive,
				string::f("Couldn't unpack '%s' from the zip (%s).",
					system::getFilename(entry.name).c_str(), err.c_str()));

		std::string inner = system::join(workDir, system::getFilename(entry.name));
		if (!writeFile(inner, data))
			return fail(FailureKind::CorruptArchive, "Couldn't write the unpacked patch.");

		r.isSelection = endsWith(lower(entry.name), ".vcvs");
		r.archivePath = inner;
		path = inner;
		format = detectFormat(path);
	}

	switch (format) {
		case PatchFormat::Rack2Archive: {
			std::string dir = system::join(workDir, "unpacked");
			try {
				system::createDirectories(dir);
				system::unarchiveToDirectory(path, dir);
			}
			catch (Exception& e) {
				WARN("PatchAudit: unarchive failed for %s: %s", path.c_str(), e.what());
				return fail(FailureKind::CorruptArchive, "That file isn't a readable Rack 2 patch archive.");
			}
			std::string patchJson = system::join(dir, "patch.json");
			if (!system::isFile(patchJson))
				return fail(FailureKind::NoPatchJson, "The archive doesn't contain a patch.json.");
			r.patchJsonPath = patchJson;
			return r;
		}
		case PatchFormat::RawJson:
			// A .vcvs selection, or a pre-2.0 .vcv -- Rack wrote both as bare JSON.
			// Neither needs unpacking, and neither needs converting: eraFromVersion
			// tells the loaders which spelling of the legacy fields to expect.
			r.patchJsonPath = path;
			return r;
		case PatchFormat::Empty:
			return fail(FailureKind::NoFile, "The downloaded file is empty.");
		case PatchFormat::Zip:
			return fail(FailureKind::UnsupportedFile, "This upload is a zip containing another zip.");
		default:
			break;
	}
	return fail(FailureKind::UnsupportedFile,
		string::f("'%s' isn't a VCV patch -- Rack can't open this file.",
			system::getFilename(originalFilename).c_str()));
}

} // namespace ps

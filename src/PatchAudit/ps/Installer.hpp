#pragma once
#include "Types.hpp"

#include <memory>
#include <string>

namespace ps {

struct PatchstorageClient;

/** The whole load pipeline, start to finish, on the Bulk lane:
    detail -> pick file -> download -> unarchive -> parse -> publish.

    Publishes an InstallResult at every phase change and bails at the first check
    where `gen` no longer matches the client's installGen. It hands the UI file
    *paths*, never a json_t*: jansson refcounts aren't atomic, and one cheap
    re-parse on the UI thread removes that whole class of bug. */
void installJob(std::shared_ptr<PatchstorageClient> self, PatchSummary patch,
                Intent intent, int64_t explicitFileId, uint64_t gen);

/** True if `filename` looks like something we can actually open. */
bool isPatchFilename(const std::string& filename);
bool isSelectionFilename(const std::string& filename);
/** True if the file begins with the Zstandard magic, i.e. it is a Rack 2 patch
    archive rather than a raw-JSON Rack v0/v1 patch. */
bool isZstdArchive(const std::string& path);

} // namespace ps

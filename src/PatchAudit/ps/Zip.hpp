#pragma once
#include <stdint.h>

#include <string>
#include <vector>

namespace ps {

struct ZipEntry {
	std::string name;
	uint16_t method = 0;        // 0 = stored, 8 = deflate
	uint64_t compSize = 0;
	uint64_t uncompSize = 0;
	uint64_t localOffset = 0;
};

/** A deliberately small read-only ZIP reader.
 *
 * Rack's system::unarchiveToDirectory only enables libarchive's tar format and
 * zstd filter (Rack/src/system.cpp:537-539), so it cannot open a ZIP -- and a
 * fair number of Patchstorage uploads are a .zip holding the .vcv (sometimes
 * with the extension still saying .vcv). Handling them here is the difference
 * between "this patch can't be opened" and it just working.
 *
 * Supports the two entry types that occur in practice: stored and deflated.
 * Zip64 and encrypted entries are rejected rather than half-read. */
bool zipList(const std::string& path, std::vector<ZipEntry>& out, std::string* error = NULL);
bool zipExtract(const std::string& path, const ZipEntry& entry, std::vector<uint8_t>& out,
                std::string* error = NULL);

/** What walking a zip from its front turned up so far. */
enum class PrefixScan {
	Found,        ///< reached the end of the entries; `out` holds the best patch seen
	NeedMore,     ///< nothing conclusive yet, but the prefix is still a well-formed zip --
	              ///< `out` may already hold the best candidate seen so far
	NotFound,     ///< walked the whole archive; there is no patch entry in it
	Unreadable,   ///< not a zip, or an entry this reader can't follow
};

/** Looks for a patch entry in the FRONT of a zip, without the central directory.
 *
 * A zip's local headers precede their data and carry the name and compressed
 * size, so a reader moving forward through the bytes knows each entry as it
 * arrives. That makes it possible to pull a small .vcv out of a large upload
 * from a server that will not honour a byte range -- read until the entry has
 * gone past, then hang up. See ps/HttpStream.hpp for why that is the only option.
 *
 * Keeps walking past the first .vcv/.vcvs it finds, the same as the normal-path
 * picker in PatchFile.cpp (pickZipEntry): zipped uploads often carry a small
 * "read me" patch beside the real one, and the largest .vcv (falling back to
 * the largest .vcvs) wins. `out`/`name` hold the best candidate found so far on
 * every call, including a NeedMore one, so a caller that gives up on a byte
 * budget before reaching the true end of the archive still has whatever the
 * best candidate was up to that point rather than whichever came first.
 *
 * Returns NeedMore while `buf` is a valid but incomplete prefix, so the caller
 * can keep feeding it. */
PrefixScan zipFindPatchInPrefix(const uint8_t* buf, size_t len,
                                std::vector<uint8_t>& out, std::string* name,
                                std::string* error = NULL);

} // namespace ps

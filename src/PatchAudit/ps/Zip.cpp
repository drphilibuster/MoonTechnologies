#include "Zip.hpp"
#include "Inflate.hpp"

#include <stdio.h>
#include <string.h>

namespace ps {

static const uint32_t SIG_EOCD = 0x06054b50;
static const uint32_t SIG_CDIR = 0x02014b50;
static const uint32_t SIG_LOCAL = 0x04034b50;

static uint16_t rd16(const uint8_t* p) { return (uint16_t) (p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t* p) {
	return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24);
}

static bool readWhole(const std::string& path, std::vector<uint8_t>& out) {
	FILE* f = fopen(path.c_str(), "rb");
	if (!f)
		return false;
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (size <= 0) {
		fclose(f);
		return false;
	}
	out.resize((size_t) size);
	size_t got = fread(&out[0], 1, (size_t) size, f);
	fclose(f);
	out.resize(got);
	return got > 0;
}

static void setError(std::string* error, const char* msg) {
	if (error)
		*error = msg;
}

bool zipList(const std::string& path, std::vector<ZipEntry>& out, std::string* error) {
	out.clear();
	std::vector<uint8_t> buf;
	if (!readWhole(path, buf)) {
		setError(error, "could not read the file");
		return false;
	}
	if (buf.size() < 22) {
		setError(error, "file is too small to be a zip");
		return false;
	}

	// The End Of Central Directory record lives at the end, after a comment of
	// up to 64 KB, so scan backwards for its signature.
	size_t maxBack = buf.size() < (0xFFFF + 22) ? buf.size() : (0xFFFF + 22);
	size_t eocd = 0;
	bool haveEocd = false;
	for (size_t back = 22; back <= maxBack; back++) {
		size_t at = buf.size() - back;
		if (rd32(&buf[at]) == SIG_EOCD) {
			eocd = at;
			haveEocd = true;
			break;
		}
	}
	if (!haveEocd) {
		setError(error, "no zip directory found");
		return false;
	}

	uint16_t count = rd16(&buf[eocd + 10]);
	uint32_t cdOffset = rd32(&buf[eocd + 16]);
	if (cdOffset == 0xFFFFFFFFu) {
		setError(error, "zip64 archives are not supported");
		return false;
	}
	if (cdOffset >= buf.size()) {
		setError(error, "zip directory is out of range");
		return false;
	}

	size_t at = cdOffset;
	for (uint16_t i = 0; i < count; i++) {
		if (at + 46 > buf.size() || rd32(&buf[at]) != SIG_CDIR)
			break;
		ZipEntry e;
		uint16_t flags = rd16(&buf[at + 8]);
		e.method = rd16(&buf[at + 10]);
		e.compSize = rd32(&buf[at + 20]);
		e.uncompSize = rd32(&buf[at + 24]);
		uint16_t nameLen = rd16(&buf[at + 28]);
		uint16_t extraLen = rd16(&buf[at + 30]);
		uint16_t commentLen = rd16(&buf[at + 32]);
		e.localOffset = rd32(&buf[at + 42]);
		if (at + 46 + nameLen <= buf.size())
			e.name.assign((const char*) &buf[at + 46], nameLen);

		bool encrypted = (flags & 0x1) != 0;
		bool zip64 = (e.compSize == 0xFFFFFFFFu || e.uncompSize == 0xFFFFFFFFu
		           || e.localOffset == 0xFFFFFFFFu);
		// Skip rather than fail: a zip may hold one readable patch alongside
		// entries we can't touch.
		if (!encrypted && !zip64 && !e.name.empty() && e.name[e.name.size() - 1] != '/')
			out.push_back(e);

		at += 46 + nameLen + extraLen + commentLen;
	}

	if (out.empty()) {
		setError(error, "zip contains no readable entries");
		return false;
	}
	return true;
}

bool zipExtract(const std::string& path, const ZipEntry& entry, std::vector<uint8_t>& out,
                std::string* error) {
	out.clear();
	std::vector<uint8_t> buf;
	if (!readWhole(path, buf)) {
		setError(error, "could not read the file");
		return false;
	}
	// The central directory records the offset of the local header, whose own
	// name and extra fields may differ in length from the directory's.
	size_t at = (size_t) entry.localOffset;
	if (at + 30 > buf.size() || rd32(&buf[at]) != SIG_LOCAL) {
		setError(error, "zip entry header is missing");
		return false;
	}
	uint16_t nameLen = rd16(&buf[at + 26]);
	uint16_t extraLen = rd16(&buf[at + 28]);
	size_t dataAt = at + 30 + nameLen + extraLen;
	if (dataAt + entry.compSize > buf.size()) {
		setError(error, "zip entry runs past the end of the file");
		return false;
	}

	if (entry.method == 0) {
		out.assign(buf.begin() + dataAt, buf.begin() + dataAt + (size_t) entry.compSize);
		return true;
	}
	if (entry.method == 8) {
		if (!inflateRaw(&buf[dataAt], (size_t) entry.compSize, out, (size_t) entry.uncompSize)) {
			setError(error, "zip entry could not be decompressed");
			return false;
		}
		return true;
	}
	setError(error, "zip entry uses an unsupported compression method");
	return false;
}


/** Case-insensitive suffix test, so ".VCV" counts. */
static bool endsWithNoCase(const std::string& s, const char* suffix) {
	size_t n = strlen(suffix);
	if (s.size() < n)
		return false;
	for (size_t i = 0; i < n; i++) {
		char a = s[s.size() - n + i];
		if (a >= 'A' && a <= 'Z')
			a = (char) (a - 'A' + 'a');
		if (a != suffix[i])
			return false;
	}
	return true;
}

PrefixScan zipFindPatchInPrefix(const uint8_t* buf, size_t len,
                                std::vector<uint8_t>& out, std::string* name,
                                std::string* error) {
	out.clear();
	size_t at = 0;
	while (true) {
		// Not enough bytes yet to even read a local header.
		if (at + 30 > len)
			return PrefixScan::NeedMore;

		uint32_t sig = rd32(&buf[at]);
		if (sig != SIG_LOCAL) {
			// The central directory follows the last entry's data, so reaching it
			// means the whole archive went by without a patch in it.
			if (sig == SIG_CDIR || sig == SIG_EOCD)
				return PrefixScan::NotFound;
			setError(error, at == 0 ? "not a zip" : "unexpected data between zip entries");
			return PrefixScan::Unreadable;
		}

		uint16_t flags = rd16(&buf[at + 6]);
		uint16_t method = rd16(&buf[at + 8]);
		uint32_t compSize = rd32(&buf[at + 18]);
		uint32_t uncompSize = rd32(&buf[at + 22]);
		uint16_t nameLen = rd16(&buf[at + 26]);
		uint16_t extraLen = rd16(&buf[at + 28]);

		if (at + 30 + nameLen > len)
			return PrefixScan::NeedMore;
		std::string entryName((const char*) &buf[at + 30], nameLen);

		// Bit 3 says the sizes are not here but in a descriptor AFTER the data,
		// which is precisely the one thing a forward-only reader cannot skip: we
		// would have to scan for a signature that may also occur inside the
		// compressed bytes. Only streamed zips do this, and they are rare on
		// Patchstorage; the caller falls back to the whole download.
		if (flags & 0x8) {
			setError(error, "zip was written as a stream, so entry sizes are not "
			                "known until after the data");
			return PrefixScan::Unreadable;
		}
		if (flags & 0x1) {
			setError(error, "zip entry is encrypted");
			return PrefixScan::Unreadable;
		}
		if (compSize == 0xFFFFFFFFu || uncompSize == 0xFFFFFFFFu) {
			setError(error, "zip64 archives are not supported");
			return PrefixScan::Unreadable;
		}

		size_t dataAt = at + 30 + (size_t) nameLen + (size_t) extraLen;
		bool wanted = endsWithNoCase(entryName, ".vcv") || endsWithNoCase(entryName, ".vcvs");

		if (wanted) {
			if (dataAt + compSize > len)
				return PrefixScan::NeedMore;      // keep reading; it is close
			if (method == 0) {
				out.assign(buf + dataAt, buf + dataAt + compSize);
			}
			else if (method == 8) {
				if (!inflateRaw(&buf[dataAt], compSize, out, uncompSize)) {
					setError(error, "zip entry could not be decompressed");
					return PrefixScan::Unreadable;
				}
			}
			else {
				setError(error, "zip entry uses an unsupported compression method");
				return PrefixScan::Unreadable;
			}
			if (name)
				*name = entryName;
			return PrefixScan::Found;
		}

		// Step over this entry's data. Overflow-safe: compSize is a uint32 and
		// `at` is a size_t, so on any platform this plugin builds for the sum
		// cannot wrap.
		at = dataAt + compSize;
	}
}

} // namespace ps

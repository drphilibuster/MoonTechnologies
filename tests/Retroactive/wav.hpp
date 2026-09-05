#pragma once
// Minimal canonical 44-byte-header 16-bit PCM WAV writer. Test-only.
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace wav {

inline void put32(unsigned char* p, uint32_t v) {
	p[0] = (unsigned char)(v & 0xff);
	p[1] = (unsigned char)((v >> 8) & 0xff);
	p[2] = (unsigned char)((v >> 16) & 0xff);
	p[3] = (unsigned char)((v >> 24) & 0xff);
}
inline void put16(unsigned char* p, uint16_t v) {
	p[0] = (unsigned char)(v & 0xff);
	p[1] = (unsigned char)((v >> 8) & 0xff);
}

/** Interleaved stereo float in nominal [-1,1] -> 16-bit PCM. Returns false on I/O error. */
inline bool writeStereo16(const std::string& path, const std::vector<float>& l,
                          const std::vector<float>& r, int sampleRate) {
	size_t n = l.size() < r.size() ? l.size() : r.size();
	uint32_t dataBytes = (uint32_t)(n * 2 * 2);

	unsigned char h[44];
	std::memcpy(h + 0, "RIFF", 4);
	put32(h + 4, 36 + dataBytes);
	std::memcpy(h + 8, "WAVEfmt ", 8);
	put32(h + 16, 16);            // fmt chunk size
	put16(h + 20, 1);             // PCM
	put16(h + 22, 2);             // channels
	put32(h + 24, (uint32_t)sampleRate);
	put32(h + 28, (uint32_t)sampleRate * 2 * 2);   // byte rate
	put16(h + 32, 4);             // block align
	put16(h + 34, 16);            // bits
	std::memcpy(h + 36, "data", 4);
	put32(h + 40, dataBytes);

	FILE* f = std::fopen(path.c_str(), "wb");
	if (!f) return false;
	std::fwrite(h, 1, 44, f);

	std::vector<unsigned char> buf(n * 4);
	for (size_t i = 0; i < n; i++) {
		float a = l[i], b = r[i];
		if (a > 1.f) a = 1.f; if (a < -1.f) a = -1.f;
		if (b > 1.f) b = 1.f; if (b < -1.f) b = -1.f;
		put16(&buf[i * 4 + 0], (uint16_t)(int16_t)(a * 32767.f));
		put16(&buf[i * 4 + 2], (uint16_t)(int16_t)(b * 32767.f));
	}
	std::fwrite(&buf[0], 1, buf.size(), f);
	std::fclose(f);
	return true;
}

} // namespace wav

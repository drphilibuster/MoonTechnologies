// Depreciation -- identifying the PCM 70 firmware images the user supplies.
//
// The ROMs are Lexicon's and are never in this repository. What IS here is their
// SHA-256 hashes, so the module can tell a V2.0 set from a V3.01 set (and a
// good dump from a bad one) without the bytes ever leaving the user's disk.
//
// No plugin.hpp dependency: tests/Depreciation compiles this with the host
// compiler and, when $PCM70_ROMS points at a folder of dumps, runs it on them.
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace pcm70 {

// --- SHA-256 (FIPS 180-4) ------------------------------------------------------
inline std::string sha256hex(const uint8_t* data, size_t len) {
	static const uint32_t K[64] = {
		0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
		0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
		0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
		0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
		0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
		0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
		0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
		0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2 };
	uint32_t h[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
	auto rotr = [](uint32_t x, int n) { return (x >> n) | (x << (32 - n)); };
	std::vector<uint8_t> msg(data, data + len);
	msg.push_back(0x80);
	while (msg.size() % 64 != 56) msg.push_back(0);
	const uint64_t bits = uint64_t(len) * 8;
	for (int i = 7; i >= 0; i--) msg.push_back(uint8_t(bits >> (8 * i)));
	for (size_t off = 0; off < msg.size(); off += 64) {
		uint32_t w[64];
		for (int i = 0; i < 16; i++)
			w[i] = uint32_t(msg[off + 4 * i]) << 24 | uint32_t(msg[off + 4 * i + 1]) << 16 | uint32_t(msg[off + 4 * i + 2]) << 8 | msg[off + 4 * i + 3];
		for (int i = 16; i < 64; i++) {
			const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
			const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
			w[i] = w[i - 16] + s0 + w[i - 7] + s1;
		}
		uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
		for (int i = 0; i < 64; i++) {
			const uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
			const uint32_t ch = (e & f) ^ (~e & g);
			const uint32_t t1 = hh + S1 + ch + K[i] + w[i];
			const uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
			const uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
			const uint32_t t2 = S0 + mj;
			hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
		}
		h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
	}
	char out[65];
	for (int i = 0; i < 8; i++) std::snprintf(out + 8 * i, 9, "%08x", h[i]);
	return std::string(out, 64);
}

// --- the five images -------------------------------------------------------------
enum Role { U62, U95, U67, U48, U49, ROLES };

inline const char* roleName(int r) {
	static const char* n[ROLES] = { "U62", "U95", "U67", "U48", "U49" };
	return n[r];
}
inline const char* roleWhat(int r) {
	static const char* n[ROLES] = { "master firmware (32 KB)", "slave firmware (16 KB)", "opcode ROM (8 KB)", "control PROM (512 B)", "sequencer PROM (32 B)" };
	return n[r];
}
inline size_t roleSize(int r) {
	static const size_t n[ROLES] = { 0x8000, 0x4000, 0x2000, 0x200, 0x20 };
	return n[r];
}

/** The dumps known to be good. The firmware set is one family: U62, U95 and U67 must come
    from the same software version (the V2 and V3 opcode ROMs differ in two pages). The two
    PROMs are the same hardware in every version. */
struct Known { int role; const char* family; const char* sha256; };
static const Known KNOWN[] = {
	{ U62, "2.0",  "6c1eabb7f6994ed788ae32595462e3dc1ad5baf89f231ebcd35a8e3fac930497" },
	{ U95, "2.0",  "da8c33220496a4b5173dfd5701e1d2751e1427e5010e439b482c0780f78d3c44" },
	{ U67, "2.0",  "73c90bd5ad13f64277021ee399611887a662e4e88847718be13c4d98788d6ead" },
	{ U62, "3.01", "ff2f907af62201b77bc116da41d02a3ca40cf58ebc687116c640ec5a6e37a016" },
	{ U95, "3.01", "2f86e617c3054992cb3143f84db85e09c606663c48e041e5c8a322b919da0a1d" },
	{ U67, "3.01", "bee743be3df77d3a5033d48c9cd9d6bb6d8734b6de1d54678930ed94bf1d430e" },
	{ U48, "",     "86de92cb1441c7ee5cd6f4ca36da20e84db35b5055b97028b4832abc12bf55f7" },
	{ U49, "",     "80d8e0e1309f7e8568648e787fb4b3404aebb2901e4f89798c9cef199c6832d5" },
};
static const char* const FAMILIES[] = { "3.01", "2.0" };   // preference order when a folder holds both sets

/** One file the user pointed us at. */
struct Candidate { std::string path; std::vector<uint8_t> bytes; };

/** What a file turned out to be. */
struct Part {
	std::string path;
	std::vector<uint8_t> bytes;
	std::string sha;
	int role = -1;            // from the hash if known, otherwise from the size (-1: not a PCM 70 image size)
	std::string family;       // software version if the hash is known
	bool known = false;
};

inline Part identify(const Candidate& c) {
	Part p;
	p.path = c.path; p.bytes = c.bytes;
	p.sha = sha256hex(c.bytes.data(), c.bytes.size());
	for (const Known& k : KNOWN)
		if (p.sha == k.sha256) { p.role = k.role; p.family = k.family; p.known = true; return p; }
	for (int r = 0; r < ROLES; r++)
		if (c.bytes.size() == roleSize(r)) { p.role = r; break; }
	return p;
}

/** The set the module will run: five images, one software version. */
struct RomSet {
	std::string path[ROLES];
	std::vector<uint8_t> data[ROLES];
	std::string family;       // "2.0" / "3.01" when complete
	std::string problem;      // empty when complete
	bool ok() const { return problem.empty() && !family.empty(); }
	bool have(int r) const { return !data[r].empty(); }
};

/** Pick the best set out of whatever the user pointed us at. A folder may hold the V2 and
    the V3 set; the newest complete one wins. Unknown dumps are refused with a message
    that says which image and what its hash is, never accepted silently. */
inline RomSet assembleParts(const std::vector<Part>& parts) {
	RomSet best;
	int bestScore = -1;
	for (const char* fam : FAMILIES) {
		RomSet s;
		s.family = fam;
		int score = 0;
		for (const Part& p : parts) {
			if (!p.known || p.role < 0) continue;
			if (p.role <= U67 && p.family != fam) continue;
			if (!s.have(p.role)) { s.data[p.role] = p.bytes; s.path[p.role] = p.path; score++; }
		}
		if (score > bestScore) { bestScore = score; best = s; }
	}
	// What is missing, and why.
	std::string why;
	for (int r = 0; r < ROLES && why.empty(); r++) {
		if (best.have(r)) continue;
		const std::string need = r <= U67 ? std::string("V") + best.family + " " : std::string();
		for (const Part& p : parts)                           // a good dump of the wrong version?
			if (p.known && p.role == r && r <= U67) { why = std::string(roleName(r)) + " IS V" + p.family + ", NEEDS V" + best.family; break; }
		if (!why.empty()) break;
		for (const Part& p : parts)                           // a file of the right size that is not a known dump?
			if (!p.known && p.role == r) { why = std::string("UNKNOWN ") + roleName(r) + " " + p.sha.substr(0, 8); break; }
		if (why.empty()) why = std::string("MISSING ") + need + roleName(r);
	}
	if (bestScore <= 0 && parts.empty()) why = "LOAD ROMS";
	best.problem = why;
	if (!best.problem.empty()) best.family = best.have(U62) ? best.family : std::string();
	return best;
}

inline RomSet assemble(const std::vector<Candidate>& files) {
	std::vector<Part> parts;
	for (const Candidate& c : files) parts.push_back(identify(c));
	return assembleParts(parts);
}

/** The line the display shows. */
inline std::string statusLine(const RomSet& s) {
	if (s.ok()) return "V" + s.family + " ROMS OK";
	return s.problem.empty() ? "LOAD ROMS" : s.problem;
}

} // namespace pcm70

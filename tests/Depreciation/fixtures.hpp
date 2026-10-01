// Shared by the Depreciation tests: reading the user's own files (ROMs, .syx banks) from outside the repository.
#pragma once
#include "../../src/Depreciation/Roms.hpp"
#include <cctype>
#include <cstdlib>
#include <dirent.h>
#include <fstream>
#include <iterator>
#include <sys/stat.h>

namespace fx {
using namespace pcm70;
typedef std::vector<uint8_t> Bytes;
static Bytes readFile(const std::string& path) { std::ifstream f(path, std::ios::binary); return Bytes(std::istreambuf_iterator<char>(f), {}); }
static void walk(const std::string& dir, std::vector<std::string>& out) {
	DIR* dp = opendir(dir.c_str()); if (!dp) return;
	while (dirent* e = readdir(dp)) {
		const std::string n = e->d_name; if (n == "." || n == "..") continue;
		const std::string p = dir + "/" + n; struct stat st; if (stat(p.c_str(), &st) != 0) continue;
		if (S_ISDIR(st.st_mode)) walk(p, out); else out.push_back(p);
	}
	closedir(dp);
}

struct Fw { std::string family; Bytes u62, u95, u67, u48, u49; };
static std::vector<Fw> loadFirmware() {
	std::vector<Fw> out; const char* dir = std::getenv("PCM70_ROMS"); if (!dir) return out;
	std::vector<std::string> files; walk(dir, files);
	for (const char* fam : FAMILIES) {
		Fw fw; fw.family = fam;
		for (const std::string& f : files) {
			struct stat st; if (stat(f.c_str(), &st) != 0 || st.st_size > 0x8000) continue;
			Candidate c{ f, readFile(f) }; const Part p = identify(c);
			if (p.known && p.role == U48) fw.u48 = p.bytes;
			if (p.known && p.role == U49) fw.u49 = p.bytes;
			if (p.known && p.family == fam) { if (p.role == U62) fw.u62 = p.bytes; if (p.role == U95) fw.u95 = p.bytes; if (p.role == U67) fw.u67 = p.bytes; }
		}
		if (!fw.u62.empty() && !fw.u95.empty()) out.push_back(fw);
	}
	return out;
}

struct Reg { int n; Bytes data; };
static std::vector<Reg> parseSyx(const Bytes& d) {
	std::vector<Reg> out; size_t i = 0;
	while (i < d.size()) {
		if (d[i] == 0) { i++; continue; }
		if (d[i] != 0xF0) break;
		size_t j = i; while (j < d.size() && d[j] != 0xF7) j++;
		const size_t n = ((d[i + 5] & 3) << 7) | (d[i + 6] & 0x7F);
		Reg r; r.n = d[i + 4] & 63;
		for (size_t k = 0; k + 1 < n; k += 2) r.data.push_back((uint8_t)((d[i + 7 + k] << 4) | (d[i + 8 + k] & 15)));
		if (n == 334 && r.n < 50) out.push_back(r);
		i = j + 1;
	}
	return out;
}
static Bytes storedMessage(const Reg& r) {                                 // single-register bulk dump, stored form
	Bytes m = { 0xF0, 0x06, 0x00, 0x10, (uint8_t)r.n, 0x02, 0x4E }; unsigned sum = 0;
	for (uint8_t b : r.data) { m.push_back(b >> 4); m.push_back(b & 15); sum += (b >> 4) + (b & 15); }
	m.push_back(sum & 0x7F); m.push_back(0xF7); return m;
}
static Bytes activeMessage(const Reg& r) {                                 // the same data as the ACTIVE program (target 50): the firmware loads it into the slave and runs it
	Bytes m = { 0xF0, 0x06, 0x00, 0x00, 50, 0x02, 0x4E }; unsigned sum = 0;
	for (uint8_t b : r.data) { m.push_back(b >> 4); m.push_back(b & 15); sum += (b >> 4) + (b & 15); }
	m.push_back(sum & 0x7F); m.push_back(0xF7); return m;
}
}

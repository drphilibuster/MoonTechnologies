// Depreciation: identifying the firmware images. Synthetic parts test the set-assembly
// logic without any ROM; with $PCM70_ROMS set the real dumps must identify as the
// versions their hashes say.
#include "../../src/Depreciation/Roms.hpp"

#include <cctype>
#include <cstdlib>
#include <dirent.h>
#include <fstream>
#include <iterator>
#include <sys/stat.h>

using namespace pcm70;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static std::vector<uint8_t> bytes(const char* s) { return std::vector<uint8_t>(s, s + std::strlen(s)); }

static Part known(int role, const char* family) {
	Part p; p.role = role; p.family = family; p.known = true; p.path = std::string("/x/") + roleName(role) + family;
	p.bytes.assign(roleSize(role), uint8_t(role + 1)); return p;
}
static Part unknownSized(int role) {
	Part p; p.role = role; p.known = false; p.path = "/x/mystery"; p.bytes.assign(roleSize(role), 0x5a);
	p.sha = sha256hex(p.bytes.data(), p.bytes.size()); return p;
}

static void testSha() {
	CHECK(sha256hex(nullptr, 0) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
	const std::vector<uint8_t> abc = bytes("abc");
	CHECK(sha256hex(abc.data(), abc.size()) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
	const std::vector<uint8_t> two = bytes("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq");   // two-block message
	CHECK(sha256hex(two.data(), two.size()) == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
	std::vector<uint8_t> big(32768, 0);                                                                // a ROM-sized input
	CHECK(sha256hex(big.data(), big.size()).size() == 64);
}

static void testTable() {
	CHECK(sizeof(KNOWN) / sizeof(KNOWN[0]) == 8);
	for (const Known& k : KNOWN) CHECK(std::strlen(k.sha256) == 64);
	for (size_t i = 0; i < 8; i++)
		for (size_t j = i + 1; j < 8; j++) CHECK(std::strcmp(KNOWN[i].sha256, KNOWN[j].sha256) != 0);
}

static void testAssembly() {
	// nothing at all
	RomSet s = assembleParts({});
	CHECK(!s.ok() && statusLine(s) == "LOAD ROMS");
	// a complete V2.0 set
	std::vector<Part> v2 = { known(U62, "2.0"), known(U95, "2.0"), known(U67, "2.0"), known(U48, ""), known(U49, "") };
	s = assembleParts(v2);
	CHECK(s.ok() && s.family == "2.0" && statusLine(s) == "V2.0 ROMS OK");
	// a complete V3.01 set
	std::vector<Part> v3 = { known(U62, "3.01"), known(U95, "3.01"), known(U67, "3.01"), known(U48, ""), known(U49, "") };
	s = assembleParts(v3);
	CHECK(s.ok() && s.family == "3.01" && statusLine(s) == "V3.01 ROMS OK");
	// both sets in one folder: the newest wins, and the firmware images all come from it
	std::vector<Part> both = v2; both.insert(both.end(), v3.begin(), v3.end());
	s = assembleParts(both);
	CHECK(s.ok() && s.family == "3.01");
	CHECK(s.path[U62].find("3.01") != std::string::npos && s.path[U67].find("3.01") != std::string::npos);
	// V3 firmware with the V2 opcode ROM: refused, and says why (the two U67s differ in two pages)
	std::vector<Part> mixed = { known(U62, "3.01"), known(U95, "3.01"), known(U67, "2.0"), known(U48, ""), known(U49, "") };
	s = assembleParts(mixed);
	CHECK(!s.ok() && statusLine(s) == "U67 IS V2.0, NEEDS V3.01");
	// a missing PROM
	std::vector<Part> noprom = { known(U62, "2.0"), known(U95, "2.0"), known(U67, "2.0"), known(U48, "") };
	s = assembleParts(noprom);
	CHECK(!s.ok() && statusLine(s) == "MISSING U49");
	// a missing firmware image names its version
	std::vector<Part> nou95 = { known(U62, "3.01"), known(U67, "3.01"), known(U48, ""), known(U49, "") };
	s = assembleParts(nou95);
	CHECK(!s.ok() && statusLine(s) == "MISSING V3.01 U95");
	// an unrecognised dump of the right size is refused with its hash, never accepted silently
	std::vector<Part> bad = { known(U62, "2.0"), unknownSized(U95), known(U67, "2.0"), known(U48, ""), known(U49, "") };
	s = assembleParts(bad);
	CHECK(!s.ok() && statusLine(s).compare(0, 11, "UNKNOWN U95") == 0 && statusLine(s).size() == 20);
	// the identify() path: arbitrary bytes are not "known"
	Candidate c; c.path = "/x/y"; c.bytes.assign(0x8000, 0);
	Part p = identify(c);
	CHECK(!p.known && p.role == U62);
	Candidate junk; junk.path = "/x/z"; junk.bytes.assign(1234, 0);
	CHECK(identify(junk).role == -1);
}

static std::vector<uint8_t> readFile(const std::string& path) {
	std::ifstream f(path, std::ios::binary);
	return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}

static void testRealRoms() {
	const char* dir = std::getenv("PCM70_ROMS");
	if (!dir) { std::printf("SKIP real ROMs (PCM70_ROMS not set)\n"); return; }
	std::vector<Candidate> files;
	std::vector<std::string> stack = { dir };
	while (!stack.empty()) {                                   // the dumps may sit in v2/ v3/ proms/ subfolders
		const std::string d = stack.back(); stack.pop_back();
		DIR* dp = opendir(d.c_str());
		if (!dp) continue;
		while (dirent* e = readdir(dp)) {
			const std::string n = e->d_name;
			if (n == "." || n == "..") continue;
			const std::string path = d + "/" + n;
			struct stat st;
			if (stat(path.c_str(), &st) != 0) continue;
			if (S_ISDIR(st.st_mode)) stack.push_back(path);
			else if (S_ISREG(st.st_mode) && st.st_size <= 0x8000) files.push_back({ path, readFile(path) });
		}
		closedir(dp);
	}
	int known = 0;
	for (const Candidate& c : files) {
		const Part p = identify(c);
		if (p.known) known++;
	}
	std::printf("real ROMs: %zu files, %d known dumps\n", files.size(), known);
	if (known == 0) { std::printf("SKIP real ROMs (no known dump in %s)\n", dir); return; }
	// Files whose path says V2 or V3 must be recognised as that version's firmware image.
	for (const Candidate& c : files) {
		const std::string lower = [&]() { std::string t = c.path; for (char& ch : t) ch = char(std::tolower(ch)); return t; }();
		const bool fw = c.bytes.size() == 0x8000 || c.bytes.size() == 0x4000 || c.bytes.size() == 0x2000;
		const bool v3 = lower.find("v3") != std::string::npos, v2 = lower.find("v2") != std::string::npos;
		if (!fw || v3 == v2) continue;
		const Part p = identify(c);
		if (!(p.known && p.family == (v3 ? "3.01" : "2.0"))) { std::printf("not recognised as V%s: %s (%s)\n", v3 ? "3.01" : "2.0", c.path.c_str(), p.sha.substr(0, 8).c_str()); failures++; }
	}
	const RomSet s = assemble(files);
	std::printf("real ROMs: status \"%s\"\n", statusLine(s).c_str());
	CHECK(s.ok());
}

int main() {
	testSha();
	testTable();
	testAssembly();
	testRealRoms();
	if (failures) { std::printf("%d FAILED\n", failures); return 1; }
	std::printf("ok\n");
	return 0;
}

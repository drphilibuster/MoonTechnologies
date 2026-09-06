// A stand-in for <rack.hpp>, just wide enough to compile src/Repossession/Process.hpp
// off the audio thread and away from Rack.
//
// Process.hpp is the one file in this module that spawns child processes, and
// what it has to get right -- telling "the tool ran and failed" apart from "the
// tool never started" -- is pure POSIX. None of it needs Rack, so none of it is
// tested through Rack. Only the five filesystem calls and the jansson handles
// the header mentions are stubbed here; the launcher itself is the real code.
#pragma once

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>

#define INFO(...)  ((void) 0)
#define WARN(...)  ((void) 0)

// --- jansson, reduced to what compiles ---------------------------------------
// loadGlobalToolDir()/saveGlobalToolDir() are never called by these tests; they
// only have to exist.
typedef struct json_t json_t;
typedef struct { int line; } json_error_t;
inline json_t* json_load_file(const char*, int, json_error_t*) { return nullptr; }
inline json_t* json_object() { return nullptr; }
inline json_t* json_object_get(const json_t*, const char*) { return nullptr; }
inline int json_is_string(const json_t*) { return 0; }
inline const char* json_string_value(const json_t*) { return ""; }
inline json_t* json_string(const char*) { return nullptr; }
inline int json_object_set_new(json_t*, const char*, json_t*) { return 0; }
inline int json_dump_file(const json_t*, const char*, size_t) { return 0; }
inline void json_decref(json_t*) {}
#define JSON_INDENT(n) ((size_t) (n))

namespace rack {
namespace system {

inline bool isFile(const std::string& p) {
	struct stat st;
	return ::stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

inline std::string join(const std::string& a, const std::string& b) {
	if (a.empty())
		return b;
	return (a[a.size() - 1] == '/') ? a + b : a + "/" + b;
}

inline std::string getDirectory(const std::string& p) {
	size_t i = p.find_last_of('/');
	return (i == std::string::npos) ? "" : p.substr(0, i);
}

inline void createDirectories(const std::string&) {}

inline std::string getFilename(const std::string& p) {
	size_t i = p.find_last_of('/');
	return (i == std::string::npos) ? p : p.substr(i + 1);
}

inline std::string getExtension(const std::string& p) {
	std::string f = getFilename(p);
	size_t i = f.find_last_of('.');
	return (i == std::string::npos) ? "" : f.substr(i);
}

inline std::string getStem(const std::string& p) {
	std::string f = getFilename(p);
	size_t i = f.find_last_of('.');
	return (i == std::string::npos) ? f : f.substr(0, i);
}

inline std::string getAbsolute(const std::string& p) { return p; }

inline uint64_t getFileSize(const std::string& p) {
	struct stat st;
	return (::stat(p.c_str(), &st) == 0) ? (uint64_t) st.st_size : 0;
}

inline std::vector<std::string> getEntries(const std::string&, int) {
	return std::vector<std::string>();
}

inline void remove(const std::string& p) { ::unlink(p.c_str()); }
inline void setThreadName(const std::string&) {}

} // namespace system

namespace string {
inline std::string f(const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	char buf[512];
	std::vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	return buf;
}
} // namespace string

namespace asset {
inline std::string user(const std::string& p) { return "/tmp/moon-test/" + p; }
} // namespace asset

} // namespace rack

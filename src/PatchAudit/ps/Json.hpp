#pragma once
#include <jansson.h>

#include <stdint.h>
#include <string>

namespace ps {

/** Owns one json_t reference and decrefs it on scope exit.

jansson refcounts are not atomic, which is why no json_t ever crosses a thread
boundary in this plugin: workers hand the UI file paths, and the UI re-parses.
*/
struct JsonRef {
	json_t* j;
	explicit JsonRef(json_t* j = NULL) : j(j) {}
	~JsonRef() { if (j) json_decref(j); }
	json_t* get() const { return j; }
	json_t* release() { json_t* t = j; j = NULL; return t; }
	void reset(json_t* n = NULL) { if (j) json_decref(j); j = n; }
	explicit operator bool() const { return j != NULL; }
private:
	JsonRef(const JsonRef&);
	JsonRef& operator=(const JsonRef&);
};

inline std::string jstr(json_t* o, const char* key, const std::string& def = "") {
	if (!o) return def;
	json_t* v = json_object_get(o, key);
	return (v && json_is_string(v)) ? std::string(json_string_value(v)) : def;
}

inline int64_t jint(json_t* o, const char* key, int64_t def = 0) {
	if (!o) return def;
	json_t* v = json_object_get(o, key);
	if (!v) return def;
	if (json_is_integer(v)) return json_integer_value(v);
	if (json_is_real(v)) return (int64_t) json_real_value(v);
	return def;
}

inline json_t* jobj(json_t* o, const char* key) {
	return o ? json_object_get(o, key) : NULL;
}

} // namespace ps

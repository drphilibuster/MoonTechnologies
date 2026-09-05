#include "HttpStream.hpp"

#include <rack.hpp>

#include <curl/curl.h>

#if defined ARCH_WIN
	#define WIN32_LEAN_AND_MEAN
	#define NOMINMAX
	#include <windows.h>
#else
	#include <dlfcn.h>
#endif

#include <mutex>

using namespace rack;

namespace ps {
namespace http {

// curl.h is included for its types and CURLOPT_* values -- those are compile-time
// constants and cost nothing. The functions, though, are looked up at runtime.
//
// Rack links libcurl into libRack and re-exports it, so the symbols are there:
// verified against /Applications/VCV Rack 2 Pro.app/Contents/Resources/libRack.dylib.
// Linking them directly would still be a mistake. A plugin with an unresolved
// symbol fails to LOAD -- the module vanishes from the browser with a log line
// most people never see -- so a Rack build that stopped re-exporting curl would
// take the whole plugin down over an optimisation it can perfectly well live
// without. Looked up at runtime the cost is one feature, and available() says so.
//
// That "one feature" is the whole point of the design, and it is what makes this
// portable: every platform where the lookup fails simply loses zip streaming,
// and the audit downloads those patches instead. Nothing else in the module
// depends on curl, so there is no platform on which PatchAudit fails to build or
// fails to load -- see get() and available().

namespace {

typedef CURL* (*InitFn)(void);
typedef CURLcode (*SetoptFn)(CURL*, CURLoption, ...);
typedef CURLcode (*PerformFn)(CURL*);
typedef void (*CleanupFn)(CURL*);
typedef const char* (*StrerrorFn)(CURLcode);

struct Curl {
	InitFn init = NULL;
	SetoptFn setopt = NULL;
	PerformFn perform = NULL;
	CleanupFn cleanup = NULL;
	StrerrorFn strerror = NULL;
	bool ok = false;
};

/** The address of `name` in something already loaded in this process, or NULL.
 *
 * Never loads anything: a second libcurl in one process is exactly the problem
 * this whole file exists to avoid. */
void* loadedSymbol(const char* name) {
#if defined ARCH_WIN
	// Windows has no process-wide symbol table to search -- no RTLD_DEFAULT --
	// only per-module export tables, so the modules that could be re-exporting
	// curl have to be named. GetModuleHandle never loads and never bumps a
	// refcount; NULL asks the running executable, which is Rack.exe standalone
	// and the host application when Rack runs as a plugin inside a DAW.
	static const char* const MODULES[] = {"libRack.dll", "Rack.exe", "libcurl.dll"};
	if (HMODULE self = GetModuleHandleA(NULL))
		if (FARPROC p = GetProcAddress(self, name))
			return (void*) p;
	for (size_t i = 0; i < sizeof(MODULES) / sizeof(MODULES[0]); i++) {
		HMODULE h = GetModuleHandleA(MODULES[i]);
		if (!h)
			continue;
		if (FARPROC p = GetProcAddress(h, name))
			return (void*) p;
	}
	return NULL;
#else
	// RTLD_DEFAULT searches everything already loaded, which is where
	// libRack's copy lives. No dlopen, so no second libcurl in the process.
	return dlsym(RTLD_DEFAULT, name);
#endif
}

Curl& curlApi() {
	static Curl c;
	static std::once_flag once;
	std::call_once(once, [] {
		c.init     = (InitFn)     loadedSymbol("curl_easy_init");
		c.setopt   = (SetoptFn)   loadedSymbol("curl_easy_setopt");
		c.perform  = (PerformFn)  loadedSymbol("curl_easy_perform");
		c.cleanup  = (CleanupFn)  loadedSymbol("curl_easy_cleanup");
		c.strerror = (StrerrorFn) loadedSymbol("curl_easy_strerror");
		c.ok = c.init && c.setopt && c.perform && c.cleanup;
		if (!c.ok)
			INFO("PatchAudit: libcurl not reachable; large zipped uploads will be "
			     "skipped by the audit rather than streamed");
	});
	return c;
}

struct SinkState {
	const Sink* sink;
	bool stopped;
};

/** curl signals "abort" by the write callback returning something other than the
    byte count it was given. That surfaces as CURLE_WRITE_ERROR, which get()
    translates back into success -- it is what a deliberate hang-up looks like. */
size_t writeCb(char* ptr, size_t size, size_t nmemb, void* userdata) {
	SinkState* st = (SinkState*) userdata;
	size_t n = size * nmemb;
	if (!(*st->sink)((const uint8_t*) ptr, n)) {
		st->stopped = true;
		return 0;
	}
	return n;
}

} // namespace

bool available() {
	return curlApi().ok;
}

bool get(const std::string& url, const Sink& sink, std::string* error) {
	Curl& c = curlApi();
	if (!c.ok) {
		if (error)
			*error = "libcurl is not available in this process";
		return false;
	}

	CURL* h = c.init();
	if (!h) {
		if (error)
			*error = "could not create a curl handle";
		return false;
	}

	SinkState st;
	st.sink = &sink;
	st.stopped = false;

	// Mirrors Rack's own createCurl (Rack/src/network.cpp:22-48) so this behaves
	// like every other request the app makes -- same CA bundle above all, since
	// without CURLOPT_CAINFO the handle falls back to a system store curl may not
	// have been built against, and every HTTPS fetch fails.
	std::string userAgent = APP_NAME + " " + APP_EDITION_NAME + "/" + APP_VERSION;
	std::string caPath = asset::system("cacert.pem");
	c.setopt(h, CURLOPT_URL, url.c_str());
	c.setopt(h, CURLOPT_USERAGENT, userAgent.c_str());
	c.setopt(h, CURLOPT_FOLLOWLOCATION, 1L);
	c.setopt(h, CURLOPT_CONNECTTIMEOUT, 30L);
	// curl raises a signal when a DNS lookup times out, which kills the process
	// when it happens off the main thread. Rack disables it for the same reason.
	c.setopt(h, CURLOPT_NOSIGNAL, 1L);
	c.setopt(h, CURLOPT_CAINFO, caPath.c_str());
	c.setopt(h, CURLOPT_SSL_VERIFYPEER, 1L);
	c.setopt(h, CURLOPT_FAILONERROR, 1L);
	c.setopt(h, CURLOPT_WRITEFUNCTION, writeCb);
	c.setopt(h, CURLOPT_WRITEDATA, &st);

	CURLcode res = c.perform(h);
	c.cleanup(h);

	if (res == CURLE_OK || (res == CURLE_WRITE_ERROR && st.stopped))
		return true;
	if (error) {
		const char* msg = c.strerror ? c.strerror(res) : NULL;
		*error = msg ? msg : "transfer failed";
	}
	return false;
}

} // namespace http
} // namespace ps

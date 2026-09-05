#include "Api.hpp"
#include "HttpCache.hpp"
#include "Html.hpp"
#include "JobRunner.hpp"
#include "Json.hpp"

#include <rack.hpp>

#include <mutex>

using namespace rack;

namespace ps {

/** Minimum gap between two outbound HTTP requests, across every lane.
 *
 * This is the whole politeness policy, and it lives here rather than in
 * JobRunner because only this function knows whether a request is about to
 * happen at all. Pacing the *lane* instead made every cache hit wait its turn
 * behind a floor meant for the network: a page of results already on disk paid
 * the full gap per job to do nothing but read a file. */
static const double MIN_REQUEST_GAP = 0.25;

static std::mutex g_gateMutex;
static double g_lastRequestTime = 0.0;

/** Blocks until at least MIN_REQUEST_GAP has passed since the last request
    started, then claims the slot. Returns false if we were asked to stop while
    waiting, in which case the caller must not issue the request.
 *
 * The mutex is held across the sleep on purpose: it is what serialises
 * concurrent callers into a queue rather than letting them all wake at once and
 * fire together. */
static bool awaitRequestSlot() {
	std::lock_guard<std::mutex> lock(g_gateMutex);
	double wait = MIN_REQUEST_GAP - (system::getTime() - g_lastRequestTime);
	if (wait > 0.0 && !JobRunner::global().interruptibleSleep(wait))
		return false;
	g_lastRequestTime = system::getTime();
	return true;
}

ApiResponse classify(json_t* j) {
	ApiResponse r;
	r.body = j;
	if (!j) {
		r.status = ApiStatus::Network;
		r.message = "Could not reach patchstorage.com.";
		return r;
	}
	if (json_is_array(j))
		return r;
	if (json_is_object(j)) {
		// Check for a real object FIRST. A patch's own detail response carries a
		// "code" field of its own -- the source-code field for text-based
		// platforms, an empty string for VCV -- which looks exactly like a
		// WordPress error code. Only a body with no "id" is an error.
		if (json_object_get(j, "id"))
			return r;
		json_t* codeJ = json_object_get(j, "code");
		if (codeJ && json_is_string(codeJ)) {
			r.status = ApiStatus::ApiError;
			r.message = plainText(jstr(j, "message", "Patchstorage returned an error."));
			r.httpStatus = (int) jint(json_object_get(j, "data"), "status", 0);
			return r;
		}
	}
	r.status = ApiStatus::Parse;
	r.message = "Unexpected response from patchstorage.com.";
	return r;
}

json_t* getJson(const std::string& url, double cacheTtl, ApiResponse* outResponse,
                bool* outFromCache, bool acceptAnyJson) {
	if (outFromCache)
		*outFromCache = false;

	json_t* cached = cache::loadJson(url, cacheTtl);
	if (cached) {
		ApiResponse r = acceptAnyJson ? ApiResponse() : classify(cached);
		r.body = cached;
		if (r.status == ApiStatus::Ok) {
			if (outFromCache) *outFromCache = true;
			if (outResponse) *outResponse = r;
			return cached;
		}
		json_decref(cached);
	}

	if (!awaitRequestSlot()) {
		ApiResponse r;
		r.status = ApiStatus::Network;
		r.message = "Cancelled.";
		if (outResponse)
			*outResponse = r;
		return NULL;
	}
	json_t* body = network::requestJson(network::METHOD_GET, url, NULL);
	ApiResponse r;
	if (acceptAnyJson) {
		r.body = body;
		if (!body) {
			r.status = ApiStatus::Network;
			r.message = "Could not reach the server.";
		}
	}
	else {
		r = classify(body);
	}
	if (outResponse) {
		*outResponse = r;
		outResponse->body = body;
	}
	if (r.status == ApiStatus::Ok)
		cache::storeJson(url, body);
	return body;
}

PatchSummary parseSummary(json_t* p) {
	PatchSummary s;
	s.id = jint(p, "id");
	s.title = plainText(jstr(p, "title"));
	s.slug = jstr(p, "slug");
	s.url = jstr(p, "url");
	s.excerpt = plainText(jstr(p, "excerpt"));
	s.updatedAt = jstr(p, "updated_at");
	s.downloads = jint(p, "download_count");
	s.likes = jint(p, "like_count");
	s.views = jint(p, "view_count");
	json_t* authorJ = json_object_get(p, "author");
	if (authorJ)
		s.author = plainText(jstr(authorJ, "name"));
	if (s.title.empty())
		s.title = "(untitled)";
	return s;
}

} // namespace ps

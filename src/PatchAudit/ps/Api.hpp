#pragma once
#include "Types.hpp"

#include <jansson.h>

#include <string>

namespace ps {

static const char* const API_BASE = "https://patchstorage.com/api/beta";

/** What came back from network::requestJson.

We can't read HTTP status codes -- network::requestJson exposes no headers -- but
it also doesn't set CURLOPT_FAILONERROR, so a WordPress error body arrives intact
as {"code":..,"message":..,"data":{"status":400}}. That is where the status code
comes from. A NULL return therefore means transport failure or a non-JSON body,
which is a cleaner signal than it first looks.
*/
struct ApiResponse {
	ApiStatus status = ApiStatus::Ok;
	int httpStatus = 0;
	std::string message;
	json_t* body = NULL;      // borrowed; owned by the caller's JsonRef
};

ApiResponse classify(json_t* j);

/** GET with the disk cache in front of it. Returns a new reference or NULL.
    `outFromCache` reports whether the network was touched.

    classify() knows Patchstorage's response shapes; set `acceptAnyJson` for
    other hosts (the VCV Library manifests are a bare object, which classify
    would otherwise call a parse failure -- and refuse to cache). */
json_t* getJson(const std::string& url, double cacheTtl, ApiResponse* outResponse,
                bool* outFromCache = NULL, bool acceptAnyJson = false);

PatchSummary parseSummary(json_t* p);

} // namespace ps

#include "PatchstorageClient.hpp"
#include "Api.hpp"
#include "Audit.hpp"
#include "HttpCache.hpp"
#include "Html.hpp"
#include "Installer.hpp"
#include "JobRunner.hpp"
#include "Json.hpp"
#include "VcvLibrary.hpp"

#include <rack.hpp>

using namespace rack;

namespace ps {

static const double SEARCH_DEBOUNCE = 0.35;

std::string buildSearchUrl(const SearchQuery& q) {
	// Built by hand rather than through requestJson's `dataJ` argument: that
	// builder only serialises JSON *strings* (Rack/src/network.cpp:99-112), so
	// every integer parameter would be silently dropped.
	std::string url = std::string(API_BASE) + "/patches";
	url += string::f("?platforms=%lld", (long long) PLATFORM_VCV_RACK);
	url += string::f("&per_page=%d", q.perPage);
	url += string::f("&page=%d", q.page);
	const SortMode& s = SORTS[clamp(q.sortIndex, 0, NUM_SORTS - 1)];
	url += std::string("&orderby=") + s.orderBy + "&order=" + s.order;
	if (q.categoryId > 0)
		url += string::f("&categories=%lld", (long long) q.categoryId);
	if (!q.search.empty())
		url += "&search=" + network::encodeUrl(q.search);
	return url;
}

PatchstorageClient::PatchstorageClient()
	: abandoned(false), searchGen(0), installGen(0), auditGen(0),
	  auditDone(0), auditTotal(0),
	  searchDirty(false), installDirty(false), auditDirty(false),
	  importNoteDirty(false),
	  downloadProgress(0.f), busy(false), searchDeadline(0.0) {}

void PatchstorageClient::abandon() {
	abandoned = true;
	std::lock_guard<std::mutex> lock(mutex);
	searchGen++;
	installGen++;
	auditGen++;
}

SearchResult PatchstorageClient::snapshotSearch() {
	std::lock_guard<std::mutex> lock(mutex);
	return search;
}

InstallResult PatchstorageClient::snapshotInstall() {
	std::lock_guard<std::mutex> lock(mutex);
	return install;
}

void PatchstorageClient::setInstall(const InstallResult& r) {
	{
		std::lock_guard<std::mutex> lock(mutex);
		install = r;
	}
	installDirty = true;
}

void PatchstorageClient::requestSearch(const SearchQuery& q) {
	pendingQuery = q;
	searchDeadline = system::getTime() + SEARCH_DEBOUNCE;
}

// --- search -----------------------------------------------------------------

static void searchJob(std::shared_ptr<PatchstorageClient> self, SearchQuery q, uint64_t gen) {
	if (self->abandoned)
		return;

	SearchResult out;
	out.query = q;

	std::string url = buildSearchUrl(q);
	ApiResponse resp;
	bool fromCache = false;
	JsonRef rootJ(getJson(url, cache::TTL_SEARCH, &resp, &fromCache));
	out.fromCache = fromCache;

	if (resp.status != ApiStatus::Ok) {
		out.status = resp.status;
		out.message = resp.message;
		if (resp.httpStatus == 429 || resp.message.find("too many") != std::string::npos)
			out.message = "Patchstorage is rate-limiting. Try again in a moment.";
	}
	else if (!json_is_array(rootJ.get())) {
		out.status = ApiStatus::Parse;
		out.message = "Unexpected response from patchstorage.com.";
	}
	else {
		size_t i;
		json_t* p;
		json_array_foreach(rootJ.get(), i, p)
			out.patches.push_back(parseSummary(p));

		// X-WP-Total is unreadable through requestJson, so the last page is
		// inferred: a short page is the end of the results.
		int n = (int) out.patches.size();
		if (n < q.perPage) {
			out.lastPageKnown = true;
			out.knownLastPage = (n == 0 && q.page > 1) ? q.page - 1 : q.page;
		}
		else {
			out.knownLastPage = q.page;
		}
	}

	{
		std::lock_guard<std::mutex> lock(self->mutex);
		if (gen != self->searchGen)
			return;                       // superseded while we were waiting
		self->search = out;
		self->busy = false;
	}
	self->searchDirty = true;
}

void PatchstorageClient::issueSearchNow(const SearchQuery& q) {
	searchDeadline = 0.0;
	pendingQuery = q;

	uint64_t gen;
	{
		std::lock_guard<std::mutex> lock(mutex);
		gen = ++searchGen;
		busy = true;
	}
	std::shared_ptr<PatchstorageClient> self = shared_from_this();
	JobRunner::global().submit(Lane::Api, std::bind(&searchJob, self, q, gen));
}

// --- install ----------------------------------------------------------------

void PatchstorageClient::issueInstall(const PatchSummary& patch, Intent intent, int64_t explicitFileId) {
	uint64_t gen;
	{
		std::lock_guard<std::mutex> lock(mutex);
		gen = ++installGen;
		install = InstallResult();
		install.phase = InstallPhase::FetchingDetail;
		install.intent = intent;
		install.patchId = patch.id;
		install.title = patch.title;
		install.patchstorageUrl = patch.url;
		downloadProgress = 0.f;
	}
	installDirty = true;

	std::shared_ptr<PatchstorageClient> self = shared_from_this();
	JobRunner::global().submit(Lane::Bulk,
		std::bind(&installJob, self, patch, intent, explicitFileId, gen));
}

void PatchstorageClient::cancelInstall() {
	{
		std::lock_guard<std::mutex> lock(mutex);
		installGen++;
		install = InstallResult();
		downloadProgress = 0.f;
	}
	installDirty = true;
}

void PatchstorageClient::issueAudit(const std::vector<PatchSummary>& page) {
	uint64_t gen;
	{
		std::lock_guard<std::mutex> lock(mutex);
		gen = ++auditGen;
		auditInbox.clear();
		auditDone = 0;
		auditTotal = (int) page.size();
	}
	auditDirty = true;
	if (page.empty())
		return;
	std::shared_ptr<PatchstorageClient> self = shared_from_this();
	JobRunner::global().submit(Lane::Bulk, std::bind(&auditJob, self, page, gen));
}

void PatchstorageClient::cancelAudit() {
	{
		std::lock_guard<std::mutex> lock(mutex);
		auditGen++;
		auditInbox.clear();
		auditDone = 0;
		auditTotal = 0;
	}
	auditDirty = true;
}

void PatchstorageClient::drainAudits(std::vector<AuditRaw>& out, int& done, int& total) {
	std::lock_guard<std::mutex> lock(mutex);
	out.swap(auditInbox);
	auditInbox.clear();
	done = auditDone;
	total = auditTotal;
}

void PatchstorageClient::issueManifests() {
	vcvlib::ensureLoaded();
}

} // namespace ps

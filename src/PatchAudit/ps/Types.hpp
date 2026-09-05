#pragma once
#include <stdint.h>

#include <string>
#include <vector>

/** Plain data shared between worker threads and the UI.

Nothing in this header may reference a Rack Module, Widget, or any other object
Rack owns -- see the invariant at the top of PatchstorageClient.hpp.
*/
namespace ps {

// --- Patchstorage taxonomy --------------------------------------------------

/** The "VCV Rack" platform id. Verified against GET /api/beta/platforms. */
static const int64_t PLATFORM_VCV_RACK = 745;

struct Category {
	int64_t id;
	const char* slug;
	const char* name;
};

/** GET /api/beta/categories returns exactly these ten, and has for years. They
    are hardcoded so the first paint needs no network round trip; CATEGORIES is
    re-validated against the API in a debug build only. */
static const Category CATEGORIES[] = {
	{0,    "all",          "All categories"},
	{74,   "synthesizer",  "Synthesizer"},
	{75,   "sampler",      "Sampler"},
	{76,   "sequencer",    "Sequencer"},
	{77,   "effect",       "Effect"},
	{91,   "video",        "Video"},
	{117,  "utility",      "Utility"},
	{372,  "sound",        "Sound"},
	{378,  "composition",  "Composition"},
	{3317, "game",         "Game"},
	{1,    "other",        "Other"},
};
static const int NUM_CATEGORIES = (int) (sizeof(CATEGORIES) / sizeof(CATEGORIES[0]));

struct SortMode {
	const char* orderBy;   // Patchstorage `orderby`
	const char* order;     // `asc` | `desc`
	const char* name;
};

/** Only these `orderby` values are accepted by the API; `relevance`, `rand` and
    `comment_count` all return HTTP 400. Verified against the live endpoint. */
static const SortMode SORTS[] = {
	{"date",           "desc", "Newest"},
	{"date",           "asc",  "Oldest"},
	{"download_count", "desc", "Most downloaded"},
	{"like_count",     "desc", "Most liked"},
	{"view_count",     "desc", "Most viewed"},
	{"title",          "asc",  "Title A-Z"},
};
static const int NUM_SORTS = (int) (sizeof(SORTS) / sizeof(SORTS[0]));

// --- queries and results ----------------------------------------------------

struct SearchQuery {
	std::string search;
	int64_t categoryId = 0;     // 0 = all
	int sortIndex = 2;          // Most downloaded
	int page = 1;               // 1-based, as the API counts
	int perPage = 20;
	bool favorites = false;     // render the local favourites list, no network

	bool sameFilters(const SearchQuery& o) const {
		return search == o.search && categoryId == o.categoryId
		    && sortIndex == o.sortIndex && perPage == o.perPage
		    && favorites == o.favorites;
	}
	bool operator==(const SearchQuery& o) const { return sameFilters(o) && page == o.page; }
	bool operator!=(const SearchQuery& o) const { return !(*this == o); }
};

struct PatchSummary {
	int64_t id = 0;
	std::string title;
	std::string author;
	std::string slug;
	std::string url;            // patchstorage.com page
	std::string excerpt;
	std::string updatedAt;
	int64_t downloads = 0;
	int64_t likes = 0;
	int64_t views = 0;
};

struct PatchFile {
	int64_t id = 0;
	std::string url;
	std::string filename;
	int64_t filesize = 0;
};

enum class ApiStatus { Ok, Network, ApiError, Parse };

struct SearchResult {
	SearchQuery query;
	std::vector<PatchSummary> patches;
	ApiStatus status = ApiStatus::Ok;
	std::string message;
	bool fromCache = false;
	/** Highest page known to exist, and whether that's the definitive last page.
	    The API's X-WP-Total header is unreadable through network::requestJson,
	    so paging is inferred: a short page is the last one. */
	int knownLastPage = 0;
	bool lastPageKnown = false;
};

// --- the load pipeline ------------------------------------------------------

/** How hard to look at what a patch actually needs.

Patchstorage's API says nothing about a patch's contents, so every mode past Off
means downloading each result on the page and reading its module list. Off is the
default for that reason. */
enum class AuditMode {
	Off,             ///< don't scan
	Annotate,        ///< scan and badge each row, keep the API's order
	PlayableFirst,   ///< scan, badge, and float fully-playable patches to the top
	OnlyPlayable,    ///< scan, badge, and hide anything with a missing module
};

static const char* const AUDIT_MODE_NAMES[] = {
	"Off",
	"Show what's missing",
	"Playable first",
	"Only patches I can play",
};

/** What the user asked for. There is no "replace the rack" intent: see the note
    in ps/Apply.hpp for why that feature was cut rather than made safe. */
enum class Intent { Import, SaveToDisk };

enum class InstallPhase {
	Idle,
	FetchingDetail,
	NeedsFileChoice,
	Downloading,
	Unarchiving,
	Parsing,
	ReadyToApply,
	Failed,
};

enum class FailureKind {
	None, Network, ApiError, NotFound, NoFile, UnsupportedFile, TooLarge,
	DownloadFailed, CorruptArchive, NoPatchJson,
};

struct InstallResult {
	InstallPhase phase = InstallPhase::Idle;
	Intent intent = Intent::Import;
	int64_t patchId = 0;
	std::string title;
	std::string patchstorageUrl;

	std::vector<PatchFile> alternateFiles;   // set when phase == NeedsFileChoice

	std::string archivePath;                 // the downloaded .vcv (or .vcvs)
	std::string patchJsonPath;               // extracted patch.json, or the .vcvs itself
	std::string suggestedFilename;
	bool isSelectionFile = false;            // .vcvs: import only, no replace

	FailureKind failure = FailureKind::None;
	std::string message;
	bool retryable = false;
};

struct Favorite {
	int64_t id = 0;
	std::string title;
	std::string author;
};

} // namespace ps

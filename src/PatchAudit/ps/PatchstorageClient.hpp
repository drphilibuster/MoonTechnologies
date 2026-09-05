#pragma once
#include "Audit.hpp"
#include "Types.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace ps {

/** Shared state between the UI thread and the worker lanes.
 *
 * INVARIANT -- the whole thread-safety story of this plugin rests on it:
 *
 *   PatchstorageClient holds only plain data. It never stores a pointer to a
 *   Module, a Widget, or anything else Rack owns. Every job lambda captures
 *   exactly one std::shared_ptr<PatchstorageClient> plus POD parameters, all by
 *   value. The client therefore outlives every job that references it, and no
 *   worker thread can ever touch a freed object -- not when the module is
 *   deleted, not when the patch is cleared, not when Rack quits.
 *
 * The module's destructor calls abandon(), which both flags the client dead and
 * bumps every generation counter, so in-flight jobs bail at their next check and
 * their results are discarded rather than published.
 */
struct PatchstorageClient : std::enable_shared_from_this<PatchstorageClient> {

	// --- cancellation --------------------------------------------------------
	std::atomic<bool> abandoned;
	void abandon();

	// --- results, all guarded by `mutex` ------------------------------------
	// The generation counters are plain integers under the same mutex as the
	// results they guard, not atomics: check-and-publish has to be one atomic
	// step with respect to a bump, or a late worker could still land a stale
	// result in the window between its check and its write.
	std::mutex mutex;
	uint64_t searchGen;
	uint64_t installGen;
	uint64_t auditGen;
	SearchResult search;
	InstallResult install;
	/** Audited patches waiting for the UI thread to resolve availability. */
	std::vector<AuditRaw> auditInbox;
	int auditDone;
	int auditTotal;
	/** What the last import did. Written by the menu action that performed it,
	    which may outlive the widget that opened the menu. */
	std::string importNote;

	// --- "something changed", consumed once by step() ------------------------
	std::atomic<bool> searchDirty;
	std::atomic<bool> installDirty;
	std::atomic<bool> auditDirty;
	std::atomic<bool> importNoteDirty;

	// --- benign races: one writer, one reader, torn reads are harmless.
	// Same pattern as Rack's own library::updateProgress / isSyncing.
	float downloadProgress;
	bool busy;

	// --- debounce, UI thread only -------------------------------------------
	double searchDeadline;      // system::getTime() target, 0 = idle
	SearchQuery pendingQuery;

	PatchstorageClient();

	/** Debounced: records the query and arms the deadline that step() polls. */
	void requestSearch(const SearchQuery& q);
	/** Issues immediately, superseding anything in flight. */
	void issueSearchNow(const SearchQuery& q);
	/** Fetches the VCV Library manifests once, well before anything needs them. */
	void issueManifests();
	/** Runs the whole load pipeline on the Bulk lane. */
	void issueInstall(const PatchSummary& patch, Intent intent, int64_t explicitFileId = -1);
	/** Discards whatever install is in flight. */
	void cancelInstall();
	/** Starts auditing a page of results, superseding any audit in flight. */
	void issueAudit(const std::vector<PatchSummary>& page);
	/** Stops the audit and clears its progress. */
	void cancelAudit();
	/** Moves everything the worker has finished into `out`, under the lock. */
	void drainAudits(std::vector<AuditRaw>& out, int& done, int& total);

	/** Snapshot helpers so callers never hold the lock while touching widgets. */
	SearchResult snapshotSearch();
	InstallResult snapshotInstall();
	void setInstall(const InstallResult& r);
};

/** Builds a fully encoded patches query URL. Public for the unit test. */
std::string buildSearchUrl(const SearchQuery& q);

} // namespace ps

#pragma once
#include "Types.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ps {

struct PatchstorageClient;

/** The plugin/model pairs one patch uses, as extracted on a worker thread.
 *
 * Only the slugs cross the thread boundary. Deciding whether a model is
 * *available* means walking plugin::plugins, which is a UI-thread job (see
 * ModuleScan.hpp), so that step happens when the UI drains this. */
struct AuditRaw {
	int64_t patchId = 0;
	std::vector<std::pair<std::string, std::string> > slugs;
	bool failed = false;
	std::string note;
};

/** The same patch after the UI thread has resolved availability. */
struct AuditInfo {
	int total = 0;
	int missing = 0;
	/** Of `missing`, how many have no route to them on this machine -- delisted,
	    retired, or built for no architecture we run. A patch with any of these
	    will not run here however much the user installs, which is a different
	    thing to say than "you are three plugins short". */
	int deadEnds = 0;
	/** False while the VCV Library manifests are still loading or have failed. No
	    row may be called a dead end on the strength of a library we haven't heard
	    from -- that would paint a whole page hopeless for a slow network. */
	bool libraryKnown = false;
	bool failed = false;
	std::string note;
	bool playable() const { return !failed && missing == 0; }
	/** Missing something, and at least one of them is unobtainable. */
	bool unobtainable() const { return !failed && deadEnds > 0; }
};

/** Downloads and inspects every patch on a page, so the browser can say which
 * ones you can actually run.
 *
 * Patchstorage has no module list in its API -- the only way to know what a
 * patch contains is to fetch the patch -- so this is genuinely N downloads for
 * a page of N results. That is why it is opt-in, serialised on the Bulk lane
 * with a floor between requests, and why every result is cached twice over: the
 * archive in the patch cache, and the extracted slug list in the JSON cache,
 * keyed by the patch's updated_at so re-visiting a page costs nothing. */
void auditJob(std::shared_ptr<PatchstorageClient> self, std::vector<PatchSummary> page,
              uint64_t gen);

} // namespace ps

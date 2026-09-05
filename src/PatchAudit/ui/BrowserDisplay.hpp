#pragma once
#include "../ps/Audit.hpp"
#include "../ps/BrowseState.hpp"
#include "../ps/PatchstorageClient.hpp"
#include "PatchRow.hpp"

#include <rack.hpp>

#include <map>
#include <memory>

namespace ui_pa {

/** What the display needs from the module widget. Implemented by
    PatchAuditWidget; never crosses a thread. */
struct BrowserHost {
	virtual ~BrowserHost() {}
	virtual ps::PatchstorageClient* client() = 0;
	virtual ps::BrowseState* state() = 0;
	/** Re-runs the current query. `immediate` skips the keystroke debounce. */
	virtual void refreshSearch(bool immediate) = 0;
	/** OPEN / IMPORT / SAVE on the selected row. */
	virtual void act(ps::Intent intent) = 0;
	virtual void openSelectedOnPatchstorage() = 0;
};

static const int kMaxRows = 50;    // == the largest per_page we offer

struct BrowserDisplay : rack::widget::Widget {
	/** NULL in Rack's module browser, where the ModuleWidget has no Module.
	    Everything that needs live state checks `preview` first. */
	BrowserHost* host = NULL;
	bool preview = false;

	rack::ui::TextField* searchField = NULL;
	rack::ui::ScrollWidget* scroll = NULL;
	std::vector<PatchRow*> rows;

	ps::SearchResult result;
	/** The raw slug lists the workers produced, kept alongside the resolved
	    AuditInfos so a page audited before the VCV Library answered can be
	    re-resolved when it does, without re-downloading anything. */
	std::map<int64_t, ps::AuditRaw> auditRaws;
	/** Indices into result.patches, in the order the rows show them. Equal to
	    0..n-1 unless an audit mode reorders or hides some. */
	std::vector<int> order;
	std::map<int64_t, ps::AuditInfo> audits;
	int auditDone = 0, auditTotal = 0;
	int selectedIndex = -1;          // index into result.patches, not into order
	std::string statusText;
	NVGcolor statusColor;
	std::string errorText;

	BrowserDisplay(rack::math::Rect box, BrowserHost* host);

	/** Copies the latest result out of the client (under its lock, released
	    before any widget is touched) and rebuilds the row pool. */
	void pullSearchResult();
	/** Drains finished audits, resolves availability against the installed
	    plugins (UI thread only -- see ps/ModuleScan.hpp) and refreshes. */
	void pullAudits();
	/** Re-runs availability resolution over the stored raw slug lists. Cheap, and
	    the only way a badge decided before the manifests loaded ever improves. */
	void reresolveAudits();
	void rebuildOrder();
	void setAuditMode(ps::AuditMode mode);
	void refreshRows();
	void updateStatus();
	void select(int index);
	void moveSelection(int delta);
	bool hasSelection() const { return selectedIndex >= 0 && selectedIndex < (int) result.patches.size(); }
	const ps::PatchSummary* selectedPatch() const;

	void setError(const std::string& message);
	void clearError();

	void onSearchTextChanged(const std::string& text);
	void toggleFavorite(int index);
	void goToPage(int page);

	void draw(const DrawArgs& args) override;
	void drawLayer(const DrawArgs& args, int layer) override;
};

} // namespace ui_pa

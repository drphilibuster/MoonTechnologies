#pragma once
#include "../ps/Audit.hpp"
#include "../ps/Types.hpp"
#include "../Panel.hpp"

#include <rack.hpp>

namespace ui_pa {

struct BrowserDisplay;

/** One result row: title on the first line, author and counts on the second.
 *
 * Rows are pooled -- BrowserDisplay creates kMaxRows of them once and only ever
 * shows, hides and refills them. Nothing is allocated or deleted per query,
 * which keeps the event system from ever holding a pointer to a row that a
 * refresh has freed. */
struct PatchRow : rack::widget::OpaqueWidget {
	BrowserDisplay* display = NULL;
	int slot = -1;                   // position in the list, 0..kMaxRows-1
	int index = -1;                  // index into the current result page
	bool selected = false;
	bool favorite = false;
	ps::PatchSummary patch;
	ps::AuditInfo audit;
	bool auditKnown = false;
	bool auditPending = false;       // scanning, badge shows a placeholder

	PatchRow();
	void setPatch(const ps::PatchSummary& p, bool isFavorite);
	void setAudit(const ps::AuditInfo* info, bool pending);
	void draw(const DrawArgs& args) override;
	void drawLayer(const DrawArgs& args, int layer) override;
	void onButton(const rack::event::Button& e) override;
	void onEnter(const rack::event::Enter& e) override;
	void onLeave(const rack::event::Leave& e) override;

	rack::math::Rect starBox() const;

private:
	bool hovered = false;

	/** Derived from `patch` and `audit`. refreshRows() refills every row on every
	    audit tick -- perPage rows, several times a second -- while only
	    ROWS_VISIBLE of them are ever on screen, so none of this is recomputed in
	    draw(). The two FittedTexts are panelkit's; the ellipsizing and its cache
	    are the tool's job, not this plugin's. */
	std::string countsText;
	std::string badgeText;
	NVGcolor badgeColor;
	panel::FittedText fittedTitle, fittedAuthor;

	void rebuildCounts();
	void rebuildBadge();
};

} // namespace ui_pa

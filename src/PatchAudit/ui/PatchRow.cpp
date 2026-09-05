#include "PatchRow.hpp"
#include "BrowserDisplay.hpp"
#include "../Panel.hpp"

#include <math.h>

using namespace rack;

namespace ui_pa {

static const float STAR_W = 14.f;   // px, right-hand hit area

// The row's three type styles, in the family's vocabulary. Sizes and inks are
// the only thing a row decides about text; measuring, ellipsizing and the
// letter-spacing discipline all belong to panelkit.
static const panel::TextStyle TITLE_STYLE(
	panel::Face::Ui, 11.5f, panel::PAPER, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
static const panel::TextStyle META_STYLE(
	panel::Face::Mono, 9.5f, panel::alpha(panel::SAGE, 0.85f),
	NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
static const panel::TextStyle BADGE_STYLE(
	panel::Face::Mono, 8.5f, panel::SAGE, NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE);

/** The badge column is reserved at its widest, so the title's right edge does
    not shift as a row's count lands. */
static const char* const BADGE_WIDEST = "-12 OF 120";

PatchRow::PatchRow() {
	box.size = mm2px(Vec(panel::IW, panel::ROW_H));
	badgeColor = panel::SAGE;
}

void PatchRow::setPatch(const ps::PatchSummary& p, bool isFavorite) {
	bool countsChanged = (p.downloads != patch.downloads || p.likes != patch.likes);
	patch = p;
	favorite = isFavorite;
	if (countsChanged || countsText.empty())
		rebuildCounts();
}

void PatchRow::setAudit(const ps::AuditInfo* info, bool pending) {
	bool known = (info != NULL);
	bool changed = (known != auditKnown || pending != auditPending);
	if (known && !changed)
		changed = (info->total != audit.total || info->missing != audit.missing
		           || info->failed != audit.failed || info->note != audit.note);
	auditKnown = known;
	auditPending = pending;
	if (info)
		audit = *info;
	if (changed)
		rebuildBadge();
}

Rect PatchRow::starBox() const {
	return Rect(Vec(box.size.x - STAR_W - 3.f, 0.f), Vec(STAR_W + 3.f, box.size.y));
}

/** 7457 -> "7.5k". Keeps the counts column narrow enough to stay out of the
    title's way on a 26 HP panel. */
static std::string abbreviate(int64_t n) {
	if (n < 1000)
		return string::f("%lld", (long long) n);
	if (n < 100000)
		return string::f("%.1fk", n / 1000.0);
	return string::f("%lldk", (long long) (n / 1000));
}

/** Favourites rows are rebuilt from local storage and carry no counts, so print
    nothing rather than a misleading "0 dl  0 lk". */
void PatchRow::rebuildCounts() {
	if (patch.downloads > 0 || patch.likes > 0)
		countsText = string::f("%s dl   %s lk",
			abbreviate(patch.downloads).c_str(), abbreviate(patch.likes).c_str());
	else
		countsText.clear();
}

/** The audit stamp: what this patch needs that you don't have. Empty when the
    audit is off, a dim rule while it's queued, then MINT "ALL n" or a clay
    count. */
void PatchRow::rebuildBadge() {
	if (!auditPending) {
		badgeText.clear();
		return;
	}
	if (!auditKnown) {
		badgeText = "--";
		badgeColor = panel::alpha(panel::SAGE, 0.4f);
	}
	else if (audit.failed) {
		badgeText = audit.note.empty() ? "?" : audit.note;
		badgeColor = panel::alpha(panel::SAGE, 0.6f);
	}
	else if (audit.missing == 0) {
		badgeText = string::f("ALL %d", audit.total);
		badgeColor = panel::MINT;
	}
	else if (audit.unobtainable()) {
		// Some of what's missing cannot be had on this machine, so no amount of
		// installing fixes it. CLAY is the palette's "action required" ink and
		// this row requires no action, so it steps back to SAGE -- and the leading
		// 'x' rather than '-' carries the same distinction without relying on
		// colour alone. 'x' is ASCII and the badge is set in a mono face, so it
		// occupies exactly the width the '-' did and the column never shifts.
		badgeText = string::f("x%d OF %d", audit.missing, audit.total);
		badgeColor = panel::alpha(panel::SAGE, 0.75f);
	}
	else {
		badgeText = string::f("-%d OF %d", audit.missing, audit.total);
		badgeColor = panel::CLAY;
	}
}

void PatchRow::draw(const DrawArgs& args) {
	if (selected) {
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0.5f, 0.5f, box.size.x - 1.f, box.size.y - 1.f, 1.5f);
		nvgFillColor(args.vg, panel::alpha(panel::LIME, 0.13f));
		nvgFill(args.vg);
		// A lime index tab down the left edge: the selected line of the form.
		nvgBeginPath(args.vg);
		nvgRect(args.vg, 0.5f, 1.5f, 1.6f, box.size.y - 3.f);
		nvgFillColor(args.vg, panel::LIME);
		nvgFill(args.vg);
	}
	else if (hovered) {
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0.5f, 0.5f, box.size.x - 1.f, box.size.y - 1.f, 1.5f);
		nvgFillColor(args.vg, panel::alpha(panel::FELT, 0.35f));
		nvgFill(args.vg);
	}
	// Ruled line between entries, as on a printed form.
	nvgBeginPath(args.vg);
	nvgRect(args.vg, 2.f, box.size.y - 0.5f, box.size.x - 4.f, 0.4f);
	nvgFillColor(args.vg, panel::alpha(panel::RULE, 0.55f));
	nvgFill(args.vg);

	Widget::draw(args);
}

static void drawStar(NVGcontext* vg, float cx, float cy, float r, bool filled) {
	nvgBeginPath(vg);
	for (int i = 0; i < 10; i++) {
		float a = (float) (-M_PI / 2 + i * M_PI / 5);
		float rr = (i % 2 == 0) ? r : r * 0.45f;
		nvgLineTo(vg, cx + rr * std::cos(a), cy + rr * std::sin(a));
	}
	nvgClosePath(vg);
	if (filled) {
		nvgFillColor(vg, panel::LIME);
		nvgFill(vg);
	}
	else {
		nvgStrokeColor(vg, panel::alpha(panel::SAGE, 0.55f));
		nvgStrokeWidth(vg, 0.9f);
		nvgStroke(vg);
	}
}

void PatchRow::drawLayer(const DrawArgs& args, int layer) {
	if (layer != 1) {
		Widget::drawLayer(args, layer);
		return;
	}

	const float pad = 6.f;
	const float right = box.size.x - STAR_W - 6.f;

	// The badge owns the right end of line 1, so the title yields to it.
	float badgeW = 0.f;
	if (!badgeText.empty()) {
		badgeW = panel::textWidth(args.vg, BADGE_STYLE, BADGE_WIDEST) + 6.f;
		panel::text(args.vg, BADGE_STYLE.inked(badgeColor), right, 12.f, badgeText);
	}

	// line 1: title
	panel::TextStyle title = TITLE_STYLE.inked(selected ? panel::LIME : panel::PAPER);
	panel::text(args.vg, title, pad, 12.f,
		fittedTitle.get(args.vg, title, patch.title, right - pad - badgeW));

	// line 2: author, then counts, right-aligned
	float countsW = 0.f;
	if (!countsText.empty()) {
		countsW = panel::textWidth(args.vg, META_STYLE, countsText);
		panel::text(args.vg, META_STYLE, right - countsW, 23.f, countsText);
	}
	panel::text(args.vg, META_STYLE, pad, 23.f,
		fittedAuthor.get(args.vg, META_STYLE, patch.author,
			right - countsW - pad - 8.f));

	drawStar(args.vg, box.size.x - STAR_W / 2 - 3.f, box.size.y / 2, 5.f, favorite);

	Widget::drawLayer(args, layer);
}

void PatchRow::onButton(const event::Button& e) {
	if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
		e.consume(this);
		if (index < 0)
			return;
		if (starBox().contains(e.pos)) {
			display->toggleFavorite(index);
			return;
		}
		display->select(index);
		return;
	}
	OpaqueWidget::onButton(e);
}

void PatchRow::onEnter(const event::Enter& e) {
	hovered = true;
	OpaqueWidget::onEnter(e);
}

void PatchRow::onLeave(const event::Leave& e) {
	hovered = false;
	OpaqueWidget::onLeave(e);
}

} // namespace ui_pa

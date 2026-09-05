#include "BrowserDisplay.hpp"
#include "../Panel.hpp"
#include "../ps/Resolve.hpp"

#include <algorithm>

using namespace rack;

namespace ui_pa {

// --- small shared widgets ---------------------------------------------------

/** A dark pill with centred text that runs a callback when clicked. */
struct FlatButton : widget::OpaqueWidget {
	std::string text;
	std::function<void()> action;
	bool enabled = true;
	bool accent = false;
	float fontSize = 11.f;
	bool hovered = false;

	void draw(const DrawArgs& args) override {
		if (hovered && enabled) {
			nvgBeginPath(args.vg);
			nvgRoundedRect(args.vg, 0.5f, 0.5f, box.size.x - 1.f, box.size.y - 1.f, 1.f);
			nvgFillColor(args.vg, panel::alpha(panel::LIME, accent ? 0.20f : 0.10f));
			nvgFill(args.vg);
		}
		Widget::draw(args);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			NVGcolor c = enabled ? (accent ? panel::LIME : panel::PAPER) : panel::RULE;
			panel::TextStyle st(panel::Face::Ui, fontSize, c,
				NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, 0.8f);
			panel::text(args.vg, st, box.size.x / 2, box.size.y / 2 + 0.5f, text);
		}
		Widget::drawLayer(args, layer);
	}

	void onButton(const event::Button& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			e.consume(this);
			if (enabled && action)
				action();
			return;
		}
		OpaqueWidget::onButton(e);
	}

	void onEnter(const event::Enter& e) override { hovered = true; OpaqueWidget::onEnter(e); }
	void onLeave(const event::Leave& e) override { hovered = false; OpaqueWidget::onLeave(e); }
};

/** A left-aligned choice that opens a menu, styled to match the panel. */
struct MenuChoice : widget::OpaqueWidget {
	std::string label;
	std::string value;
	std::function<void(ui::Menu*)> buildMenu;
	bool hovered = false;

	void draw(const DrawArgs& args) override {
		if (hovered) {
			nvgBeginPath(args.vg);
			nvgRoundedRect(args.vg, 0.5f, 0.5f, box.size.x - 1.f, box.size.y - 1.f, 1.f);
			nvgFillColor(args.vg, panel::alpha(panel::LIME, 0.09f));
			nvgFill(args.vg);
		}
		Widget::draw(args);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			static const panel::TextStyle CAPTION(panel::Face::Ui, 7.6f, panel::SAGE,
				NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, 0.9f);
			static const panel::TextStyle VALUE(panel::Face::Ui, 10.5f, panel::PAPER,
				NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
			float labelW = panel::textWidth(args.vg, CAPTION, label);
			panel::text(args.vg, CAPTION, 6.f, box.size.y / 2, label);
			panel::text(args.vg, VALUE, 6.f + labelW + 7.f, box.size.y / 2, value);
			// caret
			nvgBeginPath(args.vg);
			float cx = box.size.x - 8.f, cy = box.size.y / 2;
			nvgMoveTo(args.vg, cx - 3.f, cy - 1.5f);
			nvgLineTo(args.vg, cx + 3.f, cy - 1.5f);
			nvgLineTo(args.vg, cx, cy + 2.5f);
			nvgClosePath(args.vg);
			nvgFillColor(args.vg, panel::LIME);
			nvgFill(args.vg);
		}
		Widget::drawLayer(args, layer);
	}

	void onButton(const event::Button& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			e.consume(this);
			if (!buildMenu)
				return;
			ui::Menu* menu = createMenu();
			buildMenu(menu);
			return;
		}
		OpaqueWidget::onButton(e);
	}
	void onEnter(const event::Enter& e) override { hovered = true; OpaqueWidget::onEnter(e); }
	void onLeave(const event::Leave& e) override { hovered = false; OpaqueWidget::onLeave(e); }
};

/** ui::ScrollWidget scissors draw() (Rack/src/ui/ScrollWidget.cpp:66-70) but does
    not override drawLayer(), so anything a child draws on the light layer -- which
    is where PatchRow's text lives -- escapes the viewport and paints over the
    status line while scrolling. Clip it the same way. */
struct ClippedScroll : ui::ScrollWidget {
	void drawLayer(const DrawArgs& args, int layer) override {
		nvgScissor(args.vg, RECT_ARGS(args.clipBox));
		ui::ScrollWidget::drawLayer(args, layer);
		nvgResetScissor(args.vg);
	}
};

/** The search box. Deliberately does NOT grab focus in step() the way Rack's own
    BrowserSearchField does (Rack/src/app/Browser.cpp:351-373): that is right for
    a modal browser and would hijack every keystroke in Rack from a panel. */
struct SearchField : ui::TextField {
	BrowserDisplay* display = NULL;

	SearchField() { placeholder = "Search patches"; }

	void draw(const DrawArgs& args) override {
		// TextField's default look is the light Blendish theme; draw our own.
		static const panel::TextStyle ENTRY(panel::Face::Ui, 12.f, panel::PAPER,
			NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);

		bool focused = APP->event->getSelectedWidget() == this;
		if (focused) {
			nvgBeginPath(args.vg);
			nvgRoundedRect(args.vg, 0.5f, 0.5f, box.size.x - 1.f, box.size.y - 1.f, 2.5f);
			nvgStrokeColor(args.vg, panel::LIME);
			nvgStrokeWidth(args.vg, 0.9f);
			nvgStroke(args.vg);
		}

		panel::TextStyle st = ENTRY.inked(
			text.empty() ? panel::alpha(panel::SAGE, 0.45f) : panel::PAPER);
		nvgScissor(args.vg, 4.f, 0.f, box.size.x - 8.f, box.size.y);
		const float x = 7.f;
		panel::text(args.vg, st, x, box.size.y / 2,
			text.empty() ? placeholder : text);

		if (focused && !text.empty()) {
			std::string upTo = text.substr(0, std::min((size_t) std::max(cursor, 0), text.size()));
			float cx = x + panel::textWidth(args.vg, ENTRY, upTo);
			nvgBeginPath(args.vg);
			nvgRect(args.vg, cx, 4.f, 1.f, box.size.y - 8.f);
			nvgFillColor(args.vg, panel::LIME);
			nvgFill(args.vg);
		}
		nvgResetScissor(args.vg);
	}

	void onChange(const event::Change& e) override {
		if (display)
			display->onSearchTextChanged(text);
		TextField::onChange(e);
	}

	void onAction(const event::Action& e) override {
		// Enter searches immediately rather than waiting out the debounce.
		if (display && display->host)
			display->host->refreshSearch(true);
	}

	void onSelectKey(const event::SelectKey& e) override {
		if (e.action == GLFW_PRESS && e.key == GLFW_KEY_ESCAPE) {
			APP->event->setSelectedWidget(NULL);
			e.consume(this);
			return;
		}
		if (e.action == GLFW_PRESS && (e.key == GLFW_KEY_DOWN || e.key == GLFW_KEY_UP)) {
			if (display)
				display->moveSelection(e.key == GLFW_KEY_DOWN ? 1 : -1);
			e.consume(this);
			return;
		}
		TextField::onSelectKey(e);
	}
};

// --- BrowserDisplay ---------------------------------------------------------

BrowserDisplay::BrowserDisplay(Rect boxIn, BrowserHost* host) : host(host) {
	box = boxIn;
	statusColor = panel::SAGE;
	// In Rack's module browser there is no Module, so there is no client and no
	// state. The panel still has to look like itself, so the chrome is built
	// either way and only the wiring is skipped.
	preview = (host == NULL);
	if (preview)
		statusText = "Search, audit, import -- without leaving your rack";

	// This display spans the whole panel, so child positions are just the
	// panel coordinates from the generated PanelTheme.hpp.
	// search field
	SearchField* sf = new SearchField;
	sf->display = this;
	sf->box.pos = mm2px(Vec(panel::M, panel::SEARCH_Y));
	sf->box.size = mm2px(Vec(panel::IW, panel::SEARCH_H));
	searchField = sf;
	addChild(sf);

	// category + sort choices
	MenuChoice* cat = new MenuChoice;
	cat->label = "CATEGORY";
	cat->box.pos = mm2px(Vec(panel::M, panel::CHOICE_Y));
	cat->box.size = mm2px(Vec(panel::CHOICE_W, panel::CHOICE_H));
	BrowserDisplay* self = this;
	cat->buildMenu = preview ? std::function<void(ui::Menu*)>() : [self](ui::Menu* menu) {
		menu->addChild(createMenuLabel("Category"));
		for (int i = 0; i < ps::NUM_CATEGORIES; i++) {
			int64_t id = ps::CATEGORIES[i].id;
			menu->addChild(createCheckMenuItem(ps::CATEGORIES[i].name, "",
				[self, id]() { return self->host->state()->query.categoryId == id
				                   && !self->host->state()->query.favorites; },
				[self, id]() {
					ps::BrowseState* s = self->host->state();
					s->query.categoryId = id;
					s->query.favorites = false;
					s->query.page = 1;
					self->host->refreshSearch(true);
				}));
		}
		menu->addChild(new ui::MenuSeparator);
		menu->addChild(createCheckMenuItem("Favourites (offline)", "",
			[self]() { return self->host->state()->query.favorites; },
			[self]() {
				ps::BrowseState* s = self->host->state();
				s->query.favorites = !s->query.favorites;
				s->query.page = 1;
				self->host->refreshSearch(true);
			}));
	};
	addChild(cat);

	MenuChoice* sort = new MenuChoice;
	sort->label = "SORT";
	sort->box.pos = mm2px(Vec(panel::M + panel::CHOICE_W + 4.f, panel::CHOICE_Y));
	sort->box.size = mm2px(Vec(panel::CHOICE_W, panel::CHOICE_H));
	sort->buildMenu = preview ? std::function<void(ui::Menu*)>() : [self](ui::Menu* menu) {
		menu->addChild(createMenuLabel("Sort by"));
		for (int i = 0; i < ps::NUM_SORTS; i++) {
			menu->addChild(createCheckMenuItem(ps::SORTS[i].name, "",
				[self, i]() { return self->host->state()->query.sortIndex == i; },
				[self, i]() {
					ps::BrowseState* s = self->host->state();
					s->query.sortIndex = i;
					s->query.page = 1;
					self->host->refreshSearch(true);
				}));
		}
		menu->addChild(new ui::MenuSeparator);
		menu->addChild(createMenuLabel("Audit against installed modules"));
		for (int i = 0; i < 4; i++) {
			ps::AuditMode m = (ps::AuditMode) i;
			menu->addChild(createCheckMenuItem(ps::AUDIT_MODE_NAMES[i],
				i == 0 ? "" : "downloads each result",
				[self, m]() { return self->host->state()->auditMode == m; },
				[self, m]() { self->setAuditMode(m); }));
		}

		menu->addChild(new ui::MenuSeparator);
		menu->addChild(createMenuLabel("Results per page"));
		int counts[] = {10, 20, 50};
		for (int i = 0; i < 3; i++) {
			int n = counts[i];
			menu->addChild(createCheckMenuItem(string::f("%d", n), "",
				[self, n]() { return self->host->state()->query.perPage == n; },
				[self, n]() {
					ps::BrowseState* s = self->host->state();
					s->query.perPage = n;
					s->query.page = 1;
					self->host->refreshSearch(true);
				}));
		}
	};
	addChild(sort);

	// result list
	scroll = new ClippedScroll;
	scroll->box.pos = mm2px(Vec(panel::M, panel::LIST_Y + panel::LIST_INSET));
	scroll->box.size = mm2px(Vec(panel::IW, panel::ROWS_VISIBLE * panel::ROW_H));
	addChild(scroll);

	// Rows are created once and reused for the life of the module. Refreshing a
	// page only refills and shows/hides them.
	for (int i = 0; i < kMaxRows; i++) {
		PatchRow* row = new PatchRow;
		row->display = this;
		row->slot = i;
		row->index = -1;
		row->box.size.x = scroll->box.size.x - 12.f;   // clear of the scrollbar
		row->box.pos = Vec(0, i * row->box.size.y);
		row->hide();
		rows.push_back(row);
		scroll->container->addChild(row);
	}

	// page navigation
	FlatButton* prev = new FlatButton;
	prev->text = "< PREV";
	prev->box.pos = mm2px(Vec(panel::M, panel::PAGE_Y));
	prev->box.size = mm2px(Vec(panel::NAV_W, panel::PAGE_H));
	if (!preview)
		prev->action = [self]() { self->goToPage(self->host->state()->query.page - 1); };
	addChild(prev);

	FlatButton* next = new FlatButton;
	next->text = "NEXT >";
	next->box.pos = mm2px(Vec(panel::W - panel::M - panel::NAV_W, panel::PAGE_Y));
	next->box.size = mm2px(Vec(panel::NAV_W, panel::PAGE_H));
	if (!preview)
		next->action = [self]() { self->goToPage(self->host->state()->query.page + 1); };
	addChild(next);

	// actions. There is deliberately no "open" button: loading a patch wholesale
	// would clear the rack, taking this module with it. See ps/Apply.hpp.
	const char* names[2] = {"IMPORT INTO RACK", "SAVE TO DISK"};
	ps::Intent intents[2] = {ps::Intent::Import, ps::Intent::SaveToDisk};
	for (int i = 0; i < 2; i++) {
		FlatButton* b = new FlatButton;
		b->text = names[i];
		b->accent = (i == 0);
		b->box.pos = mm2px(Vec(panel::M + i * (panel::BTN_W + 2.f), panel::BTN_Y));
		b->box.size = mm2px(Vec(panel::BTN_W, panel::BTN_H));
		ps::Intent intent = intents[i];
		if (!preview)
			b->action = [self, intent]() { self->host->act(intent); };
		addChild(b);
	}
}

const ps::PatchSummary* BrowserDisplay::selectedPatch() const {
	if (!hasSelection())
		return NULL;
	return &result.patches[selectedIndex];
}

void BrowserDisplay::pullSearchResult() {
	if (preview)
		return;
	result = host->client()->snapshotSearch();
	selectedIndex = result.patches.empty() ? -1 : 0;
	scroll->offset = Vec();
	audits.clear();
	auditRaws.clear();
	auditDone = auditTotal = 0;
	rebuildOrder();
	refreshRows();

	if (host->state()->auditMode != ps::AuditMode::Off && !result.patches.empty())
		host->client()->issueAudit(result.patches);
	else
		host->client()->cancelAudit();
	updateStatus();
}

void BrowserDisplay::setAuditMode(ps::AuditMode mode) {
	if (preview)
		return;
	host->state()->auditMode = mode;
	if (mode == ps::AuditMode::Off) {
		host->client()->cancelAudit();
		audits.clear();
		auditRaws.clear();
		auditDone = auditTotal = 0;
	}
	else if (audits.size() < result.patches.size()) {
		host->client()->issueAudit(result.patches);
	}
	rebuildOrder();
	refreshRows();
	updateStatus();
}

/** Turns one worker's slug list into a verdict. UI thread only: it walks
    plugin::plugins, and consults the manifest map, which is mutex-guarded. */
static ps::AuditInfo resolveOne(const ps::AuditRaw& raw) {
	ps::AuditInfo info;
	info.failed = raw.failed;
	info.note = raw.note;
	info.libraryKnown = ps::vcvlib::ready();

	// Distinct missing plugins, so one delisted plugin used five times counts as
	// five dead-end instances but is classified once -- classifyPlugin is the
	// expensive half.
	std::map<std::string, std::set<std::string> > missingByPlugin;
	std::map<std::string, int> instances;
	for (size_t k = 0; k < raw.slugs.size(); k++) {
		info.total++;
		const std::string& p = raw.slugs[k].first;
		const std::string& m = raw.slugs[k].second;
		if (ps::resolve::moduleResolves(p, m))
			continue;
		info.missing++;
		missingByPlugin[ps::resolve::canonical(p)].insert(m);
		instances[ps::resolve::canonical(p)]++;
	}

	// Only ask the library once it can answer. Until then the count is honest but
	// the dead-end tally stays zero, so nothing is condemned on no evidence.
	if (info.libraryKnown) {
		for (std::map<std::string, std::set<std::string> >::const_iterator it =
		         missingByPlugin.begin(); it != missingByPlugin.end(); ++it) {
			ps::resolve::Verdict v = ps::resolve::classifyPlugin(it->first, it->second);
			if (v.deadEnd)
				info.deadEnds += instances[it->first];
		}
	}
	return info;
}

void BrowserDisplay::pullAudits() {
	if (preview)
		return;
	std::vector<ps::AuditRaw> raws;
	host->client()->drainAudits(raws, auditDone, auditTotal);

	for (size_t i = 0; i < raws.size(); i++) {
		audits[raws[i].patchId] = resolveOne(raws[i]);
		auditRaws[raws[i].patchId] = raws[i];
	}

	if (!raws.empty())
		rebuildOrder();
	refreshRows();
	updateStatus();
}

void BrowserDisplay::reresolveAudits() {
	if (preview || auditRaws.empty())
		return;
	for (std::map<int64_t, ps::AuditRaw>::const_iterator it = auditRaws.begin();
	     it != auditRaws.end(); ++it)
		audits[it->first] = resolveOne(it->second);
	rebuildOrder();
	refreshRows();
	updateStatus();
}

/** Decides which patches are shown and in what order. The API's order is kept
    unless an audit mode says otherwise; an un-audited patch never gets hidden,
    it just sorts after the ones we know are playable. */
void BrowserDisplay::rebuildOrder() {
	if (preview)
		return;
	ps::AuditMode mode = host->state()->auditMode;
	order.clear();

	for (size_t i = 0; i < result.patches.size(); i++) {
		if (mode == ps::AuditMode::OnlyPlayable) {
			std::map<int64_t, ps::AuditInfo>::const_iterator it =
				audits.find(result.patches[i].id);
			if (it != audits.end() && !it->second.playable())
				continue;
		}
		order.push_back((int) i);
	}

	if (mode != ps::AuditMode::PlayableFirst)
		return;

	// Stable partition, so within each group the sort the user actually chose
	// still holds. Four tiers rather than three: a patch that is three installs
	// from running belongs above one that can never run here, and before this
	// they were indistinguishable.
	std::vector<int> playable, partial, hopeless, unknown;
	for (size_t i = 0; i < order.size(); i++) {
		std::map<int64_t, ps::AuditInfo>::const_iterator it =
			audits.find(result.patches[order[i]].id);
		if (it == audits.end())
			unknown.push_back(order[i]);
		else if (it->second.playable())
			playable.push_back(order[i]);
		else if (it->second.unobtainable())
			hopeless.push_back(order[i]);
		else
			partial.push_back(order[i]);
	}
	// Fewest missing modules first -- the closest thing to "nearly playable" --
	// and anything we couldn't read at all (a Rack v1 patch, a missing file)
	// sorts below every patch that at least has a module count.
	BrowserDisplay* self = this;
	std::stable_sort(partial.begin(), partial.end(), [self](int a, int b) {
		const ps::AuditInfo& x = self->audits[self->result.patches[a].id];
		const ps::AuditInfo& y = self->audits[self->result.patches[b].id];
		int kx = x.failed ? (1 << 20) : x.missing;
		int ky = y.failed ? (1 << 20) : y.missing;
		return kx < ky;
	});
	order = playable;
	order.insert(order.end(), partial.begin(), partial.end());
	order.insert(order.end(), hopeless.begin(), hopeless.end());
	order.insert(order.end(), unknown.begin(), unknown.end());
}

void BrowserDisplay::refreshRows() {
	if (preview)
		return;
	ps::BrowseState* st = host->state();
	bool auditing = st->auditMode != ps::AuditMode::Off;
	for (int i = 0; i < kMaxRows; i++) {
		PatchRow* row = rows[i];
		if (i < (int) order.size()) {
			int pi = order[i];
			const ps::PatchSummary& p = result.patches[pi];
			row->index = pi;
			row->setPatch(p, st->isFavorite(p.id));
			std::map<int64_t, ps::AuditInfo>::const_iterator it = audits.find(p.id);
			row->setAudit(it == audits.end() ? NULL : &it->second, auditing);
			row->selected = (pi == selectedIndex);
			row->box.pos = Vec(0, i * row->box.size.y);
			row->show();
		}
		else {
			row->index = -1;
			row->hide();
		}
	}
}

void BrowserDisplay::select(int index) {
	if (index < 0 || index >= (int) result.patches.size())
		return;
	selectedIndex = index;
	for (int i = 0; i < kMaxRows; i++) {
		rows[i]->selected = (rows[i]->index == index && rows[i]->isVisible());
		if (rows[i]->selected)
			scroll->scrollTo(rows[i]->box);
	}
	updateStatus();
}

void BrowserDisplay::moveSelection(int delta) {
	if (order.empty())
		return;
	int slot = 0;
	for (size_t i = 0; i < order.size(); i++)
		if (order[i] == selectedIndex)
			slot = (int) i;
	select(order[clamp(slot + delta, 0, (int) order.size() - 1)]);
}

void BrowserDisplay::toggleFavorite(int index) {
	if (preview)
		return;
	if (index < 0 || index >= (int) result.patches.size())
		return;
	host->state()->toggleFavorite(result.patches[index]);
	for (int i = 0; i < kMaxRows; i++)
		if (rows[i]->index == index)
			rows[i]->favorite = host->state()->isFavorite(result.patches[index].id);
}

void BrowserDisplay::goToPage(int page) {
	if (preview)
		return;
	ps::BrowseState* st = host->state();
	if (page < 1)
		return;
	if (result.lastPageKnown && page > result.knownLastPage)
		return;
	st->query.page = page;
	host->refreshSearch(true);
}

void BrowserDisplay::onSearchTextChanged(const std::string& text) {
	if (preview)
		return;
	ps::BrowseState* st = host->state();
	st->query.search = string::trim(text);
	st->query.page = 1;
	st->query.favorites = false;
	host->refreshSearch(false);
}

void BrowserDisplay::setError(const std::string& message) {
	errorText = message;
}

void BrowserDisplay::clearError() {
	errorText.clear();
}

void BrowserDisplay::updateStatus() {
	if (preview)
		return;
	if (!errorText.empty()) {
		statusText = errorText;
		statusColor = panel::CLAY;
		return;
	}
	statusColor = panel::SAGE;

	if (result.status == ps::ApiStatus::Network) {
		statusText = result.message;
		statusColor = panel::CLAY;
		return;
	}
	if (result.status != ps::ApiStatus::Ok) {
		statusText = result.message;
		statusColor = panel::CLAY;
		return;
	}
	if (result.patches.empty()) {
		statusText = result.query.favorites
			? "No favourites yet -- click a star to add one"
			: "No patches found -- try fewer words or clear the category";
		return;
	}
	if (order.empty()) {
		statusText = auditTotal > 0 && auditDone < auditTotal
			? string::f("Auditing %d of %d...", auditDone, auditTotal)
			: "None of these patches run on your installed modules";
		return;
	}

	std::string cat = "All";
	for (int i = 0; i < ps::NUM_CATEGORIES; i++)
		if (ps::CATEGORIES[i].id == result.query.categoryId)
			cat = ps::CATEGORIES[i].name;
	if (result.query.favorites)
		cat = "Favourites";

	statusText = string::f("%s  |  %s%s", cat.c_str(),
		ps::SORTS[clamp(result.query.sortIndex, 0, ps::NUM_SORTS - 1)].name,
		result.fromCache ? "  |  cached" : "");

	if (host->state()->auditMode != ps::AuditMode::Off) {
		if (auditTotal > 0 && auditDone < auditTotal) {
			statusText += string::f("  |  auditing %d/%d", auditDone, auditTotal);
		}
		else {
			int playable = 0;
			for (size_t i = 0; i < result.patches.size(); i++) {
				std::map<int64_t, ps::AuditInfo>::const_iterator it =
					audits.find(result.patches[i].id);
				if (it != audits.end() && it->second.playable())
					playable++;
			}
			statusText += string::f("  |  %d of %d playable",
				playable, (int) result.patches.size());
		}
	}
}

void BrowserDisplay::draw(const DrawArgs& args) {
	// progress trough fill
	float p = preview ? 0.f : host->client()->downloadProgress;
	if (p > 0.f) {
		Rect r = panel::mmRect(panel::M, panel::PROG_Y, panel::IW, panel::PROG_H);
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, r.pos.x, r.pos.y, r.size.x * clamp(p, 0.f, 1.f), r.size.y,
			r.size.y / 2);
		nvgFillColor(args.vg, panel::MINT);
		nvgFill(args.vg);
	}
	Widget::draw(args);
}

void BrowserDisplay::drawLayer(const DrawArgs& args, int layer) {
	if (layer == 1) {
		// The query block's caption row carries the live status line: on a panel
		// with no knobs, what the block reports IS its caption. It is centred and
		// inked like every other block caption in the family.
		static const panel::TextStyle STATUS(panel::Face::Ui, 6.6f, panel::SAGE,
			NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE, 0.6f);
		static const panel::TextStyle PAGE(panel::Face::Ui, 8.6f, panel::SAGE,
			NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);

		Vec s = panel::mm(panel::W / 2, panel::STATUS_Y);
		nvgScissor(args.vg, panel::mm(panel::M, 0).x, s.y - 12.f,
		           panel::mm(panel::IW, 0).x, 20.f);
		panel::text(args.vg, STATUS.inked(statusColor), s, statusText);
		nvgResetScissor(args.vg);

		// page readout: "page 3 of 7" once we know, "page 3" while we don't.
		// The total page count is unknowable here -- X-WP-Total is a response
		// header, and network::requestJson exposes none -- so say only what we
		// know: the page number, and the total once a short page proves it.
		if (!preview) {
			int page = host->state()->query.page;
			std::string pageText = result.lastPageKnown
				? string::f("page %d of %d", page, std::max(result.knownLastPage, 1))
				: string::f("page %d", page);
			panel::text(args.vg, PAGE,
				mm2px(Vec(panel::W / 2, panel::PAGE_Y + panel::PAGE_H / 2)), pageText);
		}

		// An empty result table reads as a broken panel in the module browser,
		// where there is nothing to put in it.
		if (preview) {
			Vec c = mm2px(Vec(panel::W / 2, panel::LIST_Y + panel::LIST_H / 2 - 3.f));
			panel::text(args.vg,
				panel::TextStyle(panel::Face::Ui, 11.f, panel::alpha(panel::SAGE, 0.75f),
					NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE),
				c, "9,000+ VCV Rack patches");
			panel::text(args.vg,
				panel::TextStyle(panel::Face::Mono, 8.6f, panel::alpha(panel::SAGE, 0.45f),
					NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE),
				Vec(c.x, c.y + mm2px(6.f)), "from patchstorage.com");
		}
	}
	Widget::drawLayer(args, layer);
}

} // namespace ui_pa

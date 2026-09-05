#include "../plugin.hpp"
#include "ps/Apply.hpp"
#include "ps/BrowseState.hpp"
#include "ps/HttpCache.hpp"
#include "ps/Json.hpp"
#include "ps/Installer.hpp"
#include "ps/JobRunner.hpp"
#include "ps/ModuleScan.hpp"
#include "ps/PatchstorageClient.hpp"
#include "ps/VcvLibrary.hpp"
#include "ui/BrowserDisplay.hpp"
#include "ui/Dialogs.hpp"
#include "Panel.hpp"

#include <memory>


struct PatchAudit : Module {
	/** Owned here rather than on the widget so that the module-browser preview,
	    which has no Module at all, does no networking without any special case.
	    The client holds only plain data -- see the invariant in
	    PatchstorageClient.hpp -- so worker threads never see this Module. */
	std::shared_ptr<ps::PatchstorageClient> client;
	ps::BrowseState state;
	bool needsInitialSearch = true;

	// No params, inputs, outputs or lights, and no process() override: every
	// piece of state this module has is UI state, so the engine never sees it.
	PatchAudit() {
		config(0, 0, 0, 0);
		client = std::make_shared<ps::PatchstorageClient>();
	}

	~PatchAudit() override {
		if (client)
			client->abandon();
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "search", json_string(state.query.search.c_str()));
		json_object_set_new(rootJ, "categoryId", json_integer(state.query.categoryId));
		json_object_set_new(rootJ, "sortIndex", json_integer(state.query.sortIndex));
		json_object_set_new(rootJ, "page", json_integer(state.query.page));
		json_object_set_new(rootJ, "perPage", json_integer(state.query.perPage));
		json_object_set_new(rootJ, "favoritesView", json_boolean(state.query.favorites));
		json_object_set_new(rootJ, "auditMode", json_integer((int) state.auditMode));
		json_object_set_new(rootJ, "useCache", json_boolean(state.useCache));

		json_t* favsJ = json_array();
		for (size_t i = 0; i < state.favorites.size(); i++) {
			json_t* f = json_object();
			json_object_set_new(f, "id", json_integer(state.favorites[i].id));
			json_object_set_new(f, "title", json_string(state.favorites[i].title.c_str()));
			json_object_set_new(f, "author", json_string(state.favorites[i].author.c_str()));
			json_array_append_new(favsJ, f);
		}
		json_object_set_new(rootJ, "favorites", favsJ);

		json_t* recentJ = json_array();
		for (size_t i = 0; i < state.recent.size(); i++) {
			json_t* f = json_object();
			json_object_set_new(f, "id", json_integer(state.recent[i].id));
			json_object_set_new(f, "title", json_string(state.recent[i].title.c_str()));
			json_object_set_new(f, "author", json_string(state.recent[i].author.c_str()));
			json_array_append_new(recentJ, f);
		}
		json_object_set_new(rootJ, "recent", recentJ);
		return rootJ;
	}

	/** Restores the view only. Deliberately issues no request: this runs during
	    APP->patch->load, before the widget exists. The first step() picks it up. */
	void dataFromJson(json_t* rootJ) override {
		if (!rootJ)
			return;
		state.query.search = ps::jstr(rootJ, "search");
		state.query.categoryId = ps::jint(rootJ, "categoryId", 0);
		state.query.sortIndex = (int) ps::jint(rootJ, "sortIndex", 2);
		state.query.page = std::max(1, (int) ps::jint(rootJ, "page", 1));
		state.query.perPage = clamp((int) ps::jint(rootJ, "perPage", 20), 1, ui_pa::kMaxRows);
		json_t* favViewJ = json_object_get(rootJ, "favoritesView");
		state.query.favorites = favViewJ && json_is_true(favViewJ);
		state.auditMode = (ps::AuditMode) clamp((int) ps::jint(rootJ, "auditMode", 0), 0, 3);
		json_t* cacheJ = json_object_get(rootJ, "useCache");
		state.useCache = cacheJ ? json_is_true(cacheJ) : true;
		ps::cache::setEnabled(state.useCache);

		state.favorites.clear();
		json_t* favsJ = json_object_get(rootJ, "favorites");
		if (favsJ && json_is_array(favsJ)) {
			size_t i;
			json_t* f;
			json_array_foreach(favsJ, i, f) {
				ps::Favorite fav;
				fav.id = ps::jint(f, "id");
				fav.title = ps::jstr(f, "title");
				fav.author = ps::jstr(f, "author");
				if (fav.id)
					state.favorites.push_back(fav);
			}
		}
		state.recent.clear();
		json_t* recentJ = json_object_get(rootJ, "recent");
		if (recentJ && json_is_array(recentJ)) {
			size_t i;
			json_t* v;
			json_array_foreach(recentJ, i, v) {
				if (!json_is_object(v))
					continue;
				ps::Favorite rec;
				rec.id = ps::jint(v, "id");
				rec.title = ps::jstr(v, "title");
				rec.author = ps::jstr(v, "author");
				if (rec.id)
					state.recent.push_back(rec);
			}
		}
		needsInitialSearch = true;
	}
};


struct PatchAuditWidget : ModuleWidget, ui_pa::BrowserHost {
	PatchAudit* mod = NULL;
	ui_pa::BrowserDisplay* display = NULL;
	/** Guards against re-running the ReadyToApply handler every frame: the dirty
	    flag is consumed once, but a menu can sit open for a while. */
	bool applyPending = false;
	/** The patch the in-flight load was started for. Held here rather than
	    re-read from the selection, which the user may have moved meanwhile. */
	ps::PatchSummary pendingPatch;

	PatchAuditWidget(PatchAudit* module) {
		setModule(module);
		mod = module;
		setPanel(createPanel(asset::plugin(pluginInstance, "res/PatchAudit.svg")));

		// The family screws and the static silkscreen, both from PanelTheme.hpp.
		// The masthead is part of that table, so it is added before the display
		// and drawn under it -- the display never paints over the header band.
		panel::addScrews(this);
		panel::addLabels(this);

		// Built even without a Module, so the module browser shows a panel with
		// its labels on rather than a set of empty boxes. BrowserDisplay runs in
		// preview mode when there is no host.
		display = new ui_pa::BrowserDisplay(Rect(Vec(0, 0), box.size),
			module ? this : NULL);
		addChild(display);
		if (module) {
			// Show the restored search term. Assigned rather than setText()d so
			// it doesn't fire a ChangeEvent and re-arm the debounce.
			display->searchField->text = module->state.query.search;
			display->searchField->cursor = (int) module->state.query.search.size();
			display->searchField->selection = display->searchField->cursor;
		}
	}

	// --- BrowserHost ---------------------------------------------------------

	ps::PatchstorageClient* client() override { return mod->client.get(); }
	ps::BrowseState* state() override { return &mod->state; }

	void refreshSearch(bool immediate) override {
		if (!mod)
			return;
		if (mod->state.query.favorites) {
			showFavorites();
			return;
		}
		if (immediate)
			mod->client->issueSearchNow(mod->state.query);
		else
			mod->client->requestSearch(mod->state.query);
	}

	/** The favourites view is rendered straight from the module's own list, so it
	    works with no network at all. */
	void showFavorites() {
		ps::SearchResult r;
		r.query = mod->state.query;
		r.lastPageKnown = true;
		r.knownLastPage = 1;
		for (size_t i = 0; i < mod->state.favorites.size(); i++) {
			ps::PatchSummary s;
			s.id = mod->state.favorites[i].id;
			s.title = mod->state.favorites[i].title;
			s.author = mod->state.favorites[i].author;
			s.url = string::f("https://patchstorage.com/?p=%lld", (long long) s.id);
			r.patches.push_back(s);
		}
		{
			std::lock_guard<std::mutex> lock(mod->client->mutex);
			mod->client->searchGen++;      // supersede anything in flight
			mod->client->search = r;
		}
		display->pullSearchResult();
	}

	void act(ps::Intent intent) override {
		if (!display || !display->hasSelection())
			return;
		display->clearError();
		pendingPatch = *display->selectedPatch();
		mod->client->issueInstall(pendingPatch, intent);
	}

	void openSelectedOnPatchstorage() override {
		if (!display || !display->hasSelection())
			return;
		const std::string& url = display->selectedPatch()->url;
		if (!url.empty())
			system::openBrowser(url);
	}

	// --- frame loop ----------------------------------------------------------

	void step() override {
		ModuleWidget::step();
		if (!mod || !display)
			return;
		ps::PatchstorageClient& c = *mod->client;

		if (mod->needsInitialSearch) {
			mod->needsInitialSearch = false;
			// Once per session, not once per module instance.
			static bool pruned = false;
			if (!pruned) {
				pruned = true;
				ps::JobRunner::global().submit(ps::Lane::Api, &ps::cache::pruneOnStartup);
			}
			c.issueManifests();
			refreshSearch(true);
		}

		// Cheap enough to poll: it returns immediately unless the last fetch failed
		// and the retry gap has elapsed. Called every frame rather than once at
		// startup so that a browse session that began offline recovers by itself
		// when the network comes back, instead of reporting every plugin in every
		// patch as "not in the library" for the rest of the session.
		ps::vcvlib::ensureLoaded();

		// Keystroke debounce, polled here so the plugin owns no timer thread.
		if (c.searchDeadline > 0.0 && system::getTime() >= c.searchDeadline) {
			c.searchDeadline = 0.0;
			c.issueSearchNow(c.pendingQuery);
		}

		if (c.searchDirty.exchange(false))
			display->pullSearchResult();
		if (c.installDirty.exchange(false))
			onInstallChanged();
		if (c.auditDirty.exchange(false))
			display->pullAudits();
		if (c.importNoteDirty.exchange(false)) {
			std::lock_guard<std::mutex> lock(c.mutex);
			display->statusText = c.importNote;
		}
		// The VCV Library manifests decide whether a missing module is one install
		// away or gone for good, so a page audited before they landed has to be
		// re-judged when they do. This used to poll client->manifestDirty, a flag
		// nothing ever set: the manifests are a process-wide singleton and never
		// had a handle on the client to raise it.
		if (ps::vcvlib::takeDirty())
			display->reresolveAudits();
	}

	void onInstallChanged() {
		// Copy out under the lock, then release it before touching any widget.
		ps::InstallResult r = mod->client->snapshotInstall();

		switch (r.phase) {
			case ps::InstallPhase::FetchingDetail:
				display->setError("");
				display->statusText = "Fetching patch details...";
				display->statusColor = panel::SAGE;
				return;
			case ps::InstallPhase::Downloading:
				display->statusText = string::f("Downloading %s...", r.title.c_str());
				return;
			case ps::InstallPhase::Unarchiving:
				display->statusText = "Unpacking...";
				return;
			case ps::InstallPhase::Parsing:
				display->statusText = "Reading patch...";
				return;

			case ps::InstallPhase::NeedsFileChoice: {
				PatchAuditWidget* self = this;
				ps::PatchSummary patch = pendingPatch;
				ps::Intent intent = r.intent;
				ui_pa::showFileChoiceMenu(r, [self, patch, intent](int64_t fileId) {
					self->mod->client->issueInstall(patch, intent, fileId);
				});
				return;
			}

			case ps::InstallPhase::Failed:
				display->setError(r.message);
				display->updateStatus();
				WARN("PatchAudit: %s", r.message.c_str());
				return;

			case ps::InstallPhase::ReadyToApply:
				if (!applyPending) {
					applyPending = true;
					finishInstall(r);
					applyPending = false;
				}
				return;

			default:
				return;
		}
	}

	void finishInstall(ps::InstallResult r) {
		mod->state.pushRecent(pendingPatch);
		display->clearError();
		display->updateStatus();

		if (r.intent == ps::Intent::SaveToDisk) {
			ps::applySaveToDisk(r.archivePath, r.suggestedFilename);
			display->statusText = "Saved.";
			return;
		}

		ps::ScanResult scan = ps::scanPatchFile(r.patchJsonPath);
		std::string patchJsonPath = r.patchJsonPath;

		// Captures the client (which outlives everything, by the invariant) and
		// a by-value path -- never this widget, which the user could delete while
		// the missing-modules menu sits open.
		std::shared_ptr<ps::PatchstorageClient> client = mod->client;
		std::function<void()> proceed = [client, patchJsonPath]() {
			ps::ImportResult r = ps::applyImport(patchJsonPath);
			std::string note;
			if (!r.ok) {
				note = "Import failed -- see the log.";
			}
			else {
				note = string::f("Imported %d modules, %d cables", r.modules, r.cables);
				if (r.skipped > 0)
					note += string::f(", skipped %d", r.skipped);
				if (r.storage > 0)
					note += string::f(", %d with saved data", r.storage);
				if (r.remapped > 0)
					note += string::f(" -- %d id%s reassigned, some mappings may not carry over",
						r.remapped, r.remapped == 1 ? "" : "s");
			}
			{
				std::lock_guard<std::mutex> lock(client->mutex);
				client->importNote = note;
			}
			client->importNoteDirty = true;
		};

		if (!scan.ok()) {
			ui_pa::showMissingModulesMenu(scan, r, proceed);
			return;
		}
		proceed();
	}

	// --- context menu --------------------------------------------------------

	void appendContextMenu(Menu* menu) override {
		if (!mod)
			return;
		PatchAuditWidget* self = this;

		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("PatchAudit"));

		if (display && display->hasSelection()) {
			const ps::PatchSummary* p = display->selectedPatch();
			menu->addChild(createMenuLabel(p->title));
			if (!p->excerpt.empty())
				menu->addChild(createMenuLabel(string::ellipsize(p->excerpt, 60)));
			menu->addChild(createMenuItem("Open on Patchstorage", "",
				[self]() { self->openSelectedOnPatchstorage(); }));
			menu->addChild(createMenuItem(
				mod->state.isFavorite(p->id) ? "Remove from favourites" : "Add to favourites", "",
				[self]() { self->display->toggleFavorite(self->display->selectedIndex); }));
		}

		if (!mod->state.recent.empty()) {
			menu->addChild(new MenuSeparator);
			menu->addChild(createSubmenuItem("Recently imported", "", [self](Menu* sub) {
				const std::vector<ps::Favorite>& rec = self->mod->state.recent;
				for (size_t i = 0; i < rec.size(); i++) {
					ps::PatchSummary p;
					p.id = rec[i].id;
					p.title = rec[i].title;
					p.author = rec[i].author;
					sub->addChild(createMenuItem(rec[i].title, rec[i].author, [self, p]() {
						self->display->clearError();
						self->pendingPatch = p;
						self->mod->client->issueInstall(p, ps::Intent::Import);
					}));
				}
			}));
		}

		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuItem("Search again", "",
			[self]() { self->refreshSearch(true); }));
		menu->addChild(createMenuItem("Clear filters", "", [self]() {
			ps::BrowseState* s = self->state();
			s->query = ps::SearchQuery();
			if (self->display && self->display->searchField)
				self->display->searchField->text = "";
			self->refreshSearch(true);
		}));

		if (!display->errorText.empty()) {
			menu->addChild(new MenuSeparator);
			menu->addChild(createMenuLabel(display->errorText));
			menu->addChild(createMenuItem("Retry", "", [self]() {
				if (self->display->hasSelection())
					self->act(ps::Intent::Import);
			}));
		}

		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuItem("Use disk cache",
			mod->state.useCache ? "on" : "off", [self]() {
				self->mod->state.useCache = !self->mod->state.useCache;
				ps::cache::setEnabled(self->mod->state.useCache);
			}));
		menu->addChild(createMenuItem("Clear cache",
			string::f("%.1f MB", ps::cache::sizeOnDisk() / (1024.0 * 1024.0)), []() {
				ps::cache::clearAll();
			}));
		menu->addChild(createMenuItem("Open cache folder", "", []() {
			system::createDirectories(ps::cache::root());
			system::openDirectory(ps::cache::root());
		}));
	}
};


Model* modelPatchAudit = createModel<PatchAudit, PatchAuditWidget>("PatchAudit");

#pragma once
#include "Types.hpp"

#include <vector>

namespace ps {

/** Everything the module persists in its patch JSON: the view, not the data.

Results, phases, progress and manifests are all deliberately absent -- reopening
a saved patch should reproduce what you were looking at, not a stale snapshot of
the internet. */
struct BrowseState {
	SearchQuery query;
	AuditMode auditMode = AuditMode::Off;
	bool useCache = true;

	std::vector<Favorite> favorites;
	/** Recently imported, most recent first. Stores the title too, so the menu
	    reads correctly with no network. */
	std::vector<Favorite> recent;

	static const size_t MAX_FAVORITES = 200;
	static const size_t MAX_RECENT = 20;

	bool isFavorite(int64_t id) const {
		for (size_t i = 0; i < favorites.size(); i++)
			if (favorites[i].id == id) return true;
		return false;
	}

	void toggleFavorite(const PatchSummary& p) {
		for (size_t i = 0; i < favorites.size(); i++) {
			if (favorites[i].id == p.id) {
				favorites.erase(favorites.begin() + i);
				return;
			}
		}
		if (favorites.size() >= MAX_FAVORITES)
			favorites.pop_back();
		Favorite f;
		f.id = p.id;
		f.title = p.title;
		f.author = p.author;
		favorites.insert(favorites.begin(), f);
	}

	void pushRecent(const PatchSummary& p) {
		for (size_t i = 0; i < recent.size(); i++) {
			if (recent[i].id == p.id) {
				recent.erase(recent.begin() + i);
				break;
			}
		}
		Favorite f;
		f.id = p.id;
		f.title = p.title;
		f.author = p.author;
		recent.insert(recent.begin(), f);
		if (recent.size() > MAX_RECENT)
			recent.resize(MAX_RECENT);
	}
};

} // namespace ps

#pragma once
// Deliberately free of rack.hpp so the schema match can be exercised offline
// against real saved patch data. See tests/test_labels.cpp -- a match against
// another plugin's format fails silently when that format drifts, and silence
// is not something to find out about by wondering why a menu looks empty.
#include <jansson.h>

#include <cstdint>
#include <string>
#include <unordered_map>


namespace upol {


/** Pull {moduleId, text} pairs out of one module's saved state.

    Matched on shape rather than on who wrote it, and every step is guarded: a
    module that keeps something else under "labels", or that changes its format
    in a later version, yields nothing at all rather than nonsense. Free of Rack
    on purpose -- see tests/, which runs it against real saved patch data,
    because a schema match that silently stops matching is the failure mode this
    would otherwise have. */
inline void readLabelsInto(json_t* rootJ, std::unordered_map<int64_t, std::string>& out) {
	json_t* labelsJ = json_object_get(rootJ, "labels");
	if (!json_is_array(labelsJ))
		return;
	size_t i;
	json_t* entryJ;
	json_array_foreach(labelsJ, i, entryJ) {
		if (!json_is_object(entryJ))
			continue;
		json_t* idJ = json_object_get(entryJ, "moduleId");
		json_t* textJ = json_object_get(entryJ, "text");
		if (!json_is_integer(idJ) || !json_is_string(textJ))
			continue;
		const int64_t mid = (int64_t) json_integer_value(idJ);
		const std::string text = json_string_value(textJ);
		if (text.empty())
			continue;
		// A module can carry several labels -- one per section of its panel.
		// Keep them all, joined, rather than letting the last one win.
		std::string& slot = out[mid];
		if (slot.empty())
			slot = text;
		else if (slot.find(text) == std::string::npos)
			slot += " / " + text;
	}
}

} // namespace upol

// Reading the patch creator's own names for things is a schema match against
// another plugin's saved state, and the failure mode is silence: a format that
// drifts stops matching and nothing says so. So it is checked here against real
// data lifted from a saved patch -- a Subharmonicon recreation whose author
// labelled the front panel with Stoermelder Glue.
#include "../../src/UncertaintyPolicy/LabelParse.hpp"

#include <cstdio>
#include <string>

static int failures = 0;

static void check(bool ok, const std::string& what) {
	if (!ok) {
		failures++;
		std::printf("  FAIL  %s\n", what.c_str());
	}
	else {
		std::printf("  ok    %s\n", what.c_str());
	}
}

int main() {
	// Verbatim from the patch, trimmed to three entries plus the second label
	// on a module that carries two.
	static const char* REAL =
	    "{\"panelTheme\":1,\"defaultSize\":16.0,\"skewLabels\":false,\"labels\":["
	    "{\"moduleId\":1141,\"x\":1.84,\"angle\":0.0,\"width\":57.9,"
	    " \"text\":\"Rhythm 1\",\"color\":\"#ff7455\",\"font\":0},"
	    "{\"moduleId\":1145,\"x\":-0.93,\"text\":\"SEQ-1\",\"font\":0},"
	    "{\"moduleId\":1145,\"x\":63.18,\"text\":\"SEQ-2\",\"font\":0},"
	    "{\"moduleId\":1157,\"text\":\"VCO-1\"}"
	    "]}";

	json_error_t err;
	json_t* rootJ = json_loads(REAL, 0, &err);
	if (!rootJ) {
		std::printf("could not parse the fixture: %s\n", err.text);
		return 1;
	}
	std::unordered_map<int64_t, std::string> out;
	upol::readLabelsInto(rootJ, out);
	json_decref(rootJ);

	check(out.size() == 3, "three modules carry labels");
	check(out[1141] == "Rhythm 1", "a single label reads through");
	// The Subharmonicon's bus router carries one label per half of its panel,
	// and losing either would misname the module in the picker.
	check(out[1145] == "SEQ-1 / SEQ-2", "both labels on one module are kept");
	check(out[1157] == "VCO-1", "an entry with only the required keys works");

	// Shapes that must yield nothing rather than nonsense.
	struct Bad { const char* json; const char* why; };
	static const Bad bad[] = {
		{"{}", "no labels key"},
		{"{\"labels\":42}", "labels is not an array"},
		{"{\"labels\":[1,2,3]}", "entries are not objects"},
		{"{\"labels\":[{\"moduleId\":\"7\",\"text\":\"x\"}]}", "moduleId is a string"},
		{"{\"labels\":[{\"moduleId\":7}]}", "no text"},
		{"{\"labels\":[{\"moduleId\":7,\"text\":\"\"}]}", "empty text"},
	};
	for (const Bad& b : bad) {
		json_t* j = json_loads(b.json, 0, &err);
		std::unordered_map<int64_t, std::string> o;
		if (j) {
			upol::readLabelsInto(j, o);
			json_decref(j);
		}
		check(o.empty(), std::string("ignored: ") + b.why);
	}

	std::printf("\n%s\n", failures ? "FAILURES" : "label parsing behaved");
	return failures ? 1 : 0;
}

#pragma once
#include <rack.hpp>

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>


namespace upol {


/** Module-level connection graph for the current patch.
    Built on the UI thread from the engine's cable list. Patches are small enough
    that plain adjacency lists and breadth-first walks are far cheaper than the
    bookkeeping any smarter structure would need. */
struct Graph {
	std::unordered_set<int64_t> nodes;
	std::unordered_map<int64_t, std::vector<int64_t>> succ;
	std::unordered_map<int64_t, std::vector<int64_t>> pred;

	void addNode(int64_t id) {
		nodes.insert(id);
	}

	void addEdge(int64_t from, int64_t to) {
		addNode(from);
		addNode(to);
		succ[from].push_back(to);
		pred[to].push_back(from);
	}

	/** Every module that can send signal to `sink`, optionally pretending the
	    edge skipFrom -> skipTo has already been cut. Walks predecessors, so one
	    pass answers "what would still be feeding the output". */
	std::unordered_set<int64_t> feeders(int64_t sink, int64_t skipFrom = -1,
	                                    int64_t skipTo = -1) const {
		std::unordered_set<int64_t> seen;
		std::vector<int64_t> stack{sink};
		seen.insert(sink);
		while (!stack.empty()) {
			int64_t cur = stack.back();
			stack.pop_back();
			auto it = pred.find(cur);
			if (it == pred.end())
				continue;
			for (int64_t p : it->second) {
				if (p == skipFrom && cur == skipTo)
					continue;
				if (seen.insert(p).second)
					stack.push_back(p);
			}
		}
		seen.erase(sink);
		return seen;
	}

	/** Everything reachable downstream of `from`, computed in one traversal.
	    Asking "would this new cable close a loop?" for many sources against the
	    same destination is one set lookup once this is in hand, rather than a
	    fresh walk per pair. */
	std::unordered_set<int64_t> descendants(int64_t from) const {
		std::unordered_set<int64_t> seen;
		std::vector<int64_t> stack{from};
		seen.insert(from);
		while (!stack.empty()) {
			int64_t cur = stack.back();
			stack.pop_back();
			auto it = succ.find(cur);
			if (it == succ.end())
				continue;
			for (int64_t n : it->second) {
				if (seen.insert(n).second)
					stack.push_back(n);
			}
		}
		return seen;
	}

	/** Is there a directed path from `from` to `to`? Used to spot the feedback
	    loop a candidate cable would close. */
	bool reaches(int64_t from, int64_t to) const {
		if (from == to)
			return true;
		std::unordered_set<int64_t> seen{from};
		std::vector<int64_t> stack{from};
		while (!stack.empty()) {
			int64_t cur = stack.back();
			stack.pop_back();
			auto it = succ.find(cur);
			if (it == succ.end())
				continue;
			for (int64_t n : it->second) {
				if (n == to)
					return true;
				if (seen.insert(n).second)
					stack.push_back(n);
			}
		}
		return false;
	}
};


} // namespace upol

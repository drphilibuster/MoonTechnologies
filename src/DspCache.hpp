#pragma once

// ---------------------------------------------------------------------------
// Coefficient caching for the per-sample audio path.
//
// A filter or envelope coefficient is usually an expensive function --
// std::exp, std::tan, std::pow, std::cos -- of something that barely moves: a
// knob, or the sample rate. Written inline in process() that cost is paid on
// every sample, so a module with eight voices can spend most of its budget
// recomputing values that did not change.
//
// VCV's own optimization guidance leads with this ("store frequently
// re-evaluated values in variables so they are evaluated just once",
// https://vcvrack.com/manual/DSP). `Cache` is that idea with the staleness
// check made explicit: one float compare per sample instead of a transcendental
// call, and an exact recompute the moment the input does move -- so a knob
// sweep sounds precisely as it did before.
//
// Usage, in a voice or filter struct:
//
//     mt::Cache decayCoeff;
//     ...
//     float r = decayCoeff.get(t60 * fs, [&](float k) {
//         return std::exp(-6.9077553f / k);
//     });
//
// The key must capture *everything* the lambda reads that can vary -- fold the
// sample rate into it, as above, or the coefficient will go stale when the rate
// changes. Where two independent quantities matter, use two caches or key on a
// cheap combination of both.
// ---------------------------------------------------------------------------

#include <cmath>

namespace mt {

/** One cached float, recomputed only when its key changes. */
struct Cache {
	// A key no plausible coefficient argument will collide with, so the first
	// get() always computes. NaN would never compare equal and so would never
	// cache at all.
	float key = -3.4e38f;
	float value = 0.f;

	/** Returns f(k), evaluating f only when `k` differs from the last call. */
	template <typename F>
	inline float get(float k, F f) {
		if (k != key) {
			key = k;
			value = f(k);
		}
		return value;
	}

	/** Drops the cached value, so the next get() recomputes. Call from reset()
	    paths where the coefficient's inputs are restored without going through
	    get() -- cheap insurance, since a stale key would otherwise persist. */
	inline void clear() { key = -3.4e38f; }
};

}  // namespace mt

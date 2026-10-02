// Contagion's modular controls, turned back into the unit's buttons. Self-contained (no Rack)
// so tests/Contagion can drive it against a model of the firmware or the firmware itself.
//
// The Virus has no encoders and no selectors, only buttons: PART -/+ is two keys, FILT 1 is one
// key that steps through LP, HP, BP, BS and shows where it is on four LEDs. The panel here has
// knobs where the unit has those pairs and cycles, and this is what makes a knob into them:
//   * KeyPresser  -- a press of a key, paced the way the 80C515's key scan needs;
//   * Selector    -- a detented knob that presses the cycling key until the unit's own LEDs
//                    agree with it, and follows the unit when a program load changes the answer;
//   * Encoder     -- an endless knob that presses the - or + key once per detent.
#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace vc {

/** Presses, queued from any thread and played out on the audio thread. The firmware's key scan
    registers a press that is down for 30 ms or more, and a release shorter than about 40 ms makes
    the next press read as part of the same one (measured on the real firmware: eight presses at a
    10 or 20 ms gap lose or gain steps, at 40 ms they all land), so a press is HOLD down and GAP up. */
struct KeyPresser {
	static constexpr int KEYS = 35;
	static constexpr int MAX_QUEUED = 16;       // a spun knob must not queue a minute of presses
	static constexpr float HOLD = 0.045f, GAP = 0.045f;   // seconds

	std::atomic<int> want[KEYS];
	int left[KEYS];                             // samples left in the current phase
	bool down[KEYS];

	KeyPresser() {
		for (int i = 0; i < KEYS; i++) { want[i] = 0; left[i] = 0; down[i] = false; }
	}
	/** Ask for n presses of a key. Any thread. */
	void press(int key, int n = 1) {
		if (key < 0 || key >= KEYS || n <= 0) return;
		const int have = want[key].load();
		want[key].fetch_add(std::min(n, std::max(0, MAX_QUEUED - have)));
	}
	/** Presses of the key still to be played or being played. */
	int pending(int key) const { return want[key].load() + (down[key] || left[key] > 0 ? 1 : 0); }
	void clear() {
		for (int i = 0; i < KEYS; i++) { want[i] = 0; left[i] = 0; down[i] = false; }
	}
	/** One sample. True while the key is to be held down. */
	bool process(int key, float sampleRate) {
		if (left[key] > 0) {
			if (--left[key] == 0 && down[key]) { down[key] = false; left[key] = std::max(1, int(GAP * sampleRate)); }
			return down[key];
		}
		if (want[key].load() > 0) {
			want[key].fetch_sub(1);
			down[key] = true;
			left[key] = std::max(1, int(HOLD * sampleRate));
			return true;
		}
		return false;
	}
	/** Seconds a press takes, for a caller waiting on its effect. */
	static float cycle() { return HOLD + GAP; }
};

/** A knob with N positions standing for a unit's selector. The unit is the truth: `obs` is the
    position its LEDs show, and the knob follows it. When the knob is turned the selector presses
    the unit's key until the LEDs agree, then checks, and gives the knob back to the unit if they
    never do. Call tick() at a steady rate. */
struct Selector {
	int steps = 0;
	int stepKey = -1;                           // one key that advances to the next position, wrapping
	const int* directKey = nullptr;             // or a key per position
	bool lastWhenDark = false;                  // no LED lit means the last position (an unlit WAVE LED)

	int shown = -1;                             // what the knob displays
	int target = -1;
	bool dirty = false;
	int attempts = 0;
	int settle = 0;                             // ticks to wait while the presses play out and the LEDs catch up
	int stable = 0, lastObs = -2;

	static constexpr int WAIT_TICKS = 6;        // the LEDs refresh a few times a second

	/** `knob` is the knob's present position (rounded), `obs` the LED position or -1 for none,
	    `tickSeconds` the time between calls. Returns the position the knob should be moved to, or -1
	    to leave it. */
	int tick(int knob, int obs, KeyPresser& keys, float tickSeconds) {
		if (obs < 0 && lastWhenDark) obs = steps - 1;
		if (obs == lastObs) stable++; else { stable = 1; lastObs = obs; }

		if (shown < 0) {                          // first sight of the unit
			if (obs < 0) return -1;
			shown = obs;
			return obs;
		}
		if (knob != shown) {                      // the knob was turned
			target = knob;
			shown = knob;
			dirty = true;
			attempts = 0;
			settle = 0;
		}
		if (settle > 0) { settle--; return -1; }
		if (dirty) {
			if (obs < 0 || stable < 2) return -1;   // wait for a steady reading before pressing
			if (obs == target || attempts >= 2) {
				dirty = false;                      // there, or the unit will not go: it has the last word
			} else {
				attempts++;
				int n;
				if (directKey) { n = 1; keys.press(directKey[target]); }
				else { n = (target - obs + steps) % steps; keys.press(stepKey, n); }
				settle = int(std::ceil(n * KeyPresser::cycle() / tickSeconds)) + WAIT_TICKS;
				return -1;
			}
		}
		if (obs >= 0 && stable >= 3 && obs != shown) {   // the unit changed it (a program load, say)
			shown = obs;
			return obs;
		}
		return -1;
	}
};

/** An endless knob: one press of the - or + key per detent. The knob's value counts detents, a whole
    number, and the knob turns by `STEP` degrees for each, so every click is one visible step. */
struct Encoder {
	static constexpr int DETENTS = 16;          // per turn
	long last = 0;
	bool init = false;

	/** Detents moved since the last call: positive clockwise. */
	int delta(float detents) {
		const long idx = long(std::floor(double(detents) + 0.5));
		if (!init) { last = idx; init = true; return 0; }
		const long d = idx - last;
		last = idx;
		return int(std::max<long>(-KeyPresser::MAX_QUEUED, std::min<long>(KeyPresser::MAX_QUEUED, d)));
	}
};

/** The position of the lit LED among n starting at led0, from a brightness lookup: -1 for none lit
    or more than one. */
template <class F>
int litPosition(F&& brightness, const int* leds, int n) {
	int found = -1;
	for (int i = 0; i < n; i++)
		if (brightness(leds[i]) > 0.5f) {
			if (found >= 0) return -1;
			found = i;
		}
	return found;
}

} // namespace vc

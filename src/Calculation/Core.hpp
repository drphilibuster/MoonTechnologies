#pragma once
// Calculation's counter: one phrase clock, six rows that fall due at multiples
// and divisions of one base length.
//
// Pure logic -- no Rack types, no allocation -- so tests/Calculation compiles it
// on the host. The module feeds it one sample at a time: the leading edges its
// Schmitt triggers saw, and each row's length.
//
// Why it exists. Count Modula's Event Timer (Countdown 3/5) counts the START
// beat only when START lands on the very sample of a clock edge. A START that
// comes from another timer's EOC never does -- every cable in Rack is one sample
// late -- so it misses the edge that caused it and a chained timer runs one
// clock long. Calculation's default ("downbeat") start takes a clock edge seen
// up to `window` samples before START as the phrase's tick 0, which absorbs
// that delay: a row of length L falls due on the clock edge L clocks after the
// downbeat, and anything started by it starts on that same downbeat.

#include <cstdint>

namespace calculation {

static const int ROWS = 6;

// ÷8 ÷4 ÷3 ÷2, then ×1 .. ×32.
static const int DIVISORS = 4;
static const int STEPS = DIVISORS + 32;

inline int stepNum(int step) { return step < DIVISORS ? 1 : step - DIVISORS + 1; }
inline int stepDen(int step) {
	static const int den[DIVISORS] = {8, 4, 3, 2};
	return step < DIVISORS ? den[step] : 1;
}
/** The step index that means ×k. */
inline int timesStep(int k) { return DIVISORS + k - 1; }

/** A row's length in clocks: N × m, half rounded up, never less than one. */
inline int rowLength(int n, int step) {
	if (step < 0) step = 0;
	if (step >= STEPS) step = STEPS - 1;
	int den = stepDen(step);
	long l = ((long)n * stepNum(step) * 2 + den) / (2 * den);
	return l < 1 ? 1 : (int)l;
}

struct Phrase {
	enum Mode { DOWNBEAT, COUNTDOWN };

	// Settings, saved in the patch.
	int mode = DOWNBEAT;
	bool retrigger = false;
	bool repeat[ROWS] = {};
	/** How late a START may be after its clock edge and still claim it. */
	int window = 48;

	// State, saved in the patch.
	bool running = false;
	bool armed = false;      // started; the next clock edge is tick 0
	bool finished = false;   // every one-shot row has fallen due
	long ticks = 0;          // clocks since the downbeat
	int rowTicks[ROWS] = {}; // a repeating row's place in its own cycle
	bool done[ROWS] = {};
	bool gate[ROWS] = {};

	// This sample only.
	bool fired[ROWS] = {};

	// Samples since the last clock edge, saturating; not saved, since a patch
	// that loads has no edge in its past worth claiming.
	int sinceClock = 1 << 30;

	void reset() {
		running = armed = finished = false;
		ticks = 0;
		for (int i = 0; i < ROWS; i++) {
			rowTicks[i] = 0;
			done[i] = gate[i] = false;
		}
	}

	/** One sample. Edges are leading edges; `len` is each row's length now --
	 *  lengths are live, so a row shortened below where the phrase already is
	 *  falls due on the next clock. */
	void process(bool clock, bool start, bool stop, bool rst, const int len[ROWS]) {
		for (int i = 0; i < ROWS; i++) fired[i] = false;
		if (clock) sinceClock = 0;
		else if (sinceClock < (1 << 30)) sinceClock++;

		if (rst) reset();

		// The edge START claimed as its downbeat is tick 0, not tick 1.
		bool claimed = false;
		if (start) {
			bool fresh = !running && !armed && !finished && ticks == 0;
			if (retrigger || fresh) {
				reset();
				if (mode == COUNTDOWN)
					running = true;   // a clock on this same sample is tick 1
				else if (sinceClock <= window) {
					running = true;
					claimed = true;
				}
				else
					armed = true;
			}
			else if (!running && !armed && !finished)
				running = true;       // resuming after STOP: no new downbeat
		}

		if (stop) running = armed = false;

		if (!clock || claimed) return;
		if (armed) {
			armed = false;
			running = true;
			return;
		}
		if (!running) return;

		ticks++;
		bool anyRepeat = false, allDone = true;
		for (int i = 0; i < ROWS; i++) {
			int l = len[i] < 1 ? 1 : len[i];
			if (repeat[i]) {
				anyRepeat = true;
				if (++rowTicks[i] >= l) {
					rowTicks[i] = 0;
					fired[i] = true;
					gate[i] = !gate[i];
				}
			}
			else {
				if (!done[i] && ticks >= l) {
					done[i] = true;
					fired[i] = true;
					gate[i] = true;
				}
				allDone = allDone && done[i];
			}
		}
		if (allDone && !anyRepeat) {
			running = false;
			finished = true;
		}
	}

	/** What a row's counter shows: clocks until it next falls due. */
	long remaining(int i, int l) const {
		if (l < 1) l = 1;
		if (repeat[i]) return l - rowTicks[i];
		if (done[i]) return 0;
		long r = l - ticks;
		return r < 0 ? 0 : r;
	}
};

} // namespace calculation

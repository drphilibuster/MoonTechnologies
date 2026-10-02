// Calculation's phrase counter, tested without Rack.
//
// The case the module exists for is the chain: one row's TRIG patched into
// another Calculation's START, through a cable, which Rack delays by a sample.
// Count Modula's Event Timer runs one clock long in that patch, so every link
// had to be set to N-1. Here it must not drift at all -- every fall-due lands
// on a clock edge that is an exact multiple of N after the first downbeat.
//
// A clock in these tests is a square wave of PERIOD samples; edge k is the
// leading edge at sample k * PERIOD + OFFSET.

#include "../../src/Calculation/Core.hpp"

#include <cstdio>
#include <vector>

using namespace calculation;

static int checks = 0;
static int failures = 0;

#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; \
	printf("  FAIL  %s:%d  ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static const int PERIOD = 100;
static const int OFFSET = 10;

static bool clockEdge(long s) { return s >= OFFSET && (s - OFFSET) % PERIOD == 0; }
/** Which clock edge a sample is, or -1. */
static long edgeIndex(long s) { return clockEdge(s) ? (s - OFFSET) / PERIOD : -1; }

static void lengths(int out[ROWS], int n, const int mult[ROWS]) {
	for (int i = 0; i < ROWS; i++) out[i] = rowLength(n, timesStep(mult[i]));
}

/** Runs one engine with START at `startAt`, returns the clock edges row 0 fired on. */
static std::vector<long> fires(Phrase& e, long startAt, long samples, const int len[ROWS], int row = 0) {
	std::vector<long> at;
	for (long s = 0; s < samples; s++) {
		e.process(clockEdge(s), s == startAt, false, false, len);
		if (e.fired[row]) at.push_back(edgeIndex(s));
	}
	return at;
}

static void testStepTable() {
	CHECK(rowLength(16, timesStep(1)) == 16, "x1");
	CHECK(rowLength(16, timesStep(32)) == 512, "x32");
	CHECK(rowLength(16, 0) == 2, "/8 of 16 = %d", rowLength(16, 0));
	CHECK(rowLength(16, 1) == 4, "/4 of 16");
	CHECK(rowLength(6, 1) == 2, "/4 of 6 rounds 1.5 up, got %d", rowLength(6, 1));
	CHECK(rowLength(16, 2) == 5, "/3 of 16 rounds 5.33 to 5, got %d", rowLength(16, 2));
	CHECK(rowLength(1, 0) == 1, "never below one");
	CHECK(rowLength(3, 0) == 1, "never below one (3/8)");
	CHECK(stepNum(timesStep(5)) == 5 && stepDen(timesStep(5)) == 1, "x5 table");
	CHECK(STEPS == 36, "36 steps");
}

static void testStartTiming() {
	int mult[ROWS] = {1, 2, 3, 4, 5, 6};
	int len[ROWS];
	lengths(len, 4, mult);

	// START on the very sample of edge 2: that edge is the downbeat.
	{
		Phrase e;
		std::vector<long> f = fires(e, OFFSET + 2 * PERIOD, 40 * PERIOD, len);
		CHECK(f.size() == 1 && f[0] == 6, "coincident start: row 1 due on edge 6, got %ld", f.empty() ? -1L : f[0]);
	}
	// One sample after edge 2 (a cable's worth): still edge 2.
	{
		Phrase e;
		std::vector<long> f = fires(e, OFFSET + 2 * PERIOD + 1, 40 * PERIOD, len);
		CHECK(f.size() == 1 && f[0] == 6, "late-by-one start: due on edge 6, got %ld", f.empty() ? -1L : f[0]);
	}
	// Mid-period: waits, and edge 3 is the downbeat.
	{
		Phrase e;
		std::vector<long> f = fires(e, OFFSET + 2 * PERIOD + 50, 40 * PERIOD, len);
		CHECK(f.size() == 1 && f[0] == 7, "mid-period start: due on edge 7, got %ld", f.empty() ? -1L : f[0]);
	}
	// Every row: edge 2 + 4 * m.
	{
		Phrase e;
		long due[ROWS] = {-1, -1, -1, -1, -1, -1};
		for (long s = 0; s < 40 * PERIOD; s++) {
			e.process(clockEdge(s), s == OFFSET + 2 * PERIOD, false, false, len);
			for (int i = 0; i < ROWS; i++)
				if (e.fired[i]) due[i] = edgeIndex(s);
		}
		for (int i = 0; i < ROWS; i++)
			CHECK(due[i] == 2 + 4 * mult[i], "row %d due on edge %ld, want %d", i + 1, due[i], 2 + 4 * mult[i]);
		CHECK(e.finished && !e.running, "stops once every row is due");
		for (int i = 0; i < ROWS; i++)
			CHECK(e.gate[i], "row %d gate latched", i + 1);
	}
}

static void testCountdownMode() {
	int mult[ROWS] = {1, 1, 1, 1, 1, 1};
	int len[ROWS];
	lengths(len, 4, mult);
	// Count Modula: a coincident START counts its own beat -- N-1 periods.
	{
		Phrase e;
		e.mode = Phrase::COUNTDOWN;
		std::vector<long> f = fires(e, OFFSET + 2 * PERIOD, 40 * PERIOD, len);
		CHECK(f.size() == 1 && f[0] == 5, "countdown coincident: edge 5, got %ld", f.empty() ? -1L : f[0]);
	}
	// ...and a START a sample late misses it -- N periods.
	{
		Phrase e;
		e.mode = Phrase::COUNTDOWN;
		std::vector<long> f = fires(e, OFFSET + 2 * PERIOD + 1, 40 * PERIOD, len);
		CHECK(f.size() == 1 && f[0] == 6, "countdown late-by-one: edge 6, got %ld", f.empty() ? -1L : f[0]);
	}
}

/** A drives B through a one-sample cable, B drives A back: a two-section loop. */
static void testChain(int mode, bool expectExact) {
	const int N = 16;
	int mult[ROWS] = {1, 2, 3, 4, 5, 6};
	int len[ROWS];
	lengths(len, N, mult);

	Phrase a, b;
	a.mode = b.mode = mode;
	a.retrigger = b.retrigger = true;
	bool aToB = false, bToA = false;  // cables: what was written last sample
	std::vector<long> due;
	long startAt = OFFSET;            // the first downbeat, from outside
	for (long s = 0; s < 8 * N * PERIOD + 4 * PERIOD; s++) {
		bool clk = clockEdge(s);
		bool aStart = s == startAt || bToA;
		bool bStart = aToB;
		a.process(clk, aStart, false, false, len);
		b.process(clk, bStart, false, false, len);
		aToB = a.fired[0];
		bToA = b.fired[0];
		if (a.fired[0] || b.fired[0]) due.push_back(edgeIndex(s));
	}
	bool exact = due.size() == 8;
	for (size_t k = 0; k < due.size() && k < 8; k++)
		exact = exact && due[k] == (long)(k + 1) * N;
	if (expectExact) {
		CHECK(exact, "downbeat chain: %zu sections, want 8 on edges 16, 32, ...", due.size());
		for (size_t k = 0; k < due.size() && k < 8; k++)
			CHECK(due[k] == (long)(k + 1) * N, "section %zu due on edge %ld, want %ld", k + 1, due[k], (long)(k + 1) * N);
	}
	else {
		// The mismatch this module removes, kept as a control: under Countdown's
		// rules the downbeat START counts its own beat (N-1 periods) while every
		// chained START misses its edge (N periods), so the two never agree.
		CHECK(!exact, "countdown chain should not land on multiples of N");
		CHECK(due.size() >= 2 && due[0] == N - 1 && due[1] - due[0] == N,
		      "countdown chain: first due on edge %ld (want %d), link %ld (want %d)",
		      due.empty() ? -1L : due[0], N - 1, due.size() >= 2 ? due[1] - due[0] : -1L, N);
	}
}

static void testRepeat() {
	int mult[ROWS] = {1, 1, 1, 1, 1, 1};
	int len[ROWS];
	lengths(len, 3, mult);
	Phrase e;
	e.repeat[0] = true;
	std::vector<long> f;
	std::vector<bool> g;
	for (long s = 0; s < 20 * PERIOD; s++) {
		e.process(clockEdge(s), s == OFFSET, false, false, len);
		if (e.fired[0]) { f.push_back(edgeIndex(s)); g.push_back(e.gate[0]); }
	}
	CHECK(f.size() >= 5, "repeat fires every 3, got %zu", f.size());
	for (size_t k = 0; k < f.size(); k++) {
		CHECK(f[k] == (long)(k + 1) * 3, "repeat %zu on edge %ld", k, f[k]);
		CHECK(g[k] == (k % 2 == 0), "repeat gate toggles (%zu)", k);
	}
	CHECK(e.running && !e.finished, "a repeating row keeps it running");
	CHECK(e.remaining(0, 3) >= 1 && e.remaining(0, 3) <= 3, "repeat counter in range");
}

static void testTransport() {
	int mult[ROWS] = {4, 4, 4, 4, 4, 4};
	int len[ROWS];
	lengths(len, 2, mult);  // 8 clocks
	Phrase e;
	long due = -1;
	// Start at edge 0, stop just after edge 3, resume just after edge 10.
	for (long s = 0; s < 40 * PERIOD; s++) {
		bool stop = s == OFFSET + 3 * PERIOD + 5;
		bool start = s == OFFSET || s == OFFSET + 10 * PERIOD + 5;
		e.process(clockEdge(s), start, stop, false, len);
		if (s == OFFSET + 5 * PERIOD + 1)
			CHECK(!e.running && e.ticks == 3, "stopped holds ticks (3), got %ld", e.ticks);
		if (e.fired[0]) due = edgeIndex(s);
	}
	// 3 counted before STOP, 5 more from edge 11: due on edge 15.
	CHECK(due == 15, "resume keeps ticks: due on edge 15, got %ld", due);

	// RESET clears everything; START is then a fresh downbeat.
	e.process(false, false, false, true, len);
	CHECK(!e.running && !e.finished && e.ticks == 0 && !e.gate[0] && !e.done[0], "reset clears");

	// A finished phrase ignores START until RESET, unless RETRIGGER.
	Phrase f;
	int one[ROWS] = {1, 1, 1, 1, 1, 1};
	lengths(len, 1, one);
	for (long s = 0; s < 3 * PERIOD; s++) f.process(clockEdge(s), s == OFFSET, false, false, len);
	CHECK(f.finished, "one-clock phrase finishes");
	f.process(false, true, false, false, len);
	CHECK(f.finished && !f.running && !f.armed, "START after finish ignored");
	f.retrigger = true;
	f.process(false, true, false, false, len);
	CHECK(!f.finished && f.armed, "RETRIGGER restarts a finished phrase");
}

static void testLiveShorten() {
	int len[ROWS] = {10, 10, 10, 10, 10, 10};
	Phrase e;
	long due = -1;
	for (long s = 0; s < 30 * PERIOD; s++) {
		if (s == OFFSET + 6 * PERIOD + 50) len[0] = 4;  // ticks is 6 by now
		e.process(clockEdge(s), s == OFFSET, false, false, len);
		if (e.fired[0]) due = edgeIndex(s);
	}
	CHECK(due == 7, "shortened below ticks: due on the next clock (7), got %ld", due);
}

/** A line's TRIG into its own RESET and START, through a cable: a loop with no gap. */
static void testSelfLoop() {
	int mult[ROWS] = {1, 2, 3, 4, 5, 6};
	int len[ROWS];
	lengths(len, 4, mult);
	Phrase e;
	bool back = false;
	std::vector<long> due;
	for (long s = 0; s < 30 * PERIOD; s++) {
		bool r = back;
		e.process(clockEdge(s), s == OFFSET || r, false, r, len);
		back = e.fired[1];  // line 2, x2 = 8 clocks
		if (e.fired[1]) due.push_back(edgeIndex(s));
	}
	CHECK(due.size() == 3, "self loop: 3 laps in 30 clocks, got %zu", due.size());
	for (size_t k = 0; k < due.size(); k++)
		CHECK(due[k] == (long)(k + 1) * 8, "lap %zu on edge %ld, want %ld", k + 1, due[k], (long)(k + 1) * 8);
}

int main() {
	printf("Calculation core\n");
	testStepTable();
	testStartTiming();
	testCountdownMode();
	testChain(Phrase::DOWNBEAT, true);
	testChain(Phrase::COUNTDOWN, false);
	testRepeat();
	testTransport();
	testLiveShorten();
	testSelfLoop();
	printf("  %d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}

// Repossession -- the memory budget and the window reader.
//
//   make && ./test_windows
//
// Two things are under test and they fail in different ways. The budget is
// arithmetic with a rule that matters musically: eight steps share a fixed pool,
// giving a share up is free and taking it back may not be. The window reader is
// I/O: it must return exactly the span it was asked for, out of a file it never
// holds all of.
#include "../../src/Repossession/Windows.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

static int g_fail = 0;
static int g_checks = 0;
static const char* g_test = "";

#define CHECK(cond, msg) do { \
	g_checks++; \
	if (!(cond)) { \
		g_fail++; \
		std::printf("  FAIL [%s] %s   (%s:%d)\n", g_test, (msg), __FILE__, __LINE__); \
	} \
} while (0)

static void startTest(const char* name) { g_test = name; std::printf("%s\n", name); }

static bool near(double a, double b, double eps = 1e-9) {
	return std::fabs(a - b) <= eps;
}

static std::string g_tmp;


// --- the budget -------------------------------------------------------------

static void testEvenSplit() {
	startTest("a fresh budget splits evenly and holds all of itself");
	rp::Budget b;
	b.reset(80.0);
	CHECK(near(b.pool, 80.0), "pool is what was asked for");
	CHECK(near(b.free, 0.0), "nothing is unallocated");
	for (int i = 0; i < rp::NUM_SLOTS; i++)
		CHECK(near(b.grant[i], 10.0), "each slot has an eighth");
	CHECK(near(b.held(), 80.0), "the grants account for the whole pool");
}

static void testShrinkingReturnsToThePool() {
	startTest("shrinking a step frees seconds for the others");
	rp::Budget b;
	b.reset(80.0);
	double got = b.request(0, 4.0);
	CHECK(near(got, 4.0), "a smaller request always succeeds");
	CHECK(near(b.free, 6.0), "the difference went to the pool");
	CHECK(near(b.held() + b.free, 80.0), "nothing was created or lost");
}

static void testGrowingTakesFromThePool() {
	startTest("growing a step consumes the pool");
	rp::Budget b;
	b.reset(80.0);
	b.request(0, 2.0);                    // frees 8
	double got = b.request(1, 15.0);      // wants 5 more than its 10
	CHECK(near(got, 15.0), "it gets all of it, the pool could cover it");
	CHECK(near(b.free, 3.0), "and the pool is down by exactly that");
	CHECK(near(b.held() + b.free, 80.0), "still conserved");
}

static void testGrowingIsCappedByThePool() {
	startTest("a step cannot grow past what is left");
	rp::Budget b;
	b.reset(80.0);
	double got = b.request(0, 1000.0);
	CHECK(near(got, 10.0), "it keeps its own share and gets no more");
	CHECK(near(b.free, 0.0), "the pool is empty, not negative");
	CHECK(near(b.held() + b.free, 80.0), "still conserved");
}

/** The rule as it was asked for: turning a step off lets the others consume its
    share, and turning it back on gets only the remainder -- which may be none. */
static void testDisableThenReclaim() {
	startTest("a disabled step's share can be taken, and may not come back");
	rp::Budget b;
	b.reset(80.0);

	b.release(3);
	CHECK(near(b.grant[3], 0.0), "the disabled step holds nothing");
	CHECK(near(b.free, 10.0), "its share is in the pool");

	// Another step eats all of it.
	double got = b.request(0, 20.0);
	CHECK(near(got, 20.0), "the neighbour grew into it");
	CHECK(near(b.free, 0.0), "the pool is empty");

	// Now the disabled step wants back in, and there is nothing for it.
	double back = b.reclaim(3);
	CHECK(near(back, 0.0), "it gets nothing back");
	CHECK(near(b.held() + b.free, 80.0), "still conserved");
}

static void testPartialReclaim() {
	startTest("re-arming gets the remainder when there is one");
	rp::Budget b;
	b.reset(80.0);
	b.release(3);                 // 10 into the pool
	b.request(0, 14.0);           // takes 4 of them
	CHECK(near(b.free, 6.0), "six seconds still loose");
	double back = b.reclaim(3);
	CHECK(near(back, 6.0), "the re-armed step gets exactly what was left");
	CHECK(near(b.free, 0.0), "and the pool is empty again");
	CHECK(near(b.held() + b.free, 80.0), "still conserved");
}

static void testReclaimNeverExceedsAnEvenShare() {
	startTest("re-arming never takes more than an even share");
	rp::Budget b;
	b.reset(80.0);
	b.release(1);
	b.release(2);
	b.release(3);
	CHECK(near(b.free, 30.0), "three shares in the pool");
	double back = b.reclaim(1);
	CHECK(near(back, 10.0), "it takes an eighth, not everything going");
	CHECK(near(b.free, 20.0), "the rest is left for the others");
}

static void testRescaleKeepsProportions() {
	startTest("changing the budget setting keeps the shape of what was set up");
	rp::Budget b;
	b.reset(80.0);
	b.request(0, 20.0);
	b.request(1, 5.0);
	double before0 = b.grant[0] / b.pool;
	double before1 = b.grant[1] / b.pool;

	b.rescale(160.0);
	CHECK(near(b.pool, 160.0), "the pool doubled");
	CHECK(near(b.grant[0] / b.pool, before0, 1e-12), "slot 0 kept its share");
	CHECK(near(b.grant[1] / b.pool, before1, 1e-12), "slot 1 kept its share");
	CHECK(near(b.held() + b.free, 160.0), "still conserved");
}

static void testSecondsForIsTheRealArithmetic() {
	startTest("megabytes convert to seconds of stereo float");
	// 48 kHz stereo float32 is 384000 bytes a second.
	double s = rp::Budget::secondsFor(64, 48000);
	CHECK(near(s, 64.0 * 1024.0 * 1024.0 / 384000.0, 1e-9), "64 MB at 48 kHz");
	CHECK(s > 174.0 && s < 175.0, "which is about 174 seconds");
	// Twice the rate buys half the time.
	CHECK(near(rp::Budget::secondsFor(64, 96000), s / 2.0, 1e-9), "rate halves it");
}

/** Whatever the sequence of operations, the pool is neither created nor lost.
    Run against a pseudo-random walk, because the invariant is the thing that
    keeps a long editing session from drifting. */
static void testConservationUnderChurn() {
	startTest("seconds are conserved through arbitrary churn");
	rp::Budget b;
	b.reset(97.5);
	unsigned seed = 12345;
	for (int n = 0; n < 20000; n++) {
		seed = seed * 1103515245u + 12345u;
		int slot = (int) ((seed >> 16) % rp::NUM_SLOTS);
		int op = (int) ((seed >> 8) % 3);
		if (op == 0)
			b.request(slot, (double) ((seed >> 4) % 400) / 10.0);
		else if (op == 1)
			b.release(slot);
		else
			b.reclaim(slot);

		if (!near(b.held() + b.free, 97.5, 1e-6)) {
			CHECK(false, "pool conserved");
			break;
		}
		if (b.free < -1e-9) {
			CHECK(false, "pool never goes negative");
			break;
		}
		bool neg = false;
		for (int i = 0; i < rp::NUM_SLOTS; i++)
			if (b.grant[i] < -1e-9)
				neg = true;
		if (neg) {
			CHECK(false, "no grant goes negative");
			break;
		}
	}
	CHECK(near(b.held() + b.free, 97.5, 1e-6), "conserved after 20000 operations");
	CHECK(b.free >= -1e-9, "pool non-negative");
}


// --- the window reader ------------------------------------------------------

/** A .pcm whose every frame says which frame it is, so a window can be checked
    to the sample rather than merely to the byte count. */
static std::string writeRamp(const std::string& name, int64_t frames) {
	std::string path = g_tmp + "/" + name;
	FILE* f = std::fopen(path.c_str(), "wb");
	if (!f) {
		std::printf("  cannot write %s\n", path.c_str());
		std::exit(2);
	}
	std::vector<float> buf(2);
	for (int64_t i = 0; i < frames; i++) {
		buf[0] = (float) i;
		buf[1] = (float) -i;
		std::fwrite(&buf[0], sizeof(float), 2, f);
	}
	std::fclose(f);
	return path;
}

/** Drives the loader and waits for the answer. */
static std::shared_ptr<rp::Window> fetch(rp::WindowLoader& L, int slot,
                                         const std::string& path,
                                         int64_t start, int64_t frames) {
	L.request(slot, path, "ramp", start, frames,
		(float) start, (float) (start + frames));
	for (int i = 0; i < 2000; i++) {
		std::shared_ptr<rp::Window> w = L.take(slot);
		if (w)
			return w;
		struct timespec ts;
		ts.tv_sec = 0;
		ts.tv_nsec = 1000000;
		nanosleep(&ts, NULL);
	}
	return std::shared_ptr<rp::Window>();
}

static void testWindowIsExactlyTheSpanAsked() {
	startTest("a window holds exactly the frames it was asked for");
	std::string path = writeRamp("ramp.pcm", 100000);
	rp::WindowLoader L;

	std::shared_ptr<rp::Window> w = fetch(L, 0, path, 40000, 5000);
	CHECK(w != NULL, "the loader answered");
	if (!w)
		return;
	CHECK(w->frames == 5000, "the right number of frames");
	CHECK(w->startFrame == 40000, "starting where it was told");
	CHECK(w->pcm.size() == 10000, "stereo, so twice as many floats");
	CHECK(w->pcm[0] == 40000.f, "the first frame is the one at that offset");
	CHECK(w->pcm[1] == -40000.f, "and its right channel matches");
	CHECK(w->pcm[9998] == 44999.f, "the last frame is the one before the end");
	CHECK(w->holds(40000) && w->holds(44999), "holds() agrees at both ends");
	CHECK(!w->holds(39999) && !w->holds(45000), "and excludes either side");
}

static void testWindowClampsAtEndOfFile() {
	startTest("a window asked past the end returns what is there");
	std::string path = writeRamp("short.pcm", 1000);
	rp::WindowLoader L;
	std::shared_ptr<rp::Window> w = fetch(L, 1, path, 900, 5000);
	CHECK(w != NULL, "the loader answered");
	if (!w)
		return;
	CHECK(w->frames == 100, "only the hundred frames that exist");
	CHECK(w->pcm[0] == 900.f, "starting in the right place");
	CHECK(w->bytes() == 800, "and reports its real size");
}

static void testMissingFileIsEmptyNotFatal() {
	startTest("a window from a file that is gone is empty, not a crash");
	rp::WindowLoader L;
	std::shared_ptr<rp::Window> w = fetch(L, 2, g_tmp + "/nope.pcm", 0, 1000);
	CHECK(w != NULL, "the loader still answered");
	if (!w)
		return;
	CHECK(w->frames == 0, "with nothing in it");
	CHECK(w->pcm.empty(), "and no buffer");
}

/** Dragging an edge queues a request every frame; only the last one matters.
    The loader must not fall behind, and must end on the newest span. */
static void testNewestRequestWins() {
	startTest("a burst of requests settles on the newest");
	std::string path = writeRamp("drag.pcm", 200000);
	rp::WindowLoader L;
	for (int i = 0; i < 200; i++) {
		L.request(3, path, "ramp", 1000 + i * 10, 2000,
			(float) i, (float) i + 1.f);
	}
	// Whatever it lands on, it must be a real window, and once it goes quiet the
	// answer must be the last span asked for.
	std::shared_ptr<rp::Window> last;
	for (int i = 0; i < 3000; i++) {
		std::shared_ptr<rp::Window> w = L.take(3);
		if (w)
			last = w;
		if (last && !L.busy())
			break;
		struct timespec ts;
		ts.tv_sec = 0;
		ts.tv_nsec = 1000000;
		nanosleep(&ts, NULL);
	}
	CHECK(last != NULL, "something arrived");
	if (!last)
		return;
	CHECK(last->startFrame == 1000 + 199 * 10, "it is the newest span");
	CHECK(last->frames == 2000, "and it is complete");
	CHECK(last->pcm[0] == (float) (1000 + 199 * 10), "with the right audio");
}

static void testLoaderShutsDownCleanly() {
	startTest("the loader joins even with work outstanding");
	std::string path = writeRamp("big.pcm", 400000);
	{
		rp::WindowLoader L;
		for (int i = 0; i < 8; i++)
			L.request(i, path, "ramp", i * 1000, 300000, 0.f, 1.f);
		// Destructor runs here with every slot queued. If it did not wake the
		// worker, this test would hang rather than fail -- which is itself the
		// signal, and why it is the last one.
	}
	CHECK(true, "returned from the destructor");
}


int main() {
	char tmpl[] = "/tmp/repossession-win-XXXXXX";
	const char* d = ::mkdtemp(tmpl);
	if (!d) {
		std::printf("cannot make a scratch directory\n");
		return 2;
	}
	g_tmp = d;
	std::printf("Repossession -- budget and windows\nscratch: %s\n\n", g_tmp.c_str());

	testEvenSplit();
	testShrinkingReturnsToThePool();
	testGrowingTakesFromThePool();
	testGrowingIsCappedByThePool();
	testDisableThenReclaim();
	testPartialReclaim();
	testReclaimNeverExceedsAnEvenShare();
	testRescaleKeepsProportions();
	testSecondsForIsTheRealArithmetic();
	testConservationUnderChurn();

	testWindowIsExactlyTheSpanAsked();
	testWindowClampsAtEndOfFile();
	testMissingFileIsEmptyNotFatal();
	testNewestRequestWins();
	testLoaderShutsDownCleanly();

	std::printf("\n%d checks, %d failures\n", g_checks, g_fail);
	return g_fail ? 1 : 0;
}

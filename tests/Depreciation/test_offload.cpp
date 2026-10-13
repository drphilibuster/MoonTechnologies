// Depreciation: the thread offload (src/Pcm70Offload.hpp) on its own: the ring pair keeps order and an exact latency, the latency can be changed live without losing
// or repeating a sample, an engine burst longer than the latency is an underrun that is counted (and not a crash), and the lock-free mailboxes do not tear.
#include "../../src/Pcm70Offload.hpp"
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <thread>

using namespace pcm70;
static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static void burstSleep(int us) { std::this_thread::sleep_for(std::chrono::microseconds(us)); }

// The audio thread of an engine: `block` calls back to back, then a wait for the next block.
static uint64_t drive(Offload& o, int blocks, int block, int64_t& n, std::vector<double>& got, int waitUs) {
	for (int b = 0; b < blocks; b++) {
		for (int i = 0; i < block; i++, n++) { double out[4]; o.exchange((double)(n + 1), out); got.push_back(out[0]); }
		burstSleep(waitUs);
	}
	return o.underruns();
}

static void testLatency() {
	Offload o; int64_t n = 0; std::vector<double> got;
	o.start([](double in, int64_t idx, double out[4]) { out[0] = in; out[1] = (double)idx; out[2] = -in; out[3] = 0.0; }, 300);
	const uint64_t u = drive(o, 40, 256, n, got, 400);
	// output n is the answer to input n - 300; the 300 zeros come first
	int wrong = 0; for (size_t i = 0; i < got.size(); i++) { const double want = i < 300 ? 0.0 : (double)(i - 300 + 1); if (got[i] != want) wrong++; }
	std::printf("latency 300, blocks of 256: %zu samples, %d wrong, %" PRIu64 " underruns\n", got.size(), wrong, u);
	CHECK(wrong == 0); CHECK(u == 0); CHECK(o.ready() >= 290 && o.ready() <= 300 + 256);
	o.stop();
}

static void testShortLatency() {
	Offload o; int64_t n = 0; std::vector<double> got;
	// a latency under the burst: the worker cannot have answered the first inputs of a burst by the time they are asked for
	o.start([](double in, int64_t, double out[4]) { for (int c = 0; c < 4; c++) out[c] = in; burstSleep(1); }, 16);
	const uint64_t u = drive(o, 20, 512, n, got, 600);
	std::printf("latency 16, blocks of 512, a slow step: %" PRIu64 " underruns (expected many), no crash\n", u);
	CHECK(u > 0);
	o.stop();
}

static void testChange() {
	Offload o; int64_t n = 0; std::vector<double> got;
	o.start([](double in, int64_t, double out[4]) { out[0] = in; out[1] = out[2] = out[3] = 0.0; }, 200);
	drive(o, 20, 64, n, got, 300);
	const size_t a = got.size();
	o.setLatency(500);                                    // 300 zeros inserted
	drive(o, 20, 64, n, got, 300);
	const size_t b = got.size();
	o.setLatency(100);                                    // 400 of the next inputs dropped
	drive(o, 40, 64, n, got, 300);
	// walk the output: it must be strictly the inputs in order, with the zeros where they went in and a jump where the dropped inputs were
	int zerosRun = 0, ordered = 1; double prev = 0.0; size_t jumps = 0; double jumpTotal = 0;
	for (size_t i = 200; i < got.size(); i++) {
		const double v = got[i];
		if (v == 0.0) { zerosRun++; continue; }
		if (prev != 0.0 && v != prev + 1.0) { jumps++; jumpTotal += v - prev - 1.0; if (v < prev) ordered = 0; }
		prev = v;
	}
	std::printf("latency 200 -> 500 -> 100: %zu + %zu + %zu samples, %d inserted zeros, %zu jump(s) dropping %g inputs, %" PRIu64 " underruns\n", a, b - a, got.size() - b, zerosRun, jumps, jumpTotal, o.underruns());
	CHECK(zerosRun == 300); CHECK(jumps == 1); CHECK(jumpTotal == 400.0); CHECK(ordered); CHECK(o.underruns() == 0);
	o.stop();
}

static void testRestart() {
	Offload o;
	for (int k = 0; k < 50; k++) {
		o.start([](double in, int64_t, double out[4]) { out[0] = in; out[1] = out[2] = out[3] = 0.0; }, 64 + k);
		double out[4]; for (int i = 0; i < 100; i++) o.exchange(1.0, out);
	}
	o.stop(); o.stop();
	CHECK(!o.running());
	std::printf("50 start/stop cycles, no hang\n");
}

struct Pair { int a; int b; int pad[30]; };
static void testLatest() {
	Latest<Pair> mail; std::atomic<bool> go{true}; std::atomic<uint64_t> torn{0}, seen{0};
	std::thread reader([&]() {
		int last = -1;
		while (go.load()) { const Pair& p = mail.read(); if (p.a != p.b) torn++; if (p.a < last) torn++; last = p.a; seen++; }
	});
	for (int i = 0; i < 2000000; i++) { Pair& w = mail.writing(); w.a = i; w.b = i; mail.publish(); }
	go = false; reader.join();
	std::printf("Latest: 2000000 publishes, %" PRIu64 " reads, %" PRIu64 " torn or out of order\n", seen.load(), torn.load());
	CHECK(torn == 0);
}

static void testSpsc() {
	static Spsc<uint32_t, 256> q; std::atomic<uint64_t> bad{0};
	std::thread consumer([&]() { uint32_t expect = 0, v; while (expect < 1000000) { if (q.pop(v)) { if (v != expect) bad++; expect++; } } });
	for (uint32_t i = 0; i < 1000000; ) if (q.push(i)) i++;
	consumer.join();
	std::printf("Spsc: 1000000 items, %" PRIu64 " out of order\n", bad.load());
	CHECK(bad == 0);
}

int main() {
	testLatency(); testShortLatency(); testChange(); testRestart(); testLatest(); testSpsc();
	if (failures) { std::printf("%d FAILED\n", failures); return 1; }
	std::printf("ok\n");
	return 0;
}

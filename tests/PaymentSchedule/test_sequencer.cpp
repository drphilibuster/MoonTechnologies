// Payment Schedule's chips and the way the module wires them: a 74HC4017 counter
// (Baby8) and a CD4031B tap loop, both from their datasheets, on one clock.

#include "../../src/PaymentSchedule/Sequencer.hpp"

#include <cstdio>
#include <vector>

using namespace paysched;

static int checks = 0, failures = 0;
static void check(const char* what, bool ok, const char* detail = 0) {
	checks++;
	if (!ok) {
		failures++;
		printf("  FAIL  %s%s%s\n", what, detail ? ": " : "", detail ? detail : "");
	}
}

/** One full clock cycle, high then low, through process(). Returns EOC. */
static bool tick(Sequencer& s, bool up = true, int n = 8, bool reset = false,
                 bool tap = false, bool clear = false) {
	bool w = s.process(true, reset, up, n, tap, clear);
	bool w2 = s.process(false, reset, up, n, false, clear);
	return w || w2;
}

static void test4017() {
	printf("74HC4017: Johnson decade counter\n");
	cd4017::Cd4017 c;
	bool oneHot = true, order = true;
	for (int i = 0; i < 25; i++) {
		int hot = 0;
		for (int n = 0; n < 10; n++) hot += c.q(n);
		if (hot != 1) oneHot = false;
		if (c.index() != i % 10) order = false;
		c.advance();
	}
	check("exactly one output high in every state", oneHot);
	check("Q0 to Q9 in order, and round again after ten", order);
	bool carry = true;
	cd4017::Cd4017 d;
	for (int i = 0; i < 10; i++) {
		if (d.carry() != (i < 5)) carry = false;
		d.advance();
	}
	check("Q5-9 is low in states 5 to 9 and high in 0 to 4", carry);

	// The function table.
	cd4017::Cd4017 e;
	using A = cd4017::Cd4017;
	check("CP0 rising with CP1 low advances", e.sense(true, false, false) == A::ADVANCE);
	check("CP0 falling does nothing", e.sense(false, false, false) == A::NONE);
	check("CP0 rising with CP1 high does nothing", e.sense(true, true, false) == A::NONE);
	cd4017::Cd4017 x;
	x.sense(true, false, false);                         // CP0 rises: advances
	check("CP1 rising while CP0 is high does nothing", x.sense(true, true, false) == A::NONE);
	cd4017::Cd4017 f;
	f.sense(true, true, false);   // CP0 high, CP1 high
	check("CP1 falling while CP0 is high advances", f.sense(true, false, false) == A::ADVANCE);
	cd4017::Cd4017 g;
	check("MR high is a reset whatever the clock does", g.sense(true, false, true) == A::RESET);
	check("MR overrides a rising CP0", g.sense(false, false, true) == A::RESET);
	// setIndex agrees with advance.
	bool agree = true;
	cd4017::Cd4017 h, k;
	for (int i = 0; i < 10; i++) {
		k.setIndex(i);
		for (int j = 0; j < 5; j++) if (h.ff[j] != k.ff[j]) agree = false;
		h.advance();
	}
	check("setIndex(n) is the state advance() reaches after n clocks", agree);
}

static void test4031() {
	printf("CD4031B: 64-stage shift register\n");
	cd4031::Cd4031 c;
	// A single 1 in, then zeros: Q rises on exactly the 64th clock after it entered.
	c.clockRising(true, false);
	int at = -1;
	for (int i = 1; i <= 70; i++) {
		c.clockFalling();
		c.clockRising(false, false);
		if (c.q() && at < 0) at = i;
	}
	char d[48];
	snprintf(d, sizeof d, "appeared after %d more clocks", at);
	check("a bit reaches Q 64 positive-going edges after it enters", at == 63, d);

	cd4031::Cd4031 m;
	m.modeControl = false;
	m.clockRising(true, false);
	check("MODE low takes DATA IN 1", m.stages & 1u);
	cd4031::Cd4031 n;
	n.modeControl = true;
	n.clockRising(true, false);
	check("MODE high ignores DATA IN 1", !(n.stages & 1u));
	n.clockRising(false, true);
	check("MODE high takes RECIRCULATE", n.stages & 1u);

	// The 1/2 stage: Q a half clock later.
	cd4031::Cd4031 q;
	q.stages = (uint64_t) 1 << 63;
	check("Q' has not taken Q yet", !q.qHalf());
	q.clockFalling();
	check("Q' takes Q on the negative-going edge", q.qHalf());
	check("Q-bar is the inverse of Q", q.qBar() == !q.q());

	// Edges: only the positive-going one shifts.
	cd4031::Cd4031 e;
	check("a rising level shifts", e.setClock(true, true, false));
	check("a held level does not", !e.setClock(true, true, false));
	check("a falling level does not", !e.setClock(false, true, false));
	check("the next rise shifts again", e.setClock(true, true, false));
	// Static: no clock, no change.
	uint64_t before = e.stages;
	for (int i = 0; i < 1000; i++) e.setClock(e.clock, true, false);
	check("it holds with the clock high or low", e.stages == before);
}

static void testLoop() {
	printf("Tap loop: the 4031 wired as the recorder\n");
	Sequencer s;
	// Record one hit at clock 5.
	for (int i = 0; i < 5; i++) tick(s);
	tick(s, true, 8, false, true);            // tap high as the clock rises
	// It returns every 64 clocks and goes on doing so, with nothing refreshing it.
	std::vector<int> seen;
	for (int i = 1; i <= 300; i++) {
		tick(s);
		if (s.loopGate()) seen.push_back(i);
	}
	bool periodic = seen.size() >= 4;
	for (size_t i = 1; i < seen.size(); i++) if (seen[i] - seen[i - 1] != 64) periodic = false;
	char d[64];
	snprintf(d, sizeof d, "%zu sightings", seen.size());
	check("a recorded hit recirculates every 64 clocks, indefinitely", periodic, d);

	// A tap that is not high when the clock rises is never recorded.
	Sequencer m;
	m.process(false, false, true, 8, true, false);    // tap high, clock low
	m.process(false, false, true, 8, false, false);   // gone before the edge
	m.process(true, false, true, 8, false, false);
	int hits = 0;
	for (int i = 0; i < 200; i++) { tick(m); hits += m.loopGate(); }
	check("a tap that has gone before the clock rises is not recorded", hits == 0);

	// Overdub: a second tap adds to the first, and the first is kept.
	Sequencer o;
	tick(o, true, 8, false, true);
	for (int i = 0; i < 9; i++) tick(o);
	tick(o, true, 8, false, true);
	std::vector<int> two;
	for (int i = 1; i <= 64; i++) { tick(o); if (o.loopGate()) two.push_back(i); }
	check("a second tap is ORed in and the first survives", two.size() == 2 && two[1] - two[0] == 10);

	// Clear: holding it stops Q being fed back, so the pattern drains over 64 clocks.
	Sequencer c;
	for (int i = 0; i < 3; i++) { tick(c, true, 8, false, true); for (int j = 0; j < 6; j++) tick(c); }
	int before = 0;
	for (int i = 0; i < 64; i++) { tick(c); before += c.loopGate(); }
	check("a pattern is there before the clear", before >= 3);
	int during = 0;
	for (int i = 0; i < 64; i++) { tick(c, true, 8, false, false, true); during += c.loopGate(); }
	check("held for 64 clocks, the old pattern still comes out once as it drains", during >= 1);
	int after = 0;
	for (int i = 0; i < 200; i++) { tick(c); after += c.loopGate(); }
	check("and then it is gone", after == 0);

	// A clear released early leaves what has not yet reached Q.
	Sequencer p;
	tick(p, true, 8, false, true);                       // a hit
	for (int i = 0; i < 30; i++) tick(p);
	tick(p, true, 8, false, true);                       // a second hit, 31 clocks later
	for (int i = 0; i < 20; i++) tick(p, true, 8, false, false, true);   // clear for 20 clocks
	int back = 0;
	for (int i = 0; i < 200; i++) { tick(p); back += p.loopGate(); }
	check("a clear that does not last 64 clocks leaves part of the pattern", back >= 1);
}

static void testCounter() {
	printf("Counter: STEPS, RESET, direction, RUN\n");
	// Against the obvious oracle, for every STEPS value, up and down.
	for (int n = 1; n <= 8; n++) {
		for (int dir = 0; dir < 2; dir++) {
			bool up = dir == 0;
			Sequencer s;
			int step = 0;
			bool ok = true;
			for (int i = 0; i < 100; i++) {
				bool wrapped = tick(s, up, n);
				int prev = step;
				step = up ? (step + 1) % n : (step - 1 + n) % n;
				bool wantWrap = up ? (prev == n - 1) : (prev == 0);
				if (s.step() != step || wrapped != wantWrap) ok = false;
			}
			char d[48];
			snprintf(d, sizeof d, "STEPS %d, %s", n, up ? "up" : "down");
			check("the count and EOC match a modular counter", ok, d);
		}
	}
	// RESET is a level: it holds the counter at 0 and the clock is ignored.
	Sequencer r;
	for (int i = 0; i < 3; i++) tick(r);
	check("the count is 3", r.step() == 3);
	for (int i = 0; i < 5; i++) tick(r, true, 8, true);
	check("RESET held keeps the counter at 0 through five clocks", r.step() == 0);
	tick(r);
	check("and the first clock after it is released goes to 1", r.step() == 1);
	// STEPS turned down past the count.
	Sequencer t;
	for (int i = 0; i < 6; i++) tick(t);
	t.process(false, false, true, 4, false, false);
	check("STEPS turned below the count sends it home", t.step() == 0);
	// RUN gates the clock: a stopped sequencer holds the counter and the loop.
	Sequencer u;
	tick(u, true, 8, false, true);
	u.running = false;
	uint64_t held = u.loop.stages;
	for (int i = 0; i < 10; i++) tick(u);
	check("stopped, neither the counter nor the loop moves", u.step() == 1 && u.loop.stages == held);
	u.running = true;
	tick(u);
	check("running again, both do", u.step() == 2 && u.loop.stages != held);
	// The two chips are independent: RESET leaves the loop alone.
	Sequencer v;
	tick(v, true, 8, false, true);
	for (int i = 0; i < 5; i++) tick(v);
	uint64_t keep = v.loop.stages;
	v.process(false, true, true, 8, false, false);
	check("RESET restarts the steps and not the loop", v.step() == 0 && v.loop.stages == keep);
	// Direction does not touch the loop's shift direction: it always goes forward.
	Sequencer w;
	tick(w, true, 8, false, true);
	for (int i = 0; i < 10; i++) tick(w, false, 8);
	int at = -1;
	for (int i = 0; i < 80 && at < 0; i++) { tick(w, false, 8); if (w.loopGate()) at = i; }
	check("the loop runs forward whichever way the counter does", at >= 0);
}

static void testCycle() {
	printf("Counter: one-shot cycle\n");
	Sequencer s;
	for (int i = 0; i < 3; i++) tick(s);
	s.startCycle();
	check("CYCLE starts from step 1, running", s.step() == 0 && s.running);
	int clocks = 0;
	bool wrapped = false;
	while (s.running && clocks < 40) { wrapped = tick(s, true, 4); clocks++; }
	check("it stops itself at the wrap, after STEPS clocks", !s.running && wrapped && clocks == 4);
	for (int i = 0; i < 10; i++) tick(s, true, 4);
	check("and stays stopped", s.step() == 0 && !s.running);
}

static void testLegacyImport() {
	printf("Patches: the old eight-slot loop\n");
	bool slots[8] = { true, false, false, true, false, false, false, false };
	Sequencer s;
	s.counter.setIndex(0);
	s.importSlots(slots, 8, 0);
	// The old module gated step s with slot s; the register must do the same.
	bool ok = true;
	for (int i = 0; i < 40; i++) {
		if (s.loopGate() != slots[s.step()]) ok = false;
		tick(s, true, 8);
	}
	check("gates land on the steps they were saved on", ok);
}

int main() {
	test4017();
	test4031();
	testLoop();
	testCounter();
	testCycle();
	testLegacyImport();
	printf("\n%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}

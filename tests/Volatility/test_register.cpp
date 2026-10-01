// Volatility's shift register: one CD4006B, wired as the datasheet lets it be.
//
// The claim worth testing is the awkward one. The textbook two-tap solution for
// an 18-stage register is stages 18 and 11, and a 4006 has no pin on stage 11, so
// the module cannot honestly use it. What it uses instead has to be (a) tapped
// only where the chip has pins, (b) a maximal-length sequence of the length the
// docs say, and (c) the same thing a plain shift register would do -- the chip
// model must add the pins and the sections without changing the arithmetic.

#include "../../src/Volatility/Noise.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <set>
#include <vector>

using namespace volatility;

static int checks = 0, failures = 0;
static void check(const char* what, bool ok, const char* detail = 0) {
	checks++;
	if (!ok) {
		failures++;
		printf("  FAIL  %s%s%s\n", what, detail ? ": " : "", detail ? detail : "");
	}
}

static void testSections() {
	printf("CD4006: four sections, taps where the datasheet puts them\n");
	cd4006::Cd4006 c;
	// A single 1 into each section's input, then zeros: it must reach each pin
	// after exactly the number of clocks that pin's stage number says.
	c.clockFalling(true, true, true, true);
	for (int n = 1; n <= 6; n++) {
		bool p13 = c.pin13(), p11 = c.pin11(), p12 = c.pin12(), p10 = c.pin10(),
		     p8 = c.pin8(), p9 = c.pin9();
		char d[96];
		snprintf(d, sizeof d, "after %d clocks: 13=%d 11=%d 12=%d 10=%d 8=%d 9=%d", n,
		         p13, p11, p12, p10, p8, p9);
		check("pins 13, 11, 10, 8 are stage 4 and 12, 9 are stage 5",
		      p13 == (n == 4) && p11 == (n == 4) && p10 == (n == 4) && p8 == (n == 4)
		      && p12 == (n == 5) && p9 == (n == 5), d);
		c.clockFalling(false, false, false, false);
	}
	// Sections are independent: a 1 in section 3 alone does not appear elsewhere.
	cd4006::Cd4006 d;
	d.clockFalling(false, false, true, false);
	for (int i = 0; i < 3; i++) d.clockFalling(false, false, false, false);
	check("section 3 alone reaches only pin 10",
	      d.pin10() && !d.pin13() && !d.pin11() && !d.pin12() && !d.pin8() && !d.pin9());
	check("the sections hold 4+5+4+5 stages = 18",
	      cd4006::kSectionStages[0] + cd4006::kSectionStages[1] + cd4006::kSectionStages[2]
	      + cd4006::kSectionStages[3] == 18);
}

/** The plain register the chip-wired one must equal: 18 stages, new bit into stage
    1, XOR of stages 12 and 17, a one fed in when everything is zero. */
static uint32_t plainStep(uint32_t v) {
	bool fb = (v == 0u) ? true : ((((v >> 11) ^ (v >> 16)) & 1u) != 0u);
	return ((v << 1) | (fb ? 1u : 0u)) & kRegisterMask;
}

static void testWiringMatchesAPlainRegister() {
	printf("Register: the chip wiring is a plain 18-stage register\n");
	Register4006 r;
	uint32_t p = kRegisterSeed;
	bool same = true;
	long at = -1;
	for (long i = 0; i < 600000 && same; i++) {
		r.clockFalling();
		p = plainStep(p);
		if (r.stages() != p) { same = false; at = i; }
	}
	char d[64];
	snprintf(d, sizeof d, "diverged at step %ld", at);
	check("600000 steps, stage for stage", same, d);

	// And the numbering: setStages then stages is the identity, and stage k is bit k-1.
	Register4006 q;
	bool rt = true;
	for (uint32_t v : { 0x1u, 0x20000u, 0x2AAAAu, 0x3FFFFu, 0x12345u }) {
		q.setStages(v);
		if (q.stages() != v) rt = false;
	}
	check("setStages and stages round-trip", rt);
	q.setStages(1u << 11);   // stage 12 only
	check("stage 12 is pin 11", q.chip.pin11() && !q.chip.pin8());
	q.setStages(1u << 16);   // stage 17 only
	check("stage 17 is pin 8", q.chip.pin8() && !q.chip.pin11());
	q.setStages(1u << 17);   // stage 18 only
	check("stage 18 is pin 9", q.chip.pin9() && !q.chip.pin8());
}

static void testPeriod() {
	printf("Register: maximal length for the stages it taps\n");
	Register4006 r;
	r.clockFalling();                         // settle the delayed 18th stage
	uint32_t start = r.stages();
	long steps = 0;
	long ones = 0;
	do {
		bool fb = r.clockFalling();
		ones += fb ? 1 : 0;
		steps++;
	} while (r.stages() != start && steps < 400000);
	char d[64];
	snprintf(d, sizeof d, "period %ld", steps);
	check("the period is exactly 2^17 - 1 = 131071", steps == 131071, d);
	snprintf(d, sizeof d, "%ld ones in %ld", ones, steps);
	check("one period holds 2^16 ones (a maximal sequence is balanced)", ones == 65536, d);
	// The 18th stage is the 17th delayed by a clock.
	Register4006 q;
	bool delayed = true;
	for (int i = 0; i < 1000; i++) {
		uint32_t before = q.stages();
		q.clockFalling();
		bool s18 = (q.stages() >> 17) & 1u, s17before = (before >> 16) & 1u;
		if (i > 0 && s18 != s17before) delayed = false;
	}
	check("stage 18 is stage 17 a clock later", delayed);
}

/** Period of the register with new bit = stage a XOR stage b (b the deeper), from a
    one in the first stage. A register deeper than b only delays what is at b. */
static long period(int a, int b) {
	std::vector<uint8_t> s(b, 0);
	s[0] = 1;
	std::vector<uint8_t> start = s;
	long n = 0;
	do {
		uint8_t fb = s[a - 1] ^ s[b - 1];
		for (int i = b - 1; i > 0; i--) s[i] = s[i - 1];
		s[0] = fb;
		n++;
	} while (s != start && n < (1L << 19));
	return s == start ? n : -1;
}

static void testWhatACanChipCanTap() {
	printf("CD4006: which taps a single chip can reach\n");
	// Every order the four sections can be chained in, and the stage numbers whose
	// pins that exposes.
	std::set<int> everExposed;
	std::set<std::pair<int,int>> pairs;
	int order[4] = { 0, 1, 2, 3 };
	int orders = 0;
	do {
		orders++;
		std::set<int> exp;
		int base = 0;
		for (int k = 0; k < 4; k++) {
			int s = order[k], len = cd4006::kSectionStages[s];
			if (cd4006::kHasFourthTap[s]) exp.insert(base + 4);
			exp.insert(base + len);
			base += len;
		}
		everExposed.insert(exp.begin(), exp.end());
		for (int a : exp) for (int b : exp) if (a < b) pairs.insert({ a, b });
	} while (std::next_permutation(order, order + 4));
	check("24 ways to chain the sections", orders == 24);
	check("stage 11 is never exposed", !everExposed.count(11));
	check("stage 7, the reciprocal of 11, is never exposed", !everExposed.count(7));
	check("stage 18 is always the last pin", everExposed.count(18) == 1);

	// The best a single chip can do: the longest maximal sequence over every pair.
	long best = 0;
	std::pair<int,int> bestPair = { 0, 0 };
	for (auto& pr : pairs) {
		long p = period(pr.first, pr.second);
		long max = (1L << pr.second) - 1;
		if (p == max && p > best) { best = p; bestPair = pr; }
	}
	char d[96];
	snprintf(d, sizeof d, "best is %ld from stages %d and %d", best, bestPair.first, bestPair.second);
	check("no 18-stage maximal sequence is reachable; 17 stages is the most", best == 131071, d);
	check("the stages in use, 12 and 17, are among the reachable pairs",
	      pairs.count({ 12, 17 }) == 1);
	check("(11, 18), the textbook pair, is not reachable", pairs.count({ 11, 18 }) == 0);
	// And the pair in use really is one of the maximal ones, by simulation.
	check("stages 12 and 17 are maximal for 17 stages", period(12, 17) == 131071);
}

static void testLockupRecovery() {
	printf("Register: the start-up network\n");
	Register4006 r;
	r.setStages(0u);
	check("an all-zero register is fed a one", r.feedback());
	r.clockFalling();
	check("and leaves the all-zero state", r.stages() != 0u);
	// It then runs the same sequence it would have from that state.
	uint32_t seen = 0; bool moved = false;
	for (int i = 0; i < 100; i++) {
		r.clockFalling();
		if (r.stages() != seen) moved = true;
		seen = r.stages();
	}
	check("and keeps running", moved && r.stages() != 0u);
	// 18th stage alone set: the next edge shifts it out and the register would be all
	// zero; that must not stick.
	r.setStages(1u << 17);
	r.clockFalling();
	r.clockFalling();
	bool alive = false;
	for (int i = 0; i < 40; i++) { r.clockFalling(); if (r.stages() != 0u) alive = true; }
	check("a register left with only its last stage set recovers", alive);
}

static void testFallingEdge() {
	printf("Clock: negative-going edges\n");
	FallingEdge e;
	// A 50 % square, high for the first half of each cycle: one fall per cycle,
	// at the middle of it.
	int falls = 0, firstFall = -1;
	for (int i = 0; i < 1000; i++) {
		float phase = std::fmod(i * 0.01f, 1.f);        // 100 samples a cycle
		if (e.process(phase < 0.5f)) {
			falls++;
			if (firstFall < 0) firstFall = i;
		}
	}
	check("one falling edge per cycle", falls == 10);
	check("it is half a cycle after the rising one", firstFall >= 49 && firstFall <= 51);
}

int main() {
	testSections();
	testWiringMatchesAPlainRegister();
	testPeriod();
	testWhatACanChipCanTap();
	testLockupRecovery();
	testFallingEdge();
	printf("\n%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}

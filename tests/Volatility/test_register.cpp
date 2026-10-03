// Volatility's shift register: one CD4006B, wired as the datasheet lets it be.
//
// The claims: the register is the Yusynth Random Gate ring (taps 5, 9, 18 and an
// inverter, chain D4>D3>D2>D1), its cycle structure is {262140, 4} over all 2^18
// states, it cannot lock up, and the chip model equals an independent integer
// oracle step for step. Negative controls break a tap or drop the inverter.

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

/** The oracle: the Yusynth ring as plain integer arithmetic, sharing nothing with
    the chip model. Bit k-1 is stage k, the new bit enters stage 1. The feedback is
    NOT(s[a] XOR s[b]) XOR s[c] when `invert`, else s[a] XOR s[b] XOR s[c]. */
struct Ring {
	int a, b, c;
	bool invert;
	uint32_t step(uint32_t v) const {
		uint32_t x = ((v >> (a - 1)) ^ (v >> (b - 1))) & 1u;
		if (invert) x ^= 1u;
		x ^= (v >> (c - 1)) & 1u;
		return ((v << 1) | x) & 0x3FFFFu;
	}
};
static const Ring kYusynth = { 18, 5, 9, true };

struct Cycles {
	long count;          // number of cycles
	long longest;
	long shortest;
	long onLongest;      // states on the longest cycle
	bool bijective;
};

/** Every one of the 2^18 states, followed until it returns. Works for any map; a
    state that falls into a cycle it did not start on is reported by `bijective`. */
static Cycles cycleStructure(const Ring& r) {
	std::vector<int8_t> mark(1u << 18, 0);   // 0 unseen, 1 on this walk, 2 done
	Cycles c = { 0, 0, 1L << 30, 0, true };
	for (uint32_t s0 = 0; s0 < (1u << 18); s0++) {
		if (mark[s0]) continue;
		uint32_t s = s0;
		long n = 0;
		while (mark[s] == 0) { mark[s] = 1; s = r.step(s); n++; }
		if (mark[s] != 1 || s != s0) c.bijective = false;
		else {
			c.count++;
			if (n > c.longest) c.longest = n;
			if (n < c.shortest) c.shortest = n;
		}
		for (uint32_t t = s0, i = 0; i < (uint32_t) n; i++, t = r.step(t)) mark[t] = 2;
	}
	c.onLongest = c.longest;
	return c;
}

static void testWiringMatchesOracle() {
	printf("Register: the chip wiring equals the oracle, stage for stage\n");
	// Every state, one step: the chip model against independent arithmetic.
	Register4006 r;
	long bad = -1;
	for (uint32_t v = 0; v < (1u << 18); v++) {
		r.setStages(v);
		r.clockFalling();
		if (r.stages() != kYusynth.step(v)) { bad = v; break; }
	}
	char d[64];
	snprintf(d, sizeof d, "first mismatch at state %ld", bad);
	check("all 262144 states take the same step", bad < 0, d);

	// And a long free run from the seed.
	Register4006 q;
	uint32_t p = kRegisterSeed;
	bool same = true;
	for (long i = 0; i < 600000 && same; i++) {
		q.clockFalling();
		p = kYusynth.step(p);
		if (q.stages() != p) same = false;
	}
	check("600000 clocks from the seed, stage for stage", same);

	// Numbering: round-trip, and which stage is on which pin.
	bool rt = true;
	for (uint32_t v : { 0x1u, 0x20000u, 0x2AAAAu, 0x3FFFFu, 0x12345u }) {
		q.setStages(v);
		if (q.stages() != v) rt = false;
	}
	check("setStages and stages round-trip", rt);
	const struct { int stage; int pin; } pins[] = { {4, 8}, {5, 9}, {9, 10}, {13, 11}, {14, 12}, {18, 13} };
	for (auto& pr : pins) {
		q.setStages(1u << (pr.stage - 1));
		bool on[14] = {};
		on[8] = q.chip.pin8(); on[9] = q.chip.pin9(); on[10] = q.chip.pin10();
		on[11] = q.chip.pin11(); on[12] = q.chip.pin12(); on[13] = q.chip.pin13();
		int n = 0;
		for (int k = 8; k <= 13; k++) n += on[k];
		snprintf(d, sizeof d, "stage %d should be pin %d only", pr.stage, pr.pin);
		check(d, on[pr.pin] && n == 1);
	}
	// The chain order: a bit entered at D4 reaches pin 9 after 5 clocks, pin 10 after
	// 9, pin 12 after 14 and pin 13 after 18 -- the schematic's D4 > D3 > D2 > D1.
	Register4006 w;
	w.setStages(0u);
	int at9 = -1, at10 = -1, at11 = -1, at12 = -1, at13 = -1, at8 = -1;
	// Drive a single pulse through by hand: feedback bypassed with a direct chip clock.
	w.chip.clockFalling(false, false, false, true);
	for (int n = 1; n <= 20; n++) {
		if (w.chip.pin8()  && at8  < 0) at8  = n;
		if (w.chip.pin9()  && at9  < 0) at9  = n;
		if (w.chip.pin10() && at10 < 0) at10 = n;
		if (w.chip.pin11() && at11 < 0) at11 = n;
		if (w.chip.pin12() && at12 < 0) at12 = n;
		if (w.chip.pin13() && at13 < 0) at13 = n;
		w.chip.clockFalling(w.chip.pin12(), w.chip.pin10(), w.chip.pin9(), false);
	}
	check("a bit at D4 reaches pins 8, 9, 10, 11, 12, 13 after 4, 5, 9, 13, 14, 18 clocks",
	      at8 == 4 && at9 == 5 && at10 == 9 && at11 == 13 && at12 == 14 && at13 == 18);
}

static void testPeriod() {
	printf("Register: every state, simulated\n");
	Cycles c = cycleStructure(kYusynth);
	char d[96];
	snprintf(d, sizeof d, "%ld cycles, longest %ld, shortest %ld", c.count, c.longest, c.shortest);
	check("every state lies on a cycle (no transients)", c.bijective);
	check("exactly two cycles", c.count == 2, d);
	check("the long one is 2^18 - 4 = 262140 clocks", c.longest == 262140, d);
	check("the other is 4 clocks", c.shortest == 4, d);

	// Walk the real register from the seed: it must be on the long cycle and take
	// exactly 262140 clocks to return, balanced to within the inverter's offset.
	Register4006 r;
	uint32_t start = r.stages();
	long steps = 0, ones = 0;
	do {
		ones += r.clockFalling() ? 1 : 0;
		steps++;
	} while (r.stages() != start && steps < 300000);
	snprintf(d, sizeof d, "period %ld", steps);
	check("the seed is on the long cycle and returns after 262140 clocks", steps == 262140, d);
	snprintf(d, sizeof d, "%ld ones in %ld", ones, steps);
	check("the long cycle holds 131070 ones", ones == 131070, d);

	// The short cycle: 001100110011001100 (stage 18 first) and its three successors.
	uint32_t m[4];
	m[0] = 0x0CCCCu;
	for (int i = 1; i < 4; i++) m[i] = kYusynth.step(m[i - 1]);
	check("0x0CCCC returns to itself after exactly 4 clocks",
	      kYusynth.step(m[3]) == m[0] && m[1] != m[0] && m[2] != m[0] && m[3] != m[0]);
	Register4006 rs;
	bool onShort = false;
	for (int i = 0; i < 4; i++) if (rs.stages() == m[i]) onShort = true;
	check("and the seed is not on it", !onShort);
}

static void testNoLockup() {
	printf("Register: no lock-up, no start-up rule\n");
	Register4006 r;
	r.setStages(0u);
	check("an all-zero register feeds back a one (inverter), by construction", r.feedback());
	r.clockFalling();
	check("and leaves all zeros by itself", r.stages() == 1u);
	// Does all-zero sit on the long cycle? Report it; it is data, not a rule.
	Register4006 z;
	z.setStages(0u);
	long n = 0;
	do { z.clockFalling(); n++; } while (z.stages() != 0u && n < 300000);
	char d[48];
	snprintf(d, sizeof d, "all-zero cycle length %ld", n);
	check("all zeros is on the long cycle", n == 262140, d);
	// All ones: feeds back a zero, also not stuck.
	Register4006 o;
	o.setStages(kRegisterMask);
	check("all ones feeds back a zero", !o.feedback());
	// No state is a fixed point.
	bool fixed = false;
	for (uint32_t v = 0; v < (1u << 18) && !fixed; v++)
		if (kYusynth.step(v) == v) fixed = true;
	check("no state maps to itself", !fixed);
	// The module must not impose a rule on all-zero: nothing special-cased.
	Register4006 w;
	w.setStages(0u);
	uint32_t seq[3];
	for (int i = 0; i < 3; i++) { w.clockFalling(); seq[i] = w.stages(); }
	check("zero-start run is the plain ring (1, 2+..., no injected ones)",
	      seq[0] == 0x1u && seq[1] == kYusynth.step(0x1u) && seq[2] == kYusynth.step(seq[1]));
}

static void testNegativeControls() {
	printf("Negative controls: wrong taps and a missing inverter must not pass\n");
	// 1. Missing inverter: the same taps without NOT. All zeros then locks up, and
	//    the cycle structure is not {262140, 4}.
	Ring noInv = { 18, 5, 9, false };
	Cycles c1 = cycleStructure(noInv);
	check("without the inverter the period is not 262140", c1.longest != 262140);
	check("without the inverter all zeros is a fixed point", noInv.step(0u) == 0u);
	// 2. Wrong tap on each of the three.
	Ring wrongA = { 17, 5, 9, true }, wrongB = { 18, 4, 9, true }, wrongC = { 18, 5, 8, true };
	Cycles ca = cycleStructure(wrongA), cb = cycleStructure(wrongB), cc = cycleStructure(wrongC);
	check("tap 17 for 18: structure differs from {262140, 4}",
	      !(ca.count == 2 && ca.longest == 262140 && ca.shortest == 4));
	check("tap 4 for 5: structure differs",
	      !(cb.count == 2 && cb.longest == 262140 && cb.shortest == 4));
	check("tap 8 for 9: structure differs",
	      !(cc.count == 2 && cc.longest == 262140 && cc.shortest == 4));
	// 3. The chip register must disagree with each broken oracle (so the
	//    equivalence test above has teeth).
	const Ring* broken[] = { &noInv, &wrongA, &wrongB, &wrongC };
	const char* names[] = { "no inverter", "tap 17", "tap 4", "tap 8" };
	for (int i = 0; i < 4; i++) {
		Register4006 r;
		bool differs = false;
		for (uint32_t v = 0; v < 4096 && !differs; v++) {
			r.setStages(v * 61u & kRegisterMask);
			uint32_t before = r.stages();
			r.clockFalling();
			if (r.stages() != broken[i]->step(before)) differs = true;
		}
		char d[64];
		snprintf(d, sizeof d, "chip register is detected as differing from: %s", names[i]);
		check(d, differs);
	}
	// 4. The old 17-stage design (stages 12 and 17, no inverter) is not this.
	Ring old = { 12, 17, 17, false };
	check("the previous 12/17 register is not this one",
	      old.step(0x1555u) != kYusynth.step(0x1555u));
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
	testWiringMatchesOracle();
	testPeriod();
	testNoLockup();
	testNegativeControls();
	testFallingEdge();
	printf("\n%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}

// Payment Schedule's drum sequencer (src/PaymentSchedule/DrumSeq.hpp), without Rack.
//
// Every check drives the engine with a clock the way a Rack cable would -- a trigger
// of a millisecond, or a square -- and reads the trigger outputs' rising edges, so it
// tests what a drum module downstream would hear: which steps fire, and on which
// sample.
#include "../../src/PaymentSchedule/DrumSeq.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace drumseq;

static int checks = 0, failures = 0;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("  FAIL  "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static const float SR = 48000.f;

// The rising edges of each track's trigger, in samples.
struct Run {
	DrumSeq* d;
	std::vector<int64_t> edges[kTracks];
	std::vector<int64_t> eocs;
	std::vector<int64_t> ticks;		// the sample of each clock edge
	bool was[kTracks] = {};
	int64_t n = 0;
	bool hi = false;
	explicit Run(DrumSeq* d) : d(d) {}

	// `ticks` clock edges `period` samples apart, the clock high for `high` samples each.
	void clock(int nt, int period, int high, bool resetFirst = false, bool running = true) {
		for (int k = 0; k < nt; k++)
			for (int i = 0; i < period; i++) {
				// the voltage on the cable, through a Schmitt trigger as the module reads it
				float v = i < high ? 10.f : 0.f;
				bool edge = false;
				if (!hi && v >= 2.f) { hi = true; edge = true; }
				else if (hi && v <= 0.1f) hi = false;
				bool rst = resetFirst && k == 0 && i == 0;
				sample(edge, rst, running);
				if (edge) ticks.push_back(n - 1);
			}
	}
	void sample(bool edge, bool rst, bool running = true) {
		d->process(edge, rst, running);
		for (int t = 0; t < kTracks; t++) {
			if (d->trig[t] && !was[t]) edges[t].push_back(n);
			was[t] = d->trig[t];
		}
		if (d->eoc) eocs.push_back(n);
		n++;
	}
};

static DrumSeq* fresh() {
	DrumSeq* d = new DrumSeq;
	d->setSampleRate(SR);
	return d;
}

int main() {
	printf("Payment Schedule: the drum sequencer\n");
	const int P = 6000;		// a sixteenth at 120 BPM

	// --- the starter beat, from a 1 ms trigger clock and from a square -----------------
	for (int sq = 0; sq < 2; sq++) {
		DrumSeq* d = fresh();
		Run r(d);
		// the edge is all the engine sees: the square's high half must not double-step
		r.clock(32, P, sq ? P / 2 : 48);
		CHECK(r.edges[0].size() == 8 && r.edges[1].size() == 4 && r.edges[2].size() == 16 && r.edges[3].size() == 2,
		      "%s clock: kick %zu snare %zu hats %zu open %zu, want 8 4 16 2", sq ? "square" : "1 ms",
		      r.edges[0].size(), r.edges[1].size(), r.edges[2].size(), r.edges[3].size());
		CHECK(!r.edges[0].empty() && r.edges[0][0] == 0, "the first clock plays step 1 at once (kick at %lld)",
		      r.edges[0].empty() ? -1LL : (long long)r.edges[0][0]);
		CHECK(r.edges[1].size() >= 1 && r.edges[1][0] == 4 * P, "the snare lands on tick 5 (sample %lld)",
		      r.edges[1].empty() ? -1LL : (long long)r.edges[1][0]);
		CHECK(r.eocs.size() == 1 && r.eocs[0] == 16 * P, "EOC once, as track 1 comes round (%zu)", r.eocs.size());
		delete d;
	}
	printf("  steps from a trigger clock and a square\n");

	// --- reset ---------------------------------------------------------------------------
	{
		DrumSeq* d = fresh();
		Run r(d);
		r.clock(6, P, 48);
		// a reset alone: nothing plays until the clock, and the clock plays step 1
		for (int i = 0; i < 100; i++) r.sample(false, i == 0);
		size_t kicks = r.edges[0].size();
		r.clock(1, P, 48);
		CHECK(r.edges[0].size() == kicks + 1 && r.edges[0].back() == r.ticks.back(), "after RESET the next clock plays step 1");
		// a reset and a clock on the same sample: step 1, then
		r.clock(3, P, 48);
		kicks = r.edges[0].size();
		r.clock(1, P, 48, true);
		CHECK(r.edges[0].size() == kicks + 1 && r.edges[0].back() == r.ticks.back(), "RESET with a clock plays step 1 on that clock");
		delete d;
	}
	printf("  reset\n");

	// --- length and division -----------------------------------------------------------------
	{
		DrumSeq* d = fresh();
		d->pat[0].clear();
		d->pat[0].t[0].length = 3; d->pat[0].t[0].s[0].on = 1;
		d->pat[0].t[1].div = 5;  for (int i = 0; i < 16; i++) d->pat[0].t[1].s[i].on = 1;	// /2
		d->pat[0].t[2].div = 2;  for (int i = 0; i < 16; i++) d->pat[0].t[2].s[i].on = 1;	// x2
		d->pat[0].t[3].div = 3;  for (int i = 0; i < 16; i++) d->pat[0].t[3].s[i].on = 1;	// x3/2
		Run r(d);
		r.clock(12, P, 48);
		CHECK(r.edges[0].size() == 4 && r.edges[0][1] == 3 * P, "length 3: every third tick (%zu hits)", r.edges[0].size());
		CHECK(r.edges[1].size() == 6 && r.edges[1][1] == 2 * P, "/2: every other tick (%zu hits)", r.edges[1].size());
		// x2 doubles once the period is known (after the second edge)
		bool halves = r.edges[2].size() >= 20;
		for (size_t i = 3; i + 1 < r.edges[2].size() && halves; i++)
			if (r.edges[2][i + 1] - r.edges[2][i] != P / 2) halves = false;
		CHECK(halves, "x2: two steps a tick, half a period apart (%zu hits)", r.edges[2].size());
		bool thirds = true;
		int64_t gap = 2 * P / 3;
		for (size_t i = 3; i + 1 < r.edges[3].size(); i++)
			if (std::llabs((long long)(r.edges[3][i + 1] - r.edges[3][i] - gap)) > 1) thirds = false;
		CHECK(thirds && r.edges[3].size() >= 15, "x3/2: three steps every two ticks (%zu hits)", r.edges[3].size());
		delete d;
	}
	printf("  length and division\n");

	// --- ratchets, offsets, swing ------------------------------------------------------------
	{
		DrumSeq* d = fresh();
		d->pat[0].clear();
		d->pat[0].t[0].s[3].on = 1; d->pat[0].t[0].s[3].ratchet = 3;
		d->pat[0].t[1].s[3].on = 1; d->pat[0].t[1].s[3].offset = 25;
		d->pat[0].t[2].s[3].on = 1; d->pat[0].t[2].s[3].offset = -25;
		Run r(d);
		r.clock(8, P, 48);
		int64_t t3 = 3 * P;
		CHECK(r.edges[0].size() == 3 && r.edges[0][0] == t3 && r.edges[0][1] == t3 + P / 3 && r.edges[0][2] == t3 + 2 * P / 3,
		      "ratchet 3: three hits a third of a step apart");
		CHECK(r.edges[1].size() == 1 && r.edges[1][0] == t3 + P / 4, "offset +25 %%: a quarter step late (%lld)",
		      r.edges[1].empty() ? -1LL : (long long)(r.edges[1][0] - t3));
		CHECK(r.edges[2].size() == 1 && r.edges[2][0] == t3 - P / 4, "offset -25 %%: a quarter step early (%lld)",
		      r.edges[2].empty() ? -1LL : (long long)(r.edges[2][0] - t3));
		delete d;

		d = fresh();
		d->pat[0].clear();
		for (int i = 0; i < 16; i++) d->pat[0].t[0].s[i].on = 1;
		d->swing = 0.75f;
		Run s(d);
		s.clock(8, P, 48);
		CHECK(s.edges[0].size() == 8 && s.edges[0][3] - 3 * P == P / 2 && s.edges[0][4] == 4 * P,
		      "swing 75 %%: every other step half a step late");
		delete d;
	}
	printf("  ratchets, offsets and swing\n");

	// --- chance ------------------------------------------------------------------------------
	{
		DrumSeq* d = fresh();
		d->pat[0].clear();
		for (int i = 0; i < 16; i++) { d->pat[0].t[0].s[i].on = 1; d->pat[0].t[0].s[i].chance = 50; d->pat[0].t[0].s[i].ratchet = 2; }
		Run r(d);
		r.clock(4000, 200, 10);
		// a step that plays plays both its ratchets: hits come in pairs
		double f = r.edges[0].size() / 2.0 / 4000.0;
		CHECK(r.edges[0].size() % 2 == 0 && f > 0.46 && f < 0.54, "chance 50 %%: %.3f of the steps played", f);
		delete d;
	}
	printf("  chance\n");

	// --- patterns: asked for, taken at the bar -----------------------------------------------
	{
		DrumSeq* d = fresh();
		d->pat[1].clear();
		Run r(d);
		r.clock(5, P, 48);
		d->want = 1;
		r.clock(10, P, 48);
		CHECK(d->cur == 0, "a new pattern waits for the bar");
		r.clock(2, P, 48);
		CHECK(d->cur == 1, "and is playing after it");
		size_t k = r.edges[0].size();
		r.clock(16, P, 48);
		CHECK(r.edges[0].size() == k, "the empty pattern plays nothing");
		d->switchOnBar = false;
		d->want = 0;
		r.clock(1, P, 48);
		CHECK(d->cur == 0, "set to switch on the tick, it does");
		delete d;
	}
	printf("  pattern changes\n");

	// --- mute, stop, retrigger ---------------------------------------------------------------
	{
		DrumSeq* d = fresh();
		d->mute[0] = true;
		Run r(d);
		r.clock(16, P, 48);
		CHECK(r.edges[0].empty() && r.edges[1].size() == 2, "a muted track is silent, the others are not");
		r.clock(16, P, 48, false, false);
		CHECK(r.edges[1].size() == 2, "stopped, the clock does nothing");
		delete d;

		// a trigger longer than the gap between hits: every hit still has its own edge
		d = fresh();
		d->pat[0].clear();
		d->pat[0].t[0].div = 0;		// x4
		for (int i = 0; i < 16; i++) d->pat[0].t[0].s[i].on = 1;
		d->trigSeconds = 0.2f;
		Run s(d);
		s.clock(6, P, 48);
		CHECK(s.edges[0].size() >= 18, "long triggers retrigger: %zu edges for 18+ hits", s.edges[0].size());
		delete d;
	}
	printf("  mute, stop and retrigger\n");

	// --- a patch's text --------------------------------------------------------------------------
	{
		Dice dc;
		Pattern p;
		p.clear();
		randomBeat(p, dc);
		p.t[5].length = 37; p.t[5].div = 7; p.t[5].s[36].offset = -50; p.t[5].s[36].cv = 99; p.t[5].s[36].ratchet = 4;
		int bad = 0;
		for (int t = 0; t < kTracks; t++) {
			char buf[4 + 12 * kSteps + 1];
			trackToText(p.t[t], buf);
			Track back;
			if (!trackFromText(buf, back)) { bad++; continue; }
			for (int i = 0; i < kSteps; i++) if (!(back.s[i] == p.t[t].s[i])) bad++;
			if (back.length != p.t[t].length || back.div != p.t[t].div) bad++;
		}
		CHECK(bad == 0, "patterns survive a save (%d differences)", bad);
		Track junk;
		CHECK(!trackFromText("zz", junk) && !trackFromText("ff04", junk), "a damaged track is refused");
		// rotate
		Track tr;
		tr.length = 5; tr.s[0].on = 1;
		tr.rotate(2);
		CHECK(tr.s[2].on && !tr.s[0].on, "rotate moves a hit within the length");
		tr.rotate(-3);
		CHECK(tr.s[4].on, "and back round the end");
	}
	printf("  saving, rotating\n");

	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}

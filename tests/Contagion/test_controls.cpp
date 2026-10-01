// Controls: a knob made into the unit's buttons, tested against a model of a selector that behaves
// the way the real firmware was measured to (test_machine.cpp runs the same logic on the firmware).
#include "../../src/Contagion/Controls.hpp"

#include <cstdio>
#include <cstdlib>

static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { failures++; std::printf("FAIL line %d: ", __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static const float SR = 48000.f;
static const float TICK = 0.025f;               // the module's housekeeping rate, 40 Hz

/** A cycling selector as the 80C515 has it: a press (down 30 ms or more, preceded by 40 ms up) steps
    to the next position, and the LEDs show it a little later. */
struct Model {
	int steps, pos = 0, shownPos = 0;
	int downSamples = 0, upSamples = 100000, lag = 0;
	bool wasDown = false, ignoreKeys = false;
	int dropFirst = 0;                          // presses that vanish
	int presses = 0;
	Model(int n) : steps(n) {}
	void sample(bool down) {
		if (down) { downSamples++; if (!wasDown) { if (upSamples < int(0.040f * SR)) downSamples = -1000000; } upSamples = 0; }
		else { if (wasDown && downSamples >= int(0.030f * SR)) press(); upSamples++; downSamples = 0; }
		wasDown = down;
		if (lag > 0 && --lag == 0) shownPos = pos;
	}
	void press() {
		if (ignoreKeys) return;
		if (dropFirst > 0) { dropFirst--; return; }
		pos = (pos + 1) % steps;
		presses++;
		lag = int(0.050f * SR);
	}
};

struct Rig {
	Model m;
	vc::KeyPresser keys;
	vc::Selector sel;
	int knob;
	Rig(int n) : m(n) { sel.steps = n; sel.stepKey = 0; knob = 0; }
	void run(float seconds) {
		const int ticks = int(seconds / TICK);
		for (int t = 0; t < ticks; t++) {
			for (int s = 0; s < int(TICK * SR); s++) m.sample(keys.process(0, SR));
			const int k = sel.tick(knob, m.shownPos, keys, TICK);
			if (k >= 0) knob = k;
		}
	}
};

int main() {
	{   // pacing: a press is 45 ms down and 45 ms up, and requests queue
		vc::KeyPresser k;
		k.press(3, 3);
		int downs = 0, edges = 0;
		bool prev = false;
		for (int s = 0; s < int(1.0f * SR); s++) {
			const bool d = k.process(3, SR);
			downs += d; edges += d && !prev; prev = d;
		}
		CHECK(edges == 3, "three presses, got %d", edges);
		CHECK(std::abs(downs - int(3 * 0.045f * SR)) <= 3, "each down for 45 ms (%d samples)", downs);
		k.press(3, 1000);
		CHECK(k.pending(3) <= vc::KeyPresser::MAX_QUEUED, "queue is capped (%d)", k.pending(3));
	}
	{   // a knob turned forward presses the cycling key until the LEDs agree
		Rig r(4);
		r.run(0.5f);                            // first sight of the unit
		r.knob = 3;
		r.run(2.0f);
		CHECK(r.m.pos == 3 && r.m.presses == 3, "LP to BS is three presses (pos %d, %d presses)", r.m.pos, r.m.presses);
		CHECK(r.knob == 3, "the knob stays where it was turned (%d)", r.knob);
		r.knob = 1;                             // backwards is the long way round: 2 presses
		r.run(2.0f);
		CHECK(r.m.pos == 1 && r.m.presses == 5, "BS to HP wraps (pos %d, %d presses)", r.m.pos, r.m.presses);
	}
	{   // turned again while the first presses are still playing
		Rig r(4);
		r.run(0.5f);
		r.knob = 2;
		r.run(0.12f);
		r.knob = 3;
		r.run(3.0f);
		CHECK(r.m.pos == 3, "ends on the last position (%d)", r.m.pos);
	}
	{   // the unit changes it (a program load): the knob follows
		Rig r(5);
		r.run(0.5f);
		r.m.pos = r.m.shownPos = 4;
		r.run(0.5f);
		CHECK(r.knob == 4, "knob follows the unit (%d)", r.knob);
		CHECK(r.m.presses == 0, "and presses nothing");
	}
	{   // a press that is lost is pressed again
		Rig r(4);
		r.run(0.5f);
		r.m.dropFirst = 1;
		r.knob = 1;
		r.run(3.0f);
		CHECK(r.m.pos == 1, "retried after a lost press (%d)", r.m.pos);
	}
	{   // a unit that will not move has the last word
		Rig r(4);
		r.run(0.5f);
		r.m.ignoreKeys = true;
		r.knob = 2;
		r.run(4.0f);
		CHECK(r.knob == 0 && r.m.pos == 0, "knob goes back to what the unit shows (%d)", r.knob);
	}
	{   // direct selectors press the position's own key, once
		Rig r(3);
		static const int keys[3] = { 0, 1, 2 };
		r.sel.directKey = keys;
		int pressed[3] = {};
		r.run(0.5f);
		r.knob = 2;
		for (int s = 0; s < int(1.5f * SR); s++) {
			for (int i = 0; i < 3; i++) if (r.keys.process(i, SR)) pressed[i]++;
			if (s % int(TICK * SR) == 0) { const int k = r.sel.tick(r.knob, r.m.shownPos, r.keys, TICK); if (k >= 0) r.knob = k; }
		}
		CHECK(pressed[2] > 0 && pressed[0] == 0 && pressed[1] == 0, "only key 2 pressed (%d %d %d)", pressed[0], pressed[1], pressed[2]);
	}
	{   // a dark LED row means the last position when told so
		vc::Selector s;
		s.steps = 5;
		s.lastWhenDark = true;
		vc::KeyPresser k;
		int out = -1;
		for (int i = 0; i < 4; i++) { const int r = s.tick(0, -1, k, TICK); if (r >= 0) out = r; }
		CHECK(out == 4, "no LED lit is position 5 (%d)", out);
	}
	{   // encoder: sixteen detents a turn, either way, and the first reading is not a move
		vc::Encoder e;
		CHECK(e.delta(3.3f) == 0, "first reading");
		CHECK(e.delta(3.3f + 3.f / 16) == 3, "three detents clockwise");
		CHECK(e.delta(3.3f - 1.f / 16) == -4, "four back");
		CHECK(e.delta(3.3f - 1.f / 16) == 0, "still");
		CHECK(e.delta(100.f) == vc::KeyPresser::MAX_QUEUED, "a jump is limited");
	}
	{   // litPosition
		const int leds[4] = { 0, 1, 2, 3 };
		float b[4] = { 0, 0, 1, 0 };
		auto f = [&](int i) { return b[i]; };
		CHECK(vc::litPosition(f, leds, 4) == 2, "one lit");
		b[0] = 1;
		CHECK(vc::litPosition(f, leds, 4) == -1, "two lit is unknown");
		b[0] = b[2] = 0;
		CHECK(vc::litPosition(f, leds, 4) == -1, "none lit is unknown");
	}
	if (failures) { std::printf("%d FAILED\n", failures); return 1; }
	std::printf("Controls: ok\n");
	return 0;
}

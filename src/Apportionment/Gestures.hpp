// Apportionment's gestures: the DP/4's two-handed button combinations, played for you. Self-contained (no Rack) so
// tests/Apportionment can run them against the real firmware.
//
// The unit asks for several things a mouse, with its one pointer, cannot do: hold SYSTEM and press A (a soft reset), hold
// < and press CANCEL (jump to the Algorithm Select page), press A and B together (a two-unit preset), hold one Unit
// button and press another (swap, or copy, two units). Each is a short script of presses, holds and waits, played
// through Machine::button() -- the unit sees the keys it would from a hand -- one step per machine frame, so the
// timing is the machine's own and never the UI's.
#pragma once
#include <cstdint>
#include <vector>

#include "Machine.hpp"

namespace dp4 {

struct Gestures {
	enum Kind { DOWN, UP, KNOB, WAIT };
	struct Step { Kind kind; int arg; };

	/** A script being played, and the frame it is waiting for. */
	std::vector<Step> steps;
	size_t at = 0;
	long wait = 0;

	bool busy() const { return at < steps.size() || wait > 0; }
	void clear() { steps.clear(); at = 0; wait = 0; }

	/** Start a script; ignored while another is playing (a second click on a button does nothing, as a second
	    press of a held key would). Returns whether it started. */
	bool play(const std::vector<Step>& s) {
		if (busy()) return false;
		steps = s;
		at = 0;
		wait = 0;
		return true;
	}

	/** One machine frame. */
	void tick(Machine& m) {
		if (wait > 0) { wait--; return; }
		while (at < steps.size()) {
			const Step s = steps[at++];
			switch (s.kind) {
			case DOWN: m.button(s.arg, true); break;
			case UP: m.button(s.arg, false); break;
			case KNOB: m.knob(s.arg); break;
			case WAIT: wait = long(s.arg) * long(Machine::FRAME_RATE) / 1000; return;
			}
		}
	}

	// --- the scripts ------------------------------------------------------------------
	static Step down(int b) { return { DOWN, b }; }
	static Step up(int b) { return { UP, b }; }
	static Step wait_ms(int ms) { return { WAIT, ms }; }
	static void tap(std::vector<Step>& s, int b, int hold = 150, int after = 400) {
		s.push_back(down(b)); s.push_back(wait_ms(hold)); s.push_back(up(b)); s.push_back(wait_ms(after));
	}

	/** Hold `holder`, press `key` once, let go: "hold X, press Y". */
	static std::vector<Step> chord(int holder, int key) {
		std::vector<Step> s;
		s.push_back(down(holder)); s.push_back(wait_ms(300));
		tap(s, key, 150, 300);
		s.push_back(up(holder));
		return s;
	}
	/** SYSTEM held and A pressed: a soft reset that leaves the memory alone. */
	static std::vector<Step> softReset() { return chord(BTN_SYSTEM, BTN_A); }
	/** SYSTEM held and B pressed: "Hit <WRITE> To Init RAM Presets"; WRITE confirms, and > first offers the full reinitialise. */
	static std::vector<Step> initRam() { return chord(BTN_SYSTEM, BTN_B); }
	/** < held and CANCEL pressed: the first page of the unit being edited, which is its algorithm. */
	static std::vector<Step> algorithm() { return chord(BTN_LEFT, BTN_CANCEL); }
	/** > held and < pressed jumps forward a whole screen; < held and > pressed, back. */
	static std::vector<Step> screen(int dir) { return dir > 0 ? chord(BTN_RIGHT, BTN_LEFT) : chord(BTN_LEFT, BTN_RIGHT); }
	/** EDIT, then two Unit buttons together: the DATA knob now chooses a two-unit preset (A and B, or C and D). */
	static std::vector<Step> pair(int first, int second) {
		std::vector<Step> s;
		tap(s, BTN_EDIT, 150, 500);
		s.push_back(down(first)); s.push_back(down(second)); s.push_back(wait_ms(300));
		s.push_back(up(first)); s.push_back(up(second)); s.push_back(wait_ms(300));
		return s;
	}
	/** Swap or copy unit `from` to `to`: EDIT, WRITE, the first Unit button held while the second is pressed. That
	    brings up "Hit <WRITE> To Swap Units A & B"; a click of the DATA knob clockwise turns Swap into Copy. WRITE,
	    which is the unit's own button and the user's to press, makes it so. */
	static std::vector<Step> swapOrCopy(int from, int to, bool copy) {
		std::vector<Step> s;
		tap(s, BTN_EDIT, 150, 500);
		tap(s, BTN_WRITE, 150, 500);
		s.push_back(down(from)); s.push_back(wait_ms(250));
		s.push_back(down(to)); s.push_back(wait_ms(250));
		s.push_back(up(to)); s.push_back(wait_ms(250));
		s.push_back(up(from)); s.push_back(wait_ms(500));
		if (copy) { s.push_back({ KNOB, 1 }); s.push_back(wait_ms(800)); }
		return s;
	}
};

} // namespace dp4

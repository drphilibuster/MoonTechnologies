// Apportionment -- puts the DP/4's Config (its routing) on the panel.
//
// The routing is not something this module computes. On the DP/4 it is a
// Config: a source count and a handful of parameters that the operating system
// turns into DSP code -- the I/O tail of every ESP program, its mix gains and the
// C/D input mux. So the router does what a player would: it presses EDIT and
// CONFIG, steps to the right page with < and >, and turns the data knob until
// the firmware's own copy of the field in battery RAM says what the panel says.
// Then it presses SELECT. Every routing the panel can ask for is therefore one
// the real machine built -- and nothing the panel shows can disagree with the
// sound, because the panel is read back from the same RAM.
//
// Config pages, per source count (OS 1.06-1.15; read off the firmware):
//   1 source   0 config  1 AB-CD  2 AB route  3 CD route  4 AB amt  5 CD amt  6 AB in   7 bypass/kill
//   2 sources  0 config  1 AB route  2 CD route  3 AB in  4 CD in  5 AB amt  6 CD amt  7 bypass/kill
//   3 sources  0 config  1 AB out  2 CD route  3 CD in  4 CD amt  5 bypass/kill
//   4 sources  0 config  1 AB out  2 CD out  3 bypass/kill
// ("AB out" is "A>1 B>2 DualMono" or "1-2 Mixed Stereo", kept in the AB routing byte.)
#pragma once
#include "Machine.hpp"

namespace dp4 {

class Router {
public:
	enum Field { F_SOURCES, F_AB_TO_CD, F_AB_ROUTE, F_CD_ROUTE, F_AB_AMOUNT, F_CD_AMOUNT, F_AB_MONO, F_CD_MONO, F_AB_OUT, F_CD_OUT, F_COUNT };

	/** The Config page holding `f` for this many sources, or -1 where it does not exist. */
	static int page(int sources, Field f) {
		static const int PAGE[4][F_COUNT] = {
			// sources ab>cd abR cdR abAmt cdAmt abIn cdIn abOut cdOut
			{ 0,  1,  2,  3,  4,  5,  6, -1, -1, -1 },   // 1 source
			{ 0, -1,  1,  2,  5,  6,  3,  4, -1, -1 },   // 2 sources
			{ 0, -1, -1,  2, -1,  4, -1,  3,  1, -1 },   // 3 sources
			{ 0, -1, -1, -1, -1, -1, -1, -1,  1,  2 },   // 4 sources
		};
		return PAGE[std::max(0, std::min(3, sources - 1))][f];
	}

	static int value(const Routing& r, Field f) {
		switch (f) {
		case F_SOURCES: return r.sources;
		case F_AB_TO_CD: return r.abToCd;
		case F_AB_ROUTE: return r.abRoute;
		case F_CD_ROUTE: return r.cdRoute;
		case F_AB_AMOUNT: return r.abAmount;
		case F_CD_AMOUNT: return r.cdAmount;
		case F_AB_MONO: return r.abMono;
		case F_CD_MONO: return r.cdMono;
		case F_AB_OUT: return r.abOut;
		case F_CD_OUT: return r.cdOut;
		default: return 0;
		}
	}

	/** Ask for a Config. Fields the target's source count does not have are ignored. */
	void request(const Routing& target) { target_ = target; active_ = true; attempts_ = 0; }
	bool busy() const { return active_; }
	/** What the router is doing, for the display ("" when idle). */
	const char* status() const { return active_ ? "ROUTING" : ""; }

	/** Call once per machine frame. Acts only when the machine is idle. */
	void tick(Machine& m) {
		if (!active_) return;
		if (m.panelBusy()) { quietUntil_ = m.frames() + settleFrames(0.12); return; }
		if (m.frames() < quietUntil_) return;

		const Routing now = m.routing();
		const Display& d = m.display();
		const Field f = nextMismatch(now);
		if (f == F_COUNT) {
			// Done: leave the editor the way a player would.
			if (d.led(BTN_EDIT)) { press(m, BTN_SELECT); return; }
			active_ = false;
			return;
		}
		if (++attempts_ > 400) { active_ = false; return; }   // never spin forever
		if (!d.led(BTN_EDIT)) { press(m, BTN_EDIT); return; }
		if (!d.led(BTN_CONFIG)) { press(m, BTN_CONFIG); return; }

		const int want = page(now.sources, f);
		const int at = m.peek(cfg::EDIT_PAGE);
		if (at != want) { press(m, at < want ? BTN_RIGHT : BTN_LEFT); return; }

		const int diff = value(target_, f) - value(now, f);
		// Enumerations one detent at a time: the firmware applies a new source
		// count only once the knob stops, and a burst of detents across several
		// counts leaves it waiting for more knob and deaf to the buttons. Amounts
		// (0-99) go in bites, re-read in between.
		const bool amount = f == F_AB_AMOUNT || f == F_CD_AMOUNT;
		m.knob(amount ? std::max(-8, std::min(8, diff)) : (diff > 0 ? 1 : -1));
		// A new source count makes the firmware rebuild every ESP program.
		quietUntil_ = m.frames() + settleFrames(f == F_SOURCES ? 2.5 : 0.25);
	}

private:
	Routing target_;
	bool active_ = false;
	int attempts_ = 0;
	uint64_t quietUntil_ = 0;

	static uint64_t settleFrames(double s) { return uint64_t(s * Machine::FRAME_RATE); }

	void press(Machine& m, int b) {
		m.button(b, true);
		m.button(b, false);
		quietUntil_ = m.frames() + settleFrames(0.12);
	}

	Field nextMismatch(const Routing& now) const {
		if (now.sources != target_.sources) return F_SOURCES;
		for (int f = F_AB_TO_CD; f < F_COUNT; f++)
			if (page(now.sources, Field(f)) >= 0 && value(now, Field(f)) != value(target_, Field(f)))
				return Field(f);
		return F_COUNT;
	}
};

} // namespace dp4

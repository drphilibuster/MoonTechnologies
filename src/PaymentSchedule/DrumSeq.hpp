// Payment Schedule's engine: an eight-track drum sequencer, in the line of Voxglitch's
// VX Drum Sequencer -- a grid of hits, sixteen pattern memories, one clock -- taken
// further: every track has its own length (to 64 steps) and its own clock division or
// multiplication, and every step its own velocity, chance, ratchet, micro-timing offset
// and a free CV value.
//
// Time is the clock's: each rising edge at CLOCK is one tick (a sixteenth, by
// convention). A track at "1" steps once a tick, at "/2" every other tick, at "x2" twice
// a tick -- the second step placed half the measured clock period later. Ratchets,
// offsets and swing are fractions of the step, so they need that period too; until two
// edges have been seen they play straight, as VX does.
//
// Nothing here knows about Rack: tests/PaymentSchedule drives it bare.
#pragma once
#include <cstdint>
#include <cstring>

namespace drumseq {

enum { kTracks = 8, kSteps = 64, kPatterns = 16, kPage = 16 };

// What a step holds, and what the screen's lanes edit.
enum Lane { kLaneGate, kLaneVel, kLaneChance, kLaneRatchet, kLaneOffset, kLaneCv, kNumLanes };

static const char* const laneNames[kNumLanes] = { "GATE", "VEL", "CHNC", "RTCH", "OFFS", "CV" };

// The clock divisions a track can run at: steps per tick as mult/div.
enum { kNumDivs = 11, kDivOne = 4 };
static const char* const divNames[kNumDivs] = { "x4", "x3", "x2", "x3/2", "1", "/2", "/3", "/4", "/6", "/8", "/16" };
static const int divMul[kNumDivs] = { 4, 3, 2, 3, 1, 1, 1, 1, 1, 1, 1 };
static const int divDiv[kNumDivs] = { 1, 1, 1, 2, 1, 2, 3, 4, 6, 8, 16 };

struct Step {
	uint8_t on = 0;
	uint8_t vel = 100;		// 1..127
	uint8_t chance = 100;	// 0..100 %
	uint8_t ratchet = 1;	// 1..4 hits in the step
	int8_t offset = 0;		// -50..50 % of a step: early or late
	uint8_t cv = 0;			// 0..127 -> 0..10 V

	bool operator==(const Step& o) const {
		return on == o.on && vel == o.vel && chance == o.chance && ratchet == o.ratchet
			&& offset == o.offset && cv == o.cv;
	}

	// A lane's value, and its range.
	int get(int lane) const {
		switch (lane) {
			case kLaneGate: return on;
			case kLaneVel: return vel;
			case kLaneChance: return chance;
			case kLaneRatchet: return ratchet;
			case kLaneOffset: return offset;
			default: return cv;
		}
	}
	void set(int lane, int v) {
		v = v < laneMin(lane) ? laneMin(lane) : v > laneMax(lane) ? laneMax(lane) : v;
		switch (lane) {
			case kLaneGate: on = (uint8_t)v; break;
			case kLaneVel: vel = (uint8_t)v; break;
			case kLaneChance: chance = (uint8_t)v; break;
			case kLaneRatchet: ratchet = (uint8_t)v; break;
			case kLaneOffset: offset = (int8_t)v; break;
			default: cv = (uint8_t)v; break;
		}
	}
	static int laneMin(int lane) { return lane == kLaneVel || lane == kLaneRatchet ? 1 : lane == kLaneOffset ? -50 : 0; }
	static int laneMax(int lane) {
		switch (lane) {
			case kLaneGate: return 1;
			case kLaneChance: return 100;
			case kLaneRatchet: return 4;
			case kLaneOffset: return 50;
			default: return 127;
		}
	}
};

struct Track {
	Step s[kSteps];
	uint8_t length = kPage;		// 1..64
	uint8_t div = kDivOne;

	void clear() { *this = Track(); }
	bool empty() const {
		Track d;
		if (length != d.length || div != d.div) return false;
		for (int i = 0; i < kSteps; i++) if (!(s[i] == d.s[i])) return false;
		return true;
	}
	// Turn the whole track `by` steps later (negative: earlier), within its length.
	void rotate(int by) {
		int n = length < 1 ? 1 : length;
		by = ((by % n) + n) % n;
		if (!by) return;
		Step tmp[kSteps];
		for (int i = 0; i < n; i++) tmp[(i + by) % n] = s[i];
		for (int i = 0; i < n; i++) s[i] = tmp[i];
	}
};

struct Pattern {
	Track t[kTracks];
	void clear() { for (int i = 0; i < kTracks; i++) t[i].clear(); }
	bool empty() const { for (int i = 0; i < kTracks; i++) if (!t[i].empty()) return false; return true; }
	void rotate(int by) { for (int i = 0; i < kTracks; i++) t[i].rotate(by); }
};

/** xorshift32: the dice for CHANCE. */
struct Dice {
	uint32_t s = 0x6D2B79F5u;
	uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
	int percent() { return (int)(next() % 100u); }
};

// A starter beat, so a new module plays something on its first clock: kick on the
// beats, snare on two and four, closed hats on the eighths, an open hat on the last
// off-beat. The other four tracks are empty.
static inline void starterBeat(Pattern& p) {
	p.clear();
	for (int i = 0; i < 16; i += 4) p.t[0].s[i].on = 1;
	p.t[1].s[4].on = p.t[1].s[12].on = 1;
	for (int i = 0; i < 16; i += 2) { p.t[2].s[i].on = 1; p.t[2].s[i].vel = (uint8_t)(i % 4 ? 70 : 100); }
	p.t[3].s[14].on = 1;
}

// A beat made up per track, by role: 1 kick, 2 snare, 3 closed hat, 4 open hat, then
// four sparse percussion lines. Only the tracks in `mask` are rewritten.
static inline void randomBeat(Pattern& p, Dice& d, uint32_t mask = 0xFF) {
	for (int t = 0; t < kTracks; t++) {
		if (!(mask >> t & 1)) continue;
		Track& tr = p.t[t];
		int len = tr.length ? tr.length : 16;
		for (int i = 0; i < kSteps; i++) tr.s[i] = Step();
		for (int i = 0; i < len; i++) {
			Step& s = tr.s[i];
			int r = d.percent();
			switch (t) {
				case 0: s.on = (i % 4 == 0) || r < (i % 2 ? 6 : 14); break;
				case 1: s.on = (i % 8 == 4) || r < 6; if (s.on && i % 8 != 4) s.vel = 55; break;
				case 2:
					s.on = i % 2 == 0 || r < 30;
					s.vel = (uint8_t)(i % 4 == 0 ? 100 : 60 + d.percent() / 3);
					if (s.on && d.percent() < 8) s.ratchet = (uint8_t)(2 + d.percent() % 2);
					break;
				case 3: s.on = (i % 4 == 2 && r < 35); break;
				default: s.on = r < 12; s.vel = (uint8_t)(50 + d.percent() / 2); break;
			}
		}
	}
}

/** The sequencer. One call to process() per sample. */
struct DrumSeq {
	Pattern pat[kPatterns];
	int cur = 0;				// the pattern playing
	int want = 0;				// the pattern asked for: taken at the next boundary
	bool switchOnBar = true;	// false: at the next tick
	bool mute[kTracks] = {};
	float swing = 0.5f;			// 0.5 straight .. 0.75: every other step later
	float trigSeconds = 0.001f;
	float sampleRate = 48000.f;
	Dice dice;

	// Time, in samples.
	int64_t now = 0;
	int64_t lastEdge = -1;
	double period = 0.0;		// samples between the last two clock edges; 0 until known
	int64_t tick = -1;			// ticks since reset; -1: the next clock plays step 1

	struct Pending { int64_t at; uint8_t vel, cv; };
	enum { kQueue = 32 };
	struct TrackState {
		int step = -1;			// the step last started; -1 before the first
		int subLeft = 0;		// a multiplied track's steps still to come this tick
		double subNext = 0.0, subD = 0.0;
		int preFor = -1;		// a step whose early (negative offset) hits are already queued
		Pending q[kQueue];
		int qn = 0;
		int pulse = 0;			// samples of trigger left
		bool gap = false;		// a retrigger: one low sample first
		float vel = 0.f, cv = 0.f;
	} ts[kTracks];

	// Read-outs, set by process().
	bool trig[kTracks] = {};
	bool eoc = false;			// one sample, when track 1 comes round to its first step
	int eocPulse = 0;

	DrumSeq() { starterBeat(pat[0]); }

	Pattern& playing() { return pat[cur]; }

	void setSampleRate(float sr) { sampleRate = sr > 1.f ? sr : 1.f; }

	/** The next clock plays step 1 of every track. */
	void reset() {
		tick = -1;
		for (int t = 0; t < kTracks; t++) {
			TrackState& s = ts[t];
			s.step = -1;
			s.subLeft = 0;
			s.preFor = -1;
			s.qn = 0;
		}
	}

	static int stepsOf(const Track& tr) { return tr.length < 1 ? 1 : tr.length > kSteps ? kSteps : tr.length; }

	void push(int t, int64_t at, const Step& st) {
		TrackState& s = ts[t];
		if (s.qn >= kQueue) return;
		s.q[s.qn].at = at;
		s.q[s.qn].vel = st.vel;
		s.q[s.qn].cv = st.cv;
		s.qn++;
	}

	// The hits of one step, from `at` over a step of `d` samples (0: unknown -- one hit).
	// CHANCE is rolled once: a step that fails loses its ratchets with it.
	void schedule(int t, int64_t at, double d, const Step& st) {
		if (!st.on) return;
		if (st.chance < 100 && dice.percent() >= st.chance) return;
		int r = d > 0.0 ? (st.ratchet < 1 ? 1 : st.ratchet > 4 ? 4 : st.ratchet) : 1;
		for (int i = 0; i < r; i++)
			push(t, at + (int64_t)(d * i / r + 0.5), st);
	}

	// Where a step's hits begin, against its nominal start: swing, then its offset.
	double lateness(int step, const Step& st, double d) const {
		if (d <= 0.0) return 0.0;
		double sw = (step & 1) ? (2.0 * swing - 1.0) * d : 0.0;
		return sw + st.offset * 0.01 * d;
	}

	// Track t moves to its next step, which nominally starts at `at` and lasts `d`.
	void advance(int t, double at, double d) {
		TrackState& s = ts[t];
		const Track& tr = pat[cur].t[t];
		int n = stepsOf(tr);
		int prev = s.step;
		s.step = (s.step + 1) % n;
		if (t == 0 && s.step == 0 && prev >= 0) { eoc = true; }
		const Step& st = tr.s[s.step];
		if (s.preFor == s.step) s.preFor = -1;		// queued a step ago
		else {
			double late = lateness(s.step, st, d);
			schedule(t, (int64_t)(at + (late > 0.0 ? late : 0.0) + 0.5), d, st);
		}
		// an early step after this one is queued now, ahead of its own start
		int nx = (s.step + 1) % n;
		const Step& ns = tr.s[nx];
		if (d > 0.0 && ns.on && lateness(nx, ns, d) < 0.0) {
			schedule(t, (int64_t)(at + d + lateness(nx, ns, d) + 0.5), d, ns);
			s.preFor = nx;
		}
	}

	// One clock tick: every track that steps on it, steps.
	void onTick() {
		tick++;
		if (want != cur && (!switchOnBar || tick % kPage == 0)) {
			cur = want;
			for (int t = 0; t < kTracks; t++) ts[t].preFor = -1;
		}
		for (int t = 0; t < kTracks; t++) {
			const Track& tr = pat[cur].t[t];
			int dv = tr.div < kNumDivs ? tr.div : kDivOne;
			int mul = divMul[dv], div = divDiv[dv];
			TrackState& s = ts[t];
			// a fractional rate (x3/2) steps on every div-th tick, mul times across them
			if (tick % div != 0) continue;
			double d = period > 0.0 ? period * div / mul : 0.0;
			advance(t, (double)now, d);
			s.subLeft = d > 0.0 ? mul - 1 : 0;
			s.subD = d;
			s.subNext = (double)now + d;
		}
	}

	/** One sample. `clockHigh`, `resetHigh`: rising edges are what count (Schmitt them
	    first). `running` false: the clock is ignored and what is queued still plays. */
	void process(bool clockEdge, bool resetEdge, bool running) {
		eoc = false;
		if (resetEdge) reset();
		if (!running && want != cur) cur = want;	// stopped: there is no boundary to wait for
		if (clockEdge && running) {
			if (lastEdge >= 0) {
				double p = (double)(now - lastEdge);
				if (p > 0.0) period = p;
			}
			lastEdge = now;
			onTick();
		}
		// a multiplied track's steps between the ticks
		if (running)
			for (int t = 0; t < kTracks; t++) {
				TrackState& s = ts[t];
				while (s.subLeft > 0 && (double)now >= s.subNext - 0.5) {
					s.subLeft--;
					advance(t, s.subNext, s.subD);
					s.subNext += s.subD;
				}
			}
		// the hits that are due
		const int trigLen = (int)(trigSeconds * sampleRate + 0.5f) < 1 ? 1 : (int)(trigSeconds * sampleRate + 0.5f);
		for (int t = 0; t < kTracks; t++) {
			TrackState& s = ts[t];
			for (int i = 0; i < s.qn;) {
				if (s.q[i].at > now) { i++; continue; }
				if (!mute[t]) {
					s.vel = s.q[i].vel * (10.f / 127.f);
					s.cv = s.q[i].cv * (10.f / 127.f);
					// every hit its own rising edge: a hit on a high trigger drops it first
					if (s.pulse > 0) s.gap = true;
					s.pulse = trigLen;
				}
				s.q[i] = s.q[--s.qn];
			}
			if (s.gap) { trig[t] = false; s.gap = false; }
			else if (s.pulse > 0) { trig[t] = true; s.pulse--; }
			else trig[t] = false;
		}
		if (eoc) eocPulse = trigLen;
		now++;
	}

	bool eocHigh() { if (eocPulse > 0) { eocPulse--; return true; } return false; }
};

// ---------------------------------------------------------------------------
// Patterns as text, for a patch: a track is its length, its division and then 12 hex
// digits a step (on, vel, chance, ratchet, offset + 128, cv), to its last changed step.

static inline void hex2(char* o, int v) {
	static const char h[] = "0123456789abcdef";
	o[0] = h[(v >> 4) & 15];
	o[1] = h[v & 15];
}
static inline int unhex(char c) {
	return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

// `out` must hold 4 + 12 * kSteps + 1 chars.
static inline void trackToText(const Track& tr, char* out) {
	int last = -1;
	Step d;
	for (int i = 0; i < kSteps; i++) if (!(tr.s[i] == d)) last = i;
	char* o = out;
	hex2(o, tr.length); o += 2;
	hex2(o, tr.div); o += 2;
	for (int i = 0; i <= last; i++) {
		const Step& s = tr.s[i];
		hex2(o, s.on); hex2(o + 2, s.vel); hex2(o + 4, s.chance);
		hex2(o + 6, s.ratchet); hex2(o + 8, s.offset + 128); hex2(o + 10, s.cv);
		o += 12;
	}
	*o = 0;
}

static inline bool trackFromText(const char* in, Track& tr) {
	tr.clear();
	size_t n = std::strlen(in);
	if (n < 4) return false;
	auto byte = [&](size_t i) { int a = unhex(in[i]), b = unhex(in[i + 1]); return a < 0 || b < 0 ? -1 : a * 16 + b; };
	int len = byte(0), dv = byte(2);
	if (len < 1 || len > kSteps || dv < 0 || dv >= kNumDivs) return false;
	tr.length = (uint8_t)len;
	tr.div = (uint8_t)dv;
	for (int i = 0; i < kSteps && 4 + 12 * (size_t)i + 12 <= n; i++) {
		size_t b = 4 + 12 * (size_t)i;
		int v[6];
		for (int k = 0; k < 6; k++) { v[k] = byte(b + 2 * k); if (v[k] < 0) return false; }
		Step& s = tr.s[i];
		s.set(kLaneGate, v[0]);
		s.set(kLaneVel, v[1]);
		s.set(kLaneChance, v[2]);
		s.set(kLaneRatchet, v[3]);
		s.set(kLaneOffset, v[4] - 128);
		s.set(kLaneCv, v[5]);
	}
	return true;
}

} // namespace drumseq

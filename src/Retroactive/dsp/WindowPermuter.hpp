#pragma once
// Retroactive — windowed sample-permutation engine.
//
// Rack-free, C++11, no allocation on the audio thread. This header is the single
// DSP core shared by the VCV module, the offline test harness, and (phase 2) a
// JUCE AudioProcessor. It must not include rack.hpp or use rack:: types.
//
// Every mode is a permutation f(i) of sample indices within a window:
//     f(i) = map[s]*B + (rev[s] ? (B-1-r) : r),   s = i/B, r = i%B, B = N/S
// The enum is a facade over map[]/rev[].

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

// Release fills the ring with 0. The test build overrides this with a sentinel so
// that a read of never-written memory is visible in the output (ASan catches
// out-of-bounds, but not uninitialised reads, and MSan is unavailable on macOS).
#ifndef RETROACTIVE_RING_FILL
#define RETROACTIVE_RING_FILL 0.f
#endif

namespace retroactive {

static const int   kMaxSubdiv     = 64;
static const float kMaxWindowSec  = 4.0f;
static const int64_t kMaxWindowSamples = 1 << 20;
static const float kMaxFadeSec    = 0.020f;
static const int   kFadeTableSize = 1024;   // entries 0..1024 inclusive
static const int   kHannTableSize = 4096;

/** Deterministic xorshift32. Seed is patch state, so SHUFFLE/SCATTER repeat. */
class Rng {
public:
	Rng() : s_(0x9e3779b9u) {}
	void seed(uint32_t s) { s_ = s ? s : 0x9e3779b9u; }
	uint32_t state() const { return s_; }
	uint32_t next() {
		uint32_t x = s_;
		x ^= x << 13; x ^= x >> 17; x ^= x << 5;
		s_ = x;
		return x;
	}
	/** Uniform in [0, n). */
	uint32_t below(uint32_t n) { return n ? (next() % n) : 0u; }
private:
	uint32_t s_;
};

inline int64_t nextPow2(int64_t v) {
	int64_t p = 1;
	while (p < v) p <<= 1;
	return p;
}


class WindowPermuter {
public:
	enum Mode {
		MODE_IDENTITY = 0,
		MODE_REVERSE,
		MODE_BLOCK_REVERSE,
		MODE_BLOCK_INTERNAL,
		MODE_BLOCK_SHUFFLE,
		MODE_PAIRWISE_SWAP,
		MODE_STUTTER,
		MODE_SCATTER,
		NUM_MODES
	};
	enum Character {
		CHAR_CROSSFADE = 0,
		CHAR_OVERLAP,
		NUM_CHARACTERS
	};

	WindowPermuter() {
		buildTables();
		sr_ = 44100.f;
		penN_ = 11025;
		penS_ = 1;
		penMode_ = MODE_REVERSE;
		penChar_ = CHAR_CROSSFADE;
		penFade_ = (int64_t)(0.003f * sr_);
		mix_ = 1.f;
		freeze_ = false;
		dryComp_ = false;
		equalPower_ = true;
		seed_ = 0x9e3779b9u;
		latencyDirty_ = false;
		setSampleRate(44100.f);
	}

	// ---------------------------------------------------------------- config

	/** Allocates and zeroes the ring. Idempotent — safe to call on module add. */
	void setSampleRate(float sr) {
		if (!(sr > 0.f)) sr = 44100.f;
		float oldSr = sr_;
		sr_ = sr;

		maxN_ = (int64_t)(kMaxWindowSec * sr);
		if (maxN_ > kMaxWindowSamples) maxN_ = kMaxWindowSamples;
		if (maxN_ < 64) maxN_ = 64;
		maxFade_ = (int64_t)(kMaxFadeSec * sr) + 1;

		// Room for: the N-sample source region, the N samples written while it is
		// read, the -G source offset, and the fade tails that walk past both ends.
		int64_t need = 3 * maxN_ + 4 * maxFade_ + 16;
		int64_t sz = nextPow2(need);
		if (sz != ringSize_ || ring_[0].size() != (size_t)sz) {
			ringSize_ = sz;
			ringMask_ = sz - 1;
			ring_[0].assign((size_t)sz, RETROACTIVE_RING_FILL);
			ring_[1].assign((size_t)sz, RETROACTIVE_RING_FILL);
		}

		// Keep the requested window/fade in seconds, not samples, across a rate change.
		if (oldSr > 0.f && oldSr != sr) {
			penN_ = (int64_t)((double)penN_ * (double)sr / (double)oldSr + 0.5);
			penFade_ = (int64_t)((double)penFade_ * (double)sr / (double)oldSr + 0.5);
		}
		clampPending();
		reset();
	}

	/** Zeroes the ring and the schedule; reseeds the RNG so patterns repeat. */
	void reset() {
		std::fill(ring_[0].begin(), ring_[0].end(), (float)RETROACTIVE_RING_FILL);
		std::fill(ring_[1].begin(), ring_[1].end(), (float)RETROACTIVE_RING_FILL);
		writeIdx_ = 0;
		curN_ = 0; curB_ = 0; curL_ = 0; curLReq_ = 0; curG_ = 0;
		curS_ = 1; curMode_ = penMode_; curChar_ = penChar_;
		srcBase_ = 0;
		nIdealErr_ = 0;
		i_ = 0; s_ = 0; r_ = 0;
		posA_ = posB_ = 0; dirA_ = dirB_ = 1;
		fadeN_ = 0; fadeK_ = 0;
		headValid_ = false;
		boundaryRequested_ = false;
		for (int k = 0; k < 2; k++) {
			grains_[k].active = false;
			grains_[k].pos = 0; grains_[k].dir = 1; grains_[k].n = 0; grains_[k].len = 1;
		}
		nextGrain_ = 0;
		rng_.seed(seed_);
		for (int k = 0; k < kMaxSubdiv; k++) { map_[k] = k; rev_[k] = false; }
		latencyDirty_ = true;
	}

	void setWindowSamples(int64_t n) { penN_ = n; clampPending(); }
	void setWindowSeconds(float sec) { penN_ = (int64_t)((double)sec * (double)sr_ + 0.5); clampPending(); }
	void setSubdivisions(int s) { penS_ = s < 1 ? 1 : (s > kMaxSubdiv ? kMaxSubdiv : s); }
	void setMode(int m) { penMode_ = (m < 0) ? 0 : (m >= NUM_MODES ? NUM_MODES - 1 : m); }
	void setCharacter(int c) { penChar_ = (c < 0) ? 0 : (c >= NUM_CHARACTERS ? NUM_CHARACTERS - 1 : c); }
	void setFadeSamples(int64_t n) { penFade_ = n; clampPending(); }
	void setFadeSeconds(float sec) { penFade_ = (int64_t)((double)sec * (double)sr_ + 0.5); clampPending(); }
	void setMix(float m) { mix_ = m < 0.f ? 0.f : (m > 1.f ? 1.f : m); }
	void setFreeze(bool f) { freeze_ = f; }
	void setDryCompensation(bool d) { dryComp_ = d; }
	void setEqualPowerFade(bool e) { equalPower_ = e; }

	/** Reseeds immediately and on every reset(). Saved in patch JSON. */
	void setSeed(uint32_t s) { seed_ = s ? s : 0x9e3779b9u; rng_.seed(seed_); }
	uint32_t getSeed() const { return seed_; }
	void reseed() { rng_.seed(seed_); }

	/** End the current window at the next processed sample (clock phase lock, RESET). */
	void forceBoundary() { boundaryRequested_ = true; }

	// -------------------------------------------------------------- readouts

	int64_t getWindowSamples() const { return curN_; }
	int     getSubdivisions() const { return curS_; }
	int64_t getBlockSamples() const { return curB_; }
	int64_t getFadeSamples() const { return curL_; }
	int64_t getLatencySamples() const { return curN_ + curG_; }
	float   getWindowSeconds() const { return sr_ > 0.f ? (float)curN_ / sr_ : 0.f; }
	float   getLatencySeconds() const { return sr_ > 0.f ? (float)(curN_ + curG_) / sr_ : 0.f; }
	float   getSampleRate() const { return sr_; }
	int     getMode() const { return curMode_; }
	int64_t getMaxWindowSamples() const {
		int64_t byRing = (ringSize_ - 4 * maxFade_ - 16) / 3;
		return std::min(maxN_, byRing);
	}
	/** 0..1 through the current window — drives the WINDOW light. */
	float getWindowPhase() const { return curN_ > 0 ? (float)i_ / (float)curN_ : 0.f; }
	/** Overdraft: more fade was requested than the sub-block can pay for, so L is
	    clamped to B/2. How much you hear depends on how many seams the mode makes
	    per window -- IDENTITY makes none and REVERSE makes one, so those two barely
	    change; the block-scrambling modes make up to S and smear noticeably. */
	bool isOverdraft() const { return curLReq_ > 0 && curB_ > 0 && curLReq_ * 2 >= curB_; }
	/** One-shot: latency changed since the last call (JUCE setLatencySamples). */
	bool consumeLatencyChanged() { bool d = latencyDirty_; latencyDirty_ = false; return d; }

	// --------------------------------------------------------------- process

	void process(float inL, float inR, float& outL, float& outR) {
		// 1. Capture. Frozen input stops the write head, so the same region loops.
		if (!freeze_) {
			size_t w = (size_t)(writeIdx_ & ringMask_);
			ring_[0][w] = inL;
			ring_[1][w] = inR;
		}

		// 2. Window boundary — the only place sizes and the map may change.
		if (i_ >= curN_ || boundaryRequested_)
			latchWindow();

		float wetL, wetR;

		if (curChar_ == CHAR_OVERLAP) {
			int64_t hop = curB_ / 2;
			if (hop < 1) hop = 1;
			if (i_ % hop == 0)
				launchGrain();

			wetL = 0.f; wetR = 0.f;
			float wsum = 0.f;
			for (int k = 0; k < 2; k++) {
				Grain& g = grains_[k];
				if (!g.active) continue;
				int64_t h = (g.n * kHannTableSize) / g.len;
				if (h < 0) h = 0;
				if (h > kHannTableSize - 1) h = kHannTableSize - 1;
				float w = hann_[h];
				wsum += w;
				wetL += w * rd(0, g.pos);
				wetR += w * rd(1, g.pos);
				g.pos += g.dir;
				g.n++;
				if (g.n >= g.len) g.active = false;
			}
			// Periodic Hann at a 50% hop is exactly COLA, so wsum == 1 in the steady
			// state and this is a no-op. It only bites when N changes mid-flight and
			// two grains of different lengths overlap: attenuate, never boost, so the
			// warm-up fade-in of a lone first grain is left alone.
			if (wsum > 1.f) {
				float g = 1.f / wsum;
				wetL *= g; wetR *= g;
			}
		}
		else {
			// 3. Sub-block boundary: retarget, and crossfade only on a genuine seam.
			if (r_ == 0) {
				int64_t np = srcBase_ + (int64_t)map_[s_] * curB_ + (rev_[s_] ? curB_ - 1 : 0);
				int nd = rev_[s_] ? -1 : +1;
				if (!headValid_) {
					posA_ = np; dirA_ = nd; fadeN_ = 0; headValid_ = true;
				}
				else if (np != posA_ || nd != dirA_) {
					if (curL_ > 0) { posB_ = np; dirB_ = nd; fadeN_ = curL_; fadeK_ = 0; }
					else           { posA_ = np; dirA_ = nd; fadeN_ = 0; }
				}
				// else: contiguous with the previous block (REVERSE always is) — no fade.
			}

			float aL = rd(0, posA_), aR = rd(1, posA_);
			if (fadeN_ > 0) {
				int64_t ti = (fadeN_ > 1) ? (fadeK_ * kFadeTableSize) / (fadeN_ - 1) : kFadeTableSize;
				if (ti > kFadeTableSize) ti = kFadeTableSize;
				const float* ta = equalPower_ ? gaEqual_ : gaLinear_;
				const float* tb = equalPower_ ? gbEqual_ : gbLinear_;
				float ga = ta[ti], gb = tb[ti];
				wetL = ga * aL + gb * rd(0, posB_);
				wetR = ga * aR + gb * rd(1, posB_);
			}
			else {
				wetL = aL; wetR = aR;
			}

			// The outgoing head keeps walking past the seam — that tail is what makes
			// a fade sound like a fade, and is why srcBase_ is offset by -G.
			posA_ += dirA_;
			if (fadeN_ > 0) {
				posB_ += dirB_;
				fadeK_++;
				if (fadeK_ >= fadeN_) { posA_ = posB_; dirA_ = dirB_; fadeN_ = 0; }
			}
		}

		// 4. Dry/wet.
		float dryL = inL, dryR = inR;
		if (dryComp_) {
			int64_t dp = writeIdx_ - curN_ - curG_;
			dryL = rd(0, dp);
			dryR = rd(1, dp);
		}
		if (mix_ <= 0.f)      { outL = dryL;                       outR = dryR; }
		else if (mix_ >= 1.f) { outL = wetL;                       outR = wetR; }
		else                  { outL = dryL + mix_ * (wetL - dryL); outR = dryR + mix_ * (wetR - dryR); }

		// 5. Advance.
		i_++;
		r_++;
		if (r_ >= curB_) {
			r_ = 0;
			if (s_ < curS_ - 1) s_++;
		}
		if (!freeze_) writeIdx_++;
	}

private:
	struct Grain { int64_t pos; int dir; int64_t n, len; bool active; };

	/** Reads absolute stream position `pos`. Pre-history is silence, not garbage. */
	inline float rd(int lane, int64_t pos) const {
		if (pos < 0) return 0.f;
		return ring_[lane][(size_t)(pos & ringMask_)];
	}

	void clampPending() {
		int64_t mx = getMaxWindowSamples();
		if (penN_ < 2) penN_ = 2;
		if (penN_ > mx) penN_ = mx;
		if (penFade_ < 0) penFade_ = 0;
		if (penFade_ > maxFade_) penFade_ = maxFade_;
	}

	void latchWindow() {
		i_ = 0; s_ = 0; r_ = 0;
		boundaryRequested_ = false;

		int64_t prevLatency = curN_ + curG_;

		curMode_ = penMode_;
		curChar_ = penChar_;
		curS_ = penS_;
		if (curS_ < 1) curS_ = 1;
		if (curS_ > kMaxSubdiv) curS_ = kMaxSubdiv;

		// N must be a multiple of S (of 2S under OVERLAP, so the B/2 hop is exact).
		// The truncation residue is carried so it never accumulates against the clock.
		int64_t q = (curChar_ == CHAR_OVERLAP) ? (int64_t)curS_ * 2 : (int64_t)curS_;
		int64_t want = penN_ + nIdealErr_;
		int64_t maxW = getMaxWindowSamples();
		int64_t n = (want / q) * q;
		if (n < q || n > maxW) {
			if (n < q) n = q;
			if (n > maxW) n = (maxW / q) * q;
			if (n < q) n = q;
			nIdealErr_ = 0;
		}
		else {
			nIdealErr_ = want - n;
		}
		curN_ = n;
		curB_ = curN_ / curS_;

		// L <= B/2 guarantees a fade can never reach the next seam, so the fade
		// state machine needs no re-entrancy handling. 0 is an exact hard cut.
		curLReq_ = penFade_;
		int64_t L = curLReq_;
		if (L > curB_ / 2) L = curB_ / 2;
		if (L > curN_ / 2) L = curN_ / 2;
		if (L > maxFade_) L = maxFade_;
		if (L < 2) L = 0;
		curL_ = L;

		// Source-region guard: the fade tail (or an overlap grain) walks forward past
		// the end of the region. Without this offset it reads unwritten memory.
		curG_ = curL_;
		if (curChar_ == CHAR_OVERLAP && curB_ / 2 > curG_) curG_ = curB_ / 2;

		srcBase_ = writeIdx_ - curN_ - curG_;

		buildMap();

		if (curN_ + curG_ != prevLatency) latencyDirty_ = true;
	}

	void buildMap() {
		const int S = curS_;
		int k;
		switch (curMode_) {
			case MODE_IDENTITY:
				for (k = 0; k < S; k++) { map_[k] = k; rev_[k] = false; }
				break;
			case MODE_REVERSE:
				// Substituting into f(i) gives exactly f(i) = N-1-i, for any S.
				for (k = 0; k < S; k++) { map_[k] = S - 1 - k; rev_[k] = true; }
				break;
			case MODE_BLOCK_REVERSE:
				for (k = 0; k < S; k++) { map_[k] = S - 1 - k; rev_[k] = false; }
				break;
			case MODE_BLOCK_INTERNAL:
				for (k = 0; k < S; k++) { map_[k] = k; rev_[k] = true; }
				break;
			case MODE_BLOCK_SHUFFLE: {
				for (k = 0; k < S; k++) { map_[k] = k; rev_[k] = false; }
				for (k = S - 1; k > 0; k--) {
					int j = (int)rng_.below((uint32_t)(k + 1));
					int t = map_[k]; map_[k] = map_[j]; map_[j] = t;
				}
			} break;
			case MODE_PAIRWISE_SWAP:
				for (k = 0; k < S; k++) {
					rev_[k] = false;
					map_[k] = (k == S - 1 && (S & 1)) ? k : (k ^ 1);
				}
				break;
			case MODE_STUTTER: {
				// Not a bijection: it repeats an R-block group, so no RMS assertions.
				// Emits a pitch at sr/B — deliberate.
				int R = S / 4; if (R < 1) R = 1;
				int s0 = (S > R) ? (int)rng_.below((uint32_t)(S - R + 1)) : 0;
				for (k = 0; k < S; k++) { map_[k] = s0 + (k % R); rev_[k] = false; }
			} break;
			case MODE_SCATTER:
				// Not a bijection either.
				for (k = 0; k < S; k++) {
					map_[k] = (int)rng_.below((uint32_t)S);
					rev_[k] = ((rng_.next() >> 16) & 1u) != 0u;
				}
				break;
			default:
				for (k = 0; k < S; k++) { map_[k] = k; rev_[k] = false; }
				break;
		}
		for (k = S; k < kMaxSubdiv; k++) { map_[k] = 0; rev_[k] = false; }
	}

	void launchGrain() {
		int slot;
		if (!grains_[0].active) slot = 0;
		else if (!grains_[1].active) slot = 1;
		else { slot = nextGrain_; nextGrain_ ^= 1; }

		int64_t r = (curB_ > 0) ? (i_ % curB_) : 0;
		int sIdx = (curB_ > 0) ? (int)(i_ / curB_) : 0;
		if (sIdx >= curS_) sIdx = curS_ - 1;
		if (sIdx < 0) sIdx = 0;

		Grain& g = grains_[slot];
		// The grain carries its own srcBase, baked in here: one launched late in
		// window k finishes during window k+1.
		g.pos = srcBase_ + (int64_t)map_[sIdx] * curB_ + (rev_[sIdx] ? curB_ - 1 - r : r);
		g.dir = rev_[sIdx] ? -1 : +1;
		g.n = 0;
		g.len = curB_ > 0 ? curB_ : 1;
		g.active = true;
	}

	void buildTables() {
		const double kPi = 3.14159265358979323846;
		for (int k = 0; k <= kFadeTableSize; k++) {
			double u = (double)k / (double)kFadeTableSize;
			gaEqual_[k]  = (float)std::cos(0.5 * kPi * u);
			gbEqual_[k]  = (float)std::sin(0.5 * kPi * u);
			gaLinear_[k] = (float)(1.0 - u);
			gbLinear_[k] = (float)u;
		}
		gaEqual_[0] = 1.f; gbEqual_[0] = 0.f;
		gaEqual_[kFadeTableSize] = 0.f; gbEqual_[kFadeTableSize] = 1.f;

		// Periodic Hann (not i/(len-1)); the top half is built as 1-bottom so that
		// w[n] + w[n + len/2] == 1 exactly and 50% overlap is exactly COLA.
		for (int k = 0; k < kHannTableSize / 2; k++)
			hann_[k] = (float)(0.5 - 0.5 * std::cos(2.0 * kPi * (double)k / (double)kHannTableSize));
		for (int k = kHannTableSize / 2; k < kHannTableSize; k++)
			hann_[k] = 1.f - hann_[k - kHannTableSize / 2];
	}

	// --- config ---
	float   sr_;
	int64_t maxN_, maxFade_;
	int64_t penN_, penFade_;
	int     penS_, penMode_, penChar_;
	float   mix_;
	bool    freeze_, dryComp_, equalPower_;
	uint32_t seed_;

	// --- storage ---
	std::vector<float> ring_[2];
	int64_t ringSize_, ringMask_;
	int64_t writeIdx_;

	// --- latched schedule (written only at window boundaries) ---
	int64_t curN_, curB_, curL_, curLReq_, curG_, srcBase_, nIdealErr_;
	int     curS_, curMode_, curChar_;
	int     map_[kMaxSubdiv];
	bool    rev_[kMaxSubdiv];

	// --- playback ---
	int64_t i_, r_;
	int     s_;
	int64_t posA_, posB_;
	int     dirA_, dirB_;
	int64_t fadeN_, fadeK_;
	bool    headValid_, boundaryRequested_, latencyDirty_;

	Grain grains_[2];
	int   nextGrain_;

	Rng rng_;

	float gaEqual_[kFadeTableSize + 1], gbEqual_[kFadeTableSize + 1];
	float gaLinear_[kFadeTableSize + 1], gbLinear_[kFadeTableSize + 1];
	float hann_[kHannTableSize];
};


/** Clock-period tracker: integer sample counter (dsp::TTimer is a float
    accumulator and drifts), jitter smoothing with a snap branch, and a stall
    timeout so stopped edges free-run instead of snapping back to the knob. */
class ClockSync {
public:
	ClockSync() : sr_(44100.f) { reset(); }

	void setSampleRate(float sr) { sr_ = (sr > 0.f) ? sr : 44100.f; reset(); }

	void reset() {
		counter_ = 0;
		period_ = 0;
		sawFirst_ = false;
		locked_ = false;
		stale_ = false;
		justLocked_ = false;
	}

	/** Call once per sample. `edge` is a rising clock edge. */
	void process(bool edge) {
		justLocked_ = false;
		counter_++;

		int64_t minP = (int64_t)(sr_ / 1000.f);   // 1000 Hz
		if (minP < 2) minP = 2;
		int64_t maxP = (int64_t)(sr_ / 0.05f);    // 0.05 Hz

		if (edge) {
			if (!sawFirst_) {
				// One edge cannot give a period — start the counter and free-run.
				sawFirst_ = true;
				counter_ = 0;
			}
			else {
				int64_t p = counter_;
				counter_ = 0;
				if (p >= minP && p <= maxP) {
					if (!locked_) {
						period_ = p;
						locked_ = true;
					}
					else {
						int64_t d = p - period_;
						int64_t tol = period_ / 8;   // 12.5%
						if (d <= tol && -d <= tol)
							period_ += d / 4;        // smooth by 0.25
						else
							period_ = p;             // snap: track tempo changes
					}
					stale_ = false;
					justLocked_ = true;
				}
				else {
					// Out of range: treat as a fresh start.
					sawFirst_ = true;
				}
			}
		}

		if (locked_) {
			int64_t timeout = 2 * period_;
			int64_t cap = (int64_t)(4.f * sr_);
			if (timeout > cap) timeout = cap;
			if (counter_ > timeout) stale_ = true;
		}
	}

	bool    isLocked() const { return locked_; }
	bool    justLocked() const { return justLocked_; }
	bool    isStale() const { return stale_; }
	int64_t period() const { return period_; }

private:
	float sr_;
	int64_t counter_, period_;
	bool sawFirst_, locked_, stale_, justLocked_;
};

} // namespace retroactive

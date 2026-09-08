#pragma once
#include <cmath>

// ---------------------------------------------------------------------------
// Bailout's routing, kept out of the .cpp so it can be tested.
//
// The summing itself is three lines and hard to get wrong. What is easy to get
// wrong is *where a strip's signal is allowed to arrive*, because every jack on
// the footer band steals rather than copies:
//
//   * a channel with its own direct out patched leaves its bank
//   * a bank with its MIX out patched leaves MAIN
//
// Both of those are subtractions, and the failure they invite is double
// counting -- a channel that goes to its direct out *and* still reaches MAIN
// through its bank, or a bank that is heard twice because the patch check was
// made against the wrong index. Neither announces itself: the module still
// makes a sound, just a louder one than the patch describes.

namespace bailout {

static const int kChannels = 8;
static const int kBanks = 2;
static const int kPerBank = 4;

/** The op-amps' own supply rails: TL07x output swing tops out a volt or so shy
    of a +/-12 V rail, and tanh gives that a soft knee rather than the hard clip
    a starved rail would not actually produce. */
static const float kRail = 11.f;

inline float softClip(float x, bool on) {
	return on ? kRail * std::tanh(x / kRail) : x;
}

/** A strip's gain. CV *multiplies* the knob rather than adding to it, so the
    knob is the depth and an unpatched jack leaves the channel alone -- which is
    what lets the same strip be a mixer channel or a VCA with no mode switch to
    say which it is being. Consolidation's four strips do exactly this; Bailout
    has eight of them. */
inline float levelOf(float knob, bool cvConnected, float cvVolts, bool muted) {
	if (muted)
		return 0.f;
	if (!cvConnected)
		return knob;
	float cv = cvVolts * 0.1f;
	if (!(cv > 0.f))                 // and this catches a NaN on the jack
		cv = 0.f;
	if (cv > 1.f)
		cv = 1.f;
	return knob * cv;
}

struct Routing {
	float direct[kChannels];
	float mix[kBanks];
	float main;
};

/** `v` is each strip's signal after its level. `directPatched` and
    `mixPatched` are what the patch has taken. Two summing stages, as the
    circuit has: each bank sums and clips into its own MIX, and MAIN sums the
    banks that are still free and clips again.

    Each bank carries its own second inverting stage, so MIX A and MIX B leave
    the module the same way round as MAIN. Consolidation spends that stage on an
    INV output instead, which is the one thing it has that this does not. */
inline void route(const float* v, const bool* directPatched,
                  const bool* mixPatched, bool soft, Routing& out) {
	for (int b = 0; b < kBanks; b++)
		out.mix[b] = 0.f;

	for (int i = 0; i < kChannels; i++) {
		out.direct[i] = directPatched[i] ? v[i] : 0.f;
		if (!directPatched[i])
			out.mix[i / kPerBank] += v[i];
	}

	float main = 0.f;
	for (int b = 0; b < kBanks; b++) {
		out.mix[b] = softClip(out.mix[b], soft);
		if (!mixPatched[b])
			main += out.mix[b];
	}
	out.main = softClip(main, soft);
}

} // namespace bailout

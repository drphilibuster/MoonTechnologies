// Bailout's routing, tested without Rack.
//
// Every jack on the footer band steals rather than copies: a channel with its
// direct out patched leaves its bank, and a bank with its MIX out patched
// leaves MAIN. Those are subtractions, and the bug they invite is double
// counting -- which does not announce itself, because the module still makes a
// sound, only a louder one than the patch describes. So the checks below are
// mostly conservation: what went in arrives once, in exactly one place.
//
// The routing is exhaustively enumerable -- 256 direct-patch combinations by 4
// mix-patch combinations -- so it is enumerated rather than sampled, one check
// per property over the whole space, with the first ten offending patches
// printed.

#include "../../src/Bailout/Bus.hpp"

#include <cmath>
#include <cstdio>

using namespace bailout;

static int checks = 0;
static int failures = 0;

static void check(const char* what, int bad, int total) {
	checks++;
	if (bad) {
		failures++;
		printf("  FAIL  %s: %d of %d\n", what, bad, total);
	}
}

/** A distinct, exactly representable value per strip, so any sum names its own
    terms: powers of two cannot collide the way equal values would. */
static void fill(float* v) {
	for (int i = 0; i < kChannels; i++)
		v[i] = (float)(1 << i);
}

static void unpack(int mask, bool* out, int n) {
	for (int i = 0; i < n; i++)
		out[i] = (mask >> i) & 1;
}

int main() {
	printf("Bailout routing\n");
	float v[kChannels];
	bool dp[kChannels], mp[kBanks];
	Routing r;
	fill(v);

	// --- nothing is heard twice, and nothing is lost ------------------------
	// Over every patch: each strip arrives at exactly one destination. Sum the
	// direct outs, the mixes that were taken, and MAIN, and the total has to be
	// the sum of all eight strips -- no more (double counting) and no less (a
	// strip routed nowhere). Soft clip is off here so the arithmetic is exact.
	{
		int bad = 0, shown = 0, total = 0;
		for (int d = 0; d < 256; d++) {
			for (int m = 0; m < 4; m++) {
				total++;
				unpack(d, dp, kChannels);
				unpack(m, mp, kBanks);
				route(v, dp, mp, false, r);
				double got = 0.0;
				for (int i = 0; i < kChannels; i++) got += r.direct[i];
				for (int b = 0; b < kBanks; b++) if (mp[b]) got += r.mix[b];
				got += r.main;
				double want = 0.0;
				for (int i = 0; i < kChannels; i++) want += v[i];
				if (std::fabs(got - want) > 1e-3) {
					bad++;
					if (shown++ < 10)
						printf("    direct 0x%02X mix 0x%X: %.1f, wanted %.1f\n",
						       d, m, got, want);
				}
			}
		}
		check("every strip arrives exactly once", bad, total);
	}

	// --- a direct out empties its own strip out of the bank ------------------
	// The specific subtraction, checked directly rather than only through the
	// conservation sum above: an unpatched bank's mix is exactly the strips in
	// it that are not patched away.
	{
		int bad = 0, shown = 0, total = 0;
		for (int d = 0; d < 256; d++) {
			total++;
			unpack(d, dp, kChannels);
			mp[0] = mp[1] = false;
			route(v, dp, mp, false, r);
			for (int b = 0; b < kBanks; b++) {
				double want = 0.0;
				for (int i = b * kPerBank; i < (b + 1) * kPerBank; i++)
					if (!dp[i]) want += v[i];
				if (std::fabs(r.mix[b] - want) > 1e-3) {
					bad++;
					if (shown++ < 10)
						printf("    direct 0x%02X bank %d: mix %.1f, wanted %.1f\n",
						       d, b, r.mix[b], want);
					break;
				}
			}
		}
		check("a patched direct out leaves its bank's mix", bad, total);
	}

	// --- a strip only ever reaches its own bank ------------------------------
	// Channel 5 must not be audible in MIX A. An off-by-one in the bank index
	// would pass the conservation check above -- the totals would still add up
	// -- while putting half the module in the wrong output.
	{
		int bad = 0, shown = 0;
		for (int i = 0; i < kChannels; i++) {
			float one[kChannels] = {0,0,0,0,0,0,0,0};
			one[i] = 5.f;
			for (int k = 0; k < kChannels; k++) dp[k] = false;
			mp[0] = mp[1] = false;
			route(one, dp, mp, false, r);
			int wrong = 1 - (i / kPerBank);
			if (!(std::fabs(r.mix[i / kPerBank] - 5.f) < 1e-3
			      && std::fabs(r.mix[wrong]) < 1e-6)) {
				bad++;
				if (shown++ < 10)
					printf("    channel %d: mixA %.2f mixB %.2f\n",
					       i + 1, r.mix[0], r.mix[1]);
			}
		}
		check("a strip reaches only its own bank", bad, kChannels);
	}

	// --- a patched MIX takes its bank out of MAIN ----------------------------
	// And takes only its own: patching MIX A must not quiet bank B.
	{
		int bad = 0, shown = 0, total = 0;
		for (int m = 0; m < 4; m++) {
			total++;
			for (int k = 0; k < kChannels; k++) dp[k] = false;
			unpack(m, mp, kBanks);
			route(v, dp, mp, false, r);
			double want = 0.0;
			for (int b = 0; b < kBanks; b++) {
				if (mp[b]) continue;
				for (int i = b * kPerBank; i < (b + 1) * kPerBank; i++) want += v[i];
			}
			if (std::fabs(r.main - want) > 1e-3) {
				bad++;
				if (shown++ < 10)
					printf("    mix 0x%X: main %.1f, wanted %.1f\n", m, r.main, want);
			}
		}
		check("a patched MIX leaves MAIN", bad, total);
	}

	// --- a taken MIX still carries its bank ----------------------------------
	// Stealing from MAIN must not mean stealing from the jack you patched: the
	// bank still has to come out of MIX A, or the cable does nothing.
	{
		int bad = 0;
		for (int k = 0; k < kChannels; k++) dp[k] = false;
		mp[0] = true; mp[1] = false;
		route(v, dp, mp, false, r);
		double wantA = 0.0;
		for (int i = 0; i < kPerBank; i++) wantA += v[i];
		if (std::fabs(r.mix[0] - wantA) > 1e-3) bad++;
		check("a taken MIX still carries its own bank", bad, 1);
	}

	// --- the level law -------------------------------------------------------
	// CV multiplies rather than adds, so an unpatched jack is not a silent
	// channel; mute wins over any CV; and the jack cannot drive the strip past
	// its knob or below zero, either of which would make a level control that
	// is not one.
	{
		int bad = 0;
		if (levelOf(0.7f, false, 0.f, false) != 0.7f) bad++;      // unpatched
		if (levelOf(0.7f, false, 9.f, true) != 0.f) bad++;        // mute wins
		if (levelOf(0.7f, true, 9.f, true) != 0.f) bad++;         // even with CV
		if (levelOf(0.8f, true, 10.f, false) != 0.8f) bad++;      // 10 V is unity
		if (levelOf(0.8f, true, 0.f, false) != 0.f) bad++;        // 0 V is closed
		if (std::fabs(levelOf(0.8f, true, 5.f, false) - 0.4f) > 1e-6) bad++;
		if (levelOf(0.8f, true, -5.f, false) != 0.f) bad++;       // no negatives
		if (levelOf(0.8f, true, 25.f, false) != 0.8f) bad++;      // never past the knob
		check("CV multiplies the knob, mute beats it, neither exceeds it", bad, 8);
	}

	// --- the rails hold ------------------------------------------------------
	// Eight strips at 10 V sum to 80 V, which no op-amp on a 12 V rail produces.
	// With soft clip on, nothing may leave above the rail; with it off the sum
	// is honest and the module's own clamp deals with it.
	{
		float loud[kChannels];
		for (int i = 0; i < kChannels; i++) loud[i] = 10.f;
		for (int k = 0; k < kChannels; k++) dp[k] = false;
		mp[0] = mp[1] = false;
		route(loud, dp, mp, true, r);
		int bad = 0;
		if (!(std::fabs(r.mix[0]) < kRail && std::fabs(r.mix[1]) < kRail
		      && std::fabs(r.main) < kRail)) bad++;
		// Odd symmetry: the clipper must not add a DC offset.
		for (int i = 0; i < kChannels; i++) loud[i] = -10.f;
		Routing n;
		route(loud, dp, mp, true, n);
		if (std::fabs(n.main + r.main) > 1e-4) bad++;
		check("soft clip holds the rail and stays symmetric", bad, 2);
	}

	printf("%s  %d checks, %d failures\n", failures ? "FAILED" : "ok",
	       checks, failures);
	return failures ? 1 : 0;
}

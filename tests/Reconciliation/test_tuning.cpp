// Reconciliation's tuning tables and its search.
//
// The tables are the risk here, not the arithmetic. Forty-three fractions and
// a thirty-six cell diamond typed out by hand go wrong silently: a transposed
// digit still builds, still quantizes, and still sounds like a just scale --
// just not the one on the box. So none of this checks ratios one at a time
// against a list copied from the same source. It checks the properties the
// people who designed these structures said they have:
//
//   * Partch's 43 has no step smaller than 121/120 (14.4 cents) or larger than
//     45/44 (38.9 cents). One mistyped ratio breaks that on both sides of
//     itself.
//   * The 11-limit diamond has exactly 29 distinct pitches, and every interval
//     in it has its own inversion present -- Partch built it symmetrically.
//   * The 43 contains the diamond.
//   * A hexany has six pitches, an eikosany twenty.
//
// The rankings are checked against values published with the definitions:
// Euler's gradus of 3/2 is 4 and of 5/4 is 7; Barlow's indigestibility of 3 is
// 8/3 and of 5 is 32/5; Tenney's harmonic distance of 3/2 is log2 6.

#include <cmath>
#include <cstdio>

#include "../../src/Reconciliation/Tuning.hpp"

using namespace tuning;

static int checks = 0;
static int failures = 0;

static void check(bool ok, const char* what) {
	checks++;
	if (!ok) {
		failures++;
		printf("  FAIL  %s\n", what);
	}
}

static bool near(float a, float b, float eps) {
	return std::fabs(a - b) <= eps;
}

// --- T1: ratio arithmetic ----------------------------------------------------

static void t1_ratios() {
	Ratio a; a.n = 6; a.d = 4;
	check(reduce(a) == (Ratio{3, 2}), "6/4 reduces to 3/2");

	Ratio big; big.n = 693; big.d = 1;               // the eikosany's largest product
	const Ratio f = octaveReduce(big);
	check(f.n == 693 && f.d == 512, "693/1 folds to 693/512");
	check(cents(f) > 0.f && cents(f) < 1200.f, "and lands inside one octave");

	Ratio third; third.n = 5; third.d = 4;
	check(near(cents(third), 386.31f, 0.02f), "5/4 is 386.31 cents");
	check(near(cents(Ratio{3, 2}), 701.955f, 0.02f), "3/2 is 701.955 cents");
	check(mirror(Ratio{3, 2}) == (Ratio{4, 3}), "the mirror of 3/2 is 4/3");
	check(mirror(Ratio{5, 4}) == (Ratio{8, 5}), "the mirror of 5/4 is 8/5");
	check(mirror(Ratio{1, 1}) == (Ratio{1, 1}), "the unison is its own mirror");

	check(largestPrime(1) == 1, "1 has no prime factor");
	check(largestPrime(81) == 3, "81 is a power of 3");
	check(largestPrime(160) == 5, "160 = 2^5 * 5");
	check(primeLimit(Ratio{160, 81}) == 5, "160/81 is a 5-limit ratio");
	check(primeLimit(Ratio{11, 8}) == 11, "11/8 is an 11-limit ratio");
}

// --- T2: Partch's 43 ---------------------------------------------------------

static void t2_partch43() {
	Scale s;
	s.build(SET_PARTCH43, false, 11u);
	printf("T2  Partch 43: %d pitches\n", s.n);
	check(s.n == 43, "the 43-tone scale has 43 pitches");

	// The property the scale was designed around. 121/120 is 14.367 cents and
	// 45/44 is 38.906; the tolerance below is a hundredth of a cent, so a
	// mistyped ratio has nowhere to hide.
	const float minStep = 1200.f * std::log2(121.f / 120.f);
	const float maxStep = 1200.f * std::log2(45.f / 44.f);
	float worstSmall = 1e9f, worstLarge = 0.f;
	int smallAt = -1, largeAt = -1;
	for (int i = 0; i < s.n; i++) {
		const float next = (i + 1 < s.n) ? s.c[i + 1] : 1200.f;
		const float step = next - s.c[i];
		if (step < worstSmall) { worstSmall = step; smallAt = i; }
		if (step > worstLarge) { worstLarge = step; largeAt = i; }
	}
	printf("T2  steps: smallest %.3f cents (after %u/%u), largest %.3f (after %u/%u)\n",
	       worstSmall, s.r[smallAt].n, s.r[smallAt].d,
	       worstLarge, s.r[largeAt].n, s.r[largeAt].d);
	check(worstSmall >= minStep - 0.01f, "no step is smaller than 121/120");
	check(worstLarge <= maxStep + 0.01f, "no step is larger than 45/44");
	check(near(worstSmall, minStep, 0.01f), "and the smallest step is exactly 121/120");
	check(near(worstLarge, maxStep, 0.01f), "and the largest is exactly 45/44");

	// Ascending, distinct, and inside the octave.
	bool ordered = true, inOctave = true;
	for (int i = 0; i < s.n; i++) {
		inOctave = inOctave && s.c[i] >= 0.f && s.c[i] < 1200.f;
		if (i && !(s.c[i] > s.c[i - 1]))
			ordered = false;
	}
	check(ordered, "the 43 are strictly ascending");
	check(inOctave, "and all lie in [0, 1200) cents");

	// Partch built it symmetrically: the mirror of every degree is a degree.
	bool symmetric = true;
	for (int i = 0; i < s.n; i++) {
		const Ratio m = mirror(s.r[i]);
		bool found = false;
		for (int j = 0; j < s.n && !found; j++)
			found = (s.r[j] == m);
		if (!found) {
			printf("  FAIL  43-tone scale has %u/%u but not its mirror %u/%u\n",
			       s.r[i].n, s.r[i].d, m.n, m.d);
			symmetric = false;
		}
	}
	check(symmetric, "every degree of the 43 has its own inversion in the scale");
}

// --- T3: the diamond ---------------------------------------------------------

static void t3_diamond() {
	Scale d;
	d.build(SET_DIAMOND, false, 11u);
	printf("T3  11-limit diamond: %d pitches\n", d.n);
	check(d.n == 29, "the 11-limit tonality diamond has 29 distinct pitches");

	bool symmetric = true;
	for (int i = 0; i < d.n; i++) {
		const Ratio m = mirror(d.r[i]);
		bool found = false;
		for (int j = 0; j < d.n && !found; j++)
			found = (d.r[j] == m);
		symmetric = symmetric && found;
	}
	check(symmetric, "the diamond is its own mirror");

	// Partch's 43 is the diamond plus fourteen more.
	Scale s;
	s.build(SET_PARTCH43, false, 11u);
	int missing = 0;
	for (int i = 0; i < d.n; i++) {
		bool found = false;
		for (int j = 0; j < s.n && !found; j++)
			found = (s.r[j] == d.r[i]);
		if (!found) {
			printf("  FAIL  the 43 is missing the diamond's %u/%u\n", d.r[i].n, d.r[i].d);
			missing++;
		}
	}
	check(missing == 0, "the 43-tone scale contains the whole diamond");
	check(s.n - d.n == 14, "and adds exactly fourteen multiple-number ratios");

	// Every member is a ratio of two identities, which is what a diamond is.
	bool fromIdentities = true;
	for (int i = 0; i < d.n; i++) {
		bool ok = false;
		for (int a = 0; a < NUM_IDENTITIES && !ok; a++)
			for (int b = 0; b < NUM_IDENTITIES && !ok; b++) {
				Ratio x; x.n = IDENTITIES[a]; x.d = IDENTITIES[b];
				ok = (octaveReduce(x) == d.r[i]);
			}
		fromIdentities = fromIdentities && ok;
	}
	check(fromIdentities, "and every member is one identity over another");
}

// --- T4: the hexads ----------------------------------------------------------

static void t4_hexads() {
	Scale o, u;
	o.build(SET_HEXAD, false, 11u);
	u.build(SET_HEXAD, true, 11u);
	check(o.n == 6, "the Otonality is a hexad");
	check(u.n == 6, "so is the Utonality");

	// 4:5:6:7:9:11, octave-reduced and sorted.
	const Ratio wantO[6] = {{1,1}, {9,8}, {5,4}, {11,8}, {3,2}, {7,4}};
	const Ratio wantU[6] = {{1,1}, {8,7}, {4,3}, {16,11}, {8,5}, {16,9}};
	bool okO = true, okU = true;
	for (int i = 0; i < 6; i++) {
		okO = okO && (o.r[i] == wantO[i]);
		okU = okU && (u.r[i] == wantU[i]);
	}
	printf("T4  Otonality:");
	for (int i = 0; i < o.n; i++) printf(" %u/%u", o.r[i].n, o.r[i].d);
	printf("\nT4  Utonality:");
	for (int i = 0; i < u.n; i++) printf(" %u/%u", u.r[i].n, u.r[i].d);
	printf("\n");
	check(okO, "the Otonality is 1/1 9/8 5/4 11/8 3/2 7/4");
	check(okU, "the Utonality is 1/1 8/7 4/3 16/11 8/5 16/9");

	// Partch's point: the two are reflections of one another.
	bool reflected = true;
	for (int i = 0; i < 6; i++) {
		const Ratio m = mirror(o.r[i]);
		bool found = false;
		for (int j = 0; j < 6 && !found; j++)
			found = (u.r[j] == m);
		reflected = reflected && found;
	}
	check(reflected, "the Utonality is the Otonality's mirror, pitch for pitch");

	// Both live inside the diamond, being one row and one column of it.
	Scale d;
	d.build(SET_DIAMOND, false, 11u);
	bool inside = true;
	for (int i = 0; i < 6; i++) {
		bool fo = false, fu = false;
		for (int j = 0; j < d.n; j++) {
			fo = fo || (d.r[j] == o.r[i]);
			fu = fu || (d.r[j] == u.r[i]);
		}
		inside = inside && fo && fu;
	}
	check(inside, "both hexads are rows of the diamond");
}

// --- T5: Wilson's combination product sets -----------------------------------

static void t5_cps() {
	Scale h;
	h.build(SET_HEXANY, false, 11u);
	printf("T5  1-3-5-7 hexany:");
	for (int i = 0; i < h.n; i++) printf(" %u/%u", h.r[i].n, h.r[i].d);
	printf("\n");
	check(h.n == 6, "the 2)4 hexany has six pitches");
	check(h.r[0] == (Ratio{1, 1}), "normalised onto its own lowest member");

	// The published 1-3-5-7 hexany, normalised: 1/1 8/7 6/5 48/35 8/5 12/7.
	const Ratio want[6] = {{1,1}, {8,7}, {6,5}, {48,35}, {8,5}, {12,7}};
	bool ok = true;
	for (int i = 0; i < 6; i++)
		ok = ok && (h.r[i] == want[i]);
	check(ok, "and is 1/1 8/7 6/5 48/35 8/5 12/7");

	Scale e;
	e.build(SET_EIKOSANY, false, 11u);
	printf("T5  eikosany: %d pitches\n", e.n);
	check(e.n == 20, "the 3)6 eikosany has twenty pitches");
	check(e.r[0] == (Ratio{1, 1}), "also normalised onto its lowest");

	bool ordered = true;
	for (int i = 1; i < e.n; i++)
		ordered = ordered && e.c[i] > e.c[i - 1];
	check(ordered, "and is strictly ascending");
}

// --- T6: the rankings --------------------------------------------------------

static void t6_rankings() {
	// Euler, Tentamen (1739): Gamma(1) = 1, Gamma(3/2) = 4, Gamma(5/4) = 7.
	check(eulerGradus(Ratio{1, 1}) == 1, "Euler's gradus of the unison is 1");
	check(eulerGradus(Ratio{2, 1}) == 2, "of the octave is 2");
	check(eulerGradus(Ratio{3, 2}) == 4, "of the fifth is 4");
	check(eulerGradus(Ratio{5, 4}) == 7, "of the major third is 7");
	check(eulerGradus(Ratio{6, 5}) == 8, "of the minor third is 8");

	// Tenney: log2(n*d).
	check(near(tenneyHD(Ratio{1, 1}), 0.f, 1e-5f), "Tenney's distance to the unison is 0");
	check(near(tenneyHD(Ratio{3, 2}), std::log2(6.f), 1e-5f), "and to the fifth is log2 6");
	check(tenneyHD(Ratio{11, 8}) > tenneyHD(Ratio{3, 2}), "11/8 is further out than 3/2");

	// Barlow: 2(p-1)^2/p.
	check(near(indigestibility(2), 1.f, 1e-4f), "Barlow's indigestibility of 2 is 1");
	check(near(indigestibility(3), 8.f / 3.f, 1e-4f), "of 3 is 2.667");
	check(near(indigestibility(5), 32.f / 5.f, 1e-4f), "of 5 is 6.4");
	check(near(indigestibility(7), 72.f / 7.f, 1e-4f), "of 7 is 10.286");
	check(near(indigestibility(15), indigestibility(3) + indigestibility(5), 1e-4f),
	      "and a composite is the sum over its prime factors");
	check(near(indigestibility(9), 2.f * indigestibility(3), 1e-4f),
	      "counted with multiplicity");

	// Where Barlow and Tenney disagree, and why both are on the panel: 9/8 has
	// larger numbers than 7/4 but is built of easier primes.
	check(tenneyHD(Ratio{9, 8}) > tenneyHD(Ratio{7, 4}), "Tenney puts 7/4 nearer than 9/8");
	check(barlowHarmonicity(Ratio{9, 8}) > barlowHarmonicity(Ratio{7, 4}),
	      "Barlow puts 9/8 ahead of 7/4 -- the two rules genuinely disagree");

	// All four must agree that the unison is the simplest thing in the set.
	DissonanceCurve curve;
	curve.build(220.f, 7);
	for (int rule = 0; rule < NUM_RULES; rule++) {
		Scale s;
		s.build(SET_DIAMOND, false, 11u);
		s.rankBy(rule, curve);
		int bestAt = 0;
		for (int i = 1; i < s.n; i++)
			if (s.quality[i] > s.quality[bestAt])
				bestAt = i;
		bool ranged = true;
		for (int i = 0; i < s.n; i++)
			ranged = ranged && s.quality[i] >= 0.f && s.quality[i] <= 1.f
			         && std::isfinite(s.quality[i]);
		check(ranged, "every rule scores into [0, 1]");
		check(s.r[bestAt] == (Ratio{1, 1}), "and every rule calls the unison the simplest");
	}
}

// --- T7: Sethares' curve -----------------------------------------------------

static void t7_dissonance() {
	DissonanceCurve curve;
	curve.build(220.f, 7);

	bool ranged = true;
	for (int i = 0; i < DissonanceCurve::N; i++)
		ranged = ranged && std::isfinite(curve.v[i]) && curve.v[i] >= 0.f && curve.v[i] <= 1.f;
	check(ranged, "the dissonance curve is finite and normalised");

	// Sethares' result: for a harmonic timbre the minima of the curve fall on
	// the small-integer ratios. The unison is trivially zero; the interesting
	// claim is that the octave and the fifth are local minima -- lower than
	// their own neighbourhoods -- which is what makes just intonation follow
	// from the spectrum rather than from the numbers.
	const float fifth = curve.at(701.955f);
	const float octave = curve.at(1200.f);
	float worstNear5th = 0.f;
	for (float c = 650.f; c <= 755.f; c += 1.f)
		worstNear5th = std::fmax(worstNear5th, curve.at(c));
	printf("T7  harmonic timbre, 7 partials: 3/2 %.4f, worst within 50 cents %.4f, 2/1 %.4f\n",
	       fifth, worstNear5th, octave);
	check(fifth < worstNear5th, "the fifth is a local minimum of the dissonance curve");

	bool fifthWins = true;
	for (float c = 660.f; c <= 745.f; c += 1.f)
		if (std::fabs(c - 701.955f) > 8.f && curve.at(c) < fifth)
			fifthWins = false;
	check(fifthWins, "and nothing within 40 cents of it is smoother");

	// A tritone should be rougher than a fifth for a harmonic timbre.
	check(curve.at(600.f) > fifth, "the tempered tritone is rougher than the fifth");

	// Sethares' thesis proper, and the reason the timbre is a setting rather
	// than a constant: the minima are put there by *coinciding partials*, so
	// they exist for a harmonic timbre and do not exist at all for a sine.
	// Counting local minima says that far better than comparing two heights --
	// a pair of sines a fifth apart is simply past its roughness peak and still
	// falling, which is not the same thing as a minimum.
	int harmonicMinima = 0, pureMinima = 0;
	DissonanceCurve pure;
	pure.build(220.f, 1);
	for (int i = 101; i < DissonanceCurve::N - 1; i++) {
		if (curve.v[i] < curve.v[i - 1] && curve.v[i] <= curve.v[i + 1]) harmonicMinima++;
		if (pure.v[i] < pure.v[i - 1] && pure.v[i] <= pure.v[i + 1]) pureMinima++;
	}
	printf("T7  local minima above 100 cents: 7 partials %d, one partial %d\n",
	       harmonicMinima, pureMinima);
	check(pureMinima == 0, "a pair of sines has no dissonance minimum anywhere");
	check(harmonicMinima >= 4, "a harmonic timbre has several");

	// And every one of them lands on a ratio from Partch's diamond -- not near
	// one, on one, inside two cents. That is the claim: just intonation falls
	// out of the spectrum rather than being imposed on it.
	Scale d;
	d.build(SET_DIAMOND, false, 11u);
	bool onJust = true;
	printf("T7  7-partial minima:");
	for (int i = 101; i < DissonanceCurve::N - 1; i++) {
		if (!(curve.v[i] < curve.v[i - 1] && curve.v[i] <= curve.v[i + 1]))
			continue;
		int nearest = 0;
		for (int j = 1; j < d.n; j++)
			if (std::fabs(d.c[j] - (float) i) < std::fabs(d.c[nearest] - (float) i))
				nearest = j;
		const float err = std::fabs(d.c[nearest] - (float) i);
		printf(" %d=%u/%u(%+.1f)", i, d.r[nearest].n, d.r[nearest].d, (float) i - d.c[nearest]);
		if (err > 2.f)
			onJust = false;
	}
	printf("\n");
	check(onJust, "and each minimum sits on a diamond ratio, within two cents");

	// The number of them follows the timbre, which is the part of Sethares'
	// argument that a fixed table of ratios cannot express. 5/4 is the telling
	// case: it is only an inflection until there are enough partials for the
	// fifth harmonic of one tone to meet the fourth of the other and pull the
	// curve below its own downward slope.
	int prevCount = -1;
	const int partials[4] = {1, 4, 7, 12};
	bool grows = true;
	for (int k = 0; k < 4; k++) {
		DissonanceCurve t;
		t.build(220.f, partials[k]);
		int count = 0;
		for (int i = 101; i < DissonanceCurve::N - 1; i++)
			if (t.v[i] < t.v[i - 1] && t.v[i] <= t.v[i + 1])
				count++;
		printf("T7  %2d partials -> %d minima\n", partials[k], count);
		if (count < prevCount)
			grows = false;
		prevCount = count;
	}
	check(grows, "a brighter timbre never has fewer consonances than a duller one");
}

// --- T8: the search ----------------------------------------------------------

static void t8_quantise() {
	DissonanceCurve curve;
	curve.build(220.f, 7);

	Scale s;
	s.build(SET_DIAMOND, false, 11u);
	s.rankBy(RULE_NEAREST, curve);

	// Idempotent: quantizing a pitch already in the scale returns it unchanged.
	bool fixed = true;
	for (int oct = -3; oct <= 3; oct++) {
		for (int i = 0; i < s.n; i++) {
			const float x = (float) oct * 1200.f + s.c[i];
			const Choice ch = quantise(s, x, 0.f, 0.f);
			if (!near(ch.cents, x, 0.01f) || ch.index != i || ch.octave != oct) {
				printf("  FAIL  %u/%u at octave %d came back as %u/%u at %d\n",
				       s.r[i].n, s.r[i].d, oct, s.r[ch.index].n, s.r[ch.index].d, ch.octave);
				fixed = false;
			}
		}
	}
	check(fixed, "quantizing a scale degree returns that degree, in its own octave");

	// Never further than half the largest gap, and always finite, over a wide
	// sweep that runs well outside the tabulated octave in both directions.
	bool sane = true;
	float worst = 0.f;
	for (float x = -3600.f; x <= 3600.f; x += 0.7f) {
		const Choice ch = quantise(s, x, 0.f, 0.f);
		const float err = std::fabs(ch.cents - x);
		if (!std::isfinite(ch.cents) || err > 100.f) {
			printf("  FAIL  %.1f cents quantized to %.1f\n", x, ch.cents);
			sane = false;
			break;
		}
		worst = std::fmax(worst, err);
	}
	printf("T8  diamond, nearest: worst error over +-3 octaves %.2f cents\n", worst);
	check(sane, "the search is finite and close everywhere, inside and outside the octave");

	// Monotone: a rising input never produces a falling output.
	bool monotone = true;
	float prev = -1e9f;
	for (float x = -2400.f; x <= 2400.f; x += 0.5f) {
		const Choice ch = quantise(s, x, 0.f, 0.f);
		if (ch.cents < prev - 0.01f)
			monotone = false;
		prev = ch.cents;
	}
	check(monotone, "and never inverts a rising input");

	// BIAS at zero must be exactly NEAREST, whatever the rule and window. This
	// is what makes the five rules comparable against a control.
	bool control = true;
	for (int rule = 0; rule < NUM_RULES; rule++) {
		Scale t;
		t.build(SET_PARTCH43, false, 11u);
		t.rankBy(rule, curve);
		for (float x = -1200.f; x <= 2400.f; x += 3.1f) {
			const Choice a = quantise(t, x, 200.f, 0.f);
			const Choice b = quantise(t, x, 0.f, 0.f);
			if (a.index != b.index || a.octave != b.octave)
				control = false;
		}
	}
	check(control, "bias 0 is exactly NEAREST for every rule");

	// With bias up, a rule that prefers simple ratios must actually move the
	// answer toward simpler ones -- on average, over the whole octave.
	{
		Scale t;
		t.build(SET_PARTCH43, false, 11u);
		t.rankBy(RULE_TENNEY, curve);
		double plainHD = 0.0, biasedHD = 0.0;
		int count = 0;
		for (float x = 0.f; x < 1200.f; x += 1.f) {
			plainHD += tenneyHD(t.r[quantise(t, x, 60.f, 0.f).index]);
			biasedHD += tenneyHD(t.r[quantise(t, x, 60.f, 1.f).index]);
			count++;
		}
		plainHD /= count;
		biasedHD /= count;
		printf("T8  Partch 43, window 60c: mean harmonic distance %.3f -> %.3f with bias\n",
		       plainHD, biasedHD);
		check(biasedHD < plainHD - 0.3f, "TENNEY at full bias pulls the answer toward simpler ratios");
	}

	// The window is a hard bound: nothing may be chosen further from the input
	// than the window, unless it is the nearest candidate there is.
	{
		Scale t;
		t.build(SET_HEXAD, false, 11u);
		t.rankBy(RULE_BARLOW, curve);
		bool bounded = true;
		for (float x = -1200.f; x <= 2400.f; x += 1.3f) {
			const Choice near0 = quantise(t, x, 0.f, 0.f);
			const Choice ch = quantise(t, x, 50.f, 1.f);
			const float d = std::fabs(ch.cents - x);
			if (d > 50.f + 1e-3f && !near(ch.cents, near0.cents, 1e-3f))
				bounded = false;
		}
		check(bounded, "and the window is a hard bound on how far the rule may reach");
	}
}

// --- T9: the prime filter ----------------------------------------------------

static void t9_prime() {
	DissonanceCurve curve;
	curve.build(220.f, 7);
	const uint32_t primes[4] = {3u, 5u, 7u, 11u};

	for (int set = 0; set < NUM_SETS; set++) {
		int prevN = 0;
		for (int p = 0; p < 4; p++) {
			for (int u = 0; u < 2; u++) {
				Scale s;
				s.build(set, u != 0, primes[p]);
				s.rankBy(RULE_SETHARES, curve);
				check(s.n >= 1, "no setting can leave the scale empty");
				bool within = true, ordered = true, sized = true;
				for (int i = 0; i < s.n; i++) {
					within = within && primeLimit(s.r[i]) <= primes[p];
					sized = sized && s.r[i].n <= (1u << 22) && s.r[i].d <= (1u << 22);
					if (i && !(s.c[i] > s.c[i - 1]))
						ordered = false;
				}
				if (!within)
					printf("  FAIL  set %d prime %u kept an out-of-limit ratio\n", set, primes[p]);
				check(within, "every kept ratio is inside the prime limit");
				check(ordered, "the filtered scale stays ordered and distinct");
				check(sized, "and no numerator ran away");
				if (u == 0) {
					if (p > 0)
						check(s.n >= prevN, "raising the prime limit never removes a pitch");
					prevN = s.n;
				}
			}
		}
	}

	// At the 3 limit every set collapses to Pythagorean material.
	Scale three;
	three.build(SET_DIAMOND, false, 3u);
	printf("T9  diamond at the 3 limit:");
	for (int i = 0; i < three.n; i++) printf(" %u/%u", three.r[i].n, three.r[i].d);
	printf("\n");
	check(three.n == 5, "the diamond at the 3 limit is five pitches");

	// 13 lives in exactly one place, and the prime knob is what removes it.
	Scale s11, s13;
	s13.build(SET_SERIES, false, 13u);
	s11.build(SET_SERIES, false, 11u);
	printf("T9  harmonic series 8-15: %d pitches at the 13 limit, %d at the 11\n", s13.n, s11.n);
	check(s13.n == 8, "the harmonic series segment has eight pitches");
	check(s11.n == 7, "and the 11 limit takes the 13th harmonic away");
}


// --- T10: degree transposition and hysteresis --------------------------------

static void t10_degree_and_hysteresis() {
	DissonanceCurve curve;
	curve.build(220.f, 7);
	Scale s;
	s.build(SET_PARTCH43, false, 11u);
	s.rankBy(RULE_NEAREST, curve);

	// Walking the set and walking back is the identity.
	bool roundTrip = true, octaveExact = true, ordered = true;
	for (int i = 0; i < s.n; i++) {
		Choice at;
		at.index = i;
		at.octave = 0;
		at.cents = s.c[i];
		for (int d = -20; d <= 20; d++) {
			const Choice moved = transposeDegrees(s, at, d);
			const Choice back = transposeDegrees(s, moved, -d);
			if (back.index != at.index || back.octave != at.octave
			    || std::fabs(back.cents - at.cents) > 1e-3f)
				roundTrip = false;
			if (d > 0 && !(moved.cents > at.cents - 1e-3f))
				ordered = false;
		}
		// A whole set's worth of degrees is exactly one octave.
		const Choice up = transposeDegrees(s, at, s.n);
		if (std::fabs(up.cents - (at.cents + 1200.f)) > 1e-3f)
			octaveExact = false;
	}
	check(roundTrip, "transposing by n degrees and back by n is the identity");
	check(octaveExact, "a whole set of degrees is exactly an octave");
	check(ordered, "and a positive transposition never goes down");

	// Hysteresis. A ramp crossing a boundary must cross it at a different input
	// going up from going down -- that gap is what hysteresis is -- and the gap
	// has to be about twice the setting.
	Scale h;
	h.build(SET_HEXAD, false, 11u);
	h.rankBy(RULE_NEAREST, curve);
	const float frac = 0.2f;
	const float gap = h.c[1] - h.c[0];               // 1/1 to 9/8

	Choice prev;
	bool have = false;
	float upAt = 0.f, downAt = 0.f;
	int lastIdx = -1;
	for (float x = 20.f; x < 300.f; x += 0.05f) {    // through the 1/1 - 9/8 boundary
		Choice p = withHysteresis(quantise(h, x, 0.f, 0.f), prev, have, x, frac);
		if (have && p.index != lastIdx)
			upAt = x;
		prev = p; have = true; lastIdx = p.index;
	}
	for (float x = 300.f; x > 20.f; x -= 0.05f) {
		Choice p = withHysteresis(quantise(h, x, 0.f, 0.f), prev, have, x, frac);
		if (p.index != lastIdx)
			downAt = x;
		prev = p; lastIdx = p.index;
	}
	printf("T10 hexad 1/1-9/8 boundary (gap %.1f) at hysteresis %.2f: up at %.1f, down at %.1f\n",
	       gap, frac, upAt, downAt);
	check(upAt > downAt, "the boundary is higher going up than coming down");
	check(std::fabs((upAt - downAt) - frac * gap) < 1.f,
	      "and the dead band is that fraction of the step");

	// The cap exists so that no setting can make a degree unreachable: at the
	// widest hysteresis every degree of the 43 must still be selectable.
	{
		bool allReachable = true;
		for (int i = 0; i < s.n; i++) {
			Choice pv; bool hv = false;
			bool landed = false;
			for (float x = -60.f; x < 1260.f; x += 0.25f) {
				Choice p = withHysteresis(quantise(s, x, 0.f, 0.f), pv, hv, x, 0.5f);
				pv = p; hv = true;
				if (p.index == i && p.octave == 0)
					landed = true;
			}
			if (!landed) {
				printf("  FAIL  %u/%u is unreachable at full hysteresis\n", s.r[i].n, s.r[i].d);
				allReachable = false;
			}
		}
		check(allReachable, "no hysteresis setting makes a degree of the 43 unreachable");
	}

	// The point of it: a noisy input sitting on a boundary must not chatter.
	unsigned long seed = 7u;
	int chatterOff = 0, chatterOn = 0;
	for (int pass = 0; pass < 2; pass++) {
		const float hh = pass ? frac : 0.f;
		Choice pv; bool hv = false; int li = -1; int flips = 0;
		const float boundary = 0.5f * (h.c[0] + h.c[1]);
		for (int i = 0; i < 20000; i++) {
			seed = seed * 1103515245u + 12345u;
			const float noise = ((float) ((seed >> 16) & 0x7FFFu) / 32768.f - 0.5f) * 8.f;
			const float x = boundary + noise;
			Choice p = withHysteresis(quantise(h, x, 0.f, 0.f), pv, hv, x, hh);
			if (hv && p.index != li)
				flips++;
			pv = p; hv = true; li = p.index;
		}
		(pass ? chatterOn : chatterOff) = flips;
	}
	printf("T10 sitting on a boundary under +-4 cents of noise: %d flips at 0, %d at %.2f\n",
	       chatterOff, chatterOn, frac);
	check(chatterOff > 1000, "with no hysteresis a noisy input chatters");
	check(chatterOn == 0, "with hysteresis it does not");
}


int main() {
	t1_ratios();
	t2_partch43();
	t3_diamond();
	t4_hexads();
	t5_cps();
	t6_rankings();
	t7_dissonance();
	t8_quantise();
	t9_prime();
	t10_degree_and_hysteresis();
	printf("\n%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}

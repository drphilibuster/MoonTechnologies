#pragma once
// Reconciliation's engine: just-intonation pitch sets, four published measures
// of how consonant a ratio is, and the search that turns a voltage into one of
// them.
//
// The sets are Partch's. His eleven-limit tonality diamond is every ratio i/j
// with i and j drawn from the identities 1, 3, 5, 7, 9 and 11 -- thirty-six
// cells, twenty-nine distinct pitches once the unisons and the 9/3 duplicates
// are removed -- and his 43-tone scale is those twenty-nine plus fourteen
// "multiple-number ratios" that fill the gaps. A row of the diamond, one
// denominator held constant, is an Otonality: the 4:5:6:7:9:11 hexad, Partch's
// analogue of a major chord. A column, one numerator held constant, is a
// Utonality, its minor. Partch chose eleven because the eleventh harmonic is
// the first that is utterly foreign to Western ears.
//
//   H. Partch, "Genesis of a Music", 2nd ed. (Da Capo, 1974).
//
// Beside them are two of Erv Wilson's combination product sets -- the hexany
// (products of the pairs from four factors) and the eikosany (the triples from
// six) -- which are harmonically symmetric and, unlike the diamond, imply no
// tonic at all.
//
// The rankings are four different published answers to "which of these ratios
// is simpler", and they disagree, which is the point of having all four:
//
//   Euler's gradus suavitatis (Tentamen novae theoriae musicae, 1739)
//   James Tenney's harmonic distance, log2(n*d)
//   Clarence Barlow's harmonicity, from the indigestibility 2(p-1)^2/p
//   Sethares' sensory dissonance, after Plomp and Levelt (1965)
//
// Only the last of those knows anything about timbre, and that is Sethares'
// whole argument: which intervals sound good is a fact about the spectrum you
// are playing them with, not about the numbers alone.
//
// Rack-free on purpose, like src/Collusion/Swarm.hpp: tests/Reconciliation
// compiles the shipping tables and the shipping search with a host compiler.

#include <cmath>
#include <cstdint>

namespace tuning {

// --- ratios ------------------------------------------------------------------

/** A frequency ratio, kept in lowest terms.

    Constructors rather than default member initialisers, because this plugin
    builds as C++11 and a default member initialiser would stop Ratio being an
    aggregate -- which is what lets the 43-tone table below be written as a
    braced list of fractions and read like one. */
struct Ratio {
	uint32_t n, d;
	Ratio() : n(1u), d(1u) {}
	Ratio(uint32_t nn, uint32_t dd) : n(nn), d(dd) {}
	bool operator==(const Ratio& o) const { return n == o.n && d == o.d; }
	bool operator!=(const Ratio& o) const { return !(*this == o); }
};

inline uint32_t gcd32(uint32_t a, uint32_t b) {
	while (b) { const uint32_t t = a % b; a = b; b = t; }
	return a ? a : 1u;
}

inline Ratio reduce(Ratio r) {
	if (r.n == 0u) r.n = 1u;
	if (r.d == 0u) r.d = 1u;
	const uint32_t g = gcd32(r.n, r.d);
	r.n /= g;
	r.d /= g;
	return r;
}

/** Folds a ratio into one octave, [1, 2). Reduces at every step rather than at
    the end, so the eikosany's products -- which reach 693 before folding -- do
    not run a numerator up into six figures on the way. */
inline Ratio octaveReduce(Ratio r) {
	r = reduce(r);
	int guard = 0;
	while (r.n >= 2u * r.d && guard++ < 64) { r.d *= 2u; r = reduce(r); }
	guard = 0;
	while (r.n < r.d && guard++ < 64) { r.n *= 2u; r = reduce(r); }
	return r;
}

inline Ratio mul(Ratio a, Ratio b) {
	Ratio r; r.n = a.n * b.n; r.d = a.d * b.d; return reduce(r);
}

inline Ratio div(Ratio a, Ratio b) {
	Ratio r; r.n = a.n * b.d; r.d = a.d * b.n; return reduce(r);
}

/** The utonal mirror: the interval that completes the octave. */
inline Ratio mirror(Ratio r) {
	Ratio two; two.n = 2u; two.d = 1u;
	return octaveReduce(div(two, r));
}

inline float cents(Ratio r) {
	return 1200.f * std::log2((float) r.n / (float) r.d);
}

// --- the identities ----------------------------------------------------------

static const int NUM_IDENTITIES = 6;
//! Partch's six: the odd numbers up to eleven.
static const uint32_t IDENTITIES[NUM_IDENTITIES] = {1u, 3u, 5u, 7u, 9u, 11u};

// --- how simple is a ratio ---------------------------------------------------

/** The largest prime factor of n. 1 has none, and returns 1. */
inline uint32_t largestPrime(uint32_t n) {
	uint32_t best = 1u;
	for (uint32_t p = 2u; p * p <= n; p++) {
		while (n % p == 0u) { best = p; n /= p; }
	}
	return (n > 1u) ? ((n > best) ? n : best) : best;
}

/** A ratio's prime limit: the largest prime in either term. */
inline uint32_t primeLimit(Ratio r) {
	const uint32_t a = largestPrime(r.n), b = largestPrime(r.d);
	return (a > b) ? a : b;
}

/** Tenney's harmonic distance: log2(n*d) for a reduced ratio. Zero for the
    unison and rising with the product of the terms, which is as close as any
    of these measures gets to being simply "how big are the numbers".

    J. Tenney, "John Cage and the Theory of Harmony" (1983); the metric is
    developed further in his "A History of 'Consonance' and 'Dissonance'". */
inline float tenneyHD(Ratio r) {
	r = reduce(r);
	return std::log2((float) r.n * (float) r.d);
}

/** Euler's gradus suavitatis -- "degree of pleasantness". For n = prod p_i^a_i,
    Gamma(n) = 1 + sum a_i (p_i - 1); for a reduced ratio it is Gamma(n*d).
    Gamma(1/1) = 1, Gamma(3/2) = 4, Gamma(5/4) = 7.

    L. Euler, "Tentamen novae theoriae musicae" (St Petersburg, 1739), ch. IV. */
inline int eulerGradus(Ratio r) {
	r = reduce(r);
	uint64_t m = (uint64_t) r.n * (uint64_t) r.d;
	int g = 1;
	for (uint64_t p = 2u; p * p <= m; p++) {
		while (m % p == 0u) { g += (int) (p - 1u); m /= p; }
	}
	if (m > 1u)
		g += (int) (m - 1u);
	return g;
}

/** Barlow's indigestibility of a prime: 2(p-1)^2/p. x(2) = 1, x(3) = 2.667,
    x(5) = 6.4, x(7) = 10.286, x(11) = 18.18. For a composite it is the sum
    over the prime factorisation, with multiplicity.

    Note what this does that Tenney's metric does not: it rates a number by the
    primes *in* it rather than by its size, so 9 (two 3s, 5.33) is easier than
    7 (10.29) even though it is larger. That disagreement is the reason both
    rules are on the panel. */
inline float indigestibility(uint32_t n) {
	float sum = 0.f;
	for (uint32_t p = 2u; p * p <= n; p++) {
		while (n % p == 0u) {
			const float fp = (float) p;
			sum += 2.f * (fp - 1.f) * (fp - 1.f) / fp;
			n /= p;
		}
	}
	if (n > 1u) {
		const float fp = (float) n;
		sum += 2.f * (fp - 1.f) * (fp - 1.f) / fp;
	}
	return sum;
}

/** Barlow's harmonicity, as a magnitude: the reciprocal of the summed
    indigestibility of the two terms. The unison has no indigestibility at all,
    so it is given the largest finite value rather than an infinity.

    Barlow's own harmonicity also carries a polarity -- a sign that separates
    the "major" from the "minor" side of a ratio. Reconciliation ranks by
    magnitude only, and says so rather than pretending to a convention it does
    not use.

    C. Barlow, "Bus Journey to Parametron" (Feedback Papers 21-23, 1980);
    "On the Quantification of Harmony and Metre" (1987). */
inline float barlowHarmonicity(Ratio r) {
	r = reduce(r);
	const float x = indigestibility(r.n) + indigestibility(r.d);
	return (x > 1e-6f) ? (1.f / x) : 1e6f;
}

// --- sensory dissonance ------------------------------------------------------

/** Plomp and Levelt's roughness between two sinusoids, in Sethares'
    parameterisation: d = a1 a2 [exp(-3.5 s df) - exp(-5.75 s df)], with
    s = 0.24 / (0.0207 f_min + 18.96). The scaling by the lower frequency is
    the critical band: the same interval in hertz is far rougher low down than
    it is high up, which is why this measure has anything to say that counting
    the numerator does not.

    R. Plomp and W. J. M. Levelt, "Tonal consonance and critical bandwidth",
    JASA 38 (1965), 548-560. W. A. Sethares, "Local consonance and the
    relationship between timbre and scale", JASA 94 (1993), 1218-1228. */
inline float roughness(float f1, float f2, float a1, float a2) {
	const float lo = (f1 < f2) ? f1 : f2;
	const float df = (f1 < f2) ? (f2 - f1) : (f1 - f2);
	const float s = 0.24f / (0.0207f * lo + 18.96f);
	const float x = s * df;
	return a1 * a2 * (std::exp(-3.5f * x) - std::exp(-5.75f * x));
}

/** The total dissonance of one interval, as a function of its width in cents,
    for two copies of one harmonic timbre.

    Sethares' argument is that a scale is not a fact about numbers but about
    the spectrum you play it with: the minima of this curve are the intervals
    that sound settled, and for a harmonic timbre they land on the small-integer
    ratios -- which is *why* just intonation works, not a coincidence beside it.
    Change the partials and the minima move.

    Tabulated once per cent and interpolated, because rebuilding it costs
    partials^2 exponentials a cent and the search needs it every sample. */
struct DissonanceCurve {
	static const int N = 1201;         // one entry a cent, 0 .. 1200
	static const int MAX_PARTIALS = 16;
	float v[N];
	float builtHz = 0.f;
	int builtPartials = 0;

	DissonanceCurve() {
		for (int i = 0; i < N; i++)
			v[i] = 0.f;
	}

	/** Amplitudes fall as 1/k, the standard harmonic timbre in Sethares'
	    own figures. */
	void build(float rootHz, int partials) {
		if (!(rootHz > 1.f))
			rootHz = 220.f;
		if (partials < 1) partials = 1;
		if (partials > MAX_PARTIALS) partials = MAX_PARTIALS;
		builtHz = rootHz;
		builtPartials = partials;

		float f[MAX_PARTIALS], a[MAX_PARTIALS];
		for (int k = 0; k < partials; k++) {
			f[k] = rootHz * (float) (k + 1);
			a[k] = 1.f / (float) (k + 1);
		}

		float worst = 1e-9f;
		for (int c = 0; c < N; c++) {
			const float ratio = std::exp2((float) c / 1200.f);
			float sum = 0.f;
			for (int i = 0; i < partials; i++) {
				for (int j = 0; j < partials; j++)
					sum += roughness(f[i], f[j] * ratio, a[i], a[j]);
			}
			v[c] = sum;
			if (sum > worst)
				worst = sum;
		}
		for (int c = 0; c < N; c++)
			v[c] /= worst;             // 0 .. 1
	}

	/** Linearly interpolated, clamped to the tabulated octave. */
	float at(float c) const {
		if (!(c > 0.f)) return v[0];
		if (c >= (float) (N - 1)) return v[N - 1];
		const int i = (int) c;
		const float t = c - (float) i;
		return v[i] + (v[i + 1] - v[i]) * t;
	}
};

// --- the pitch sets ----------------------------------------------------------

enum SetId {
	SET_DIAMOND  = 0,   //!< Partch's 11-limit tonality diamond, 29 pitches
	SET_PARTCH43 = 1,   //!< Partch's 43-tone scale
	SET_HEXAD    = 2,   //!< one Otonality or Utonality, 6 pitches
	SET_HEXANY   = 3,   //!< Wilson's 2)4 combination product set on 1-3-5-7
	SET_EIKOSANY = 4,   //!< Wilson's 3)6 set on 1-3-5-7-9-11
	SET_SERIES   = 5,   //!< the harmonic (or undertone) series, 8 through 15
	NUM_SETS     = 6,
};

enum RuleId {
	RULE_NEAREST  = 0,
	RULE_TENNEY   = 1,
	RULE_BARLOW   = 2,
	RULE_EULER    = 3,
	RULE_SETHARES = 4,
	RULE_ADAPTIVE = 5,
	NUM_RULES     = 6,
};

static const int MAX_PITCHES = 48;

/** Partch's 43, in his own order. The fourteen ratios that are not in the
    diamond -- 81/80, 33/32, 21/20, 16/15, 32/27, 21/16, 27/20 and their seven
    mirrors -- are the "multiple-number ratios" he added so that a complete set
    of chords could be built on one tonic.

    Typing forty-three fractions by hand is exactly the kind of thing that goes
    wrong silently, so the test does not check them one by one: it checks the
    property Partch designed the scale to have. No step in it is smaller than
    121/120 (14.4 cents) or larger than 45/44 (38.9 cents), and any single
    mistyped ratio breaks that on both sides of itself. */
static const Ratio PARTCH_43[43] = {
	{  1,  1}, { 81, 80}, { 33, 32}, { 21, 20}, { 16, 15}, { 12, 11}, { 11, 10},
	{ 10,  9}, {  9,  8}, {  8,  7}, {  7,  6}, { 32, 27}, {  6,  5}, { 11,  9},
	{  5,  4}, { 14, 11}, {  9,  7}, { 21, 16}, {  4,  3}, { 27, 20}, { 11,  8},
	{  7,  5}, { 10,  7}, { 16, 11}, { 40, 27}, {  3,  2}, { 32, 21}, { 14,  9},
	{ 11,  7}, {  8,  5}, { 18, 11}, {  5,  3}, { 27, 16}, { 12,  7}, {  7,  4},
	{ 16,  9}, {  9,  5}, { 20, 11}, { 11,  6}, { 15,  8}, { 40, 21}, { 64, 33},
	{160, 81},
};

/** One pitch set, resolved: ratios ascending by cents, with each one's score
    under whichever rule is selected, normalised so that 1 is the most consonant
    member of *this* set and 0 the least. Normalising per set is what makes the
    BIAS knob mean the same thing whether the choice is between six pitches or
    forty-three. */
struct Scale {
	Ratio r[MAX_PITCHES];
	float c[MAX_PITCHES];        //!< cents above the root, ascending
	float quality[MAX_PITCHES];  //!< 0 .. 1, 1 = most consonant under the rule
	int n = 0;

	void clear() { n = 0; }

	void add(Ratio x) {
		x = octaveReduce(x);
		for (int i = 0; i < n; i++)
			if (r[i] == x)
				return;                     // the diamond's own duplicates
		if (n < MAX_PITCHES) {
			r[n] = x;
			c[n] = cents(x);
			quality[n] = 0.f;
			n++;
		}
	}

	void sortByCents() {
		for (int i = 1; i < n; i++) {       // insertion sort; n <= 48, once per change
			const Ratio kr = r[i];
			const float kc = c[i];
			int j = i - 1;
			while (j >= 0 && c[j] > kc) {
				r[j + 1] = r[j];
				c[j + 1] = c[j];
				j--;
			}
			r[j + 1] = kr;
			c[j + 1] = kc;
		}
	}

	/** Divide every member by the lowest, so the set starts on 1/1.

	    A combination product set has no tonic -- that is Wilson's point, and
	    the raw 1-3-5-7 hexany starts on 35/32 rather than on a unison. A
	    quantizer has to put *something* at the root, so the structure is
	    normalised onto its own lowest member; the intervals inside it, which
	    are what the set is, are untouched. */
	void normaliseToLowest() {
		if (n == 0)
			return;
		sortByCents();
		const Ratio base = r[0];
		if (base.n == base.d)
			return;
		for (int i = 0; i < n; i++) {
			r[i] = octaveReduce(div(r[i], base));
			c[i] = cents(r[i]);
		}
		sortByCents();
	}

	/** Build `set`, mirrored into its utonal form if asked, then keep only the
	    ratios whose prime limit is at or below `prime`. */
	void build(int set, bool utonal, uint32_t prime) {
		clear();
		switch (set) {
			case SET_PARTCH43:
				for (int i = 0; i < 43; i++)
					add(PARTCH_43[i]);
				break;

			case SET_HEXAD: {
				// The 1-Otonality is the identities over unity; the 1-Utonality
				// is unity over the identities. Every other Otonality and
				// Utonality in the diamond is one of these two transposed, which
				// is what NEXUS does -- see the module.
				for (int i = 0; i < NUM_IDENTITIES; i++) {
					Ratio x;
					x.n = utonal ? 1u : IDENTITIES[i];
					x.d = utonal ? IDENTITIES[i] : 1u;
					add(x);
				}
				break;
			}

			case SET_HEXANY: {
				static const uint32_t F[4] = {1u, 3u, 5u, 7u};
				for (int i = 0; i < 4; i++)
					for (int j = i + 1; j < 4; j++) {
						Ratio x; x.n = F[i] * F[j]; x.d = 1u;
						add(x);
					}
				normaliseToLowest();
				break;
			}

			case SET_EIKOSANY: {
				for (int i = 0; i < NUM_IDENTITIES; i++)
					for (int j = i + 1; j < NUM_IDENTITIES; j++)
						for (int k = j + 1; k < NUM_IDENTITIES; k++) {
							Ratio x;
							x.n = IDENTITIES[i] * IDENTITIES[j] * IDENTITIES[k];
							x.d = 1u;
							add(x);
						}
				normaliseToLowest();
				break;
			}

			case SET_SERIES:
				// Harmonics 8 through 15 over 8, or the same eight undertones.
				// The only place 13 appears anywhere in the module, and the
				// PRIME knob is what takes it away again.
				for (uint32_t k = 8u; k <= 15u; k++) {
					Ratio x;
					x.n = utonal ? 16u : k;
					x.d = utonal ? k : 8u;
					add(x);
				}
				break;

			case SET_DIAMOND:
			default:
				// Every i/j from the six identities. Thirty-six cells, twenty-nine
				// distinct pitches: the six unisons collapse to one, and 9/3 and
				// 3/9 repeat 3/1 and 1/3.
				for (int i = 0; i < NUM_IDENTITIES; i++)
					for (int j = 0; j < NUM_IDENTITIES; j++) {
						Ratio x; x.n = IDENTITIES[i]; x.d = IDENTITIES[j];
						add(x);
					}
				break;
		}

		// The utonal mirror. The diamond and the 43 are their own mirrors --
		// Partch built both symmetrically, so every interval's inversion is
		// already present -- and the hexad and the series build their utonal
		// form directly above, so this only has work to do for the two
		// combination product sets.
		if (utonal && (set == SET_HEXANY || set == SET_EIKOSANY)) {
			Scale m;
			m.clear();
			for (int i = 0; i < n; i++)
				m.add(mirror(r[i]));
			m.normaliseToLowest();
			*this = m;
		}

		// The prime filter. Applied last, so it prunes whatever the set turned
		// out to be rather than interfering with how it was built.
		if (prime < 2u)
			prime = 2u;
		int keep = 0;
		for (int i = 0; i < n; i++) {
			if (primeLimit(r[i]) <= prime) {
				r[keep] = r[i];
				c[keep] = c[i];
				keep++;
			}
		}
		n = keep;
		if (n == 0) {                       // never leave the module with nothing
			Ratio one; one.n = 1u; one.d = 1u;
			r[0] = one; c[0] = 0.f; n = 1;
		}
		sortByCents();
	}

	/** Score every member under `rule` and normalise the scores onto [0, 1].
	    Called when the set, the rule or the dissonance curve changes -- never
	    per sample. */
	void rankBy(int rule, const DissonanceCurve& curve) {
		float raw[MAX_PITCHES];
		for (int i = 0; i < n; i++) {
			switch (rule) {
				case RULE_TENNEY:   raw[i] = -tenneyHD(r[i]); break;
				case RULE_BARLOW:   raw[i] = barlowHarmonicity(r[i]); break;
				case RULE_EULER:    raw[i] = -(float) eulerGradus(r[i]); break;
				case RULE_SETHARES: raw[i] = -curve.at(c[i]); break;
				// ADAPTIVE looks for the purest interval from a moving reference,
				// and "purest" there is Tenney's answer.
				case RULE_ADAPTIVE: raw[i] = -tenneyHD(r[i]); break;
				// NEAREST does not consult the ranking -- the module forces its
				// bias to zero -- but it is still scored, because the PURITY
				// output reports how consonant the chosen degree is and a jack
				// that reads a flat ten volts in one mode is a broken jack.
				case RULE_NEAREST:
				default:            raw[i] = -tenneyHD(r[i]); break;
			}
			if (!std::isfinite(raw[i]))
				raw[i] = 0.f;
		}
		float lo = raw[0], hi = raw[0];
		for (int i = 1; i < n; i++) {
			if (raw[i] < lo) lo = raw[i];
			if (raw[i] > hi) hi = raw[i];
		}
		const float span = hi - lo;
		for (int i = 0; i < n; i++)
			quality[i] = (span > 1e-9f) ? ((raw[i] - lo) / span) : 1.f;
	}
};

/** What the search settled on. */
struct Choice {
	int index = 0;        //!< which member of the scale
	int octave = 0;       //!< how many octaves above the root
	float cents = 0.f;    //!< total cents above the root, octave included
};

/** Quantise `x` cents above the root.

    `window` is how far, in cents, the search may reach past the nearest
    candidate; `bias` is how hard the rule's own preference pulls against plain
    distance. At bias zero every rule collapses to NEAREST, which is the control
    the other five are worth comparing against. The nearest candidate is always
    in the running whatever the window is, so there is always an answer. */
inline Choice quantise(const Scale& s, float x, float window, float bias) {
	Choice best;
	if (s.n <= 0 || !std::isfinite(x))
		return best;

	if (window < 0.f) window = 0.f;
	if (bias < 0.f) bias = 0.f;
	if (bias > 1.f) bias = 1.f;

	// The octave the input falls in, and its neighbours: a pitch just under the
	// octave has to be able to snap up to the next 1/1.
	const int base = (int) std::floor(x / 1200.f);

	// Two passes, because "the nearest is always eligible" and "prefer the best
	// score inside the window" are two different questions and answering both
	// in one loop makes the answer depend on the order the candidates come in.
	Choice nearest;
	float nearestDist = 1e30f;
	bool haveWindowed = false;
	float bestScore = 1e30f;

	for (int k = -1; k <= 1; k++) {
		const int oct = base + k;
		for (int i = 0; i < s.n; i++) {
			const float cand = (float) oct * 1200.f + s.c[i];
			const float dist = std::fabs(cand - x);
			if (dist > 700.f)
				continue;

			if (dist < nearestDist) {
				nearestDist = dist;
				nearest.index = i;
				nearest.octave = oct;
				nearest.cents = cand;
			}
			if (dist <= window) {
				const float score = dist - bias * window * s.quality[i];
				if (score < bestScore) {
					bestScore = score;
					haveWindowed = true;
					best.index = i;
					best.octave = oct;
					best.cents = cand;
				}
			}
		}
	}
	return haveWindowed ? best : nearest;
}

/** Transpose a choice by `degree` steps along the set.

    Stepping off either end wraps into the next octave, which is what makes this
    a transposition rather than a clamp: at DIAMOND one step is whatever the next
    ratio happens to be -- anywhere from fourteen cents to a whole tone -- so
    walking the structure is a different move from transposing across it. */
inline Choice transposeDegrees(const Scale& s, Choice pick, int degree) {
	if (degree == 0 || s.n <= 0)
		return pick;
	int j = pick.index + degree;
	const int carry = (int) std::floor((float) j / (float) s.n);
	j -= carry * s.n;
	if (j < 0) j = 0;
	if (j >= s.n) j = s.n - 1;
	Choice out;
	out.index = j;
	out.octave = pick.octave + carry;
	out.cents = (float) out.octave * 1200.f + s.c[j];
	return out;
}

/** Hold the previous choice unless the new one is closer to the input by more
    than `fraction` of the gap between the two.

    Without this a quantizer sitting on a boundary dithers between its two
    neighbours on whatever noise is in the input. What makes the threshold a
    fraction of the local gap rather than a count of cents is that these sets
    are wildly uneven: a hexad steps by two hundred cents and Partch's 43 by
    fourteen, and any absolute setting wide enough to steady the one is wide
    enough to make the other unreachable. At `fraction` = 0.5 the dead band is
    half a step whatever the step happens to be, which is as far as it can go
    and still let every degree be selected.

    The dead band's total width works out at `fraction` times the gap: the
    difference of the two distances changes at twice the rate the input does,
    so the boundary moves half that far in each direction. `x` is the same
    input the choices were made from. */
inline Choice withHysteresis(Choice pick, Choice prev, bool havePrev, float x, float fraction) {
	if (!havePrev || fraction <= 0.f)
		return pick;
	if (pick.index == prev.index && pick.octave == prev.octave)
		return pick;
	const float gap = std::fabs(pick.cents - prev.cents);
	const float dNew = std::fabs(pick.cents - x);
	const float dOld = std::fabs(prev.cents - x);
	return ((dOld - dNew) < fraction * gap) ? prev : pick;
}

} // namespace tuning

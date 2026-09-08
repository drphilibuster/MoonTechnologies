// Toll's modal voice, tested without Rack.
//
// The same five things Kickback's suite checks, because it is the same physics
// and the same faults are available -- a coefficient written per sample rather
// than per second, a resonator pumped by its own amplitude, a strike that never
// reaches the output -- plus the two this voice has that a drum does not:
//
//   * Every partial set has to be *heard* as a different object. Four ratio
//     tables that all come out sounding the same would mean the bank is not
//     really using them, which is exactly what a stray normalisation or a
//     stale cache key would produce.
//
//   * SPREAD has to move the partials monotonically and only upward. It is
//     n^(1+s): at s = 0 the printed ratios, and above it every partial higher
//     than it was, never lower and never crossing its neighbour.

#include "../../src/Toll/Voice.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

using namespace toll;

static int checks = 0;
static int failures = 0;

static void fail(const char* what, const char* detail) {
	failures++;
	printf("  FAIL  %s: %s\n", what, detail);
}

static const char* kSetName[kSets] = { "MEMBRANE", "BAR", "BELL", "HARMONIC" };

/** One strike, rendered. Controls in the order Toll::process takes them. */
static void render(Toll& v, float vel, int set, float tune, float volts,
                   float decay, float damp, float pos, float hard,
                   float spread, float bend, float buzz, float* out, int n) {
	v.reset();
	for (int i = 0; i < n; i++)
		out[i] = v.process(i == 0, vel, set, tune, volts, decay, damp, pos,
		                   hard, spread, bend, buzz, false);
}

/** Energy above `hz`, through a 24 dB/oct highpass. */
static double highBand(const float* y, int n, float fs, float hz, int from) {
	float s[4] = { 0.f, 0.f, 0.f, 0.f };
	float g = std::tan(kPi * hz / fs); g = g / (1.f + g);
	double e = 0.0; int used = 0;
	for (int i = 0; i < n; i++) {
		float u = y[i];
		for (int k = 0; k < 4; k++) {
			float lo = s[k] + (u - s[k]) * g;
			s[k] = lo + (lo - s[k]);
			u = u - lo;
		}
		if (i >= from) { e += (double)u * u; used++; }
	}
	return used ? std::sqrt(e / used) : 0.0;
}


// ---------------------------------------------------------------------------
// 1: nothing non-finite, nothing runaway, at any setting or rate
// ---------------------------------------------------------------------------

static void sweep(float fs) {
	static float buf[192000];
	int n = (int)(fs * 0.5f);
	Toll v;
	v.setRate(fs);

	float worst = 0.f;
	for (int set = 0; set < kSets; set++)
	for (int a = 0; a <= 2; a++)
	for (int b = 0; b <= 2; b++)
	for (int c = 0; c <= 2; c++)
	for (int d = 0; d <= 2; d++)
	for (int e = 0; e <= 2; e++)
	for (int iv = 0; iv < 3; iv++) {
		static const float volts[3] = { -5.f, 0.f, 5.f };
		render(v, 1.f, set, a / 2.f, volts[iv], b / 2.f, c / 2.f, d / 2.f,
		       e / 2.f, b / 2.f, c / 2.f, d / 2.f, buf, n);
		for (int i = 0; i < n; i++) {
			if (!std::isfinite(buf[i])) {
				char m[160];
				snprintf(m, sizeof m, "%s non-finite at %.0f Hz, knobs %d%d%d%d%d, %+.0f V",
				         kSetName[set], fs, a, b, c, d, e, volts[iv]);
				fail("finite", m);
				return;
			}
			float x = std::fabs(buf[i]);
			if (x > worst) worst = x;
		}
	}
	checks++;
	if (worst > 40.f) {
		char m[128];
		snprintf(m, sizeof m, "peaked at %.1f V at %.0f Hz", worst, fs);
		fail("sane level", m);
	}
}


// ---------------------------------------------------------------------------
// 2: every strike ends
// ---------------------------------------------------------------------------

static void decays() {
	const float fs = 48000.f;
	static float buf[1440000];
	// DECAY reaches twelve seconds here, far longer than any drum, so the
	// window has to be longer to match: thirty seconds is two and a half t60s
	// at the very top of the knob.
	int n = (int)(fs * 30.f);
	int late = (int)(fs * 26.f);
	Toll v;
	v.setRate(fs);

	for (int set = 0; set < kSets; set++) {
		float worst = 0.f;
		for (int b = 0; b <= 2; b++)
		for (int c = 0; c <= 2; c++) {
			render(v, 1.f, set, 0.42f, 0.f, 1.f, c / 2.f, 0.5f, 0.6f,
			       b / 2.f, 1.f, b / 2.f, buf, n);
			float pk = 0.f, tail = 0.f;
			for (int i = 0; i < n; i++) {
				float m = std::fabs(buf[i]);
				if (m > pk) pk = m;
				if (i >= late && m > tail) tail = m;
			}
			float rel = pk > 1e-6f ? tail / pk : 0.f;
			if (rel > worst) worst = rel;
		}
		checks++;
		if (!(worst < 0.02f)) {
			char m[192];
			snprintf(m, sizeof m,
			         "%s is still at %.1f%% of its peak twenty-six seconds after"
			         " one strike -- it is being driven, not ringing",
			         kSetName[set], worst * 100.f);
			fail("strikes end", m);
		}
	}
}


// ---------------------------------------------------------------------------
// 3: the four partial sets are four different objects
// ---------------------------------------------------------------------------

static void setsDiffer() {
	const float fs = 48000.f;

	// Read the partial frequencies back out of the bank rather than inferring
	// them from a band of the audio. An energy measurement in one band is a
	// poor discriminator here -- the strike's click and attack layer are
	// identical in all four sets by design and are broadband enough to bury the
	// difference, and once they are excluded a set whose partials all sit below
	// the band reads the same as one whose partials sit above it, which is two
	// different objects giving one number. The coefficients are the thing under
	// test, so test them: a2 is -r*r and a1 is 2 r cos(w).
	Bank bank;
	bank.setRate(fs);

	float f[kSets][kParts];
	for (int set = 0; set < kSets; set++) {
		bank.setPosition(0.5f, set);
		bank.clearCoef();
		bank.setTuning(110.f, 2.f, 0.2f, 1.f, 0.f, set);
		for (int i = 0; i < kParts; i++) {
			if (bank.a2[i] == 0.f) { f[set][i] = 0.f; continue; }
			float r = std::sqrt(-bank.a2[i]);
			float w = std::acos(clampf(bank.a1[i] / (2.f * r), -1.f, 1.f));
			f[set][i] = w * fs / kTwoPi;
		}
	}

	for (int a = 0; a < kSets; a++)
	for (int b = a + 1; b < kSets; b++) {
		int differing = 0;
		for (int i = 1; i < kParts; i++) {
			if (f[a][i] == 0.f || f[b][i] == 0.f) continue;
			if (std::fabs(f[a][i] / f[b][i] - 1.f) > 0.02f) differing++;
		}
		checks++;
		// Two objects that agree on all but a couple of partials are the same
		// object with a rounding error.
		if (differing < 4) {
			char m[192];
			snprintf(m, sizeof m,
			         "%s and %s agree on all but %d partials -- they are not two"
			         " different objects", kSetName[a], kSetName[b], differing);
			fail("sets differ", m);
		}
	}

	// And the bank has to be reachable from the voice: four sets that differ in
	// the coefficients but render identically would mean nothing downstream is
	// using them.
	static float bufA[96000], bufB[96000];
	int n = (int)(fs * 1.0f);
	Toll v;
	v.setRate(fs);
	render(v, 1.f, 0, 0.35f, 0.f, 0.8f, 0.15f, 0.5f, 0.f, 0.f, 0.f, 0.f, bufA, n);
	for (int set = 1; set < kSets; set++) {
		render(v, 1.f, set, 0.35f, 0.f, 0.8f, 0.15f, 0.5f, 0.f, 0.f, 0.f, 0.f, bufB, n);
		double diff = 0.0, ref = 0.0;
		for (int i = 0; i < n; i++) {
			double d = (double)bufA[i] - bufB[i];
			diff += d * d;
			ref += (double)bufA[i] * bufA[i];
		}
		checks++;
		if (!(ref > 1e-9 && std::sqrt(diff / ref) > 0.2)) {
			char m[192];
			snprintf(m, sizeof m,
			         "MEMBRANE and %s render within %.1f%% of each other -- the"
			         " voice is not using the set it was handed",
			         kSetName[set], ref > 1e-9 ? std::sqrt(diff / ref) * 100.0 : 0.0);
			fail("sets reach the output", m);
		}
	}
}


// ---------------------------------------------------------------------------
// 4: SPREAD moves the partials up, monotonically, and never past each other
// ---------------------------------------------------------------------------

static void spreadIsMonotone() {
	// Read the bank's own coefficients rather than the audio: a2 is -r*r and
	// a1 is 2 r cos(w), so acos(a1 / (2 sqrt(-a2))) recovers each partial's
	// frequency exactly. Checking the thing itself beats inferring it.
	const float fs = 48000.f;
	Bank bank;
	bank.setRate(fs);
	bank.setPosition(0.5f, 2);

	float prev[kParts];
	for (int i = 0; i < kParts; i++) prev[i] = 0.f;

	for (int s = 0; s <= 10; s++) {
		float spread = s / 10.f;
		bank.clearCoef();
		bank.setTuning(110.f, 1.f, 0.3f, 1.f, spread, 2);   // BELL
		float f[kParts];
		int live = 0;
		for (int i = 0; i < kParts; i++) {
			if (bank.a2[i] == 0.f) { f[i] = 0.f; continue; }
			float r = std::sqrt(-bank.a2[i]);
			float w = std::acos(clampf(bank.a1[i] / (2.f * r), -1.f, 1.f));
			f[i] = w * fs / kTwoPi;
			live++;
		}
		checks++;
		if (live < 4) {
			char m[128];
			snprintf(m, sizeof m, "only %d partials survive at spread %.1f", live, spread);
			fail("spread", m);
		}
		// Two properties, counted once each rather than once per partial:
		// "partials never cross" and "stretching only moves them up" are single
		// claims about SPREAD, and restating them fifteen times a step said
		// nothing the first said. Every partial is still compared, and the
		// first offender at each spread is named.
		{
			int crossed = 0;
			for (int i = 1; i < kParts; i++) {
				if (f[i] == 0.f || f[i - 1] == 0.f) continue;
				if (!(f[i] > f[i - 1])) {
					if (!crossed) {
						char m[160];
						snprintf(m, sizeof m, "at spread %.1f partial %d (%.1f Hz) is not"
						         " above partial %d (%.1f Hz)",
						         spread, i, f[i], i - 1, f[i - 1]);
						fail("spread ordering", m);
					}
					crossed++;
				}
			}
		}
		if (s > 0) {
			int fell = 0;
			for (int i = 1; i < kParts; i++) {
				if (f[i] == 0.f || prev[i] == 0.f) continue;
				if (f[i] < prev[i] - 0.5f) {
					if (!fell) {
						char m[160];
						snprintf(m, sizeof m, "partial %d fell from %.1f to %.1f Hz when"
						         " SPREAD went up", i, prev[i], f[i]);
						fail("spread monotone", m);
					}
					fell++;
				}
			}
		}
		for (int i = 0; i < kParts; i++) prev[i] = f[i];
	}
	checks += 2;      // "partials never cross" and "stretching only moves up"

	// And spread zero has to be the printed table, not nearly it.
	bank.clearCoef();
	bank.setTuning(110.f, 1.f, 0.3f, 1.f, 0.f, 2);
	for (int i = 0; i < 6; i++) {
		float r = std::sqrt(-bank.a2[i]);
		float w = std::acos(clampf(bank.a1[i] / (2.f * r), -1.f, 1.f));
		float got = w * fs / kTwoPi;
		float want = 110.f * kSet[2][i];
		checks++;
		if (std::fabs(got - want) > want * 0.002f) {
			char m[160];
			snprintf(m, sizeof m, "at spread 0 partial %d is %.2f Hz, table says %.2f",
			         i, got, want);
			fail("spread zero", m);
		}
	}
}


// ---------------------------------------------------------------------------
// 5: the same object at every sample rate
// ---------------------------------------------------------------------------

static void sampleRates() {
	static const float rates[5] = { 44100.f, 48000.f, 88200.f, 96000.f, 192000.f };
	static float buf[192000];

	for (int set = 0; set < kSets; set++) {
		double loudest = 0.0, quietest = 1e30;
		for (int r = 0; r < 5; r++) {
			float fs = rates[r];
			Toll v;
			v.setRate(fs);
			int n = (int)(fs * 0.8f);
			render(v, 1.f, set, 0.4f, 0.f, 0.5f, 0.35f, 0.45f, 0.6f, 0.2f, 0.3f, 0.3f, buf, n);
			double e = 0.0;
			for (int i = 0; i < n; i++) e += (double)buf[i] * buf[i];
			double rms = std::sqrt(e / n);
			if (rms > loudest) loudest = rms;
			if (rms < quietest) quietest = rms;
		}
		checks++;
		double ratio = quietest > 1e-9 ? loudest / quietest : 1e9;
		if (!(ratio < 1.5)) {
			char m[192];
			snprintf(m, sizeof m,
			         "%s is %.2fx louder at one sample rate than another -- a"
			         " coefficient somewhere is written per sample rather than"
			         " per second", kSetName[set], ratio);
			fail("rate independent", m);
		}
	}
}


// ---------------------------------------------------------------------------
// 6: V/oct tracks
// ---------------------------------------------------------------------------

static void tracking() {
	const float fs = 48000.f;
	Bank bank;
	bank.setRate(fs);
	bank.setPosition(0.5f, 3);
	float prev = 0.f;
	for (int v = -2; v <= 2; v++) {
		bank.clearCoef();
		bank.setTuning(transpose(220.f, (float)v), 1.f, 0.3f, 1.f, 0.f, 3);
		float r = std::sqrt(-bank.a2[0]);
		float w = std::acos(clampf(bank.a1[0] / (2.f * r), -1.f, 1.f));
		float f = w * fs / kTwoPi;
		float want = 220.f * std::pow(2.f, (float)v);
		checks++;
		if (std::fabs(f - want) > want * 0.002f) {
			char m[160];
			snprintf(m, sizeof m, "%+d V gave %.2f Hz, wanted %.2f", v, f, want);
			fail("v/oct", m);
		}
		if (v > -2) {
			checks++;
			if (std::fabs(f / prev - 2.f) > 0.01f) fail("v/oct", "a volt is not an octave");
		}
		prev = f;
	}
}


/** Peak absolute value of a render. */
static double peakOf(const float* y, int n) {
	double p = 0.0;
	for (int i = 0; i < n; i++) if (std::fabs(y[i]) > p) p = std::fabs(y[i]);
	return p;
}

/** VELOCITY.

    The voice always took a velocity and the panel could never set it -- it ran
    pinned at full force -- so this is the first time any of it is exercised.
    Two properties matter and they are different properties: that a softer
    strike is quieter, and that it is also *duller*. The second is the reason
    velocity is worth a jack at all. Contact time is 1/(0.6 + 0.4v), so a soft
    strike rests on the object longer and cannot push energy as far up the
    partial bank; if velocity were only a gain, the two renders would be the
    same sound at two levels and the level check alone would not notice. */
static void velocity() {
	static const int kN = 40000;
	static float soft[kN], hard[kN];
	const float fs = 44100.f;

	// --- the mapping ---------------------------------------------------------
	{
		bool ok = Toll::velocityFrom(10.f) == 1.f          // patched full = unpatched
		       && Toll::velocityFrom(12.f) == 1.f          // and above it, clamped
		       && Toll::velocityFrom(5.f) > 0.49f && Toll::velocityFrom(5.f) < 0.51f
		       && Toll::velocityFrom(0.f) == kVelFloor     // a ghost note, not silence
		       && Toll::velocityFrom(-8.f) == kVelFloor;
		checks++;
		if (!ok) fail("velocity mapping", "0-10 V does not map to the floor..1");
	}

	// --- a softer strike is quieter, all the way down ------------------------
	{
		Toll v; v.setRate(fs);
		double prev = 1e9; int bad = 0;
		for (int i = 10; i >= 1; i--) {
			float vel = (float)i / 10.f;
			render(v, vel, 2, 0.4f, 0.f, 0.5f, 0.5f, 0.3f, 0.6f, 0.f, 0.f, 0.f,
			       soft, kN);
			double pk = peakOf(soft, kN);
			if (!(pk < prev)) bad++;
			prev = pk;
		}
		checks++;
		if (bad) fail("velocity", "a softer strike is not quieter");
	}

	// --- and it clatters, which is not a level difference --------------------
	// The docstring's claim for BUZZ: "a soft strike is a clean ring and a hard
	// one clatters". That is Bilbao's one-sided collision -- the loose layer
	// only speaks once the body swings far enough to reach it -- so it is the
	// one place velocity changes the *timbre* rather than the level. Both
	// renders are normalised to the same peak first, so nothing here can be
	// explained by the hard strike simply being louder.
	{
		Toll v; v.setRate(fs);
		render(v, 1.00f, 2, 0.4f, 0.f, 0.5f, 0.5f, 0.3f, 0.6f, 0.f, 0.f, 0.8f,
		       hard, kN);
		render(v, 0.15f, 2, 0.4f, 0.f, 0.5f, 0.5f, 0.3f, 0.6f, 0.f, 0.f, 0.8f,
		       soft, kN);
		double ph = peakOf(hard, kN), ps = peakOf(soft, kN);
		for (int i = 0; i < kN; i++) { hard[i] /= (float)ph; soft[i] /= (float)ps; }
		double hh = highBand(hard, kN, fs, 2000.f, 0);
		double hs = highBand(soft, kN, fs, 2000.f, 0);
		checks++;
		if (!(hh > hs * 2.0)) {
			char d[144];
			snprintf(d, sizeof d,
			         "buzz up, level-matched: hard %.5f, soft %.5f (%.2fx)",
			         hh, hs, hs > 0.0 ? hh / hs : 0.0);
			fail("velocity", d);
		}
	}

	// --- and with the layer off, it is a level control and nothing else -------
	// The other half of the same claim, and the one that says where the
	// brightness above comes from. With BUZZ at zero there is no collision to
	// reach, so two velocities have to be the same sound at two levels -- if
	// this drifted, the clatter check above could be passing on some general
	// coupling between loudness and brightness rather than on the collision.
	{
		Toll v; v.setRate(fs);
		render(v, 1.00f, 2, 0.4f, 0.f, 0.5f, 0.5f, 0.3f, 0.6f, 0.f, 0.f, 0.f,
		       hard, kN);
		render(v, 0.15f, 2, 0.4f, 0.f, 0.5f, 0.5f, 0.3f, 0.6f, 0.f, 0.f, 0.f,
		       soft, kN);
		double ph = peakOf(hard, kN), ps = peakOf(soft, kN);
		for (int i = 0; i < kN; i++) { hard[i] /= (float)ph; soft[i] /= (float)ps; }
		double hh = highBand(hard, kN, fs, 2000.f, 0);
		double hs = highBand(soft, kN, fs, 2000.f, 0);
		double ratio = hs > 0.0 ? hh / hs : 0.0;
		checks++;
		if (!(ratio > 0.8 && ratio < 1.25)) {
			char d[144];
			snprintf(d, sizeof d,
			         "buzz off, level-matched: hard %.5f, soft %.5f (%.2fx)",
			         hh, hs, ratio);
			fail("velocity", d);
		}
	}

	// --- the floor still lands ------------------------------------------------
	// A trigger that arrives while a velocity CV happens to rest at 0 V has to
	// make a sound. Silence there reads as a broken patch, not as a soft hit.
	{
		Toll v; v.setRate(fs);
		render(v, Toll::velocityFrom(0.f), 2, 0.4f, 0.f, 0.5f, 0.5f, 0.3f, 0.6f,
		       0.f, 0.f, 0.f, soft, kN);
		double pk = peakOf(soft, kN);
		checks++;
		if (!(pk > 0.01)) {
			char d[96];
			snprintf(d, sizeof d, "0 V strike peaks at %.5f", pk);
			fail("velocity", d);
		}
	}
}


int main() {
	printf("Toll voice\n");
	printf("  sweeping 44100 Hz...\n"); sweep(44100.f);
	printf("  sweeping 96000 Hz...\n"); sweep(96000.f);
	printf("  strikes end...\n");       decays();
	printf("  sets differ...\n");       setsDiffer();
	printf("  spread...\n");            spreadIsMonotone();
	printf("  sample rates...\n");      sampleRates();
	printf("  v/oct...\n");             tracking();
	printf("  velocity...\n");          velocity();
	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}

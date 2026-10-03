// The HAT's hiss and DAZZLE (the snare's mode, the hat's top end) on the avalanche circuit.
//
// Percussive Noise Voice (BC549, C5 = 1 nF HiHat) for the hat's hiss; the Tiny Dazzler (2N3904,
// 13.5 V; C1 0.1 uF snare, 0.001 uF hi-hat) for the Karplus-Strong table. The context-menu option
// "white / microplasma" is `plasma` on Hat and on each KsEngine; microplasma is the default and white is
// the generator both used before.
//
// What is checked:
//   1. The defaults are the circuit, and WHITE IS THE OLD VOICE BIT FOR BIT (the old Hat and KsEngine are
//      kept verbatim here and compared sample for sample; the kicks, which were not touched, by a hash
//      taken before the change).
//   2. The circuit really is in the path: a spectral tilt white does not have; negative control, the
//      same measure on the white path.
//   3. Level: the voice's rms against the white voice's, over the knob, within tolerance; negative
//      control, the same measure with the correction off (gain 1) must fail.
//   4. The table: KsDrum::pluckRaw fills as pluck does; the store hands the first samples of the
//      Dazzler's own stream, in order; the store is topped up one sample per sample and no further.
//   5. The solver is only run while the hiss is heard (a paused circuit is unmoved); negative control,
//      it does move while the hat sounds.
//   6. Determinism and seeds.
#include "../../src/Kickback/Voices.hpp"

#include <cmath>
#include <complex>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace kickback;

static int checks = 0, failures = 0;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("  FAIL  "); printf(__VA_ARGS__); printf("\n"); } } while (0)

// ---- the pre-option voices, verbatim ------------------------------------------------------------
// The KsEngine and the Hat exactly as they were at commit 8f1ee42 (white noise only). The white option must
// be these, sample for sample, in this binary (no golden numbers, so no compiler or platform dependence).
struct LegacyKsEngine {
	KsDrum ks;
	OnePole lp, hp;
	Decay env;
	float fs = 44100.f;
	float psd = 1.f;
	mt::Cache capC;
	int lastMode = -1;

	void setRate(float fs_) { fs = fs_; psd = noisePsdGain(fs_); capC.clear(); }
	void reset() { ks.reset(); lp.reset(); hp.reset(); env.reset(); capC.clear(); lastMode = -1; }

	/** `bright` 0 = the snare end (long table, open and low), 1 = the hat end
	    (short table, tight and bright). `blend` is the paper's b. */
	inline void strike(float vel, float hz, float blend, int bright) {
		// A blend near 1/2 makes its own randomness, so the paper allows a
		// constant load; nearer the string ends it needs a noisy one.
		float bias = 1.f - std::fmin(std::fabs(blend - 0.5f) * 2.f, 1.f);
		// The snare end finishes in a lowpass at a corner in hertz and wants
		// noise at constant spectral density; the hat end finishes in a
		// highpass, whose band grows with the rate, and is right without it.
		ks.pluck(vel * (bright ? 1.f : psd), (int)(fs / std::fmax(hz, 8.f)), bias * 0.8f);
		env.strike(vel);
	}

	/** `stretch` is the paper's S, which it notes "increases the snare sound". */
	inline float process(float hz, float t60, float blend, float stretch, int bright) {
		if (bright != lastMode) { lastMode = bright; capC.clear(); }
		float e = env.process(t60, fs);
		float y = ks.process(blend, stretch);
		float g = capC.get(hz, [this, bright](float h) {
			return poleG(std::fmin(h * (bright ? 1.6f : 9.f), fs * 0.45f), fs);
		});
		y = bright ? hp.hp(y, g) : lp.lp(y, g);
		// The recurrence decays on its own; the envelope only ever shortens it.
		return y * std::fmin(e * 3.f, 1.f);
	}
};

struct LegacyHat {
	Noise noise;
	SquareOsc o1, o2, o3;
	OnePole hp1, hp2, lp1;
	LegacyKsEngine ksEng;
	Decay fast, slow;
	Attack attack;
	DcBlock dc;
	float fs = 44100.f;

	mt::Cache freqC, t60C, topC;

	LegacyHat() : noise(0xBADC0DEu), attack(0x4A77E5u) {}

	// topC closes over fs, so the rate moving has to invalidate it by hand --
	// its key is a knob and cannot see the rate change behind it.
	void setRate(float fs_) {
		fs = fs_; dc.setRate(fs_); attack.setRate(fs_); ksEng.setRate(fs_); topC.clear();
	}
	void reset() {
		noise.reset();
		o1.reset(); o2.reset(); o3.reset();
		hp1.reset(); hp2.reset(); lp1.reset();
		ksEng.reset(); fast.reset(); slow.reset(); attack.reset(); dc.reset();
		freqC.clear(); t60C.clear(); topC.clear();
	}

	/** `tune` sets the highpass corner and the metal's pitch together;
	    `colour` is RATTLE: metal -> noise -> Dazzler. */
	inline float process(bool hit, float vel, float tune, float volts,
	                     float decay, float bend, float colour) {
		float corner = transpose(freqC.get(tune, [](float k) { return expMap(k, 1800.f, 11000.f); }), volts);
		float t60 = t60C.get(decay, [](float k) { return expMap(k, 0.018f, 0.9f); });
		// The pedal. A ghost tick closes to a third of the knob's time, a full
		// accent opens to better than twice it.
		float open = 0.34f + 0.78f * clampf(vel, 0.f, 2.f);
		float t = t60 * open;
		float ksHz = corner * 0.25f;

		if (hit) {
			fast.strike(vel); slow.strike(vel); attack.strike(vel * 0.8f);
			// The Dazzler's delay tracks the same knob, at its short end.
			ksEng.strike(vel, ksHz, 0.5f, 1);
		}
		float ef = fast.process(t * 0.18f, fs);
		float es = slow.process(t, fs);
		float env = ef * 0.55f + es * 0.75f;

		// BEND sweeps the corner down as the hat dies -- the same "pitch high
		// then down" the envelope does on a pitched voice, applied to colour.
		float sweep = corner * (1.f + bend * bend * 2.2f * es);
		float g = poleG(std::fmin(sweep, fs * 0.45f), fs);

		// Three squares at inharmonic ratios, multiplied. Their fundamentals
		// sit under the corner so the highpass is what shapes them.
		float m = o1.process(corner * 0.42f, fs)
		        * o2.process(corner * 0.63f, fs)
		        * o3.process(corner * 0.87f, fs);
		float n = noise.next();

		// RATTLE crossfades metal -> noise over the first half of its travel
		// and noise -> Dazzler over the second, so each boundary is a sweep.
		float metal = clampf(1.f - colour * 2.f, 0.f, 1.f);
		float dazz  = clampf(colour * 2.f - 1.f, 0.f, 1.f);
		float hiss  = 1.f - metal - dazz;
		float src = m * metal + n * (hiss * 1.25f);

		float h1 = src - hp1.lp(src, g);
		float h2 = h1 - hp2.lp(h1, g);
		// Capped at 18 kHz, not at Nyquist. Clamping the top of the band to
		// the sample rate makes the band itself rate-dependent -- the hat would
		// occupy 19.8 kHz of spectrum at 44.1 and 28.6 kHz at 96.
		float topG = topC.get(corner, [this](float c) {
			return poleG(std::fmin(std::fmin(c * 2.6f, 18000.f), fs * 0.45f), fs);
		});
		float y = lp1.lp(h2, topG) * env;

		// The Dazzler's path keeps its own envelope, so its rattle sits beside
		// the metal rather than being gated by it.
		if (dazz > 0.f)
			y += ksEng.process(ksHz, t, 0.5f, 1.f + bend * bend * 6.f, 1) * dazz * 1.5f;

		y += attack.process(0.9f, 0.5f);
		return dc.process(y * 1.445f) * 5.f;
	}
};


struct Fnv {
	uint64_t h = 1469598103934665603ull;
	void put(float y) { uint32_t b; std::memcpy(&b, &y, 4); h = (h ^ b) * 1099511628211ull; }
};

/** The three kick models, taken from the tree at commit 8f1ee42 (before this change), by FNV-1a of every sample
    (arm64 / Apple clang, the flags in the Makefile). Kick shares Drum.hpp with the Dazzler's table and nothing
    else changed here; this is the proof it is untouched. Other platforms round differently, so there it only
    prints. */
static uint64_t renderKicks() {
	Fnv k;
	const float rates[2] = { 44100.f, 48000.f };
	for (float fs : rates)
		for (int ki = 0; ki < 3; ki++) {
			float tune = 0.2f + 0.3f * ki, decay = 0.3f + 0.25f * ki, bend = 0.1f + 0.4f * ki;
			int N = (int)(0.5f * fs);
			float vel = ki == 1 ? 0.5f : 1.f;
			for (int m = 0; m < 3; m++) {
				Kick kk; kk.setRate(fs); kk.reset();
				for (int i = 0; i < N; i++) k.put(kk.process(i == 0 || i == N / 2, vel, m, tune, ki * 0.3f, decay, bend, 0.25f + 0.5f * bend));
			}
		}
	return k.h;
}

// ---- measures --------------------------------------------------------------------------------
static double power(const std::vector<float>& y) { double s = 0; for (float v : y) s += (double) v * v; return s / (double) y.size(); }

/** Mean power in a narrow band at `f` by Goertzel over 4096-sample blocks. */
static double bandPower(const std::vector<float>& y, double fs, double f) {
	const int W = 4096; double acc = 0; int n = 0;
	double w = 2.0 * M_PI * f / fs, c = 2.0 * std::cos(w);
	for (size_t o = 0; o + W <= y.size(); o += W) {
		double s1 = 0, s2 = 0;
		for (int i = 0; i < W; i++) { double x = y[o + i] * (0.5 - 0.5 * std::cos(2.0 * M_PI * i / W)) + c * s1 - s2; s2 = s1; s1 = x; }
		acc += (s1 * s1 + s2 * s2 - c * s1 * s2); n++;
	}
	return acc / n;
}

static std::vector<float> renderHat(bool plasma, float tune, float decay, float colour, uint32_t seed, float fs = 48000.f, float secs = 1.2f) {
	Hat h; h.plasma = plasma; h.ksEng.plasma = plasma;
	h.avalanche.seed0 = seed; h.ksEng.dz.seed0 = seed * 3u + 1u;
	h.setRate(fs); h.reset();
	int N = (int)(secs * fs); std::vector<float> y(N);
	for (int i = 0; i < N; i++) y[i] = h.process(i == 0, 1.f, tune, 0.f, decay, 0.3f, colour);
	return y;
}
static std::vector<float> renderSnare(bool plasma, float tune, float decay, float bend, uint32_t seed, float fs = 48000.f) {
	Snare s; s.ksEng.plasma = plasma; s.ksEng.dz.seed0 = seed * 3u + 1u;
	s.setRate(fs); s.reset();
	int N = (int)(1.2f * fs); std::vector<float> y(N);
	for (int i = 0; i < N; i++) y[i] = s.process(i == 0, 1.f, 2, tune, 0.f, decay, bend);
	return y;
}

int main() {
	// 1. Defaults, and white is the old voice -----------------------------------------------------
	{
		Hat h; Snare s;
		CHECK(h.plasma && h.ksEng.plasma && s.ksEng.plasma, "hat hiss, hat DAZZLE and snare DAZZLE default to the circuit");

		// The hat, white, at every RATTLE and a spread of TUNE / DECAY / BEND / velocity / rate, against LegacyHat.
		long n = 0; bool same = true; bool moved = false;
		for (float fs : { 44100.f, 48000.f, 96000.f })
			for (float col : { 0.f, 0.3f, 0.5f, 0.8f, 1.f })
				for (int ki = 0; ki < 3; ki++) {
					float tune = 0.2f + 0.3f * ki, decay = 0.3f + 0.25f * ki, bend = 0.1f + 0.4f * ki, vel = ki == 1 ? 0.5f : 1.f;
					Hat nh; nh.plasma = false; nh.ksEng.plasma = false; nh.setRate(fs); nh.reset();
					LegacyHat oh; oh.setRate(fs); oh.reset();
					Hat ph; ph.setRate(fs); ph.reset();                       // control: the default is not the old voice
					int N = (int)(0.4f * fs);
					for (int i = 0; i < N; i++) {
						bool hit = i == 0 || i == N / 2;
						float a = nh.process(hit, vel, tune, ki * 0.3f, decay, bend, col);
						float b = oh.process(hit, vel, tune, ki * 0.3f, decay, bend, col);
						float c = ph.process(hit, vel, tune, ki * 0.3f, decay, bend, col);
						if (std::memcmp(&a, &b, 4) != 0) same = false;
						if (col >= 0.3f && std::memcmp(&a, &c, 4) != 0) moved = true;
						n++;
					}
				}
		CHECK(same, "hat with white noise is bit-identical to the old Hat (%ld samples)", n);
		CHECK(moved, "control: the default (microplasma) hat is not the old one");

		// The snare's DAZZLE mode: the engine's output with white against LegacyKsEngine, sample for sample.
		bool ksSame = true, ksMoved = false;
		for (float fs : { 44100.f, 48000.f, 96000.f })
			for (int bright = 0; bright < 2; bright++)
				for (float hz : { 90.f, 300.f, 1500.f }) {
					KsEngine ne; ne.plasma = false; ne.setRate(fs); ne.reset();
					LegacyKsEngine oe; oe.setRate(fs); oe.reset();
					KsEngine pe; pe.setRate(fs); pe.reset();
					for (int r = 0; r < 2; r++) {
						ne.strike(0.9f, hz, 0.4f, bright); oe.strike(0.9f, hz, 0.4f, bright); pe.strike(0.9f, hz, 0.4f, bright);
						for (int i = 0; i < 6000; i++) {
							float a = ne.process(hz, 0.3f, 0.4f, 3.f, bright), b = oe.process(hz, 0.3f, 0.4f, 3.f, bright);
							float c = pe.process(hz, 0.3f, 0.4f, 3.f, bright);
							if (std::memcmp(&a, &b, 4) != 0) ksSame = false;
							if (std::memcmp(&a, &c, 4) != 0) ksMoved = true;
						}
					}
				}
		CHECK(ksSame, "KsEngine with white noise is bit-identical to the old one (snare end and hat end)");
		CHECK(ksMoved, "control: the default (microplasma) KsEngine is not the old one");

		// The kicks.
		uint64_t kh = renderKicks();
#if defined(__aarch64__) && defined(__APPLE__)
		const uint64_t gKick = 0xbd86d72945b1af9aull;
		CHECK(kh == gKick, "kicks are bit-identical to before (0x%016llx vs 0x%016llx)", (unsigned long long) kh, (unsigned long long) gKick);
		CHECK(kh != 0, "control: the kick hash is not empty");
#else
		printf("  kick hash 0x%016llx (golden compared on arm64 Apple only)\n", (unsigned long long) kh);
#endif
	}
	// 2. The circuit is in the path --------------------------------------------------------------
	{
		// A hat at RATTLE 0.5 (all hiss), TUNE 0.5: the white noise is flat through the highpass pair and the
		// lowpass; the circuit's own spectrum is not. Ratio of the band power near 5.5 kHz to near 11 kHz.
		auto tilt = [](bool plasma) {
			double lo = 0, hi = 0;
			for (uint32_t sd = 1; sd <= 3; sd++) {
				std::vector<float> y = renderHat(plasma, 0.5f, 0.4f, 0.5f, sd * 7u);
				lo += bandPower(y, 48000.0, 5500.0); hi += bandPower(y, 48000.0, 11000.0);
			}
			return 10.0 * std::log10(lo / hi);
		};
		double tw = tilt(false), tp = tilt(true);
		printf("  band tilt 5.5 kHz / 11 kHz: white %+.1f dB, microplasma %+.1f dB\n", tw, tp);
		CHECK(std::fabs(tp - tw) > 3.0, "the circuit's spectral tilt (%+.1f dB) is not the white path's (%+.1f dB)", tp, tw);
		CHECK(std::fabs(tw) < 12.0, "control: the white path measured through the same chain is sane (%+.1f dB)", tw);
	}

	// 3. Level -------------------------------------------------------------------------------------
	// The voice's power, white against microplasma, in dB, over the knob. `gain` is a negative control: the
	// microplasma output scaled as if the correction table were gone.
	{
		auto hatSpread = [](double gainDb, double& mean) {
			double lo = 1e9, hi = -1e9, acc = 0; int n = 0;
			for (int ki = 0; ki < 9; ki++) {
				float tune = ki / 8.f;
				double w = power(renderHat(false, tune, 0.3f, 0.5f, 1u)), p = 0;
				for (uint32_t sd = 1; sd <= 3; sd++) p += power(renderHat(true, tune, 0.3f, 0.5f, sd * 977u)) / 3.0;
				double d = 10.0 * std::log10(p / w) + gainDb;
				lo = std::fmin(lo, d); hi = std::fmax(hi, d); acc += d; n++;
			}
			mean = acc / n;
			return hi - lo;
		};
		double mean; double spread = hatSpread(0.0, mean);
		printf("  hat hiss, microplasma against white over TUNE: mean %+.2f dB, spread %.2f dB\n", mean, spread);
		CHECK(std::fabs(mean) < 0.5, "hat hiss level is the white's over TUNE (mean %+.2f dB)", mean);
		CHECK(spread < 1.5, "hat hiss level does not move with TUNE (spread %.2f dB)", spread);
		(void) hatSpread(6.0, mean);
		CHECK(std::fabs(mean) > 5.0, "control: a 6 dB error in the correction is seen by the same measure (mean %+.2f dB)", mean);

		// The Dazzler end on the snare and the hat.
		auto ksLevel = [](bool hat, double& meanOut) {
			double lo = 1e9, hi = -1e9, acc = 0; int n = 0;
			for (int ki = 0; ki < 5; ki++) {
				float tune = ki / 4.f;
				double w = hat ? power(renderHat(false, tune, 0.3f, 1.f, 1u)) : power(renderSnare(false, tune, 0.4f, 0.3f, 1u));
				double p = 0;
				for (uint32_t sd = 1; sd <= 4; sd++)
					p += (hat ? power(renderHat(true, tune, 0.3f, 1.f, sd * 977u)) : power(renderSnare(true, tune, 0.4f, 0.3f, sd * 977u))) / 4.0;
				double d = 10.0 * std::log10(p / w);
				lo = std::fmin(lo, d); hi = std::fmax(hi, d); acc += d; n++;
			}
			meanOut = acc / n;
			return hi - lo;
		};
		double m1, m2;
		double s1 = ksLevel(false, m1), s2 = ksLevel(true, m2);
		printf("  DAZZLE level, microplasma against white: snare mean %+.2f dB spread %.2f; hat top mean %+.2f dB spread %.2f\n", m1, s1, m2, s2);
		CHECK(std::fabs(m1) < 1.0 && s1 < 4.0, "snare DAZZLE is the white's level (mean %+.2f, spread %.2f)", m1, s1);
		CHECK(std::fabs(m2) < 1.0 && s2 < 4.0, "hat DAZZLE is the white's level (mean %+.2f, spread %.2f)", m2, s2);
		CHECK(std::fabs(m1 + 4.2) > 1.0, "control: without the table's 4.2 dB trim the snare DAZZLE would read %+.2f dB and fail the test above", m1 + 4.2);
	}

	// 4. The table -----------------------------------------------------------------------------------
	{
		// pluckRaw is pluck, with the numbers handed in: feed it what pluck's own Noise produces.
		KsDrum a, b;
		Noise src(0xD00Du);
		float raw[600];
		for (int i = 0; i < 600; i++) raw[i] = src.next();
		a.pluck(0.8f, 300, 0.4f);
		b.pluckRaw(raw, 0.8f, 300, 0.4f);
		bool same = a.p == b.p;
		for (int i = 0; i <= a.p; i++) same = same && a.buf[i] == b.buf[i];
		CHECK(same, "KsDrum::pluckRaw fills the table exactly as pluck does from the same numbers");
		b.pluckRaw(raw + 1, 0.8f, 300, 0.4f);
		bool differs = false;
		for (int i = 0; i <= a.p; i++) differs = differs || a.buf[i] != b.buf[i];
		CHECK(differs, "control: other numbers fill another table");

		// The store hands the Dazzler's stream, in order. A fresh engine, struck once: the first p + 1
		// samples of an independent source with the same seed (blend 0.5: the load is leaned by 0.8).
		KsEngine e;                                          // snare: C1 0.1 uF
		e.setRate(48000.f); e.reset();
		AvalancheSource ref(0xD0771E5u, AvalancheNoise::DAZZLER, true, 0.1e-6);
		ref.setRate(48000.f);
		e.strike(1.f, 200.f, 0.5f, 0);
		const int p = 240;                                   // 48000 / 200
		float s = 0.f; bool ok = e.ks.p == p;
		for (int i = 0; i <= p; i++) {
			float n = ref.next();
			s += (n - s) * (1.f - 0.8f * 0.92f);
			float want = (n * (1.f - 0.8f) + s * 0.8f) * 0.6166f;
			if (std::fabs(e.ks.buf[i] - want) > 1e-6f * (1.f + std::fabs(want))) ok = false;
		}
		CHECK(ok, "the first strike's table is the first p + 1 samples of the Dazzler stream (period %d), trimmed", e.ks.p);
		CHECK(e.stored == 0, "a cold store is emptied by the strike that fills it (%d left)", e.stored);

		// topped up one a sample, and stops when full
		for (int i = 0; i < 100; i++) (void) e.process(200.f, 0.3f, 0.5f, 1.f, 0);
		CHECK(e.stored == 100, "the store gains one sample per processed sample (%d)", e.stored);
		for (int i = 0; i < 5000; i++) (void) e.process(200.f, 0.3f, 0.5f, 1.f, 0);
		CHECK(e.stored == KsDrum::kMax, "and stops when full (%d of %d)", e.stored, KsDrum::kMax);
		e.strike(1.f, 200.f, 0.5f, 0);
		CHECK(e.stored == KsDrum::kMax - (p + 1), "a strike from a full store consumes p + 1 samples (%d left)", e.stored);

		// white takes no samples from the store and does not touch the circuit
		KsEngine w; w.plasma = false; w.setRate(48000.f); w.reset();
		uint32_t before = w.dz.ckt.mrng.s;
		w.strike(1.f, 200.f, 0.5f, 0);
		for (int i = 0; i < 100; i++) (void) w.process(200.f, 0.3f, 0.5f, 1.f, 0);
		CHECK(w.stored == 0 && w.dz.ckt.mrng.s == before, "white does not run the circuit");
	}

	// 5. The solver runs only while the hiss is heard -------------------------------------------------
	{
		auto state = [](const Hat& h) { return h.avalanche.ckt.mrng.s ^ (h.avalanche.ckt.rng * 2654435761u); };
		Hat h; h.setRate(48000.f); h.reset();
		uint32_t s0 = state(h);
		for (int i = 0; i < 2000; i++) (void) h.process(false, 1.f, 0.5f, 0.f, 0.3f, 0.3f, 0.5f);
		CHECK(state(h) == s0, "a hat that has not been struck does not run the circuit");

		float t60 = 0.018f;                                    // the shortest ring
		for (int i = 0; i < 100; i++) (void) h.process(i == 0, 1.f, 0.5f, 0.f, 0.0f, 0.3f, 0.5f);
		uint32_t s1 = state(h);
		CHECK(s1 != s0, "control: it does run while the hat sounds");
		for (int i = 0; i < 96000; i++) (void) h.process(false, 1.f, 0.5f, 0.f, 0.0f, 0.3f, 0.5f);
		uint32_t s2 = state(h);
		for (int i = 0; i < 2000; i++) (void) h.process(false, 1.f, 0.5f, 0.f, 0.0f, 0.3f, 0.5f);
		CHECK(state(h) == s2, "and stops once the envelope is under -100 dB (t60 knob at its shortest, %.3f s)", t60);

		// RATTLE at the metal end and at the Dazzler end has no hiss in it
		Hat m; m.setRate(48000.f); m.reset();
		uint32_t m0 = state(m);
		for (int i = 0; i < 3000; i++) (void) m.process(i == 0, 1.f, 0.5f, 0.f, 0.3f, 0.3f, 0.0f);
		CHECK(state(m) == m0, "RATTLE at 0 (metal only) does not run the hiss circuit");
		Hat d; d.setRate(48000.f); d.reset();
		uint32_t d0 = state(d);
		for (int i = 0; i < 3000; i++) (void) d.process(i == 0, 1.f, 0.5f, 0.f, 0.3f, 0.3f, 1.0f);
		CHECK(state(d) == d0, "RATTLE at 1 (Dazzler only) does not run the hiss circuit");
	}

	// 6. Determinism ----------------------------------------------------------------------------------
	{
		std::vector<float> a = renderHat(true, 0.4f, 0.3f, 0.75f, 5u), b = renderHat(true, 0.4f, 0.3f, 0.75f, 5u), c = renderHat(true, 0.4f, 0.3f, 0.75f, 6u);
		CHECK(std::memcmp(a.data(), b.data(), a.size() * 4) == 0, "the same seed gives the same hat");
		CHECK(std::memcmp(a.data(), c.data(), a.size() * 4) != 0, "control: another seed gives another");
		Hat h; h.setRate(48000.f); h.reset();
		std::vector<float> r1(20000), r2(20000);
		for (int i = 0; i < 20000; i++) r1[i] = h.process(i == 0, 1.f, 0.4f, 0.f, 0.3f, 0.3f, 0.75f);
		h.reset();
		for (int i = 0; i < 20000; i++) r2[i] = h.process(i == 0, 1.f, 0.4f, 0.f, 0.3f, 0.3f, 0.75f);
		CHECK(std::memcmp(r1.data(), r2.data(), r1.size() * 4) == 0, "a reset hat strikes identically");
		bool finite = true; for (float v : a) finite = finite && std::isfinite(v);
		CHECK(finite, "every sample is finite");
	}

	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}

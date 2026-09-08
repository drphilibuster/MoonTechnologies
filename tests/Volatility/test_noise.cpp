// Volatility's noise colours and random-gate length, tested without Rack.
//
// Two claims are made on the panel and both are easy to get wrong quietly.
//
// COLOUR says it changes the spectrum and not the volume. A filter fed by a
// per-sample white source gets quieter as the sample rate rises -- white's
// spectral density halves when the rate doubles at the same RMS -- so a fixed
// output gain would make COLOUR a volume control that behaves differently on
// every engine setting. And three "colours" that were really three gains would
// pass any level check while sounding identical, so the slopes are measured
// too: white, pink and red have to be 3 dB/octave apart, in that order.
//
// LENGTH says a short gate makes two successes readable as two events and a
// full one makes them one unbroken high. Both halves matter: without the first
// nothing downstream can count the draws, and a full gate that dropped a
// single sample between two successes would put a spurious extra trigger into
// everything downstream instead.

#include "../../src/Volatility/Noise.hpp"

#include <cmath>
#include <cstdio>
#include <cstdint>

using namespace volatility;

static int checks = 0;
static int failures = 0;

static void check(const char* what, bool ok, const char* detail = 0) {
	checks++;
	if (!ok) {
		failures++;
		printf("  FAIL  %s%s%s\n", what, detail ? ": " : "", detail ? detail : "");
	}
}

/** A repeatable white source at the module's own +/-5 V, so a failure is the
    same failure every run. */
struct White {
	uint32_t s;
	White(uint32_t seed = 12345u) : s(seed) {}
	float next() {
		s = s * 1664525u + 1013904223u;
		return ((float)(s >> 8) / 8388608.f - 1.f) * 5.f;
	}
};

/** Constant-Q bandpass, for measuring how energy is spread across octaves.
    Its bandwidth grows with centre frequency, so white measures as +3 dB per
    octave through it, pink as flat and red as -3 -- only the differences
    between the three are asserted, so the filter's own gain cancels out. */
static double bandRms(const float* x, int n, float fs, float fc, int skip) {
	double lp = 0.0, bp = 0.0;
	double f = 2.0 * std::sin(3.14159265358979 * fc / fs);
	double q = 0.35;
	double acc = 0.0;
	int used = 0;
	for (int i = 0; i < n; i++) {
		double hp = x[i] - lp - q * bp;
		bp += f * hp;
		lp += f * bp;
		if (i >= skip) { acc += bp * bp; used++; }
	}
	return used ? std::sqrt(acc / used) : 0.0;
}

/** dB per octave of a colour, measured over six octaves from 100 Hz. */
static double slopePerOctave(const float* x, int n, float fs) {
	double first = bandRms(x, n, fs, 100.f, (int)fs / 4);
	double last = bandRms(x, n, fs, 3200.f, (int)fs / 4);
	if (first <= 0.0 || last <= 0.0)
		return 0.0;
	return 20.0 * std::log10(last / first) / 5.0;   // 100 -> 3200 is 5 octaves
}

static const int kN = 400000;
static float bufW[kN], bufP[kN], bufR[kN];

/** Fill the three colour buffers at `fs`, and return white's RMS. */
static double render(float fs) {
	NoiseColours n;
	n.build(fs);
	n.reset();
	White w(999u);
	for (int i = 0; i < kN; i++) {
		float x = w.next();
		float p, r;
		n.step(x, p, r);
		bufW[i] = x; bufP[i] = p; bufR[i] = r;
	}
	double acc = 0.0;
	for (int i = 0; i < kN; i++) acc += (double)bufW[i] * bufW[i];
	return std::sqrt(acc / kN);
}

static double rmsOf(const float* x, int skip) {
	double acc = 0.0;
	for (int i = skip; i < kN; i++) acc += (double)x[i] * x[i];
	return std::sqrt(acc / (kN - skip));
}

int main() {
	printf("Volatility noise colours and gate length\n");
	static const float kRates[3] = { 44100.f, 48000.f, 96000.f };

	// --- COLOUR is a spectrum control, not a volume control --------------------
	// All three colours at white's own RMS, at every sample rate. The rate part
	// is the one that rots silently: tuned at 44.1 kHz and left there, pink and
	// red would each drop about 1.5 dB going to 96 kHz.
	{
		bool ok = true;
		char detail[256] = "";
		for (int r = 0; r < 3; r++) {
			double w = render(kRates[r]);
			double p = rmsOf(bufP, (int)kRates[r]);
			double d = rmsOf(bufR, (int)kRates[r]);
			if (std::fabs(p / w - 1.0) > 0.08 || std::fabs(d / w - 1.0) > 0.08) {
				ok = false;
				snprintf(detail, sizeof detail,
				         "at %.0f Hz white %.3f, pink %.3f, red %.3f",
				         kRates[r], w, p, d);
				break;
			}
		}
		check("every colour sits at white's RMS, at every rate", ok, detail);
	}

	// --- the three colours really are three slopes -----------------------------
	// Pink is 3 dB/octave below white and red another 3 below pink. Three gains
	// wearing colour names would pass the level check above and fail here.
	{
		render(44100.f);
		double sw = slopePerOctave(bufW, kN, 44100.f);
		double sp = slopePerOctave(bufP, kN, 44100.f);
		double sr = slopePerOctave(bufR, kN, 44100.f);
		char detail[256];
		snprintf(detail, sizeof detail,
		         "white %+.2f, pink %+.2f, red %+.2f dB/oct", sw, sp, sr);
		bool ok = std::fabs((sw - sp) - 3.0) < 0.9 && std::fabs((sp - sr) - 3.0) < 0.9;
		check("pink is 3 dB/oct under white, red 3 under pink", ok, detail);
	}

	// --- red's leak actually catches it ----------------------------------------
	// An integrator without a leak is a random walk, and a random walk fed to a
	// patch wanders off as a DC offset and stays there. The 20 Hz corner is what
	// stops that; without it this mean grows without bound.
	{
		render(44100.f);
		double mean = 0.0;
		for (int i = 44100; i < kN; i++) mean += bufR[i];
		mean /= (kN - 44100);
		char detail[128];
		snprintf(detail, sizeof detail, "mean %.4f V", mean);
		check("red does not wander off as DC", std::fabs(mean) < 0.5, detail);
	}

	// --- a nonsense sample rate cannot poison the filters ----------------------
	// build() divides by the rate. Rack calls onSampleRateChange before the
	// engine has necessarily settled, and a zero there would make every
	// coefficient a NaN -- which the filter states then hold forever, so the
	// module would never produce a number again for the rest of the session.
	// The guard has to leave the last good build in place.
	{
		NoiseColours n;
		n.build(44100.f);
		n.reset();
		n.build(0.f);
		n.build(-48000.f);
		White w(7u);
		int bad = 0;
		for (int i = 0; i < 20000; i++) {
			float p, r;
			n.step(w.next(), p, r);
			if (!std::isfinite(p) || !std::isfinite(r)) bad++;
		}
		char detail[96];
		snprintf(detail, sizeof detail, "%d non-finite samples", bad);
		check("a bad sample rate leaves the last good build alone", bad == 0, detail);
	}

	// --- COLOUR's endpoints and its crossfade ----------------------------------
	// Full CCW has to be exactly white and full CW exactly red, with pink at the
	// centre -- a mix that never quite reached an endpoint would mean the panel
	// cannot actually select the colour its label names.
	{
		bool ends = colourMix(1.f, 2.f, 3.f, 0.f) == 1.f
		         && colourMix(1.f, 2.f, 3.f, 0.5f) == 2.f
		         && colourMix(1.f, 2.f, 3.f, 1.f) == 3.f;
		int bad = 0;
		for (int i = 0; i <= 1000; i++) {
			float k = (float)i / 1000.f;
			float y = colourMix(1.f, 2.f, 3.f, k);
			float want = (k < 0.5f) ? 1.f + 2.f * k : 2.f + 2.f * (k - 0.5f);
			if (std::fabs(y - want) > 1e-5f) bad++;
		}
		check("COLOUR reaches each colour and crossfades between them",
		      ends && bad == 0);
	}

	// --- a full-length gate is unbroken across consecutive successes -----------
	// This is the whole reason full length latches rather than running a timer
	// for exactly one period: a single low sample between two successes reads
	// downstream as an extra trigger that the draw never produced.
	{
		// On a *slowing* clock, which is the case that separates a latch from
		// a timer: the period is measured from the last interval, so a timer
		// set to exactly one of those expires early and leaves a hole in the
		// middle of what should be one unbroken high.
		GateStretcher g;
		float dt = 1.f / 44100.f;
		int lowSamples = 0;
		int i = 0, period = 441, edges = 0;
		int next = 0;
		for (; i < 44100 * 3; i++) {
			bool edge = (i == next);
			if (edge) {
				next = i + period;
				period += period / 8;         // each period 12.5% longer
				edges++;
			}
			g.tick(dt, edge);
			if (edge && edges > 1) g.fire(1.f);
			if (edges > 1 && !g.high()) lowSamples++;
		}
		char detail[128];
		snprintf(detail, sizeof detail, "%d low samples over %d draws",
		         lowSamples, edges);
		check("full length never drops between two successes", lowSamples == 0, detail);
	}

	// --- a short gate falls before the next edge -------------------------------
	// Two successes have to read as two events. If the gate always filled the
	// period, nothing downstream could count the draws.
	{
		GateStretcher g;
		float dt = 1.f / 44100.f;
		int period = 4410;              // 10 Hz clock, so a 20% gate is 20 ms
		int rises = 0;
		bool prev = false;
		for (int i = 0; i < period * 12; i++) {
			bool edge = (i % period) == 0;
			g.tick(dt, edge);
			if (edge && i > 0) g.fire(0.2f);
			bool now = g.high();
			if (i > period && now && !prev) rises++;
			prev = now;
		}
		char detail[128];
		snprintf(detail, sizeof detail, "%d rising edges over 10 draws", rises);
		check("a short gate makes one countable event per draw", rises == 10, detail);
	}

	// --- the gate never outlasts the period it belongs to ----------------------
	// A gate longer than the clock would run into the next draw, so a failed
	// draw could not be heard as a gap.
	{
		// On an *accelerating* clock, the case a measured period gets wrong in
		// the dangerous direction: the last interval was longer than the one
		// now running, so a timer set from it would spill past the next edge
		// and fill a period whose own draw had failed. Fire once, then assert
		// the gate is down from the following edge onward -- a draw that is
		// still audible during the next draw's period is the bug.
		bool ok = true;
		char detail[192] = "";
		float dt = 1.f / 44100.f;
		for (int li = 0; li <= 10 && ok; li++) {
			float len = (float)li / 10.f;
			GateStretcher g;
			int period = 8000, next = 0, edges = 0;
			bool fired = false, closed = false;
			int lateSamples = 0;
			for (int i = 0; i < 44100 * 2; i++) {
				bool edge = (i == next);
				if (edge) {
					next = i + period;
					period -= period / 8;      // each period 12.5% shorter
					if (period < 200) period = 200;
					edges++;
					if (fired) closed = true;  // the edge after the draw
				}
				g.tick(dt, edge);
				// One success, then only failures: the gate has to end by the
				// next edge rather than be ended by a later success.
				if (edge && edges == 3) { g.fire(len); fired = true; closed = false; }
				if (closed && g.high()) lateSamples++;
			}
			if (lateSamples > 0) {
				ok = false;
				snprintf(detail, sizeof detail,
				         "length %.1f: still high %d samples into the next period",
				         len, lateSamples);
			}
		}
		check("a gate never outlasts one clock period", ok, detail);
	}

	// --- even the shortest setting is visible ----------------------------------
	// Length at zero still has to make a trigger something can see, not a gate
	// so short it falls between two of a downstream module's samples.
	{
		bool ok = true;
		char detail[160] = "";
		float dt = 1.f / 44100.f;
		for (int pi = 0; pi < 3 && ok; pi++) {
			int period = (pi == 0) ? 220 : (pi == 1) ? 4410 : 44100;
			GateStretcher g;
			int high = 0;
			for (int i = 0; i < period * 6; i++) {
				bool edge = (i % period) == 0;
				g.tick(dt, edge);
				if (edge && i == period * 3) g.fire(0.f);
				if (i >= period * 3 && i < period * 4 && g.high()) high++;
			}
			int want = (period / 2 < 44) ? period / 2 : 44;   // 1 ms, or half
			if (high < want - 2) {
				ok = false;
				snprintf(detail, sizeof detail,
				         "period %d: %d samples high, wanted about %d",
				         period, high, want);
			}
		}
		check("length at zero still makes a visible trigger", ok, detail);
	}

	// --- the period comes from the clock's own edges ---------------------------
	// Not from RATE, which says nothing at all when CLOCK IN is patched. A
	// stretcher that read the knob would give the wrong gate length for every
	// external clock.
	{
		GateStretcher g;
		float dt = 1.f / 44100.f;
		int period = 2205;              // 20 Hz
		for (int i = 0; i < period * 5; i++)
			g.tick(dt, (i % period) == 0);
		float want = period * dt;
		char detail[128];
		snprintf(detail, sizeof detail, "measured %.5f s, clock is %.5f s",
		         g.periodSec, want);
		check("the period is measured from the clock", 
		      std::fabs(g.periodSec - want) < want * 0.02f, detail);
	}

	printf("%s  %d checks, %d failures\n", failures ? "FAILED" : "ok",
	       checks, failures);
	return failures ? 1 : 0;
}

// Racketeer's PT2399 loop, swept for NaN and for a feedback path that gets
// away from itself.
//
// This one is the most deliberately unstable thing in the plugin: ECHO runs to
// 1.5, well past unity, because a PT2399 delay that cannot be pushed into
// runaway self-oscillation is not the pedal being modelled. So "the output got
// loud" is not a bug here, and the test cannot simply watch for a big number.
// What it checks is that the loop stays *bounded* -- the chip's op-amp clip at
// the modulator's input is what guarantees that -- and never goes non-finite.
// It also checks the chip itself: the PT2399 is a 1-bit delta modulator on a
// 44 kbit RAM, so delay is RAM / clock, and the loop's behaviour past that
// (arrival time, replay at a new clock, sustain and decay either side of unity)
// follows from the model rather than being tuned.
//
// The corners that matter:
//
//   echo > 1        self-oscillation, on purpose. Must sustain, not diverge.
//   filterInLoop    with the filter inside, resonance multiplies feedback.
//   delaySec moving TIME is read by interpolation, so a swept delay bends
//                   pitch and walks the read pointer against the write pointer.
//   d at the rails  kMinDelay/kMaxDelay clamp TIME; the bit clock is 44 kbit / d,
//                   so the shortest delay is the fastest clock (1.47 Mbit/s).
//   oversample      the module runs the loop at 1x or 2x, which changes fs
//                   underneath every coefficient in here.

#include "../../src/Racketeer/Pt2399Loop.hpp"

#include <cmath>
#include <cstdio>

using racketeer::Pt2399Loop;

static int checks = 0;
static int failures = 0;

// The modulator's input is clipped at the chip's rail, so what is written is
// bounded by construction and the filters around it have unity-ish gain. Anything past this is a real loss of control, not a loud delay.
static const float SANE = 50.f;

static bool run(float echo, float delaySec, float cutHz, float res,
                bool filterInLoop, float chipNoise, int os, float base,
                bool sweepTime, const char*& why, int& badSample, float& badValue) {
	Pt2399Loop loop;
	loop.setSampleRate(base, os);
	loop.setFilter(cutHz, res);
	loop.delaySec = delaySec;
	loop.echo = echo;
	loop.polarity = 1.f;
	loop.loopGain = 1.f;
	loop.chop = 1.f;
	loop.filterInLoop = filterInLoop;
	loop.kill = false;
	loop.chipNoise = chipNoise;

	const float fs = base * (float) os;
	const int n = (int) (fs * 3.f);   // long enough for a 1.2 s delay to wrap

	for (int i = 0; i < n; i++) {
		const float t = (float) i / fs;

		// Feed it hard for a moment, then let go: at echo > 1 the interesting
		// part is what the loop does on its own after the input stops.
		float x;
		if (t < 0.5f)
			x = std::sin(2.f * (float) M_PI * 220.f * t);
		else if (t < 0.55f)
			x = 1.f;                    // a step, straight into the tanh
		else
			x = 0.f;

		// TIME under a slow sweep: the read pointer chases the write pointer and
		// the interpolation runs at every fractional offset.
		if (sweepTime) {
			const float u = 0.5f + 0.5f * std::sin(2.f * (float) M_PI * 0.7f * t);
			loop.delaySec = Pt2399Loop::kMinDelay
			              + u * (Pt2399Loop::kMaxDelay - Pt2399Loop::kMinDelay);
		}

		float dirty = 0.f;
		const float y = loop.process(x, dirty);

		const float vals[2] = { y, dirty };
		for (int k = 0; k < 2; k++) {
			if (!std::isfinite(vals[k]) || std::fabs(vals[k]) > SANE) {
				why = std::isnan(vals[k]) ? "NaN"
				    : (std::isinf(vals[k]) ? "infinity" : "runaway");
				badSample = i;
				badValue = vals[k];
				return false;
			}
		}
	}
	return true;
}

int main() {
	const float bases[] = { 44100.f, 96000.f };
	const int oss[] = { 1, 2 };
	// Past unity on purpose: 1.5 is what the panel allows.
	const float echos[] = { 0.f, 0.7f, 1.0f, 1.25f, 1.5f };
	const float delays[] = { Pt2399Loop::kMinDelay, 0.19f, Pt2399Loop::kMaxDelay };
	const float cutoffs[] = { 40.f, 3000.f, 18000.f };
	const float resos[] = { 0.f, 1.f };

	const int NB = (int) (sizeof(bases) / sizeof(bases[0]));
	const int NO = (int) (sizeof(oss) / sizeof(oss[0]));
	const int NE = (int) (sizeof(echos) / sizeof(echos[0]));
	const int ND = (int) (sizeof(delays) / sizeof(delays[0]));
	const int NC = (int) (sizeof(cutoffs) / sizeof(cutoffs[0]));
	const int NQ = (int) (sizeof(resos) / sizeof(resos[0]));

	printf("Racketeer: %d echo x %d delays x %d cutoffs x %d res x %d os x %d rates,\n"
	       "           each with the filter in and out of the loop\n",
	       NE, ND, NC, NQ, NO, NB);

	// One check for the sweep, not one per setting: the property is "the loop
	// stays bounded", and it is that whether it took one combination to say so
	// or seven hundred. Every combination still runs; the first ten failures
	// print in full so a fault is diagnosable.
	{
	  int bad = 0, total = 0;
	  for (int b = 0; b < NB; b++)
	   for (int o = 0; o < NO; o++)
	    for (int e = 0; e < NE; e++)
	     for (int d = 0; d < ND; d++)
	      for (int c = 0; c < NC; c++)
	       for (int q = 0; q < NQ; q++)
	        for (int f = 0; f < 2; f++) {
	          const char* why = "";
	          int bs = -1;
	          float bv = 0.f;
	          total++;
	          if (!run(echos[e], delays[d], cutoffs[c], resos[q], f != 0, 1.f,
	                   oss[o], bases[b], false, why, bs, bv)) {
	            if (bad < 10)
	              printf("    echo=%.2f delay=%.3f cut=%-6.0f res=%.0f %s "
	                     "os=%d sr=%.0f  %s at sample %d (%g)\n",
	                     echos[e], delays[d], cutoffs[c], resos[q],
	                     f ? "in-loop " : "post    ", oss[o], bases[b], why, bs, bv);
	            bad++;
	          }
	        }
	  checks++;
	  if (bad) {
	    failures++;
	    printf("  FAIL  the loop stays bounded: %d of %d settings broke\n", bad, total);
	  }
	}
	printf("T1  the loop stays bounded at every echo, delay and filter setting\n");

	// --- T2: TIME swept while the loop is self-oscillating -------------------
	// The read pointer moves against the write pointer under interpolation, at
	// feedback past unity, with the filter inside the loop. This is the setting
	// a patch reaches for and the one most likely to walk an index or blow up.
	{
		int bad = 0, total = 0;
		for (int b = 0; b < NB; b++)
			for (int o = 0; o < NO; o++)
				for (int e = 2; e < NE; e++) {   // echo >= 1.0 only
					const char* why = "";
					int bs = -1;
					float bv = 0.f;
					total++;
					if (!run(echos[e], 0.3f, 3000.f, 1.f, true, 1.f, oss[o],
					         bases[b], true, why, bs, bv)) {
						if (bad < 10)
							printf("    swept TIME, echo=%.2f os=%d sr=%.0f  "
							       "%s at sample %d (%g)\n",
							       echos[e], oss[o], bases[b], why, bs, bv);
						bad++;
					}
				}
		checks++;
		if (bad) {
			failures++;
			printf("  FAIL  sweeping TIME while self-oscillating stays bounded:"
			       " %d of %d settings broke\n", bad, total);
		}
	}
	printf("T2  sweeping TIME while self-oscillating stays bounded\n");

	// --- T3: the delay is RAM / clock ---------------------------------------
	// A burst goes in; the first thing out must arrive one delay later, at any
	// delay and either sample rate. Echo 0: nothing recirculates.
	{
		int bad = 0;
		const float ds[] = { 0.030f, 0.1f, 0.34f, 1.2f };
		for (int b = 0; b < NB; b++)
			for (int di = 0; di < 4; di++) {
				Pt2399Loop loop;
				loop.setSampleRate(bases[b], 1);
				loop.setFilter(18000.f, 0.f);
				loop.delaySec = ds[di];
				loop.echo = 0.f;
				loop.chipNoise = 0.f;
				const float fs = bases[b];
				const int n = (int) (fs * (ds[di] + 0.15f));
				const int burst = (int) (fs * 0.004f);
				int first = -1;
				float peak = 0.f;
				for (int i = 0; i < n; i++) {
					float x = i < burst ? 0.5f * std::sin(2.f * (float) M_PI * 1000.f * i / fs) : 0.f;
					float dirty;
					float y = loop.process(x, dirty);
					if (i >= burst) peak = std::fmax(peak, std::fabs(y));
					// "arrived": clearly above the idle chatter of the modulator
					if (first < 0 && i > burst && std::fabs(y) > 0.15f) first = i;
				}
				float arrive = first < 0 ? -1.f : first / fs;
				float err = std::fabs(arrive - ds[di]);
				if (first < 0 || err > 0.003f + 0.05f * ds[di]) {
					printf("    delay %.3f s at %.0f Hz: arrived at %.4f s\n", ds[di], bases[b], arrive);
					bad++;
				}
			}
		checks++;
		if (bad) { failures++; printf("  FAIL  the delay is RAM / clock: %d of 8 off\n", bad); }
	}
	printf("T3  the first echo arrives one RAM-length / clock after the burst\n");

	// --- T4: below unity it dies, past unity it sings ------------------------
	{
		auto tail = [&](float echo) {
			Pt2399Loop loop;
			loop.setSampleRate(48000.f, 1);
			loop.setFilter(18000.f, 0.f);
			loop.delaySec = 0.06f;
			loop.echo = echo;
			loop.chipNoise = 1.f;
			double e = 0; int cnt = 0;
			const int n = 48000 * 12;
			for (int i = 0; i < n; i++) {
				float x = (i < 2400) ? 0.4f * std::sin(2.f * (float) M_PI * 440.f * i / 48000.f) : 0.f;
				float d;
				float y = loop.process(x, d);
				if (i > n - 48000) { e += (double) y * y; cnt++; }
			}
			return std::sqrt(e / cnt);
		};
		float low = tail(0.5f), high = tail(1.5f);
		printf("      tail rms: echo 0.5 -> %.5f, echo 1.5 -> %.4f\n", low, high);
		checks++;
		if (!(low < 0.01f && high > 0.1f)) {
			failures++;
			printf("  FAIL  echo 0.5 should die (<0.01) and 1.5 should sing (>0.1)\n");
		}
	}
	printf("T4  feedback below unity decays, past unity self-oscillates\n");

	// --- T5: a clock change replays what is stored at the new rate -----------
	// Fill the RAM with a tone at one delay, halve the delay, and the tone comes
	// back twice as fast: an octave up. A read-pointer model would instead jump.
	{
		Pt2399Loop loop;
		loop.setSampleRate(48000.f, 1);
		loop.setFilter(18000.f, 0.f);
		loop.echo = 0.f;
		loop.chipNoise = 0.f;
		loop.delaySec = 0.2f;
		const int fill = 48000 / 2;
		for (int i = 0; i < fill; i++) {
			float d;
			loop.process(0.4f * std::sin(2.f * (float) M_PI * 500.f * i / 48000.f), d);
		}
		loop.delaySec = 0.1f;
		// count zero crossings of the output over the next 0.05 s of replay
		int zc = 0; float prev = 0.f;
		const int n = 48000 / 20;
		for (int i = 0; i < n; i++) {
			float d;
			float y = loop.process(0.f, d);
			if (i > 0 && ((prev < 0.f) != (y < 0.f))) zc++;
			prev = y;
		}
		float f = zc / 2.f / 0.05f;
		printf("      500 Hz stored at 0.2 s replays at %.0f Hz after halving the delay\n", f);
		checks++;
		if (f < 800.f || f > 1200.f) {
			failures++;
			printf("  FAIL  replay should be about 1000 Hz\n");
		}
	}
	printf("T5  a clock change replays the stored bits at the new rate\n");

	// --- T6: the bit clock is what TIME says --------------------------------
	{
		Pt2399Loop loop;
		loop.setSampleRate(48000.f, 1);
		float dd = 0.f;
		loop.delaySec = Pt2399Loop::kMinDelay; loop.process(0.f, dd);
		double fast = loop.fsInternal;
		loop.delaySec = Pt2399Loop::kMaxDelay; loop.process(0.f, dd);
		double slow = loop.fsInternal;
		printf("      clock %.3f Mbit/s at %.0f ms, %.1f kbit/s at %.1f s\n",
		       fast * 1e-6, Pt2399Loop::kMinDelay * 1e3, slow * 1e-3, Pt2399Loop::kMaxDelay);
		checks++;
		if (std::fabs(fast - 44000.0 / 0.030) > 1.0 || std::fabs(slow - 44000.0 / 1.2) > 1.0) {
			failures++;
			printf("  FAIL  bit clock should be 44 kbit / delay\n");
		}
	}
	printf("T6  bit clock = 44 kbit / delay\n");

	printf("\n%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}

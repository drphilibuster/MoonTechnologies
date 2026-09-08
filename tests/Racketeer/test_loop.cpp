// Racketeer's PT2399 loop, swept for NaN and for a feedback path that gets
// away from itself.
//
// This one is the most deliberately unstable thing in the plugin: ECHO runs to
// 1.5, well past unity, because a PT2399 delay that cannot be pushed into
// runaway self-oscillation is not the pedal being modelled. So "the output got
// loud" is not a bug here, and the test cannot simply watch for a big number.
// What it checks is that the loop stays *bounded* -- the tanh in the feedback
// path and the sample-and-hold quantiser are what guarantee that -- and never
// goes non-finite.
//
// The corners that matter:
//
//   echo > 1        self-oscillation, on purpose. Must sustain, not diverge.
//   filterInLoop    with the filter inside, resonance multiplies feedback.
//   delaySec moving TIME is read by interpolation, so a swept delay bends
//                   pitch and walks the read pointer against the write pointer.
//   d at the rails  kMinDelay/kMaxDelay clamp the read; fsInternal = kMemory/d
//                   scales the chip clock, so the shortest delay is the fastest
//                   internal rate and the widest reconstruction filter.
//   oversample      the module runs the loop at 1x or 2x, which changes fs
//                   underneath every coefficient in here.

#include "../../src/Racketeer/Pt2399Loop.hpp"

#include <cmath>
#include <cstdio>

using racketeer::Pt2399Loop;

static int checks = 0;
static int failures = 0;

// The loop is bounded by tanh() before anything is written to memory, so the
// ring holds |x| <= 1 by construction and the filters around it have unity-ish
// gain. Anything past this is a real loss of control, not a loud delay.
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

	printf("\n%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}

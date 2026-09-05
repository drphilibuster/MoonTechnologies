// Does the movement statistic actually tell a drone from a patch that is
// playing? The whole going-concern / wind-down verdict rests on that, and on
// the absolute threshold of 1.5 the wind-down test uses, so both are checked
// here against signals whose character we know by construction rather than
// only by ear inside Rack.
#include "../../src/UncertaintyPolicy/Motion.hpp"

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

using namespace upol;

static const float SR = 48000.f;
static const float WINDOW = 1.8f;

static MotionStats run(const std::function<float(float)>& sig) {
	SinkMeter m;
	m.reset();
	m.sizeFor(SR, WINDOW);
	const float hp = 2.f * (float) M_PI * 1000.f / SR;
	const int n = (int) (SR * WINDOW);
	for (int i = 0; i < n; i++)
		m.push(sig((float) i / SR), hp);
	return analyse(m, SR);
}

static int failures = 0;

/** `lo`/`hi` bound the going-concern index; `alo`/`ahi` the wind-down
    articulation. They differ on exactly one signal, which is the point. */
static void expect(const std::string& name, const MotionStats& st,
                   float lo, float hi, float alo, float ahi) {
	const bool ok = st.index >= lo && st.index <= hi &&
	                st.articulation >= alo && st.articulation <= ahi;
	if (!ok)
		failures++;
	std::printf("%-26s alive %7.2f  artic %7.2f  "
	            "(level %5.2f tone %5.2f onsets %5.2f)  %s\n",
	            name.c_str(), st.index, st.articulation, st.levelFluxDb,
	            st.toneFluxDb, st.onsetRate, ok ? "ok" : "FAIL");
}

int main() {
	// A held tone. The case the old audition could not see: loud, unclipped,
	// and completely static.
	expect("held sine (drone)",
	       run([](float t) { return 5.f * std::sin(2.f * (float) M_PI * 220.f * t); }),
	       0.f, 1.5f, 0.f, 1.5f);

	// Steady broadband noise: also a drone, and one whose brightness is high
	// but constant -- so the tone channel must not mistake it for movement.
	expect("steady noise (drone)",
	       run([](float t) {
		       (void) t;
		       static unsigned s = 22222;
		       s = s * 1103515245u + 12345u;
		       return 5.f * ((float) ((s >> 16) & 0x7fff) / 16384.f - 1.f);
	       }),
	       0.f, 1.5f, 0.f, 1.5f);

	// The same tone under a 4 Hz percussive envelope: unmistakably playing.
	expect("enveloped sine (rhythmic)",
	       run([](float t) {
		       const float ph = std::fmod(t * 4.f, 1.f);
		       const float env = std::exp(-ph * 9.f);
		       return 5.f * env * std::sin(2.f * (float) M_PI * 220.f * t);
	       }),
	       6.f, 1e6f, 6.f, 1e6f);

	// A drone with an LFO on the filter: the level barely moves, so this is the
	// case that justifies carrying a brightness channel at all. It has to read
	// as alive -- and, at the same time, as a drone, because that is what it is
	// and wind-down must be able to land on it rather than hunting past it for
	// something completely static.
	expect("drone + cutoff LFO",
	       run([](float t) {
		       const float lfo = 0.5f + 0.5f * std::sin(2.f * (float) M_PI * 0.7f * t);
		       return 3.f * std::sin(2.f * (float) M_PI * 110.f * t) +
		              3.f * lfo * std::sin(2.f * (float) M_PI * 3300.f * t);
	       }),
	       1.5f, 1e6f, 0.f, 1.5f);

	// Silence is caught by the level test long before this one, but it must not
	// register as movement on the way past.
	expect("silence", run([](float t) { (void) t; return 0.f; }),
	       0.f, 1.5f, 0.f, 1.5f);

	std::printf("\n%s\n", failures ? "FAILURES" : "all motion cases behaved");
	return failures ? 1 : 0;
}

// Projection's band analysis and its three renderers.
//
// Two kinds of failure are worth catching here and neither is loud. An analysis
// bug makes a picture that moves, just not with the music -- and a picture that
// moves looks like it works. A renderer bug writes outside its buffer, which on
// a good day is a crash and on a bad one is somebody else's memory, so the
// renderers are given a guarded canvas and every one of them is checked for
// having stayed inside it.
//
// The FFT is not here. It is Rack's, it links against libRack, and what it does
// is not in question; what is in question is where the bands are and what is
// done with them, which is all reachable with a synthetic spectrum.

#include "../../src/Projection/Bands.hpp"
#include "../../src/Projection/Render.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace projection;

static int checks = 0;
static int failures = 0;

static void check(const char* what, bool ok, const char* detail = 0) {
	checks++;
	if (!ok) {
		failures++;
		printf("  FAIL  %s%s%s\n", what, detail ? ": " : "", detail ? detail : "");
	}
}

/** A canvas with a poisoned margin either side, so anything that writes outside
    its own buffer is caught rather than merely being lucky. */
struct Guarded {
	std::vector<uint8_t> mem;
	Canvas c;
	static const size_t PAD = 256;
	Guarded(int w, int h) {
		size_t n = (size_t) w * (size_t) h * 4u;
		mem.assign(n + 2 * PAD, 0xA5);
		c.px = &mem[PAD];
		c.w = w;
		c.h = h;
		std::memset(c.px, 0, n);
	}
	bool intact() const {
		for (size_t i = 0; i < PAD; i++)
			if (mem[i] != 0xA5) return false;
		for (size_t i = mem.size() - PAD; i < mem.size(); i++)
			if (mem[i] != 0xA5) return false;
		return true;
	}
	double mean() const {
		double s = 0;
		for (size_t i = 0; i < c.bytes(); i += 4)
			s += c.px[i] + c.px[i + 1] + c.px[i + 2];
		return s / (double)(c.bytes() / 4 * 3);
	}
};

int main() {
	printf("Projection bands and renderers\n");
	const int FFT = 2048;
	const float SR = 44100.f;
	const float BINHZ = SR / (float) FFT;

	// --- bands are ordered, and none of them is empty -------------------------
	// A log scale at the bottom of the range asks for bands narrower than a bin,
	// so edges have to be forced apart. An empty band would read as permanent
	// silence in that part of the picture, which looks broken rather than quiet.
	{
		bool ok = true;
		char detail[160] = "";
		static const float rates[] = { 44100.f, 48000.f, 96000.f, 192000.f };
		// Small transforms as well as the one actually used. bandEdges takes the
		// size as an argument, and at 256 points there are fewer bins in range
		// than there are bands -- which is the case that makes the edges need
		// pushing apart at the bottom and pulling back down at the top.
		static const int sizes[] = { 256, 512, 2048, 8192 };
		for (int z = 0; z < 4 && ok; z++) {
			for (int r = 0; r < 4 && ok; r++) {
				int e[kBands + 1];
				bandEdges(e, kBands, sizes[z], rates[r]);
				for (int i = 0; i < kBands; i++) {
					if (e[i + 1] <= e[i]) {
						ok = false;
						snprintf(detail, sizeof detail,
						         "%d-point at %.0f Hz: band %d spans %d..%d",
						         sizes[z], rates[r], i, e[i], e[i + 1]);
						break;
					}
				}
				if (ok && (e[0] < 1 || e[kBands] > sizes[z] / 2)) {
					ok = false;
					snprintf(detail, sizeof detail,
					         "%d-point at %.0f Hz: edges %d..%d outside 1..%d",
					         sizes[z], rates[r], e[0], e[kBands], sizes[z] / 2);
				}
			}
		}
		check("every band is non-empty and inside the spectrum", ok, detail);
	}

	// --- bands rise with frequency --------------------------------------------
	{
		int e[kBands + 1];
		bandEdges(e, kBands, FFT, SR);
		bool ok = true;
		for (int i = 0; i <= kBands; i++)
			if (i && e[i] <= e[i - 1]) ok = false;
		check("band edges are strictly increasing", ok);
	}

	// --- a tone lands in the band that contains it ----------------------------
	// The whole point of the analysis. A spike at one bin must light the band
	// whose range covers that bin and, at these widths, essentially nothing else.
	{
		int e[kBands + 1];
		bandEdges(e, kBands, FFT, SR);
		int bad = 0, shown = 0;
		for (int b = 0; b < kBands; b++) {
			int bin = (e[b] + e[b + 1]) / 2;
			std::vector<float> mag(FFT / 2, 0.f);
			mag[bin] = 1.f;
			float out[kBands];
			bandEnergies(&mag[0], FFT / 2, e, kBands, BINHZ, 0.f, out);
			int loudest = 0;
			for (int i = 1; i < kBands; i++)
				if (out[i] > out[loudest]) loudest = i;
			if (loudest != b) {
				bad++;
				if (shown++ < 6)
					printf("    bin %d (band %d) read loudest in band %d\n", bin, b, loudest);
			}
		}
		check("a tone lights its own band", bad == 0);
	}

	// --- tilt flattens pink ----------------------------------------------------
	// Music is roughly pink. Untilted, the bass end of the picture is always lit
	// and the top never moves; TILT 0.5 is the weighting that evens that out.
	{
		int e[kBands + 1];
		bandEdges(e, kBands, FFT, SR);
		std::vector<float> mag(FFT / 2, 0.f);
		for (int k = 1; k < FFT / 2; k++)
			mag[k] = 1.f / std::sqrt((float) k * BINHZ);      // pink: amplitude ~ 1/sqrt(f)
		float flat[kBands], tilted[kBands];
		bandEnergies(&mag[0], FFT / 2, e, kBands, BINHZ, 0.f, flat);
		bandEnergies(&mag[0], FFT / 2, e, kBands, BINHZ, 0.5f, tilted);
		double sf = flat[0] / (flat[kBands - 1] + 1e-12);
		double st = tilted[0] / (tilted[kBands - 1] + 1e-12);
		char detail[128];
		snprintf(detail, sizeof detail,
		         "bottom/top ratio: untilted %.1f, tilted %.1f", sf, st);
		check("tilt flattens a pink spectrum", st < sf * 0.35, detail);
	}

	// --- the follower rises at once and falls slowly --------------------------
	{
		BandFollower f;
		float in[kBands];
		for (int i = 0; i < kBands; i++) in[i] = 1.f;
		f.step(in, kBands, 1.f / 60.f, 0.3f);
		bool instant = f.v[0] >= 1.f;
		for (int i = 0; i < kBands; i++) in[i] = 0.f;
		f.step(in, kBands, 1.f / 60.f, 0.3f);
		bool stillUp = f.v[0] > 0.9f;             // one frame later, barely moved
		for (int i = 0; i < 60; i++)
			f.step(in, kBands, 1.f / 60.f, 0.3f);
		bool decayed = f.v[0] < 0.1f;             // a second later, gone
		check("the follower attacks instantly and releases slowly",
		      instant && stillUp && decayed);
	}

	// --- and never goes negative or NaN ---------------------------------------
	// A NaN out of the FFT would otherwise be latched forever by the max().
	{
		BandFollower f;
		float in[kBands];
		for (int i = 0; i < kBands; i++) in[i] = std::sqrt(-1.f);
		f.step(in, kBands, 1.f / 60.f, 0.3f);
		bool clean = true;
		for (int i = 0; i < kBands; i++)
			if (!(f.v[i] == f.v[i]) || f.v[i] < 0.f) clean = false;
		for (int i = 0; i < kBands; i++) in[i] = -5.f;
		f.step(in, kBands, 1.f / 60.f, 0.3f);
		for (int i = 0; i < kBands; i++)
			if (f.v[i] < 0.f) clean = false;
		check("a NaN or a negative cannot be latched", clean);
	}

	// --- onset fires on a transient and not on a steady tone ------------------
	{
		Onset on;
		float q[kBands];
		int steadyFires = 0;
		for (int i = 0; i < kBands; i++) q[i] = 0.3f;
		for (int i = 0; i < 120; i++)
			if (on.step(q, kBands, 1.f / 60.f, 0.3f)) steadyFires++;
		for (int i = 0; i < kBands; i++) q[i] = 3.0f;
		bool fired = false;
		for (int i = 0; i < 3; i++)
			if (on.step(q, kBands, 1.f / 60.f, 0.3f)) fired = true;
		char detail[96];
		snprintf(detail, sizeof detail, "%d spurious fires on a steady tone", steadyFires);
		// One fire as the steady tone first arrives is correct; more is chatter.
		check("onset fires on a step and not on a hold", fired && steadyFires <= 1, detail);
	}

	// --- every renderer stays inside its buffer -------------------------------
	// The check that matters most, because overrunning is silent when it is not
	// fatal. Deliberately awkward sizes, and inputs well outside their range.
	{
		bool ok = true;
		char detail[160] = "";
		static const int sizes[][2] = { {1,1}, {3,7}, {17,5}, {160,90}, {321,181} };
		float bands[kBands];
		for (int i = 0; i < kBands; i++) bands[i] = 0.7f;
		std::vector<float> xs(256), ys(256);
		for (int i = 0; i < 256; i++) {
			xs[i] = (i % 2) ? 12.f : -12.f;      // far outside +/-1, both ways
			ys[i] = std::sin(i * 0.3f) * 9.f;
		}
		Look look; look.trail = 0.6f; look.flash = 0.5f;
		for (int s = 0; s < 5 && ok; s++) {
			for (int mode = 0; mode < 3 && ok; mode++) {
				Guarded g(sizes[s][0], sizes[s][1]);
				clear(g.c);
				fade(g.c, look.trail, 1.f / 30.f);
				if (mode == 0) renderScope(g.c, &xs[0], &ys[0], 256, look);
				else if (mode == 1) renderBars(g.c, bands, kBands, look);
				else renderField(g.c, bands, kBands, 0.4f, look);
				applyFlash(g.c, look.flash);
				if (!g.intact()) {
					ok = false;
					snprintf(detail, sizeof detail, "mode %d wrote outside a %dx%d canvas",
					         mode, sizes[s][0], sizes[s][1]);
				}
			}
		}
		check("no renderer writes outside its canvas", ok, detail);
	}

	// --- a signal past the edge piles up on the edge, it does not wrap ---------
	// An oscilloscope clips. Wrapping would put a loud left channel on the right
	// of the picture, which reads as a glitch nobody asked for.
	{
		Guarded g(64, 64);
		clear(g.c);
		float xs[2] = { 8.f, 8.f }, ys[2] = { 0.f, 0.f };   // hard right, twice
		renderScope(g.c, xs, ys, 2, Look());
		// Every lit pixel has to be in the last column. Checking only that the
		// left half is dark is not enough -- a wrap lands wherever the modulus
		// puts it, which for one particular over-range value is the right half,
		// and the check would pass on a signal that had in fact wrapped.
		int litOffEdge = 0, litOnEdge = 0;
		for (int y = 0; y < 64; y++) {
			for (int x = 0; x < 64; x++) {
				const uint8_t* p = g.c.px + ((size_t) y * 64 + x) * 4;
				if (p[0] || p[1] || p[2]) {
					if (x == 63) litOnEdge++;
					else litOffEdge++;
				}
			}
		}
		char detail[128];
		snprintf(detail, sizeof detail, "%d lit on the edge, %d lit away from it",
		         litOnEdge, litOffEdge);
		check("an over-range signal clips to the edge rather than wrapping",
		      litOnEdge > 0 && litOffEdge == 0, detail);
	}

	// --- silence is black, and loud is not ------------------------------------
	{
		Guarded a(64, 64), b(64, 64);
		float quiet[kBands], loud[kBands];
		for (int i = 0; i < kBands; i++) { quiet[i] = 0.f; loud[i] = 1.f; }
		Look look;
		clear(a.c); renderBars(a.c, quiet, kBands, look);
		clear(b.c); renderBars(b.c, loud, kBands, look);
		char detail[96];
		snprintf(detail, sizeof detail, "silent %.3f, loud %.3f", a.mean(), b.mean());
		check("bars are dark on silence and lit on signal",
		      a.mean() == 0.0 && b.mean() > 40.0, detail);
	}

	// --- a trail decays at the same rate whatever the frame rate --------------
	// TRAIL is per second, not per frame. A per-frame factor would make the look
	// change with the output rate, which is not what that control says it does.
	{
		double after[2];
		static const float dts[2] = { 1.f / 24.f, 1.f / 60.f };
		for (int r = 0; r < 2; r++) {
			Guarded g(32, 32);
			std::memset(g.c.px, 200, g.c.bytes());
			// half a second of fading, however many frames that takes
			int frames = (int) (0.5f / dts[r] + 0.5f);
			for (int i = 0; i < frames; i++)
				fade(g.c, 0.5f, dts[r]);
			after[r] = g.mean();
		}
		char detail[128];
		snprintf(detail, sizeof detail, "24 fps left %.1f, 60 fps left %.1f",
		         after[0], after[1]);
		// 8, because the two rates measure 5 apart when the decay is right and
		// 10 apart when the per-frame bias is back. The 5 is 8-bit truncation
		// and cannot be removed without a wider buffer; anything much above it
		// is the frame rate leaking into what TRAIL means.
		check("trail decay is per second, not per frame",
		      std::fabs(after[0] - after[1]) < 8.0, detail);
	}

	// --- and a trail always reaches black ------------------------------------
	// An integer shift that rounded up would leave a permanent ghost, and the
	// picture would slowly fill in and never recover.
	{
		Guarded g(32, 32);
		std::memset(g.c.px, 255, g.c.bytes());
		for (int i = 0; i < 2000; i++)
			fade(g.c, 0.9f, 1.f / 60.f);
		char detail[96];
		snprintf(detail, sizeof detail, "mean after 33 s of fading: %.3f", g.mean());
		check("a trail fades to black rather than to a ghost", g.mean() == 0.0, detail);
	}

	printf("%s  %d checks, %d failures\n", failures ? "FAILED" : "ok",
	       checks, failures);
	return failures ? 1 : 0;
}

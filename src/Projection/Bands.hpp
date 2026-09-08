#pragma once
#include <cmath>
#include <cstring>

// ---------------------------------------------------------------------------
// Turning a spectrum into something a picture can be driven by.
//
// The FFT itself is Rack's (pffft, through dsp::RealFFT) and stays in the .cpp,
// because it links against libRack and a test cannot. Everything here is the
// part that decides what the numbers *mean* -- where the bands are, how energy
// is gathered into them, and how they move -- which is the part with opinions in
// it and therefore the part worth testing.
//
// Two of those opinions matter enough to name:
//
//   * Bands are spaced logarithmically, because hearing is. Linear bands put
//     three quarters of their resolution above 5 kHz, where almost nothing in a
//     mix lives, and the picture ends up driven by cymbals and nothing else.
//
//   * Energy is tilted before it is used. Music is roughly pink -- power falls
//     about 3 dB per octave -- so untilted bands make a picture whose bass end
//     is permanently lit and whose top end never moves. TILT at 0.5 is the
//     square-root-of-frequency weighting that flattens pink noise; 0 leaves the
//     spectrum honest, 1 over-corrects and favours the top.

namespace projection {

static const int kBands = 16;
static const float kFreqLo = 40.f;
static const float kFreqHi = 12000.f;

/** Bin index of each band's lower edge, plus a final upper edge -- so band `i`
    covers `edges[i] .. edges[i+1]-1` and there are `nBands + 1` entries.

    Edges are forced to be strictly increasing even when the maths would put two
    in the same bin, which it does at the bottom: at 2048 points and 44.1 kHz a
    bin is 21.5 Hz, so the first few log-spaced bands are narrower than one bin.
    An empty band would read as permanent silence in that part of the picture,
    which looks like a broken module rather than like quiet. */
inline void bandEdges(int* edges, int nBands, int fftSize, float sampleRate,
                      float fLo = kFreqLo, float fHi = kFreqHi) {
	int nBins = fftSize / 2;
	float binHz = sampleRate / (float) fftSize;
	if (fLo < binHz) fLo = binHz;
	if (fHi > sampleRate * 0.5f) fHi = sampleRate * 0.5f;
	float lo = std::log(fLo), hi = std::log(fHi);
	for (int i = 0; i <= nBands; i++) {
		float f = std::exp(lo + (hi - lo) * (float) i / (float) nBands);
		int b = (int) (f / binHz + 0.5f);
		if (b < 1) b = 1;
		if (b > nBins) b = nBins;
		if (i > 0 && b <= edges[i - 1])
			b = edges[i - 1] + 1;
		if (b > nBins) b = nBins;
		edges[i] = b;
	}
}

/** Mean magnitude per band, tilted. `mag` is `nBins` magnitudes, bin 0 being DC.

    Mean rather than sum: a log band at the top spans hundreds of bins and one
    at the bottom spans one, so summing would make the answer a measure of
    bandwidth more than of loudness. */
inline void bandEnergies(const float* mag, int nBins, const int* edges,
                         int nBands, float binHz, float tilt, float* out) {
	for (int i = 0; i < nBands; i++) {
		int a = edges[i], b = edges[i + 1];
		if (a < 0) a = 0;
		if (b > nBins) b = nBins;
		float acc = 0.f;
		int n = 0;
		for (int k = a; k < b; k++) {
			float w = 1.f;
			if (tilt != 0.f) {
				// f^tilt, normalised at 1 kHz so TILT does not also change the
				// overall brightness of the picture.
				float f = (float) k * binHz;
				if (f < 1.f) f = 1.f;
				w = std::pow(f / 1000.f, tilt);
			}
			acc += mag[k] * w;
			n++;
		}
		out[i] = n ? acc / (float) n : 0.f;
	}
}

/** Fast up, slow down, per band.

    The same ballistics a meter wants and for the same reason: a picture that
    tracked the spectrum exactly would flicker at the frame rate, and one that
    smoothed both directions equally would miss every transient. Attack is
    immediate by default so a kick lands on the frame it happened. */
struct BandFollower {
	float v[kBands];

	BandFollower() { reset(); }

	void reset() {
		for (int i = 0; i < kBands; i++)
			v[i] = 0.f;
	}

	void step(const float* in, int n, float dt, float releaseSec) {
		if (n > kBands)
			n = kBands;
		float k = (releaseSec > 1e-4f) ? std::exp(-dt / releaseSec) : 0.f;
		for (int i = 0; i < n; i++) {
			// The comparison is what keeps this safe, so do not replace it with
			// anything that averages the input in. Every comparison against a
			// NaN is false, so a NaN out of the FFT takes the decay branch and
			// is gone by the next frame; an averaging follower would latch it
			// and that band would never light again for the rest of the
			// session. A negative goes the same way, for the same reason.
			float x = in[i];
			v[i] = (x > v[i]) ? x : v[i] * k;
		}
	}
};

/** A rough onset: total energy rising sharply above its own running average.

    Deliberately not a spectral-flux detector with a median filter -- this drives
    a flash on a picture, where being early and occasionally wrong beats being
    late and correct. */
struct Onset {
	float slow = 0.f;
	float armed = 0.f;
	bool primed = false;

	void reset() { slow = 0.f; armed = 0.f; primed = false; }

	/** True on the frame a transient starts. `sens` 0..1 raises the threshold
	    from a hair above the average to about three times it. */
	bool step(const float* bands, int n, float dt, float sens) {
		float sum = 0.f;
		for (int i = 0; i < n; i++)
			sum += bands[i];
		sum /= (n > 0) ? (float) n : 1.f;
		// Seeded on the first call rather than started at zero. A running
		// average climbing up from nothing sits below the signal for its whole
		// first time constant, so every steady tone would announce itself as a
		// run of onsets simply for having begun.
		if (!primed) {
			slow = sum;
			primed = true;
		}
		float k = std::exp(-dt / 0.25f);
		float prev = slow;
		slow = sum + (slow - sum) * k;
		// An absolute floor as well as a relative one: near silence the average
		// is tiny and any dither at all clears a purely proportional threshold.
		float thresh = prev * (1.15f + 1.85f * sens) + 1e-3f;
		bool fire = (sum > thresh) && (armed <= 0.f);
		// A short refractory period, or one transient fires on several frames.
		if (fire)
			armed = 0.05f;
		else
			armed -= dt;
		return fire;
	}
};

} // namespace projection

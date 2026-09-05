#pragma once
// Deliberately free of rack.hpp: nothing here needs Rack, and keeping it that
// way means the movement statistic can be exercised against synthetic signals
// offline rather than only by ear inside a running patch. See tests/.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>


namespace upol {


/** A held drone and a sequenced patch are both "loud and unclipped", which is
    why the original audition could not tell them apart -- it measured one RMS
    over the whole window and asked only whether it was above zero. Judging
    whether a patch still *moves* needs the shape of the level over time, not a
    single number for it.

    METER_BLOCKS_MAX is 1024 floats each of two channels: 8 KB, owned outright,
    never resized. Nothing here allocates, locks, or calls anything a realtime
    thread should not. */
static const int METER_BLOCKS_MAX = 1024;


/** Block-RMS envelope of the signal reaching the audio device, plus the same
    thing for a high-passed copy of it.

    The second channel earns its one-pole filter on one case: a drone with an
    LFO on the filter cutoff has an almost flat level envelope and is obviously
    still alive. Brightness catches that where level alone cannot.

    Written by the audio thread inside process(), read by the UI thread only
    after it has observed `active` go false -- the same discipline as PortStats,
    plain floats published behind a released atomic. */
struct SinkMeter {
	// --- audio-thread scratch ---
	double blockSumSq = 0.0;
	double blockHiSumSq = 0.0;
	int blockFill = 0;
	float hpState = 0.f;
	int blockLen = 256;

	// --- published ---
	/** Level and peak so far, republished once per block. The quick level and
	    clipping gate reads these while the meter is still running, and reading a
	    plain float mid-write would be a race however benign it looked in
	    practice -- so the audio thread hands them over properly, at a cost of two
	    relaxed stores per 256 samples. */
	std::atomic<float> liveRms{0.f};
	std::atomic<float> livePeak{0.f};

	float peak = 0.f;
	double totalSumSq = 0.0;
	int64_t totalSamples = 0;
	float rmsBlk[METER_BLOCKS_MAX] = {};
	float briBlk[METER_BLOCKS_MAX] = {};
	int blocks = 0;

	void reset() {
		blockSumSq = blockHiSumSq = 0.0;
		blockFill = 0;
		hpState = 0.f;
		peak = 0.f;
		totalSumSq = 0.0;
		totalSamples = 0;
		blocks = 0;
		liveRms.store(0.f, std::memory_order_relaxed);
		livePeak.store(0.f, std::memory_order_relaxed);
	}

	/** Size a block so the window fills the ring rather than overrunning it.
	    At 192 kHz a 1.8 s window is 345,600 samples, which is 1350 blocks of
	    256 -- so the length has to follow the sample rate, not be a constant.
	    Called on the UI thread before the meter is armed. */
	void sizeFor(float sampleRate, float windowSec) {
		const int wanted = (int) (sampleRate * windowSec / (float) METER_BLOCKS_MAX);
		int len = ((wanted + 63) / 64) * 64;
		blockLen = std::max(256, len);
	}

	inline void push(float v, float hpCoeff) {
		peak = std::max(peak, std::fabs(v));
		totalSumSq += (double) v * (double) v;
		totalSamples++;

		// One-pole high pass: the input minus a lagging copy of itself.
		hpState += hpCoeff * (v - hpState);
		const float hi = v - hpState;

		blockSumSq += (double) v * (double) v;
		blockHiSumSq += (double) hi * (double) hi;
		if (++blockFill >= blockLen) {
			if (blocks < METER_BLOCKS_MAX) {
				const double n = (double) blockFill;
				rmsBlk[blocks] = (float) std::sqrt(blockSumSq / n);
				briBlk[blocks] = (float) std::sqrt(blockHiSumSq / n);
				blocks++;
			}
			blockSumSq = blockHiSumSq = 0.0;
			blockFill = 0;
			livePeak.store(peak, std::memory_order_relaxed);
			liveRms.store(totalSamples > 0
			              ? (float) std::sqrt(totalSumSq / (double) totalSamples)
			              : 0.f,
			              std::memory_order_relaxed);
		}
	}
};


/** What the audition heard. */
struct MotionStats {
	float rms = 0.f;
	float peak = 0.f;
	float levelFluxDb = 0.f;   // how much the level moves
	float toneFluxDb = 0.f;    // ... and how much the brightness does
	float onsetRate = 0.f;     // rising edges per second
	float index = 0.f;         // the three of them, in one number
	/** Movement you could clap along to -- level and onsets, with brightness
	    left out on purpose.

	    The two questions are not the same one. "Is this patch still alive"
	    should count a drone with an LFO on the cutoff as alive, because it is.
	    "Is this patch a drone yet" should count that same sound as a drone,
	    because it also is -- and a good one. Judging wind-down on `index` made
	    the module reject exactly the slowly evolving drones worth having, in
	    favour of dead static ones. */
	float articulation = 0.f;
};


/** Reduce a captured envelope to a movement figure.

    Flux is the standard deviation in the *log* domain, which is scale-invariant
    for free: a trial that comes out quieter or louder than the baseline is not
    thereby judged to be moving less or more. That is the property that makes
    comparing a trial against a baseline honest, and it is why this is not simply
    a coefficient of variation. */
inline MotionStats analyse(const SinkMeter& m, float sampleRate) {
	MotionStats st;
	if (m.totalSamples > 0)
		st.rms = (float) std::sqrt(m.totalSumSq / (double) m.totalSamples);
	st.peak = m.peak;
	if (m.blocks < 3 || sampleRate <= 0.f)
		return st;

	const float eps = 1e-5f;
	double sum = 0.0, sumSq = 0.0;
	double tSum = 0.0, tSumSq = 0.0;
	for (int i = 0; i < m.blocks; i++) {
		const double L = 20.0 * std::log10((double) m.rmsBlk[i] + eps);
		sum += L;
		sumSq += L * L;
		// Brightness as a ratio to level, so it tracks timbre rather than
		// simply restating the level envelope in another unit.
		const double T = 20.0 * std::log10(
		        ((double) m.briBlk[i] + eps) / ((double) m.rmsBlk[i] + eps));
		tSum += T;
		tSumSq += T * T;
	}
	const double n = (double) m.blocks;
	st.levelFluxDb = (float) std::sqrt(std::max(0.0, sumSq / n - (sum / n) * (sum / n)));
	st.toneFluxDb = (float) std::sqrt(std::max(0.0, tSumSq / n - (tSum / n) * (tSum / n)));

	// An onset is a block that jumps clearly above the one before it. Crude
	// against a real onset detector and entirely adequate here, where the
	// question is "is anything being articulated at all".
	int onsets = 0;
	for (int i = 1; i < m.blocks; i++) {
		const float prev = m.rmsBlk[i - 1] + eps;
		if (m.rmsBlk[i] > prev * 1.8f)
			onsets++;
	}
	const float windowSec = (float) (m.blocks * m.blockLen) / sampleRate;
	st.onsetRate = windowSec > 0.f ? (float) onsets / windowSec : 0.f;

	st.index = st.levelFluxDb + 0.5f * st.toneFluxDb + 2.0f * st.onsetRate;
	st.articulation = st.levelFluxDb + 2.0f * st.onsetRate;
	return st;
}


} // namespace upol

// A 1 V/oct quantizer, shared.
//
// Five fixed scales -- chromatic, major, minor, and their pentatonics --
// exactly the varimode quantizer's own set (Day 10's PIC16F684 firmware,
// `varimodequantizer_100.asm`). Payment Schedule uses it on its per-step CV
// path; Dependents uses it on the root, so a chord's fundamental can be
// snapped to a scale instead of swept continuously. One table and one
// nearest-neighbour search back both rather than two copies drifting apart.
//
// Self-contained -- <cmath>, <cstdio>, <string> and nothing else -- so it
// needs no <rack.hpp> shim to test.
#pragma once
#include <cmath>
#include <cstdio>
#include <string>

namespace quant {

static const int NUM_SCALES = 5;
enum ScaleId { SCALE_CHROMATIC, SCALE_MAJOR, SCALE_MINOR, SCALE_MAJ_PENT, SCALE_MIN_PENT };

static const char* const SCALE_NAMES[NUM_SCALES] = {
	"Chromatic", "Major", "Minor", "Major pentatonic", "Minor pentatonic"
};

static const int MAJOR[]     = {0, 2, 4, 5, 7, 9, 11};
static const int MINOR[]     = {0, 2, 3, 5, 7, 8, 10};
static const int MAJ_PENT[]  = {0, 2, 4, 7, 9};
static const int MIN_PENT[]  = {0, 3, 5, 7, 10};
static const int CHROMATIC[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};

struct ScaleDef { const int* notes; int len; };
static const ScaleDef SCALES[NUM_SCALES] = {
	{CHROMATIC, 12}, {MAJOR, 7}, {MINOR, 7}, {MAJ_PENT, 5}, {MIN_PENT, 5}
};

inline int clampi(int x, int lo, int hi) { return x < lo ? lo : (x > hi ? hi : x); }

inline int floorDiv(int a, int b) {
	int q = a / b, r = a % b;
	if (r != 0 && ((r < 0) != (b < 0)))
		q--;
	return q;
}

/** Nearest in-scale voltage to `volts`, a scale rooted `rootSemi` semitones
    above C. Searches the octave the raw value falls in plus one either side,
    so a root near an octave seam still finds its true nearest neighbour. */
inline float quantize(float volts, int scaleIdx, int rootSemi) {
	const ScaleDef& sc = SCALES[clampi(scaleIdx, 0, NUM_SCALES - 1)];
	float semitones = volts * 12.f;
	int nearest = (int) std::floor(semitones + 0.5f);
	int k0 = floorDiv(nearest - rootSemi, 12);

	int bestSemi = rootSemi;
	float bestDist = 1e9f;
	for (int k = k0 - 1; k <= k0 + 1; k++) {
		for (int i = 0; i < sc.len; i++) {
			int absSemi = rootSemi + k * 12 + sc.notes[i];
			float d = std::fabs((float) absSemi - semitones);
			if (d < bestDist) {
				bestDist = d;
				bestSemi = absSemi;
			}
		}
	}
	return (float) bestSemi / 12.f;
}

/** `volts` as a note name -- 0 V is C4, VCV's own convention (dsp::FREQ_C4). */
inline std::string noteName(float volts) {
	static const char* const NAMES[12] = {
		"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
	};
	int semis = (int) std::floor(volts * 12.f + 0.5f);
	int note = ((semis % 12) + 12) % 12;
	int octave = 4 + floorDiv(semis, 12);
	char buf[8];
	snprintf(buf, sizeof buf, "%s%d", NAMES[note], octave);
	return std::string(buf);
}

} // namespace quant

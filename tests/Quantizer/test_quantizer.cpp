// The shared 1 V/oct quantizer -- does it snap to the scale it's told, and
// does the note-name readout agree with VCV's own 0 V = C4 convention?
#include "../../src/Quantizer.hpp"

#include <cmath>
#include <cstdio>

using namespace quant;

static int checks = 0;
static int failures = 0;

static void fail(const char* what, const char* detail) {
	failures++;
	printf("  FAIL  %s: %s\n", what, detail);
}

static void expectNear(const char* what, float got, float want, float tol) {
	checks++;
	if (std::fabs(got - want) > tol) {
		char d[160];
		snprintf(d, sizeof d, "got %.5f, wanted %.5f +/- %.5f", (double) got, (double) want, (double) tol);
		fail(what, d);
	}
}

static void expectStr(const char* what, const std::string& got, const char* want) {
	checks++;
	if (got != want) {
		char d[160];
		snprintf(d, sizeof d, "got \"%s\", wanted \"%s\"", got.c_str(), want);
		fail(what, d);
	}
}

int main() {
	printf("  quantize snaps to the chosen scale...\n");
	{
		// C major rooted at C: a semitone either side of C is equidistant from
		// C and D: the search order (ascending k, ascending scale degree) means
		// C -- the first candidate found -- wins the tie.
		expectNear("C stays C in major", quantize(0.f, SCALE_MAJOR, 0), 0.f, 1e-6f);
		expectNear("C# rounds down to C in major", quantize(1.f / 12.f, SCALE_MAJOR, 0), 0.f, 1e-6f);
		// D is in the scale outright.
		expectNear("D stays D in major", quantize(2.f / 12.f, SCALE_MAJOR, 0), 2.f / 12.f, 1e-6f);
		// Chromatic never moves anything.
		for (int s = -6; s <= 6; s++)
			expectNear("chromatic is the identity", quantize(s / 12.f, SCALE_CHROMATIC, 0),
			           s / 12.f, 1e-6f);
		// A scale rooted away from C: minor rooted at A (9 semitones above C)
		// is the piano's A natural minor, whose degrees are the same as C
		// major's -- so A minor rooted at A should quantize like C major
		// rooted at C, transposed up 9 semitones.
		expectNear("A minor's root", quantize(9.f / 12.f, SCALE_MINOR, 9), 9.f / 12.f, 1e-6f);
	}

	printf("  quantize holds across an octave seam...\n");
	{
		// Nearer B than the C an octave up -- the +/-1 octave search window
		// has to actually reach into the octave above the raw value's own to
		// find it, not just search within the octave semitones%12 falls in.
		float q = quantize(11.4f / 12.f, SCALE_CHROMATIC, 0);
		expectNear("near-B rounds to B, not C an octave up", q, 11.f / 12.f, 1e-6f);
	}

	printf("  noteName agrees with 0 V = C4...\n");
	{
		expectStr("0 V is C4", noteName(0.f), "C4");
		expectStr("-2 V is C2", noteName(-2.f), "C2");
		expectStr("+1 V is C5", noteName(1.f), "C5");
		expectStr("3 semitones up is D#4", noteName(3.f / 12.f), "D#4");
		expectStr("a semitone down is B3", noteName(-1.f / 12.f), "B3");
	}

	printf("%d checks, %d failure%s\n", checks, failures, failures == 1 ? "" : "s");
	return failures ? 1 : 0;
}

// Installment's CURVE shaper, tested without Rack.
//
// CURVE is applied to a finished function-generator output, after the core has
// run, and the whole claim made for it in the header is that it bends the path
// between the endpoints and touches nothing else. That claim is what is tested
// here, because breaking it is quiet: a shaper that moved 10 V would move the
// envelope's peak, one that moved 0 V would leave a DC offset under an idle
// envelope, and one that was not monotonic would put extra turning points in a
// ramp -- none of which announce themselves as a bug, they just sound wrong.
//
// One check per property, over the whole grid of (knob, voltage). The first
// ten offending pairs print, so a failure names where it starts rather than
// only that it happened.

#include "../../src/Installment/Curve.hpp"

#include <cmath>
#include <cstdio>

using namespace installment;

static int checks = 0;
static int failures = 0;

static void check(const char* what, int bad, int total) {
	checks++;
	if (bad) {
		failures++;
		printf("  FAIL  %s: %d of %d\n", what, bad, total);
	}
}

/** The knob grid: both ends, both signs, and the exact zero that must bypass. */
static const int kKnobs = 41;
static float knobAt(int i) { return -1.f + 2.f * (float)i / (float)(kKnobs - 1); }

/** The voltage grid: both endpoints and 200 points between them. */
static const int kVolts = 201;
static float voltAt(int i) { return 10.f * (float)i / (float)(kVolts - 1); }

int main() {
	printf("Installment CURVE\n");

	// --- the endpoints are fixed points --------------------------------------
	// An envelope's peak is 10 V and its rest is 0 V. If either moved with the
	// knob, CURVE would be a level control wearing a shape control's label,
	// and an idle AR would sit off zero.
	{
		int bad = 0, shown = 0;
		for (int k = 0; k < kKnobs; k++) {
			float lo = curveShape(0.f, knobAt(k));
			float hi = curveShape(10.f, knobAt(k));
			if (lo != 0.f || hi != 10.f) {
				bad++;
				if (shown++ < 10)
					printf("    knob %+.2f: 0 V -> %.6f, 10 V -> %.6f\n",
					       knobAt(k), lo, hi);
			}
		}
		check("endpoints are fixed", bad, kKnobs);
	}

	// --- linear is a bypass, exactly ------------------------------------------
	// Not "close to" the input: bit-for-bit the input. A shaper that ran
	// pow(u, 1) at the detent would put a rounding error on every sample of
	// every patch that never touches CURVE.
	{
		int bad = 0, shown = 0;
		for (int i = 0; i < kVolts; i++) {
			float v = voltAt(i);
			if (curveShape(v, 0.f) != v) {
				bad++;
				if (shown++ < 10)
					printf("    %.4f V -> %.9f\n", v, curveShape(v, 0.f));
			}
		}
		check("knob at zero passes through unchanged", bad, kVolts);
	}

	// --- monotonic in voltage -------------------------------------------------
	// A rising ramp has to stay a rising ramp. A fold here would add turning
	// points the core never generated, and an LFO would gain harmonics from
	// nowhere.
	{
		int bad = 0, shown = 0, total = 0;
		for (int k = 0; k < kKnobs; k++) {
			float knob = knobAt(k);
			for (int i = 1; i < kVolts; i++) {
				total++;
				float a = curveShape(voltAt(i - 1), knob);
				float b = curveShape(voltAt(i), knob);
				if (!(b > a)) {
					bad++;
					if (shown++ < 10)
						printf("    knob %+.2f: %.4f V -> %.6f, %.4f V -> %.6f\n",
						       knob, voltAt(i - 1), a, voltAt(i), b);
				}
			}
		}
		check("strictly rising in voltage", bad, total);
	}

	// --- monotonic in the knob ------------------------------------------------
	// Turning CURVE has to move the shape one way, continuously: every point
	// strictly below where the next-lower setting put it. A knob whose two
	// halves fought each other would have a dead spot somewhere in its travel.
	{
		int bad = 0, shown = 0, total = 0;
		for (int i = 1; i < kVolts - 1; i++) {
			float v = voltAt(i);
			for (int k = 1; k < kKnobs; k++) {
				total++;
				float a = curveShape(v, knobAt(k - 1));
				float b = curveShape(v, knobAt(k));
				if (!(b < a)) {
					bad++;
					if (shown++ < 10)
						printf("    %.4f V: knob %+.2f -> %.6f, knob %+.2f -> %.6f\n",
						       v, knobAt(k - 1), a, knobAt(k), b);
				}
			}
		}
		check("strictly falling in the knob", bad, total);
	}

	// --- which way each half bends --------------------------------------------
	// Negative is the logarithmic side and sits above the linear line, positive
	// is the exponential side and sits below it. Inverted, the panel would be
	// lying about which direction is which.
	{
		int bad = 0, shown = 0, total = 0;
		for (int i = 1; i < kVolts - 1; i++) {
			float v = voltAt(i);
			for (int k = 0; k < kKnobs; k++) {
				float knob = knobAt(k);
				if (knob == 0.f)
					continue;
				total++;
				float y = curveShape(v, knob);
				bool ok = (knob < 0.f) ? (y > v) : (y < v);
				if (!ok) {
					bad++;
					if (shown++ < 10)
						printf("    knob %+.2f: %.4f V -> %.6f\n", knob, v, y);
				}
			}
		}
		check("log bends above linear, exp below", bad, total);
	}

	// --- the bend stays inside the range --------------------------------------
	// The output goes to a clamp at +-12 V. If the shaper could overshoot, that
	// clamp would be flattening the top of an envelope rather than catching
	// something that never happens.
	{
		int bad = 0, shown = 0, total = 0;
		for (int k = 0; k < kKnobs; k++) {
			float knob = knobAt(k);
			for (int i = 0; i < kVolts; i++) {
				total++;
				float y = curveShape(voltAt(i), knob);
				if (!(y >= 0.f && y <= 10.f)) {
					bad++;
					if (shown++ < 10)
						printf("    knob %+.2f: %.4f V -> %.6f\n",
						       knob, voltAt(i), y);
				}
			}
		}
		check("output stays within 0..10 V", bad, total);
	}

	// --- full deflection is the cube and the cube root -------------------------
	// kCurveExpLog2 is log2(3), so the ends of the travel are exponent 3 and
	// exponent 1/3. This pins the number: a wrong constant would still pass
	// every shape property above and simply bend by the wrong amount.
	{
		int bad = 0, shown = 0, total = 0;
		for (int i = 0; i < kVolts; i++) {
			float u = (float)i / (float)(kVolts - 1);
			total += 2;
			float hiWant = std::pow(u, 3.f) * 10.f;
			float loWant = std::pow(u, 1.f / 3.f) * 10.f;
			float hiGot = curveShape(voltAt(i), 1.f);
			float loGot = curveShape(voltAt(i), -1.f);
			if (std::fabs(hiGot - hiWant) > 1e-4f) {
				bad++;
				if (shown++ < 10)
					printf("    knob +1 at %.4f V: %.6f, wanted %.6f\n",
					       voltAt(i), hiGot, hiWant);
			}
			if (std::fabs(loGot - loWant) > 1e-4f) {
				bad++;
				if (shown++ < 10)
					printf("    knob -1 at %.4f V: %.6f, wanted %.6f\n",
					       voltAt(i), loGot, loWant);
			}
		}
		check("full deflection is x^3 and x^(1/3)", bad, total);
	}

	// --- nothing out of range gets through ------------------------------------
	// The cores are all unipolar, but pow() of a negative base is a NaN, and a
	// NaN reaching a port poisons every module downstream of it. The guard is
	// cheap; losing a patch to a silent NaN is not.
	{
		int bad = 0, shown = 0, total = 0;
		static const float kOut[] = { -12.f, -10.f, -1.f, -1e-6f, 10.000001f,
		                              11.f, 12.f };
		for (int k = 0; k < kKnobs; k++) {
			for (int j = 0; j < 7; j++) {
				total++;
				float y = curveShape(kOut[j], knobAt(k));
				bool ok = std::isfinite(y) && (kOut[j] < 0.f ? y == 0.f
				                                            : y == kOut[j]);
				if (!ok) {
					bad++;
					if (shown++ < 10)
						printf("    knob %+.2f: %.6f V -> %.6f\n",
						       knobAt(k), kOut[j], y);
				}
			}
		}
		check("out-of-range input is clamped, never NaN", bad, total);
	}

	printf("%s  %d checks, %d failures\n", failures ? "FAILED" : "ok",
	       checks, failures);
	return failures ? 1 : 0;
}

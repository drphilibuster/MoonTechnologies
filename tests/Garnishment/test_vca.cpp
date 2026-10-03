// Garnishment's three circuits, at the level where they are just arithmetic.
//
// The claims worth pinning are the ones that are the *point* of each part: the
// slew is asymmetric because a vactrol is, the LPG's filter closes with its
// gain because that is what makes it a gate rather than a VCA. (The JFET stage
// is a circuit now and has its own suite, test_iamo.cpp.)
#include "../../src/Garnishment/Vca.hpp"

#include <cmath>
#include <cstdio>

static int checks = 0;
static int failures = 0;

static void fail(const char* what, const char* detail) {
	failures++;
	printf("  FAIL  %s: %s\n", what, detail);
}

static const float kFs = 48000.f;
static const float kDt = 1.f / kFs;

/** Samples for `prev` to travel nine tenths of the way to `target`. */
static int travel(float from, float to, float atk, float dec) {
	float v = from;
	float want = from + (to - from) * 0.9f;
	for (int i = 1; i <= (int)(kFs * 10.f); i++) {
		v = slewTo(v, to, atk, dec, kDt);
		bool there = (to > from) ? (v >= want) : (v <= want);
		if (there) return i;
	}
	return -1;
}

int main() {
	// --- the slew is asymmetric, and that is the whole circuit ----------------
	// A vactrol's LED lights faster than its photoresistor lets go. If these two
	// ever come out equal the module has become an ordinary slew limiter.
	printf("  the vactrol's asymmetry...\n");
	{
		int up = travel(0.f, 1.f, 0.002f, 0.200f);
		int down = travel(1.f, 0.f, 0.002f, 0.200f);
		checks++;
		if (up < 0 || down < 0) fail("slew", "one direction never arrived");
		else if (!(down > up * 10)) {
			char d[160];
			snprintf(d, sizeof d, "rise took %d samples and fall %d -- the fall must be"
			         " far slower, or this is not a vactrol", up, down);
			fail("slew", d);
		}
	}

	// --- and it actually gets there -------------------------------------------
	printf("  the slew converges...\n");
	{
		float v = 0.f;
		for (int i = 0; i < (int)kFs; i++) v = slewTo(v, 1.f, 0.002f, 0.200f, kDt);
		checks++;
		if (std::fabs(v - 1.f) > 1e-3f) {
			char d[128];
			snprintf(d, sizeof d, "a second of rising reached %.5f, not 1.0", (double)v);
			fail("slew", d);
		}
	}

	// --- the LPG's filter passes what is under it and stops what is over ------
	printf("  the low-pass gate's filter...\n");
	{
		struct { float hz; float cut; bool pass; } cases[] = {
			{  100.f, 8000.f, true  },      // well under: through
			{ 12000.f,  300.f, false },     // well over: stopped
		};
		for (int c = 0; c < 2; c++) {
			OnePoleLP lp;
			double peak = 0.0;
			for (int i = 0; i < (int)(kFs * 0.2f); i++) {
				float x = std::sin(2.f * (float)M_PI * cases[c].hz * (float)i / kFs);
				float y = lp.process(x, cases[c].cut, kDt);
				if (i > (int)(kFs * 0.1f)) peak = std::fmax(peak, std::fabs((double)y));
			}
			checks++;
			bool ok = cases[c].pass ? (peak > 0.8) : (peak < 0.2);
			if (!ok) {
				char d[160];
				snprintf(d, sizeof d, "%.0f Hz through a %.0f Hz low-pass peaked at %.3f",
				         (double)cases[c].hz, (double)cases[c].cut, peak);
				fail("lpg filter", d);
			}
		}
	}

	// --- reset clears every voice ---------------------------------------------
	// Sixteen voices of three pieces of state each: one left behind is a channel
	// that clicks on the next note it plays.
	printf("  reset clears every voice...\n");
	{
		VcaBus bus;
		for (int i = 0; i < MAX_POLY; i++) {
			bus.ctrl[i] = 0.7f;
			bus.lpg[i].process(1.f, 1000.f, kDt);
			bus.jfet[i].process(-1.0, 0.5, 48000.0);
			bus.ldr[i].setSampleTime(kDt);
			bus.ldr[i].step(0.02);
		}
		bus.reset();
		int dirty = 0;
		for (int i = 0; i < MAX_POLY; i++) {
			if (bus.ctrl[i] != 0.f) dirty++;
			if (bus.lpg[i].state != 0.f) dirty++;
			if (bus.jfet[i].started) dirty++;
			if (bus.ldr[i].g[0] != 0.f || bus.ldr[i].g[1] != 0.f || bus.ldr[i].g[2] != 0.f) dirty++;
		}
		checks++;
		if (dirty) {
			char d[128];
			snprintf(d, sizeof d, "%d pieces of state survived a reset", dirty);
			fail("reset", d);
		}
	}

	// --- the VACTROL mode's cell (Vactrol.hpp, VTL5C3) -------------------------
	// The module's gain is the cell in series with the audio against the 100 kohm it is
	// assumed to feed; these are the numbers that follow from the part, and the shape of
	// the response in time that is the point of the mode.
	printf("  the vactrol cell as a VCA...\n");
	{
		auto settleGain = [](float volts) {
			vactrol::Vactrol c;
			float g = 0;
			for (int i = 0; i < (int)(kFs * 3.f); i++) g = vactrolGain(c, volts, kDt);
			return g;
		};
		float g0 = settleGain(0.f), g1 = settleGain(1.f), g5 = settleGain(5.f), g10 = settleGain(10.f);
		checks++;
		if (!(g0 < 0.01f)) { char d[96]; snprintf(d, sizeof d, "dark cell passes %.4f, should be ~0.005", (double) g0); fail("vactrol", d); }
		checks++;
		if (!(g1 < 0.01f)) { char d[96]; snprintf(d, sizeof d, "1 V is below the LED's knee but passes %.4f", (double) g1); fail("vactrol", d); }
		checks++;
		// VTL5C3 ~2.9 kohm at the 10 mA that 5 V makes through 330 ohm: 100k/(100k+2.9k) = 0.972
		if (!(g5 > 0.96f && g5 < 0.985f)) { char d[96]; snprintf(d, sizeof d, "5 V gain %.4f, expect 0.972", (double) g5); fail("vactrol", d); }
		checks++;
		// ~1.5 kohm at 25 mA: 0.985
		if (!(g10 > g5 && g10 > 0.98f && g10 < 0.995f)) { char d[96]; snprintf(d, sizeof d, "10 V gain %.4f, expect 0.985 (and above 5 V's %.4f)", (double) g10, (double) g5); fail("vactrol", d); }
		// a dim LED sits in the middle of the divider: 2 V is ~1.5 mA, ~30 kohm
		float g2 = settleGain(2.f);
		checks++;
		if (!(g2 > 0.6f && g2 < 0.85f)) { char d[96]; snprintf(d, sizeof d, "2 V gain %.4f, expect ~0.75", (double) g2); fail("vactrol", d); }

		// in time: open fast, close slowly, and the closing has a tail
		vactrol::Vactrol c;
		int tOpen = -1, tClose = -1, tTail = -1;
		float g = 0;
		for (int i = 1; i <= (int)(kFs * 0.5f) && tOpen < 0; i++) { g = vactrolGain(c, 10.f, kDt); if (g > 0.9f) tOpen = i; }
		for (int i = 0; i < (int)(kFs * 1.f); i++) vactrolGain(c, 10.f, kDt);
		for (int i = 1; i <= (int)(kFs * 2.f); i++) {
			g = vactrolGain(c, 0.f, kDt);
			if (tClose < 0 && g < 0.5f) tClose = i;
			if (tTail < 0 && g < 0.05f) tTail = i;
		}
		checks++;
		if (tOpen < 0 || tClose < 0 || !(tClose > tOpen * 0.5)) {
			char d[128]; snprintf(d, sizeof d, "opened to 90%% in %d samples, closed to 50%% in %d", tOpen, tClose); fail("vactrol", d);
		}
		checks++;
		// the memory: from half gain to 5 % takes several times as long as the first half closing
		if (tTail < 0 || !(tTail > 3 * tClose)) {
			char d[128]; snprintf(d, sizeof d, "closing to 50%% took %d samples and to 5%% %d: no slow tail", tClose, tTail); fail("vactrol", d);
		}
	}

	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}

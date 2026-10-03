// The SNARE's VACTROL mode: the strike envelope lights a VTL5C3's LED through 330 ohm and the
// cell's conductance opens the filter. The part itself is tested in tests/Vactrol; this checks
// the wiring: the corner opens at the strike, follows the envelope down, and the cell's memory
// (not the envelope) is what holds it open once the LED has dropped below its knee.
#include "../../src/Kickback/Voices.hpp"

#include <cmath>
#include <cstdio>

using namespace kickback;

static int checks = 0, failures = 0;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("  FAIL  "); printf(__VA_ARGS__); printf("\n"); } } while (0)

struct Trace { double g[48000]; double e[48000]; };

static void run(VactrolEngine& v, float t60, Trace& tr, bool cellLive) {
	v.setRate(48000.f); v.reset(); v.strike(1.f);
	for (int i = 0; i < 48000; i++) {
		(void) v.process(4000.f, t60, 0.5f, 0.5f);
		tr.g[i] = v.cell.conductance() / v.gRef;
		tr.e[i] = v.env.env;
		if (!cellLive) v.cell.reset();      // negative control: a cell with no memory
	}
}

int main() {
	static Trace tr;
	VactrolEngine v;
	run(v, 0.25f, tr, true);
	printf("the VACTROL snare...\n");

	// the corner opens: nearly fully lit within 15 ms, the part's attack, not instantly
	int iOpen = -1;
	for (int i = 0; i < 48000; i++) if (tr.g[i] > 0.5) { iOpen = i; break; }
	CHECK(iOpen > 20 && iOpen < 480, "the cell reached half-lit after %d samples (expect 1-10 ms)", iOpen);
	double peak = 0; int ipk = 0;
	for (int i = 0; i < 48000; i++) if (tr.g[i] > peak) { peak = tr.g[i]; ipk = i; }
	CHECK(peak > 0.5 && peak <= 1.0 + 1e-9, "peak openness %.3f at %d", peak, ipk);

	// the LED is out below ~1.5 V, i.e. when the envelope is under ~0.15 of its strike
	int iDark = -1;
	for (int i = 0; i < 48000; i++) if (tr.e[i] < 0.15) { iDark = i; break; }
	CHECK(iDark > 0, "envelope never fell under the LED's knee");
	// the memory: 10 ms after the LED goes out the cell is still well open; a cell that
	// followed the LED instantly would be at its dark conductance
	double gAfter = tr.g[iDark + 480];
	double gDark = ::vactrol::kDarkConductance / v.gRef;
	CHECK(gAfter > 20.0 * gDark, "10 ms after the LED went out the cell is at %.2e of full (dark %.2e)", gAfter, gDark);
	// and it does let go: a second later it is dark again
	CHECK(tr.g[47999] < 5.0 * gDark + 1e-3, "after a second the cell is still at %.2e", tr.g[47999]);

	// the decay of the cell trails the envelope: while the envelope halves several times the cell
	// stays above the dark floor with a monotone fall
	bool mono = true;
	for (int i = ipk + 100; i < 47999; i++) if (tr.g[i + 1] > tr.g[i] + 1e-9) { mono = false; break; }
	CHECK(mono, "the cell's conductance rose again during the decay");

	// NEGATIVE CONTROL: a cell with no memory (reset every sample) must be caught by the checks above
	Trace broken; VactrolEngine w;
	run(w, 0.25f, broken, false);
	double gB = broken.g[iDark + 480];
	CHECK(!(gB > 20.0 * gDark), "negative control: a cell reset every sample still looks like it remembers (%.2e)", gB);
	printf("  (negative control: memoryless cell reads %.2e of full 10 ms after the LED goes out; the real part %.2e)\n", gB, gAfter);

	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}

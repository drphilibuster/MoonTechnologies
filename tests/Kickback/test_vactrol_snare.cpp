// The SNARE's VACTROL mode: the strike envelope lights a VTL5C3's LED through 330 ohm and the
// cell's conductance opens the filter. The part itself is tested in tests/Vactrol; this checks
// the wiring: the corner opens at the strike, follows the envelope down, and the cell's memory
// (not the envelope) is what holds it open once the LED has dropped below its knee.
//
// Then the two menu options: the vactrol PART (VTL5C3 / NSL-32SR2 / NSL-32SR3 / LED + GL5528) and the
// TOPOLOGY (the module's filter corner, or the Day 9 board where the LDR sets the envelope's decay).
// The defaults must be the engine exactly as it was; each option has checks and negative controls.
#include "../../src/Kickback/Voices.hpp"

#include <cmath>
#include <cstdio>
#include <functional>

using namespace kickback;

static int checks = 0, failures = 0;
static bool quiet = false;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; if (!quiet) { printf("  FAIL  "); printf(__VA_ARGS__); printf("\n"); } } } while (0)

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


// ===========================================================================================
#define CHK(cond, what, ...) do { checks++; if (!(cond)) { failures++; if (!quiet) { printf("  FAIL  %s: ", (what)); printf(__VA_ARGS__); printf("\n"); } } } while (0)
// The two options.
// ===========================================================================================

// ---- the pre-option engine, verbatim (the old VactrolEngine), for the bit-identity regression --
struct LegacyVactrolEngine {
	kickback::AvalancheSource noise;
	OnePole lp, lp2;
	::vactrol::Vactrol cell;
	double gRef = 0.0;
	Decay env;
	float fs = 44100.f;
	LegacyVactrolEngine() : noise(0x5EAF00Du) {
		gRef = ::vactrol::kDarkConductance + ::vactrol::steadyLight(::vactrol::ledCurrent(10.0, 330.0));
		cell.configure(1.0 / fs);
	}
	void setRate(float fs_) { fs = fs_; noise.setRate(fs_); cell.configure(1.0 / (double) fs_); }
	void reset() { noise.reset(); lp.reset(); lp2.reset(); cell.reset(); env.reset(); }
	inline void strike(float vel) { env.strike(vel); }
	inline float process(float top, float t60, float bend, float grain) {
		float e = env.process(t60, fs);
		double iLed = ::vactrol::ledCurrent(10.0 * (double) e, 330.0);
		float smoothed = (float) std::fmin(cell.step(iLed) / gRef, 1.0);
		float span = top * (0.25f + 0.75f * (1.f - bend));
		float g = poleG(140.f + smoothed * span * (1.f + bend * 2.f), fs);
		return lp2.lp(lp.lp(noise.next(), g), g) * e * (1.5f + 0.4f * grain);
	}
};

// An engine factory, so the same checks can be run on a broken one.
struct Rig {
	const char* name;
	std::function<void(VactrolEngine&, int part, bool board)> configure;
};
static void configureGood(VactrolEngine& v, int part, bool board) { v.setPart(part); v.board = board; }

// The pot and the LDR, written out again here from the schematic (not read back from the engine).
static double potR2(double k) { return 100e3 * (k <= 0.5 ? 0.15 * (k / 0.5) : 0.15 + 0.85 * (k - 0.5) / 0.5); }
static double expectTau(int part, double knob, double bend) {
	const ::vactrol::Part& P = ::vactrol::part(part);
	double i = ::vactrol::ledCurrent(10.0 * bend, 330.0, P);
	double rl = ::vactrol::steadyResistance(i, P);
	double r2 = potR2(knob);
	return 10e-6 * (100.0 + r2 * rl / (r2 + rl));
}
/** C2's 1/e time on the board, measured: a settled cell (the BEND knob is not moving), a full strike. */
static double boardTau(const Rig& rig, int part, double knob, double bend) {
	VactrolEngine v; v.setRate(48000.f); rig.configure(v, part, true); v.reset();
	double i = ::vactrol::ledCurrent(10.0 * bend, 330.0, *v.cell.p);
	v.cell.settle(i);
	v.strike(1.f);
	double prev = std::log(v.vC2), cur = prev;
	for (int n = 1; n < 48000 * 30; n++) {
		v.processBoard(4000.f, (float) knob, (float) bend);
		if (v.vC2 <= 0.0) return -1;
		cur = std::log(v.vC2);
		if (cur <= -1.0) return ((n - 1) + (-1.0 - prev) / (cur - prev)) / 48000.0;
		prev = cur;
	}
	return -1;
}

static double hfRatio(const float* x, int a, int b) {       // first-difference energy over energy
	double d = 0, e = 0;
	for (int i = a + 1; i < b; i++) { double df = x[i] - x[i - 1]; d += df * df; e += (double) x[i] * x[i]; }
	return e > 0 ? d / e : 0;
}

static void optionChecks(const Rig& rig) {
	// -- the board's pump: trigger -> C1 -> D2 -> C2, the diode's 0.6 V lost, against a 10 V trigger
	{
		VactrolEngine v; rig.configure(v, 0, true); v.reset();
		v.strike(1.f);   CHK(std::fabs(v.vC2 - 1.0) < 1e-12, "pump", "full strike pumps C2 to %.4f", v.vC2);
		v.reset(); v.strike(0.5f); CHK(std::fabs(v.vC2 - (5.0 - 0.6) / 9.4) < 1e-6, "pump", "half strike pumps C2 to %.4f, want %.4f", v.vC2, 4.4 / 9.4);
		v.reset(); v.strike(0.05f); CHK(v.vC2 == 0.0, "pump", "a strike under the diode drop still charged C2 (%.4f)", v.vC2);
		v.reset(); v.strike(1.f); for (int i = 0; i < 3000; i++) v.processBoard(4000.f, 0.5f, 0.f);
		double mid = v.vC2; v.strike(0.4f);
		CHK(v.vC2 == mid, "pump", "a weaker strike lowered C2 (%.4f -> %.4f): D2 would be reverse biased", mid, v.vC2);
	}
	// -- the decay is C2 * (R5 + R2 || LDR), for every part, from the schematic's values
	for (int part = 0; part < ::vactrol::PART_COUNT; part++) {
		double worst = 0;
		for (double knob : { 0.0, 0.25, 0.5, 1.0 })
			for (double bend : { 0.0, 0.3, 1.0 }) {
				double m = boardTau(rig, part, knob, bend), e = expectTau(part, knob, bend);
				worst = std::fmax(worst, std::fabs(m / e - 1.0));
			}
		CHK(worst < 0.012, ::vactrol::part(part).name, "board decay constant is %.2f %% off C2*(R5 + R2||LDR)", worst * 100);
	}
	// -- the LDR sets the decay: lighting the LED (BEND) shortens it, enormously at the top of R2
	for (int part = 0; part < ::vactrol::PART_COUNT; part++) {
		double dark = boardTau(rig, part, 1.0, 0.0), lit = boardTau(rig, part, 1.0, 1.0);
		CHK(dark > 0.9 && lit > 0 && lit < dark / 25.0, ::vactrol::part(part).name,
		      "R2 at the top: %.0f ms dark LED, %.1f ms with 10 V on the Decay CV", dark * 1e3, lit * 1e3);
	}
	// -- the part decides how short it can get: R5 (100 ohm) is the floor, the LDR's on-resistance rides on it
	{
		double t[4];
		for (int part = 0; part < 4; part++) t[part] = boardTau(rig, part, 1.0, 1.0);
		CHK(t[1] < t[2] && t[2] < t[3] && t[3] < t[0], "part", "shortest decay (R2 full, 10 V CV) tau: VTL5C3 %.2f NSL-SR2 %.2f NSL-SR3 %.2f GL5528 %.2f ms",
		      t[0] * 1e3, t[1] * 1e3, t[2] * 1e3, t[3] * 1e3);
		CHK(t[0] > 14e-3 && t[0] < 18e-3, "part", "VTL5C3 floor tau %.2f ms (t60 %.0f ms): C2 * (100 + 1.4k) at 25 mA", t[0] * 1e3, t[0] * 6.9078e3);
		CHK(t[1] > 1.0e-3 && t[1] < 1.4e-3, "part", "NSL-SR2 floor tau %.2f ms: R5 alone is 1.0 ms", t[1] * 1e3);
	}
	// -- TUNE stays a fixed low-pass on the board; on the module's topology the cell opens it as the
	//    envelope dies, so the tail is duller than the attack
	for (int board = 0; board < 2; board++) {
		VactrolEngine v; v.setRate(48000.f); rig.configure(v, 0, board != 0); v.reset();
		static float x[48000];
		if (board) v.cell.settle(::vactrol::ledCurrent(0.0, 330.0));
		v.strike(1.f);
		for (int i = 0; i < 48000; i++) x[i] = board ? v.processBoard(4000.f, 0.38f, 0.f) : v.process(4000.f, 0.25f, 0.f, 0.5f);
		// windows where the envelope is comparable: attack 5-60 ms, and the same 55 ms once the envelope has dropped to ~1/4
		int a0 = 240, a1 = 2880, b0 = board ? 14400 : 9600, b1 = b0 + 2640;
		double ha = hfRatio(x, a0, a1), hb = hfRatio(x, b0, b1);
		if (board) CHK(hb > 0.85 * ha && hb < 1.18 * ha, "topology", "board: TUNE's filter moved (HF ratio %.4f early, %.4f late)", ha, hb);
		else       CHK(hb < 0.9 * ha, "topology", "module: the cell no longer darkens the tail (HF ratio %.4f early, %.4f late)", ha, hb);
	}
	// -- the part changes the module topology too: the NSL's cell lets go in ms, the DIY pair rings on
	{
		double t10[4];
		for (int part = 0; part < 4; part++) {
			VactrolEngine v; v.setRate(48000.f); rig.configure(v, part, false); v.reset(); v.strike(1.f);
			double pk = 0; int ipk = 0; t10[part] = -1;
			for (int i = 0; i < 48000 * 2; i++) {
				v.process(4000.f, 0.25f, 0.5f, 0.5f);
				double o = v.cell.conductance() / v.gRef;
				if (o > pk) { pk = o; ipk = i; }
				if (i > ipk && o < 0.1 * pk) { t10[part] = (i - ipk) / 48.0; break; }
			}
		}
		// fall to 10 % of the peak, ms after it: VTL5C3 ~53, SR2 ~53, SR3 ~55, GL5528 ~93 (the envelope, not the cell, limits the NSLs)
		CHK(t10[3] > 1.4 * t10[0] && t10[3] > 1.4 * t10[1], "part", "the DIY pair should ring past the others: %.0f %.0f %.0f %.0f ms", t10[0], t10[1], t10[2], t10[3]);
		VactrolEngine a, b; a.setRate(48000.f); b.setRate(48000.f); rig.configure(a, 0, false); rig.configure(b, 1, false);
		a.reset(); b.reset(); a.strike(1.f); b.strike(1.f);
		double dmax = 0; int i;
		for (i = 0; i < 4800; i++) dmax = std::fmax(dmax, std::fabs(a.process(4000.f, 0.25f, 0.5f, 0.5f) - b.process(4000.f, 0.25f, 0.5f, 0.5f)));
		CHK(dmax > 0.05, "part", "VTL5C3 and NSL-32SR2 gave the same snare (largest difference %.4f)", dmax);
	}
}

static void snareChecks(const std::function<void(Snare&, int, bool)>& set) {
	Snare a, b; a.setRate(48000.f); b.setRate(48000.f); a.reset(); b.reset(); set(b, 0, true);
	double e1 = 0, e2 = 0;
	for (int i = 0; i < 48000; i++) {
		float ya = a.process(i == 0, 1.f, 1, 0.5f, 0.f, 0.5f, 0.3f), yb = b.process(i == 0, 1.f, 1, 0.5f, 0.f, 0.5f, 0.3f);
		if (i > 24000) { e1 += (double) ya * ya; e2 += (double) yb * yb; }
	}
	// DECAY at mid-travel: the module's t60 is ~0.18 s, the board's R2 at 15 kohm is ~1 s
	CHK(e2 > 20.0 * e1, "snare", "board topology DECAY mid-travel should ring far longer than the module's (%.3g vs %.3g)", e2, e1);
	Snare c; c.setRate(48000.f); c.reset(); set(c, 0, true);
	double e3 = 0;
	for (int i = 0; i < 48000; i++) { float y = c.process(i == 0, 1.f, 1, 0.5f, 0.f, 0.5f, 1.0f); if (i > 24000) e3 += (double) y * y; }
	CHK(e3 < 0.02 * e2, "snare", "BEND (the Decay CV) at 1 should shorten the board's decay (%.3g vs %.3g at 0.3)", e3, e2);
	// the part reaches the snare (module topology): VTL5C3 and NSL-32SR2 sound different
	Snare p0, p1; p0.setRate(48000.f); p1.setRate(48000.f); p0.reset(); p1.reset(); set(p0, 0, false); set(p1, 1, false);
	double dp = 0;
	for (int i = 0; i < 12000; i++) dp = std::fmax(dp, std::fabs(p0.process(i == 0, 1.f, 1, 0.5f, 0.f, 0.5f, 0.3f) - p1.process(i == 0, 1.f, 1, 0.5f, 0.f, 0.5f, 0.3f)));
	CHK(dp > 0.01, "snare", "the vactrol part did not reach the snare (largest difference %.4f)", dp);
	// XOR and Dazzle do not see the options
	Snare x1, x2; x1.setRate(48000.f); x2.setRate(48000.f);
	double dx = 0;
	for (int m = 0; m < 3; m += 2) {
		x1.reset(); x2.reset(); set(x2, 3, true);
		for (int i = 0; i < 12000; i++) dx = std::fmax(dx, std::fabs(x1.process(i == 0, 1.f, m, 0.5f, 0.f, 0.5f, 0.3f) - x2.process(i == 0, 1.f, m, 0.5f, 0.f, 0.5f, 0.3f)));
	}
	CHK(dx == 0.0, "snare", "the VACTROL options changed the XOR / DAZZLE snare by %.3g", dx);
}

static void optionTests() {
	printf("the options...\n");
	// --- regression: the defaults are the engine as it was (bit-identical) ----------------------
	{
		LegacyVactrolEngine a; VactrolEngine b, c;
		a.setRate(48000.f); b.setRate(48000.f); c.setRate(48000.f);
		a.reset(); b.reset(); c.reset();
		c.setPart(2); c.setPart(0); c.board = true; c.board = false;       // set, set back: still the old engine
		double d1 = 0, d2 = 0; int nonzero = 0;
		for (int blk = 0; blk < 4; blk++) {
			float vel = 1.f - 0.2f * blk;
			a.strike(vel); b.strike(vel); c.strike(vel);
			for (int i = 0; i < 24000; i++) {
				float t60 = 0.05f + 0.3f * blk, bend = 0.25f * blk;
				float ya = a.process(3000.f, t60, bend, bend), yb = b.process(3000.f, t60, bend, bend), yc = c.process(3000.f, t60, bend, bend);
				d1 = std::fmax(d1, std::fabs(ya - yb)); d2 = std::fmax(d2, std::fabs(ya - yc));
				if (ya != 0.f) nonzero++;
			}
		}
		CHK(nonzero > 50000 && d1 == 0.0, "default", "the default engine differs from the old one by %.3g (bit-identity)", d1);
		CHK(d2 == 0.0, "default", "an engine put through every option and back differs from the old one by %.3g", d2);
		// and through Snare: setVactrol(0, false) is the old snare
		Snare s1, s2; s1.setRate(48000.f); s2.setRate(48000.f); s1.reset(); s2.reset();
		s2.setVactrol(1, true); s2.setVactrol(0, false);
		double d3 = 0;
		for (int i = 0; i < 48000; i++) d3 = std::fmax(d3, std::fabs(s1.process(i == 0 || i == 20000, 0.9f, 1, 0.5f, 0.f, 0.5f, 0.4f) -
		                                                             s2.process(i == 0 || i == 20000, 0.9f, 1, 0.5f, 0.f, 0.5f, 0.4f)));
		CHK(d3 == 0.0, "default", "Snare VACTROL with the options left at their defaults differs from itself (%.3g)", d3);
	}

	Rig good = { "good", configureGood };
	int f0 = failures, c0 = checks;
	optionChecks(good);
	printf("  %d checks, %d failures\n", checks - c0, failures - f0);

	// --- the Snare's own plumbing: the options really reach what the SNARE does -------------------
	typedef std::function<void(Snare&, int, bool)> SnareSet;
	SnareSet realSet = [](Snare& s, int part, bool board) { s.setVactrol(part, board); };
	f0 = failures; c0 = checks;
	snareChecks(realSet);
	printf("  %d checks, %d failures\n", checks - c0, failures - f0);

	// --- negative controls --------------------------------------------------------------------
	static ::vactrol::Part deadPart;      // an LDR that never lights: flat 20 Mohm for any current
	static double flatR[9];
	for (int k = 0; k < 9; k++) flatR[k] = 20e6 - k * 1.0;      // strictly falling, so it is a legal table
	Rig ctl[2] = {
		{ "board mode ignores the LDR (an LED that never lights the cell)", [](VactrolEngine& v, int part, bool board) { v.setPart(part); v.board = board; deadPart = ::vactrol::part(part); deadPart.anchorR = flatR; v.cell.p = &deadPart; } },
		{ "the part option is ignored (always the VTL5C3)", [](VactrolEngine& v, int, bool board) { v.board = board; } },
	};
	int caught = 0, total = 0;
	for (int i = 0; i < 2; i++) {
		int fb = failures; quiet = true;
		optionChecks(ctl[i]);
		quiet = false;
		int hit = failures - fb;
		printf("  negative control '%s': %d checks failed%s\n", ctl[i].name, hit, hit ? "" : "  <-- NOT CAUGHT");
		if (hit) caught++;
		total++;
		failures = fb;
	}
	struct SCtl { const char* name; SnareSet set; };
	SCtl sctl[2] = {
		{ "the topology option is ignored by the Snare (always the module's)", [](Snare& s, int part, bool) { s.setVactrol(part, false); } },
		{ "the part option is ignored by the Snare (always the VTL5C3)", [](Snare& s, int, bool board) { s.setVactrol(0, board); } },
	};
	for (int i = 0; i < 2; i++) {
		int fb = failures; quiet = true;
		snareChecks(sctl[i].set);
		quiet = false;
		int hit = failures - fb;
		printf("  negative control '%s': %d checks failed%s\n", sctl[i].name, hit, hit ? "" : "  <-- NOT CAUGHT");
		if (hit) caught++;
		total++;
		failures = fb;
	}
	checks++;
	if (caught != total) { failures++; printf("  FAIL  option negative controls: only %d of %d broken engines were caught\n", caught, total); }
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

	optionTests();

	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}

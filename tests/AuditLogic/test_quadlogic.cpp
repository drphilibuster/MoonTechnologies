// AuditLogic's "Quad Logic board" mode: the Day 8 MiaW Quad Logic Module, as parts.
// Oracles are the BOM values and the TI datasheets (TL074 SLOS080W, CD4071B SCHS056D),
// worked by hand where a number is derived. Negative controls at the bottom break the
// model and require the sanity predicate to notice.

#include "../../src/QuadLogicBoard.hpp"

#include <cmath>
#include <cstdio>

static int checks = 0, failures = 0;
static void check(const char* what, bool ok) {
	checks++;
	if (!ok) { failures++; printf("  FAIL  %s\n", what); }
}
static bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

/** What the real board must do. Used on the good model, and on broken ones that it must reject. */
static bool boardSane(const quadlogic::Board& b, bool verbose = false) {
	bool ok = true;
	// 1. the jack threshold: (+) = Vin*100/101 crossing Vref = 12*10/110.
	double lo = 0.0, hi = 5.0;
	for (int i = 0; i < 60; i++) {
		double mid = 0.5 * (lo + hi);
		if (b.orTrue(mid, true, 0.0, false)) hi = mid; else lo = mid;
	}
	double want = 12.0 * 10.0 / 110.0 * 101.0 / 100.0;     // 1.10182 V
	if (verbose) printf("  threshold at the jack %.5f V (want %.5f)\n", hi, want);
	ok = ok && near(hi, want, 2e-4);
	// 2. a true input must reach the 4071 above its guaranteed VIH; a false one below VIL.
	double nh = b.node(5.0, true), nl = b.node(0.0, true);
	if (verbose) printf("  4071 input: %.3f V true, %.4f V false (VIH min %.2f, VIL max %.2f)\n",
		nh, nl, cd4071::vihMin(b.vdd), cd4071::vilMax(b.vdd));
	ok = ok && nh >= cd4071::vihMin(b.vdd) && nl <= cd4071::vilMax(b.vdd);
	// 3. the OR drives the jack: ~half the emitter, well above 0 and below the rail.
	ok = ok && b.yHigh > 4.5 && b.yHigh < 6.5 && std::fabs(b.yLow) < 1e-3;
	return ok;
}

int main() {
	printf("CD4071B\n");
	{
		using namespace cd4071;
		check("VIL max 1.5 / 3 / 4 V at 5 / 10 / 15 V",
			near(vilMax(5), 1.5, 1e-12) && near(vilMax(10), 3.0, 1e-12) && near(vilMax(15), 4.0, 1e-12));
		check("VIH min 3.5 / 7 / 11 V at 5 / 10 / 15 V",
			near(vihMin(5), 3.5, 1e-12) && near(vihMin(10), 7.0, 1e-12) && near(vihMin(15), 11.0, 1e-12));
		check("VIL/VIH interpolate between supplies (12 V: 3.4 / 8.6)",
			near(vilMax(12), 3.4, 1e-9) && near(vihMin(12), 8.6, 1e-9));
		bool bands = true;
		for (double v : { 5.0, 8.0, 10.0, 12.0, 15.0 })
			bands = bands && threshold(v) > vilMax(v) && threshold(v) < vihMin(v);
		check("the VDD/2 switching point lies between VIL max and VIH min", bands);
		bool tt = true;
		for (double v : { 5.0, 10.0, 12.0, 15.0 }) {
			double L = vilMax(v) * 0.5, H = vihMin(v) + 0.1;
			tt = tt && !or2(L, L, v) && or2(H, L, v) && or2(L, H, v) && or2(H, H, v);
		}
		check("OR truth table at guaranteed levels, four supplies", tt);
		check("output resistance 400 / 192 / 221 ohm at 5 / 10 / 15 V",
			near(rout(5), 400, 1e-9) && near(rout(10), 0.5 / 2.6e-3, 1e-9) && near(rout(15), 1.5 / 6.8e-3, 1e-9));
		check("output source is the rails", outputSource(true, 12) == 12.0 && outputSource(false, 12) == 0.0);
	}

	printf("TL074 comparator\n");
	{
		tl074::Comparator c;
		check("saturates to rail minus 0.79 V (11.21 / -11.21)", near(c.emf(5, 0), 11.21, 1e-9) && near(c.emf(0, 5), -11.21, 1e-9));
		check("linear in the microvolt window: 20 uV in -> 4 V out", near(c.emf(20e-6, 0), 4.0, 1e-6));
		// datasheet anchors: 1.5 V headroom at 1.35 mA, 5 V at 5 mA (min), 3 V at 1.2 mA (min)
		double d1 = c.headroom + c.rout * 1.35e-3, d2 = c.headroom + c.rout * 1.2e-3, d3 = c.headroom + c.rout * 5e-3;
		check("headroom model passes through the datasheet's typical point", near(d1, 1.5, 0.01));
		check("and stays inside the minimum-swing limits (3 V at 1.2 mA, 5 V at 5 mA)", d2 < 3.0 && near(d3, 3.42, 0.01) && d3 < 5.0);
	}

	printf("Quad Logic board\n");
	quadlogic::Board board;
	check("reference is 12 V * 10k/110k = 1.0909 V", near(board.vref, 1.09090909, 1e-6));
	check("sane: threshold, 4071 levels, follower", boardSane(board, true));
	{
		double nh = board.node(5.0, true), nl = board.node(0.0, true);
		printf("  comparator-high NODE %.3f V, Y true %.3f V, Y false %.4f V, Y LED %.2f mA\n",
			nh, board.yHigh, board.yLow, board.ledHighA * 1e3);
		// KCL: the diode and the 2k7 carry the same current; the LED drops 1.6..2.1 V at that current.
		check("comparator-high NODE is above the 4071's VIH min (8.6 V at 12 V) and below the rail",
			nh > 8.6 && nh < 11.0);
		check("comparator-low NODE is ~0 V (diode blocks; the 4071 input clamp holds it within 0.1 V)", near(nl, 0.0, 0.1));
		// Follower: Ve = 2*Y; the LED branch carries (Ve - Vf)/2k7 with Vf ~ 1.9..2.2 V at ~4 mA.
		double ve = 2.0 * board.yHigh, vf = ve - 2700.0 * board.ledHighA;
		check("Y is half the emitter (1k/1k) and the LED branch is consistent (Vf 1.7-2.2 V)", vf > 1.7 && vf < 2.2);
		check("emitter sits one Vbe below the base (0.55-0.75 V under ~12 V)", ve > 12.0 - 0.75 - 0.4 && ve < 12.0 - 0.55);
	}
	{
		// truth table over a grid, jacks patched, against the arithmetic of the divider.
		const double thr = 12.0 * 10.0 / 110.0 * 1.01;
		const double grid[] = { -10, -5, -0.5, 0, 0.5, 1.0, 1.1, 1.105, 1.2, 2, 5, 10 };
		bool ok = true;
		for (double a : grid) for (double bb : grid) {
			bool want = a > thr || bb > thr;
			ok = ok && board.orTrue(a, true, bb, true) == want;
			double y = board.channel(a, true, bb, true);
			ok = ok && near(y, want ? board.yHigh : board.yLow, 1e-12);
		}
		check("OR over a 12 x 12 grid of jack voltages, both patched", ok);
		check("an unpatched jack reads 0 V (100k to ground), not a rail",
			!board.orTrue(0, false, 0, false) && board.orTrue(5, true, 0, false) && board.orTrue(0, false, 5, true));
		check("negative inputs never read true", !board.orTrue(-10, true, -10, true));
		check("OR is symmetric", board.channel(3, true, 0, true) == board.channel(0, true, 3, true));
	}
	{
		// a node the follower sits at: true output is not 10 V gates; it is the board's own level.
		check("true level is the board's ~5.6 V, not a 10 V gate", near(board.yHigh, 5.6, 0.4));
	}
	{
		// supply dependence of the jack: the follower scales with VDD; the comparator branch does not.
		quadlogic::Board b5, b15;
		b5.vdd = 5.0;  b5.prepare();
		b15.vdd = 15.0; b15.prepare();
		check("yHigh grows with VDD (5 V < 12 V < 15 V)", b5.yHigh < board.yHigh && board.yHigh < b15.yHigh);
		check("at VDD = 5 V the comparator's ~9 V node still exceeds the gate's VIH", b5.node(5, true) > cd4071::vihMin(5));
		check("at VDD = 15 V the same node is below the gate's guaranteed VIH (11 V): outside the board's design",
			b15.node(5, true) < cd4071::vihMin(15));
	}

	printf("negative controls (each broken model must fail boardSane)\n");
	{
		quadlogic::Board b;
		b.op.headroom = 7.0; b.prepare();                 // swing too small: NODE falls below VDD/2
		check("CONTROL: 7 V of headroom is rejected", !boardSane(b));
	}
	{
		quadlogic::Board b;
		b.rRefBottom = 100e3; b.prepare();                // Vref = 6 V instead of 1.09 V
		check("CONTROL: a 6 V reference is rejected", !boardSane(b));
	}
	{
		quadlogic::Board b;
		b.rPulldown = 1e12; b.prepare();                   // no 100k/1k divider: threshold 1.0909, not 1.1018
		check("CONTROL: dropping the input divider is rejected", !boardSane(b));
	}
	{
		quadlogic::Board b;
		b.rEmit = 1e9; b.prepare();                        // jack no longer half the emitter
		check("CONTROL: an open emitter resistor is rejected", !boardSane(b));
	}

	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}

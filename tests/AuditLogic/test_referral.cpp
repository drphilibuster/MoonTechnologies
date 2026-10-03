// REFERRAL's HEF4066BT and the Day 10 enable board (src/Hef4066.hpp, src/ReferralBoard.hpp)
// against the Nexperia datasheet's own numbers, then against arithmetic written here
// independently of the model. Every group that matters runs again on a deliberately broken
// model and must FAIL there (the negative controls).

#include "../../src/ReferralBoard.hpp"

#include <cmath>
#include <cstdio>

static int checks = 0, failures = 0;
static void check(const char* what, bool ok) {
	checks++;
	if (!ok) { failures++; printf("  FAIL  %s\n", what); }
}
static bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }
static bool rel(double a, double b, double r) { return std::fabs(a - b) <= r * std::fabs(b); }

static const double PI = 3.14159265358979323846;

// ---- Ron against Table 7 and Fig. 6 ------------------------------------------------------
// Table 7 (Tamb 25 C, ISW 200 uA): rail 0 V, rail VDD, peak; typical, ohms.
static bool ronGroup(const hef4066::Chip& base, bool verbose) {
	bool ok = true;
	const double vdds[3] = { 5, 10, 15 };
	const double rail0[3] = { 115, 50, 40 }, railV[3] = { 120, 65, 50 }, peak[3] = { 350, 80, 60 };
	for (int i = 0; i < 3; i++) {
		hef4066::Chip c = base;
		c.vdd = vdds[i];
		double r0 = c.ron(0.0), rv = c.ron(vdds[i]), pk = 0.0;
		for (double v = 0.0; v <= vdds[i]; v += 0.01) if (c.ron(v) > pk) pk = c.ron(v);
		if (verbose) printf("    VDD %2.0f V: Ron(0) %.1f (Table 115/50/40), Ron(VDD) %.1f (120/65/50), peak %.1f (350/80/60)\n",
			vdds[i], r0, rv, pk);
		ok = ok && rel(r0, rail0[i], 0.06) && rel(rv, railV[i], 0.15) && rel(pk, peak[i], 0.13);
	}
	return ok;
}

// Fig. 6's shape at 10 V: a hump near 2.8 V and a bigger one near 8.9 V, between them a dip.
static bool shapeGroup(const hef4066::Chip& base) {
	hef4066::Chip c = base;
	c.vdd = 10.0;
	return c.ron(2.8) > c.ron(0.0) && c.ron(8.9) > c.ron(2.8) && c.ron(4.1) < c.ron(2.8) && c.ron(4.1) < c.ron(8.9);
}

int main() {
	printf("HEF4066B / Day 10 REFERRAL board\n");
	hef4066::Chip chip;

	printf("-- Ron: Table 7 and Fig. 6, temperature\n");
	check("Ron(0), Ron(VDD), peak at 5/10/15 V within tolerance of Table 7 (6/15/13 %)", ronGroup(chip, true));
	check("Fig. 6 at 10 V has its two humps and the dip between", shapeGroup(chip));
	{
		hef4066::Chip c = chip;
		bool between = true;
		for (double v = 0.0; v <= 12.0; v += 0.25) {
			hef4066::Chip a = chip, b = chip, m = chip;
			a.vdd = 10.0; b.vdd = 15.0; m.vdd = 12.0;
			double x = v / 12.0;
			double ra = a.ron(x * 10.0), rb = b.ron(x * 15.0), rm = m.ron(v);
			if (!(rm >= std::fmin(ra, rb) - 1e-9 && rm <= std::fmax(ra, rb) + 1e-9)) between = false;
		}
		check("at 12 V Ron lies between the 10 V and 15 V curves at the same VI/VDD", between);
		printf("    VDD 12 V: Ron(0) %.1f  Ron(6) %.1f  Ron(12) %.1f ohm\n", c.ron(0), c.ron(6), c.ron(12));
		check("Ron falls as VDD rises (6 V signal: 10 V > 12 V > 15 V)", [&] {
			hef4066::Chip a = chip, b = chip, d = chip; a.vdd = 10; b.vdd = 12; d.vdd = 15;
			return a.ron(5.0) > b.ron(6.0) && b.ron(6.0) > d.ron(7.5);
		}());
	}
	{
		hef4066::Chip a = chip, b = chip, d = chip;
		a.vdd = b.vdd = d.vdd = 10.0;
		a.tempC = -55; b.tempC = 25; d.tempC = 125;
		check("temperature: Ron(125 C)/Ron(25 C) = 550/400 and Ron(-55 C)/Ron(25 C) = 310/400 at 10 V",
			near(d.ron(5) / b.ron(5), 550.0 / 400.0, 1e-9) && near(a.ron(5) / b.ron(5), 310.0 / 400.0, 1e-9));
		hef4066::Chip e = chip; e.vdd = 15; e.tempC = 85;
		hef4066::Chip f = chip; f.vdd = 15;
		check("temperature at 15 V, 85 C: 300/240", near(e.ron(5) / f.ron(5), 300.0 / 240.0, 1e-9));
	}

	printf("-- the ON switch driven through a source resistance into a load\n");
	{
		// Independent residuals: rebuild the two KCL equations here with std::exp.
		const double Rs = 1000.0, RL = 100e3;
		bool kcl = true, gain = true, inside = true;
		double worst = 0.0;
		for (double vs = -15.0; vs <= 27.0; vs += 0.5) {
			hef4066::OnState s = chip.solveOn(vs, Rs, RL);
			auto ic = [&](double v) {
				return 8.4e-14 * (std::exp((v - 12.0) / 0.025852) - 1.0) - 8.4e-14 * (std::exp(-v / 0.025852) - 1.0);
			};
			double f1 = (vs - s.vin) / Rs - ic(s.vin) - (s.vin - s.vout) / s.ron;
			double f2 = (s.vin - s.vout) / s.ron - s.vout / RL - ic(s.vout);
			double scale = std::fabs(vs) / Rs + 1e-6;
			worst = std::fmax(worst, std::fmax(std::fabs(f1), std::fabs(f2)) / scale);
			if (!(std::fabs(f1) < 1e-8 + 1e-6 * scale && std::fabs(f2) < 1e-8 + 1e-6 * scale)) kcl = false;
			if (vs >= 0.5 && vs <= 11.5) {
				double want = vs * RL / (RL + Rs + s.ron);
				if (!near(s.vout, want, 1e-3 * want)) gain = false;
			}
			if (s.vin < -0.9 || s.vin > 12.9 || s.vout < -0.9 || s.vout > 12.9) inside = false;
		}
		check("both KCL equations hold at -15..+27 V drive (worst relative residual below 1e-6)", kcl);
		printf("    worst relative KCL residual %.2e\n", worst);
		check("inside 0.5..11.5 V the switch is the divider RL/(RL + Rs + Ron) to 0.1 %", gain);
		check("pins never leave -0.9 .. VDD + 0.9 V however hard they are driven (clamps)", inside);
		hef4066::OnState a = chip.solveOn(-5.0, Rs, RL), b = chip.solveOn(17.0, Rs, RL), m = chip.solveOn(-0.2, Rs, RL);
		printf("    -5 V in -> %.3f V out; +17 V in -> %.3f V out; -0.2 V in -> %.3f V out\n", a.vout, b.vout, m.vout);
		check("-5 V is clipped to about one diode drop below ground", a.vout > -0.9 && a.vout < -0.3);
		check("+17 V is clipped to about one diode drop above VDD", b.vout > 12.3 && b.vout < 12.9);
		check("-0.2 V still passes (inside the clamp)", near(m.vout, -0.2 * RL / (RL + Rs + m.ron), 5e-3));
	}

	printf("-- distortion against Table 12\n");
	auto thd = [&](double vdd, double rl, double amp, double centre) {
		hef4066::Chip c = chip;
		c.vdd = vdd;
		const int N = 4096, cycles = 64;
		double re[16] = {}, im[16] = {};
		for (int n = 0; n < N; n++) {
			double ph = 2.0 * PI * cycles * n / N;
			hef4066::OnState s = c.solveOn(centre + amp * std::sin(ph), 1e-3, rl);
			for (int h = 1; h <= 8; h++) { re[h] += s.vout * std::cos(h * ph); im[h] += s.vout * std::sin(h * ph); }
		}
		double fund = std::sqrt(re[1] * re[1] + im[1] * im[1]), rest = 0.0;
		for (int h = 2; h <= 8; h++) rest += re[h] * re[h] + im[h] * im[h];
		return 100.0 * std::sqrt(rest) / fund;
	};
	double thd10 = thd(10.0, 10e3, 2.5, 5.0), thd5 = thd(5.0, 10e3, 1.25, 2.5), thd15 = thd(15.0, 10e3, 3.75, 7.5);
	printf("    THD 0.5 VDD p-p, RL 10k: 5 V %.3f %% (0.25), 10 V %.3f %% (0.04), 15 V %.3f %% (0.04)\n", thd5, thd10, thd15);
	// Fig. 6 is read to a couple of ohms, and THD is a difference of those. At 10 and 15 V (the
	// board runs at 12) the model is within a factor of 3 of the datasheet, which a constant Ron
	// (no distortion at all) cannot meet. At 5 V the model is 7x high: Fig. 6's 350 ohm hump
	// across a 2.5 V swing into 10k implies about 1.7 %, the table says 0.25 % -- an inconsistency
	// inside the datasheet (or a gentler hump at the test current) that does not touch 12 V.
	auto thdOk = [](double v, double typ) { return v > typ / 3.0 && v < typ * 3.0; };
	check("THD within a factor of 3 of Table 12 at 10 and 15 V", thdOk(thd10, 0.04) && thdOk(thd15, 0.04));
	check("THD at 5 V is within a factor of 8 (known 7x discrepancy, see the comment)", thd5 > 0.25 / 8.0 && thd5 < 0.25 * 8.0);
	check("THD is highest at 5 V and no worse at 15 V than at 5 V", thd5 > thd10 && thd5 > thd15);

	printf("-- OFF-state feedthrough, crosstalk and charge injection\n");
	const referral::Board board;
	auto runSine = [&](const referral::Board& bd, double fHz, double dt, double amp, bool onOther, double rl) {
		referral::Board b = bd;
		b.rLoad = rl;
		referral::Channel ch;
		double sumsq = 0.0, sumA = 0.0;
		int n = (int)(40.0 / (fHz * dt));
		for (int i = 0; i < n; i++) {
			double x = amp * std::sin(2.0 * PI * fHz * dt * i);
			referral::Out o = onOther ? ch.process(b, 0.0, 0.0, x, dt, true, false, false)
			                          : ch.process(b, 0.0, x, 0.0, dt, true, false, false);
			if (i > n / 2) { sumsq += o.b * o.b; sumA += x * x; }
		}
		return 20.0 * std::log10(std::sqrt(sumsq / sumA));
	};
	auto feedOk = [&](const referral::Board& bd) {
		// Table 12: -50 dB at 1 MHz, RL = 1k (isolation, and crosstalk between switches).
		double iso = runSine(bd, 1e6, 1e-9, 1.0, false, 1000.0);
		double xt = runSine(bd, 1e6, 1e-9, 1.0, true, 1000.0);
		return std::fabs(iso + 50.0) < 1.5 && std::fabs(xt + 50.0) < 1.5;
	};
	check("OFF isolation and crosstalk are -50 dB (+-1.5) at 1 MHz into 1k", feedOk(board));
	printf("    isolation %.2f dB, crosstalk %.2f dB\n", runSine(board, 1e6, 1e-9, 1.0, false, 1000.0),
		runSine(board, 1e6, 1e-9, 1.0, true, 1000.0));
	{
		double db = runSine(board, 1000.0, 1.0 / 48000.0, 5.0, false, 100e3);
		double want = 20.0 * std::log10(0.5e-12 * 100e3 * 2.0 * PI * 1000.0);
		printf("    at 1 kHz into 100k: %.2f dB (a first-order Cios RL w: %.2f dB)\n", db, want);
		check("at audio the leak is the 0.5 pF into 100k differentiator to 0.3 dB", near(db, want, 0.3));
	}

	auto injectionOk = [&](const referral::Board& bd, double* upQ, double* downQ) {
		const double dt = 1.0 / 48000.0;
		referral::Channel ch;
		double qUp = 0.0, qDown = 0.0;
		for (int i = 0; i < 200; i++) {
			double gate = (i < 50 || i >= 120) ? 0.0 : 5.0;
			referral::Out o = ch.process(bd, gate, 0.0, 0.0, dt, true, false, false);
			if (i >= 50 && i < 120) qUp += o.b * dt;
			else if (i >= 120) qDown += o.b * dt;
		}
		// Area (V s) = Q * (RL || (Rs + Ron)) with the signal at 0 V.
		hef4066::OnState s = bd.chip.solveOn(0.0, bd.rSource, bd.rLoad);
		double reff = bd.rLoad * (bd.rSource + s.ron) / (bd.rLoad + bd.rSource + s.ron);
		double want = bd.chip.edgeCharge() * reff;
		if (upQ) *upQ = qUp;
		if (downQ) *downQ = qDown;
		return near(qUp, want, 0.02 * want) && near(qDown, -want, 0.02 * want);
	};
	{
		double u = 0, d = 0;
		check("each enable edge leaves +q (rising) / -q (falling) on the output, area q * Reff", injectionOk(board, &u, &d));
		printf("    edge area up %.3e V s, down %.3e V s (q %.3e C)\n", u, d, board.chip.edgeCharge());
		check("the charge reproduces Fig. 11: 25 mV per edge (50 mV between spikes) on CL + Cos at 10 V",
			[&] { hef4066::Chip c; c.vdd = 10.0; return near(c.edgeCharge() / c.cSpike, 0.025, 1e-9); }());
	}

	printf("-- the enable path\n");
	{
		const referral::Drive& dir = board.drive[0];
		const referral::Drive& inv = board.drive[1];
		printf("    direct:   E on %.3f V, off %.3f V, LED %.2f mA, delay on/off %.2f / %.2f us\n",
			dir.eOn, dir.eOff, dir.ledOnA * 1e3, dir.delayToOn * 1e6, dir.delayToOff * 1e6);
		printf("    inverted: E on %.3f V, off %.3f V, LED %.2f mA, delay on/off %.2f / %.2f us\n",
			inv.eOn, inv.eOff, inv.ledOnA * 1e3, inv.delayToOn * 1e6, inv.delayToOff * 1e6);
		check("Vref = 12 V * 10k / 110k = 1.0909 V", near(board.vref, 12.0 * 10e3 / 110e3, 1e-12));

		// Independent scalar solutions of the two ON levels by hand-rolled fixed points.
		auto vdiode = [](double i, double is, double nvt) { return nvt * std::log(i / is + 1.0); };
		const double vt = 0.025852, ledNvt = 2.0 * vt, ledIs = 20e-3 / std::exp(2.0 / ledNvt);
		const double emf = board.op.emfHigh();
		// direct, comparator high: EMF - 526 I = Vd1(I) + x, I = (x - Vbe)/1k + (x - Vled(I_L))/1k
		double lo = 0.0, hi = emf;
		for (int it = 0; it < 80; it++) {
			double x = 0.5 * (lo + hi);
			double il = std::fmax(0.0, 0.0), ib = 0.0;
			// LED branch: x = I_L * 1k + Vled(I_L): solve I_L
			double a = 0.0, b = x / 1000.0;
			for (int k = 0; k < 80; k++) { double m = 0.5 * (a + b); il = m; if (m * 1000.0 + vdiode(m, ledIs, ledNvt) > x) b = m; else a = m; }
			// base branch: x = Ib*1k + Vbe(Ib), Ib = Is/BF exp(Vbe/Vt)
			a = 0.0; b = x / 1000.0;
			for (int k = 0; k < 80; k++) { double m = 0.5 * (a + b); ib = m; if (m * 1000.0 + vdiode(m, 7.6e-14 / 520.0, vt) > x) b = m; else a = m; }
			double itot = il + ib;
			double need = x + vdiode(itot, 2.52e-9, 1.752 * vt) + 526.0 * itot;
			if (need > emf) hi = x; else lo = x;
		}
		double xDirect = 0.5 * (lo + hi);
		// inverted, comparator low: E = 12 - 1k I, I = LED branch current
		double a = 0.0, b = 12.0 / 1000.0, il = 0.0;
		for (int k = 0; k < 100; k++) { double m = 0.5 * (a + b); il = m; if (12.0 - m * 1000.0 < m * 1000.0 + vdiode(m, ledIs, ledNvt)) b = m; else a = m; }
		double xInv = 12.0 - 1000.0 * il;
		printf("    hand-solved: direct %.3f V, inverted %.3f V\n", xDirect, xInv);
		check("direct ON level equals the hand-solved fixed point (to 30 mV)", near(dir.eOn, xDirect, 0.03));
		check("inverted ON level equals the hand-solved collector pull-up against the LED (to 30 mV)", near(inv.eOn, xInv, 0.03));
		check("both OFF levels are at or below one clamp drop above ground / the saturated collector",
			dir.eOff > -0.8 && dir.eOff < 0.1 && inv.eOff >= 0.0 && inv.eOff < 0.3);
		// The datasheet's guaranteed levels at 12 V (VIL max 3.0 -> 4.0, VIH min 7.0 -> 11.0 across 10 -> 15 V).
		double vil = 3.0 + (4.0 - 3.0) * 2.0 / 5.0, vih = 7.0 + (11.0 - 7.0) * 2.0 / 5.0;
		check("the ON levels sit in the undefined band VIL max .. VIH min (the board is marginal)",
			dir.eOn > vil && dir.eOn < vih && inv.eOn > vil && inv.eOn < vih);
		check("the LED is lit on the ON side in both positions (mA range)", dir.ledOnA > 2e-3 && inv.ledOnA > 2e-3);
		check("delay midpoints: E(emfMid) is half way between its levels (1 mV)", [&] {
			double e0 = board.eVolts(false, dir.emfMid), e1 = board.eVolts(true, inv.emfMid);
			return near(e0, 0.5 * (dir.eOn + dir.eOff), 1e-3) && near(e1, 0.5 * (inv.eOn + inv.eOff), 1e-3);
		}());
		check("comparator slew delays are between 0.05 and 2 us", dir.delayToOn > 5e-8 && dir.delayToOn < 2e-6 &&
			dir.delayToOff > 0.0 && dir.delayToOff < 2e-6 && inv.delayToOn > 0.0 && inv.delayToOff > 5e-8);
	}

	printf("-- the gate comparator (one threshold, no hysteresis) and the channel\n");
	{
		const double dt = 1.0 / 48000.0;
		referral::Channel ch;
		auto pass = [&](double gate, double a, bool hiOn, bool route, int settle = 40) {
			referral::Out o;
			for (int i = 0; i < settle; i++) o = ch.process(board, gate, a, 0.0, dt, hiOn, route, false);
			return o;
		};
		referral::Out low = pass(1.05, 4.0, true, false), high = pass(1.13, 4.0, true, false);
		check("gate 1.05 V is below the 1.0909 V line: off; 1.13 V: on", low.wB < 1e-9 && high.wB > 1.0 - 1e-9);
		check("on passes A through Ron and the divider (4 V -> 3.95..3.99 V)", high.b > 3.95 && high.b < 3.99);
		check("off leaves B at the feedthrough (below 1 mV at a constant 4 V)", std::fabs(low.b) < 1e-3);
		referral::Out mid = pass(0.5, 4.0, true, false);
		check("0.5 V stays off however it got there (the old 0.1/1 V hysteresis is gone)", mid.wB < 1e-9);
		referral::Out lowPol = pass(0.0, 4.0, false, false), lowPol1 = pass(2.0, 4.0, false, false);
		check("Lo On: gate low passes, gate high blocks", lowPol.wB > 1.0 - 1e-9 && lowPol1.wB < 1e-9);
		referral::Out ab = pass(2.0, 4.0, true, false), ac = pass(0.0, 4.0, true, true), acOn = pass(2.0, 4.0, true, true);
		check("A-B mode leaves C at 0 V", std::fabs(ab.c) < 1e-9);
		check("A-B/A-C: gate low sends A to C, gate high to B", ac.c > 3.95 && std::fabs(ac.b) < 1e-3 && acOn.b > 3.95 && std::fabs(acOn.c) < 1e-3);
		referral::Out neg = pass(2.0, -5.0, true, false);
		check("a -5 V input comes out near -0.6 V (the negative half is clipped)", neg.b > -0.9 && neg.b < -0.2);
		referral::Out big = pass(2.0, 17.0, true, false);
		check("a +17 V input comes out near VDD + 0.6 V", big.b > 12.2 && big.b < 12.9);
	}

	printf("-- make-before-break\n");
	{
		hef4066::Chip c;
		bool all = true;
		for (double v = 5.0; v <= 15.0; v += 1.0) {
			c.vdd = v;
			if (!(c.tPHZ() > c.tPZH() && c.tPLZ() > c.tPZL())) all = false;
		}
		check("Table 8: tPHZ > tPZH and tPLZ > tPZL at every VDD (no break-before-make)", all);
		c.vdd = 12.0;
		printf("    at 12 V: tPHZ %.0f ns, tPLZ %.0f ns, tPZH %.0f ns, tPZL %.0f ns\n",
			c.tPHZ() * 1e9, c.tPLZ() * 1e9, c.tPZH() * 1e9, c.tPZL() * 1e9);

		const double dt = 1.0 / 48000.0;
		auto overlap = [&](const referral::Board& bd, double* expected) {
			referral::Channel ch;
			for (int i = 0; i < 20; i++) ch.process(bd, 0.0, 8.0, 0.0, dt, true, true, false);
			double best = 0.0;
			// Cross the 1.0909 V line a quarter of the way into one sample: previous -2, now 4.
			ch.process(bd, 0.0, 8.0, 0.0, dt, true, true, false);
			referral::Out o = ch.process(bd, 4.0 * 1.0 + 0.0, 8.0, 0.0, dt, true, true, false);
			// previous gate was 0 V: the crossing is at f = 1.0909/4 of the sample.
			best = o.wB + o.wC;
			double f = bd.vref / 4.0;
			bool hiLevel = 8.0 > 0.5 * bd.chip.vdd;
			double tOn = f * dt + bd.drive[0].delayToOn + (hiLevel ? bd.chip.tPZH() : bd.chip.tPZL());
			double tOff = f * dt + bd.drive[1].delayToOff + (hiLevel ? bd.chip.tPHZ() : bd.chip.tPLZ());
			if (expected) *expected = 1.0 + (tOff - tOn) / dt;
			return best;
		};
		double want = 0.0;
		double got = overlap(board, &want);
		printf("    gate edge: wB + wC = %.6f (expected %.6f)\n", got, want);
		printf("    (%s by %.0f ns: the chip alone is make-before-break by %.0f ns, the comparator's slew into the direct E pin is %.0f ns slower than into the inverted one)\n",
			want < 1.0 ? "BREAK-before-make" : "make-before-break", std::fabs((want - 1.0) * dt) * 1e9,
			(c.tPHZ() - c.tPZH()) * 1e9, (board.drive[0].delayToOn - board.drive[1].delayToOff) * 1e9);
		check("across the edge wB + wC = 1 + (tOff - tOn)/dt exactly, with the board's own delays", near(got, want, 1e-9));
		referral::Board mbb = board;
		mbb.drive[0].delayToOn = 0.0;
		mbb.drive[1].delayToOff = 2e-6;
		double wantM = 0.0, gotM = overlap(mbb, &wantM);
		check("engineered make-before-break (OFF side late): the pair conducts more than the sample", gotM > 1.0 && near(gotM, wantM, 1e-9));
	}

	printf("-- declick (the module's own, 1 ms)\n");
	{
		const double dt = 1.0 / 48000.0;
		referral::Channel ch;
		double prev = 0.0, maxStep = 0.0;
		for (int i = 0; i < 400; i++) {
			double gate = i < 100 ? 0.0 : 5.0;
			referral::Out o = ch.process(board, gate, 5.0, 0.0, dt, true, false, true);
			if (i >= 100 && i < 300) maxStep = std::fmax(maxStep, std::fabs(o.b - prev));
			prev = o.b;
		}
		check("with declick on, the turn-on steps at most 5 V / 48 + a little per sample", maxStep < 0.2);
		check("and has finished the fade within 1 ms (50 samples): B is at the divider level", prev > 4.9 && prev < 5.0);
	}

	// ---- negative controls: break the model, the matching group must fail ----------------
	printf("-- negative controls (each must FAIL)\n");
	{
		hef4066::Chip wrong = chip;
		wrong.tempC = 125.0;                       // hot: every Table 7 rail value is far off
		check("NEG: at 125 C the 25 C Table 7 group fails", !ronGroup(wrong, false));

		referral::Board noFeed = board;
		noFeed.chip.cFeed = 0.0;
		check("NEG: no Cios: the -50 dB isolation check fails", !feedOk(noFeed));

		referral::Board noQ = board;
		noQ.chip.edgeSpikeAt10V = 0.0;
		check("NEG: no control charge: the injection check fails", !injectionOk(noQ, nullptr, nullptr));

		referral::Board noClamp = board;
		noClamp.chip.clampIs = 1e-30;
		{
			hef4066::OnState a = noClamp.chip.solveOn(-5.0, 1000.0, 100e3);
			check("NEG: no clamp diodes: -5 V is NOT clipped (the clipping check would fail)", a.vout < -3.0);
		}
		// A constant-Ron switch has no distortion: THD far below the datasheet's.
		{
			const int N = 2048, cycles = 64;
			double re[8] = {}, im[8] = {};
			double r = 75.0;
			for (int n = 0; n < N; n++) {
				double ph = 2.0 * PI * cycles * n / N;
				double v = (5.0 + 2.5 * std::sin(ph)) * 10e3 / (10e3 + r);
				for (int h = 1; h <= 7; h++) { re[h] += v * std::cos(h * ph); im[h] += v * std::sin(h * ph); }
			}
			double fund = std::sqrt(re[1] * re[1] + im[1] * im[1]), rest = 0.0;
			for (int h = 2; h <= 7; h++) rest += re[h] * re[h] + im[h] * im[h];
			double t = 100.0 * std::sqrt(rest) / fund;
			check("NEG: a constant Ron has THD far below the datasheet's", !thdOk(t, 0.04));
		}
		// Break-before-make instead of make-before-break: the overlap check must see it.
		{
			referral::Board bbm = board;
			bbm.drive[1].delayToOff = 0.0;
			bbm.drive[0].delayToOn = 5e-6;           // the ON side much later than the OFF side
			const double dt = 1.0 / 48000.0;
			referral::Channel ch;
			for (int i = 0; i < 20; i++) ch.process(bbm, 0.0, 8.0, 0.0, dt, true, true, false);
			ch.process(bbm, 0.0, 8.0, 0.0, dt, true, true, false);
			referral::Out o = ch.process(bbm, 4.0, 8.0, 0.0, dt, true, true, false);
			check("NEG: with the ON side late the pair is break-before-make and wB + wC < 1", o.wB + o.wC < 1.0);
		}
		// Rail the supply: at 5 V the 12 V claims about the ON levels fail.
		{
			referral::Board low = board;
			low.chip.vdd = 5.0;
			low.prepare();
			check("NEG: a 5 V board puts the ON level where the 12 V hand solution is not", !near(low.drive[0].eOn, 5.803, 0.5));
		}
	}

	printf("\n%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}

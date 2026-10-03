// Accrual: the 4069 VCO (src/Accrual/Vco.hpp) against oracles that are NOT its own equations.
//
//   * the PNP added to Mna.hpp, against a one-unknown Ebers-Moll solve written out here, and
//     against the NPN it mirrors;
//   * the exponential pair + summing node (Front), against the full netlist's DC solution (the
//     solver's stamps, not Front's hand-written Jacobian);
//   * the integrator + Schmitt + reset (Plant, Voice), against the WHOLE netlist of the PDF --
//     16 unknowns, three BJTs, six CD4069UB inverters, two diodes -- stepped by the trapezoid rule
//     at an adaptive 0.02 ns .. 20 us step (tests/Accrual/Fullnet.hpp). Period over eleven pitch
//     settings, three supplies and three input capacitances; the integrator's waveform over a
//     cycle; the pulse duty over PW and PWM settings; the saw's amplitude;
//   * the audio-rate behaviour: sample-rate independence, band-limiting (and that the test can
//     see its absence), the AC coupling;
//   * negative controls: the model is broken in six ways and the same checks are shown to fail.

#include "Fullnet.hpp"

#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

using namespace accrual;
using namespace fullnet;

static int checks = 0, failures = 0;
static void check(const char* what, bool ok) {
	checks++;
	if (!ok) { failures++; printf("  FAIL  %s\n", what); }
}
static void checkv(const char* what, double got, double want, double tol) {
	checks++;
	if (!(std::fabs(got - want) <= tol)) {
		failures++;
		printf("  FAIL  %s: got %.6g, want %.6g (tol %.3g)\n", what, got, want, tol);
	}
}

// ---------------------------------------------------------------------------------------------
// 1. The PNP in Mna.hpp
// ---------------------------------------------------------------------------------------------
// Oracle: emitter fed from 5 V through 10k, base at a fixed 4.3 V, collector to ground through
// 1k. One unknown (the emitter), bisection on the Ebers-Moll law written here in its own form.
static void testPnp() {
	printf("PNP (Mna.hpp)\n");
	const double Is = 3e-14, BF = 150.0, BR = 3.0, VAF = 60.0, Vt = mna::kVt;
	const double vcc = 5.0, re = 10e3, rc = 1e3, vb = 4.3;
	// unknowns: emitter ve, collector vc. Currents: PNP emitter current into the device ie,
	// collector current out of the device ic.
	auto eval = [&](double ve, double vc, double& ie, double& ic) {
		double vbe = ve - vb, vbc = vc - vb;                 // forward when ve > vb
		double ef = std::exp(vbe / Vt), er = std::exp(vbc / Vt);
		double icT = Is * (ef - er) * (1.0 - vbc / VAF);     // emitter -> collector transport
		double ibF = Is / BF * (ef - 1.0), ibR = Is / BR * (er - 1.0);
		ic = icT - ibR;                                       // into the collector node
		ie = icT + ibF;                                       // from the emitter node
	};
	// nested bisection is overkill; Newton with numerical Jacobian on the two KCLs
	double ve = 4.9, vc = 0.1;
	for (int it = 0; it < 200; it++) {
		double ie, ic, F0, F1;
		auto F = [&](double e, double c, double& f0, double& f1) {
			double a, b; eval(e, c, a, b);
			f0 = (vcc - e) / re - a;                          // KCL at the emitter
			f1 = b - c / rc;                                  // KCL at the collector
		};
		F(ve, vc, F0, F1); (void) ie; (void) ic;
		double h = 1e-7, f0e, f1e, f0c, f1c;
		F(ve + h, vc, f0e, f1e); F(ve, vc + h, f0c, f1c);
		double a = (f0e - F0) / h, b = (f0c - F0) / h, c = (f1e - F1) / h, d = (f1c - F1) / h;
		double det = a * d - b * c;
		double de = (-F0 * d + F1 * b) / det, dc = (-F1 * a + F0 * c) / det;
		ve += de; vc += dc;
		if (std::fabs(de) + std::fabs(dc) < 1e-13) break;
	}
	mna::Circuit c;
	c.n = 2;
	enum { E, C };
	c.addResistor(mna::fixed(0), E, re);
	c.addResistor(C, mna::GND, rc);
	c.addPnp(C, mna::fixed(1), E, Is, BF, BR, VAF);
	double tgt[2] = { vcc, vb };
	check("PNP circuit converges", c.solveDc(tgt, 2, 24));
	checkv("PNP emitter voltage", c.v[E], ve, 1e-6);
	checkv("PNP collector voltage", c.v[C], vc, 1e-6);
	check("PNP conducts (collector above ground)", vc > 0.005);
	// The mirror: negate every voltage and the same circuit is an NPN.
	mna::Circuit m;
	m.n = 2;
	m.addResistor(mna::fixed(0), E, re);
	m.addResistor(C, mna::GND, rc);
	m.addNpn(C, mna::fixed(1), E, Is, BF, BR, VAF);
	double tm[2] = { -vcc, -vb };
	m.solveDc(tm, 2, 24);
	checkv("NPN mirror: emitter", m.v[E], -c.v[E], 1e-9);
	checkv("NPN mirror: collector", m.v[C], -c.v[C], 1e-9);
	// reverse-active corner: collector held above the base so the collector junction conducts
	mna::Circuit r;
	r.n = 1;
	r.addResistor(0, mna::fixed(0), 1e3);
	r.addPnp(mna::fixed(1), mna::fixed(2), 0, Is, BF, BR, VAF);
	double tr2[3] = { 4.0, 3.0, 4.2 };               // emitter pulled up by 1k to 4 V, collector 3 V, base 4.2 V
	check("PNP, base above emitter: converges", r.solveDc(tr2, 3, 24));
	check("PNP, base above emitter: emitter near the supply (off)", r.v[0] > 3.9);
}

// ---------------------------------------------------------------------------------------------
// 2. The exponential pair
// ---------------------------------------------------------------------------------------------
static void testFront() {
	printf("exponential converter\n");
	double r21 = calibrateTrim(true, true);
	check("calibrated trimmer is a plausible rheostat setting (0..1k)", r21 > 0.0 && r21 < 1000.0);
	struct In { double tune, fine, cv1, cv2; };
	const In ins[] = { {0,0,0,0}, {2,0,0,0}, {-3,0,0,0}, {1,0.5,0.25,-0.5}, {4,-2,1,1}, {-4,0,-2,2}, {5,0,1,0} };
	double worstA = 0, worstQ = 0;
	for (size_t i = 0; i < sizeof(ins) / sizeof(ins[0]); i++) {
		static Net n;
		n.build(r21, true, true, false, 12.0, 0.5);
		n.setInputs(ins[i].tune, ins[i].fine, ins[i].cv1, ins[i].cv2, 0.0);
		n.start();
		// the DC start leaves the integrator saturated (X near ground, Q2's collector junction
		// forward), which is not an operating point; let it run to the middle of a ramp
		static Trace tr0;
		tr0 = Trace();
		bool okRun = runTrips(n, tr0, 3, 5.0);
		if (!okRun) { printf("    (oracle did not reach three trips at input %zu: %zu)\n", i, tr0.trips.size()); check("oracle runs", false); continue; }
		{
			double per = tr0.trips[2] - tr0.trips[1];
			Transient<Big> T(&n.c);
			T.hMax = 20e-6; T.dvMax = 0.05;
			while (T.t < 0.5 * per) T.advance(0.5 * per);
		}
		Front f;
		double g = 1.0 / R1 + 1.0 / R2 + 1.0 / R3 + 1.0 / R4 + 1.0 / (r21 + R6);
		f.solve(ins[i].tune / R1 + ins[i].fine / R2 + ins[i].cv1 / R3 + ins[i].cv2 / R4, g);
		worstA = std::max(worstA, std::fabs(f.a - n.c.v[A]));
		worstQ = std::max(worstQ, std::fabs(f.qb - n.c.v[QB]));
	}
	checkv("summing node A, closed-form front end vs full netlist DC (worst of 7)", worstA, 0.0, 2e-5);
	checkv("QB (Q2's base), front end vs full netlist DC (worst of 7)", worstQ, 0.0, 5e-5);

	// one volt on a CV input is one octave, to the exponential's own accuracy
	double worst = 0;
	for (double v = -4.0; v <= 5.0; v += 1.0) {
		Front f0, f1;
		double g = 1.0 / R1 + 1.0 / R2 + 1.0 / R3 + 1.0 / R4 + 1.0 / (r21 + R6);
		f0.solve(v / R3, g);
		f1.solve((v + 1.0) / R3, g);
		worst = std::max(worst, std::fabs(std::log2(f1.ibase / f0.ibase) - 1.0));
	}
	checkv("1 V/oct on CV1 after calibration, -4..+6 V (octaves of error per volt)", worst, 0.0, 0.01);
	// the trimmer moves the scale the way the sheet says: more resistance, more octaves per volt
	Front a, b;
	double ga = 1.0 / R1 + 1.0 / R2 + 1.0 / R3 + 1.0 / R4 + 1.0 / (0.0 + R6);
	double gb = 1.0 / R1 + 1.0 / R2 + 1.0 / R3 + 1.0 / R4 + 1.0 / (1000.0 + R6);
	a.solve(0, ga); b.solve(0, gb);
	Front a1, b1; a1.solve(1.0 / R3, ga); b1.solve(1.0 / R3, gb);
	double oa = std::log2(a1.ibase / a.ibase), ob = std::log2(b1.ibase / b.ibase);
	check("R21 = 0 gives less than an octave per volt, 1k more (the sheet: 15..24 mV/V)", oa < 1.0 && ob > 1.0);
	checkv("R21 = 0: slope in mV per volt on A matches the divider (about 14.8)", oa * mna::kVt * std::log(2.0) * 1e3, 14.8, 0.8);
	// unpatched inputs are open: a patched-but-0-V CV2 shifts the scale (it adds a 100k to ground)
	Front p0, p1; double g3 = 1.0 / R1 + 1.0 / R2 + 1.0 / R3 + 1.0 / (r21 + R6);
	p0.solve(0, g3); p1.solve(1.0 / R3, g3);
	double o3 = std::log2(p1.ibase / p0.ibase);
	check("CV2 left open raises the scale slightly (less loading on A)", o3 > 1.0 && o3 < 1.04);
}

// ---------------------------------------------------------------------------------------------
// 3. The plant
// ---------------------------------------------------------------------------------------------
static Plant& plantAt(double vdd, double cin = kCin) {
	static std::map<std::string, Plant*> cache;
	char key[64]; snprintf(key, sizeof key, "%g/%g", vdd, cin * 1e12);
	auto it = cache.find(key);
	if (it != cache.end()) return *it->second;
	Plant* p = new Plant;
	p->build(vdd, cin);
	cache[key] = p;
	return *p;
}

static void testPlant() {
	printf("the tabulated integrator\n");
	Plant& p = plantAt(12.0);
	check("plant built at 12 V", p.ok);
	bool mono = true;
	for (int k = 1; k < p.n; k++) if (!(p.W[k] > p.W[k - 1]) || !(p.Zt[k] >= p.Zt[k - 1] - 1e-9) || !(p.Psi0[k] > p.Psi0[k - 1])) mono = false;
	check("W, Z and the phase are monotone along the ramp table", mono);
	check("table is dense enough (>= 300 nodes, < the array)", p.n >= 300 && p.n < kTab - 1);
	// the CD4069UB gain at its self-bias point: the documented constant, bounded -11..-30 at 12 V
	double gain = cd4069::selfBiasedGain(12.0);
	check("CD4069UB gain at 12 V inside the datasheet-bounded -11..-30 (the model: about -23)", gain < -11.0 && gain > -30.0);
	// the Schmitt trips below the ideal threshold VM (1 + R9/R10), because the inverter that drives
	// it is a finite-gain stage with a load, and the fold is where the S-low equilibrium ends
	double ideal = p.vm * (1.0 + R9 / R10);
	check("Schmitt trips below the ideal-source threshold, within 10 %", p.zTrip < ideal && p.zTrip > 0.9 * ideal);
	// hysteresis: at an X just above the fold the S-HIGH solution also exists (bistable), which is
	// what makes it a Schmitt trigger and not a comparator
	Plant::Dc dc;
	dc.build(12.0, p.vm, p.vc2);
	dc.c.v[Plant::Dc::Z] = 3.0; dc.c.v[Plant::Dc::P3] = 7.0; dc.c.v[Plant::Dc::P4] = 0.0; dc.c.v[Plant::Dc::S] = 12.0;
	dc.c.fixedV[0] = p.uTrip + 0.02;
	bool ok = dc.c.solveHere(200);
	check("S-high equilibrium exists at the fold's X + 20 mV (hysteresis)", ok && dc.c.v[Plant::Dc::S] > 6.0);
	dc.c.v[Plant::Dc::Z] = p.zTrip; dc.c.v[Plant::Dc::P3] = 4.0; dc.c.v[Plant::Dc::P4] = 12.0; dc.c.v[Plant::Dc::S] = 0.0;
	ok = dc.c.solveHere(200);
	check("and the S-low one", ok && dc.c.v[Plant::Dc::S] < 6.0);
	// ... and past the fold (X lower by 20 mV) the S-low one is gone
	dc.c.fixedV[0] = p.uTrip - 0.02;
	dc.c.v[Plant::Dc::Z] = p.zTrip; dc.c.v[Plant::Dc::P3] = 4.0; dc.c.v[Plant::Dc::P4] = 12.0; dc.c.v[Plant::Dc::S] = 0.0;
	ok = dc.c.solveHere(200);
	check("no S-low equilibrium 20 mV past the fold", !ok || dc.c.v[Plant::Dc::S] > 6.0);
	// the reset
	check("reset lasts 3..10 us at every ramp current", [&] {
		for (int k = 0; k < Plant::kRs; k++) if (!(p.rs[k].dur > 3e-6 && p.rs[k].dur < 10e-6)) return false;
		return true; }());
	check("C1 is left at a negative voltage by the reset (Z below X)", p.rs[4].vc1 < -2.0 && p.rs[4].vc1 > -3.5);
	check("the reset path starts at Z_trip and passes the overshoot (> 9 V)", [&] {
		double mx = 0; for (int k = 0; k < kReset; k++) mx = std::max(mx, p.rs[4].zr[k]);
		return std::fabs(p.rs[4].zr[0] - p.zTrip) < 0.1 && mx > 9.0; }());
	// every supply builds
	for (double v : {9.0, 10.0, 15.0}) {
		Plant& q = plantAt(v);
		char w[64]; snprintf(w, sizeof w, "plant builds at %g V", v);
		check(w, q.ok);
	}
	for (double c : {5e-12, 15e-12, 20e-12}) {
		Plant& q = plantAt(12.0, c);
		char w[64]; snprintf(w, sizeof w, "plant builds at Cin = %g pF", c * 1e12);
		check(w, q.ok);
	}
}

// ---------------------------------------------------------------------------------------------
// 4. The scenarios: oracle and model on the same settings
// ---------------------------------------------------------------------------------------------
struct Sc {
	double tune = 0, fine = 0, cv1 = 0, cv2 = 0;
	bool p1 = true, p2 = true;
	double pot = 0.5;
	bool pw = false;
	double vp = 0.0;
	double vdd = 12.0, cin = kCin;
	std::string key() const {
		char b[200];
		snprintf(b, sizeof b, "%g %g %g %g %d %d %g %d %g %g %g", tune, fine, cv1, cv2, p1, p2, pot, pw, vp, vdd, cin * 1e12);
		return b;
	}
};

struct Res {
	double period = 0, duty = 0, sawPP = 0, pulsePP = 0;
	double z[64];            // Z over the cycle, from S-up, 64 phase points
	bool ok = false;
};

static double r21Default() { static double r = calibrateTrim(true, true); return r; }

static Res oracle(const Sc& s) {
	static std::map<std::string, Res> cache;
	auto it = cache.find(s.key());
	if (it != cache.end()) return it->second;
	static Net n;
	n.build(r21Default(), s.p1, s.p2, s.pw, s.vdd, s.pot, s.cin);
	n.setInputs(s.tune, s.fine, s.cv1, s.cv2, s.vp);
	n.start();
	static Trace tr;
	tr = Trace();
	Res r;
	r.ok = runTrips(n, tr, 6, 3.0);
	if (!r.ok) { cache[s.key()] = r; return r; }
	size_t m = tr.trips.size();
	double t1 = tr.trips[m - 2], t2 = tr.trips[m - 1];
	r.period = t2 - t1;
	double hi = 0, tt = 0, smin = 1e9, smax = -1e9, pmin = 1e9, pmax = -1e9;
	size_t k = 1;
	for (size_t i = 1; i < tr.t.size(); i++) {
		if (tr.t[i] < t1 || tr.t[i] >= t2) continue;
		double dt = tr.t[i] - tr.t[i - 1];
		tt += dt;
		if (tr.p12[i] > 0.5 * s.vdd) hi += dt;
		// the saw's amplitude without the 1.5 V, 1 us overshoot spike the reset throws above the ramp
		if (tr.t[i] > t1 + 8e-6) { smin = std::min(smin, tr.saw[i]); smax = std::max(smax, tr.saw[i]); }
		pmin = std::min(pmin, tr.pulse[i]); pmax = std::max(pmax, tr.pulse[i]);
	}
	r.duty = hi / tt; r.sawPP = smax - smin; r.pulsePP = pmax - pmin;
	for (int j = 0; j < 64; j++) {
		double to = t1 + (j + 0.5) / 64.0 * r.period;
		while (k + 1 < tr.t.size() && tr.t[k] < to) k++;
		double f = (to - tr.t[k - 1]) / (tr.t[k] - tr.t[k - 1]);
		r.z[j] = tr.z[k - 1] + (tr.z[k] - tr.z[k - 1]) * f;
	}
	cache[s.key()] = r;
	return r;
}

/** The audio-rate model on the same settings; `fs` and the plant are arguments. */
static Res model(const Plant& P, const Sc& s, double fs, double seconds, bool wantWave = false,
                 std::vector<double>* sawOut = nullptr, std::vector<double>* pulseOut = nullptr, bool blep = true) {
	Res r;
	Front f;
	double r21 = r21Default();
	double g = 1.0 / R1 + 1.0 / R2 + (s.p1 ? 1.0 / R3 : 0.0) + (s.p2 ? 1.0 / R4 : 0.0) + 1.0 / (r21 + R6);
	f.solve(s.tune / R1 + s.fine / R2 + (s.p1 ? s.cv1 / R3 : 0.0) + (s.p2 ? s.cv2 / R4 : 0.0), g);
	PwmFront pf;
	double zth = pulseThreshold(P.vm, s.pot, pf.solve(P.vm, s.vp, s.pw));
	static Voice v;
	v = Voice();
	v.setPlant(&P);
	double hiMark = 0, clMark = 0;
	v.bandLimit = blep;
	const double dt = 1.0 / fs;
	const int N = (int) (fs * seconds);
	std::vector<double> zs(N), hs(N);
	double saw, pulse;
	for (int i = 0; i < N; i++) {
		if (i == N / 2) { hiMark = v.highTime; clMark = v.clock; }
		v.process(dt, f.ibase, f.vb, zth, saw, pulse);
		zs[i] = v.zNow; hs[i] = v.pulseHigh ? 1.0 : 0.0;
		if (sawOut) sawOut->push_back(saw);
		if (pulseOut) pulseOut->push_back(pulse);
	}
	r.period = v.period;
	r.ok = v.period > 0;
	if (!r.ok) return r;
	const double T = v.period, tEnd = N * dt;
	// the last complete cycle starts at lastTrip - period
	double tStart = v.lastTrip - T;
	int i0 = (int) std::ceil(tStart / dt), i1 = (int) std::floor((v.lastTrip) / dt);
	// duty over the last 3 cycles to average the sampling
	r.duty = (v.highTime - hiMark) / (v.clock - clMark);        // from the exact edge times
	(void) i0; (void) i1; (void) tEnd; (void) hs;
	if (wantWave) {
		for (int j = 0; j < 64; j++) {
			double tm = tStart + (j + 0.5) / 64.0 * T;
			int i = (int) std::floor(tm / dt);
			if (i < 0 || i + 1 >= N) { r.z[j] = NAN; continue; }
			double fr = (tm - i * dt) / dt;
			// zNow at sample i is the value at the END of sample i: time (i+1) dt
			double ta = (i) * dt;
			int ia = (int) std::floor((tm - dt) / dt);
			(void) ta;
			if (ia < 0) { r.z[j] = NAN; continue; }
			double frr = (tm - (ia + 1) * dt) / dt;
			r.z[j] = zs[ia] + (zs[ia + 1] - zs[ia]) * frr;
			(void) fr;
		}
	}
	return r;
}

static double relErr(double a, double b) { return std::fabs(a - b) / b; }

static void testPeriods() {
	printf("period against the whole netlist\n");
	Plant& P = plantAt(12.0);
	const double tunes[] = { -4, -3, -2, -1, 0, 1, 2, 3, 4 };
	double worst = 0;
	for (double t : tunes) {
		Sc s; s.tune = t;
		Res o = oracle(s);
		Res m = model(P, s, 96000.0, std::max(0.35, 8 * o.period));
		char w[96]; snprintf(w, sizeof w, "oracle runs at Tune %+g V", t);
		check(w, o.ok);
		if (!o.ok || !m.ok) continue;
		double e = relErr(m.period, o.period);
		worst = std::max(worst, e);
		snprintf(w, sizeof w, "period at Tune %+g V (%.1f Hz): model/oracle within 0.25 %%", t, 1.0 / o.period);
		check(w, e < 0.0025);
	}
	printf("    worst relative period error over Tune: %.3f %%\n", 100.0 * worst);

	// mixed inputs: CV1, CV2 and Fine all in, and a CV left open
	{
		Sc s; s.tune = 1.0; s.fine = 2.5; s.cv1 = 0.5; s.cv2 = -1.25;
		Res o = oracle(s), m = model(P, s, 96000.0, std::max(0.3, 8 * o.period));
		check("period, all four inputs summing", o.ok && m.ok && relErr(m.period, o.period) < 0.0025);
		Sc q; q.tune = 1.0; q.p2 = false;
		Res o2 = oracle(q), m2 = model(P, q, 96000.0, std::max(0.3, 8 * o2.period));
		check("period, CV2 unpatched (open, as on the board)", o2.ok && m2.ok && relErr(m2.period, o2.period) < 0.0025);
	}
	// 1 V/oct measured on the oracle itself (whole circuit, not the model)
	{
		Sc a, b; a.tune = 0; b.tune = 1;
		double oct = std::log2(oracle(a).period / oracle(b).period);
		checkv("whole circuit: one volt is one octave within 1 % (Early effect, base current)", oct, 1.0, 0.012);
		Sc c; c.tune = 2; Sc d; d.tune = 4;
		oct = std::log2(oracle(c).period / oracle(d).period);
		checkv("whole circuit: two octaves in two volts (2..4 V)", oct, 2.0, 0.03);
	}
	// supplies and input capacitances
	for (double vdd : {9.0, 10.0, 15.0}) {
		Plant& Q = plantAt(vdd);
		for (double t : {0.0, 3.0}) {
			Sc s; s.tune = t; s.vdd = vdd;
			Res o = oracle(s), m = model(Q, s, 96000.0, std::max(0.3, 8 * o.period));
			char w[96]; snprintf(w, sizeof w, "period at VDD = %g V, Tune %+g V: within 0.4 %%", vdd, t);
			check(w, o.ok && m.ok && relErr(m.period, o.period) < 0.004);
		}
	}
	for (double c : {5e-12, 15e-12, 20e-12}) {
		Plant& Q = plantAt(12.0, c);
		Sc s; s.tune = 1.0; s.cin = c;
		Res o = oracle(s), m = model(Q, s, 96000.0, std::max(0.3, 8 * o.period));
		char w[96]; snprintf(w, sizeof w, "period at Cin = %g pF: within 0.4 %%", c * 1e12);
		check(w, o.ok && m.ok && relErr(m.period, o.period) < 0.004);
	}
	// the gain is the one number the datasheet only bounds: the ends of that bound both build, the
	// period moves monotonically with it and by well under a semitone-and-a-half at Tune 0
	{
		double f[3] = {0, 0, 0};
		const double k[3] = { 0.75, 1.0, 2.4 };
		double gains[3];
		for (int i = 0; i < 3; i++) {
			cd4069::Params q = cd4069::typical();
			q.n.lambda *= k[i]; q.p.lambda *= k[i];
			gains[i] = cd4069::selfBiasedGain(12.0, q);
			static Plant g;
			g.build(12.0, kCin, q);
			Sc s0; s0.tune = 0;
			Res m = model(g, s0, 96000.0, 0.4);
			f[i] = g.ok && m.ok ? 1.0 / m.period : 0.0;
		}
		printf("    inverter gain %.1f / %.1f / %.1f -> %.1f / %.1f / %.1f Hz at Tune 0\n", gains[0], gains[1], gains[2], f[0], f[1], f[2]);
		check("gain -30 and -11 bounds both build and oscillate", f[0] > 0 && f[2] > 0);
		check("the lower the gain the higher the pitch, monotonically", f[0] < f[1] && f[1] < f[2]);
		check("the whole datasheet-bounded gain range moves the pitch by under 20 % (the uncertainty to quote)", f[2] / f[0] < 1.20 && f[2] / f[0] > 1.05);
	}
	// the supply and the stray capacitance are real levers on the absolute pitch (the assumptions)
	{
		Sc a, b; a.tune = 1; b.tune = 1; b.cin = 20e-12;
		double r = oracle(b).period / oracle(a).period;
		check("a 20 pF input capacitance (vs 10) slows the oracle by more than 5 %: the assumption matters", r > 1.05);
	}
}

static void testWaveform() {
	printf("the integrator's waveform, the pulse and the saw\n");
	Plant& P = plantAt(12.0);
	for (double t : {0.0, 2.0}) {
		Sc s; s.tune = t;
		Res o = oracle(s), m = model(P, s, 192000.0, std::max(0.3, 8 * o.period), true);
		// skip the first 3 % (the reset) and the last 3 %
		double se = 0, mx = 0; int n = 0;
		for (int j = 2; j < 62; j++) {
			if (std::isnan(m.z[j])) continue;
			double e = m.z[j] - o.z[j];
			se += e * e; mx = std::max(mx, std::fabs(e)); n++;
		}
		char w[96];
		snprintf(w, sizeof w, "Z over a cycle at Tune %+g V: rms error under 10 mV", t);
		checkv(w, std::sqrt(se / n), 0.0, 0.010);
		snprintf(w, sizeof w, "Z over a cycle at Tune %+g V: worst error under 30 mV", t);
		checkv(w, mx, 0.0, 0.030);
	}
	// the pulse width
	struct W { double pot; bool pw; double vp; };
	const W ws[] = { {0.1,false,0}, {0.3,false,0}, {0.5,false,0}, {0.7,false,0}, {0.9,false,0},
	                 {0.5,true,-5}, {0.5,true,-2}, {0.5,true,0}, {0.5,true,2}, {0.3,true,3} };
	double worst = 0;
	for (const W& w : ws) {
		Sc s; s.tune = 2; s.pot = w.pot; s.pw = w.pw; s.vp = w.vp;
		Res o = oracle(s), m = model(P, s, 192000.0, 0.3);
		double e = std::fabs(m.duty - o.duty);
		worst = std::max(worst, e);
		char b[128]; snprintf(b, sizeof b, "duty, PW %.1f%s%+g V: oracle %.4f model %.4f (within 0.006)", w.pot, w.pw ? ", PWM " : ", PWM open", w.pw ? w.vp : 0.0, o.duty, m.duty);
		check(b, o.ok && m.ok && e < 0.006);
	}
	printf("    worst duty error: %.4f\n", worst);
	// ... and at a higher pitch (edges closer to the reset)
	{
		Sc s; s.tune = 4; s.pot = 0.5;
		Res o = oracle(s), m = model(P, s, 192000.0, 0.2);
		check("duty at 2 kHz within 0.01", std::fabs(m.duty - o.duty) < 0.01);
	}
	// the saw's amplitude through the AC coupling: the oracle's SAW node peak to peak
	{
		Sc s; s.tune = 2;
		Res o = oracle(s);
		std::vector<double> saw;
		model(P, s, 96000.0, 0.5, false, &saw);
		double mn = 1e9, mx = -1e9;
		size_t n0 = saw.size() - (size_t) (3 * 96000.0 * o.period);
		for (size_t i = n0; i < saw.size(); i++) { mn = std::min(mn, saw[i]); mx = std::max(mx, saw[i]); }
		checkv("saw peak-to-peak vs the oracle's SAW node (volts)", mx - mn, o.sawPP, 0.08 * o.sawPP);
	}
	// the AC coupling removes the DC: means of both outputs after settling, over whole cycles
	{
		Sc s; s.tune = 3; s.pot = 0.3;
		std::vector<double> saw, pul;
		model(P, s, 96000.0, 1.0, false, &saw, &pul);
		double ms = 0, mp = 0; size_t n = 0;
		for (size_t i = saw.size() / 2; i < saw.size(); i++) { ms += saw[i]; mp += pul[i]; n++; }
		ms /= n; mp /= n;
		check("saw mean is near 0 (AC coupled)", std::fabs(ms) < 0.15);
		check("pulse mean is near 0 (AC coupled)", std::fabs(mp) < 0.15);
	}
	// the hand-checkable high-pass: a held level decays with C R = 22 ms
	{
		const double al = 1.0 - std::exp(-1.0 / (48000.0 * kHpTau));
		double lp = 0, y = 0;
		for (int i = 0; i < 48000 * 22 / 1000; i++) { lp += al * (1.0 - lp); y = 1.0 - lp; }
		checkv("C R = 22 ms: after one time constant the step has fallen to 1/e", y, std::exp(-1.0), 0.01);
	}
}

static void testAudioRate() {
	printf("audio-rate behaviour\n");
	Plant& P = plantAt(12.0);
	Sc s; s.tune = 1.7;
	double base = 0;
	for (double fs : {44100.0, 48000.0, 96000.0, 192000.0}) {
		Res m = model(P, s, fs, 0.6);
		if (base == 0) base = m.period;
		char w[96]; snprintf(w, sizeof w, "period at %.0f Hz sampling equals the first rate's within 0.03 %%", fs);
		check(w, m.ok && relErr(m.period, base) < 0.0003);
	}
	for (double fs : {44100.0, 192000.0}) {
		Sc q; q.tune = 2.2; q.pot = 0.37;
		Res m = model(P, q, fs, 0.6);
		static double d0 = -1;
		if (d0 < 0) d0 = m.duty;
		char w[96]; snprintf(w, sizeof w, "duty at %.0f Hz sampling equals 44.1 kHz's within 0.003 (edges have sub-sample timing)", fs);
		check(w, std::fabs(m.duty - d0) < 0.003);
	}
	// the polyBLEP: aliasing of the saw at about 1.2 kHz with and without it
	auto aliasDb = [&](bool bl) {
		Sc q; q.tune = 3.1;
		std::vector<double> saw, pul;
		Res mr = model(P, q, 48000.0, 0.9, false, &saw, &pul, bl);
		const int N = 16384;
		std::vector<double> x(N);
		size_t o = saw.size() - N - 1;
		for (int i = 0; i < N; i++) x[i] = saw[o + i] * (0.5 - 0.5 * std::cos(2 * M_PI * i / N));
		// DFT by bins (N log N would do; this is a test): power per bin, up to Nyquist
		std::vector<double> P2(N / 2);
		for (int k = 1; k < N / 2; k++) {
			double re = 0, im = 0, w = 2 * M_PI * k / N;
			for (int i = 0; i < N; i++) { re += x[i] * std::cos(w * i); im -= x[i] * std::sin(w * i); }
			P2[k] = re * re + im * im;
		}
		const double f0 = 1.0 / mr.period;               // the model's own frequency, not an FFT peak
		// energy near harmonics (+-6 bins) vs the rest above 2 f0 ... of bins that are not near one
		double harm = 0, other = 0;
		const int kTop = (int) (8000.0 / (48000.0 / N));          // up to 8 kHz: a two-point polyBLEP leaves the octave below Nyquist
		for (int k = 4; k < kTop; k++) {
			double hh = k * 48000.0 / N / f0;
			bool near = std::fabs(hh - std::round(hh)) * f0 / (48000.0 / N) < 6.0;
			(near ? harm : other) += P2[k];
		}
		return 10.0 * std::log10(other / harm);
	};
	double withB = aliasDb(true), withoutB = aliasDb(false);
	printf("    non-harmonic / harmonic energy of the saw at ~1.2 kHz, below 8 kHz: %.1f dB with polyBLEP, %.1f dB without\n", withB, withoutB);
	check("with polyBLEP the saw's non-harmonic energy below 8 kHz is under -48 dB", withB < -48.0);
	check("without it the same test sees at least 25 dB more (the test can see aliasing)", withoutB > withB + 25.0);
	// determinism
	Res a = model(P, s, 48000.0, 0.3), b = model(P, s, 48000.0, 0.3);
	check("deterministic: two identical runs agree exactly", a.period == b.period && a.duty == b.duty);
	// no NaNs or runaway at the extremes
	bool sane = true;
	for (double tune : {-9.0, -6.0, 6.0, 8.0, 10.0}) {
		Sc q; q.tune = tune; q.pot = 0.9; q.pw = true; q.vp = 8.0;
		std::vector<double> saw, pul;
		model(P, q, 48000.0, 0.2, false, &saw, &pul);
		for (size_t i = 0; i < saw.size(); i++)
			if (!std::isfinite(saw[i]) || !std::isfinite(pul[i]) || std::fabs(saw[i]) > 13 || std::fabs(pul[i]) > 13) sane = false;
	}
	{
		Sc q; q.tune = 8.0;
		Res m = model(P, q, 48000.0, 0.2);
		check("pitch is clamped just under Nyquist (a cycle is 2.0 to 2.4 samples at Tune +8 V)", m.ok && m.period * 48000.0 > 2.0 && m.period * 48000.0 < 2.4);
	}
	check("outputs finite and inside +-13 V from Tune -9 V to +10 V (the model never runs faster than it can be sampled)", sane);
}

// ---------------------------------------------------------------------------------------------
// 5. Negative controls: break the model, watch the checks fail
// ---------------------------------------------------------------------------------------------
static void testNegativeControls() {
	printf("negative controls\n");
	Plant& good = plantAt(12.0);
	static Plant bad;
	// 1: the reset leaves C1 100 mV too high
	bad = good;
	for (int k = 0; k < Plant::kRs; k++) bad.rs[k].vc1 += 0.1;
	bad.vc1Ref += 0.1;
	{
		Sc s; s.tune = 0;
		Res o = oracle(s), m = model(bad, s, 96000.0, 0.3);
		check("control 1: C1 left 100 mV high -> the period check fails (>1 %)", relErr(m.period, o.period) > 0.01);
	}
	// 2: D2's reverse leakage forgotten (matters at the lowest pitches)
	bad = good; bad.leak = 0.0;
	{
		Sc s; s.tune = -4;
		Res o = oracle(s), m = model(bad, s, 96000.0, 0.6);
		check("control 2: no D2 leakage -> the 8 Hz period check fails (>1 %)", relErr(m.period, o.period) > 0.01);
	}
	// 3: the Early-effect table flattened (phase = W)
	bad = good;
	for (int k = 0; k < bad.n; k++) { bad.Psi0[k] = bad.W[k] - bad.W[0]; bad.Psi1[k] = 0.0; }
	{
		Sc s; s.tune = 2;
		Res o = oracle(s), m = model(bad, s, 96000.0, 0.3);
		check("control 3: Q2 with no Early effect -> the period check fails (>5 %)", relErr(m.period, o.period) > 0.05);
	}
	// 4: the pulse threshold computed without R15 (the 68k to VCC)
	{
		Sc s; s.tune = 2; s.pot = 0.5;
		Res o = oracle(s);
		PwmFront pf;
		double qe = pf.solve(good.vm, 0.0, false);
		double zthBad = good.vm - R12 * ((kVcc * 0.5 - good.vm) / (R13 + kPwPot * 0.25) + (qe - good.vm) / R17);
		Sc q = s;
		// run the good model but at the wrong threshold by moving the pot to give the same Zth
		double lo = 0, hi = 1;
		for (int i = 0; i < 60; i++) { double m = 0.5 * (lo + hi); if (pulseThreshold(good.vm, m, qe) > zthBad) lo = m; else hi = m; }
		q.pot = 0.5 * (lo + hi);
		Res m = model(good, q, 192000.0, 0.3);
		check("control 4: threshold without R15 -> the duty check fails (> 0.06 off)", std::fabs(m.duty - o.duty) > 0.06);
	}
	// 5: edges without sub-sample timing: blep off is not a timing error, so break the edge time by
	// quantising the period to the sample: run at a rate where one sample is 1/12 of a cycle
	{
		Sc s; s.tune = 4.0;
		Res o = oracle(s), m = model(good, s, 24000.0, 0.3);
		// at 2.1 kHz and 24 kHz there are 11 samples a cycle; a duty read from samples alone is
		// within 1/11 but the model's is exact to the edge: it must still match
		check("control 5 (reference): duty at 11 samples per cycle still within 0.02 of the oracle", std::fabs(m.duty - o.duty) < 0.02);
	}
	// 6: swap the transistors' parameters: the front end no longer matches the netlist
	{
		Front f;
		f.q1 = bc550c(); f.q2 = bc560c();
		double g = 1.0 / R1 + 1.0 / R2 + 1.0 / R3 + 1.0 / R4 + 1.0 / (r21Default() + R6);
		f.solve(0.0, g);
		Net n; n.build(r21Default(), true, true, false, 12.0, 0.5); n.setInputs(0, 0, 0, 0, 0); n.start();
		check("control 6: Q1 and Q2 parameter sets swapped -> QB no longer matches the netlist (> 5 mV)", std::fabs(f.qb - n.c.v[QB]) > 0.005);
	}
}

int main() {
	testPnp();
	testFront();
	testPlant();
	testPeriods();
	testWaveform();
	testAudioRate();
	testNegativeControls();
	printf("\n%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}

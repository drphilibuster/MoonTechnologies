// The hand-eliminated avalanche solver (FastAvalanche) against the generic mna::Circuit it replaces:
// same circuit, same noise, same plasma switching, so the two must stay together sample for sample.
#include "../../src/Kickback/AvalancheNoise.hpp"
#include <chrono>
#include <cstdio>
#include <cmath>
using namespace kickback;

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

int main() {
	const double c5s[2] = { 0.1e-6, 1e-9 };
	const AvalancheNoise::Variant vars[2] = { AvalancheNoise::PERCUSSIVE, AvalancheNoise::DAZZLER };
	for (int vi = 0; vi < 2; vi++) for (int ci = 0; ci < 2; ci++) {
		AvalancheNoise a(vars[vi], c5s[ci]), b(vars[vi], c5s[ci]);
		a.seed(77); b.seed(77);
		a.fast = true; b.fast = false;
		a.start(192000.0); b.start(192000.0);
		double worst = 0.0, scale = 0.0; int on = 0;
		for (int i = 0; i < 192000 * 2; i++) {
			double ya = a.step(), yb = b.step();
			worst = std::fmax(worst, std::fabs(ya - yb));
			scale = std::fmax(scale, std::fabs(yb - b.ckt.v[AvalancheNoise::B1] + 0));
			for (int k = 0; k < AvalancheNoise::kPlasmas; k++) on += a.sw[k].on;
		}
		// the output is volts at B1 with ~mV noise on it; agreement must be far below that
		printf("variant %d C5 %.0e: worst |fast - generic| = %.3g V, plasma-on samples %d, ok %d/%d\n",
		       vi, c5s[ci], worst, on, (int)a.ok, (int)b.ok);
		CHECK(worst < 1e-6, "fast and generic solvers diverged (%.3g V)", worst);
		CHECK(a.ok && b.ok, "a solver failed to converge");
		CHECK(on > 0, "no plasma ever switched on: the test is not exercising them");
	}
	// speed
	for (int f = 1; f >= 0; f--) {
		AvalancheSource s; s.setRate(48000);
		for (int k = 0; k < 1; k++) s.ckt.fast = f;
		auto t0 = std::chrono::steady_clock::now(); double acc = 0;
		for (int i = 0; i < 48000 * 2; i++) acc += s.next();
		double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count() / (48000 * 2);
		printf("%s: %.0f ns per audio sample (%g)\n", f ? "fast   " : "generic", ns, acc);
	}
	if (fails) { printf("%d FAILED\n", fails); return 1; }
	printf("ALL PASSED\n"); return 0;
}

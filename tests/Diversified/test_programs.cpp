// Every Diversified program, driven to its corners, must stay finite.
//
// This test exists because of a bug in Gross: Rack default-constructs a
// BiquadFilter by calling setParameters(LOWPASS, f=0, Q=0, V=1), whose branch
// divides K by Q -- 0/0 -- so a filter that is processed before it is designed
// has NaN coefficients. NaN then lodges in the filter's state history and never
// leaves, and because clamp() returns its upper bound for NaN the module sits
// at full scale for ever. Nothing in the build catches that: it compiles, it
// runs, and it is only audible as a module that has "always sounded broken".
//
// Diversified is where that class of bug is easiest to hide -- 106 programs
// across eight algorithms, most of them with feedback, and only whichever one
// the knob happens to be on ever runs. So: run all of them, at the macro
// settings most likely to blow a feedback path up, through signals designed to
// excite one (DC steps, full-scale impulses, sustained loud tone), and check
// every single output sample.
//
// A "loud" program is not a failure. Only NaN, infinity, and a magnitude no
// sane effect reaches are.

#include "../../src/Diversified/Dsp99.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

using divfx::Patch;
using divfx::BankCtx;
using divfx::MiawCtx;
using divfx::NUM_PROGRAMS;

static int checks = 0;
static int failures = 0;

/** Volts. Rack's convention is +-5 V audio and +-12 V at the rails; a module
    that has lost control of a feedback path leaves this far behind, while one
    that is merely loud does not. */
static const float SANE_VOLTS = 200.f;

/** One program, one macro setting, one sample rate. Returns false on the first
    bad sample so the report names where it went wrong rather than repeating. */
static bool runProgram(int prog, float macro, float sr, const char*& why,
                       int& badSample, float& badValue) {
	Patch p;
	divfx::programAt(prog, p);
	for (int i = 0; i < 3; i++)
		p.setMacro(i, macro);

	divfx::Dsp99 bank;
	divfx::MiawRack miaw;
	MiawCtx mctx;

	bank.init();
	miaw.init();
	bank.setSampleRate(sr);
	miaw.setSampleRate(sr);

	BankCtx bc;
	bc.sr = sr;
	bc.sampleTime = 1.f / sr;
	MiawCtx tmpl;
	tmpl.sr = sr;

	// Voice::prepare(): zero only what this program will use.
	const bool isMiaw = (p.algA == divfx::A_MIAW);
	if (isMiaw) {
		miaw.clear(p.chrA);
	}
	else {
		bank.clear(p.algA);
		if (p.algB != divfx::A_NONE)
			bank.clear(p.algB);
	}

	// Voice::setParams().
	if (isMiaw) {
		mctx = tmpl;
		for (int i = 0; i < 3; i++)
			mctx.p[i] = p.a[i];
		miaw.setParams(p.chrA, mctx);
	}
	else {
		bank.setParams(p, bc);
	}

	// Two seconds: long enough for a reverb tail to build and for a delay at
	// the top of its range to wrap several times.
	const int n = (int) (sr * 2.f);
	for (int i = 0; i < n; i++) {
		const float t = (float) i / sr;

		// A deliberately hostile input. Silence first, so a program that rings
		// on its own is caught with nothing to blame it on; then full-scale
		// steps and impulses, which is what excites a feedback path; then a
		// sustained loud tone.
		float in;
		if (t < 0.2f)
			in = 0.f;
		else if (t < 0.5f)
			in = (i % 2048 == 0) ? 5.f : 0.f;            // impulse train
		else if (t < 0.9f)
			in = ((i / 512) % 2) ? 5.f : -5.f;           // DC steps
		else
			in = 5.f * std::sin(2.f * (float) M_PI * 220.f * t);

		float outL = 0.f, outR = 0.f;
		if (isMiaw) {
			mctx.aux = tmpl.aux;
			mctx.auxConnected = tmpl.auxConnected;
			mctx.auxGate = tmpl.auxGate;
			mctx.ret = tmpl.ret;
			mctx.retConnected = tmpl.retConnected;
			mctx.inRConnected = tmpl.inRConnected;
			mctx.sr = tmpl.sr;
			miaw.process(p.chrA, mctx, in, in, outL, outR);
		}
		else {
			bank.process(p, bc, in, in, outL, outR);
		}

		const float outs[2] = { outL, outR };
		for (int c = 0; c < 2; c++) {
			if (std::isnan(outs[c])) {
				why = "NaN";
				badSample = i;
				badValue = outs[c];
				return false;
			}
			if (std::isinf(outs[c])) {
				why = "infinity";
				badSample = i;
				badValue = outs[c];
				return false;
			}
			if (std::fabs(outs[c]) > SANE_VOLTS) {
				why = "runaway";
				badSample = i;
				badValue = outs[c];
				return false;
			}
		}
	}
	return true;
}

int main() {
	// 44.1k and 96k: coefficients that are only ever computed from the sample
	// rate are exactly the ones a single-rate test would never disagree about.
	const float rates[] = { 44100.f, 96000.f };
	const float macros[] = { 0.f, 0.5f, 1.f };

	printf("Diversified: %d programs x %d macro settings x %d sample rates\n",
	       NUM_PROGRAMS, (int) (sizeof(macros) / sizeof(macros[0])),
	       (int) (sizeof(rates) / sizeof(rates[0])));

	// One check for the sweep, not one per combination. The sweep is the same
	// size it always was -- every program, every macro setting, every rate --
	// but "636 checks" was six hundred and thirty-six reports of a single
	// property, which tells you nothing that "the sweep is clean" does not.
	// Each failure still names its own program, and the first ten print in full
	// so a real fault is diagnosable rather than a wall of six hundred lines.
	int bad = 0;
	int total = 0;
	for (unsigned r = 0; r < sizeof(rates) / sizeof(rates[0]); r++) {
		for (unsigned m = 0; m < sizeof(macros) / sizeof(macros[0]); m++) {
			for (int prog = 0; prog < NUM_PROGRAMS; prog++) {
				const char* why = "";
				int badSample = -1;
				float badValue = 0.f;
				total++;
				if (!runProgram(prog, macros[m], rates[r], why, badSample, badValue)) {
					if (bad < 10) {
						Patch p;
						divfx::programAt(prog, p);
						printf("    program %3d %-13s  macros=%.1f  sr=%.0f  "
						       "%s at sample %d (%g)\n",
						       prog, p.name, macros[m], rates[r], why, badSample,
						       badValue);
					}
					bad++;
				}
			}
		}
	}
	checks++;
	if (bad) {
		failures++;
		printf("  FAIL  every program stays finite: %d of %d combinations produced"
		       " NaN or ran away\n", bad, total);
	}

	printf("\n%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}

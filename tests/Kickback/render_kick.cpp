// Renders Kickback's kick models to WAV and prints what they do to pitch.
//
// This is an audition and measurement tool, not a test: nothing here passes or
// fails. It exists so a kick can be heard and measured before and after a change.
// Voices.hpp is pure DSP, so it compiles against the host toolchain as it stands.
//
//   render_kick [outdir] [tune decay bend]     (knobs 0..1; defaults 0.40 0.50 0.60)
//
// For each model it writes <outdir>/kick_<model>_v<vel>.wav (mono in both channels,
// 48 kHz, 1.5 s) and prints the instantaneous frequency from zero-crossing
// intervals of the first 200 ms, the level at fixed times, and the peak.
#include "../../src/Kickback/Voices.hpp"
#include "../Retroactive/wav.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace kickback;

static const char* kModel[] = { "bridge", "smurf", "sweep" };
static const int kModels = 3;

int main(int argc, char** argv) {
	std::string outdir = argc > 1 ? argv[1] : ".";
	float tune = argc > 4 ? (float)atof(argv[2]) : 0.40f;
	float decay = argc > 4 ? (float)atof(argv[3]) : 0.50f;
	float bend = argc > 4 ? (float)atof(argv[4]) : 0.60f;
	// Kickback.cpp derives DRIVE from BEND for the kick.
	float colour = 0.25f + 0.5f * bend;

	const float fs = 48000.f;
	const int n = (int)(1.5f * fs);
	float f0 = expMap(tune, 32.f, 190.f);
	printf("tune %.2f -> %.1f Hz, decay %.2f, bend %.2f, drive %.2f\n", tune, f0, decay, bend, colour);

	for (int m = 0; m < kModels; m++) {
		for (int vi = 0; vi < 2; vi++) {
			float vel = vi == 0 ? 1.0f : 0.4f;
			Kick k;
			k.setRate(fs);
			k.reset();
			std::vector<float> y(n);
			for (int i = 0; i < n; i++)
				y[i] = k.process(i == 0, vel, m, tune, 0.f, decay, bend, colour) / 5.f;

			char name[256];
			snprintf(name, sizeof name, "%s/kick_%s_v%d.wav", outdir.c_str(), kModel[m], (int)(vel * 10));
			wav::writeStereo16(name, y, y, (int)fs);

			float peak = 0.f;
			for (int i = 0; i < n; i++) peak = std::fmax(peak, std::fabs(y[i]));
			printf("\n== %s, vel %.1f  (peak %.3f of full scale) -> %s\n", kModel[m], vel, peak, name);

			// Level (rms over 5 ms) at fixed times.
			printf("  rms dBFS:");
			const float t_ms[] = { 2, 5, 10, 25, 50, 100, 200, 400, 800 };
			for (float t : t_ms) {
				int a = (int)(t * 0.001f * fs), b = a + (int)(0.005f * fs);
				double s = 0; for (int i = a; i < b && i < n; i++) s += (double)y[i] * y[i];
				printf(" %gms:%.0f", t, 10.0 * std::log10(s / (b - a) + 1e-12));
			}
			printf("\n");

			// Frequency from zero crossings of a ~400 Hz-lowpassed copy (the click and
			// attack noise would otherwise cross constantly), first 200 ms.
			OnePole p1, p2;
			float G = poleG(400.f, fs);
			std::vector<float> lo(n);
			for (int i = 0; i < n; i++) lo[i] = p2.lp(p1.lp(y[i], G), G);
			printf("  zero-crossing frequency (Hz) at time (ms):\n   ");
			float lastPos = -1.f, prev = lo[0];
			int shown = 0;
			for (int i = 1; i < (int)(0.2f * fs); i++) {
				if ((prev < 0.f) != (lo[i] < 0.f)) {
					float pos = (i - 1) + prev / (prev - lo[i]);   // interpolated crossing
					if (lastPos >= 0.f && pos > lastPos) {
						printf(" %.1f:%.0f", pos / fs * 1000.f, 0.5f * fs / (pos - lastPos));
						if (++shown % 8 == 0) printf("\n   ");
					}
					lastPos = pos;
				}
				prev = lo[i];
			}
			printf("\n");
		}
	}
	return 0;
}

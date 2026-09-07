// Collusion's coupled-oscillator engine.
//
// Three things here are worth a test and would not otherwise be caught.
//
//   The phase transition. It is the module: COUPLING does nothing at all below
//   a threshold and everything above it, and the threshold is set by SPREAD.
//   If the coupling term ever loses a sign or a factor of N, the module still
//   builds, still makes sound, and simply stops being the thing it is. Nothing
//   but a measurement of the order parameter notices.
//
//   Staying finite. The population is a feedback loop with a user-controlled
//   gain, and LEVERAGE closes a second loop through the shift register on top
//   of it -- the ledger detunes the swarm, the swarm writes the ledger. One
//   inf that escapes poisons every phase on the next sample and the module is
//   silent for the rest of the session.
//
//   The fast sine. The coupling costs around forty sines a sample, so it uses
//   an approximation; if that is wrong anywhere in its range the coupling aims
//   somewhere other than where it says it does, and the failure looks like a
//   design choice rather than a bug.

#include <cmath>
#include <cstdio>

#include "../../src/Collusion/Swarm.hpp"

using namespace collusion;

static int checks = 0;
static int failures = 0;

static void check(bool ok, const char* what) {
	checks++;
	if (!ok) {
		failures++;
		printf("  FAIL  %s\n", what);
	}
}

/** A fixed pseudo-random sequence, so DEAL is reproducible here. */
struct Lcg {
	unsigned long s = 12345u;
	float operator()() {
		s = s * 1103515245u + 12345u;
		return (float) ((s >> 16) & 0x7FFFu) / 32768.f;
	}
};

// --- T1: the fast sine -------------------------------------------------------

static void t1_trig() {
	const float TAU = 6.283185307179586f;
	float worst = 0.f;
	// Well outside [0, 1): phases run negative under repulsive coupling, and
	// cos2pi() is sin2pi() of an argument a quarter turn past the end.
	for (int i = -5000; i <= 5000; i++) {
		const float x = (float) i * 0.0017f;
		worst = std::fmax(worst, std::fabs(sin2pi(x) - std::sin(TAU * x)));
		worst = std::fmax(worst, std::fabs(cos2pi(x) - std::cos(TAU * x)));
	}
	printf("T1  sin2pi/cos2pi worst error %.2e over x in [-8.5, 8.5]\n", worst);
	check(worst < 1.2e-3f, "fast trig within 1.2e-3 of std::sin/std::cos");
	check(std::fabs(sin2pi(0.f)) < 1e-6f, "sin2pi(0) == 0");
	check(std::fabs(sin2pi(0.25f) - 1.f) < 1e-6f, "sin2pi(1/4) == 1");
	check(std::fabs(cos2pi(0.f) - 1.f) < 1e-6f, "cos2pi(0) == 1");
}

// --- T2: the phase transition ------------------------------------------------

/** Mean order parameter over the back half of a run, which is where the
    transient from the initial scatter has died away. */
static float meanOrder(float spreadOct, float coupling, int scheme,
                       float baseHz = 2.f, float seconds = 12.f,
                       float evasion = 0.f) {
	const float sr = 44100.f;
	Swarm s;
	s.fan();
	s.scatter();

	Controls c;
	c.baseFreq = baseHz;
	c.spread = spreadOct;
	c.shape = 0.f;
	c.coupling = coupling;
	c.evasion = evasion;
	c.scheme = scheme;
	c.leverage = 0.f;
	c.ledger = 0.f;
	c.sampleTime = 1.f / sr;
	c.sampleRate = sr;

	const int n = (int) (sr * seconds);
	double sum = 0.0;
	int counted = 0;
	for (int i = 0; i < n; i++) {
		s.process(c);
		if (i >= n / 2) {
			sum += s.order;
			counted++;
		}
	}
	return (float) (sum / (double) counted);
}

static void t2_transition() {
	// Kuramoto's estimate for a uniform spread of natural frequencies is
	// Kc = 4 * half-width / pi, which for a half-width of half an octave is
	// about 0.45. Six oscillators smear the transition, so the check is that
	// well below is incoherent and well above is locked -- not where the knee is.
	const float spread = 0.5f;
	const float free = meanOrder(spread, 0.f, SCHEME_CARTEL);
	const float weak = meanOrder(spread, 0.2f, SCHEME_CARTEL);
	const float strong = meanOrder(spread, 2.0f, SCHEME_CARTEL);
	const float repel = meanOrder(spread, -2.0f, SCHEME_CARTEL);
	printf("T2  cartel, spread 0.5 oct:  K=0 r=%.3f  K=0.2 r=%.3f  "
	       "K=2 r=%.3f  K=-2 r=%.3f\n", free, weak, strong, repel);

	check(free < 0.7f, "uncoupled population is not locked");
	check(strong > 0.9f, "strong coupling locks the population");
	check(strong > free + 0.25f, "coupling raises the order parameter");
	check(repel < free + 0.05f, "negative coupling does not lock the population");

	// SPREAD is the other half of the phase diagram: the same coupling that
	// locks a narrow fan must fail to lock a wide one. This is the property
	// that makes the two knobs a phase diagram rather than two gain controls.
	const float narrowLocked = meanOrder(0.15f, 0.5f, SCHEME_CARTEL);
	const float wideLoose = meanOrder(1.5f, 0.5f, SCHEME_CARTEL);
	printf("T2  K=0.5:  spread 0.15 oct r=%.3f   spread 1.5 oct r=%.3f\n",
	       narrowLocked, wideLoose);
	check(narrowLocked > 0.9f, "a narrow fan locks at moderate coupling");
	check(wideLoose < narrowLocked - 0.2f, "a wide fan does not");

	// Pulse coupling has its own threshold but the same direction of effect.
	const float fireFree = meanOrder(spread, 0.f, SCHEME_FIREFLY);
	const float fireLock = meanOrder(spread, 2.0f, SCHEME_FIREFLY);
	printf("T2  firefly, spread 0.5 oct: K=0 r=%.3f  K=2 r=%.3f\n", fireFree, fireLock);
	check(fireLock > fireFree + 0.2f, "pulse coupling synchronises too");

	// The pyramid's head hears nobody, so it is a plain LFO no matter what
	// COUPLING is doing to the five below it.
	{
		const float sr = 44100.f;
		Swarm a, b;
		a.fan(); a.scatter();
		b.fan(); b.scatter();
		Controls c;
		c.baseFreq = 3.f; c.spread = 0.5f; c.scheme = SCHEME_PYRAMID;
		c.sampleTime = 1.f / sr; c.sampleRate = sr;
		Controls d = c;
		c.coupling = 0.f;
		d.coupling = 2.0f;
		float worst = 0.f;
		for (int i = 0; i < (int) (sr * 4.f); i++) {
			a.process(c);
			b.process(d);
			worst = std::fmax(worst, std::fabs(a.phase[0] - b.phase[0]));
		}
		printf("T2  pyramid head drift between K=0 and K=2: %.2e turns\n", worst);
		check(worst < 1e-4f, "the pyramid's head is unaffected by coupling");
	}
}

// --- T2b: the gate output ----------------------------------------------------

/** Edges per second on PULSE. */
static float pulseRate(float coupling, float seconds = 8.f) {
	const float sr = 44100.f;
	Swarm s;
	s.fan();
	s.scatter();
	Controls c;
	c.baseFreq = 4.f;
	c.spread = 0.5f;
	c.coupling = coupling;
	c.scheme = SCHEME_CARTEL;
	c.sampleTime = 1.f / sr;
	c.sampleRate = sr;

	const int n = (int) (sr * seconds);
	bool prev = false;
	int edges = 0;
	for (int i = 0; i < n; i++) {
		s.process(c);
		if (i >= n / 2) {                 // count only after the swarm has settled
			if (s.pulse != prev)
				edges++;
			prev = s.pulse;
		}
		else {
			prev = s.pulse;
		}
	}
	return (float) edges / (seconds * 0.5f);
}

static void t2b_pulse() {
	// PULSE is a gate per fire, held for a twentieth of a cycle. Six filers at
	// 4 Hz spread around an incoherent cycle give six gates a cycle -- 48 edges
	// a second -- and the same six drawn together by the coupling merge into
	// one, at 8. That collapse is the whole output, and it is the reason PULSE
	// is not the XOR of the six comparators: "locked" in Kuramoto's sense means
	// a *constant phase offset*, not an identical phase, so the comparators go
	// on chattering at exactly the same rate on both sides of the transition
	// and a parity output cannot tell you which side you are on.
	const float locked = pulseRate(2.0f);
	const float loose = pulseRate(0.f);
	printf("T2b PULSE edges/s at 4 Hz:  locked %.1f  incoherent %.1f\n", locked, loose);
	check(locked > 6.f && locked < 12.f, "a locked swarm fires as one, about once a cycle");
	check(loose > locked * 2.5f, "an incoherent swarm fires six times over");
}

// --- T3: staying finite ------------------------------------------------------

static void t3_corners() {
	const float rates[] = {44100.f, 48000.f, 96000.f};
	const float freqs[] = {0.02f, 1.f, 40.f, 400.f, 4000.f, 20000.f};
	const float spreads[] = {0.f, 0.5f, 1.5f};
	const float couplings[] = {-2.6f, -1.f, 0.f, 1.f, 2.6f};
	const float shapes[] = {0.f, 0.5f, 1.f};
	const float evasions[] = {0.f, 0.5f, 1.f};

	bool ok = true;
	for (unsigned r = 0; r < sizeof(rates) / sizeof(*rates) && ok; r++) {
		for (int scheme = 0; scheme < NUM_SCHEMES && ok; scheme++) {
			for (unsigned f = 0; f < sizeof(freqs) / sizeof(*freqs) && ok; f++) {
				for (unsigned sp = 0; sp < sizeof(spreads) / sizeof(*spreads) && ok; sp++) {
					for (unsigned k = 0; k < sizeof(couplings) / sizeof(*couplings) && ok; k++) {
						for (unsigned sh = 0; sh < sizeof(shapes) / sizeof(*shapes) && ok; sh++) {
							for (unsigned e = 0; e < sizeof(evasions) / sizeof(*evasions) && ok; e++) {
								Swarm s;
								Lcg rng;
								s.deal(rng);
								s.scatter();
								Controls c;
								c.baseFreq = freqs[f];
								c.spread = spreads[sp];
								c.shape = shapes[sh];
								c.coupling = couplings[k];
								c.evasion = evasions[e];
								c.scheme = scheme;
								c.sampleTime = 1.f / rates[r];
								c.sampleRate = rates[r];
								for (int i = 0; i < 4000 && ok; i++) {
									s.process(c);
									for (int j = 0; j < N_FILERS; j++) {
										if (!std::isfinite(s.wave[j]) || std::fabs(s.wave[j]) > 1.f
										    || !(s.phase[j] >= 0.f && s.phase[j] < 1.f)) {
											printf("  FAIL  scheme %d sr=%.0f f=%g spread=%g "
											       "K=%g shape=%g alpha=%g: filer %d wave=%g phase=%g\n",
											       scheme, rates[r], freqs[f], spreads[sp],
											       couplings[k], shapes[sh], evasions[e],
											       j, s.wave[j], s.phase[j]);
											ok = false;
											break;
										}
									}
									if (!std::isfinite(s.order) || s.order < 0.f || s.order > 1.f
									    || !std::isfinite(s.consensus)) {
										printf("  FAIL  collective outputs: r=%g consensus=%g\n",
										       s.order, s.consensus);
										ok = false;
									}
								}
							}
						}
					}
				}
			}
		}
	}
	checks++;
	if (!ok)
		failures++;
	printf("T3  1620 corners x 4000 samples: phases in [0,1), waves in [-1,1]%s\n",
	       ok ? "" : "  -- FAILED");
}

/** The closed loop: the ledger detunes the swarm through LEVERAGE while the
    swarm writes the ledger through its own consensus. The two feed each other
    with nothing in between, which is the Benjolin's runaway path and the one
    place in the module where a small error compounds instead of decaying. */
static void t3_feedback() {
	const float sr = 48000.f;
	bool ok = true;
	for (int scheme = 0; scheme < NUM_SCHEMES && ok; scheme++) {
		for (int sign = -1; sign <= 1 && ok; sign += 2) {
			Swarm s;
			Ledger led;
			Lcg rng;
			s.deal(rng);
			s.scatter();
			led.setLength(8);
			Controls c;
			c.baseFreq = 6.f;
			c.spread = 0.8f;
			c.shape = 1.f;
			c.coupling = 1.6f * (float) sign;
			c.evasion = 0.3f;
			c.scheme = scheme;
			c.leverage = 1.5f * (float) sign;      // full LEVERAGE, both polarities
			c.sampleTime = 1.f / sr;
			c.sampleRate = sr;
			for (int i = 0; i < (int) (sr * 20.f) && ok; i++) {
				c.ledger = led.value();
				s.process(c);
				if (s.fired[0])
					led.clock(s.consensus > 0.f, rng() < 0.5f);
				for (int j = 0; j < N_FILERS; j++) {
					if (!std::isfinite(s.wave[j]) || std::fabs(s.wave[j]) > 1.f
					    || !std::isfinite(s.freq[j])) {
						printf("  FAIL  scheme %d leverage %+.1f: filer %d wave=%g freq=%g "
						       "at sample %d\n", scheme, c.leverage, j, s.wave[j], s.freq[j], i);
						ok = false;
						break;
					}
				}
			}
		}
	}
	checks++;
	if (!ok)
		failures++;
	printf("T3  20 s of ledger/swarm feedback at full leverage, every scheme%s\n",
	       ok ? "" : "  -- FAILED");
}

// --- T4: the shape ceiling ---------------------------------------------------

/** The largest step the waveform takes in one sample, over a whole cycle.
    A crude bandwidth proxy, but the right one: what the ceiling exists to stop
    is the phase warp turning into a discontinuity as the swarm is swept up into
    the audio band. */
static float maxSlew(float freq, float sr, float m) {
	const float dt = freq / sr;
	float worst = 0.f, prev = shapedWave(0.f, m);
	for (float p = dt; p < 1.f; p += dt) {
		const float v = shapedWave(p, m);
		worst = std::fmax(worst, std::fabs(v - prev));
		prev = v;
	}
	return worst;
}

static void t4_ceiling() {
	const float sr = 44100.f;
	check(shapeCeiling(0.5f, sr) > 0.95f, "the ceiling is out of the way at LFO rates");
	check(shapeCeiling(20000.f, sr) < 0.2f, "and closes down near Nyquist");

	float prev = 2.f;
	bool monotone = true;
	for (float f = 1.f; f < 20000.f; f *= 1.3f) {
		const float v = shapeCeiling(f, sr);
		if (v > prev + 1e-6f)
			monotone = false;
		prev = v;
	}
	check(monotone, "the ceiling falls monotonically with frequency");

	// What the ceiling is for is the *excess* over a plain sine: at 4 kHz on a
	// 44.1 k rate the fundamental alone already moves a fifth of a turn a
	// sample, and that part is not the shape control's doing. The excess is.
	const float f = 4000.f;
	const float plain = maxSlew(f, sr, 0.f);
	const float capped = maxSlew(f, sr, 1.f * shapeCeiling(f, sr));
	const float uncapped = maxSlew(f, sr, 0.98f);
	printf("T4  4 kHz at 44.1 k, max per-sample step:  sine %.3f  capped %.3f  uncapped %.3f "
	       "(excess %.3f vs %.3f)\n", plain, capped, uncapped, capped - plain, uncapped - plain);
	check(capped - plain < (uncapped - plain) * 0.5f,
	      "the ceiling halves the excess slew the shape adds up there");
	check(capped >= plain, "and does not soften it below a plain sine");

	// Down where an LFO lives the knob is not touched at all.
	check(std::fabs(shapeCeiling(2.f, sr) - 0.98f) < 1e-6f, "no ceiling at 2 Hz");
}

// --- T5: the ledger ----------------------------------------------------------

static void t5_ledger() {
	// Sealed: never rewritten, so the register is a loop and the sequence has
	// to repeat with exactly the period TERM says it does.
	for (int t = 0; t < Ledger::NUM_TERMS; t++) {
		const int len = Ledger::termLength(t);
		Ledger led;
		led.setLength(len);
		led.setWidth(3);
		float first[16];
		for (int i = 0; i < len; i++) {
			led.clock(true, false);            // dataBit ignored when sealed
			first[i] = led.value();
		}
		bool same = true;
		for (int rep = 0; rep < 3 && same; rep++) {
			for (int i = 0; i < len; i++) {
				led.clock(false, false);
				if (led.value() != first[i])
					same = false;
			}
		}
		if (!same)
			printf("  FAIL  sealed ledger of %d does not repeat\n", len);
		check(same, "a sealed ledger repeats with its own term");
	}

	// Fully audited: every step written from the data bit, so a constant data
	// bit fills the register and pins the output at an end stop.
	{
		Ledger led;
		led.setLength(8);
		led.setWidth(3);
		for (int i = 0; i < 32; i++)
			led.clock(true, true);
		check(std::fabs(led.value() - 1.f) < 1e-6f, "all ones reads full scale");
		for (int i = 0; i < 32; i++)
			led.clock(false, true);
		check(std::fabs(led.value() + 1.f) < 1e-6f, "all zeros reads minus full scale");
	}

	// Range and clamping, across every term and every ladder width the menu offers.
	{
		bool ok = true;
		Lcg rng;
		const int widths[] = {1, 3, 5, 8, 99};
		for (int t = 0; t < Ledger::NUM_TERMS; t++) {
			for (unsigned w = 0; w < sizeof(widths) / sizeof(*widths); w++) {
				Ledger led;
				led.setLength(Ledger::termLength(t));
				led.setWidth(widths[w]);
				for (int i = 0; i < 500; i++) {
					led.clock(rng() > 0.5f, rng() < 0.4f);
					const float v = led.value();
					if (!std::isfinite(v) || v < -1.f || v > 1.f) {
						printf("  FAIL  term %d width %d: value %g\n",
						       Ledger::termLength(t), widths[w], v);
						ok = false;
					}
				}
			}
		}
		check(ok, "the ledger reads within [-1, 1] at every term and width");
		Ledger led;
		led.setLength(1);  check(led.length == 2, "term clamps up to 2");
		led.setLength(99); check(led.length == 16, "term clamps down to 16");
		led.setWidth(0);   check(led.width == 1, "width clamps up to 1");
		led.setWidth(99);  check(led.width == 8, "width clamps down to 8");
	}
}

// --- T6: sync and scatter ----------------------------------------------------

static void t6_reset() {
	Swarm s;
	s.scatter();
	check(s.order < 1e-3f, "a scattered population is maximally incoherent");
	s.align();
	check(std::fabs(s.order - 1.f) < 1e-6f, "SYNC puts the population in unison");
	for (int i = 0; i < N_FILERS; i++)
		check(s.phase[i] == 0.f, "SYNC aligns every filer to phase zero");

	// The fan is the module's own state, and DEAL has to leave it usable.
	Lcg rng;
	s.deal(rng);
	bool inRange = true, varied = false;
	for (int i = 0; i < N_FILERS; i++) {
		inRange = inRange && s.detune[i] >= -1.f && s.detune[i] <= 1.f;
		varied = varied || std::fabs(s.detune[i] - s.detune[0]) > 1e-3f;
	}
	check(inRange, "a dealt fan stays in [-1, 1]");
	check(varied, "a dealt fan is not six identical rates");
	s.fan();
	check(std::fabs(s.detune[0] + 1.f) < 1e-6f
	   && std::fabs(s.detune[N_FILERS - 1] - 1.f) < 1e-6f,
	      "the even fan spans the whole spread");
}


int main() {
	t1_trig();
	t2_transition();
	t2b_pulse();
	t3_corners();
	t3_feedback();
	t4_ceiling();
	t5_ledger();
	t6_reset();
	printf("\n%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}

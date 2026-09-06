#pragma once
/** The seven dedicated Modular in a Week circuits -- Diversified's programs
 * 99 to 105.
 *
 * Unlike the DSP99 bank next door, which is a family of general algorithms
 * mapped onto a numbered list, each of these models one board Kristian Blasol
 * published for the Modular in a Week series (and, for 102, the MXR pedal that
 * board copies). They are modelled from the topology rather than from a
 * description of the sound: the PT2399's converter clock really does slow down
 * as the delay lengthens, the 4011's four NAND gates really are wired as the
 * canonical exclusive-or, the ADC0809 bit-swapper really does reverse an eight
 * bit word before the R-2R ladder sees it.
 *
 * Everything here is mono-in unless the circuit had two inputs. Voltages are
 * Rack's: audio at +-5 V, gates 0/10 V.
 */
#include "Primitives.hpp"

namespace divfx {

/** The dedicated circuits, in program order from 99. */
enum MiawId {
	MW_ECHOMATIC,     // 99  Echomatic PT2399 echo
	MW_ANGEL,         // 100 Little Angel PT2399 chorus
	MW_SPRING,        // 101 spring tank + driver
	MW_DISTPLUS,      // 102 MXR Distortion+
	MW_TALKFUNNY,     // 103 Talk Funny
	MW_BITCRUSH,      // 104 MW Bitcrusher / bit swapper
	MW_RING4011,      // 105 4011 ring modulator
	MW_COUNT
};

/** Everything a circuit needs from the panel and the patch, and the one thing
    it hands back (SEND). Built fresh by the module each sample; the coefficient
    work keyed off it happens at control rate. */
struct MiawCtx {
	float sr;
	float p[3];          // the three macros, 0..1, CV already folded in
	float aux;           // AUX input, volts
	bool auxConnected;
	bool auxGate;        // AUX read as a gate
	float tapSec;        // clocked delay time, or 0
	float ret;           // RETURN input, volts
	bool retConnected;
	bool inRConnected;   // IN R really patched, not normalled
	bool crushSwap;      // 104: reverse the eight bit word
	bool crushLpf;       // 104: reconstruction filter in circuit
	bool ringSmooth;     // 105: analogue product instead of the gate array
	float send;          // written by the circuit

	MiawCtx() : sr(44100.f), aux(0.f), auxConnected(false), auxGate(false),
	            tapSec(0.f), ret(0.f), retConnected(false), inRConnected(false),
	            crushSwap(false), crushLpf(true), ringSmooth(false), send(0.f) {
		p[0] = p[1] = p[2] = 0.5f;
	}
};

/** Band-limiting residual for a hard-edged oscillator. */
inline float polyBlep(float t, float dt) {
	if (dt <= 0.f)
		return 0.f;
	if (t < dt) {
		t /= dt;
		return t + t - t * t - 1.f;
	}
	if (t > 1.f - dt) {
		t = (t - 1.f) / dt;
		return t * t + t + t + 1.f;
	}
	return 0.f;
}

// ---------------------------------------------------------------------------
// 99  Echomatic -- PT2399 echo, 30 ms to 1 s
//
// The whole character of a PT2399 is that it is not a delay line, it is a
// converter whose clock is the delay control. Longer delay means a slower
// internal rate, which means less bandwidth, coarser quantisation and more of
// the chip's own noise -- so a long repeat is dark and grainy and a short one is
// nearly clean. That is modelled here rather than faked with a fixed lowpass:
// one clock, one sample-and-hold, and an anti-alias pair that tracks it.
//
// The TO FX / FROM FX jacks are the board's insert, and they sit inside the
// feedback path, which is why a distortion patched into them gets dirtier with
// every repeat rather than once.

struct Echomatic {
	DelayLine line;
	OnePole preLp, postLp, fbLp;
	DCBlocker dc;
	Smooth timeSm;
	Rng rng;

	float phase, held, fbState;
	// control rate
	float delaySamples, fb, level, quantStep, clockInc, noiseAmp;

	Echomatic() : phase(0.f), held(0.f), fbState(0.f), delaySamples(1000.f),
	              fb(0.3f), level(0.8f), quantStep(0.01f), clockInc(1.f),
	              noiseAmp(0.f) {}

	void init() { line.init((int) (1.05f * MAX_SR)); }

	void clear() {
		line.clear();
		preLp.clear(); postLp.clear(); fbLp.clear();
		dc.clear();
		phase = held = fbState = 0.f;
		timeSm.snap(delaySamples);
	}

	void setSampleRate(float sr) {
		dc.setSampleRate(sr);
		timeSm.setTime(60.f, sr);
	}

	void setParams(const MiawCtx& c) {
		// 30 ms .. 1 s, log. A locked TAP clock takes the knob's place.
		float t = 0.03f * std::pow(1.f / 0.03f, clamp(c.p[0], 0.f, 1.f));
		if (c.tapSec > 0.f)
			t = clamp(c.tapSec, 0.03f, 1.f);
		delaySamples = clamp(t * c.sr, 4.f, line.maxDelay());

		// The converter clock. 30 ms is nearly full bandwidth; 1 s is the mush
		// the board is loved for.
		float fi = clamp(40000.f * 0.03f / t, 3000.f, 40000.f);
		clockInc = std::min(fi / c.sr, 1.f);
		float bw = std::min(0.42f * fi, c.sr * 0.45f);
		preLp.setCutoff(bw, c.sr);
		postLp.setCutoff(bw, c.sr);
		fbLp.setCutoff(std::min(bw, 4500.f), c.sr);

		// Ten bits at the short end, seven at the long one, over +-5 V.
		float bits = lerp(10.f, 7.f, clamp(c.p[0], 0.f, 1.f));
		quantStep = 10.f / std::pow(2.f, bits);
		noiseAmp = quantStep * 0.35f;

		fb = clamp(c.p[1], 0.f, 1.f) * 1.25f;   // past unity on purpose
		level = clamp(c.p[2], 0.f, 1.f);
	}

	void process(MiawCtx& c, float in, float& out) {
		float x = in + fbState;
		x = clamp(x, -12.f, 12.f);
		float v = preLp.lp(x);

		phase += clockInc;
		if (phase >= 1.f) {
			phase -= 1.f;
			float q = std::floor(v / quantStep + 0.5f) * quantStep;
			held = q + rng.bi() * noiseAmp;
		}
		line.write(held);

		float y = postLp.lp(line.read(timeSm.process(delaySamples)));
		c.send = y;

		// The insert. Nothing patched means the send is what comes back, so the
		// loop behaves exactly as it does with the jacks empty.
		float r = c.retConnected ? c.ret : y;
		// Feedback is allowed past unity; the soft ceiling is what stops that
		// being a fault instead of a feature.
		fbState = sanitize(5.f * tanhApprox(fbLp.lp(dc.process(r)) * fb * 0.2f));

		out = y * level;
	}
};

// ---------------------------------------------------------------------------
// 100  Little Angel -- PT2399 chorus
//
// The same chip, run short: 5 to 30 ms, modulated. VIBE kills the dry path so
// only the pitch modulation is left; WARBLE adds the second, slower and
// irregular drift that makes the board sound like a tape motor rather than a
// chorus pedal. The two switches are one macro here, because the panel has
// three knobs and no room for a switch.

struct LittleAngel {
	DelayLine lineL, lineR;
	OnePole preLp, lpL, lpR;
	Lfo lfo;
	Rng rng;

	float warble, warbleTarget, warblePhase;
	// control rate
	float baseSamples, depthSamples, dryGain, wetGain, warbleAmt, quantStep;
	int mode;

	LittleAngel() : warble(0.f), warbleTarget(0.f), warblePhase(0.f),
	                baseSamples(400.f), depthSamples(100.f), dryGain(1.f),
	                wetGain(1.f), warbleAmt(0.f), quantStep(0.02f), mode(0) {}

	void init() {
		lineL.init((int) (0.05f * MAX_SR));
		lineR.init((int) (0.05f * MAX_SR));
	}

	void clear() {
		lineL.clear(); lineR.clear();
		preLp.clear(); lpL.clear(); lpR.clear();
		lfo.reset();
		warble = warbleTarget = warblePhase = 0.f;
	}

	void setSampleRate(float sr) {
		preLp.setCutoff(5500.f, sr);
		lpL.setCutoff(5500.f, sr);
		lpR.setCutoff(5500.f, sr);
	}

	void setParams(const MiawCtx& c) {
		lfo.setFreq(0.05f + clamp(c.p[0], 0.f, 1.f) * 6.f, c.sr);   // SPEED
		float depth = clamp(c.p[1], 0.f, 1.f);                      // DEPTH
		baseSamples = 0.005f * c.sr + depth * 0.006f * c.sr;
		depthSamples = depth * 0.010f * c.sr;

		// MODE: chorus/normal, chorus/warble, vibe/normal, vibe/warble.
		mode = (int) clamp(std::floor(clamp(c.p[2], 0.f, 0.999f) * 4.f), 0.f, 3.f);
		bool vibe = (mode >= 2);
		bool wob = (mode == 1 || mode == 3);
		dryGain = vibe ? 0.f : 1.f;
		wetGain = vibe ? 1.f : 0.7f;
		warbleAmt = wob ? 0.008f * c.sr : 0.f;
		quantStep = 10.f / 512.f;   // nine bits: the chip, run short
	}

	void process(const MiawCtx& c, float in, float& outL, float& outR) {
		lfo.step();

		// The warble: a random walk resampled a few times a second, smoothed, so
		// it drifts rather than steps.
		warblePhase += 3.5f / c.sr;
		if (warblePhase >= 1.f) {
			warblePhase -= 1.f;
			warbleTarget = rng.bi();
		}
		warble += 0.002f * (warbleTarget - warble);

		float v = preLp.lp(clamp(in, -12.f, 12.f));
		v = std::floor(v / quantStep + 0.5f) * quantStep;
		lineL.write(v);
		lineR.write(v);

		float wob = warble * warbleAmt;
		float dL = baseSamples + depthSamples * lfo.sine() + wob;
		float dR = baseSamples + depthSamples * lfo.sine(0.25f) - wob;

		float wL = lpL.lp(lineL.read(dL));
		float wR = lpR.lp(lineR.read(dR));

		outL = in * dryGain + wL * wetGain;
		outR = in * dryGain + wR * wetGain;
	}
};

// ---------------------------------------------------------------------------
// 101  Spring reverb
//
// A spring is not a room: it is four lengths of wire, each of which disperses
// high frequencies ahead of low ones, so an impulse comes back as a descending
// chirp rather than as a copy. Four delay loops with an eight-stage allpass
// chain inside each loop is the standard way to get that, and it is what makes
// this sound like a tank instead of like a short hall.
//
// DRIVE is the driver transducer, which is the part that actually distorts on a
// real tank. AUX is TWANG: the kick you give the box.

struct Spring {
	DelayLine line[4];
	Ap1 disp[4][8];
	OnePole damp[4];
	OnePole hp;
	Svf tone;
	DCBlocker dc;
	Rng rng;

	float twang;
	bool prevGate;
	// control rate
	float dl[4], drive, fbGain, outGain;

	Spring() : twang(0.f), prevGate(false), drive(1.f), fbGain(0.6f), outGain(1.f) {
		for (int i = 0; i < 4; i++)
			dl[i] = 1000.f;
	}

	void init() {
		for (int i = 0; i < 4; i++)
			line[i].init((int) (0.06f * MAX_SR));
	}

	void clear() {
		for (int i = 0; i < 4; i++) {
			line[i].clear();
			damp[i].clear();
			for (int j = 0; j < 8; j++)
				disp[i][j].clear();
		}
		hp.clear();
		tone.clear();
		dc.clear();
		twang = 0.f;
		prevGate = false;
	}

	void setSampleRate(float sr) {
		hp.setCutoff(120.f, sr);
		dc.setSampleRate(sr);
	}

	void setParams(const MiawCtx& c) {
		// Four incommensurate loop lengths, so the tank does not ring on one note.
		static const float len[4] = {0.0281f, 0.0331f, 0.0397f, 0.0451f};
		for (int i = 0; i < 4; i++) {
			dl[i] = clamp(len[i] * c.sr, 4.f, line[i].maxDelay());
			damp[i].setCutoff(lerp(2200.f, 5200.f, clamp(c.p[2], 0.f, 1.f)), c.sr);
			// The dispersion corner sits inside the tank's passband; spreading it
			// across the four loops is what stops the chirp sounding like one
			// filter sweeping.
			for (int j = 0; j < 8; j++)
				disp[i][j].setFreq(700.f + 220.f * (float) i + 90.f * (float) j, c.sr);
		}
		drive = 0.4f + clamp(c.p[0], 0.f, 1.f) * 8.f;             // DRIVE
		fbGain = 0.45f + clamp(c.p[1], 0.f, 1.f) * 0.44f;         // DWELL
		tone.set(lerp(1400.f, 6500.f, clamp(c.p[2], 0.f, 1.f)), 0.7f, c.sr);
		// A tank is quiet, but not four times quieter than everything else in
		// the bank; the makeup tracks the driver so DRIVE stays a tone control.
		outGain = 6.4f / std::sqrt(drive);
	}

	void process(const MiawCtx& c, float in, float& outL, float& outR) {
		// TWANG: a gate edge kicks the tank the way a knuckle does.
		if (c.auxGate && !prevGate)
			twang = 1.f;
		prevGate = c.auxGate;
		twang *= 0.999f;
		if (twang < 1e-4f)
			twang = 0.f;

		// The driver transducer: this is the stage that clips on a real tank.
		float x = 2.5f * tanhApprox(hp.hp(clamp(in, -12.f, 12.f)) * drive * 0.2f);
		if (twang > 0.f)
			x += rng.bi() * twang * twang * 6.f;

		float sum[2] = {0.f, 0.f};
		for (int i = 0; i < 4; i++) {
			float y = line[i].read(dl[i]);
			float v = y;
			for (int j = 0; j < 8; j++)
				v = disp[i][j].process(v);
			v = damp[i].lp(v);
			line[i].write(sanitize(x * 0.25f + v * fbGain));
			sum[i & 1] += (i & 2) ? -y : y;
		}

		tone.process(dc.process(0.5f * (sum[0] + sum[1])) * outGain);
		float mono = tone.lp;
		// A tank has two pickup positions at best; the stereo here is the two
		// loop pairs, not a synthesised width.
		outL = clamp(0.5f * (mono + sum[0] * 0.25f * outGain), -10.f, 10.f);
		outR = clamp(0.5f * (mono + sum[1] * 0.25f * outGain), -10.f, 10.f);
	}
};

// ---------------------------------------------------------------------------
// 102  MXR Distortion+
//
// One LM741 in a non-inverting stage. The gain leg is a 1 M pot to a 4.7 k
// resistor in series with 47 nF, so the boost is 1 + Rf/4.7k above roughly
// 720 Hz and unity below it -- that lift is the pedal's whole voice. The 1 nF
// across the pot rolls the top of the boosted band off again, which is why a
// Distortion+ is thick rather than fizzy. The op-amp then clips against its own
// rails and a pair of germanium diodes clamps the output through 10 k.
//
// Two times oversampled around the clipper, because that is where the harmonics
// that would alias are made.

struct DistPlus {
	OnePole inHp, boostHp, boostLp, toneLp;
	DCBlocker dc;
	dsp::Upsampler<2, 8> up;
	dsp::Decimator<2, 8> down;

	float boost, level;

	DistPlus() : boost(1.f), level(0.5f) {}

	void init() {}

	void clear() {
		inHp.clear(); boostHp.clear(); boostLp.clear(); toneLp.clear();
		dc.clear();
		up.reset();
		down.reset();
	}

	void setSampleRate(float sr) {
		inHp.setCutoff(30.f, sr);
		dc.setSampleRate(sr);
	}

	void setParams(const MiawCtx& c) {
		float sr2 = c.sr * 2.f;
		// Rf, the 1 M pot, tapered so the useful half of the sweep is not all in
		// the last eighth of the rotation.
		float rf = 1000.f + 999000.f * std::pow(clamp(c.p[0], 0.f, 1.f), 2.2f);
		boost = rf / 4700.f;
		boostHp.setCutoff(720.f, sr2);                                   // 4k7 + 47n
		boostLp.setCutoff(clamp(1.f / (2.f * (float) M_PI * rf * 1e-9f),
		                        500.f, 18000.f), sr2);                   // 1n over Rf
		toneLp.setCutoff(lerp(1200.f, 12000.f, clamp(c.p[2], 0.f, 1.f)), c.sr);
		level = clamp(c.p[1], 0.f, 1.f);
	}

	/** One pass of the gain stage, at twice the sample rate. Signals here are
	    scaled so 1.0 is 5 V, which is what makes the 4.5 V rails and the 0.3 V
	    germanium knee readable as the numbers they are on the schematic. */
	inline float stage(float v) {
		float b = boostLp.lp(boostHp.hp(v)) * boost;
		float o = v + b;
		o = 0.9f * softClip(o / 0.9f);                 // +-4.5 V rails
		const float knee = 0.07f;                      // 0.35 V of germanium
		return tanhApprox(o / knee);                   // the clamp, normalised
	}

	void process(const MiawCtx& c, float in, float& out) {
		float x = inHp.hp(clamp(in, -12.f, 12.f)) * 0.2f;
		float buf[2];
		up.process(x, buf);
		buf[0] = stage(buf[0]);
		buf[1] = stage(buf[1]);
		float y = down.process(buf);
		y = toneLp.lp(dc.process(y));
		out = clamp(y * level * 5.f, -10.f, 10.f);
	}
};

// ---------------------------------------------------------------------------
// 103  Talk Funny
//
// A carrier oscillator frequency-modulated by the input, then used to modulate
// the input back. MODULATION I is the ring: the carrier is bipolar, so the
// input is multiplied by something that goes negative and the carrier itself
// disappears from the output. MODULATION II is amplitude modulation: the
// carrier is unipolar, so it stays. CHOPPER gates the result at a quarter of
// the carrier, which is what turns a robot voice into a broken one.
//
// FM SOURCE is the AUX jack; with nothing patched the input's own envelope
// drives the carrier, which is the board's normalled behaviour.

struct TalkFunny {
	Follower env;
	OnePole fmLp, outLp;
	DCBlocker dc;
	float phase;
	int wraps;
	bool chopState;
	// control rate
	float baseFreq, fmAmt;
	int mode;   // 0 ring, 1 ring+chop, 2 AM, 3 AM+chop

	TalkFunny() : phase(0.f), wraps(0), chopState(true),
	              baseFreq(200.f), fmAmt(0.f), mode(0) {}

	void init() {}

	void clear() {
		env.clear();
		fmLp.clear(); outLp.clear();
		dc.clear();
		phase = 0.f;
		wraps = 0;
		chopState = true;
	}

	void setSampleRate(float sr) {
		env.set(2.f, 60.f, sr);
		fmLp.setCutoff(120.f, sr);
		outLp.setCutoff(std::min(12000.f, sr * 0.45f), sr);
		dc.setSampleRate(sr);
	}

	void setParams(const MiawCtx& c) {
		baseFreq = 2.f * std::pow(2000.f / 2.f, clamp(c.p[0], 0.f, 1.f));  // 2 Hz .. 2 kHz
		fmAmt = clamp(c.p[1], 0.f, 1.f) * 5.f;                             // octaves
		mode = (int) clamp(std::floor(clamp(c.p[2], 0.f, 0.999f) * 4.f), 0.f, 3.f);
	}

	void process(const MiawCtx& c, float in, float& out) {
		float x = clamp(in, -12.f, 12.f);

		// FM source: the AUX jack, or the input's own envelope when it is empty.
		float src = c.auxConnected ? clamp(c.aux, -10.f, 10.f) * 0.1f
		                           : fmLp.lp(env.process(x)) * 0.2f;
		float f = clamp(baseFreq * std::pow(2.f, fmAmt * src), 0.02f, c.sr * 0.45f);

		float dt = f / c.sr;
		phase += dt;
		if (phase >= 1.f) {
			phase -= 1.f;
			if (++wraps >= 2) {          // carrier / 4
				wraps = 0;
				chopState = !chopState;
			}
		}
		float sq = (phase < 0.5f) ? 1.f : -1.f;
		sq += polyBlep(phase, dt);
		float p2 = phase + 0.5f;
		if (p2 >= 1.f)
			p2 -= 1.f;
		sq -= polyBlep(p2, dt);

		bool am = (mode >= 2);
		float carrier = am ? (0.5f + 0.5f * sq) : sq;
		float y = x * carrier;
		if (mode == 1 || mode == 3)
			y *= chopState ? 1.f : 0.f;

		out = clamp(outLp.lp(dc.process(y)), -10.f, 10.f);
	}
};

// ---------------------------------------------------------------------------
// 104  MW Bitcrusher / bit swapper
//
// An ADC0809 with its eight data lines on jacks, feeding an R-2R ladder with
// eight jacks of its own. The board's joke is that the patch cable between them
// is the effect: leave a bit unpatched and it is masked, cross two and the word
// is rewired. SWAP is the extreme of that -- the whole eight bit word reversed,
// so the loudest bit becomes the quietest.
//
// The converter clock is an LTC1799 with a /1 /10 /100 divider, which is the
// sample rate control; AUX is its CV.

struct BitCrush {
	OnePole lp1, lp2;
	DCBlocker dc;
	float phase, held;
	// control rate
	float rateHz, inGain;
	int bits;
	bool swap, lpfOn;

	BitCrush() : phase(0.f), held(0.f), rateHz(8000.f), inGain(1.f),
	             bits(8), swap(false), lpfOn(true) {}

	void init() {}

	void clear() {
		lp1.clear(); lp2.clear(); dc.clear();
		phase = held = 0.f;
	}

	void setSampleRate(float sr) { dc.setSampleRate(sr); }

	void setParams(const MiawCtx& c) {
		inGain = 0.1f + clamp(c.p[0], 0.f, 1.f) * 3.9f;
		bits = (int) clamp(std::floor(1.f + clamp(c.p[1], 0.f, 1.f) * 7.999f), 1.f, 8.f);
		float r = clamp(c.p[2], 0.f, 1.f);
		if (c.auxConnected)
			r = clamp(r + clamp(c.aux, -10.f, 10.f) * 0.1f, 0.f, 1.f);
		rateHz = 200.f * std::pow(48000.f / 200.f, r);
		swap = c.crushSwap;
		lpfOn = c.crushLpf;
		// The board's reconstruction filter, an LM358 second order at ~5 kHz.
		lp1.setCutoff(std::min(5000.f, c.sr * 0.45f), c.sr);
		lp2.setCutoff(std::min(5000.f, c.sr * 0.45f), c.sr);
	}

	static inline int reverse8(int v) {
		int r = 0;
		for (int i = 0; i < 8; i++)
			r |= ((v >> i) & 1) << (7 - i);
		return r;
	}

	void process(const MiawCtx& c, float in, float& out) {
		phase += rateHz / c.sr;
		if (phase >= 1.f) {
			phase -= std::floor(phase);
			float v = clamp(in * inGain, -5.f, 5.f);
			int code = (int) clamp(std::floor((v * 0.1f + 0.5f) * 255.f + 0.5f), 0.f, 255.f);
			int shift = 8 - bits;
			code = (code >> shift) << shift;      // the unpatched bits, grounded
			if (swap)
				code = reverse8(code);            // MSB <-> LSB
			held = ((float) code * (1.f / 255.f) - 0.5f) * 10.f;
		}
		float y = held;
		if (lpfOn)
			y = lp2.lp(lp1.lp(y));
		out = clamp(dc.process(y), -10.f, 10.f);
	}
};

// ---------------------------------------------------------------------------
// 105  4011 ring modulator
//
// Four NAND gates. Reading the board: input 1 lands on pins 13 and 9, input 2
// on pins 8 and 1, pin 10 feeds pins 2 and 12, pin 3 feeds pin 5, pin 11 feeds
// pin 6, and the output leaves from pin 4. Naming pin 10's gate X:
//
//     X   = NAND(A, B)
//     p3  = NAND(B, X)
//     p11 = NAND(A, X)
//     out = NAND(p3, p11)  =  A XOR B
//
// -- the canonical four-NAND exclusive-or, which for two squarewaves is exactly
// ring modulation. Each input reaches the gates through a 1N4148 and a 100 k
// pulldown, so only the positive half of a signal can raise a pin at all: the
// comparators below have their thresholds up off zero for that reason.
//
// SMOOTH replaces the gate array with the analogue product the array is a
// caricature of, for when the square edges are not the point.

struct Ring4011 {
	Comparator ca, cb;
	OnePole lp;
	DCBlocker dc;
	float phase;
	// control rate
	float carrierHz, thrLo, thrHi;
	bool smooth;

	Ring4011() : phase(0.f), carrierHz(220.f), thrLo(0.3f), thrHi(0.9f),
	             smooth(false) {}

	void init() {}

	void clear() {
		ca.clear(); cb.clear();
		lp.clear(); dc.clear();
		phase = 0.f;
	}

	void setSampleRate(float sr) { dc.setSampleRate(sr); }

	void setParams(const MiawCtx& c) {
		carrierHz = 1.f * std::pow(4000.f, clamp(c.p[0], 0.f, 1.f));   // 1 Hz .. 4 kHz
		// BIAS: where the CMOS input decides, and how much hysteresis it has.
		float b = clamp(c.p[1], 0.f, 1.f);
		float mid = lerp(0.15f, 2.5f, b);
		thrLo = mid * 0.75f;
		thrHi = mid * 1.25f;
		lp.setCutoff(lerp(800.f, 16000.f, clamp(c.p[2], 0.f, 1.f)), c.sr);
		smooth = c.ringSmooth;
	}

	void process(const MiawCtx& c, float inA, float inB, float& out) {
		float dt = carrierHz / c.sr;
		phase += dt;
		if (phase >= 1.f)
			phase -= 1.f;

		// Input 2 is normalled to the internal carrier, which is what makes this
		// usable with one cable.
		float b;
		if (c.inRConnected) {
			b = clamp(inB, -12.f, 12.f);
		}
		else {
			float sq = (phase < 0.5f) ? 1.f : -1.f;
			sq += polyBlep(phase, dt);
			float p2 = phase + 0.5f;
			if (p2 >= 1.f)
				p2 -= 1.f;
			sq -= polyBlep(p2, dt);
			b = sq * 5.f;
		}
		float a = clamp(inA, -12.f, 12.f);

		float y;
		if (smooth) {
			y = a * b * 0.2f;
		}
		else {
			bool la = ca.process(a, thrLo, thrHi);
			bool lb = cb.process(b, thrLo, thrHi);
			y = (la != lb) ? 5.f : -5.f;
		}
		out = clamp(lp.lp(dc.process(y)), -10.f, 10.f);
	}
};

// ---------------------------------------------------------------------------
/** The seven boards, wired to one selector. Only one runs at a time; they all
    exist at once so a program change is a branch rather than an allocation. */
struct MiawRack {
	Echomatic echo;
	LittleAngel angel;
	Spring spring;
	DistPlus dist;
	TalkFunny talk;
	BitCrush crush;
	Ring4011 ring;

	void init() {
		echo.init();
		angel.init();
		spring.init();
		dist.init();
		talk.init();
		crush.init();
		ring.init();
	}

	void setSampleRate(float sr) {
		echo.setSampleRate(sr);
		angel.setSampleRate(sr);
		spring.setSampleRate(sr);
		dist.setSampleRate(sr);
		talk.setSampleRate(sr);
		crush.setSampleRate(sr);
		ring.setSampleRate(sr);
	}

	void clearAll() {
		echo.clear();
		angel.clear();
		spring.clear();
		dist.clear();
		talk.clear();
		crush.clear();
		ring.clear();
	}

	void clear(int id) {
		switch (id) {
			case MW_ECHOMATIC:  echo.clear(); break;
			case MW_ANGEL:      angel.clear(); break;
			case MW_SPRING:     spring.clear(); break;
			case MW_DISTPLUS:   dist.clear(); break;
			case MW_TALKFUNNY:  talk.clear(); break;
			case MW_BITCRUSH:   crush.clear(); break;
			case MW_RING4011:   ring.clear(); break;
			default: break;
		}
	}

	void setParams(int id, const MiawCtx& c) {
		switch (id) {
			case MW_ECHOMATIC:  echo.setParams(c); break;
			case MW_ANGEL:      angel.setParams(c); break;
			case MW_SPRING:     spring.setParams(c); break;
			case MW_DISTPLUS:   dist.setParams(c); break;
			case MW_TALKFUNNY:  talk.setParams(c); break;
			case MW_BITCRUSH:   crush.setParams(c); break;
			case MW_RING4011:   ring.setParams(c); break;
			default: break;
		}
	}

	void process(int id, MiawCtx& c, float inL, float inR, float& outL, float& outR) {
		float mono = 0.5f * (inL + inR);
		float y = 0.f;
		switch (id) {
			case MW_ECHOMATIC:
				echo.process(c, mono, y);
				outL = outR = y;
				break;
			case MW_ANGEL:
				angel.process(c, mono, outL, outR);
				break;
			case MW_SPRING:
				spring.process(c, mono, outL, outR);
				break;
			case MW_DISTPLUS:
				dist.process(c, mono, y);
				outL = outR = y;
				break;
			case MW_TALKFUNNY:
				talk.process(c, mono, y);
				outL = outR = y;
				break;
			case MW_BITCRUSH:
				crush.process(c, mono, y);
				outL = outR = y;
				break;
			case MW_RING4011:
				ring.process(c, inL, inR, y);
				outL = outR = y;
				break;
			default:
				outL = inL;
				outR = inR;
				break;
		}
	}
};

} // namespace divfx

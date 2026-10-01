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
#include "../Pt2399.hpp"
#include "../Adc0809.hpp"

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
	int crushDiv;        // 104: LTC1799 DIV switch, 0 /1, 1 /10, 2 /100
	int crushGain;       // 104: output stage's RP3, 0..3 = 1x 2x 4x 10x
	bool crushUnipolar;  // 104: the ADC's own 0-5 V input, no offset ahead of it
	bool ringSmooth;     // 105: analogue product instead of the gate array
	float send;          // written by the circuit

	MiawCtx() : sr(44100.f), aux(0.f), auxConnected(false), auxGate(false),
	            tapSec(0.f), ret(0.f), retConnected(false), inRConnected(false),
	            crushSwap(false), crushLpf(true), crushDiv(1), crushGain(1),
	            crushUnipolar(false), ringSmooth(false), send(0.f) {
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
// The PT2399 boards (99 Echomatic, 100 Little Angel)
//
// Both are the chip in src/Pt2399.hpp, the one Amortization and Racketeer run:
// a 1-bit adaptive delta modulator writing 44 kbit of RAM, a demodulator reading
// it back, and a clock set by pin 6. It is not a delay line. The delay is the
// RAM length over the bit rate, so it is the *clock* that is the delay control:
// moving it changes the rate the stored bits are replayed at, which is what bends
// the pitch on the real chip, and a long delay is a slow clock -- less bandwidth,
// more slope overload and granular hiss -- with nothing faked to say so.
//
// What is NOT from the datasheet or the chip model is the board around it, and
// the two boards' schematics were not in the course folder this was built from
// (only their panel numbers, 12.3 and 12.6, are on record). Everything that
// stands in for them is here, in one place, so it can be replaced by the real
// values rather than hunted down.
namespace miaw_assumed {
	// Volts at the chip's pins per Rack volt. +-5 V audio is +-1 loop unit, and a
	// unit is the chip's full scale (CHIP_CLIP, 2.4 V): a divider of about 0.48 in
	// front of a 5 V part. ASSUMED -- the boards' input stages are not on record.
	static const float RACK_VOLTS_PER_UNIT = 5.f;
	// LPF1 and LPF2 around the chip: two real poles each, not resonant. The
	// datasheet's application circuit puts them in the 6-10 kHz region; 7 kHz is
	// a midpoint, not a reading. ASSUMED.
	static const float ECHO_BOARD_FC = 7000.f;
	// The Little Angel's roll-off, as documented before the chip replaced the old
	// converter model (one pole each side). Kept as it was.
	static const float ANGEL_FC = 5500.f;
	// The datasheet's output noise floor, -90 dBV, at the comparator.
	static const float CHIP_NOISE_V = 40e-6f;
	// The fastest clock the Little Angel is allowed to ask for, as a delay. The
	// measured delay law (Electric Druid) stops at 30 ms; a chorus runs the same
	// part well below that, and how far is the board's business. ASSUMED, and
	// bounded because the cost is the bit rate: 3 ms is ~15 Mbit/s.
	static const float ANGEL_MIN_DELAY = 0.003f;
}

// ---------------------------------------------------------------------------
// 99  Echomatic -- PT2399 echo, 30 ms to 1 s
//
// TIME is the chip's clock (or the TAP clock). FEEDBACK goes past unity on
// purpose; what stops that being a fault is the chip itself -- the modulator's
// integrator and the input op-amp clip at the supply -- not a tanh added for
// the purpose, so the loop sings at a bounded level and gets dirtier as it does.
//
// The TO FX / FROM FX jacks are the board's insert, and they sit inside the
// feedback path, which is why a distortion patched into them gets dirtier with
// every repeat rather than once.

struct Echomatic {
	pt2399::Pt2399 chip;
	OnePole in1, in2, out1, out2;
	DCBlocker dc;
	Smooth timeSm;
	Rng noise;

	float prevIn, fbState;
	// control rate
	float delaySec, fb, level;

	Echomatic() : prevIn(0.f), fbState(0.f), delaySec(0.3f), fb(0.3f), level(0.8f) {}

	void init() {}

	void resetChip() {
		chip.reset();
		chip.setBitRate((double) pt2399::Pt2399::kBits / (double) delaySec);
	}

	void clear() {
		in1.clear(); in2.clear(); out1.clear(); out2.clear();
		dc.clear();
		prevIn = fbState = 0.f;
		timeSm.snap(delaySec);
		resetChip();
	}

	void setSampleRate(float sr) {
		chip.setSampleRate((double) sr);
		dc.setSampleRate(sr);
		timeSm.setTime(60.f, sr);
		in1.setCutoff(miaw_assumed::ECHO_BOARD_FC, sr);
		in2.a = in1.a;
		out1.setCutoff(miaw_assumed::ECHO_BOARD_FC, sr);
		out2.a = out1.a;
	}

	void setParams(const MiawCtx& c) {
		// 30 ms .. 1 s, log. A locked TAP clock takes the knob's place.
		float t = 0.03f * std::pow(1.f / 0.03f, clamp(c.p[0], 0.f, 1.f));
		if (c.tapSec > 0.f)
			t = clamp(c.tapSec, 0.03f, 1.f);
		delaySec = t;

		fb = clamp(c.p[1], 0.f, 1.f) * 1.25f;   // past unity on purpose
		level = clamp(c.p[2], 0.f, 1.f);
	}

	void process(MiawCtx& c, float in, float& out) {
		const float volts = (float) pt2399::assumed::CHIP_CLIP;

		// --- the clock: TIME sets the bit rate, smoothed so a knob is a bend
		// rather than a step ---
		float d = clamp(timeSm.process(delaySec), 0.03f, 1.f);
		chip.setBitRate((double) pt2399::Pt2399::kBits / (double) d);
		chip.begin();

		// --- the demodulator reads what was written a delay ago ---
		float y = (float) (chip.demod() / volts);
		if (!std::isfinite(y)) {
			resetChip();
			prevIn = 0.f;
			y = 0.f;
		}
		y = out2.lp(out1.lp(dc.process(y)));
		float ySend = y * miaw_assumed::RACK_VOLTS_PER_UNIT;
		c.send = ySend;

		// --- the insert. Nothing patched means the send is what comes back, so
		// the loop behaves exactly as it does with the jacks empty. ---
		float r = c.retConnected ? c.ret : ySend;
		fbState = sanitize(fb * r / miaw_assumed::RACK_VOLTS_PER_UNIT);

		// --- input stage; its op-amp clips at the chip's rail ---
		float x = clamp(in, -12.f, 12.f) / miaw_assumed::RACK_VOLTS_PER_UNIT + fbState;
		float v = clamp(in2.lp(in1.lp(x)), -1.f, 1.f);

		// --- the comparator: noise floor, then the modulator writes the RAM ---
		float cur = volts * v
		          + miaw_assumed::CHIP_NOISE_V * 1.7320508f * noise.bi();
		chip.modulate(prevIn, cur);
		prevIn = cur;

		out = ySend * level;
	}
};

// ---------------------------------------------------------------------------
// 100  Little Angel -- PT2399 chorus
//
// The same chip, run short: 5 to 30 ms modulated. VIBE kills the dry path so
// only the pitch modulation is left; WARBLE adds the second, slower and
// irregular drift that makes the board sound like a tape motor rather than a
// chorus pedal. The two switches are one macro here, because the panel has
// three knobs and no room for a switch.
//
// The modulation is of the clock, as it is on the board, so the pitch shift is
// the chip's own and not a moving read pointer. The board is mono; the second
// chip, run a quarter cycle on, is this module's stereo addition.

struct LittleAngel {
	pt2399::Pt2399 chipL, chipR;
	OnePole in1, lpL, lpR;
	Lfo lfo;
	Rng rng, noise;

	float warble, warbleTarget, warblePhase;
	float prevIn;
	// control rate
	float baseSec, depthSec, dryGain, wetGain, warbleSec;
	int mode;

	LittleAngel() : warble(0.f), warbleTarget(0.f), warblePhase(0.f), prevIn(0.f),
	                baseSec(0.008f), depthSec(0.002f), dryGain(1.f),
	                wetGain(1.f), warbleSec(0.f), mode(0) {}

	void init() {}

	void resetChips() {
		chipL.reset(); chipR.reset();
		chipL.setBitRate((double) pt2399::Pt2399::kBits / (double) baseSec);
		chipR.setBitRate((double) pt2399::Pt2399::kBits / (double) baseSec);
	}

	void clear() {
		in1.clear(); lpL.clear(); lpR.clear();
		lfo.reset();
		warble = warbleTarget = warblePhase = 0.f;
		prevIn = 0.f;
		resetChips();
	}

	void setSampleRate(float sr) {
		chipL.setSampleRate((double) sr);
		chipR.setSampleRate((double) sr);
		in1.setCutoff(miaw_assumed::ANGEL_FC, sr);
		lpL.setCutoff(miaw_assumed::ANGEL_FC, sr);
		lpR.setCutoff(miaw_assumed::ANGEL_FC, sr);
	}

	void setParams(const MiawCtx& c) {
		lfo.setFreq(0.05f + clamp(c.p[0], 0.f, 1.f) * 6.f, c.sr);   // SPEED
		float depth = clamp(c.p[1], 0.f, 1.f);                      // DEPTH
		baseSec = 0.005f + depth * 0.006f;
		depthSec = depth * 0.010f;

		// MODE: chorus/normal, chorus/warble, vibe/normal, vibe/warble.
		mode = (int) clamp(std::floor(clamp(c.p[2], 0.f, 0.999f) * 4.f), 0.f, 3.f);
		bool vibe = (mode >= 2);
		bool wob = (mode == 1 || mode == 3);
		dryGain = vibe ? 0.f : 1.f;
		wetGain = vibe ? 1.f : 0.7f;
		warbleSec = wob ? 0.008f : 0.f;
	}

	void process(const MiawCtx& c, float in, float& outL, float& outR) {
		const float volts = (float) pt2399::assumed::CHIP_CLIP;
		lfo.step();

		// The warble: a random walk resampled a few times a second, smoothed, so
		// it drifts rather than steps.
		warblePhase += 3.5f / c.sr;
		if (warblePhase >= 1.f) {
			warblePhase -= 1.f;
			warbleTarget = rng.bi();
		}
		warble += 0.002f * (warbleTarget - warble);
		float wob = warble * warbleSec;

		// The clocks. Delay in seconds is what the board's pin-6 network sets; the
		// bit rate follows from it.
		float dL = clamp(baseSec + depthSec * lfo.sine() + wob,
		                 miaw_assumed::ANGEL_MIN_DELAY, 0.05f);
		float dR = clamp(baseSec + depthSec * lfo.sine(0.25f) - wob,
		                 miaw_assumed::ANGEL_MIN_DELAY, 0.05f);
		chipL.setBitRate((double) pt2399::Pt2399::kBits / (double) dL);
		chipR.setBitRate((double) pt2399::Pt2399::kBits / (double) dR);
		chipL.begin();
		chipR.begin();

		// The input stage. No feedback around the chips, so each one's demodulator
		// and modulator can run in the same pass, and the two chips together
		// (Pt2399::demodModulate2, bit-identical to demod() then modulate() on each).
		float v = clamp(in1.lp(clamp(in, -12.f, 12.f) / miaw_assumed::RACK_VOLTS_PER_UNIT),
		                -1.f, 1.f);
		float cur = volts * v
		          + miaw_assumed::CHIP_NOISE_V * 1.7320508f * noise.bi();
		float yL, yR;
		pt2399::Pt2399::demodModulate2(chipL, chipR, prevIn, cur, yL, yR);
		yL = (float) (yL / volts);
		yR = (float) (yR / volts);
		if (!std::isfinite(yL) || !std::isfinite(yR)) {
			resetChips();
			prevIn = 0.f;
			yL = yR = 0.f;
		}
		else {
			prevIn = cur;
		}
		float wL = lpL.lp(yL) * miaw_assumed::RACK_VOLTS_PER_UNIT;
		float wR = lpR.lp(yR) * miaw_assumed::RACK_VOLTS_PER_UNIT;

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
// The board as drawn (Kristian Blasol, Sourcery Studios, Rev 1.0): an LTC1799
// clocks an ADC0809 that converts for ever, its eight data lines feed a 1k / 2k
// ladder, and the ladder sums into an inverting op-amp. The board's joke is that
// the connection between the two is the effect: leave a bit unpatched and its
// leg is open, cross two and the word is rewired. SWAP is the extreme of that --
// the whole word reversed, so the loudest bit becomes the quietest.
//
// The sample rate is the converter's own: one conversion is 64 clocks plus the
// START-to-EOC gap, so the rate is the LTC1799's clock over 72, and RATE (and its
// CV, AUX) is R_SET. The ladder is solved as a network (src/Adc0809.hpp), so an
// open leg changes its neighbours' weights as it does on the board. Behind it:
// SW1's passive filter (680 ohm, 68 nF: 3.44 kHz, one pole), U7.2's non-inverting
// gain stage, and C1 into whatever the output feeds.
//
// Everything the schematic leaves open is in miaw_assumed, below.

namespace miaw_assumed {
	// U7.1: the ladder's sum into a 1k feedback resistor (R27).
	static const float BC_RF = 1000.f;
	// SW1's filter: R29 680 ohm, C2 68 nF.
	static const float BC_LPF_R = 680.f, BC_LPF_C = 68e-9f;
	// U7.2: gain = 1 + RP3 / R28 (2k2). RP3 is a 20k pot; four positions are
	// offered, 1x 2x 4x and the pot's own top, 10.09x. 2x is the default because
	// it makes the converter's full scale +-5 V at the output, Rack's nominal.
	static const float BC_RP3[4] = { 0.f, 2200.f, 6600.f, 20000.f };
	static const float BC_R28 = 2200.f;
	// C1, 10 uF, into the next input. A Eurorack input is about 100k.
	static const float BC_C1 = 10e-6f, BC_LOAD = 100000.f;
	// The LM358's output swing on +-12 V: V+ - 1.5 V at the top, to within about
	// 20 mV of V- at the bottom (datasheet typicals). ASSUMED: the schematic
	// draws pin 4 on GND, but U7.1 inverts a positive current and so needs to
	// swing negative; a single supply would leave it stuck at 0 V and the board
	// silent. It is modelled as the +-12 V part it has to be.
	static const float BC_VHI = 10.5f, BC_VLO = -11.98f;
	// Offset ahead of the ADC for ordinary bipolar audio: +-5 V onto 0..5 V.
	// The board itself has none (see crushUnipolar).
	static const float BC_BIAS = 2.5f, BC_SCALE = 0.5f;
	// The RP2 pot is mapped log across R_SET's travel, 3k (R26 alone) to 1.003M
	// (R26 plus the whole pot). The board's pot taper is not on record; this is
	// this module's own mapping, chosen so RATE is musical.
	static const double BC_RSET_MIN = 3000.0, BC_RSET_MAX = 1003000.0;
}

struct BitCrush {
	adc0809::Adc0809 adc;
	adc0809::R2rDac dac;
	float table[256];
	OnePole lpf, ac;
	double vPrev;
	bool primed;
	// control rate
	double clocks;           // LTC1799 clock periods per audio sample
	float level, gain;
	int bitsOn;
	bool swap, lpfOn, unipolar;
	int builtBits;
	bool builtSwap;

	BitCrush() : vPrev(0.0), primed(false), clocks(1.0), level(1.f), gain(2.f),
	             bitsOn(8), swap(false), lpfOn(true), unipolar(false),
	             builtBits(-1), builtSwap(false) {
		for (int i = 0; i < 256; i++) table[i] = 0.f;
	}

	void init() {}

	void clear() {
		adc.reset();
		lpf.clear(); ac.clear();
		primed = false;
		acPrimed = false;
		vPrev = 0.0;
		builtBits = -1;
	}

	void setSampleRate(float sr) {
		float fc = 1.f / (2.f * (float) M_PI * miaw_assumed::BC_LPF_R * miaw_assumed::BC_LPF_C);
		lpf.setCutoff(std::min(fc, sr * 0.45f), sr);
		// C1 into its load: a corner under 1 Hz, which OnePole::setCutoff floors.
		ac.a = 1.f - std::exp(-1.f / (miaw_assumed::BC_C1 * miaw_assumed::BC_LOAD * sr));
	}

	void setParams(const MiawCtx& c) {
		level = clamp(c.p[0], 0.f, 1.f);                              // RP1
		bitsOn = (int) clamp(std::floor(1.f + clamp(c.p[1], 0.f, 1.f) * 7.999f), 1.f, 8.f);

		float x = clamp(c.p[2], 0.f, 1.f);                            // RP2; 1 is fastest
		if (c.auxConnected)
			x = clamp(x + clamp(c.aux, -10.f, 10.f) * 0.1f, 0.f, 1.f);
		double rSet = miaw_assumed::BC_RSET_MIN
		            * std::pow(miaw_assumed::BC_RSET_MAX / miaw_assumed::BC_RSET_MIN, 1.0 - (double) x);
		static const int divN[3] = { 1, 10, 100 };
		int d = c.crushDiv < 0 ? 0 : (c.crushDiv > 2 ? 2 : c.crushDiv);
		clocks = adc0809::Ltc1799::hz(rSet, divN[d]) / (double) c.sr;

		int g = c.crushGain < 0 ? 0 : (c.crushGain > 3 ? 3 : c.crushGain);
		gain = 1.f + miaw_assumed::BC_RP3[g] / miaw_assumed::BC_R28;
		unipolar = c.crushUnipolar;
		lpfOn = c.crushLpf;
		swap = c.crushSwap;

		// A leg is open when its data line is not patched, which is what BITS
		// does to the low bits. The table is the ladder solved for that.
		if (bitsOn != builtBits || swap != builtSwap) {
			unsigned connected = (0xFFu << (8 - bitsOn)) & 0xFFu;
			dac.buildTable(miaw_assumed::BC_RF, connected, swap, table);
			builtBits = bitsOn;
			builtSwap = swap;
		}
	}

	void process(const MiawCtx& c, float in, float& out) {
		(void) c;
		// RP1, then the ADC's input. The converter reads 0..5 V; the offset ahead
		// of it is what makes bipolar audio whole.
		double v = (double) (clamp(in, -12.f, 12.f) * level);
		if (!unipolar)
			v = (double) miaw_assumed::BC_BIAS + (double) miaw_assumed::BC_SCALE * v;
		if (!primed) {
			vPrev = v;
			primed = true;
		}
		// The ladder stage, averaged over this audio sample.
		float v1 = adc.run(clocks, vPrev, v, table);
		vPrev = v;
		v1 = clamp(v1, miaw_assumed::BC_VLO, miaw_assumed::BC_VHI);

		// C2 and C1 are charged to the DC the board sits at, so a program change
		// does not open with the whole offset as a step.
		if (!acPrimed) {
			lpf.z = v1;
			ac.z = clamp(v1 * gain, miaw_assumed::BC_VLO, miaw_assumed::BC_VHI);
			acPrimed = true;
		}
		float v1f = lpfOn ? lpf.lp(v1) : v1;                          // SW1
		float v2 = clamp(v1f * gain, miaw_assumed::BC_VLO, miaw_assumed::BC_VHI);   // U7.2
		out = clamp(sanitize(v2 - ac.lp(v2)), -12.f, 12.f);
	}

	bool acPrimed = false;
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

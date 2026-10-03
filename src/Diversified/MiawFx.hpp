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
#include "../OpAmp.hpp"
#include "LittleAngel.hpp"
#include "SpringBoard.hpp"

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
	int springGain;      // 101: RP2, the pickup's gain pot, 0..5 = 1x 11x 26x 51x 76x 101x
	int springTank;      // 101: 0 Leem KA-1210 (three springs), 1 Olson X-82 (two)
	float send;          // written by the circuit

	MiawCtx() : sr(44100.f), aux(0.f), auxConnected(false), auxGate(false),
	            tapSec(0.f), ret(0.f), retConnected(false), inRConnected(false),
	            crushSwap(false), crushLpf(true), crushDiv(1), crushGain(1),
	            crushUnipolar(false), ringSmooth(false), springGain(4), springTank(0), send(0.f) {
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
// the Echomatic's schematic was not in the course folder this was built from
// (only its panel number, 12.3, is on record). Everything that stands in for it
// is here, in one place, so it can be replaced by the real values rather than
// hunted down. The Little Angel is Rick Holt's published board and has its own
// header, LittleAngel.hpp, with its own list of what is assumed.
namespace miaw_assumed {
	// Volts at the chip's pins per Rack volt. +-5 V audio is +-1 loop unit, and a
	// unit is the chip's full scale (CHIP_CLIP, 2.4 V): a divider of about 0.48 in
	// front of a 5 V part. ASSUMED -- the boards' input stages are not on record.
	static const float RACK_VOLTS_PER_UNIT = 5.f;
	// LPF1 and LPF2 around the chip: two real poles each, not resonant. The
	// datasheet's application circuit puts them in the 6-10 kHz region; 7 kHz is
	// a midpoint, not a reading. ASSUMED.
	static const float ECHO_BOARD_FC = 7000.f;
	// The datasheet's output noise floor, -90 dBV, at the comparator.
	static const float CHIP_NOISE_V = 40e-6f;
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
// 100  Little Angel -- Rick Holt's PT2399 mini chorus (Jack Orman's NYE rev 2)
//
// The board and its notes are in LittleAngel.hpp. It is a PT2399 held at its
// shortest delay with the LFO on the chip's REF pin, not a swept clock; the
// MODE macro is the board's Chorus/Vibe and Space/Warbler switches.

// ---------------------------------------------------------------------------
// 101  Spring reverb -- Kristian Blasol's Day 12 "Spring reverb with speaker and piezo"
//
// The board is SpringBoard.hpp: a 2N2222 driving a small speaker (solved with the nodal
// solver, speaker as a voice coil plus its motional impedance), a piezo into an NE5532
// gain stage with RP2, and a follower. The drawing has no tank ("Put the spring here"),
// so the tank is SpringTank.hpp: Parker & Bilbao's dispersion relation for a helical
// spring, solved, with the springs' dimensions as Parker measured them off two real tanks.
// Everything assumed is marked ASSUMED where it is declared.
//
// DRIVE is RP1, the input pot. DWELL is the tank's reverberation time (0.4 s to 6 s) and
// TONE the loss in the loop (a 1 kHz to 6 kHz damping pole); neither is on the drawing,
// they are the two tank properties a player would want. RP2 (the pickup's 1x-101x gain
// pot) and the choice of tank are in the module's menu. AUX is a knock on the box.
// The pickup is one piezo: the output is mono.

struct Spring {
	springboard::Board board;
	float lastSr;
	bool built;

	Spring() : lastSr(0.f), built(false) {}

	void init() {
		// Designs the springs (a one-off, shared by every instance) and sizes the buffers.
		(void) springtank::tankPreset(0);
		(void) springtank::tankPreset(1);
	}

	void clear() {
		if (built)
			board.clear();
	}

	void setSampleRate(float sr) {
		if (built && sr == lastSr)
			return;
		lastSr = sr;
		board.setSampleRate((double) sr);
		built = true;
	}

	void setParams(const MiawCtx& c) {
		if (!built)
			return;
		static const double rp2[6] = {0.0, 0.1, 0.25, 0.5, 0.75, 1.0};
		board.setTank(c.springTank);
		double dwell = clamp(c.p[1], 0.f, 1.f), tone = clamp(c.p[2], 0.f, 1.f);
		int g = c.springGain < 0 ? 0 : (c.springGain > 5 ? 5 : c.springGain);
		board.setParams(clamp(c.p[0], 0.f, 1.f), 0.4 * std::pow(15.0, dwell), 1000.0 * std::pow(6.0, tone), rp2[g]);
	}

	void process(const MiawCtx& c, float in, float& outL, float& outR) {
		if (!built) {
			outL = outR = 0.f;
			return;
		}
		double y = board.process((double) clamp(in, -12.f, 12.f), c.auxGate);
		outL = outR = clamp(sanitize((float) y), -10.f, 10.f);
	}
};

// ---------------------------------------------------------------------------
// 102  MXR Distortion+
//
// One LM741 in a non-inverting stage. The gain leg is a 1 M pot to a 4.7 k
// resistor in series with 47 nF, so the boost is 1 + Rf/4.7k above roughly
// 720 Hz and unity below it -- that lift is the pedal's whole voice. The 1 nF
// across the pot rolls the top of the boosted band off again, which is why a
// Distortion+ is thick rather than fizzy. A pair of germanium diodes clamps the
// output through 10 k.
//
// The op-amp is the datasheet's 741 (src/OpAmp.hpp), in the network as drawn:
// 1 MHz gain-bandwidth, which at a gain of 200 is a 5 kHz amplifier; 0.5 V/us, which
// turns the clipper's edges into ramps; and a swing of the +-4.5 V rails less the
// 741's 0.73 V and the 10 k's drop. The two corners are not first-order sections
// stood in for the network, they are the network: the same nodal equations the
// closed form comes from, solved a step at a time (tests/OpAmp checks them against
// it). Sixteen times oversampled around the op-amp (1.3 us steps), because that is
// what its loop needs; two times at the clipper, where the harmonics that would
// alias are made.

struct DistPlus {
	OnePole inHp, toneLp;
	DCBlocker dc;
	dsp::Upsampler<2, 8> up;
	dsp::Decimator<2, 8> down;
	opamp::NonInvertingStage amp;

	float level;
	double dtOs;           // seconds per oversampled (2x) sample
	double prev;           // the last input to the op-amp, volts
	static const int kSub = 8;

	DistPlus() : level(0.5f), dtOs(1.0 / 96000.0), prev(0.0) { init(); }

	void init() {
		amp = opamp::NonInvertingStage();
		amp.op.setSpec(opamp::lm741());
		amp.op.setSupply(4.5, -4.5);        // 9 V battery, 4.5 V virtual ground
		amp.op.setLoad(10e3, 0.0);          // the clamp's 10 k
		amp.rg = 4700.0; amp.cg = 47e-9;
		amp.rf = 1e6; amp.cf = 1e-9;
		amp.reset(0.0);
		prev = 0.0;
	}

	void clear() {
		inHp.clear(); toneLp.clear();
		dc.clear();
		up.reset();
		down.reset();
		amp.reset(0.0);
		prev = 0.0;
	}

	void setSampleRate(float sr) {
		inHp.setCutoff(30.f, sr);
		dc.setSampleRate(sr);
		dtOs = 1.0 / (2.0 * (double) sr);
	}

	void setParams(const MiawCtx& c) {
		// Rf, the 1 M pot, tapered so the useful half of the sweep is not all in
		// the last eighth of the rotation.
		float rf = 1000.f + 999000.f * std::pow(clamp(c.p[0], 0.f, 1.f), 2.2f);
		amp.rf = (double) rf;
		toneLp.setCutoff(lerp(1200.f, 12000.f, clamp(c.p[2], 0.f, 1.f)), c.sr);
		level = clamp(c.p[1], 0.f, 1.f);
	}

	/** One pass of the gain stage, at twice the sample rate: the op-amp in volts,
	    the clamp in units of 5 V (so the 0.35 V germanium knee reads as 0.07). */
	inline float stage(float v) {
		double y = amp.stepRamp(prev, (double) v, dtOs, kSub);
		prev = (double) v;
		float o = (float) (y * 0.2);
		const float knee = 0.07f;                      // 0.35 V of germanium
		return tanhApprox(o / knee);                   // the clamp, normalised
	}

	void process(const MiawCtx& c, float in, float& out) {
		(void) c;
		float x = inHp.hp(clamp(in, -12.f, 12.f));
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
	// The LM358s (src/OpAmp.hpp: 0.7 MHz, 0.3 V/us, a class-B output with its
	// crossover and a swing that depends on the load). ASSUMED: the schematic
	// draws pin 4 on GND, but U7.1 inverts a positive current and so needs to
	// swing negative; a single supply would leave it stuck at 0 V and the board
	// silent. They are modelled as running from +-12 V, as the part has to be.
	static const double BC_VPOS = 12.0, BC_VNEG = -12.0;
	// What each LM358 drives, ASSUMED from the schematic's parts. U7.1: its own
	// 1k feedback resistor to the virtual ground, and SW1 (680 ohm into a 68 nF that
	// is 780 ohm at 3 kHz) in parallel, 1k || 1360 at the filter's corner; with SW1
	// out just the 1k. U7.2: its feedback and R28 (RP3 + 2k2) to ground, in
	// parallel with C1 into 100 k.
	static const double BC_U71_LOAD_LPF = 576.0, BC_U71_LOAD = 1000.0;
	// Steps of the analogue half per audio sample. The converter's code changes land
	// anywhere in a sample, and a 5 V step takes the LM358 17 us to slew, so the
	// stages run at 8x and the output is the mean of the 8 (integrate and dump,
	// which is the anti-alias filter the old per-sample average was).
	static const int BC_SUB = 8;
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
	opamp::InvertingStage u71;         // the ladder's sum
	opamp::NonInvertingStage u72;      // the gain stage
	double vPrev;
	double subDt;
	bool primed;
	// control rate
	double clocks;           // LTC1799 clock periods per audio sample
	float level, gain;
	int bitsOn;
	bool swap, lpfOn, unipolar;
	int builtBits;
	bool builtSwap;
	int crushGainIdx;

	BitCrush() : vPrev(0.0), subDt(1.0 / (48000.0 * miaw_assumed::BC_SUB)), primed(false),
	             clocks(1.0), level(1.f), gain(2.f),
	             bitsOn(8), swap(false), lpfOn(true), unipolar(false),
	             builtBits(-1), builtSwap(false), crushGainIdx(1) {
		for (int i = 0; i < 256; i++) table[i] = 0.f;
		init();
	}

	void init() {
		u71 = opamp::InvertingStage();
		u72 = opamp::NonInvertingStage();
		u71.op.setSpec(opamp::lm358());
		u72.op.setSpec(opamp::lm358());
		u71.op.setSupply(miaw_assumed::BC_VPOS, miaw_assumed::BC_VNEG);
		u72.op.setSupply(miaw_assumed::BC_VPOS, miaw_assumed::BC_VNEG);
		u71.rf = miaw_assumed::BC_RF;
		u71.gin = 1.0 / 1000.0;
		u71.op.setLoad(miaw_assumed::BC_U71_LOAD_LPF, 0.0);
		u72.rg = miaw_assumed::BC_R28;
		u72.rf = 0.0;
		u72.op.setLoad(2200.0, 0.0);
		acPrimed = false;
	}

	void clear() {
		adc.reset();
		lpf.clear(); ac.clear();
		primed = false;
		acPrimed = false;
		vPrev = 0.0;
		builtBits = -1;
	}

	void setSampleRate(float sr) {
		double srSub = (double) sr * miaw_assumed::BC_SUB;
		subDt = 1.0 / srSub;
		float fc = 1.f / (2.f * (float) M_PI * miaw_assumed::BC_LPF_R * miaw_assumed::BC_LPF_C);
		lpf.setCutoff(std::min(fc, (float) srSub * 0.45f), (float) srSub);
		// C1 into its load: a corner under 1 Hz, which OnePole::setCutoff floors.
		ac.a = (float) -std::expm1(-1.0 / ((double) miaw_assumed::BC_C1 * miaw_assumed::BC_LOAD * srSub));
	}

	/** The conductance the 1k/2k ladder presents to U7.1's summing node, looking
	    into the string from the MSB end, with the legs in `driven` (bit j = data
	    line j, LSB = 0) at 0 V: it sets U7.1's noise gain, 1 + Rf * gin. */
	static double ladderConductance(unsigned driven) {
		double y = 1.0 / 2000.0 + (((driven >> 0) & 1u) ? 1.0 / 2000.0 : 0.0);      // LSB node: leg + termination
		for (int j = 1; j < 8; j++) {
			double r = 1.0 / y;
			y = (((driven >> j) & 1u) ? 1.0 / 2000.0 : 0.0) + 1.0 / (1000.0 + r);
		}
		return y;
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

		// U7.2: Rf is RP3, R28 is the leg to ground; with RP3 at 0 it is a follower
		// and R28 just hangs on the output. Its load is (RP3 + R28) || C1 into 100k.
		u72.rf = (double) miaw_assumed::BC_RP3[g];
		double lf = (double) miaw_assumed::BC_RP3[g] + (double) miaw_assumed::BC_R28;
		double ll = lf * miaw_assumed::BC_LOAD / (lf + miaw_assumed::BC_LOAD);
		if (g != crushGainIdx || ll != u72.op.rl) {
			crushGainIdx = g;
			u72.op.setLoad(ll, 0.0);
		}
		double l1 = lpfOn ? miaw_assumed::BC_U71_LOAD_LPF : miaw_assumed::BC_U71_LOAD;
		if (l1 != u71.op.rl)
			u71.op.setLoad(l1, 0.0);

		// A leg is open when its data line is not patched, which is what BITS
		// does to the low bits. The table is the ladder solved for that.
		if (bitsOn != builtBits || swap != builtSwap) {
			unsigned connected = (0xFFu << (8 - bitsOn)) & 0xFFu;
			dac.buildTable(miaw_assumed::BC_RF, connected, swap, table);
			unsigned driven = 0;
			for (int b = 0; b < 8; b++)
				if ((connected >> b) & 1u)
					driven |= 1u << (swap ? 7 - b : b);
			u71.gin = ladderConductance(driven);
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

		const int M = miaw_assumed::BC_SUB;
		double acc = 0.0;
		for (int k = 0; k < M; k++) {
			double f0 = (double) k / (double) M, f1 = (double) (k + 1) / (double) M;
			// The ladder stage over this eighth of the sample: the ideal virtual-
			// ground output the codes make, then what U7.1 does of it.
			float ideal = adc.run(clocks / (double) M, vPrev + (v - vPrev) * f0, vPrev + (v - vPrev) * f1, table);

			// C2 and C1 are charged to the DC the board sits at, so a program
			// change does not open with the whole offset as a step.
			if (!acPrimed) {
				u71.reset((double) ideal);
				lpf.z = ideal;
				double y2 = (double) ideal * (double) gain;
				u72.op.update();
				u72.reset(y2);
				ac.z = (float) u72.op.y;
				acPrimed = true;
			}
			// U7.1 is an inverting summer: the table is -Rf times the current the
			// legs push in, so that current is -table / Rf.
			double y1 = u71.step(-(double) ideal / miaw_assumed::BC_RF, subDt);
			float v1f = lpfOn ? lpf.lp((float) y1) : (float) y1;       // SW1
			double y2 = u72.step((double) v1f, subDt);                  // U7.2
			float v2 = (float) y2;
			acc += (double) v2 - (double) ac.lp(v2);
		}
		vPrev = v;
		out = clamp(sanitize((float) (acc / (double) M)), -12.f, 12.f);
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
			case MW_ANGEL:      angel.setParams(c.p[0], c.p[1], c.p[2]); break;
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
				angel.process(mono, outL, outR);
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

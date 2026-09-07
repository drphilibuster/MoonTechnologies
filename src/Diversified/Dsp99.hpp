#pragma once
/** The DSP99 bank -- Diversified's programs 0 to 98 -- and the program table
 * for the whole module.
 *
 * The DSP99 board is a Chinese reverb/delay module whose entire user interface
 * is a number from 0 to 99 and a sheet of paper listing what each number is:
 * three small halls, three medium halls, seven kinds of plate, nine delays, and
 * so on down to eleven combinations of a delay with something else. The sheet
 * is a taxonomy, not ninety-nine algorithms, and it is treated as one here:
 * twenty real algorithms, each a genuine implementation, arranged into the
 * bands the sheet names and varied across each band so that every number is a
 * different, sensible setting rather than a duplicate.
 *
 * A program is at most two blocks in series. That is enough for the whole
 * sheet, because every combination it lists -- reverb+chorus, delay+phase
 * shift, delay+transposition -- is exactly two things in a row.
 *
 * The table also carries programs 99 to 105, the dedicated MiaW boards, so that
 * one lookup answers for the whole knob. That is why this header includes
 * MiawFx.hpp: the bank that owns most of the numbers owns the table.
 */
#include "Primitives.hpp"
#include "MiawFx.hpp"

namespace divfx {

// --- what a block can be ---------------------------------------------------
enum Alg {
	A_NONE, A_REVERB, A_DELAY, A_MOD, A_PHASER, A_TONE, A_PITCH, A_CRUSH, A_MIAW
};
enum RvChar { RV_HALL, RV_ROOM, RV_PLATE, RV_SPRING, RV_GATE, RV_REVERSE, RV_EARLY };
enum DlChar { DL_MONO, DL_STEREO, DL_PINGPONG, DL_TAPE };
enum MdChar { MD_CHORUS, MD_ENSEMBLE, MD_FLANGE, MD_VIBRATO, MD_TREMOLO };
enum TnChar { TN_LP, TN_BP, TN_HP, TN_WAH };

/** Where one macro knob lands. */
enum MacroTarget { MA0, MA1, MA2, MA3, MB0, MB1, MB2, MB3, MNONE = 255 };

//: How many macro knobs the panel has. Three are named by the program table and
//: the rest are worked out below from what the running algorithm actually reads
//: -- not hand-authored into forty-eight program records, which would be two
//: hundred and forty decisions drifting out of step with the algorithms the
//: first time one of them changed.
//:
//: Eight, because eight is what a program can have: four in each block. Most
//: programs do not fill that -- sixty-nine of the hundred and six have four real
//: parameters and eleven have three -- but twenty-two have all eight, and until
//: there were knobs for them those were simply unreachable. A knob reading "--"
//: on a program that has nothing for it costs nothing; a parameter with no knob
//: cannot be got at.
static const int kMacros = 8;

/** What slot `i` of algorithm `alg` means, or NULL if that algorithm never
    reads it.

    Taken from the blocks themselves rather than invented: ReverbFx reads p[0]
    as size, p[1] as decay, p[2] as tone and p[3] as its character's own extra;
    DelayFx reads p[3] as stereo spread; ToneFx and CrushFx read only three and
    the fourth is genuinely nothing. NULL is what makes a knob say "--" instead
    of pretending to control something. */
inline const char* slotName(uint8_t alg, int i) {
    if (i < 0 || i > 3) return NULL;
    switch (alg) {
        case A_REVERB: { static const char* n[4] = {"SIZE", "DECAY", "TONE", "SHAPE"};
                         return n[i]; }
        case A_DELAY:  { static const char* n[4] = {"TIME", "FEEDBK", "TONE", "SPREAD"};
                         return n[i]; }
        case A_MOD:    { static const char* n[4] = {"RATE", "DEPTH", "VOICES", "TONE"};
                         return n[i]; }
        case A_PHASER: { static const char* n[4] = {"RATE", "DEPTH", "FEEDBK", "STAGES"};
                         return n[i]; }
        case A_TONE:   { static const char* n[4] = {"FREQ", "Q", "ENV", NULL};
                         return n[i]; }
        case A_PITCH:  { static const char* n[4] = {"PITCH", "MIX", "FEEDBK", "WINDOW"};
                         return n[i]; }
        case A_CRUSH:  { static const char* n[4] = {"BITS", "RATE", "TONE", NULL};
                         return n[i]; }
        case A_MIAW:   { static const char* n[4] = {"P1", "P2", "P3", NULL};
                         return n[i]; }
        default:       return NULL;
    }
}

/** What the module and the read-out know about the running program. */
struct Patch {
	int index;
	const char* name;
	const char* mac[kMacros];
	uint8_t tgt[kMacros];
	uint8_t absMask;     // bit i: macro i is the control itself, not a trim
	uint8_t algA, chrA;
	uint8_t algB, chrB;
	float a[4];
	float b[4];

	Patch() : index(0), name("--"), absMask(0), algA(A_NONE), chrA(0),
	          algB(A_NONE), chrB(0) {
		for (int i = 0; i < kMacros; i++) { mac[i] = "--"; tgt[i] = MNONE; }
		for (int i = 0; i < 4; i++) {
			a[i] = 0.5f;
			b[i] = 0.5f;
		}
	}

	/** Folds one macro into whichever block parameter it drives.
	 *
	 * On the DSP99 bank a macro is a trim about the program's own setting: the
	 * knob centred is exactly what that number is, and the knob at either end
	 * still reaches the parameter's limit. That is what keeps the twenty-six
	 * numbers the sheet calls "plate" twenty-six different plates rather than
	 * one plate with the knobs in twenty-six identical positions.
	 *
	 * On the seven dedicated boards a macro is the board's own knob, absolute,
	 * because there FEEDBACK means feedback and nothing else. */
	inline void setMacro(int i, float v) {
		if (i < 0 || i >= kMacros) return;
		uint8_t t = tgt[i];
		if (t >= 8)
			return;
		float* slot = (t < 4) ? &a[t] : &b[t - 4];
		*slot = ((absMask >> i) & 1) ? clamp(v, 0.f, 1.f)
		                             : clamp(*slot + (v - 0.5f) * 2.f, 0.f, 1.f);
	}
};

/** Everything a block needs that is not a parameter. */
struct BankCtx {
	float sr;
	float sampleTime;
	float aux;
	float tapSec;
	bool auxConnected;
	bool auxGate;

	BankCtx() : sr(44100.f), sampleTime(1.f / 44100.f), aux(0.f), tapSec(0.f),
	            auxConnected(false), auxGate(false) {}
};

// ---------------------------------------------------------------------------
// Reverb: one tank, seven characters.
//
// A Schroeder/Freeverb topology -- pre-delay, four damped combs per channel,
// four allpasses per channel -- with the comb lengths scaled by SIZE and the
// character deciding what happens around it: a dispersion chain in front for
// the spring, taps instead of a tank for early reflections, a mirrored read for
// the reverse, an envelope door for the gate.

struct ReverbFx {
	DelayLine pre;
	Comb comb[8];
	Allpass ap[8];
	Ap1 disp[8];
	DelayLine rev;
	OnePole hpL, hpR;
	Follower env;
	Smooth preSm;

	int revPos, revLen;
	float gateEnv, gateHold;
	// control rate
	int chr;
	float preSamples, gateHoldTime, wetTrim;

	ReverbFx() : revPos(0), revLen(2000), gateEnv(0.f), gateHold(0.f),
	             chr(RV_HALL), preSamples(0.f), gateHoldTime(0.1f), wetTrim(1.f) {}

	void init() {
		// Freeverb's tuning, in samples at 44.1 kHz, scaled by SIZE up to 2x and
		// by the sample rate up to 192 kHz.
		for (int i = 0; i < 8; i++)
			comb[i].init((int) (0.0367f * 2.05f * MAX_SR));
		for (int i = 0; i < 8; i++)
			ap[i].init((int) (0.0127f * 1.45f * MAX_SR));
		pre.init((int) (0.2f * MAX_SR));
		rev.init((int) (0.5f * MAX_SR));
	}

	void clear() {
		for (int i = 0; i < 8; i++) {
			comb[i].clear();
			ap[i].clear();
			disp[i].clear();
		}
		pre.clear();
		rev.clear();
		hpL.clear();
		hpR.clear();
		env.clear();
		revPos = 0;
		gateEnv = gateHold = 0.f;
		preSm.snap(0.f);
	}

	void setSampleRate(float sr) {
		hpL.setCutoff(60.f, sr);
		hpR.setCutoff(60.f, sr);
		env.set(3.f, 90.f, sr);
		preSm.setTime(50.f, sr);
	}

	void setParams(int character, const float p[4], const BankCtx& c) {
		chr = character;
		float size = clamp(p[0], 0.f, 1.f);
		float decay = clamp(p[1], 0.f, 1.f);
		float tone = clamp(p[2], 0.f, 1.f);
		float aux = clamp(p[3], 0.f, 1.f);

		static const float combTune[8] = {
			1116.f, 1188.f, 1277.f, 1356.f, 1422.f, 1491.f, 1557.f, 1617.f
		};
		static const float apTune[4] = {556.f, 441.f, 341.f, 225.f};

		float scale, fbMin, fbMax, apG;
		switch (chr) {
			case RV_ROOM:    scale = lerp(0.22f, 0.85f, size); fbMin = 0.40f; fbMax = 0.86f; apG = 0.50f; break;
			case RV_PLATE:   scale = lerp(0.30f, 1.05f, size); fbMin = 0.55f; fbMax = 0.93f; apG = 0.64f; break;
			case RV_SPRING:  scale = lerp(0.24f, 0.62f, size); fbMin = 0.50f; fbMax = 0.91f; apG = 0.58f; break;
			case RV_EARLY:   scale = lerp(0.30f, 1.20f, size); fbMin = 0.00f; fbMax = 0.00f; apG = 0.50f; break;
			case RV_GATE:
			case RV_REVERSE: scale = lerp(0.35f, 1.10f, size); fbMin = 0.50f; fbMax = 0.90f; apG = 0.54f; break;
			default:         scale = lerp(0.55f, 2.00f, size); fbMin = 0.55f; fbMax = 0.945f; apG = 0.50f; break;
		}
		float fb = clamp(lerp(fbMin, fbMax, decay), 0.f, 0.95f);
		float dampFc = lerp(1100.f, 13000.f, tone);
		float srScale = c.sr / 44100.f;

		for (int i = 0; i < 8; i++) {
			float extra = (i >= 4) ? 23.f : 0.f;   // the stereo spread, in samples
			comb[i].dl = clamp((combTune[i] + extra) * srScale * scale,
			                   4.f, comb[i].d.maxDelay());
			comb[i].fb = fb;
			comb[i].damp.setCutoff(std::min(dampFc, c.sr * 0.45f), c.sr);
			float apScale = (chr == RV_PLATE) ? 1.4f : 1.f;
			ap[i].dl = clamp((apTune[i & 3] + ((i >= 4) ? 11.f : 0.f)) * srScale * apScale,
			                 4.f, ap[i].d.maxDelay());
			ap[i].g = apG;
			// The spring's dispersion: eight allpasses spread across the tank's
			// passband, so an impulse leaves as a chirp.
			disp[i].setFreq(600.f + 180.f * (float) i, c.sr);
		}

		preSamples = aux * 0.15f * c.sr;
		if (chr == RV_GATE)
			gateHoldTime = lerp(0.02f, 0.6f, aux);
		if (chr == RV_REVERSE) {
			revLen = (int) clamp(lerp(0.08f, 0.5f, aux) * c.sr, 64.f, (float) (rev.size() - 1));
			if (revPos >= revLen)
				revPos = 0;
		}
		wetTrim = (chr == RV_EARLY) ? 1.0f : 0.65f;
	}

	void process(const BankCtx& c, float inL, float inR, float& outL, float& outR) {
		float mono = 0.5f * (inL + inR);

		pre.write(mono);
		float x = pre.read(preSm.process(preSamples));

		if (chr == RV_EARLY) {
			// A pattern of discrete reflections, no tank at all. The taps are
			// prime-ish ratios of the pre-delay line so they do not comb.
			static const float tap[8] = {
				0.0043f, 0.0071f, 0.0113f, 0.0157f, 0.0211f, 0.0263f, 0.0317f, 0.0389f
			};
			static const float gain[8] = {
				0.90f, -0.76f, 0.62f, -0.54f, 0.44f, -0.37f, 0.29f, -0.23f
			};
			// SIZE stretches the pattern; the comb tuning already carries it.
			float stretch = 0.4f + 1.6f * (comb[0].dl / (2200.f * c.sr / 44100.f));
			float l = 0.f, r = 0.f;
			for (int i = 0; i < 8; i++) {
				float v = pre.read(tap[i] * c.sr * stretch);
				if (i & 1)
					r += v * gain[i];
				else
					l += v * gain[i];
			}
			outL = clamp(hpL.hp(l * 0.5f), -10.f, 10.f);
			outR = clamp(hpR.hp(r * 0.5f), -10.f, 10.f);
			return;
		}

		if (chr == RV_SPRING) {
			for (int i = 0; i < 8; i++)
				x = disp[i].process(x);
		}

		if (chr == RV_REVERSE) {
			// Write forwards, read the mirror image of the write position, and
			// window the seam so the loop point is a swell rather than a click.
			rev.put(revPos, x);
			int rp = revLen - 1 - revPos;
			float w = 1.f - std::fabs(2.f * (float) revPos / (float) revLen - 1.f);
			x = rev.at(rp) * w;
			if (++revPos >= revLen)
				revPos = 0;
		}

		float l = 0.f, r = 0.f;
		for (int i = 0; i < 4; i++)
			l += comb[i].process(x);
		for (int i = 4; i < 8; i++)
			r += comb[i].process(x);
		l *= 0.25f;
		r *= 0.25f;
		for (int i = 0; i < 4; i++)
			l = ap[i].process(l);
		for (int i = 4; i < 8; i++)
			r = ap[i].process(r);

		if (chr == RV_GATE) {
			// The door. Open while there is something to reverberate, held for a
			// moment after, then shut fast -- which is the effect.
			float e = env.process(mono);
			if (e > 0.35f || c.auxGate) {
				gateEnv = 1.f;
				gateHold = gateHoldTime;
			}
			else if (gateHold > 0.f) {
				gateHold -= c.sampleTime;
			}
			else {
				gateEnv -= c.sampleTime / 0.015f;
				if (gateEnv < 0.f)
					gateEnv = 0.f;
			}
			l *= gateEnv;
			r *= gateEnv;
		}

		outL = clamp(hpL.hp(l * wetTrim), -10.f, 10.f);
		outR = clamp(hpR.hp(r * wetTrim), -10.f, 10.f);
	}
};

// ---------------------------------------------------------------------------
// Delay: mono, stereo, ping-pong and tape.

struct DelayFx {
	DelayLine line[2];
	OnePole damp[2];
	DCBlocker dc[2];
	Smooth tsm[2];
	Lfo wow, flutter;
	float fbL, fbR;
	// control rate
	int chr;
	float tSamp[2], fb, sat;
	float wowDepth, flutDepth;

	DelayFx() : fbL(0.f), fbR(0.f), chr(DL_STEREO), fb(0.4f), sat(1.f),
	            wowDepth(0.f), flutDepth(0.f) {
		tSamp[0] = tSamp[1] = 1000.f;
	}

	void init() {
		line[0].init((int) (1.02f * MAX_SR));
		line[1].init((int) (1.02f * MAX_SR));
	}

	void clear() {
		for (int i = 0; i < 2; i++) {
			line[i].clear();
			damp[i].clear();
			dc[i].clear();
			tsm[i].snap(tSamp[i]);
		}
		wow.reset();
		flutter.reset();
		fbL = fbR = 0.f;
	}

	void setSampleRate(float sr) {
		dc[0].setSampleRate(sr);
		dc[1].setSampleRate(sr);
		tsm[0].setTime(80.f, sr);
		tsm[1].setTime(80.f, sr);
	}

	void setParams(int character, const float p[4], const BankCtx& c) {
		chr = character;
		// 10 ms .. 1 s, log. A locked TAP clock replaces the knob.
		float t = 0.01f * std::pow(100.f, clamp(p[0], 0.f, 1.f));
		if (c.tapSec > 0.f)
			t = clamp(c.tapSec, 0.01f, 1.f);
		float spread = clamp(p[3], 0.f, 1.f);
		float ratio = (chr == DL_MONO) ? 1.f : lerp(1.f, 0.5f, spread);
		tSamp[0] = clamp(t * c.sr, 4.f, line[0].maxDelay());
		tSamp[1] = clamp(t * ratio * c.sr, 4.f, line[1].maxDelay());
		if (chr == DL_PINGPONG)
			tSamp[1] = tSamp[0];

		fb = clamp(p[1], 0.f, 1.f) * ((chr == DL_TAPE) ? 1.15f : 1.02f);
		float tone = clamp(p[2], 0.f, 1.f);
		float fc = (chr == DL_TAPE) ? lerp(900.f, 6500.f, tone)
		                            : lerp(1400.f, 16000.f, tone);
		damp[0].setCutoff(std::min(fc, c.sr * 0.45f), c.sr);
		damp[1].setCutoff(std::min(fc, c.sr * 0.45f), c.sr);
		sat = (chr == DL_TAPE) ? 1.6f : 0.6f;

		if (chr == DL_TAPE) {
			wow.setFreq(0.6f, c.sr);
			flutter.setFreq(7.3f, c.sr);
			wowDepth = 0.0016f * c.sr * (0.3f + 0.7f * (1.f - tone));
			flutDepth = 0.00018f * c.sr;
		}
		else {
			wowDepth = flutDepth = 0.f;
		}
	}

	void process(const BankCtx& c, float inL, float inR, float& outL, float& outR) {
		wow.step();
		flutter.step();
		float w = wow.sine() * wowDepth + flutter.sine() * flutDepth;

		float dL = tsm[0].process(tSamp[0]);
		float dR = tsm[1].process(tSamp[1]);
		float yL = line[0].read(dL + w);
		float yR = line[1].read(dR - w);

		float srcL = (chr == DL_MONO) ? 0.5f * (inL + inR) : inL;
		float srcR = (chr == DL_MONO) ? 0.5f * (inL + inR) : inR;

		float fL, fR;
		if (chr == DL_PINGPONG) {
			// One input, then the repeats walk across.
			fL = yR;
			fR = yL;
			srcR = 0.f;
			srcL = 0.5f * (inL + inR);
		}
		else {
			fL = yL;
			fR = yR;
		}

		float wL = srcL + fb * fL;
		float wR = srcR + fb * fR;
		wL = 5.f * tanhApprox(wL * 0.2f * sat) / sat;
		wR = 5.f * tanhApprox(wR * 0.2f * sat) / sat;
		line[0].write(sanitize(damp[0].lp(dc[0].process(wL))));
		line[1].write(sanitize(damp[1].lp(dc[1].process(wR))));

		outL = clamp(yL, -10.f, 10.f);
		outR = clamp(yR, -10.f, 10.f);
	}
};

// ---------------------------------------------------------------------------
// Modulation: chorus, ensemble, flanger, vibrato, tremolo. One modulated short
// delay covers all but the last, which is the same LFO with no delay at all.

struct ModFx {
	DelayLine line[2];
	OnePole tone[2];
	Lfo lfo;
	float fbL, fbR;
	// control rate
	int chr;
	float base, depth, fb, dryGain, wetGain, shape;

	ModFx() : fbL(0.f), fbR(0.f), chr(MD_CHORUS), base(200.f), depth(50.f),
	          fb(0.f), dryGain(1.f), wetGain(0.7f), shape(0.f) {}

	void init() {
		line[0].init((int) (0.06f * MAX_SR));
		line[1].init((int) (0.06f * MAX_SR));
	}

	void clear() {
		line[0].clear();
		line[1].clear();
		tone[0].clear();
		tone[1].clear();
		lfo.reset();
		fbL = fbR = 0.f;
	}

	void setSampleRate(float sr) {
		tone[0].setCutoff(std::min(14000.f, sr * 0.45f), sr);
		tone[1].setCutoff(std::min(14000.f, sr * 0.45f), sr);
	}

	void setParams(int character, const float p[4], const BankCtx& c) {
		chr = character;
		float rate = clamp(p[0], 0.f, 1.f);
		float dep = clamp(p[1], 0.f, 1.f);
		float third = clamp(p[2], 0.f, 1.f);
		float tn = clamp(p[3], 0.f, 1.f);

		switch (chr) {
			case MD_FLANGE:
				lfo.setFreq(0.03f * std::pow(200.f, rate), c.sr);
				base = 0.0008f * c.sr;
				depth = dep * 0.0045f * c.sr;
				fb = -0.95f + 1.9f * third;       // through zero, both signs
				dryGain = 1.f;
				wetGain = 0.9f;
				break;
			case MD_VIBRATO:
				lfo.setFreq(0.05f * std::pow(200.f, rate), c.sr);
				base = 0.004f * c.sr;
				depth = dep * 0.0035f * c.sr;
				fb = 0.f;
				dryGain = 0.f;
				wetGain = 1.f;
				break;
			case MD_TREMOLO:
				lfo.setFreq(0.1f * std::pow(200.f, rate), c.sr);
				base = 4.f;
				depth = 0.f;
				fb = 0.f;
				dryGain = 1.f;
				wetGain = dep;
				break;
			case MD_ENSEMBLE:
				lfo.setFreq(0.05f * std::pow(120.f, rate), c.sr);
				base = 0.011f * c.sr;
				depth = dep * 0.005f * c.sr;
				fb = 0.f;
				dryGain = 1.f;
				wetGain = 0.55f;
				break;
			default:   // MD_CHORUS
				lfo.setFreq(0.05f * std::pow(120.f, rate), c.sr);
				base = 0.009f * c.sr + third * 0.006f * c.sr;
				depth = dep * 0.005f * c.sr;
				fb = 0.f;
				dryGain = 1.f;
				wetGain = 0.75f;
				break;
		}
		shape = third;
		float fc = lerp(2000.f, 16000.f, tn);
		tone[0].setCutoff(std::min(fc, c.sr * 0.45f), c.sr);
		tone[1].setCutoff(std::min(fc, c.sr * 0.45f), c.sr);
	}

	void process(const BankCtx& c, float inL, float inR, float& outL, float& outR) {
		lfo.step();

		if (chr == MD_TREMOLO) {
			// SHAPE bends the sine towards a square, which is where a tremolo
			// stops being a wobble and becomes a gate.
			float s = lfo.sine();
			s = lerp(s, tanhApprox(s * 6.f), shape);
			float g = 1.f - wetGain * 0.5f * (1.f - s);
			outL = clamp(inL * g, -10.f, 10.f);
			outR = clamp(inR * g, -10.f, 10.f);
			return;
		}

		float mL = lfo.sine();
		float mR = lfo.sine(0.25f);
		if (chr == MD_ENSEMBLE) {
			mL = 0.6f * lfo.sine() + 0.4f * lfo.sine(0.37f);
			mR = 0.6f * lfo.sine(0.5f) + 0.4f * lfo.sine(0.83f);
		}

		float wL = line[0].read(base + depth * mL);
		float wR = line[1].read(base + depth * mR);

		line[0].write(sanitize(5.f * tanhApprox(tone[0].lp(inL + fb * wL) * 0.2f)));
		line[1].write(sanitize(5.f * tanhApprox(tone[1].lp(inR + fb * wR) * 0.2f)));

		outL = clamp(inL * dryGain + wL * wetGain, -10.f, 10.f);
		outR = clamp(inR * dryGain + wR * wetGain, -10.f, 10.f);
	}
};

// ---------------------------------------------------------------------------
// Phaser: four, six or eight first-order allpasses per channel, swept.

struct PhaserFx {
	Ap1 st[2][8];
	Lfo lfo;
	float fbL, fbR;
	// control rate
	int stages;
	float loHz, hiHz, fb, depth, mix;

	PhaserFx() : fbL(0.f), fbR(0.f), stages(6), loHz(200.f), hiHz(2000.f),
	             fb(0.3f), depth(1.f), mix(0.5f) {}

	void init() {}

	void clear() {
		for (int ch = 0; ch < 2; ch++)
			for (int i = 0; i < 8; i++)
				st[ch][i].clear();
		lfo.reset();
		fbL = fbR = 0.f;
	}

	void setSampleRate(float sr) { (void) sr; }

	void setParams(int character, const float p[4], const BankCtx& c) {
		(void) character;
		lfo.setFreq(0.03f * std::pow(300.f, clamp(p[0], 0.f, 1.f)), c.sr);
		depth = clamp(p[1], 0.f, 1.f);
		fb = clamp(p[2], 0.f, 1.f) * 0.85f;
		int s = (int) clamp(std::floor(clamp(p[3], 0.f, 0.999f) * 3.f), 0.f, 2.f);
		stages = 4 + 2 * s;
		loHz = 180.f;
		hiHz = lerp(1400.f, 5000.f, depth);
		mix = 0.5f;
	}

	void process(const BankCtx& c, float inL, float inR, float& outL, float& outR) {
		lfo.step();
		float mL = 0.5f + 0.5f * lfo.sine();
		float mR = 0.5f + 0.5f * lfo.sine(0.25f);
		float fL = loHz * std::pow(hiHz / loHz, mL);
		float fR = loHz * std::pow(hiHz / loHz, mR);

		// The stages are identical, so the coefficient is solved once per channel
		// and copied -- eight tangents a sample would be the expensive part of
		// the whole module otherwise.
		float tL = std::tan((float) M_PI * clamp(fL, 5.f, c.sr * 0.45f) / c.sr);
		float tR = std::tan((float) M_PI * clamp(fR, 5.f, c.sr * 0.45f) / c.sr);
		float aL = (tL - 1.f) / (tL + 1.f);
		float aR = (tR - 1.f) / (tR + 1.f);

		float vL = inL + fb * fbL;
		float vR = inR + fb * fbR;
		for (int i = 0; i < stages; i++) {
			st[0][i].a = aL;
			st[1][i].a = aR;
			vL = st[0][i].process(vL);
			vR = st[1][i].process(vR);
		}
		fbL = sanitize(clamp(vL, -10.f, 10.f));
		fbR = sanitize(clamp(vR, -10.f, 10.f));

		outL = clamp(inL * (1.f - mix) + vL * mix, -10.f, 10.f);
		outR = clamp(inR * (1.f - mix) + vR * mix, -10.f, 10.f);
	}
};

// ---------------------------------------------------------------------------
// Tone: a filter, and the envelope-swept version of it.

struct ToneFx {
	Svf f[2];
	Follower env;
	// control rate
	int chr;
	float baseHz, q, envAmt;

	ToneFx() : chr(TN_LP), baseHz(1000.f), q(0.7f), envAmt(0.f) {}

	void init() {}

	void clear() {
		f[0].clear();
		f[1].clear();
		env.clear();
	}

	void setSampleRate(float sr) { env.set(4.f, 120.f, sr); }

	void setParams(int character, const float p[4], const BankCtx& c) {
		chr = character;
		baseHz = 80.f * std::pow(150.f, clamp(p[0], 0.f, 1.f));   // 80 Hz .. 12 kHz
		q = lerp(0.6f, 12.f, clamp(p[1], 0.f, 1.f));
		envAmt = clamp(p[2], 0.f, 1.f) * ((chr == TN_WAH) ? 4.f : 2.f);
		// A static filter is solved here and left alone; only the swept one pays
		// for a tangent per sample.
		f[0].set(baseHz, q, c.sr);
		f[1].set(baseHz, q, c.sr);
	}

	void process(const BankCtx& c, float inL, float inR, float& outL, float& outR) {
		if (envAmt > 0.f) {
			float e = env.process(0.5f * (inL + inR)) * 0.2f;
			float fc = clamp(baseHz * std::pow(2.f, envAmt * e), 20.f, c.sr * 0.45f);
			f[0].set(fc, q, c.sr);
			f[1].g = f[0].g;
			f[1].k = f[0].k;
			f[1].a1 = f[0].a1;
			f[1].a2 = f[0].a2;
			f[1].a3 = f[0].a3;
		}
		f[0].process(inL);
		f[1].process(inR);
		float l, r;
		switch (chr) {
			case TN_BP:  l = f[0].bp; r = f[1].bp; break;
			case TN_HP:  l = f[0].hp; r = f[1].hp; break;
			case TN_WAH: l = f[0].bp; r = f[1].bp; break;
			default:     l = f[0].lp; r = f[1].lp; break;
		}
		outL = clamp(l, -10.f, 10.f);
		outR = clamp(r, -10.f, 10.f);
	}
};

// ---------------------------------------------------------------------------
// Pitch: two taps reading a buffer at a rate the write head does not share,
// crossfaded so the wrap is a dip rather than a click. The cheap granular
// shifter -- warbly on sustained tone, which is the sound the DSP99's
// "transposition" programs have.

struct PitchFx {
	DelayLine line[2];
	float gphase;
	// control rate
	float ratio, win, fbAmt, mix;

	PitchFx() : gphase(0.f), ratio(1.f), win(4000.f), fbAmt(0.f), mix(1.f) {}

	void init() {
		line[0].init((int) (0.22f * MAX_SR));
		line[1].init((int) (0.22f * MAX_SR));
	}

	void clear() {
		line[0].clear();
		line[1].clear();
		gphase = 0.f;
	}

	void setSampleRate(float sr) { (void) sr; }

	void setParams(int character, const float p[4], const BankCtx& c) {
		(void) character;
		// -12 .. +12 semitones, with a detente at unity in the middle.
		float semis = (clamp(p[0], 0.f, 1.f) - 0.5f) * 24.f;
		if (std::fabs(semis) < 0.25f)
			semis = 0.f;
		ratio = std::pow(2.f, semis / 12.f);
		win = clamp(lerp(0.02f, 0.12f, clamp(p[3], 0.f, 1.f)) * c.sr,
		            64.f, line[0].maxDelay() * 0.5f);
		fbAmt = clamp(p[2], 0.f, 1.f) * 0.85f;
		mix = clamp(p[1], 0.f, 1.f);
	}

	void process(const BankCtx& c, float inL, float inR, float& outL, float& outR) {
		(void) c;
		gphase += (1.f - ratio) / win;
		gphase -= std::floor(gphase);

		float d1 = gphase * win;
		float g2 = gphase + 0.5f;
		g2 -= std::floor(g2);
		float d2 = g2 * win;
		float w1 = 0.5f - 0.5f * std::cos(2.f * (float) M_PI * gphase);
		float w2 = 1.f - w1;

		float yL = line[0].read(d1) * w1 + line[0].read(d2) * w2;
		float yR = line[1].read(d1) * w1 + line[1].read(d2) * w2;

		line[0].write(sanitize(clamp(inL + yL * fbAmt, -12.f, 12.f)));
		line[1].write(sanitize(clamp(inR + yR * fbAmt, -12.f, 12.f)));

		outL = clamp(lerp(inL, yL, mix), -10.f, 10.f);
		outR = clamp(lerp(inR, yR, mix), -10.f, 10.f);
	}
};

// ---------------------------------------------------------------------------
// Crush: the lo-fi partner of the tone programs. Bit depth and sample rate,
// with the reconstruction filter always in circuit -- the dedicated MW
// Bitcrusher at program 104 is the one that lets you take it out.

struct CrushFx {
	OnePole lp[2];
	float phase, hL, hR;
	// control rate
	float rateHz, step;

	CrushFx() : phase(0.f), hL(0.f), hR(0.f), rateHz(12000.f), step(0.02f) {}

	void init() {}

	void clear() {
		lp[0].clear();
		lp[1].clear();
		phase = hL = hR = 0.f;
	}

	void setSampleRate(float sr) { (void) sr; }

	void setParams(int character, const float p[4], const BankCtx& c) {
		(void) character;
		float bits = lerp(2.f, 12.f, clamp(p[0], 0.f, 1.f));
		step = 10.f / std::pow(2.f, bits);
		rateHz = 400.f * std::pow(120.f, clamp(p[1], 0.f, 1.f));
		float fc = std::min(lerp(1200.f, 12000.f, clamp(p[2], 0.f, 1.f)), c.sr * 0.45f);
		lp[0].setCutoff(fc, c.sr);
		lp[1].setCutoff(fc, c.sr);
	}

	void process(const BankCtx& c, float inL, float inR, float& outL, float& outR) {
		phase += rateHz / c.sr;
		if (phase >= 1.f) {
			phase -= std::floor(phase);
			hL = std::floor(clamp(inL, -5.f, 5.f) / step + 0.5f) * step;
			hR = std::floor(clamp(inR, -5.f, 5.f) / step + 0.5f) * step;
		}
		outL = clamp(lp[0].lp(hL), -10.f, 10.f);
		outR = clamp(lp[1].lp(hR), -10.f, 10.f);
	}
};

// ---------------------------------------------------------------------------
/** The whole bank. One instance of each algorithm: no program in the sheet
    needs two reverbs or two delays, so a two-block chain is served by seven
    units and a switch. */
struct Dsp99 {
	ReverbFx reverb;
	DelayFx delay;
	ModFx mod;
	PhaserFx phaser;
	ToneFx tone;
	PitchFx pitch;
	CrushFx crush;

	void init() {
		reverb.init();
		delay.init();
		mod.init();
		phaser.init();
		tone.init();
		pitch.init();
		crush.init();
	}

	void setSampleRate(float sr) {
		reverb.setSampleRate(sr);
		delay.setSampleRate(sr);
		mod.setSampleRate(sr);
		phaser.setSampleRate(sr);
		tone.setSampleRate(sr);
		pitch.setSampleRate(sr);
		crush.setSampleRate(sr);
	}

	void clearAll() {
		reverb.clear();
		delay.clear();
		mod.clear();
		phaser.clear();
		tone.clear();
		pitch.clear();
		crush.clear();
	}

	void clear(int alg) {
		switch (alg) {
			case A_REVERB: reverb.clear(); break;
			case A_DELAY:  delay.clear(); break;
			case A_MOD:    mod.clear(); break;
			case A_PHASER: phaser.clear(); break;
			case A_TONE:   tone.clear(); break;
			case A_PITCH:  pitch.clear(); break;
			case A_CRUSH:  crush.clear(); break;
			default: break;
		}
	}

	void setBlock(int alg, int chr, const float p[4], const BankCtx& c) {
		switch (alg) {
			case A_REVERB: reverb.setParams(chr, p, c); break;
			case A_DELAY:  delay.setParams(chr, p, c); break;
			case A_MOD:    mod.setParams(chr, p, c); break;
			case A_PHASER: phaser.setParams(chr, p, c); break;
			case A_TONE:   tone.setParams(chr, p, c); break;
			case A_PITCH:  pitch.setParams(chr, p, c); break;
			case A_CRUSH:  crush.setParams(chr, p, c); break;
			default: break;
		}
	}

	void runBlock(int alg, const BankCtx& c, float inL, float inR,
	              float& outL, float& outR) {
		switch (alg) {
			case A_REVERB: reverb.process(c, inL, inR, outL, outR); break;
			case A_DELAY:  delay.process(c, inL, inR, outL, outR); break;
			case A_MOD:    mod.process(c, inL, inR, outL, outR); break;
			case A_PHASER: phaser.process(c, inL, inR, outL, outR); break;
			case A_TONE:   tone.process(c, inL, inR, outL, outR); break;
			case A_PITCH:  pitch.process(c, inL, inR, outL, outR); break;
			case A_CRUSH:  crush.process(c, inL, inR, outL, outR); break;
			default:       outL = inL; outR = inR; break;
		}
	}

	void setParams(const Patch& p, const BankCtx& c) {
		setBlock(p.algA, p.chrA, p.a, c);
		if (p.algB != A_NONE)
			setBlock(p.algB, p.chrB, p.b, c);
	}

	void process(const Patch& p, const BankCtx& c, float inL, float inR,
	             float& outL, float& outR) {
		float l = inL, r = inR;
		runBlock(p.algA, c, l, r, outL, outR);
		if (p.algB != A_NONE) {
			l = outL;
			r = outR;
			runBlock(p.algB, c, l, r, outL, outR);
		}
	}
};

// ---------------------------------------------------------------------------
// The program table.
//
// One row per band of the DSP99 sheet, plus one per dedicated board. Every row
// gives the two blocks, their parameters at the low and high end of the band,
// the three macro names and where each macro lands. A program number inside a
// band interpolates: 0, 1 and 2 are three genuinely different small halls, not
// the same one three times.

struct ProgRange {
	uint8_t lo, hi;
	const char* name;
	uint8_t algA, chrA;
	float a0[4], a1[4];
	uint8_t algB, chrB;
	float b0[4], b1[4];
	const char* mac[3];
	uint8_t tgt[3];
	uint8_t absMask;
};

static const ProgRange PROGRAMS[] = {
	// -- reverbs ------------------------------------------------------------
	{  0,  2, "SMALL HALL",   A_REVERB, RV_HALL,    {0.26f, 0.42f, 0.55f, 0.06f}, {0.34f, 0.52f, 0.58f, 0.10f},
	   A_NONE, 0, {0,0,0,0}, {0,0,0,0}, {"SIZE", "DECAY", "TONE"}, {MA0, MA1, MA2}, 0x0 },
	{  3,  5, "MEDIUM HALL",  A_REVERB, RV_HALL,    {0.44f, 0.58f, 0.52f, 0.12f}, {0.54f, 0.66f, 0.56f, 0.18f},
	   A_NONE, 0, {0,0,0,0}, {0,0,0,0}, {"SIZE", "DECAY", "TONE"}, {MA0, MA1, MA2}, 0x0 },
	{  6,  8, "LARGE HALL",   A_REVERB, RV_HALL,    {0.66f, 0.72f, 0.48f, 0.20f}, {0.80f, 0.80f, 0.52f, 0.28f},
	   A_NONE, 0, {0,0,0,0}, {0,0,0,0}, {"SIZE", "DECAY", "TONE"}, {MA0, MA1, MA2}, 0x0 },
	{  9,  9, "CHURCH",       A_REVERB, RV_HALL,    {0.90f, 0.88f, 0.34f, 0.30f}, {0.90f, 0.88f, 0.34f, 0.30f},
	   A_NONE, 0, {0,0,0,0}, {0,0,0,0}, {"SIZE", "DECAY", "TONE"}, {MA0, MA1, MA2}, 0x0 },
	{ 10, 12, "SMALL ROOM",   A_REVERB, RV_ROOM,    {0.12f, 0.28f, 0.62f, 0.02f}, {0.22f, 0.38f, 0.66f, 0.04f},
	   A_NONE, 0, {0,0,0,0}, {0,0,0,0}, {"SIZE", "DECAY", "TONE"}, {MA0, MA1, MA2}, 0x0 },
	{ 13, 15, "MEDIUM ROOM",  A_REVERB, RV_ROOM,    {0.30f, 0.44f, 0.58f, 0.05f}, {0.40f, 0.54f, 0.62f, 0.08f},
	   A_NONE, 0, {0,0,0,0}, {0,0,0,0}, {"SIZE", "DECAY", "TONE"}, {MA0, MA1, MA2}, 0x0 },
	{ 16, 18, "LARGE ROOM",   A_REVERB, RV_ROOM,    {0.50f, 0.58f, 0.54f, 0.09f}, {0.64f, 0.68f, 0.58f, 0.14f},
	   A_NONE, 0, {0,0,0,0}, {0,0,0,0}, {"SIZE", "DECAY", "TONE"}, {MA0, MA1, MA2}, 0x0 },
	{ 19, 19, "HALL",         A_REVERB, RV_HALL,    {0.58f, 0.70f, 0.54f, 0.15f}, {0.58f, 0.70f, 0.54f, 0.15f},
	   A_NONE, 0, {0,0,0,0}, {0,0,0,0}, {"SIZE", "DECAY", "TONE"}, {MA0, MA1, MA2}, 0x0 },
	{ 20, 26, "METAL PLATE",  A_REVERB, RV_PLATE,   {0.30f, 0.52f, 0.80f, 0.02f}, {0.78f, 0.86f, 0.46f, 0.10f},
	   A_NONE, 0, {0,0,0,0}, {0,0,0,0}, {"SIZE", "DECAY", "TONE"}, {MA0, MA1, MA2}, 0x0 },
	{ 27, 29, "SPRING VERB",  A_REVERB, RV_SPRING,  {0.28f, 0.50f, 0.60f, 0.01f}, {0.55f, 0.70f, 0.52f, 0.04f},
	   A_NONE, 0, {0,0,0,0}, {0,0,0,0}, {"SIZE", "DWELL", "TONE"}, {MA0, MA1, MA2}, 0x0 },
	{ 30, 35, "REVERB GATE",  A_REVERB, RV_GATE,    {0.35f, 0.60f, 0.60f, 0.15f}, {0.72f, 0.82f, 0.55f, 0.75f},
	   A_NONE, 0, {0,0,0,0}, {0,0,0,0}, {"SIZE", "DECAY", "HOLD"}, {MA0, MA1, MA3}, 0x0 },
	{ 36, 39, "REVERSE",      A_REVERB, RV_REVERSE, {0.32f, 0.50f, 0.58f, 0.25f}, {0.70f, 0.76f, 0.55f, 0.75f},
	   A_NONE, 0, {0,0,0,0}, {0,0,0,0}, {"SIZE", "DECAY", "LEN"}, {MA0, MA1, MA3}, 0x0 },
	{ 40, 43, "EARLY REFL",   A_REVERB, RV_EARLY,   {0.22f, 0.35f, 0.65f, 0.03f}, {0.70f, 0.55f, 0.60f, 0.12f},
	   A_NONE, 0, {0,0,0,0}, {0,0,0,0}, {"SIZE", "SHAPE", "TONE"}, {MA0, MA1, MA2}, 0x0 },
	{ 44, 47, "ATMOSPHERE",   A_REVERB, RV_HALL,    {0.82f, 0.86f, 0.30f, 0.30f}, {1.00f, 0.94f, 0.42f, 0.55f},
	   A_MOD, MD_ENSEMBLE, {0.10f, 0.40f, 0.00f, 0.60f}, {0.22f, 0.70f, 0.00f, 0.60f},
	   {"SIZE", "DECAY", "DRIFT"}, {MA0, MA1, MB1}, 0x0 },
	{ 48, 48, "STADIUM",      A_REVERB, RV_HALL,    {1.00f, 0.92f, 0.40f, 0.60f}, {1.00f, 0.92f, 0.40f, 0.60f},
	   A_NONE, 0, {0,0,0,0}, {0,0,0,0}, {"SIZE", "DECAY", "TONE"}, {MA0, MA1, MA2}, 0x0 },
	{ 49, 49, "AMBIENT FX",   A_REVERB, RV_HALL,    {0.95f, 0.95f, 0.30f, 0.35f}, {0.95f, 0.95f, 0.30f, 0.35f},
	   A_PITCH, 0, {0.75f, 0.45f, 0.40f, 0.60f}, {0.75f, 0.45f, 0.40f, 0.60f},
	   {"DECAY", "TONE", "SHIMR"}, {MA1, MA2, MB1}, 0x0 },

	// -- delays -------------------------------------------------------------
	{ 50, 52, "DELAY",        A_DELAY, DL_MONO,     {0.18f, 0.35f, 0.60f, 0.00f}, {0.35f, 0.50f, 0.55f, 0.00f},
	   A_NONE, 0, {0,0,0,0}, {0,0,0,0}, {"TIME", "FDBK", "TONE"}, {MA0, MA1, MA2}, 0x0 },
	{ 53, 55, "STEREO DLY",   A_DELAY, DL_STEREO,   {0.20f, 0.38f, 0.60f, 0.30f}, {0.40f, 0.55f, 0.55f, 0.70f},
	   A_NONE, 0, {0,0,0,0}, {0,0,0,0}, {"TIME", "FDBK", "TONE"}, {MA0, MA1, MA2}, 0x0 },
	{ 56, 58, "PING-PONG",    A_DELAY, DL_PINGPONG, {0.22f, 0.45f, 0.58f, 0.50f}, {0.42f, 0.62f, 0.52f, 0.80f},
	   A_NONE, 0, {0,0,0,0}, {0,0,0,0}, {"TIME", "FDBK", "TONE"}, {MA0, MA1, MA2}, 0x0 },
	{ 59, 59, "ECHO",         A_DELAY, DL_TAPE,     {0.30f, 0.55f, 0.42f, 0.60f}, {0.30f, 0.55f, 0.42f, 0.60f},
	   A_NONE, 0, {0,0,0,0}, {0,0,0,0}, {"TIME", "FDBK", "AGE"}, {MA0, MA1, MA2}, 0x0 },

	// -- modulation ---------------------------------------------------------
	{ 60, 63, "CHORUS",       A_MOD, MD_CHORUS,     {0.18f, 0.35f, 0.20f, 0.70f}, {0.45f, 0.75f, 0.60f, 0.70f},
	   A_NONE, 0, {0,0,0,0}, {0,0,0,0}, {"RATE", "DEPTH", "WIDTH"}, {MA0, MA1, MA2}, 0x0 },
	{ 64, 64, "VIBRATO",      A_MOD, MD_VIBRATO,    {0.30f, 0.45f, 0.00f, 0.75f}, {0.30f, 0.45f, 0.00f, 0.75f},
	   A_NONE, 0, {0,0,0,0}, {0,0,0,0}, {"RATE", "DEPTH", "TONE"}, {MA0, MA1, MA3}, 0x0 },
	{ 65, 65, "TREMOLO",      A_MOD, MD_TREMOLO,    {0.35f, 0.60f, 0.20f, 0.80f}, {0.35f, 0.60f, 0.20f, 0.80f},
	   A_NONE, 0, {0,0,0,0}, {0,0,0,0}, {"RATE", "DEPTH", "SHAPE"}, {MA0, MA1, MA2}, 0x0 },
	{ 66, 69, "FLANGER",      A_MOD, MD_FLANGE,     {0.15f, 0.40f, 0.62f, 0.75f}, {0.50f, 0.85f, 0.92f, 0.75f},
	   A_NONE, 0, {0,0,0,0}, {0,0,0,0}, {"RATE", "DEPTH", "FDBK"}, {MA0, MA1, MA2}, 0x0 },

	// -- phase and tone -----------------------------------------------------
	{ 70, 73, "PHASER",       A_PHASER, 0,          {0.20f, 0.60f, 0.30f, 0.00f}, {0.50f, 0.90f, 0.70f, 1.00f},
	   A_NONE, 0, {0,0,0,0}, {0,0,0,0}, {"RATE", "DEPTH", "FDBK"}, {MA0, MA1, MA2}, 0x0 },
	{ 74, 74, "TONE LP",      A_TONE, TN_LP,        {0.60f, 0.25f, 0.00f, 0.00f}, {0.60f, 0.25f, 0.00f, 0.00f},
	   A_NONE, 0, {0,0,0,0}, {0,0,0,0}, {"FREQ", "RESO", "ENV"}, {MA0, MA1, MA2}, 0x0 },
	{ 75, 75, "TONE BP",      A_TONE, TN_BP,        {0.50f, 0.40f, 0.00f, 0.00f}, {0.50f, 0.40f, 0.00f, 0.00f},
	   A_NONE, 0, {0,0,0,0}, {0,0,0,0}, {"FREQ", "RESO", "ENV"}, {MA0, MA1, MA2}, 0x0 },
	{ 76, 76, "TONE HP",      A_TONE, TN_HP,        {0.40f, 0.30f, 0.00f, 0.00f}, {0.40f, 0.30f, 0.00f, 0.00f},
	   A_NONE, 0, {0,0,0,0}, {0,0,0,0}, {"FREQ", "RESO", "ENV"}, {MA0, MA1, MA2}, 0x0 },
	{ 77, 77, "WAH",          A_TONE, TN_WAH,       {0.18f, 0.45f, 0.65f, 0.00f}, {0.18f, 0.45f, 0.65f, 0.00f},
	   A_NONE, 0, {0,0,0,0}, {0,0,0,0}, {"FREQ", "RESO", "ENV"}, {MA0, MA1, MA2}, 0x0 },
	{ 78, 79, "LO-FI TONE",   A_TONE, TN_BP,        {0.45f, 0.45f, 0.00f, 0.00f}, {0.55f, 0.60f, 0.00f, 0.00f},
	   A_CRUSH, 0, {0.40f, 0.45f, 0.50f, 0.00f}, {0.22f, 0.28f, 0.40f, 0.00f},
	   {"FREQ", "BITS", "RATE"}, {MA0, MB0, MB1}, 0x0 },

	// -- reverb, plus ------------------------------------------------------
	{ 80, 81, "VERB+CHORUS",  A_REVERB, RV_HALL,    {0.55f, 0.68f, 0.52f, 0.10f}, {0.68f, 0.78f, 0.50f, 0.15f},
	   A_MOD, MD_CHORUS, {0.20f, 0.45f, 0.30f, 0.70f}, {0.32f, 0.65f, 0.40f, 0.70f},
	   {"SIZE", "DECAY", "MOD"}, {MA0, MA1, MB1}, 0x0 },
	{ 82, 83, "VERB+FLANGE",  A_REVERB, RV_HALL,    {0.55f, 0.68f, 0.52f, 0.10f}, {0.68f, 0.78f, 0.50f, 0.15f},
	   A_MOD, MD_FLANGE, {0.16f, 0.50f, 0.70f, 0.70f}, {0.28f, 0.75f, 0.82f, 0.70f},
	   {"SIZE", "DECAY", "FLNG"}, {MA0, MA1, MB1}, 0x0 },
	{ 84, 85, "VERB+PHASER",  A_REVERB, RV_HALL,    {0.55f, 0.68f, 0.52f, 0.10f}, {0.68f, 0.78f, 0.50f, 0.15f},
	   A_PHASER, 0, {0.18f, 0.60f, 0.35f, 0.50f}, {0.30f, 0.80f, 0.55f, 0.50f},
	   {"SIZE", "DECAY", "PHSR"}, {MA0, MA1, MB1}, 0x0 },
	{ 86, 87, "VERB+TONE",    A_REVERB, RV_HALL,    {0.55f, 0.68f, 0.52f, 0.10f}, {0.68f, 0.78f, 0.50f, 0.15f},
	   A_TONE, TN_LP, {0.62f, 0.35f, 0.00f, 0.00f}, {0.50f, 0.55f, 0.00f, 0.00f},
	   {"SIZE", "DECAY", "FREQ"}, {MA0, MA1, MB0}, 0x0 },

	// -- delay, plus --------------------------------------------------------
	{ 88, 89, "DELAY+VERB",   A_DELAY, DL_STEREO,   {0.24f, 0.42f, 0.58f, 0.40f}, {0.38f, 0.55f, 0.54f, 0.60f},
	   A_REVERB, RV_HALL, {0.55f, 0.70f, 0.50f, 0.10f}, {0.66f, 0.78f, 0.48f, 0.14f},
	   {"TIME", "FDBK", "VERB"}, {MA0, MA1, MB1}, 0x0 },
	{ 90, 90, "DELAY+GATE",   A_DELAY, DL_STEREO,   {0.26f, 0.45f, 0.58f, 0.45f}, {0.26f, 0.45f, 0.58f, 0.45f},
	   A_REVERB, RV_GATE, {0.55f, 0.75f, 0.58f, 0.35f}, {0.55f, 0.75f, 0.58f, 0.35f},
	   {"TIME", "FDBK", "HOLD"}, {MA0, MA1, MB3}, 0x0 },
	{ 91, 91, "DELAY+REVRS",  A_DELAY, DL_STEREO,   {0.26f, 0.45f, 0.58f, 0.45f}, {0.26f, 0.45f, 0.58f, 0.45f},
	   A_REVERB, RV_REVERSE, {0.55f, 0.70f, 0.55f, 0.45f}, {0.55f, 0.70f, 0.55f, 0.45f},
	   {"TIME", "FDBK", "LEN"}, {MA0, MA1, MB3}, 0x0 },
	{ 92, 93, "DELAY+CHORUS", A_DELAY, DL_STEREO,   {0.22f, 0.42f, 0.58f, 0.40f}, {0.36f, 0.55f, 0.54f, 0.60f},
	   A_MOD, MD_CHORUS, {0.20f, 0.45f, 0.30f, 0.70f}, {0.32f, 0.70f, 0.45f, 0.70f},
	   {"TIME", "FDBK", "MOD"}, {MA0, MA1, MB1}, 0x0 },
	{ 94, 95, "DELAY+FLANGE", A_DELAY, DL_STEREO,   {0.22f, 0.42f, 0.58f, 0.40f}, {0.36f, 0.55f, 0.54f, 0.60f},
	   A_MOD, MD_FLANGE, {0.16f, 0.50f, 0.70f, 0.70f}, {0.28f, 0.78f, 0.84f, 0.70f},
	   {"TIME", "FDBK", "FLNG"}, {MA0, MA1, MB1}, 0x0 },
	{ 96, 97, "DELAY+PHASE",  A_DELAY, DL_STEREO,   {0.22f, 0.42f, 0.58f, 0.40f}, {0.36f, 0.55f, 0.54f, 0.60f},
	   A_PHASER, 0, {0.18f, 0.60f, 0.35f, 0.50f}, {0.30f, 0.82f, 0.58f, 0.50f},
	   {"TIME", "FDBK", "PHSR"}, {MA0, MA1, MB1}, 0x0 },
	{ 98, 98, "DELAY+PITCH",  A_DELAY, DL_STEREO,   {0.26f, 0.48f, 0.56f, 0.50f}, {0.26f, 0.48f, 0.56f, 0.50f},
	   A_PITCH, 0, {0.75f, 0.60f, 0.35f, 0.45f}, {0.75f, 0.60f, 0.35f, 0.45f},
	   {"TIME", "FDBK", "PITCH"}, {MA0, MA1, MB0}, 0x0 },

	// -- the dedicated MiaW boards -----------------------------------------
	{  99,  99, "ECHOMATIC",  A_MIAW, MW_ECHOMATIC, {0.40f, 0.35f, 0.85f, 0.00f}, {0.40f, 0.35f, 0.85f, 0.00f},
	   A_NONE, 0, {0,0,0,0}, {0,0,0,0}, {"TIME", "FDBK", "LEVEL"}, {MA0, MA1, MA2} , 0x7 },
	{ 100, 100, "LITTLE ANGEL", A_MIAW, MW_ANGEL,   {0.30f, 0.50f, 0.00f, 0.00f}, {0.30f, 0.50f, 0.00f, 0.00f},
	   A_NONE, 0, {0,0,0,0}, {0,0,0,0}, {"SPEED", "DEPTH", "MODE"}, {MA0, MA1, MA2} , 0x7 },
	{ 101, 101, "SPRING TANK", A_MIAW, MW_SPRING,   {0.35f, 0.55f, 0.50f, 0.00f}, {0.35f, 0.55f, 0.50f, 0.00f},
	   A_NONE, 0, {0,0,0,0}, {0,0,0,0}, {"DRIVE", "DWELL", "TONE"}, {MA0, MA1, MA2} , 0x7 },
	{ 102, 102, "DISTORTION+", A_MIAW, MW_DISTPLUS, {0.45f, 0.50f, 0.60f, 0.00f}, {0.45f, 0.50f, 0.60f, 0.00f},
	   A_NONE, 0, {0,0,0,0}, {0,0,0,0}, {"DIST", "LEVEL", "TONE"}, {MA0, MA1, MA2} , 0x7 },
	{ 103, 103, "TALK FUNNY",  A_MIAW, MW_TALKFUNNY, {0.45f, 0.40f, 0.00f, 0.00f}, {0.45f, 0.40f, 0.00f, 0.00f},
	   A_NONE, 0, {0,0,0,0}, {0,0,0,0}, {"FREQ", "FM INT", "MODE"}, {MA0, MA1, MA2} , 0x7 },
	{ 104, 104, "BITCRUSHER",  A_MIAW, MW_BITCRUSH, {0.40f, 0.60f, 0.70f, 0.00f}, {0.40f, 0.60f, 0.70f, 0.00f},
	   A_NONE, 0, {0,0,0,0}, {0,0,0,0}, {"LEVEL", "BITS", "RATE"}, {MA0, MA1, MA2} , 0x7 },
	{ 105, 105, "4011 RING",   A_MIAW, MW_RING4011, {0.50f, 0.40f, 0.60f, 0.00f}, {0.50f, 0.40f, 0.60f, 0.00f},
	   A_NONE, 0, {0,0,0,0}, {0,0,0,0}, {"CARR", "BIAS", "TONE"}, {MA0, MA1, MA2} , 0x7 },
};

static const int NUM_RANGES = (int) (sizeof(PROGRAMS) / sizeof(PROGRAMS[0]));
static const int NUM_PROGRAMS = 106;

/** Resolves a program number to its blocks and macro names, interpolating
    across the band it falls in. Called on a program change, never per sample. */
inline void programAt(int n, Patch& out) {
	n = clamp(n, 0, NUM_PROGRAMS - 1);
	const ProgRange* row = &PROGRAMS[0];
	for (int i = 0; i < NUM_RANGES; i++) {
		if (n >= (int) PROGRAMS[i].lo && n <= (int) PROGRAMS[i].hi) {
			row = &PROGRAMS[i];
			break;
		}
	}
	float t = (row->hi > row->lo)
	          ? (float) (n - (int) row->lo) / (float) ((int) row->hi - (int) row->lo)
	          : 0.f;

	out.index = n;
	out.name = row->name;
	out.algA = row->algA;
	out.chrA = row->chrA;
	out.algB = row->algB;
	out.chrB = row->chrB;
	for (int i = 0; i < 4; i++) {
		out.a[i] = lerp(row->a0[i], row->a1[i], t);
		out.b[i] = lerp(row->b0[i], row->b1[i], t);
	}
	out.absMask = row->absMask;
	for (int i = 0; i < 3; i++) {
		out.mac[i] = row->mac[i];
		out.tgt[i] = row->tgt[i];
	}

	// The three the program names are the three the sheet gave it. The rest are
	// whatever that program still has and is not already spending: every slot
	// its two blocks really read, in order, skipping the ones already claimed.
	// A program whose second block is A_NONE -- most of them -- simply runs out,
	// and the knobs that run out say "--".
	bool taken[8] = {false, false, false, false, false, false, false, false};
	for (int i = 0; i < 3; i++)
		if (out.tgt[i] < 8) taken[out.tgt[i]] = true;

	int m = 3;
	for (int slot = 0; slot < 8 && m < kMacros; slot++) {
		if (taken[slot]) continue;
		uint8_t alg = (slot < 4) ? out.algA : out.algB;
		const char* nm = slotName(alg, slot & 3);
		if (!nm) continue;                 // that block does not read it
		out.tgt[m] = (uint8_t) slot;
		out.mac[m] = nm;
		m++;
	}
	for (; m < kMacros; m++) {
		out.tgt[m] = MNONE;
		out.mac[m] = "--";
	}
}

} // namespace divfx

// Contagion's knobs, kept where the sound is. Self-contained (no Rack) so tests/Contagion can check
// every curve against the firmware.
//
// The Virus's knobs are not motorised: load a program and the knobs stay where they were, and the
// first one you touch makes its parameter jump to the knob's position. That is the unit's own
// behaviour and a panel on a screen has no reason to keep it, so the module reads the sound the unit
// has loaded and moves each knob to match -- on screen only. The firmware is never told: a knob
// reaching it through the A/D converter is a knob someone turned, which marks the program edited
// and puts the parameter on the LCD. Touch a knob afterwards and it starts from the sound's value.
//
// What is needed is, for each knob, which byte of the edit buffer it edits (it depends on the
// selected LFO, oscillator, effect and filter) and the curve from the knob's A/D code to that byte's
// value. Both were measured by turning every knob on the real firmware in every context
// (tests/Contagion/test_machine.cpp checks them against it, so a firmware update that moves them is
// caught). The edit buffer is the 256 bytes at the bottom of the microcontroller's external RAM:
// page A in bytes 0-127 and page B in 128-255, parameter numbers as in the owner's manual's tables.
#pragma once
#include <cmath>
#include <cstdint>

namespace vc {

struct KnobSync {
	/** What the panel has selected, which decides what several knobs edit. */
	struct Context {
		int lfo = 0;                   // 0-2 for LFO 1-3, 3 for MOD
		int osc = 0;                   // 0-2
		int fx = 1;                    // 0 distortion, 1 phaser, 2 chorus
		bool filt1 = false, filt2 = true;   // SEL 1 and SEL 2, the filters RESO and ENV AMT edit
	};

	/** The shape of a knob's law, A/D code (0-255) to the parameter's value. */
	enum Curve {
		NONE,       // the knob edits nothing in the edit buffer here
		LINEAR,     // code / 2
		CENTRED,    // a flat spot at the middle: code 120-136 is the parameter's 64
		SEMITONE,   // the pitch knobs' own, uneven, curve
		WAVE,       // wave select, 0-63
		MODE3,      // oscillator 3's mode, 0-67
		TYPE        // a short list: code / 24
	};

	struct Map {
		int byte;                      // page * 128 + parameter number
		Curve curve;
		Map() : byte(-1), curve(NONE) {}
		Map(int b, Curve c) : byte(b), curve(c) {}
	};

	static constexpr int POTS = 32;
	static constexpr int LAMP_SEL1 = 59, LAMP_SEL2 = 60;   // LED[] indices the context is read from
	static constexpr int EDIT_BUFFER = 256;

	/** The sound's clock tempo, page B parameter 16: 0-127 for 63-190 BPM. There is no knob for it on the
	    unit (it is a menu item), so the panel's BPM knob sets it with a SysEx parameter change to the
	    single-mode edit buffer (part $40), the form the firmware answers. */
	static constexpr int TEMPO_BYTE = 128 + 16, TEMPO_BPM_AT_ZERO = 63;
	static void tempoMessage(int value, uint8_t out[11]) {
		const uint8_t m[11] = { 0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x71, 0x40, 16, uint8_t(value < 0 ? 0 : (value > 127 ? 127 : value)), 0xF7 };
		for (int i = 0; i < 11; i++) out[i] = m[i];
	}

	/** The context from the unit's lamps: `lit(i)` is the brightness of LED[i]. A group with no single
	    lamp lit leaves its default, which is what the unit shows before it has settled. */
	template <class F>
	static Context context(F&& lit) {
		Context cx;
		auto one = [&](int first, int n) {
			int found = -1;
			for (int i = 0; i < n; i++)
				if (lit(first + i) > 0.5f) { if (found >= 0) return -1; found = i; }
			return found;
		};
		const int lfo = one(1, 4), osc = one(12, 3), fx = one(17, 3);
		if (lfo >= 0) cx.lfo = lfo;
		if (osc >= 0) cx.osc = osc;
		if (fx >= 0) cx.fx = fx;
		cx.filt1 = lit(LAMP_SEL1) > 0.5f;
		cx.filt2 = lit(LAMP_SEL2) > 0.5f;
		return cx;
	}

	/** The knob's value for an A/D code: what the firmware makes of it. */
	static int value(Curve c, int code) {
		if (code < 0) code = 0;
		if (code > 255) code = 255;
		switch (c) {
		case LINEAR: return code >> 1;
		case CENTRED:
			if (code < 120) return int(std::floor(code * 64.0 / 120.0 + 0.5));
			if (code <= 136) return 64;
			return int(std::floor(64.0 + (code - 136) * 63.0 / 119.0 + 0.5));
		case SEMITONE: {
			// Measured: the knob's code every 8 steps; the law between is close enough to straight.
			static const uint8_t T[33] = { 16, 20, 24, 28, 31, 35, 39, 41, 45, 49, 52, 53, 57, 58, 61, 63, 64,
				65, 67, 70, 71, 75, 76, 80, 84, 88, 90, 94, 98, 100, 104, 108, 112 };
			const int i = code >> 3, f = code & 7;
			if (i >= 32) return T[32];
			return int(std::floor(T[i] + (T[i + 1] - T[i]) * f / 8.0 + 0.5));
		}
		case WAVE: return int(std::floor(code * 0.2476 + 0.5));
		case MODE3: {
			const int v = int(std::floor(31 + (code - 128) * 0.2812 + 0.5));
			return v < 0 ? 0 : v;
		}
		case TYPE: return int(std::floor(code / 24.0 + 0.5));
		default: return 0;
		}
	}

	/** An A/D code that gives parameter value v: the middle of the run of codes that do, or the
	    nearest value's if none does. */
	static int code(Curve c, int v) {
		int lo = -1, hi = -1, best = 0, bestErr = 1 << 20;
		for (int k = 0; k < 256; k++) {
			const int e = std::abs(value(c, k) - v);
			if (e < bestErr) { bestErr = e; best = k; lo = hi = k; }
			else if (e == bestErr) hi = k;
		}
		(void)best;
		return (lo + hi) / 2;
	}

	/** How far a knob and the sound may disagree before the knob is moved: the curves are fitted to
	    measurements, and a knob the user has just set is within a step of what it sent. */
	static int tolerance(Curve c) { return c == SEMITONE ? 2 : 1; }

	/** Which byte a knob (panel order, 0-31) edits now, and by which law. `buf` is the edit buffer:
	    WAVE SEL/PW edits wave select when the oscillator's shape is all the way to wave, the pulse
	    width when it is at saw or past, and nothing between. */
	static Map map(int pot, const Context& cx, const uint8_t* buf) {
		const int osc = cx.osc < 0 ? 0 : (cx.osc > 2 ? 2 : cx.osc);
		switch (pot) {
		case 0: {                                          // LFO RATE
			static const int B[3] = { 67, 79, 128 + 7 };
			return cx.lfo >= 0 && cx.lfo < 3 ? Map(B[cx.lfo], LINEAR) : Map();
		}
		case 1: return Map(116, LINEAR);                    // DLY/REV TIME
		case 2: return Map(osc == 1 ? 22 : 17, CENTRED);    // SHAPE (oscillator 3 has none: it edits oscillator 1's)
		case 3: {                                          // WAVE SEL/PW
			if (osc == 2) return Map(128 + 41, MODE3);
			const int shape = buf[osc == 1 ? 22 : 17];
			if (shape == 0) return Map(osc == 1 ? 24 : 19, WAVE);
			if (shape >= 64) return Map(osc == 1 ? 23 : 18, LINEAR);
			return Map();
		}
		case 4: { static const int B[3] = { 20, 25, 128 + 43 }; return Map(B[osc], SEMITONE); }   // SEMITONE
		case 5: return Map(osc == 2 ? 128 + 44 : 26, LINEAR);   // DETUNE 2/3
		case 6: return Map(27, LINEAR);                     // FM AMOUNT
		case 7: {                                          // effects INTENSITY
			static const Map M[3] = { Map(128 + 101, LINEAR), Map(128 + 89, CENTRED), Map(107, LINEAR) };
			return M[cx.fx < 0 ? 0 : (cx.fx > 2 ? 2 : cx.fx)];
		}
		case 8: return Map(33, CENTRED);                    // OSC BAL
		case 9: return Map(34, LINEAR);                     // SUB OSC
		case 10: return Map(36, CENTRED);                   // OSC VOL
		case 11: return Map(37, LINEAR);                    // NOISE
		case 12: return Map(38, LINEAR);                    // RING MOD
		case 13: {                                         // effects TYPE/MIX
			static const Map M[3] = { Map(128 + 100, TYPE), Map(128 + 85, LINEAR), Map(105, LINEAR) };
			return M[cx.fx < 0 ? 0 : (cx.fx > 2 ? 2 : cx.fx)];
		}
		case 14: return Map(118, LINEAR);                   // FDBK/DAMP
		case 15: return Map(113, LINEAR);                   // SEND
		case 19: return Map(40, LINEAR);                    // CUTOFF
		case 20: return Map(41, CENTRED);                   // CUTOFF 2
		case 21: return Map(cx.filt1 ? 42 : 43, LINEAR);    // RESO
		case 22: return Map(cx.filt1 ? 44 : 45, LINEAR);    // ENV AMT
		case 23: return Map(48, CENTRED);                   // FLT BAL
		case 24: return Map(54, LINEAR);                    // filter ATTACK, DECAY, SUSTAIN, RELEASE
		case 25: return Map(55, LINEAR);
		case 26: return Map(56, LINEAR);
		case 27: return Map(58, LINEAR);
		case 28: return Map(59, LINEAR);                    // amp ATTACK, DECAY, SUSTAIN, RELEASE
		case 29: return Map(60, LINEAR);
		case 30: return Map(61, LINEAR);
		case 31: return Map(63, LINEAR);
		default: return Map();                             // SOFT 1, SOFT 2 and VOLUME are not in the sound
		}
	}

	/** If knob `pot`, at A/D code `knobCode`, disagrees with the sound, the code it should be at;
	    otherwise -1. */
	static int resync(int pot, int knobCode, const Context& cx, const uint8_t* buf) {
		const Map m = map(pot, cx, buf);
		if (m.byte < 0 || m.curve == NONE) return -1;
		const int want = buf[m.byte];
		if (std::abs(value(m.curve, knobCode) - want) <= tolerance(m.curve)) return -1;
		return code(m.curve, want);
	}
};

} // namespace vc

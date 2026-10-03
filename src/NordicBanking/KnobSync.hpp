// Nordic Banking's knobs, kept where the sound is. Self-contained (no Rack) so tests/NordicBanking can
// check every law against the firmware.
//
// The Nord Lead 2X's knobs are not motorised: select another program and the knobs stay where they
// were, and the first one you touch makes its parameter jump to the knob's position. That is the
// unit's own behaviour and a panel on a screen has no reason to keep it, so the module reads the
// program the unit is editing and moves each knob to match -- on screen only. The firmware is never
// told: a knob reaching it through the A/D converter is a knob someone turned, and it marks the
// program edited. Touch a knob afterwards and it starts from the sound's value.
//
// What is needed is, for each knob, which byte of the edit buffer it edits and the law from the knob's
// A/D code to that byte's value. Both were measured by turning every channel on the real firmware
// (tests/NordicBanking/test_machine.cpp checks them against it, so a firmware update that moves them
// is caught). The edit buffer is the 66-byte program of the owner's manual ("Patch Dump Format") at
// PanelMap.hpp's EDIT_BUFFER, in the 68331's RAM.
#pragma once
#include <cmath>
#include <cstdint>
#include <cstdlib>

#include "PanelMap.hpp"

namespace nb {

struct KnobSync {
	/** The shape of a knob's law, A/D code (0-255) to the parameter's value. */
	enum Curve {
		NONE,       // the knob is not in the program (master volume)
		LINEAR,     // code / 2, 0-127
		SEMITONE,   // OSC 2's semitone knob: 1-120 with a flat spot at 48, 60 and 72
		FINE        // OSC 2's fine tune: one higher below the middle, a flat spot at 64
	};

	static Curve curve(int knob) {
		const int o = KNOBS[knob].offset;
		return o < 0 ? NONE : (o == 0 ? SEMITONE : (o == 1 ? FINE : LINEAR));
	}

	/** The parameter's value for an A/D code: what the firmware makes of it. */
	static int value(Curve c, int code) {
		if (code < 0) code = 0;
		if (code > 255) code = 255;
		switch (c) {
		case LINEAR: return code >> 1;
		case SEMITONE: {
			// Measured: the value every 8 codes; the law between is close enough to straight.
			static const uint8_t T[33] = { 1, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 44, 48, 50, 54, 58, 60,
				63, 67, 71, 73, 77, 81, 85, 89, 93, 97, 101, 105, 109, 113, 117, 120 };
			const int i = code >> 3, f = code & 7;
			if (i >= 32) return T[32];
			return int(std::floor(T[i] + (T[i + 1] - T[i]) * f / 8.0 + 0.5));
		}
		case FINE: return code <= 125 ? (code >> 1) + 1 : (code <= 129 ? 64 : code >> 1);
		default: return 0;
		}
	}

	/** An A/D code that gives parameter value v: the middle of the run of codes that do, or the nearest
	    value's if none does. */
	static int code(Curve c, int v) {
		int lo = -1, hi = -1, bestErr = 1 << 20;
		for (int k = 0; k < 256; k++) {
			const int e = std::abs(value(c, k) - v);
			if (e < bestErr) { bestErr = e; lo = hi = k; }
			else if (e == bestErr) hi = k;
		}
		return (lo + hi) / 2;
	}

	/** How far a knob and the sound may disagree before the knob is moved: the law is fitted to
	    measurements, and a knob the user has just set is within a step of what it sent. */
	static int tolerance(Curve c) { return c == SEMITONE ? 2 : 1; }

	/** If knob `knob` (panel order, 0-25), at A/D code `knobCode`, disagrees with the program in `buf` (the
	    66-byte edit buffer), the code it should be at; otherwise -1. */
	static int resync(int knob, int knobCode, const uint8_t* buf) {
		const Curve c = curve(knob);
		if (c == NONE) return -1;
		const int want = buf[KNOBS[knob].offset];
		if (std::abs(value(c, knobCode) - want) <= tolerance(c)) return -1;
		return code(c, want);
	}
};

} // namespace nb

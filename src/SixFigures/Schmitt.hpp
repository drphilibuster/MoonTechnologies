#pragma once
// SixFigures' 40106 core: the pot, capacitor and supply of the MiaW Day 1 "40106 Hex Oscillator Bank"
// (src/Cd40106.hpp is the chip and the astable law), mapped from the panel's RATE knob and CV.
//
// What the schematic fixes: per inverter a pot ("Value N") from output to input, a capacitor ("Value N",
// to ground) on the input, and an "In N" jack through a 1N4448 and 1k into the same node. What it does
// not fix, and is chosen here (tell the user): the capacitor values ("the course picks them"), the pot's
// end values and taper, and VDD (= VCC; +12 V is taken).
//
// RATE keeps its dial: at the default supply and the typical thresholds the pot is the resistor for
// which the *ideal* law, T = R C [ln((VDD-VN)/(VDD-VP)) + ln(VP/VN)], gives the frequency the knob has
// always printed (an exponential sweep over the range). The chip's own output resistance, delay,
// corner, VDD and whatever is injected then move the frequency off that, as they do on the board.
//
// No Rack dependency, so tests/ drives it bare.

#include "../Cd40106.hpp"

#include <cmath>

namespace sixfigures {
namespace schmitt {

//: ASSUMED: the course leaves "Value N" open. 68 nF puts the pot at 4.5 k for 4 kHz and 0.9 M for
//: 20 Hz (a 1M pot with a 1 k stop); 47 uF puts it at 3.3 k for 8 Hz and 0.52 M for 0.05 Hz.
static const double kCapAudio = 68e-9;
static const double kCapLfo   = 47e-6;
static const double kVddDefault = 12.0;

/** The ideal law's R C product per period at `vdd`, typical thresholds, nothing else. */
inline double idealPeriodPerRc(double vdd = kVddDefault) {
	return cd40106::timing(1.0, 1.0, vdd, cd40106::CORNER_TYP, false, false).period;
}

/** The pot, for knob in [0, 1] over [lo, hi] hertz, as an exponential sweep. */
inline double potResistance(double knob, bool lfo, double lo, double hi) {
	const double c = lfo ? kCapLfo : kCapAudio;
	const double f = lo * std::pow(hi / lo, knob);
	return idealPeriodPerRc() / (c * f);
}

struct Setting {
	double r;       // ohms, the pot
	double c;       // farads
	double vin;     // volts at the In jack (0 = nothing)
};

/** `cvVolts` is the CV after its amount knob. Default (board) response: the CV is the voltage at the
    In jack, through the 1N4448 and 1k, and does nothing until it is within a diode drop of the timing
    node's swing, then slows and finally stops the oscillator. The menu's 1 V/oct response is not on the
    board: an exponential converter scales the pot, R * 2^-CV, and nothing is injected. */
inline Setting setting(double knob, bool lfo, double lo, double hi, bool voltPerOct, double cvVolts) {
	Setting s;
	s.c = lfo ? kCapLfo : kCapAudio;
	s.r = potResistance(knob, lfo, lo, hi);
	s.vin = 0.0;
	if (voltPerOct) s.r *= std::pow(2.0, -cvVolts);
	else s.vin = cvVolts;
	return s;
}

} // namespace schmitt
} // namespace sixfigures

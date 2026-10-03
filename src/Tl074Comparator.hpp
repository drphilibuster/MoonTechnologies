#pragma once
// A TL074 quarter used as an open-loop comparator, as the Quad Logic Module and the 4066
// board use it: no feedback, (-) on a fixed reference, (+) on the signal.
//
// Source: TI SLOS080W, MiawResearch/datasheets/tl074.pdf, the TL07xC electrical table:
//
//   AOL  large-signal gain        200 V/mV typ (25 min)           -> 2e5
//   VOM  peak output swing        +-13.5 V typ at RL = 10k, VS = +-15 V
//                                 +-12 V min (10k), +-10 V min (RL >= 2k)
//
// Output stage as a Thevenin source: the saturated output is the rail less a headroom
// that grows with the current drawn. The two anchors are the typical swing at 10k
// (1.5 V below the rail at 1.35 mA) and the slope between the datasheet's two minimum
// swings (10k: 3 V down at 1.2 mA; 2k: 5 V down at 5 mA, i.e. 526 ohm). That gives
//
//     V_out(sat) = V_rail -/+ (0.79 V + 526 ohm * I_load)
//
// so an EMF of rail - 0.79 V behind 526 ohm. The linear region is 2e5 times the
// differential input, so it is about 55 microvolts wide at 11 V of swing.
//
// NOT modelled (datasheet silent or negligible): input offset (a few mV), input bias
// (65 pA), phase reversal below the common-mode range, slew and bandwidth (13 V/us and
// 3 MHz: far inside one 48 kHz sample for a 10 V step).
//
// No Rack dependency, so tests/ drives it bare.

namespace tl074 {

struct Comparator {
    double vcc = 12.0;                  // positive supply
    double vee = 12.0;                  // magnitude of the negative supply
    double headroom = 0.79;             // rail - saturated EMF, volts
    double rout = 526.0;                // ohms, behind the EMF
    double aol = 2.0e5;                 // open-loop gain

    double emfHigh() const { return vcc - headroom; }
    double emfLow() const { return -vee + headroom; }

    /** The open-circuit output (the Thevenin EMF) for the two inputs. */
    double emf(double vPlus, double vMinus) const {
        double e = aol * (vPlus - vMinus);
        double hi = emfHigh(), lo = emfLow();
        return e > hi ? hi : (e < lo ? lo : e);
    }
};

} // namespace tl074

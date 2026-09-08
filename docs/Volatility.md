# Volatility

A random-signal utility for VCV Rack 2, consolidating three circuits from Modular in
a Week's random day onto one panel, all three sharing a single clock.

Part of the [Moon Technologies](../README.md) plugin.

## What it is based on

- **NOISE** — the 4006 digital noise generator: a CD4006-style static shift
  register, wired here as 18 stages with two XOR feedback taps for a maximal-length
  sequence (the two-tap table for an 18-bit register gives taps at bit 18 and bit 11,
  counting from the input end). Clocked fast, the bitstream reads as broadband
  digital noise; clocked slow, the same bit held between edges reads as a random
  gate — one circuit, and RATE is the only thing that decides which.
- **SAMPLE & HOLD** — René Schmitz's *Yet Another Sample & Hold* (YASH).
- **RND GATE** — PHObos's random gate: a probability draw on every clock, either
  from the module's own RNG or from an external voltage compared against the same
  odds (the classic noise-plus-comparator random gate, given a patchable source).

## The shared clock

**RATE** (in the NOISE section, because NOISE's own character is what actually
depends on it) sets an internal clock from roughly 0.05 Hz up to 4 kHz, log-scaled.
**RATE CV** is 1 V/octave on top of it — the same convention the knob's own log
taper follows, so a pitch source drives the clock as a pitch. Patching
**CLOCK IN** overrides it — the module then follows CLOCK IN's edges
instead, Schmitt-triggered with the usual ~1 V/2 V hysteresis. **CLK** mirrors
whichever is active: the internal square wave, or a 1 ms retriggered pulse on each
external edge. Every section below reacts to the same edge unless its own jack is
patched.

## NOISE

| Control/jack | Role |
|---|---|
| **RATE** | The shared clock's rate (see above). |
| **RATE CV** | 1 V/octave on the clock, on top of RATE. Unattenuated — ±5 V is ±5 octaves. |
| **CLOCK IN** | Overrides RATE and RATE CV both. |
| **BITS** | Picks which contiguous 8 of the register's 18 bits DAC reads, as a window offset 0–10. |
| **BITS CV** | Adds to BITS, ±5 V covering the window's full travel. Sweeping it re-reads the same register through a different slice, so DAC's stepped CV changes character without the sequence itself changing. |
| **COLOR** | The colour of the module's continuous noise: white at full CCW, pink at centre, red at full CW, crossfading between neighbours. It moves NOISE **and** both normalled inputs together, which is the point — sampling red is a smooth random walk where sampling white is a jump, and RND GATE's Ext draw goes from a fresh coin flip to a drifting, correlated one. |
| **RND** | The register's own feedback bit each clock, as ±5 V — white-ish noise at an audio-rate clock, a random gate held between edges at a slow one. |
| **DAC** | The 8-bit window BITS selects, read as an unsigned value and scaled to a stepped 0–10 V random CV. A new step lands on every clock edge. |

## SAMPLE & HOLD

| Control/jack | Role |
|---|---|
| **SRC** | What gets sampled. Normalled to the module's own white noise (the same signal NOISE carries) when unpatched — polyphonic, so a patched multi-channel source samples every channel independently. |
| **TRIG** | When to sample. Normalled to the shared clock when unpatched. |
| **SLEW** | A trim that glides the stepped output rather than jumping to it — 0 is instant, full CW is about 250 ms to settle. |
| **S&H** | The held (and optionally slewed, optionally drooping) voltage. |

## RND GATE

| Control/jack | Role |
|---|---|
| **PROB** | The odds, 0–100%, that GATE goes high on the next shared clock edge. |
| **PROB CV** | Trim over the CV jack below it, added to PROB. |
| **SRC** | Int: the draw is the module's own RNG. Ext: the draw is the voltage at SRC IN, rescaled from ±5 V to 0–100% and compared against PROB the same way — a voltage-controlled coin flip. |
| **SRC IN** | The external source for SRC: Ext. Normalled to the module's own white noise when unpatched, so Ext mode still does something without a cable. |
| **LENGTH** | How much of the clock's own period a successful draw holds GATE high. Full CW (the default) latches until the next edge, so consecutive successes read as one unbroken high; anywhere below that the gate falls before the next edge, so each success is a separate, countable event. Never shorter than about 1 ms — a trigger something downstream can actually see. Bypassed in TOGGLE mode, which holds a state rather than a gate. |
| **GATE** | The random gate, 0/10 V, held for LENGTH of the clock period on a success — or see the TOGGLE menu option below. |
| **NOISE** | The module's continuous noise at whatever COLOR selects, the same signal SAMPLE & HOLD's SRC and RND GATE's SRC IN both normal to. |

## Voltage conventions

- CLOCK IN/CLK: 0/10 V gate convention, ~1 V (CLOCK IN) or 2 V (CLOCK IN, high
  threshold) Schmitt hysteresis; CLK retriggers a 1 ms pulse per external edge,
  or mirrors the internal square wave's own 50% duty.
- RND, SRC, SRC IN, NOISE: bipolar, roughly ±5 V. The three colours are matched
  by RMS rather than by peak, so COLOR changes the spectrum and not the level;
  pink and red have the higher crest factor and reach past ±10 V on about 0.05% of
  samples, held there to Rack's ±12 V rail.
- RATE CV: 1 V/octave, ±10 V accepted.
- BITS CV: bipolar, ±5 V covers the window's full 0–10 travel.
- DAC, S&H: unipolar/whatever SRC carries — DAC is fixed at
  0–10 V.
- GATE: 0/10 V.
- PROB CV: bipolar, ±5 V nominal through the CV amount trim.

## Context menu

- **Sample & hold droop** — the held voltage leaks back toward 0 V with a roughly
  4-second time constant, the way a real FET-input sample and hold does when the
  hold capacitor is not driving an ideal high-impedance load. Off (an ideal hold) by
  default.
- **Random gate: toggle mode** — GATE flips its state on a successful draw
  instead of going high for one clock period and low otherwise, useful for building
  slower on/off patterns out of a fast clock. Off by default.

## Polyphony

SAMPLE & HOLD is polyphonic, following SRC's channel count (falling back to 1
channel when SRC is unpatched and it is sampling the module's own mono white
noise). NOISE and RND GATE are monophonic — one shared shift register and one shared
probability draw, in keeping with the spec's framing of three sections around a
single clock rather than sixteen independent copies of each.

## Approximated or left out

- The LFSR's two feedback taps (bit 18, bit 11) are the standard two-tap maximal-
  length solution for an 18-bit register rather than a trace of the original 4006
  wiring, which is not preserved in the source material in enough detail to copy
  exactly; the resulting sequence is still full-length (2¹⁸−1 states) and passes the
  same "sounds like noise" test the original circuit was built for.
- RND GATE's "Ext" source and SAMPLE & HOLD's SRC both normal to the same internally
  generated noise rather than to the digital LFSR output, on the reading that a
  continuous source makes a more useful out-of-the-box default for both a sample and
  hold and a voltage-compared coin flip; the LFSR's own bit and DAC window remain
  available on RND and DAC for patching in by hand.
- That continuous noise source belongs to no source circuit — the 4006 is a digital
  noise generator, and the analogue source was invented so the two normalled inputs
  would have something useful to fall back on. COLOR fills it out rather than
  reproducing anything: pink is Paul Kellet's three-pole economy filter with its
  poles re-derived as corner frequencies so the −3 dB/octave slope holds at any
  sample rate rather than only at the 44.1 kHz his coefficients were published for,
  and red is a single integrator leaking at 20 Hz so it drifts without walking off
  as a DC offset.
- LENGTH shortens the gate but cannot make it longer than one clock period. A gate
  that outlived its own period would fill the next one, and a failed draw could no
  longer be heard as a gap — so a clock edge ends the previous draw's gate whatever
  the timer still holds. This matters on a clock that is speeding up, where the
  period measured from the last interval is longer than the one now running.

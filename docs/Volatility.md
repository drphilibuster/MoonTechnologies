# Volatility — noise, S&H & random

A random-signal utility for VCV Rack 2, consolidating three circuits from Modular in
a Week's random day onto one panel, all three sharing a single clock.

Part of the [Moon Technologies](../README.md) plugin.

## What it is based on

- **NOISE** — the 4006 digital noise generator: one CD4006B (`src/Cd4006.hpp`,
  from its datasheet) wired as an 18-stage shift register, closed through two XOR
  gates and an inverter as in Yves Usson's Random Eight Pole Gate Switch (Yusynth,
  2005; only its noise register is used, not the 4051 router): the bit entering
  is NOT(stage 18 XOR stage 5) XOR stage 9, on pins 13, 9 and 10, for a sequence
  of 2¹⁸ − 4 = 262,140 clocks. Clocked fast, the bitstream reads as broadband
  digital noise; clocked slow, the same bit held between edges reads as a random
  gate — one circuit, and RATE is the only thing that decides which. The register
  shifts on the clock's *falling* edge, as the datasheet has it (see the shared
  clock, below).
- **SAMPLE & HOLD** — René Schmitz's *Yet Another Sample & Hold* (YASH, 1999): an
  LF398 with a 1 nF hold capacitor, sampled by a ~3 µs pulse made from the trigger
  by a BC548 and three CD4093 gates. Modelled as the chip and the pulse circuit, not
  as an ideal instant sample — see "The YASH, and what the model does" below.
- **RND GATE** — PHObos's random gate: a probability draw on every clock, either
  from the module's own RNG or from an external voltage compared against the same
  odds (the classic noise-plus-comparator random gate, given a patchable source).

## The shared clock

**RATE** (in the NOISE section, because NOISE's own character is what actually
depends on it) sets an internal clock from roughly 0.05 Hz up to 4 kHz, log-scaled.
**RATE CV** is 1 V/octave on top of it — the same convention the knob's own log
taper follows, so a pitch source drives the clock as a pitch. Patching
**CLOCK IN** overrides it — the module then follows CLOCK IN's edges
instead, Schmitt-triggered with the usual 0.1 V/2 V hysteresis. **CLK** mirrors
whichever is active: the internal square wave, or a 1 ms retriggered pulse on each
external edge. Every section below reacts to the same rising edge unless its own
jack is patched, except the register: a CD4006 moves on the negative-going
transition of its clock, so RND and DAC change half a cycle after the edge the
sample & hold and the random gate use (at the end of the pulse, for an external
clock), and a sample & hold triggered by the clock reads a DAC value that has had
half a cycle to settle.

## NOISE

| Control/jack | Role |
|---|---|
| **RATE** | The shared clock's rate (see above). |
| **RATE CV** | 1 V/octave on the clock, on top of RATE. Unattenuated — ±5 V is ±5 octaves. |
| **CLOCK IN** | Overrides RATE and RATE CV both. |
| **BITS** | Picks which contiguous 8 of the register's 18 stages DAC reads, as a window offset 0–10. A real 4006 has no pins on most of those stages; reading them is this module's addition. |
| **BITS CV** | Adds to BITS, ±5 V covering the window's full travel. Sweeping it re-reads the same register through a different slice, so DAC's stepped CV changes character without the sequence itself changing. |
| **COLOR** | The colour of the module's continuous noise: white at full CCW, pink at centre, red at full CW, crossfading between neighbours. It moves NOISE **and** both normalled inputs together, which is the point — sampling red is a smooth random walk where sampling white is a jump, and RND GATE's Ext draw goes from a fresh coin flip to a drifting, correlated one. |
| **RND** | The register's own feedback bit each clock, as ±5 V — white-ish noise at an audio-rate clock, a random gate held between edges at a slow one. |
| **DAC** | The 8-bit window BITS selects, read as an unsigned value and scaled to a stepped 0–10 V random CV. A new step lands on every clock edge. |

## SAMPLE & HOLD

| Control/jack | Role |
|---|---|
| **SRC** | What gets sampled. Normalled to the module's own white noise (the same signal NOISE carries) when unpatched — polyphonic, so a patched multi-channel source samples every channel independently. |
| **TRIG** | When to sample. Normalled to the shared clock when unpatched — or, from the context menu, to the YASH's own free-running oscillator (RATE is then its pot). A rising edge above about 0.7 V starts a sample; a plug in the jack disconnects the oscillator, as the circuit's switch jack does. |
| **SLEW** | A trim that glides the stepped output rather than jumping to it — 0 is instant, full CW is about 250 ms to settle. |
| **S&H** | The held (and optionally slewed) voltage: what the LF398's hold capacitor carries, with its offset, hold step, feedthrough and (optionally) leakage droop. |

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

- CLOCK IN/CLK: 0/10 V gate convention, 0.1 V (CLOCK IN low threshold) / 2 V
  (CLOCK IN high threshold) Schmitt hysteresis; CLK retriggers a 1 ms pulse per external edge,
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

- **Sample & hold droop (LF398 leakage)** — the hold capacitor's leakage current: the
  held voltage creeps at I / 1 nF, linearly, rather than the old 4-second exponential
  back to 0 V (the LF398 datasheet has a leakage *current*, not a time constant; the old
  curve was not supported by it). 30 pA typical is 0.03 V/s at 25 °C, and the chip's own
  self-heating takes that to about 0.045 V/s. Off (an ideal hold) by default, as before. A
  patch saved with the old option on loads with it on, and now droops this way.
- **Sample & hold chip** — LF398 typical (default) or worst case (the datasheet's maxima:
  200 pA leakage, 7 mV offset, 2.5 mV hold step per 10 nF, 10 dB worse feedthrough,
  the CD4093's wide-threshold corner).
- **Droop direction** — which way the leakage pulls. The datasheet gives a magnitude only
  ("leakage current into hold capacitor"), so up (into the capacitor) is the default.
- **Sample & hold trigger when TRIG is empty** — the shared clock (default, as before), or
  the YASH's own free-running CD4093 oscillator: 470 nF and a 1 Meg pot plus 2k2,
  3.7 Hz to 1.5 kHz. RATE is the pot (clockwise is faster, linear taper) and RATE CV is
  1 V/octave on the loop resistance, neither of which the circuit has. Plugging TRIG
  disconnects it, as on the board.
- **Random gate: toggle mode** — GATE flips its state on a successful draw
  instead of going high for one clock period and low otherwise, useful for building
  slower on/off patterns out of a fast clock. Off by default.

## The YASH, and what the model does

SAMPLE & HOLD is Schmitz's schematic (<https://schmitzbits.de/yash.png>) solved, around the
TI LF398-N datasheet (SNOSBI3C):

- **The trigger path.** The rising edge goes through 4k7 into a BC548, which flips gate 1
  (1/4 CD4093, inputs tied) once the base is above about 0.70 V (0.68 V coming back). A
  470 pF / 10k differentiator follows gate 1; gate 2 is a Schmitt trigger, so the pulse lasts
  from the node jumping above VP until it decays below VN: 4.8 µs · ln(14.7 V / VN) =
  **3.4 µs** with the datasheet's typical tied-input VN of 7.3 V at 15 V (2.0 µs to 5.4 µs
  across the datasheet's min/max VN) — Schmitz's "3 µs pulses". Gate 3 and the 5k6 / 1k8
  divider give the LF398 a 3.5 V logic high. The pulse starts three gate delays (65 ns each)
  after the edge. Retriggering inside a pulse extends it; the trigger falling inside a pulse
  cuts it short; the falling edge alone fires nothing.
- **Sample time.** The edge's position *inside* the audio sample is kept: exactly for the
  internal clock and for the oscillator, and by straight-line interpolation between the two
  samples for a patched jack (the true shape of an edge between samples is not knowable).
  The LF398 therefore reads SRC a few microseconds after the trigger, not at a sample
  boundary — each held value is a slightly lagged input, and the result does not depend on
  the sample rate.
- **Acquisition.** A 0.6 µs front-end lag, then an output stage that charges the 1 nF through
  a 5 mA current limit. That reproduces the datasheet's 4 µs to 0.1 % on 1000 pF (the model:
  4.2 µs), 20 µs on 0.01 µF (20.1 µs) and Schmitz's "3 µs gives about 1 %" (0.7 %). A
  full-scale step is not finished in a 3.4 µs pulse; a small one is.
- **Hold.** Hold begins an aperture time after the pulse ends (148 ns for a rising input, 205 ns
  falling, Figure 1 at 25 °C); the input is still being tracked until then. Then a hold step of
  −10 mV typical on 1 nF (1 mV at 0.01 µF, inversely proportional to the capacitor, scaled
  with output voltage after Figure 15), 2 mV of input offset, 0.004 % gain error, and
  feedthrough of the input at −80 dB (typical, 1 nF).
- **Droop** is the leakage option above, off by default.

### Assumptions

Everything here is from the datasheet or the schematic except:

- The CD4093's supply is **+15 V** (the schematic's pin 14 arrow is unlabelled). It is the
  only supply for which the 5k6 / 1k8 divider is a valid LF398 logic level.
- **The LF398-N column** (not the A-grade or the LF198): 2 mV offset, 1 mV hold step, 30 pA.
  The "typical characteristics" curves are not labelled by grade and Figure 5 reads half the
  hold step; the table was followed.
- The front-end lag τ = 0.6 µs is **fitted** to the datasheet's two acquisition times and its
  5 mA; it is the model's one free parameter.
- The BC548 (IS 2e-14, hFE 290) and the LED (1.9 V) only place the trigger threshold, which is
  insensitive to them; the BC548's turn-on and storage times are left out.
- Offset, leakage and hold-step **signs**: the datasheet gives magnitudes. Offset is positive,
  leakage up (menu: down) and the hold step down (read from Figure 17).
- **The oscillator pot** is a 1 Meg *linear* pot used as a variable resistor, as drawn;
  RATE maps onto it linearly, clockwise = faster. The schematic does not give the taper.
  RATE also still sets the shared clock; the oscillator and the shared clock then run at
  unrelated rates from one knob.
- Junction temperature is 25 °C ambient plus the chip's self-heating in a PDIP (48.9 °C/W,
  4.5 mA × 30 V typical), +6.6 °C, which makes the leakage 1.5× its 25 °C datasheet figure.
- The 4093's outputs have the datasheet's on-resistance (220 Ω at 15 V) and 65 ns delay; its
  40 ns output transition time and 5 pF input capacitance (about 1 % on the pulse width) are
  not modelled.
- The input is treated as an ideal source: the schematic's 100k input resistor and the chip's
  ~10 nA bias current only matter to a source with resistance.
- Not modelled: the sample-start and hold-start output transients (Figures 16, 17; they settle
  inside the pulse or are below the hold step), feedthrough above about 10 kHz (Figure 14 turns
  up there), output impedance, supply rejection, noise and dielectric absorption (a styrene
  hold capacitor).

## Polyphony

SAMPLE & HOLD is polyphonic, following SRC's channel count (falling back to 1
channel when SRC is unpatched and it is sampling the module's own mono white
noise). NOISE and RND GATE are monophonic — one shared shift register and one shared
probability draw, in keeping with the spec's framing of three sections around a
single clock rather than sixteen independent copies of each.

## Approximated or left out

- **SAMPLE & HOLD** is the YASH and its LF398 as far as their datasheets and the
  schematic go; what is assumed, fitted or left out is listed under "The YASH, and what
  the model does" above.
- **Three taps and an inverter, because one 4006 cannot reach the textbook pair.**
  The standard two-tap solution for 18 stages is stages 18 and 11, and the 4006
  has no pin on stage 11 (it brings out only the ends of its four sections). The
  Yusynth ring does not need it: chained D4, D3, D2, D1, the pins give stages 5, 9
  and 18, and the feedback NOT(s18 XOR s5) XOR s9 (two 4070 gates and a BC547
  inverter on the board) has period 262,140 = 2¹⁸ − 4, plus a separate 4-clock
  cycle the sequence never reaches. At the 4 kHz top of RATE that is about 65
  seconds before it repeats. The trace is from the published schematic (pin
  assignments read off the drawing; verified by simulating all 2¹⁸ states in
  `tests/Volatility/test_register.cpp`). Whether Usson's board has other
  component values or the MiaW course used a different wiring is not known.
- **The register's stages are not all on pins.** The pins are stages 4, 5, 9, 13,
  14 and 18; RND is the XOR gate's output (the bit entering stage 1) and DAC reads
  eight consecutive stages starting at stage BITS+1, most of them inside the chip
  with no pin: that read is this module's addition.
- **No start-up network.** The inverter means an all-zero register feeds back a
  one, so the register cannot lock up and the old rule that fed a one into an
  all-zero register is gone.
- The chip's delayed fourth-stage output (pin 2), its propagation delay and its
  2.5 to 12 MHz clock limits are not modelled.
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

# The op-amps (TL07x, LM741, LM358)

`src/OpAmp.hpp` models the three op-amps the Modular in a Week boards use, from
their datasheets, and `src/MixerStages.hpp` wires the TL07x into Consolidation and
Bailout. Diversified 102 and 104 use the LM741 and the LM358 directly. This page is
the numbers and where each came from.

## The model

One integrator, not a transistor netlist:

    du/dt = clamp( wt (V+ - V-) - (wt / A0) u ,  +-SR )        wt = 2 pi GBW

which is, together, the gain-bandwidth product (a loop with feedback fraction
beta has a pole at GBW x beta), the finite DC gain A0, and slew limiting -- and the
right interaction between them: a slewing loop is a ramp, not a pole. The output pin
is `u` through an output stage that stops short of the rails by an amount that grows
with the supply and with the load current (a Thevenin source: an EMF of rail - h0
behind rs ohms). The LM358's output stage is a different shape, below.

The circuit around it enters as `V- = a y + b` and is solved a step at a time, with
the capacitors in the feedback network solved exactly for the resistor they sit
across (an exponentially fitted companion, so a 27 pF cap on a 21 us sample is
neither a few percent of Rf, as backward Euler would have it, nor ringing, as the
trapezoid rule would). `tests/OpAmp` checks the result against the closed-form
frequency response of the same network with the same op-amp, to 0.6 % at the step the
Distortion+ runs at, and breaks the model six ways (no slew limit, no rail, no dead
band, no DC-gain leak, plain backward-Euler capacitors, backward-Euler integrator) to
watch the suite fail.

## What the datasheets say

| | TL071/72/74 (TI SLOS080W, Jul 2025) | LM741 (TI SNOSC25D, Oct 2015) | LM358 (TI SLOS068AB, Oct 2024) |
|---|---|---|---|
| Gain-bandwidth | 5.25 MHz (older revisions: 3 MHz) | 1.0 MHz (see below) | 0.7 MHz (the B parts: 1.2 MHz) |
| Slew rate, unity gain | 20 V/us typ, 8 min | 0.5 V/us typ | 0.3 V/us (B: 0.5) |
| Open-loop gain, DC | 200 V/mV typ (25 min) | 200 V/mV typ (50 min) | 100 V/mV typ (25 min) |
| Output swing, +-15 V | +-13.5 V typ at 10k; +-12.3 V at 2k (Fig. 5-42/43); Fig. 5-44: VOM = 0.952 \|Vcc\| - 0.83 into 10k | +-14 V typ at 10k, +-13 V typ at 2k | V+ - 2 V typ at 30 V (RL >= 10k); V+ - 4 V max at 2k; V- + ~5 mV on the constant sink |
| Input resistance | 1 TOhm | 2 MOhm typ | not in this revision |
| Open-loop output impedance | 125 ohm (1 MHz) | 75 ohm (National LM741 revisions; TI's uA741 sheet says 575 ohm) | not given |
| Quiescent current | 1.4 mA / amp | 1.7 mA | 0.35 mA |

The swing fits in `OpAmp.hpp`:

* **TL07x.** Fig. 5-44 is a straight line (3.5 V -> 2.5 V, 14 V -> 12.5 V), which makes
  the loaded EMF 0.9758 |Vcc| - 0.851 behind 250 ohm; the 250 ohm is what turns 13.5 V at
  10k into 12.3 V at 2k, and it also lands the 1k, 500, 200 ohm points of Fig. 5-43 to
  a few tenths. On +-12 V that is 10.60 V into 10k, 10.86 V open, 10.34 V into the 5k the
  Consolidation first stage drives -- not the 11 V the old `tanh` assumed.
* **LM741.** The two typical points give 196 ohm and an EMF 0.73 V under the rail. The
  datasheet has no swing-against-supply figure for the 741; the 0.73 V is carried to any
  supply. The MXR runs it on +-4.5 V, below the +-5 V the part is specified at, so that
  number is an extrapolation.
* **LM358.** h(10k) = 1.4 + 0.02 S (S the total supply), read off 2 V typ at 30 V and
  1.5 V max at 5 V; the load slope is the 1 V the datasheet adds between 10k and 2k
  (200 ohm). Interpolating a max against a typ is a guess and is marked as one.

The **LM358's output stage** is the one in TI SLOA277B: no static bias on the output
transistors (class B). A Darlington NPN sources, a PNP emitter follower sinks, and the
node that drives them has to slew 3 Vbe to change over, during which the output sits at
the load times the always-on constant sink (about 40 uA typ, 12 uA min): that is the
datasheet's own 3 Vbe / SR = 4 us at 0.5 V/us, and the model reproduces it (the output
stalls 6.5 us at 0.3 V/us). It follows that a ground-referred load sees crossover
distortion at every zero crossing (0.38 % THD at 1 kHz, 1 V peak, 10k to ground,
against 0.31 % measured at 2 V rms by Rod Elliott on a real part) and a load pulled to
V- does not. Sinking, the PNP follower holds the output about 0.62 V above V- plus
47 ohm x I (Fig. 3-2, 25 C curve read by eye).

## Where they are wired

* **Consolidation, Bailout** -- the mixer's two TL07x stages (10k in, 10k feedback with
  27 pF; 10k, 10k with 47 pF) and the multiples' TL07x followers, behind two menu
  items, both off by default so existing patches sound as they did. See those pages.
* **Diversified 102 (MXR Distortion+)** -- always on, since there is no ideal version
  of a 741 to be faithful to: the network is solved as drawn, the 741 slews at 0.5 V/us,
  and its gain-bandwidth turns a gain of 200 into a 5 kHz amplifier.
* **Diversified 104 (bitcrusher)** -- always on: U7.1 and U7.2 are LM358s on +-12 V
  (assumed, the schematic draws one supply and the board cannot work on it), run eight
  times per audio sample so a code change that lands anywhere in a sample is slewed
  (0.3 V/us: 17 us for 5 V).
* **AuditLogic** -- not wired here. Its TL074 comparators are `src/Tl074Comparator.hpp`
  (another worklist item); it fits the swing to the *minimum* columns where this file
  fits the typical curves, so the two disagree by a few tenths of a volt at the rail.

## Left out, and why

* Input offset voltage and bias current (mV and pA against volt-scale signals).
* Phase reversal and the common-mode range (every stage here is far inside it, except
  the Quad Gated Switch's 1.09 V reference on a TL074 whose negative supply the
  schematic does not label; if it is ground, 1.09 V is below the TL07x's common-mode
  range and that board's behaviour is not defined by the datasheet).
* Noise (37 nV/rtHz for the TL07x is far below anything in a 10 V signal chain).
* Output impedance: closed loop it is the open-loop 75-125 ohm over a loop gain of
  hundreds to thousands -- a fraction of an ohm at audio. The multiples' 100 ohm series
  resistor, which is in the schematic, is modelled.
* The LM741's 1.0 MHz is the usual printed gain-bandwidth figure, not a number in
  TI's document (which gives a 0.3 us rise time, i.e. 1.17 MHz).

## Sources

* TL07x: https://www.ti.com/lit/ds/symlink/tl072.pdf (SLOS080W; the copy in
  `MiawResearch/datasheets/tl072.pdf`)
* LM741: https://www.ti.com/lit/ds/symlink/lm741.pdf (SNOSC25D); output resistance
  from the µA741 sheet https://www.ti.com/lit/ds/symlink/ua741.pdf
* LM358: https://www.ti.com/lit/ds/symlink/lm358.pdf (SLOS068AB);
  crossover and output stage: https://www.ti.com/lit/pdf/sloa277 (SLOA277B,
  "Application Design Guidelines for LM324 and LM358 Devices", sections 3 and 4.3)
* LM358 crossover measured: https://www.sound-au.com/articles/lm358.htm

# Consolidation — mixer & multiples

A Modular in a Week mixer and multiples on one panel, for VCV Rack 2.

**MIXER** models "A Simple Mixer Right?" v1.4 (Kristian Blåsol) directly: four
10 kΩ-per-channel inputs summed by a unity-gain inverting op-amp stage, fed
into a second unity-gain inverting stage that brings the sum back to normal
polarity. Both stages' outputs are broken out — OUT (normal polarity) and INV
OUT (the raw inverted sum) — the way the sibling "Simple Unbuffered Mixer"
schematic exposes its own first stage as `InvertedOutput`. Each channel's
level knob stands in for that schematic's front-panel pot, attenuating the
input before it reaches the summing node, which is exactly the gain structure
of the original: unity per channel, no makeup gain anywhere in the chain.

**MULTIPLES** reworks "Buffured Multiple 3x1:2" (three independent
one-in-two-out unity buffers) into two one-in-three-out multiples instead —
MULT B normals from MULT A's input, so one cable into A gives a 1:3, a second
cable into B gives a 1:6, and patching both independently gives two separate
1:3s. That flexibility is the point of combining the board's three small
buffers into two slightly bigger ones. A subtotal rule splits the felt block
between MULT A's input and its three legs on one side and MULT B on the
other, keeping the two multiples visually distinct within the one section.

Credit: Modular in a Week, circuits by Kristian Blåsol (mixer) and Niklas
Rönnberg (multiples). Part of the [Moon Technologies](../README.md) plugin.

## Controls

| Control | Range | Default |
|---|---|---|
| LVL 1–4 | 0–100 % | 100 % |
| MUTE 1–4 | latching | passing |

Each level knob wears its channel's meter as a lime arc struck into the seat
ring around it — the dark band already there between the knob and its well, so
the read-out costs the panel nothing. The arc sweeps the knob's own travel, so
full scale is where the pointer sits at maximum. It rises instantly and falls
slowly: a meter on a mixer is there to say which channel is the loud one.

## Mixer, VCA, or both at once

Each channel has a **LEVEL CV** input and a **direct output**, and between them
they decide what the module is being.

**LEVEL CV multiplies the knob** rather than adding to it. The knob is the
depth, so an unpatched jack leaves the channel exactly as it was and a patched
one turns that strip into a VCA with the knob as its ceiling. There is no
attenuator on top, deliberately — the knob already is one, and a second would
have cost a row this panel could not spare.

**A patched direct output takes that channel out of the mix.** Leave it empty
and the channel sums as it always did. So the same four strips are a four-input
mixer, four independent VCAs, or any split of the two, with no mode switch to
say which — patch two direct outs and you have a two-channel mix and two VCAs.

MUTE is a latch per channel and silences it wherever it is going.

## Jacks

| Jack | Notes |
|---|---|
| IN 1–4 | mixer channel inputs, polyphonic |
| LEVEL CV 1–4 | 0–10 V multiplies that channel's knob; unpatched leaves it alone |
| 1–4 (band) | that channel's direct output. Patched, the channel leaves the mix |
| MIX | the mix, normal polarity — the main output |
| INV | the mix, inverted — the first stage's own output |
| MULT A | multiple A's input, with its three legs on the same row |
| MULT B | multiple B's input; **normals from MULT A's input** when unpatched |

## Voltage conventions and polyphony

The mixer sums audio or CV alike — whatever the four inputs carry — and is
fully polyphonic: each of the sixteen possible polyphony channels is summed
independently, so a poly cable on IN 1 and a monophonic cable on IN 2 mix the
way you would expect (the mono signal is added to every polyphonic channel of
the sum). Output channel count follows the widest input. The multiples pass
whatever channel count arrives on their input straight through to all three
legs, unchanged except for output-safety clamping to ±12 V.

## Context menu

- **Mixer: soft-clip at the op-amp rails** — off by default, matching the bare
  schematic (which simply clips hard against the supply once you overdrive
  it). When on, the sum is soft-limited with a `tanh` knee around ±11 V, the
  swing a real TL072 running from a ±12 V supply actually delivers.
- **TL07x op-amps (datasheet: rails, slew, GBW, caps)** — off by default. Runs the
  mixer's two stages and the multiples' followers as the TL072 the schematic names
  (`src/OpAmp.hpp`, numbers in `docs/OpAmps.md`) and replaces the soft clip above:
  the output stops where a TL07x on ±12 V stops, which depends on what it drives
  (10.34 V from the mixer's first stage, whose 5 kΩ load is its own feedback
  resistor in parallel with the second stage's input; 10.6 V into 10 kΩ; 10.8 V from
  a follower into a high impedance), and not at ±12 V. The 27 pF and 47 pF
  compensation caps are in, as poles at 590 kHz and 339 kHz, and the stages have the
  datasheet's 5.25 MHz gain-bandwidth (the first stage's noise gain is 5, four 10 kΩ
  inputs) and 20 V/µs slew: none of which is audible below the rail (the pass band is
  flat to 0.1 dB at 20 kHz, tested), which is exactly the claim this page used to make
  without being able to show it. INV OUT is the first stage's pin, OUT the second's.
  The three legs of a multiple are three identical op-amps with one input and one
  load, so they are solved once and fanned out.
- **Output networks (100 Ω; mixer 10 µF) into 100 kΩ** — off by default. The
  schematic's output RC: the mixer's OUT goes through 100 Ω and 10 µF, a DC blocker
  with a 0.16 Hz corner into the assumed 100 kΩ input (so a mix of CV decays with
  τ = 1 s, which is why this is off); each multiple leg has its 100 Ω in series,
  dropping 0.1 % into 100 kΩ. Takes effect with the op-amp option.
- **Multiples: passive-style loading droop** — off by default (both multiples
  are buffered, per the schematic). When on, each multiple's output sags
  about 1% per patched leg. This is the "a passive mult loads down as you
  patch more of it" folklore, deliberately implemented as the joke it is
  rather than as measured physics — an ideal buffer, or even a bare wire
  driven by a low-impedance source, does not actually behave this way.

## What was approximated or left out

- The mixer's HF compensation caps (27 pF, 47 pF across the two feedback
  resistors) and its output RC (100 Ω / 10 µF, primarily a DC blocker into
  whatever follows) are in the model only with the TL07x option on (above); by
  default the strips are ideal. The compensation values come from this page's own
  earlier transcription of the schematic -- the board file itself is not in the
  reference folder -- and the 100 kΩ the output sees is assumed. Input offset,
  bias current and noise are not modelled.
- The "Buffured Multiple 3x1:2" board's exact three-buffer-pair layout is not
  reproduced; MULTIPLES implements the more useful two-multiple, B-normalled
  arrangement the consolidated design calls for instead, using the same
  unity-buffer idea.
- The passive-multiple droop is a knowingly unphysical joke feature (see
  above), not a simulation of cable capacitance or a real passive mult's
  behavior.

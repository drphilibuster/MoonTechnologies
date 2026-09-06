# Consolidation

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
buffers into two slightly bigger ones.

Credit: Modular in a Week, circuits by Kristian Blåsol (mixer) and Niklas
Rönnberg (multiples). Part of the [Moon Technologies](../README.md) plugin.

## Controls

| Control | Range | Default |
|---|---|---|
| LVL 1–4 | 0–100 % | 0 % |

## Jacks

| Jack | Notes |
|---|---|
| IN 1–4 | mixer channel inputs, polyphonic |
| OUT | the mix, normal polarity |
| INV OUT | the mix, inverted — the first stage's own output |
| MULT A | multiple A's input |
| (3 unlabeled outputs below MULT A) | A's three legs — electrically identical |
| MULT B | multiple B's input; **normals from MULT A's input** when unpatched |
| (3 unlabeled outputs below MULT B) | B's three legs |

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
- **Multiples: passive-style loading droop** — off by default (both multiples
  are buffered, per the schematic). When on, each multiple's output sags
  about 1% per patched leg. This is the "a passive mult loads down as you
  patch more of it" folklore, deliberately implemented as the joke it is
  rather than as measured physics — an ideal buffer, or even a bare wire
  driven by a low-impedance source, does not actually behave this way.

## What was approximated or left out

- The mixer's HF compensation caps (27 pF, 47 pF across the two feedback
  resistors) and its output RC (100 Ω / 10 µF, primarily a DC blocker into
  whatever follows) have no audible effect at audio rates and are not
  modeled.
- The "Buffured Multiple 3x1:2" board's exact three-buffer-pair layout is not
  reproduced; MULTIPLES implements the more useful two-multiple, B-normalled
  arrangement the consolidated design calls for instead, using the same
  unity-buffer idea.
- The passive-multiple droop is a knowingly unphysical joke feature (see
  above), not a simulation of cable capacitance or a real passive mult's
  behavior.

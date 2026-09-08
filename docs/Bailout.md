# Bailout

Consolidation at twice the size, and not quite the same shape — an eight-channel
mixer for VCV Rack 2, arranged as two banks of four.

The strips are the same circuit Consolidation uses, from "A Simple Mixer Right?"
v1.4 (Kristian Blåsol): a 10 kΩ level pot into a 10 kΩ input resistor feeding a
unity-gain inverting summer, with a second unity-gain inverting stage after it.
Unity per channel, no makeup gain anywhere in the chain.

What is different is the grouping. **Eight strips in two banks of four**, each
bank with its own **MIX** output, and a **MAIN** that sums whichever banks are
not already spoken for. Consolidation spends its second inverting stage on an
INV output; Bailout spends it once per bank, so MIX A and MIX B leave the module
the same way round as MAIN rather than against it.

**MULTIPLES** is the same rework of "Buffured Multiple 3x1:2" that Consolidation
carries, widened to what the mixer's eight columns already pay for: two
**one-in-seven** multiples, MULT B normalled from MULT A's input. One cable is a
1:7, two cables are a 1:14, and patching both independently gives two separate
1:7s.

Credit: Modular in a Week, circuits by Kristian Blåsol (mixer) and Niklas
Rönnberg (multiples). Part of the [Moon Technologies](../README.md) plugin.

## Controls

| Control | Range | Default |
|---|---|---|
| LVL 1–8 | 0–100 % | 100 % |
| MUTE 1–8 | latching | passing |

Levels default to unity, not zero — a mixer that starts silent makes a patched
module look broken, and it is Rack's own convention (every Fundamental level
defaults to unity). It also matches the topology: 10 kΩ in against 10 kΩ
feedback is unity gain per channel.

Each level knob wears its channel's meter as a lime arc struck into the seat
ring around it — the dark band already there between the knob and its well, so
the read-out costs the panel nothing. It rises instantly and falls slowly: a
meter on a mixer is there to say which channel is the loud one.

## Every output steals rather than copies

This is the whole of how the module routes, and it is two rules:

- **A channel with its direct out patched leaves its bank.** Patch OUT 3 and
  channel 3 stops feeding MIX A and goes there instead.
- **A bank with its MIX out patched leaves MAIN.** Patch MIX A and channels 1–4
  stop reaching MAIN; MIX A still carries them, and bank B is untouched.

Between them, and with no switch anywhere to say which it is being, the same
panel is:

| Patch | What it is |
|---|---|
| nothing on the footer but MAIN | one 8-into-1 mixer |
| MIX A and MIX B both patched | two independent 4-channel mixers |
| all eight direct outs patched | eight VCAs |
| MIX A patched, B left alone | a 4-channel submix, plus bank B on MAIN |

Nothing is ever heard twice: a strip arrives at exactly one destination, whatever
the patch. That is checked exhaustively over all 1,024 patch combinations in
`tests/Bailout`, because the failure it invites — a channel that reaches its
direct out *and* still reaches MAIN through its bank — does not announce itself.
The module still makes a sound, just a louder one than the patch describes.

## Mixer, VCA, or both at once

**LEVEL CV multiplies the knob** rather than adding to it. The knob is the depth,
so an unpatched jack leaves the channel alone and a patched one scales it from
silence at 0 V to the knob's own setting at 10 V. Negative voltage is treated as
0 V; above 10 V it stops at the knob. That is what lets the same strip be a mixer
channel or a VCA without a mode switch to say which.

**MUTE beats everything**, CV included: a muted channel passes nothing at any
voltage.

## Voltage conventions

- IN 1–8, MULT A/B IN: any audio or CV, ±12 V accepted.
- LEVEL CV: 0–10 V, 10 V being the knob's own setting. Below 0 V reads as 0.
- Direct outs, MIX A, MIX B, MAIN, multiple legs: clamped to ±12 V.

## Context menu

- **Mixer: soft-clip at the op-amp rails** — eight channels at 10 V sum to 80 V,
  which no op-amp on a 12 V rail produces. This soft-clamps each bank and then
  MAIN at about 11 V with a `tanh` knee, the way a TL07x runs out of swing,
  rather than the hard clip a starved rail would not actually produce. Off by
  default, in which case the sum is honest and only the ±12 V output clamp
  catches it.
- **Multiples: passive-style loading droop** — knocks about a percent off per
  patched leg. A bare-wire multiple has no source impedance of its own to sag
  under load, so this is faithful to the folklore rather than to the physics.
  Off by default.

## Polyphony

Every strip is polyphonic and follows the widest IN patched to the module, so a
16-channel cable mixes as sixteen. The multiples follow their own input.

## What it does not have, and why

- **No INV output.** Consolidation has one; a fourth jack on this footer band
  costs two more HP, and the second inverting stage is spent here on giving each
  bank its own correct polarity instead. If you want both polarities of a sum,
  Consolidation is 14 HP and still does that.
- **No per-bank level.** MIX A and MIX B are the banks' own summing nodes, at
  unity, exactly as the schematic has them. A master level would be a stage the
  original board does not have.
- **No pan and no aux sends.** This is a summing mixer from a breadboard week,
  doubled — not a console.

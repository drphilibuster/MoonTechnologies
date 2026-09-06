# Six Figures

A six-voice oscillator bank for VCV Rack 2, form W-2 — six sources of income, one
line each.

Part of the [Moon Technologies](../README.md) plugin.

It consolidates the whole *Modular in a Week* oscillator day into one module: the
40106 hex Schmitt-trigger bank, the All About Circuits 4069-integrator VCO, the
4046 PLL VCO and the Kassutronics reverse-avalanche oscillator become four
selectable **cores**, and the panel gives you six of them side by side rather
than four different 3-8 HP modules. Credit to Kristian Blåsol (the 40106 bank
and the 4046 and avalanche schematics) and to All About Circuits / Kassutronics
for the circuits the other two cores are drawn from.

None of the four originals is a tracking VCO — they are RC relaxation
oscillators, and their "CV in" is a photocell or a summing resistor landing
directly on the timing capacitor's pot, not a exponential converter. Six
Figures keeps that crudeness as the default behaviour and adds a real 1 V/oct
mode as a menu option, the way the brief for this family asks: keep the
original character available and default, and let VCV do the rest.

## Layout

24 HP, six identical voice columns and a seventh "totals" column on the right
that holds the controls shared by all six voices.

## The four cores

Every voice can be set to any core independently, so you can run six of the
same oscillator, six different ones, or anything between.

| Core | Original circuit | Main output (`OUT`) | Auxiliary output (`AUX`) |
|---|---|---|---|
| **40106 Schmitt** | 40106 hex Schmitt-trigger astable (six voices sharing one IC in the original) | band-limited square | the RC timing cap's charge/discharge ramp, bent toward its true exponential shape |
| **4069 AAC** | All About Circuits' 4069-integrator VCO (`SQU` and `TRI` in the original schematic) | band-limited square | band-limited triangle, from a leaky integration of the same square |
| **4046 PLL** | 4046 phase-locked-loop VCO | band-limited square, free-running or pulled toward `SIGNAL` | the raw XOR phase-comparator bit, as a 0/10 V logic signal — exactly what the 4046's `PC1` pin actually outputs |
| **Avalanche** | Kassutronics reverse-avalanche oscillator (a BC337 run in reverse breakdown, charging a cap through the vactrol's LDR) | band-limited saw, bent toward the RC charge curve | a 1 ms, 10 V pulse once per cycle — the avalanche breakdown pulse itself |

All four are band-limited with polyBLEP; the RC "bend" applied to the Schmitt
core's `AUX` and the avalanche core's `OUT` is a cosmetic reshaping applied
after band-limiting, for the rounded look a real RC ramp has, and is a
deliberate approximation rather than a circuit simulation.

## Per-voice controls (×6)

| Control | Type | Description |
|---|---|---|
| **CORE** | 4-position snap knob | Which of the four cores this voice runs |
| **RATE** | knob | The voice's frequency, RC-pot style by default (see [CV response](#cv-response) below) |
| **CV** | trim, paired with the jack below it | How much the `CV` jack affects `RATE` |
| **CV** (jack) | input | CV for this voice's rate |
| **OUT** | output, footer | The core's main waveform, ±5 V |
| **AUX** | output | The core's secondary signal — see the table above |
| rate LED | light, beside `RATE` | Blinks with the voice's own cycle **only in LO range** — a visual clock/LFO indicator. In HI range it stays off; blinking at audio rate would just read as flicker. |

## Shared controls (the totals column)

| Control | Type | Description |
|---|---|---|
| **RANGE** | 2-position switch | `LO` ≈ 0.05–8 Hz (LFO range), `HI` ≈ 20 Hz–4 kHz (audio range). Applies to every voice. |
| **CAPT** | knob | For voices set to the PLL core: the loop filter's bandwidth. Low = narrow/slow lock, high = wide capture range and faster lock, at the cost of more audible ripple from the phase comparator leaking into the pitch. |
| **LOCK** | light, beside `CAPT` | Lit when at least one PLL-core voice is locked to `SIGNAL`. See [PLL lock](#pll-lock-heuristic) below. |
| **SYNC** | input | A rising edge (Schmitt, 0.1 V/2 V thresholds) hard-resets every voice's phase, drift and PLL state at once, whatever core each is set to. |
| **DRIFT** | trim | For voices set to the Avalanche core: depth of a slow random wander added to the rate, modelling the vactrol's LDR never quite settling. Up to ±0.5 octave of smoothed, one-pole-filtered noise at maximum. Has no effect on the other three cores. |
| **SIGNAL** | input | The reference signal PLL-core voices compare against — the 4046's comparator input. Unconnected, PLL voices simply free-run at their own `RATE`. |
| **MIX** | output, footer, primary | The sum of all six voices' `OUT` signals, soft-clipped (`tanh`) rather than hard-clamped, so stacking six voices thickens and eventually saturates rather than just clipping flat. |

## CV response

By default every voice's CV response is the "crude" one every circuit here
actually has: the CV jack (attenuated by that voice's trim) is summed directly
into the `RATE` knob's own 0–1 travel, *before* the knob's exponential taper —
matching the "CV summing mixer" landing on the frequency pot's wiper in the
AAC sketch this module is partly drawn from. It is not 1 V/oct, and doubling a
CV's swing does not double the pitch shift the same way at both ends of the
knob's travel.

The context menu's **1 V/oct tracking (all voices)** option replaces this,
module-wide, with a real exponential converter: `RATE` becomes a coarse offset
from the range's centre frequency, and CV tracks pitch at 1 V/oct. None of the
four original circuits could do this — it is the "bells and whistles" VCV
Rack allows, added on top of the default, characterful behaviour rather than
instead of it.

## PLL lock heuristic

The 4046 core's `LOCK` light is not a cycle-accurate lock detector — this
module doesn't have a discrete phase counter to check — but a heuristic: it
low-passes the phase comparator's XOR bit twice, once quickly (the same
`CAPT`-controlled corner the pitch pull itself uses) and once slowly
(around 0.5 Hz), and calls the loop "locked" once the fast average stops
drifting away from the slow one. It reads correctly in practice — steady
while genuinely tracking `SIGNAL`, dark while beating against it or free-running
— but treat it as an indicator of stability, not of exact frequency or phase.

## Voltages

- `OUT` and the triangle/ramp `AUX` signals: ±5 V.
- The PLL `AUX` (raw comparator bit) and the avalanche `AUX` (breakdown pulse):
  0/10 V, logic-style — these are comparator and trigger outputs, not audio.
- `SYNC`: a gate/trigger, 0/10 V convention, Schmitt thresholds 0.1 V/2 V.
- `SIGNAL`: bipolar audio-range signal; only its zero-crossings matter (it is
  squared internally by a Schmitt comparator, thresholds ±0.1 V).
- `MIX`: ±5 V nominal, soft-clipped above that as more voices stack up.
- CV jacks: no fixed convention (see [CV response](#cv-response) — the crude
  mode treats CV as a 0–10 V-ish trim on the pot, the 1 V/oct mode treats it
  literally).

## Context menu

- **1 V/oct tracking (all voices)** — off by default. See
  [CV response](#cv-response).

## What was approximated or left out

- The reverse-avalanche and 40106 "RC bend" is a post-hoc reshaping of a
  band-limited BLEP waveform, not a simulation of the transistor avalanche
  breakdown or the 40106's actual threshold voltages.
- The PLL only locks to the fundamental, not to harmonics or subharmonics of
  `SIGNAL` the way a real 4046 can be coaxed into doing (the "or its harmonics
  or subharmonics" the brief mentions) — implementing genuine N:M lock was out
  of scope for this pass.
- `DRIFT` and `CAPT` are each one shared control across all six voices
  rather than per-voice, matching the panel's single totals column; every
  voice running that core gets its own independent noise generator or PLL
  state, so six avalanche voices drift independently even though they share
  one `DRIFT` depth.

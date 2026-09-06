# Installment

A dual function generator for VCV Rack 2 — two identical circuits, each running one
of three Modular in a Week circuits at a time, plus a small CV-controlled PWM driver
riding on channel one.

Part of the [Moon Technologies](../README.md) plugin.

## What it is based on

Installment consolidates four days from Kristian Blåsol's *Modular in a Week*:

- **Simple LFO** and the **13700 Dual VCLFO** — an OTA-integrator triangle core: a
  linear ramp up, a linear ramp down, free-running.
- **Dual AR**, based on Niklas Rönnberg's diode-steered RC design (also built up on
  LMNC's channel) — a gated attack/release envelope that holds at its peak for as
  long as the gate stays high.
- **PHObos's AD** (from the Moon Base Xplorer firmware) — an attack/decay envelope
  that fires once per trigger and, looped, free-runs as an LFO of its own.
- The **Day 12 CV-controllable PWM** sketch (a Falstad circuit built for driving tape
  motors) — a triangle compared against a threshold makes a duty-cycle drive signal.

Rather than three separate modules, one circuit is exposed per channel and MODE picks
which day it is. ATTACK and RELEASE are the same two knobs regardless of MODE — what
they set changes meaning with it, which is the underlying claim: a function generator
*is* an LFO, an AR and an AD, depending only on how its rise and fall are triggered.

## Per channel

Both channels are identical, independent, and polyphonic on their own CV and gate
inputs.

| Control | What it does |
|---|---|
| **MODE** | LFO / AR / AD — which circuit this channel runs. |
| **LOOP** | In AD, makes the envelope retrigger itself the instant it reaches zero, with no gate needed — PHObos's "AD loops into an LFO." In AR, the same move latches the envelope into repeating once it has been triggered once, ignoring the gate from then on. Has no effect in LFO, which always free-runs. |
| **ATTACK** | The rise time. In LFO, the ramp-up segment of the triangle (with RELEASE, sets both the LFO's rate and its skew). In AR/AD, the attack time of an RC-exponential rise to the +10 V peak. |
| **RELEASE** | The fall time — the ramp-down segment in LFO, the release (AR) or decay (AD) in envelope modes. |
| **RANGE** | Lo (10 ms – 10 s) or Hi (1 ms – 1 s) — the span ATTACK and RELEASE cover. Hi reaches into audio rates, which is what MINIMUM DUE's band-limiting is for. |
| **BIAS** | A manual octave offset on top of ATTACK and RELEASE, applied to both equally so their skew is preserved. Up to ±3 octaves. |
| **CV AMT** | Attenuverter for the CV jack below it, on the same axis as BIAS: ±5 octaves at full deflection and 10 V. |

## Jacks, per channel

| Jack | Role |
|---|---|
| **CV IN** | Adds to ATTACK and RELEASE together (see BIAS/CV AMT above), through the trim directly over it. Polyphonic — the channel's polyphony count follows this and GATE/RESET. |
| **GATE/RESET IN** | A level-sensitive gate in AR (attack while high, sustain, release on drop), an edge trigger in AD (fire and forget), and a phase reset in LFO (there is no gate to speak of once it is free-running — a trigger here restarts the cycle at zero). Threshold ~1 V with Schmitt hysteresis. |
| **ENV***n* | The core's main output, 0–10 V: the triangle in LFO, the envelope in AR/AD. |
| **SQU***n* | In LFO, a band-limited (polyBLEP) pulse at the same duty as the triangle's rise/fall split — this is the panel's stand-in for the comparator/squarer the OTA core would otherwise need a second circuit to drive. In AR/AD, the inverted envelope (10 V − ENV). |
| **EOC***n* | A 1 ms, 10 V trigger once per cycle: at the LFO's wrap, or at the moment an AR/AD envelope reaches zero. |

## MINIMUM DUE — the PWM driver

**DUTY** and its **CV** (0–10 V adds directly to DUTY, no attenuverter — the Day 12
circuit had none) set a threshold; **PWM OUT** is 0/10 V, high whenever channel one's
core is below that threshold. It reads channel one's output directly, whatever MODE
that channel is in — a triangle in LFO, an attack/release shape in AR or AD, either
way giving the comparator a carrier to work against. The crossing is band-limited by
inserting a MinBLEP correction at the sub-sample position the carrier actually crossed
the threshold (found by linear interpolation between samples), so it does not alias
even when channel one is in RANGE:Hi and running at an audio rate.

The **"Tape-motor duty limit (10-90%)"** context-menu option clamps DUTY (knob + CV)
to 10–90%, the way the original circuit kept a real motor from stalling at an
all-the-way-open or all-the-way-shut duty cycle. Off by default.

## Voltage conventions

- CV IN, PWM CV IN: unipolar/bipolar CV, ±5 V nominal, 1 V ≈ 1 octave through CV AMT.
- GATE/RESET IN: 0/10 V gate convention, ~1 V threshold with hysteresis.
- ENV, SQU, PWM OUT: unipolar 0–10 V.
- EOC: 0/10 V, 1 ms trigger.

## Context menu

- **Channel 1 / Channel 2: sine-shaped LFO** — reshapes that channel's LFO triangle
  through a cheap odd waveshaper for a smoother, sine-like output. Off (plain
  triangle, the OTA integrator's native shape) by default.
- **Tape-motor duty limit (10-90%)** — see MINIMUM DUE above.

## Approximated or left out

- The AR/AD attack and release/decay segments are RC-style exponentials computed
  from elapsed phase-time (time-constant = phase time ÷ 5, so each phase reaches
  about 99% of its target at the nominal time) rather than a literal diode-steered RC
  network simulation — authentically curved, not a SPICE match.
- BIAS and CV both shift ATTACK and RELEASE together rather than independently, which
  keeps their skew ratio intact under modulation; the original 13700 VCLFO's Bias
  control was read the same way, as an offset on the same axis a CV would reach.
- No panel read-out: with two full channels and a PWM section already filling 17 HP,
  a digital display did not fit without losing a row of controls.

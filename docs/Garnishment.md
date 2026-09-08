# Garnishment

A six-channel VCA / low-pass gate for VCV Rack 2, built from three Modular in a
Week circuits sharing one panel.

Each of the six identical channels has a MODE switch that picks which circuit
it is:

- **OTA** — Simple 13700 Dual VCA (Kristian Blåsol): an LM13700 operational
  transconductance amplifier whose control current sets its gain. Clean and
  linear.
- **VACTROL** — Vactrol VCA (Kristian Blåsol): an LED driving a photoresistor
  in series with the audio. Slow, asymmetric, and the reason low-pass gates
  sound the way they do.
- **JFET AM** — "I AM O" (Quincas): a 2N5457 JFET used as a voltage-controlled
  resistor, dividing one signal against its own channel resistance — a crude
  two-input multiplier riding the JFET's square-law asymmetry.

Credit: Modular in a Week, circuits by Kristian Blåsol (OTA, Vactrol) and
Quincas (I AM O). Part of the [Moon Technologies](../README.md) plugin.

## One control set, three circuits

The panel does not grow a row per topology. Instead, BIAS, LAG, CV IN and CV
AMOUNT mean something different depending on MODE, the way the same five
knobs on a real Buchla-style LPG module mean something different depending on
which cell is patched into it:

| Control | OTA | VACTROL | JFET AM |
|---|---|---|---|
| BIAS | initial/manual gain | LED bias (base brightness) | JFET gate bias point |
| CV IN + CV AMOUNT | adds to the gain | adds to the LED drive | adds to the gate drive |
| LAG | response smoothing | the vactrol's own attack/decay | response smoothing |
| IN | the audio being amplified | the audio being gated | the "AM" carrier being divided |
| OUT | amplified/saturated audio | gated (and optionally filtered) audio | the divided, DC-blocked result |

In the original I AM O circuit the JFET's gate is driven by an "In" signal and
the drain carries an "AM" carrier through a resistor; that gate drive is what
BIAS/CV IN now supply, so IN keeps meaning "the audio this channel processes"
across all three modes.

BIAS and CV IN are combined as `(BIAS + CV_AMOUNT × CV) / 10`, clamped to
0–1 — the unipolar 0–10 V CV convention, with CV AMOUNT as a bipolar
attenuverter (the original vactrol schematic's "CV Amount" trimmer was a fixed
positive divider; making it invert as well is the one liberty taken with that
control).

## Per-circuit detail

**OTA.** `gain = BIAS/CV combined, optionally squared (EXP CV menu)`; output is
`5·tanh(in/5 · gain)`, a fixed-drive soft clip that gives unity small-signal
gain at full open and increasing saturation as either the input level or the
gain rises — modeling the LM13700's unlinearized input differential pair
(this schematic does not use the chip's linearizing-diode pins).

**VACTROL.** The combined control drives an LED-brightness state, slewed with
the LAG control's asymmetric attack (~2 ms, fixed) and decay (50–200 ms). LED
brightness sets a modeled LDR resistance interpolated log-ish between 4.7 MΩ
dark and 150 Ω lit, dividing against an assumed 100 kΩ downstream input
impedance — the schematic's `R12` has no fixed divider partner, so whatever
follows it supplies the other half. The **low-pass gate** menu option closes a
one-pole filter (20 Hz–15 kHz) in step with the gain, so the channel darkens
tonally as it closes, not just in level — the Buchla LPG behaviour the bare
schematic doesn't have on its own.

**JFET AM.** The combined control (0–1) is squared to `g`, modeling
`Id ∝ (1 − Vgs/Vp)²`; the audio input is divided against it as
`g / (g + 1)`, doubled to make up for a passive divider never reaching unity,
and DC-blocked (~20 Hz) the way the schematic's output capacitor does.

## Controls

| Control | Range | Default |
|---|---|---|
| BIAS | 0–10 V | 0 V |
| MODE | OTA / Vactrol / JFET AM | OTA |
| LAG | 0–100 % (50–200 ms decay, ~2 ms attack) | 0 % |
| CV AMOUNT | −100–100 % | 100 % |
| CV IN | 0–10 V typical | — |
| IN | audio, ±5 V | — |
| OUT | audio, ±5 V | — |

Channel 2's CV IN normals from channel 1's, so one CV cable drives both
channels' gain; patch channel 2's own CV IN to override it.

## Context menu

- **OTA: exponential CV response** — squares the combined control before it
  reaches the OTA path, in place of the schematic's inherently linear
  response. Off by default (the original's own character).
- **Vactrol: low-pass gate (filter tracks gain)** — see above. Off by default
  (the schematic is a bare divider, not a filter).

Both options apply to whichever channel is currently in that mode; there is no
separate per-channel copy of either, since an 11 HP panel repeated twice has no
room for controls the originals didn't have front-panel switches for either.

## Six channels

The Modular in a Week day this comes from is two boards, and this module was two
channels to match. The circuits are the same either way, so the count was only
ever a question of panel: each channel took a whole section, three rows of three
columns to hold three controls and a CV pair, with two thirds of two of those
rows empty. One channel per *column* instead holds six in 18 HP — 3.0 HP a
channel against the old 5.0.

Six rather than four or eight because the rest of the family runs on six:
[Six Figures](SixFigures.md)' oscillators, [Kickback](Kickback.md)'s drum voices,
[Collusion](Collusion.md)'s LFOs. One channel each, with nothing left over and
nothing short.

**CV normals down the bank.** Channel 2's CV input falls back to channel 1's,
3's to 2's, and so on, so a single envelope patched into CV 1 opens all six. The
dual version did this between its two channels; a chain is the same idea with
somewhere to go. Patch any channel's own CV and it and everything below it take
that instead.

Channels 1 and 2 keep the parameter and jack indices they had as a dual, so a
patch saved against the two-channel version still restores their settings.

## Polyphony

Fully polyphonic, per channel independently: each channel's output takes its
channel count from its own IN port, and all per-voice state (the slewed
control, the vactrol's optional filter, the JFET's DC blocker) is kept
per-polyphony-channel.

## What was approximated

- The OTA and JFET-AM stages are not transistor-level SPICE models; they are
  DSP shapes chosen to reproduce the *character* the schematic implies (soft
  saturation growing with level for the OTA; a square-law divider for the
  JFET) rather than to match measured voltages from real hardware.
- The vactrol's LDR resistance curve (4.7 MΩ dark / 150 Ω lit, log-interpolated)
  and the 100 kΩ assumed load are reasonable stand-ins for a real LED/LDR pair,
  not a measured part.
- LAG is a single shared knob applied identically to all three circuits, even
  though the original schematics only give the vactrol an inherent lag; this
  keeps the panel from needing per-mode controls it has no room for.

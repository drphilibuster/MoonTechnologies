# Garnishment — VCA / low-pass gate

A six-channel VCA / low-pass gate for VCV Rack 2, built from three Modular in a
Week circuits sharing one panel.

Each of the six identical channels picks which circuit it is -- click the
channel's name on the read-out (OTA, LPG, JFET) and choose:

- **OTA** — Simple 13700 Dual VCA (Kristian Blåsol): an LM13700 operational
  transconductance amplifier whose control current sets its gain. Clean and
  linear.
- **VACTROL** — Vactrol VCA (Kristian Blåsol): an LED driving a photoresistor
  in series with the audio. Slow, asymmetric, and the reason low-pass gates
  sound the way they do.
- **JFET AM** — "I AM O" (Quincas): a 2N5457 JFET used as a voltage-controlled
  resistor, dividing one signal against its own channel resistance. Solved as the circuit
  (`src/Garnishment/IAmO.hpp` on `src/Mna.hpp`), so it saturates, clips and pinches itself as the
  board does, rather than multiplying.

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
| OUT | amplified/saturated audio | gated (and optionally filtered) audio | the drain through C1 |

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
gain rises — a tanh shape that stands in for the LM13700's input stage. **It is not
what the Day 2 schematic builds:** that board *does* use the linearizing diodes (the
diode-bias pin 2 is fed from V+ through 12k), takes the signal through 470 nF and 27k to a
1k BIAS trimmer that sets the differential input, sets the gain current from CV through 22k
into the amplifier-bias pin (so Iabc is about (CV + 11.3 V) / 22k on a ±12 V supply, and the
channel is half open at 0 V), and reads the output current across 33k into the chip's buffer
with a 4k7 pull-down to V-. The shape here is a stand-in for that; the real network is a
worklist item (see below).

**VACTROL.** The combined control drives an LED-brightness state, slewed with
the LAG control's asymmetric attack (~2 ms, fixed) and decay (50–200 ms). LED
brightness sets a modeled LDR resistance interpolated log-ish between 4.7 MΩ
dark and 150 Ω lit, dividing against an assumed 100 kΩ downstream input
impedance — the schematic's `R12` has no fixed divider partner, so whatever
follows it supplies the other half. The **low-pass gate** menu option closes a
one-pole filter (20 Hz–15 kHz) in step with the gain, so the channel darkens
tonally as it closes, not just in level — the Buchla LPG behaviour the bare
schematic doesn't have on its own.

**JFET AM.** The combined control (0-1) is the 2N5457's gate voltage, 0 V at zero down to
-4 V at one (the datasheet's typical pinch-off is -2 V). The audio IN is the AM signal: it
goes through R2 (100 ohm) to the drain, and out through C1 (100 nF, which is the DC blocker),
into a 100k load. The gate is fed through R1 (100k) and has its two junctions, so the circuit
is solved: the 2N5457 as a square-law channel (IDSS 3 mA, VGS(off) -2 V, lambda from the
datasheet's 10 umho output admittance), symmetric in drain and source, and the gate-source and
gate-drain diodes. What that does, none of it a multiplier:
- Small signals are divided by R2 against the channel: 2.3 dB down with the gate at 0 V, no loss
  once the gate passes pinch-off. The whole control range is worth 2.3 dB.
- A big signal runs the channel out of current (it saturates near 3 mA), so a 5 V carrier is
  only 0.75 dB down at gate 0 V: the stage compresses less as it is driven harder.
- A negative carrier forward-biases the gate-drain junction, drags the gate below pinch-off and
  turns the channel off: the stage pinches itself on the negative half.

## Controls

The read-out at the top shows each channel's circuit by name over a bar of how
open the channel is right now (bias and CV together, through the lag), with a
tick where BIAS alone sits. Click a name to pick the circuit; drag a bar up or
down to set BIAS -- the knobs below do the same. Right-click either for the
param's own menu.

| Control | Range | Default |
|---|---|---|
| BIAS | 0–10 V | 0 V |
| MODE (read-out) | OTA / Vactrol (LPG) / JFET AM | OTA |
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
rows empty. One channel per *column* instead holds six in 17 HP — under 3 HP a
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

- **The JFET-AM stage is a divider against 100 ohms, not a VCA.** The I AM O board (Day 2)
  puts the carrier through a fixed 100 ohm resistor into the 2N5457's drain, the gate is fed
  from IN through 100k, and the output is the drain through 100 nF, so a small carrier comes
  out times Rds / (Rds + 100): 0.77 (-2.3 dB) with the gate at 0 V and 1.0 with the gate
  pinched off. That is modelled as the circuit, with the typical part's numbers; a real 2N5457
  is anywhere in the datasheet's ranges (IDSS 1-5 mA, VGS(off) -0.5 to -6 V), which moves that
  depth by a couple of dB. Assumed: the output is loaded by 100k, and the gate capacitances
  (4.5 pF Ciss, 1.5 pF Crss) are left out as they do nothing at audio. The control's -4 V span
  and the gate-drive sign (control up = gate more negative = more open) are the module's.
- The OTA is not a transistor-level model; it is a DSP shape chosen to reproduce the
  *character* the schematic implies (soft saturation growing with level), not to match
  measured voltages from real hardware.
- The vactrol's LDR resistance curve (4.7 MΩ dark / 150 Ω lit, log-interpolated)
  and the 100 kΩ assumed load are reasonable stand-ins for a real LED/LDR pair,
  not a measured part.
- LAG is a single shared knob applied identically to all three circuits, even
  though the original schematics only give the vactrol an inherent lag; this
  keeps the panel from needing per-mode controls it has no room for.

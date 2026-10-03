# Garnishment — VCA / low-pass gate

A six-channel VCA / low-pass gate for VCV Rack 2, built from three Modular in a
Week circuits sharing one panel.

Each of the six identical channels picks which circuit it is -- click the
channel's name on the read-out (OTA, LPG, JFET) and choose:

- **OTA** — Simple 13700 Dual VCA (Kristian Blåsol): an LM13700 operational
  transconductance amplifier whose control current sets its gain. Solved as the board
  (`src/Garnishment/OtaVca.hpp` on `src/Lm13700.hpp`): linearizing diodes, the BIAS
  trimmer, the 22k into the bias pin, the 33k and the buffer. Linear to 10 V of input,
  then it clips; half open at 0 V CV.
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
| BIAS | volts added to the CV at the 22k (0 V = half open) | LED drive volts, through the schematic's 330 ohm | JFET gate bias point |
| CV IN + CV AMOUNT | volts at the 22k into the chip's bias pin | adds to the LED drive | adds to the gate drive |
| LAG | smooths the control volts | none: the lag is the part's own (VTL5C3) | response smoothing |
| IN | the audio being amplified | the audio being gated | the "AM" carrier being divided |
| OUT | the buffer pin: audio on a -1.4 V rest level (menu removes it) | gated (and optionally filtered) audio | the drain through C1 |

In the original I AM O circuit the JFET's gate is driven by an "In" signal and
the drain carries an "AM" carrier through a resistor; that gate drive is what
BIAS/CV IN now supply, so IN keeps meaning "the audio this channel processes"
across all three modes.

For JFET AM, BIAS and CV IN are combined as `(BIAS + CV_AMOUNT × CV) / 10`, clamped to
0–1; VACTROL takes the same sum as LED drive volts (0–15 V, below). This is the unipolar 0–10 V CV convention (the OTA takes the same sum as volts instead, see below), with CV AMOUNT as a bipolar
attenuverter (the original vactrol schematic's "CV Amount" trimmer was a fixed
positive divider; making it invert as well is the one liberty taken with that
control).

## Per-circuit detail

**OTA.** The Day 2 board, solved sample by sample (twice per sample at 48 kHz):

- *Control.* BIAS plus CV x CV AMOUNT is **volts** at the 22k into pin 1 (no 0-10 V "how open"
  convention). Pin 1 sits two junction drops above V- (datasheet Figure 10: 1.0 V at 0.1 uA,
  1.5 V at 1 mA), so Iabc = (CV - V- - Vpin1) / 22k on the course's +-12 V: **0.48 mA at 0 V**
  (small-signal gain 0.55 end to end), 0.93 mA at +10 V (gain 1.05), 0.1 uA at -11 V, and
  nothing below -11.6 V. A BIAS knob at its 0 V default therefore leaves the channel half
  open; closing it takes a negative CV (or CV AMOUNT inverted on a positive one). The
  exponential-CV menu item squares Iabc against 1 mA.
- *Input.* The audio goes through 470 nF and 27k to the (+) pin. The 1k BIAS trimmer's
  **wiper is grounded**: its two ends are the (-) pin and the (+) pin, so it is a split of
  the 1k to ground on each pin, not a gain control. The linearizing diodes (pin 2, 12k to V+,
  about 0.92 mA between the two) carry the signal current, so the pair sees only ~19 mV at
  10 V in where a bare pair would see ~180 mV, and the gain is flat to 1 % from 0.5 V to
  10 V in. Past a signal current of Id/2 (about 12 V through 27k) the diodes run dry and
  the stage clips. Corner: 12.3 Hz.
- *Trimmer.* At the centre (the default; menu slider) both pins carry the same DC and the pair
  is balanced, so Iout is zero at rest and nothing leaks through whatever the CV. Off centre
  the difference of the two I x R drops is an offset current that is a fixed fraction of
  Iabc (about 18 % at 40/60 %): a DC offset on Out that follows the CV, and pin 5 hits a rail
  at the extremes.
- *Output.* Iout = Iabc x tanh(Vd / 2Vt) (datasheet Eq. 5) into 33k || the chip's output
  resistance (Figure 12), pin 5 limited to V+ - 0.8 V / V- + 0.6 V (Figure 5), the buffer's
  two Vbe below it with the 4k7 to V-. **Out is DC coupled, as drawn: it rests at -1.4 V.**
  The menu item "remove the output's DC rest level" high-passes it at ~2 Hz.

**VACTROL.** `BIAS + CV AMOUNT × CV` volts (0–15 V) drive the LED through the schematic's 330 Ω
(CV AMOUNT stands in for the 100k pot). The LED is a diode (about 1.65 V at 20 mA), so 10 V is
about 25 mA and anything under ~1.4 V is dark. The cell is a **PerkinElmer VTL5C3**
(`src/Vactrol.hpp`, which documents its sources and fit): resistance against LED current from
the databook's curve (about 46 kΩ at 1 mA, 5.5 kΩ at 5 mA, 3 kΩ at 10 mA, 1.25 kΩ at 40 mA, 20 MΩ
dark), then the cell's response in time — a rise to 63 % in about 3 ms, a fall to 100 kΩ in about
19 ms from 10 mA, and the **memory**: three pools of conductance (fast, ~6 ms, ~60 ms) so the last
decades of resistance take an order of magnitude longer than the first, and a cell that has been
lit a while lets go more slowly than one that only flashed. The cell is in series with the audio,
dividing against an assumed 100 kΩ downstream input impedance — the schematic's `R12` has no
fixed divider partner, so whatever follows it supplies the other half. **LAG has no effect in
this mode**: the lag is the part's own, and slewing the drive too would count it twice. The
module's VACTROL read-out shows the gain the cell is actually passing.
Which part is **inferred**, not stated: the course schematic says only "LDR Vactrol" (and, on the
Percussive Noise Voice, "5-10k to 500k ohm", which the VTL5C3's 5 mA to 0.25 mA range matches).
The **Vactrol part** menu option swaps in a Silonex NSL-32 or a DIY LED + GL5528 (below). The **low-pass gate** menu option closes a
one-pole filter (20 Hz–15 kHz) in step with the gain, so the channel darkens
tonally as it closes, not just in level — the Buchla LPG behaviour the bare
schematic doesn't have on its own.

**Vactrol parts.** The part is an option (context menu), the VTL5C3 by default and bit-identical to
what it has always been. Each part carries its own LED law, its own resistance-against-current
anchor table and its own time constants (`src/Vactrol.hpp`, where every source is listed):

| part | R at 1 mA / 5 mA / 20 mA | closed (dark) | opens to 63 % | lets go |
|---|---|---|---|---|
| VTL5C3 (default) | 46 k / 5.5 k / 1.7 k | 20 MΩ, leaks 0.5 % into 100 k | 3 ms | 20 ms to 50 % gain, a 60 ms tail (memory) |
| NSL-32SR2 | 122 / 48 / 24 Ω | 5 MΩ, leaks 2.0 % (-34 dB) | 5 ms (sheet) | 5 ms to 100 kΩ from 16 mA (sheet); 5 ms to 50 % |
| NSL-32SR3 | 293 / 115 / 61 Ω | 25 MΩ, leaks 0.4 % | 5 ms (sheet) | 10 ms to 100 kΩ from 5 mA (sheet); 11 ms to 50 % |
| LED + GL5528 | 1.6 k / 0.6 k / 0.26 k | 2 MΩ, leaks 4.8 % (-26 dB) | 20 ms (sheet) | 30 ms constant: 180 ms to 50 % |

The NSL-32s are the fast, low-resistance cell: against the 100 kΩ load they are fully open at any
LED current above a few hundred microamps (the control is nearly a switch with a 5-10 ms
release), and an LED limit of 25 mA clamps the drive (the VTL5C3's is 40 mA, the DIY LED's 30). The
DIY pair is the slowest and the leakiest. **Assumed, not on a sheet:** the NSL's LED (2.0 V at
20 mA; the sheet bounds it under 2.5 V), the DIY LED (5 mm green, 2.1 V), the DIY coupling (378
lux per mA of LED current, fixed by one forum report of ~400 Ω at 10 mA), the GL5528's rise/decay
definitions (63 % up, 37 % down), and the SR2's 5 MΩ typical dark. **Not modelled for the three added parts:**
any slow light-history tail (the NSL sheets give none to fit; real DIY pairs are reported to ring
longer than a single 30 ms constant and to take seconds to recover to megohms), temperature (the
NSL-32's 0.7 %/°C is quoted, not applied), and sample spread (a surplus NSL-32SR3 measured at
modularsynthesis.com took 120 ms to reach 100 kΩ, twelve times its sheet: the model follows the sheet).

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

- **OTA: BIAS trimmer** — the 1k trimmer's wiper, 0-100 %, 50 % balanced (default).
- **OTA: remove the output's DC rest level** — the board's Out sits at -1.4 V; this
  high-passes it at ~2 Hz. Off by default (the board's own character).
- **OTA: exponential CV response (Iabc squared)** — squares the control current against 1 mA,
  in place of the schematic's inherently linear current. Off by default.
- **Vactrol: low-pass gate (filter tracks gain)** — see above. Off by default
  (the schematic is a bare divider, not a filter).
- **Vactrol part** — which vactrol the VACTROL mode is (saved in the patch, applies to every
  channel): **VTL5C3 (PerkinElmer)** (default; the model above, unchanged), **NSL-32SR2
  (Silonex / API)**, **NSL-32SR3 (Silonex)** or **LED + GL5528 (DIY)**. See "Vactrol parts" below.

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
- **The OTA** is the board's node equations (three Newton unknowns: the two pins and the diode
  bias pin) around the datasheet's transfer function, not a transistor-level chip. ASSUMED:
  +-12 V rails; a nominal part (no offset voltage, typical 0.4 mV); Vt at 300 K; the
  junction saturation current fitted to datasheet Figure 10 (read off a graph); the output
  resistance and swing read off Figures 12 and 5; the input transistors' beta (625) from the
  typical 0.4 uA bias current and the buffer's (73 a stage) from its 0.5 uA at 5k; the
  trimmer centred. Left out: the chip's 2 MHz bandwidth and 5 pF, temperature, the 470 nF's
  leakage. The channel's output slews with the LAG control (on the control volts), which the
  board does not have.
- The vactrol is the VTL5C3's published curves, digitised by eye and fitted (about 20–30 % rms in
  log resistance on the response-time plots; the turn-on of the 10 mA curve from a 1 MΩ start is
  not reproduced), and the part itself is an inference (see VACTROL above). Temperature, the
  part-to-part spread (2–3× at 1 mA between samples of the same number) and any multi-second
  dark-recovery tail are not modelled. The 100 kΩ assumed load is a guess.
- LAG is a shared knob applied to OTA and JFET AM only; VACTROL ignores it because the cell
  supplies its own lag.

# Deduction

Schedule A: itemised deductions. Six well-known filter/distortion circuits from
the *Modular in a Week* (MiaW) course, one model at a time behind a single set
of controls. 13 HP, polyphonic.

Part of the [Moon Technologies](../README.md) plugin.

## What it is based on

Six MiaW designs, credited by circuit rather than by episode:

1. **PAiA 2720-3L** -- two BC549C stages: a common-emitter gain stage with a twin-T
   from its base round to its collector (the R-C-R arm fixed; the C-R-C arm's shunt
   a 1N4148 whose dynamic resistance the control current sets) and an emitter
   follower out through a coupling cap. This one is **solved as the circuit**
   (`src/Deduction/PaiaCircuit.hpp` on the nodal solver `src/Mna.hpp`: ten nodes,
   two Ebers-Moll transistors with the Early effect, the diode, trapezoidal
   capacitors, Newton at each step), not drawn as a filter: the clipping is the
   transistors' and the diode's own, and the diode's distortion is in the signal path.
2. **Escobedo Q&D VCF** -- a 2-pole state-variable low-pass whose resonance
   path runs through an asymmetric soft clipper (so the peak folds over rather
   than screaming) ahead of a dirt stage.
3. **Korg35** -- the MS-10/MS-20's low-pass: two one-pole filters in a loop
   closed by a saturating amplifier, self-oscillating once the loop gain
   reaches 2.
4. **MS-20 OTA high-pass/low-pass** -- the Korg35 topology again, but the
   loop's amplifier clips against a diode pair instead of a soft tanh, for a
   harder, more aggressive character. This is also where the module's HP IN
   path comes from: MS-20 filters run the same loop in reverse for the
   high-pass side.
5. **EFM Moog-type high-pass** -- a four-pole ladder of one-pole high-pass
   filters, each leading 45 degrees at the corner, closed by a tanh feedback
   path; loop gain 4 is self-oscillation.
6. **Synthrotek DIRT** -- two CMOS inverter stages, each with an RC in its
   feedback (so each is a one-pole low-pass with a steep, bounded transfer),
   and the pair's output fed back to the input through RES.

Credit: MiaW course material (Kristian Blasol), PAiA, Escobedo, Korg, EFM
Retrofit and Synthrotek for the circuits themselves. Panel labels are read from
the MiaW panel SVGs (Day 7, sheets 7.1-7.6) and the PAiA 2720-3L schematic;
where only a panel exists, the module models the well-known circuit it names.
Every model is reimplemented as a topology-preserving (TPT / zero-delay)
digital filter, not lifted from the original schematic component-for-component,
so it is stable at any cutoff and any sample rate -- the original circuits are
neither.

## Controls

### DEDUCTIONS -- the filter itself

| Control | What it does |
|---|---|
| **CUTOFF** | 20 Hz - 20 kHz, exponential. The primary control. |
| MODEL | Which of the six circuits is running: PAiA, Q&D, Korg35, MS-20, EFM or DIRT. A 6-position snap control; changing it while a cable is patched still lands on a whole step. Switching models crossfades over about 20 ms so nothing clicks. |
| RES | Resonance / feedback. Meaning per model: PAiA is the board's R11 gain trimmer (1k at zero down to 1 ohm at full: the loop gain, so the peak's height; it never self-oscillates); Q&D, Korg35 and MS-20 raise the loop gain toward self-oscillation; EFM raises the ladder's feedback toward self-oscillation; DIRT raises the amount of output fed back to the input. |
| DRIVE | Level into the nonlinear stage. Meaning per model: PAiA and Q&D's input gain (for the PAiA, the level into R1, 1/8 to 8 of 5 V); Korg35's saturation amount; MS-20's diode threshold (inverted -- higher DRIVE means an *earlier*, harder knee); EFM's ladder input level; DIRT's operating-point BIAS, which can choke the signal to silence at either extreme, as the real CMOS inverter's transfer curve does. The light beside it reports the stage is driven past its own clipping knee. |
| NRM/INV | Flips which way CUTOFF's CV drives the frequency: NRM raises cutoff on a rising CV, INV lowers it. From the Q&D's own CV Response switch, applied here to the whole module rather than to one circuit. |

### WITHHOLDING -- what the CV inputs may take off each control

Four trimpots, each directly over its own jack, each pair sharing one label.

| Pair | Range |
|---|---|
| CUTOFF | 1 V/octave at the trim's full clockwise position (100 %); the trim is an attenuverter, so past centre the response inverts, and NRM/INV inverts it again on top. |
| RES | Attenuverter; 10 V at full trim covers the whole 0-100 % range. |
| DRIVE | Attenuverter; 10 V at full trim covers the whole 0-100 % range. |
| MODEL | Attenuverter; 10 V at full trim walks the selector across all six positions. |

## Jacks

| Jack | Direction | Convention |
|---|---|---|
| CUTOFF, RES, DRIVE, MODEL (WITHHOLDING) | in | CV, see above. |
| LP IN | in | Feeds the model's low-pass path (or its only path, for PAiA/Q&D/EFM/DIRT, which have no separate high-pass input). |
| HP IN | in | Feeds the model's high-pass path -- meaningful only for Korg35 and MS-20, which run two one-poles in a loop with a genuine low-pass and high-pass tap. On every other model HP IN is simply summed with LP IN before the stage, the same as feeding both into one input. |
| OUT | out | LP IN and HP IN's contributions, summed, clamped to +-12 V. |

Audio is nominally +-5 V; 1.0 inside the filter cores represents 5 V, which is
where each original circuit's own diodes and transistors have their knees.
Bypass (when the module is disabled) passes LP IN straight to OUT.

## Polyphony

Fully polyphonic: each channel gets its own filter state, its own model
crossfade, and its own oversamplers, driven by whichever of LP IN / HP IN
carries the most channels. Because MODEL CV can be polyphonic too, different
channels can legitimately run different circuits at once -- the read-out and
the DRIVE light always describe channel 1.

## The read-out

Top row: CUTOFF in Hz (or kHz above 1 kHz) and the loaded model's short name.
Bottom row: RES and DRIVE as they stand once their own CV is added in. All four
reflect channel 1.

## Context menu

* **2x oversampling** -- runs each channel's filter at twice the engine's
  sample rate and decimates through a windowed-sinc half-band low-pass, to
  push the models' clipping harmonics (Q&D, Korg35, MS-20, EFM, DIRT all clip
  in the loop) further from Nyquist at high RES or DRIVE. Off by default;
  the setting is saved with the patch.

## Notes on fidelity and what was approximated

* Except the PAiA, every model is a from-scratch digital reconstruction of the circuit's
  behaviour -- topology, self-oscillation threshold, single- versus
  double-ended clipping -- not a component-level circuit simulation, so exact
  cutoff-to-control-voltage curves and clipping thresholds are chosen to match
  the character described rather than measured against real hardware.
* **PAiA (solved circuit).** CUTOFF tunes the twin-T's peak: the module turns it into the
  control voltage that peaks there (a table built from the circuit's own small-signal
  response, with R11 at 100 ohms), so CUTOFF reaches only what the board does, about
  430 Hz to 5 kHz, and clamps outside it. The board's response is -1 dB at its peak with
  R11 at 100 ohms, about -14 dB at 1k and +16 dB at 1 ohm; there is no make-up gain, so
  the PAiA is quieter than the other five unless RES is up. A 5 V input already compresses
  it, as it would the board. Assumed, because the drawing does not give them: the 12 V
  supply, the BC549C's model (Is from 0.58 V at 0.5 mA, hFE 520, VAF 100 V), a 100k load
  on the output, and no junction capacitances. It costs more than the other models (a 10x10
  solve about twice per sample at 48 kHz, roughly 10 % of a core per voice), so a
  polyphonic PAiA is expensive; the other five models are unchanged. The solver runs at 96 kHz
  or more however fast the module is.
* HP IN only does something distinct from LP IN on Korg35 and MS-20, which is
  the one place the MiaW panels document a separate high-pass tap. Feeding
  both models with only the topology to run one path (PAiA, Q&D, EFM, DIRT)
  sums LP IN and HP IN ahead of the stage, so patching either jack alone works
  exactly the same way.
* EFM's high-pass ladder is documented on its panel as a Moog-type circuit with
  no published schematic in the source material; it is modelled as the
  well-known four-pole high-pass ladder the panel names, mirroring the
  low-pass ladder's usual topology and self-oscillation behaviour.
* DRIVE's per-model meaning is deliberately not unified into one "gain" knob:
  MS-20's DRIVE sets a clip threshold (so turning it up tightens the knee) and
  DIRT's sets a bias point (so the extremes mute rather than merely soften),
  matching what each original control actually did rather than forcing one
  convention onto all six.
* Model changes crossfade over a fixed ~20 ms regardless of CUTOFF, RES or
  DRIVE, so switching models rapidly under CV is audible as a series of short
  blends rather than clicks.

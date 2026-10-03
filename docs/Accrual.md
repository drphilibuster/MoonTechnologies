# Accrual — 4069 saw & pulse VCO · FORM 3115

**The Modular in a Week course's 4069 VCO, solved as the circuit it is.** A
BC560/BC550 pair makes an exponential current from four summed voltages; that
current charges a CD4069UB inverter turned into a Miller integrator; two more
inverters make a Schmitt trigger that dumps the integrator when it reaches the
top; a fourth turns the ramp into a pulse whose width a pot and a CV jack set.
Saw and pulse come out of the board's own 220 nF coupling capacitors. 8 HP,
polyphonic (the poly channels of CV1, CV2 and PWM, up to 16 voices).

Form 3115 is the IRS's *Application for Change in Accounting Method* — Accrual is
the method, and the form to file when you change it.

## Where it came from

`Unfinished projects/Schematic_4069vco` in the course folder (kristian.borgstedt,
EasyEDA, 2019-12-06): one sheet, six inverters of a 4069, three transistors, two
diodes. It is not the same thing as SixFigures' "4069 AAC" core, which is the All
About Circuits square-wave VCO and lives in [SixFigures](SixFigures.md); this one
makes a saw and a pulse, and it is the circuit on the sheet, not a model of what
such circuits sound like.

## What the circuit does

1. **Exponential converter.** TUNE, FINE, CV1 and CV2 sum through 100k, 1M, 100k
   and 100k into node A, which the trimmer R21 and R6 (1k5) hold near ground
   — A moves only tens of millivolts. Q1 (BC560, PNP) sits from A to Q2's base;
   R7 (1M) feeds Q1's emitter from +12 V. So V<sub>BE</sub>(Q2) − V<sub>EB</sub>(Q1)
   = A and Q2's collector current is I<sub>Q1</sub>·e<sup>A/V<sub>t</sub></sup>:
   one octave for every 17.9 mV on A. R21 sets how many volts at a CV input that
   is. Q1 is a PNP and Q2 an NPN, so unlike a matched pair their temperature
   drift does not cancel; this model sits at 300 K.
2. **Integrator.** Q2 pulls its current out of the input of inverter U1.1; C1
   (2n2) from the inverter's input to its output (through R8, 680 Ω, with D1
   across it) integrates it. A CD4069UB is an *unbuffered* inverter, which is the
   point: its gain around the switching voltage is only about 23, so it is a
   usable amplifier, and the Miller integrator is as good as that gain and no
   better.
3. **Schmitt reset.** The ramp goes through R9 (10k) into U1.2 and U1.3, which
   are two inverters in a row, with R10 (22k) feeding the output back to the
   input: a comparator with hysteresis. When the ramp reaches the top its output
   S goes high; D2 then pulls the integrator's input up, the output falls fast
   (D1 short-circuits R8), and S falls again once the output has come down
   through the lower threshold. The whole reset takes about five microseconds.
4. **SAW** is the integrator's output through C2 and R11 (a 7.2 Hz high-pass).
5. **PULSE.** U1.4 is an inverter with R14 from its output to its input — an
   inverting summer — adding the ramp (47k), the PW pot's wiper (100k), +12 V
   (68k) and, from Q3's emitter, the PWM CV (100k). U1.5 and U1.6 square the
   result. The pulse is high while the ramp is *below* a threshold that PW and
   PWM move, then through C3 and R16 (another 7.2 Hz high-pass).

## What this module is, and what it is not

The board is solved, not imitated. The exponential pair and the PWM follower are
solved per sample by Newton's method (two junction equations, the Early effect,
the base currents). The integrator, Schmitt trigger and reset are a relaxation
oscillator whose only slow variable is the voltage on C1, so the module works
out its behaviour *once* (per supply voltage and input capacitance) by running
the real circuit through the nodal solver in `src/Mna.hpp` and `src/Cd4069.hpp`,
and then plays it back at audio rate:

- the **ramp** is a table of the inverter's DC curve, loaded by R9 + R10 and R12,
  not a straight line — it bends where the inverter's gain bends, and Q2's
  collector current changes with its collector voltage (the Early effect, which
  with the BC550C's Early voltage is 20 % across the ramp);
- the **trip point** is the fold of that curve (where the stable state with S low
  stops existing), found by continuation, then refined by a transient of the
  reset at fifteen ramp currents;
- the **reset** is a 5 µs event at an exact moment *inside* the sample, with
  polyBLEP on the saw's step and the pulse's edges, so the oscillator is pitch
  accurate to 0.03 % from 44.1 to 192 kHz sampling and its edges do not alias;
- the **pulse edges** are timed to a fraction of a sample (an edge inside the
  reset is found on the stored path of the integrator's output);
- the **high-passes** are exactly C·R = 22 ms.

It is **not** a sample-rate-stepped copy of the whole netlist (the netlist is solved
only in the test suite, at steps down to 20 ps, as the oracle).

## Controls

| control | what it does |
|---|---|
| **TUNE** | The Tune pot's voltage into the summing node: ±5 V by default (menu: ±12 V, the pot across the rails). 100k: one octave per volt once R21 is set. |
| **FINE** (on the screen) | The Fine pot, through 1M: a tenth of a volt of TUNE per volt, so ±0.5 octave. |
| **CV1, CV2** | 100k each into the summing node, 1 V/oct after calibration. Polyphonic. **Unpatched, they are open** (they are resistors to nothing on the board): patching CV2 with 0 V on it does change the scale slightly, by the extra 100k on A. |
| **PW** (on the screen) | The PW pot's wiper (+12 V to ground, assumed 100k linear) into U1.4 through 100k. The percent shown is the pot's position; the pulse's duty is a function of it, of PWM and of where the ramp sits (see below). |
| **PWM** | Q3's emitter follower: the PWM jack through 100k and 1M. **Open when unpatched** — Q3's base then rests on its own base current through the 1M, and the unpatched pulse is offset from the PW pot's centre because of it. Polyphonic. |
| **SAW, PULSE** | The two coupled outputs, in real volts: the saw is about 4.7 V peak to peak (the ramp from about 3.5 V to 8.3 V at 12 V), the pulse 12 V (0 to VDD before the coupling), polyphonic. |

The read-out is the integrator itself, one cycle of the voltage at the saw node,
the PWM threshold as a line (the pulse is high below it, shaded) and a marker for
where the cycle is. The pitch in hertz is TUNE; FINE and PW are the two other
fields. All three are drags.

### Pitch

At the calibrated trimmer (R21 = 403 Ω, the value that gives one octave per volt
with both CV jacks patched):

| TUNE (±5 V range) | volts | frequency |
|---|---|---|
| fully down | −5 V | 4.5 Hz |
| −2.5 V | −2.5 V | 24 Hz |
| centre | 0 V | 136 Hz |
| +2.5 V | +2.5 V | 766 Hz |
| fully up | +5 V | 4.27 kHz |

CV1 and CV2 add to that. The module will not run faster than it can be sampled: a cycle
is kept to about 2 samples, so the top is just under half the sampling rate.

The *absolute* pitch of the 0 V point depends on things a schematic cannot tell
you (see below); the **tracking** — one volt, one octave — does not. On the
whole circuit, a volt of Tune is 1.00 octave ±1 % from 4.5 Hz to 4 kHz.

## The left-click menu (saved in the patch)

| item | meaning |
|---|---|
| **R21 slider, Calibrate** | The v/oct trimmer, 0 to 1 kΩ, as on the sheet. 0 Ω gives 0.8 octave per volt, 1 kΩ gives 1.3. *Calibrate* sets the value that gives one octave per volt with both CV inputs patched. |
| **CD4069 supply VDD** | 9, 10, **12 (as drawn, VDD = VCC)**, 15 V. The sheet leaves the 4069's supply pins unlabelled; this is what they are inferred to be. VDD sets the ramp's height, the pulse's amplitude and, a little, the pitch (165, 153, **136**, 121 Hz at TUNE 0). |
| **CD4069 input capacitance** | 5, **10 (the datasheet's typical)**, 15 (maximum), 20 pF. This is the biggest lever on the absolute pitch (144, **136**, 130, 124 Hz at TUNE 0): the Schmitt loop's input capacitance sets how long S stays high, and so how deep the reset dumps C1. It is real: a 20 pF stray changes the oscillator by 9 %. |
| **Tune and Fine pot range** | ±5 V (default) or ±12 V. The sheet draws Tune and Fine as bare pads; the pot's end voltages are not on it. |

## What is assumed, and what it costs

Everything below is a named constant in `src/Accrual/Vco.hpp`, or a menu item.

| assumption | where it comes from | how much it matters |
|---|---|---|
| The CD4069UB's transfer curve | `Cd4069.hpp`, fitted to the TI datasheet's typical curves (10–20 % on the curves) | **Gain at 12 V is only bounded by the datasheet, −11 to −30; the model sits at −23.1.** Across the whole bound the pitch at TUNE 0 goes from 133 to 152 Hz (the test suite prints it). |
| Input capacitance 10 pF | TI SCHS054E, CIN typical | −5 % of frequency per +5 pF |
| BC550C | Philips SPICE set (Is 7.05 fA, BF 493, VAF 23.9 V, Cje 11.5 pF, Cjc 5.5 pF) | Is only sets the 0 V pitch; **VAF sets the 20 % bend of the ramp** |
| BC560C | Is 60 fA, VAF 160 V, Cje 19 pF, Cjc 3.9 pF from a diyaudio set; **BF = 520 and BR = 4 are assumed** (that set's BF = 900 is outside the datasheet's 420–800) | Is of the pair moves the 0 V pitch by octaves; **a real pair has a different offset** (calibrate by ear: the trimmer and TUNE are there for it) |
| 1N4448 as the 1N4148 set | `Cd40106.hpp` (Is 2.52 nA, n 1.752) | the 2.5 nA reverse leakage of D2 shows at the lowest pitches |
| VDD = VCC = 12 V | inferred | see the menu |
| PW pot 100k linear, ends at VCC and ground | the sheet's pads P9–P11 | ±0.03 % of the period |
| Tune/Fine pot ends ±5 V | the sheet has bare pads | menu |
| 300 K | | the pair does not compensate for temperature; a real board drifts |

Sources: the CD4069UB datasheet (TI SCHS054E, <https://www.ti.com/lit/ds/symlink/cd4069ub.pdf>);
the Fairchild BC546–BC550 datasheet (hFE, Cob; `MiawResearch/research4069/`); the BC550C
Philips SPICE set (<https://mikrocontroller.net/attachment/263906/_AM_Mittelwellen-Pruefsender.pdf>);
the BC560C set (<https://www.diyaudio.com/community/threads/modifying-transistor-spice-models-to-simulate-typical-device-spread.397317/>);
National Semiconductor AN-88 on CMOS linear amplifiers (`MiawResearch/research4069/`).

### What is not modelled

- **Temperature**, as above.
- The reset throws the integrator's output about 1.5 V above its trigger level for
  about a microsecond. It is in the *pulse* (the threshold crossings are found on
  that path) but on SAW the reset is drawn as a step.
- The pulse's edges trail the ideal threshold crossing by up to about a
  microsecond at 2 kHz (the PWM chain's input capacitances): at that pitch a
  quarter of a percent of the duty.
- The PWM summing node loading the ramp differently on its two sides (at worst
  0.04 % of the period).
- The outputs' load (open); supply ripple.
- Above half the sampling rate the oscillator is clamped (a real board would go on
  to 100 kHz and beyond; the audio would only be the alias).

## How it is checked

`tests/Accrual` (103 checks) steps the **whole netlist of the PDF** — 16 unknowns,
three BJTs, six inverters, two diodes, twelve capacitors, with the 4069's input
capacitances and the transistors' junction capacitances — with an adaptive
trapezoid and holds the audio-rate model to it:

- the **PNP** that `Mna.hpp` gained for Q1, against a one-unknown Ebers–Moll solve
  written out in the test and against the NPN it mirrors;
- the **exponential converter** against the netlist's own DC solution (stamps, not
  the hand-written Jacobian);
- the **period**, 4.5 Hz to 4 kHz at 12 V (worst 0.09 %), at 9, 10 and 15 V, at 5, 15 and
  20 pF (within 0.4 %), with all four inputs summing and with CV2 open;
- the **shape of the integrator's output** over a cycle (3 mV rms) and the **pulse
  duty** over ten PW/PWM settings (within 0.2 % of the cycle);
- the saw's **amplitude**, the **AC coupling** (means near zero), **sample-rate
  independence** (the period at 44.1, 48, 96 and 192 kHz within 0.03 %), and the
  **band-limiting**: the saw at 1.2 kHz has −53 dB of non-harmonic energy below
  8 kHz with the polyBLEP and −21 dB without it, so the test can see the difference;
- six **negative controls**: C1 left 100 mV high, D2's leakage forgotten, no Early
  effect, the pulse threshold without R15, the transistor parameter sets swapped,
  and the edges read at eleven samples a cycle.

It costs about 2 % of a core per voice at 48 kHz, with CV and PWM moving every sample.

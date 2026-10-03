# Diversified — multi-effects · FORM 1099-B

**One hundred and six effects on one screen.** The ninety-nine programs of the
DSP99 board, then the seven dedicated Modular in a Week effect circuits, in one
stereo multi-effect with eight macros whose meaning changes with the
program. 14 HP, stereo in, stereo out, plus an effects loop.

Form 1099-B is the IRS's *Proceeds From Broker and Barter Exchange
Transactions* — the form that reports what a diversified portfolio actually did.
The read-out holds the program, the clock division and the macros, ALLOCATION is
what CV may take off each of them, HOLDINGS is the blend and the two gates.

## What it is based on

Everything here comes from **Modular in a Week**, Kristian Blåsol's DIY series,
and from the boards and sheets published with it.

* **DSP99** — the numbered reverb/delay board that Day 12 puts behind a rotary
  switch. Its user interface is a number from 0 to 99 and a printed sheet saying
  what each number is (`dsp99_english_effects_sheet`). The sheet is a taxonomy —
  three small halls, three medium halls, seven plates, nine delays — not
  ninety-nine algorithms, and it is treated as one here.
* **Echomatic** (Day 12, panel *12.3 Echomatic wider*) — a PT2399 echo with
  Feedback, Level, Mix and a TO FX / FROM FX insert.
* **Little Angel PT2399 Chorus** (Day 12, panel *12.6*) — Depth, Speed, a
  Vibe/Chorus switch and a Normal/Warble switch.
* **Spring reverb** (Day 12, `Schematic_Spring reverb_2020-09-01`) — driver amp
  into a tank, with dwell and tone.
* **MXR Distortion+** — the pedal, via the Falstad model in
  `OtherStuff/Falstad models/MXR Distortion +.txt` and the Day 13 panel *13.1*.
  Credit for the circuit is MXR's; credit for the board is MiaW's.
* **Talk Funny** (Day 13, panel *13.6*) — FM Intensity, Frequency, Chopper,
  Function, Modulation I/II, FM Source.
* **MW Bitcrusher / Bit swapper / Sample rate reducer** (Day 13,
  `Schematic_BIT crusher_2021-07-06` and panel *13.7*) — an ADC0809 with its
  eight data lines on jacks feeding an R-2R ladder with eight jacks of its own.
* **4011 ring modulator** (Day 13,
  `Schematic_simple ringmodulator with a 4011_2021-04-06`) — four NAND gates and
  two diodes.

## Panel

### The read-out

Everything the portfolio holds is on the glass under the masthead, and every
value there is a control. A choice from a list (PROGRAM, DIV) opens the list
when you click it, or steps through it as you hold and drag; a value (the
macros) is held and dragged up or down, Ctrl for fine and Shift for coarse.
Right-click any of them for the usual parameter menu: typed entry, MIDI-Map,
reset.

| control | what it does |
|---|---|
| **PROGRAM** | The top line: the number in seven-segment and the program's name. 0–105: 0–98 is the DSP99 sheet, 99–105 the dedicated boards. Click it to pick a program by name, or drag through them -- the bank is a ring, so past 105 is 0. Changing it crossfades over about 30 ms, so a reverb tail survives the change |
| **DIV** | Beside it: what the clock at CLOCK is worth -- 1/4, 1/3, 1/2, 2/3, ×1, dotted, ×2, ×3, ×4. Click to pick. Mint while a clock is locked. A delay exactly on the beat is rarely the one you want, and without this the clock's own tempo was the only one on offer |
| **P1–P8** | The eight macros, two rows of four. Each shows what it *is* for the running program -- the program's own name for it -- over its value, CV included. A macro the program has nothing for says `--` and is dimmed |
| **ACTIVE** | The light beside the ALLOCATION caption; follows the level of the wet signal |

**The macros work differently on the two banks, on purpose.**

On **0–98** a macro is a *trim about the program's own setting*. At 50 %, the
macro is exactly what that number is; either end still reaches the parameter's
limit. This is what keeps the seven numbers the sheet calls "plate" seven
different plates rather than one plate with the macros in seven identical
settings — turn PROGRAM with the macros untouched and the sound really changes.

On **99–105** a macro is the board's own knob, absolute, because on the
Echomatic FEEDBACK means feedback and nothing else.

### Why some macros say `--`

Every program carries eight parameters — four in each of its two blocks — and
the program sheet names three of them. **P4 to P8 are whichever slots that
program still has and is not already spending**, worked out from what the
running algorithm actually reads rather than hand-authored into the table:

| algorithm | slot 1 | slot 2 | slot 3 | slot 4 |
|---|---|---|---|---|
| reverb | SIZE | DECAY | TONE | SHAPE |
| delay | TIME | FEEDBK | TONE | SPREAD |
| modulation | RATE | DEPTH | VOICES | TONE |
| phaser | RATE | DEPTH | FEEDBK | STAGES |
| pitch | PITCH | MIX | FEEDBK | WINDOW |
| tone | FREQ | Q | ENV | — |
| bitcrusher | BITS | RATE | TONE | — |
| MiaW boards | P1 | P2 | P3 | — |

Across the bank that comes out as:

| real parameters | programs |
|---|---|
| 3 | 11 |
| 4 | 69 |
| 6 | 2 |
| 7 | 2 |
| 8 | 22 |

So most programs leave several macros reading `--`, and that is fine: a macro
that says it has nothing to do costs you nothing, while a parameter with no
macro cannot be reached at all. Twenty-two programs use all eight, and before
there were eight macros those five extra parameters were simply unavailable.

### ALLOCATION

A trimmer over a jack for each of PROG, P1 through P8, and MIX, in two rows of
five: PROG and P1–P4, then P5–P8 and MIX. Each trimmer is an
attenuverter, ±100 %; each jack takes 0–10 V (or ±5 V through a negative
trimmer setting). PROG's CV spans the whole bank, so a ramp sweeps all 106
programs. The trimmers are excluded from randomisation.

### HOLDINGS

| control | what it does |
|---|---|
| **MIX** | Dry to wet, 0–100 %. Default 50 % |
| **AUX** | A gate or a CV, whichever the running program wants — see the table below. Gate threshold 1 V, Schmitt hysteresis down to 0.1 V |
| **CLOCK** | A clock or a tap. Two edges give a period, and every delay-based program uses it in place of its TIME macro, scaled by DIV on the glass. The light beside it is lit while a clock is locked; pull the cable or stop clocking for 3 s and the macro takes over again |

### The footer

| jack | direction | what it is |
|---|---|---|
| **IN L** | in | Left audio, ±5 V. A polyphonic cable is summed |
| **IN R** | in | Right audio; normalled to IN L, so one cable is dual mono |
| **SEND** | out | Effects loop send |
| **RETURN** | in | Effects loop return, normalled to SEND |
| **OUT L / OUT R** | out | ±5 V nominal, hard-clamped to ±12 V |

**The loop.** On program **99 Echomatic** the loop is what the board's TO FX /
FROM FX jacks are: an insert *inside the feedback path*, so a distortion patched
across it gets dirtier with every repeat rather than once. With nothing patched
the send is what comes back and the echo behaves exactly as it does with the
jacks empty.

On every other program the loop is a mono insert across the wet path: SEND
carries the summed wet signal before MIX, and anything at RETURN replaces it.
Because the insert is mono, using it on a stereo program collapses the wet path
to mono; that is the trade, and leaving RETURN empty avoids it.

Bypass (Rack's own) passes IN L and IN R straight to OUT L and OUT R.

## Why there are no macro knobs

The eight macros used to be eight knobs under the glass, each wearing a little
lit plate the module wrote the macro's name into, with PROGRAM a pair of step
buttons and DIV a knob of its own. The glass already showed the program, so
those were the same numbers twice, and the knobs' two rows were what kept
ALLOCATION in one long row of nine. With the macros, PROGRAM and DIV as fields
on the glass -- the same parameters, so patches, CV, MIDI-Map and undo are
unchanged -- ALLOCATION folds into two rows and the panel is as narrow as its
audio row allows. A field drags exactly as far as a knob does for the same
movement of the mouse; MIX keeps its knob because it is the one control that is
not on the screen.

## The programs

Everything on the DSP99 sheet is built from twenty real algorithms — seven
reverb characters, four delay characters, five modulation characters, a phaser,
four filters, a granular pitch shifter and a bitcrusher — arranged into the
bands the sheet names, with each number in a band a different setting of its
band's algorithm. A program is at most two blocks in series, which is enough for
the whole sheet, because every combination it lists is exactly two things in a
row.

| No. | name | what runs | P1 / P2 / P3 |
|---|---|---|---|
| 0-2 | SMALL HALL | hall | SIZE / DECAY / TONE |
| 3-5 | MEDIUM HALL | hall | SIZE / DECAY / TONE |
| 6-8 | LARGE HALL | hall | SIZE / DECAY / TONE |
| 9 | CHURCH | hall | SIZE / DECAY / TONE |
| 10-12 | SMALL ROOM | room | SIZE / DECAY / TONE |
| 13-15 | MEDIUM ROOM | room | SIZE / DECAY / TONE |
| 16-18 | LARGE ROOM | room | SIZE / DECAY / TONE |
| 19 | HALL | hall | SIZE / DECAY / TONE |
| 20-26 | METAL PLATE | plate | SIZE / DECAY / TONE |
| 27-29 | SPRING VERB | spring | SIZE / DWELL / TONE |
| 30-35 | REVERB GATE | gated reverb | SIZE / DECAY / HOLD |
| 36-39 | REVERSE | reverse reverb | SIZE / DECAY / LEN |
| 40-43 | EARLY REFL | early reflections | SIZE / SHAPE / TONE |
| 44-47 | ATMOSPHERE | hall + ensemble | SIZE / DECAY / DRIFT |
| 48 | STADIUM | hall | SIZE / DECAY / TONE |
| 49 | AMBIENT FX | hall + pitch shift | DECAY / TONE / SHIMR |
| 50-52 | DELAY | mono delay | TIME / FDBK / TONE |
| 53-55 | STEREO DLY | stereo delay | TIME / FDBK / TONE |
| 56-58 | PING-PONG | ping-pong delay | TIME / FDBK / TONE |
| 59 | ECHO | tape echo | TIME / FDBK / AGE |
| 60-63 | CHORUS | chorus | RATE / DEPTH / WIDTH |
| 64 | VIBRATO | vibrato | RATE / DEPTH / TONE |
| 65 | TREMOLO | tremolo | RATE / DEPTH / SHAPE |
| 66-69 | FLANGER | flanger | RATE / DEPTH / FDBK |
| 70-73 | PHASER | phaser, 4/6/8 stages | RATE / DEPTH / FDBK |
| 74 | TONE LP | lowpass filter | FREQ / RESO / ENV |
| 75 | TONE BP | bandpass filter | FREQ / RESO / ENV |
| 76 | TONE HP | highpass filter | FREQ / RESO / ENV |
| 77 | WAH | envelope-swept bandpass | FREQ / RESO / ENV |
| 78-79 | LO-FI TONE | bandpass + bitcrush | FREQ / BITS / RATE |
| 80-81 | VERB+CHORUS | hall + chorus | SIZE / DECAY / MOD |
| 82-83 | VERB+FLANGE | hall + flanger | SIZE / DECAY / FLNG |
| 84-85 | VERB+PHASER | hall + phaser | SIZE / DECAY / PHSR |
| 86-87 | VERB+TONE | hall + lowpass | SIZE / DECAY / FREQ |
| 88-89 | DELAY+VERB | stereo delay + hall | TIME / FDBK / VERB |
| 90 | DELAY+GATE | stereo delay + gated reverb | TIME / FDBK / HOLD |
| 91 | DELAY+REVRS | stereo delay + reverse reverb | TIME / FDBK / LEN |
| 92-93 | DELAY+CHORUS | stereo delay + chorus | TIME / FDBK / MOD |
| 94-95 | DELAY+FLANGE | stereo delay + flanger | TIME / FDBK / FLNG |
| 96-97 | DELAY+PHASE | stereo delay + phaser | TIME / FDBK / PHSR |
| 98 | DELAY+PITCH | stereo delay + pitch shift | TIME / FDBK / PITCH |
| 99 | ECHOMATIC | MiaW Echomatic | TIME / FDBK / LEVEL |
| 100 | LITTLE ANGEL | MiaW Little Angel | SPEED / DEPTH / MODE |
| 101 | SPRING TANK | MiaW spring reverb | DRIVE / DWELL / TONE |
| 102 | DISTORTION+ | MXR Distortion+ | DIST / LEVEL / TONE |
| 103 | TALK FUNNY | MiaW Talk Funny | FREQ / FM INT / MODE |
| 104 | BITCRUSHER | MW Bitcrusher | LEVEL / BITS / RATE |
| 105 | 4011 RING | 4011 ring modulator | CARR / BIAS / TONE |

### Where this differs from the sheet

* The sheet's **82–83 and 94–95 "reverb + chrome" / "delay + chrome"** are a
  translation artefact: "chrome" is flanging. They are built as flangers.
* The sheet runs its delay-plus-transposition band over **98–99**. Program 99
  here is the Echomatic, so that band is program 98 alone.
* **64 vibrato** and **65 tremolo** sit at the top of the sheet's 60–65 "chorus"
  band. They are the degenerate members of the same family — a chorus with the
  dry path removed is vibrato, and one with the delay removed is tremolo — so
  the band ends with them rather than with two more chorus settings.
* **78–79** are the sheet's tone band taken lo-fi, which is where the bitcrusher
  lives inside the DSP99 half.

### What AUX and TAP do, by program

| program | AUX | TAP |
|---|---|---|
| 30–35, 90 gated reverb | gate: forces the door open | delay time (90) |
| 50–59, 88–98 delays | — | delay time |
| 101 spring tank | gate: TWANG, a kick to the box | — |
| 103 Talk Funny | CV: the FM source. Unpatched, the input's own envelope drives the carrier, which is the board's normalled behaviour | — |
| 104 bitcrusher | CV: sample rate, summed with the RATE macro | — |
| 99 Echomatic | — | delay time |
| everything else | — | — |

## How the dedicated circuits are modelled

**99 Echomatic.** The PT2399 itself, the model Amortization and Racketeer use
(`src/Pt2399.hpp`): a 1-bit adaptive delta modulator writing 44 kbit of RAM, a
demodulator reading it back, and a clock. It is not a delay line. The delay is
the RAM length over the bit rate, so TIME *is* the clock, and a longer delay is
a slower clock: less bandwidth, more slope overload and granular hiss, none of
it added. Moving TIME replays the stored bits at the new rate, so the pitch
bends the way it does on the board, and the knob is smoothed over about 60 ms so
a sweep is a bend and not a step. TIME is 30 ms to 1 s, or the TAP clock.
FEEDBACK goes past unity on purpose; what stops that being a fault is the chip —
its integrator and the input op-amp clip at the supply — so a loop at 1.25 sings
at a bounded level and gets dirtier as it does, rather than meeting a tanh added
to tame it. LEVEL is the echo's own output level, ahead of MIX.

**100 Little Angel.** The same chip run short, one for each side. The modulation
is of the clock, as on the board, so the pitch shift is the chip's own and not a
moving read pointer. The delay swings between 5 and 30 ms, and the clock that
implies is the clock the chip runs at: about 8.8 Mbit/s at 5 ms. MODE is one
macro carrying both of the board's switches, in four positions across the knob's
travel: chorus/normal, chorus/warble, vibe/normal, vibe/warble. VIBE kills the
dry path so only the pitch modulation is left; WARBLE adds the slower, irregular
drift that makes it sound like a tape motor rather than a chorus pedal. The board
is mono; the second chip, a quarter cycle on, is this module's stereo addition.

**101 Spring tank.** Four delay loops, each with an eight-stage allpass chain
inside the loop, because a spring disperses high frequencies ahead of low ones
and returns an impulse as a descending chirp rather than as a copy. DRIVE is the
driver transducer — the part that actually distorts on a real tank — with makeup
gain that tracks it so DRIVE stays a tone control rather than a volume. DWELL is
the loop gain, TONE the bandwidth. The output is highpassed at 120 Hz, which is
about where a tank stops.

**102 MXR Distortion+.** One op-amp in a non-inverting stage. The gain leg is a
1 MΩ pot to 4.7 kΩ in series with 47 nF, so the boost is 1 + Rf/4.7k above about
720 Hz and unity below it; the 1 nF across the pot rolls the top of that band
off again, which is why a Distortion+ is thick rather than fizzy. The op-amp
then clips against ±4.5 V rails and a germanium pair clamps the output through
10 kΩ at roughly a 0.35 V knee. Two times oversampled around the clipper, where
the harmonics that would alias are made. TONE is an addition — the pedal has
none — and is a post-clipper lowpass from 1.2 to 12 kHz.

**103 Talk Funny.** A band-limited (polyBLEP) square carrier, 2 Hz to 2 kHz,
frequency-modulated by the AUX jack or, unpatched, by the input's own envelope.
MODE is one macro across four positions: MODULATION I (ring — a bipolar
carrier, so the carrier itself cancels out of the output), I with the CHOPPER,
MODULATION II (AM — a unipolar carrier, so it stays), and II with the CHOPPER.
The chopper gates the output at a quarter of the carrier.

**104 MW Bitcrusher.** The board from `Schematic_BIT crusher_2021-07-06`,
chip by chip (`src/Adc0809.hpp`).

An **LTC1799** is the clock: 10 MHz × 10 kΩ / (N × R_SET), where R_SET is a 3k
resistor in series with a 1M pot and N is 1, 10 or 100 from the DIV switch.
RATE is the pot and AUX its CV; the **Clock divider (U6)** menu item is the
switch. An **ADC0809** converts for ever (START is tied to EOC on the schematic),
one conversion being 64 clocks plus the START-to-EOC gap, so the sample rate is
the clock over 72: about 139 Hz to 46 kHz across RATE at the default /10. The
input is taken as the conversion starts and the result arrives 64 clocks later,
so the converter has a latency of its own. A converter running faster than the
audio rate is averaged across each audio sample rather than aliased.

The eight data lines go into the board's **1k/2k ladder**, which sums into an
inverting op-amp (U7.1, Rf = 1k). The ladder is solved as the network it is: the
MSB is the leg nearest the op-amp and the LSB the one at the termination end —
the opposite of the jacks' top-to-bottom numbering — and a leg left open is not
the same as one driven low, because it stops loading its node and so moves the
other bits' weights slightly. BITS leaves the low legs open, as leaving a data
jack unpatched does. **Swap MSB and LSB** reverses the whole word before the
ladder sees it — the extreme of the board's joke, which is that the patch cable
between the converter and the ladder *is* the effect.

After the ladder: **SW1**'s passive filter (680 Ω into 68 nF to ground: one pole
at 3.44 kHz — not the second-order 5 kHz this entry used to claim; that was read
off the panel, and the schematic says otherwise), U7.2's non-inverting stage
(gain 1 + RP3 / 2k2, offered as 1×, 2×, 4× and the pot's 10×; 2× by default, which
makes the converter's full scale ±5 V), and C1 into the next input. The ladder
stage inverts, so **the output is the input upside down**, as it is on the board.
At the higher gains the converter's DC offset runs into the op-amp's rails, and
the output clips asymmetrically, which the board does too.

LEVEL is RP1, the 100k pot at the input: an attenuator, as on the board, so full
turn is the converter's full scale. (It was an input gain of up to 4× before; the
default is now full turn rather than 40 %.)

**The input range.** The ADC0809 reads 0 to 5 V and the board puts nothing in
front of it, so ordinary bipolar audio loses its negative half. By default a 2.5 V
offset is added ahead of the converter so the whole waveform is crushed, as it
always was here; **Board-faithful 0-5 V input** in the context menu removes it,
and then negative half-waves read as code 0, exactly as on the hardware.

**105 4011 ring modulator.** Reading the board: input 1 lands on pins 13 and 9,
input 2 on pins 8 and 1, pin 10 feeds pins 2 and 12, pin 3 feeds pin 5, pin 11
feeds pin 6, and the output leaves from pin 4. Naming pin 10's gate X:

```
X   = NAND(A, B)
p3  = NAND(B, X)
p11 = NAND(A, X)
out = NAND(p3, p11)   =   A XOR B
```

— the canonical four-NAND exclusive-or, which for two squarewaves is exactly
ring modulation. Each input reaches the gates through a 1N4148 and a 100 kΩ
pulldown, so only the positive half of a signal can raise a pin at all; the
comparators here have their thresholds up off zero for that reason, and BIAS
moves them. Input A is **IN L**. Input B is **IN R** when it is really patched,
and otherwise an internal carrier at CARR (1 Hz to 4 kHz), which is what makes
the program usable with one cable. **Smooth (analogue product)** in the context
menu replaces the gate array with the multiplication it is a caricature of.

## Context menu

| item | what it does |
|---|---|
| **Swap MSB and LSB** | 104 only. Reverses the eight-bit word before the ladder |
| **Reconstruction filter (SW1)** | 104 only. The board's 680 ohm / 68 nF lowpass (3.44 kHz, one pole), in or out of circuit. Default on |
| **Clock divider (U6)** | 104 only. The LTC1799's DIV switch: /1, /10 or /100. Default /10, which gives the 139 Hz to 46 kHz range |
| **Output gain (RP3)** | 104 only. U7.2's gain: 1×, 2×, 4× or 10×. Default 2× |
| **Board-faithful 0-5 V input** | 104 only. Takes the 2.5 V offset away from ahead of the converter. Default off |
| **Smooth (analogue product)** | 105 only. Analogue multiplication instead of the exclusive-or. Default off |

All of them are saved with the patch.

## Voltages, polyphony and CPU

Audio is ±5 V nominal; every output is clamped to ±12 V and every feedback path
is soft-limited and denormal-flushed, so no macro position produces a NaN or a
runaway. Gates are 0/10 V with a Schmitt threshold at 1 V. CV inputs are 0–10 V
through their attenuverters.

The module is **monophonic**: it is a stereo effect with long state, and a
polyphonic cable at IN L or IN R is summed rather than having its channels
dropped.

There are two complete engines, so a program change is a real crossfade rather
than a mute and a restart, and the delay buffers are sized once at construction
for 192 kHz. That costs roughly eight megabytes per instance and, briefly, twice
the CPU during the 30 ms fade. Sweeping PROGRAM under CV is safe: a change
arriving mid-fade is held until the fade lands, so a sweep is a sequence of
clean crossfades rather than a stutter of half-finished ones.

The two PT2399 programs have no delay buffer at all — a chip is 44 kbit — and
what they cost is the bit clock. Measured on an M-series Mac at 48 kHz, Echomatic
at its fastest (30 ms) is about 2.5 % of one core, and falls as TIME lengthens.
Little Angel is the expensive one at about 13 %, because its clock is 5 to 30
times faster and there are two chips; that figure is roughly the same across
DEPTH. A crossfade into or out of it briefly runs both engines.

## Known approximations

* The DSP99 board's own algorithms are not documented anywhere; its sheet names
  categories, not implementations. These are honest implementations of the
  categories, not a clone of that board's DSP.
* **49 AMBIENT FX** puts the pitch shifter after the reverb rather than inside
  its feedback loop, so the shimmer is an octave over the tail rather than a
  tail that climbs. The two-block chain is what the whole bank is built on and
  this is where the shape shows.
* The **Distortion+**'s two frequency-shaping corners are modelled as
  first-order sections rather than as the exact op-amp network, and the boost
  lowpass is floored at 500 Hz so that the maximum-gain setting stays a pedal
  rather than a subwoofer.
* The pitch shifter is the cheap two-tap granular kind, so sustained tone
  warbles. That is the sound the DSP99's "transposition" programs have, and it
  is deliberate.
* The 4011's inputs are modelled as comparators with hysteresis rather than as
  CMOS gate thresholds against a real +12 V rail, so BIAS spans a useful range
  at Eurorack levels instead of the tiny one the actual chip would give.
* **The PT2399 boards' own circuits were not available.** The course folder has
  the panel numbers (12.3 and 12.6) but neither board's schematic. The chip is
  the datasheet's; what surrounds it is assumed, and every assumed value is in
  one place, `miaw_assumed` in `src/Diversified/MiawFx.hpp`: Rack volts to chip
  volts (5 V is the chip's 2.4 V full scale), the two poles on each side of the
  Echomatic's chip (7 kHz, a midpoint of the datasheet application circuit's
  range, not a reading), the Little Angel's 5.5 kHz roll-off and its 5-to-30 ms
  delay range (both kept from before the chip model), and the 3 ms floor on how
  fast the Little Angel's clock may be pushed, because the measured delay law the
  chip model uses stops at 30 ms and the chorus runs below it. With the real
  schematics these become readings.
* **The BitCrusher's op-amps are drawn on a single supply, and cannot be.** U7.1
  inverts a positive current into its input, so its output has to go negative;
  with pin 4 on ground, as drawn, it would sit at 0 V and the board would be
  silent. Both LM358s are modelled as running from ±12 V, swinging to +10.5 V and
  −11.98 V (the datasheet's typical V+ − 1.5 V and V− + 20 mV).
* **The ADC0809's pins are modelled as ideal 0 V and 5 V.** Its data pins are only
  guaranteed to 4.5 V at 360 µA, and a ladder leg draws up to 2.5 mA, so the real
  high level sags by an amount the datasheet does not give. The chip's own error
  (±1 LSB total unadjusted), its input sample-and-hold droop, and its 10 kHz to
  1.28 MHz clock limits are not modelled either: a clock outside that range is
  simply obeyed. ALE and OUTPUT ENABLE are drawn unconnected, and are modelled
  as tied the usual way.
* **The START-to-EOC gap is 8 clocks**, the datasheet's worst case (0 to 8 plus
  2 µs, no typical given), so a cycle is 72 clocks. The LM358s' slew rate is not
  modelled; at the fastest conversion rates it would round the converter's steps.
* **RP2's taper is this module's own.** The board's pot taper isn't on record; RATE
  sweeps R_SET logarithmically from 3k to 1.003M so that it is playable.
  C1 is loaded by an assumed 100k, which puts its corner at 0.16 Hz, so a level
  change takes a second or so to settle at the output, as it would on the board.

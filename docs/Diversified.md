# Diversified — FORM 1099-B

**One hundred and six effects behind one knob.** The ninety-nine programs of the
DSP99 board, then the seven dedicated Modular in a Week effect circuits, in one
stereo multi-effect with three macro knobs whose meaning changes with the
program. 18 HP, stereo in, stereo out, plus an effects loop.

Form 1099-B is the IRS's *Proceeds From Broker and Barter Exchange
Transactions* — the form that reports what a diversified portfolio actually did.
PORTFOLIO holds the program and the macros, ALLOCATION is what CV may take off
each of them, HOLDINGS is the blend and the two gates.

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

### PORTFOLIO

| control | what it does |
|---|---|
| **PROGRAM** | 0–105. The primary control: 0–98 is the DSP99 sheet, 99–105 the dedicated boards. Changing it crossfades over about 30 ms, so a reverb tail survives the change |
| **P1 P2 P3** | The three macros. What each one *is* changes with the program and is named on the read-out, over the knob it belongs to |
| **ACTIVE** | Beside the caption; follows the level of the wet signal |

**The macros work differently on the two banks, on purpose.**

On **0–98** a macro is a *trim about the program's own setting*. Centred, the
knob is exactly what that number is; either end still reaches the parameter's
limit. This is what keeps the seven numbers the sheet calls "plate" seven
different plates rather than one plate with the knobs in seven identical
positions — turn PROGRAM with the macros untouched and the sound really changes.

On **99–105** a macro is the board's own knob, absolute, because on the
Echomatic FEEDBACK means feedback and nothing else.

### ALLOCATION

A trimmer over a jack for each of PROG, P1, P2, P3 and MIX. Each trimmer is an
attenuverter, ±100 %; each jack takes 0–10 V (or ±5 V through a negative
trimmer setting). PROG's CV spans the whole bank, so a ramp sweeps all 106
programs. The trimmers are excluded from randomisation.

### HOLDINGS

| control | what it does |
|---|---|
| **MIX** | Dry to wet, 0–100 %. Default 50 % |
| **AUX** | A gate or a CV, whichever the running program wants — see the table below. Gate threshold 1 V, Schmitt hysteresis down to 0.1 V |
| **TAP** | A clock. Two edges give a period, and every delay-based program uses it in place of its TIME macro. The light beside it is lit while a clock is locked; pull the cable or stop clocking for 3 s and the knob takes over again |

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

## The read-out

The program number in seven-segment, the program's name beside it, and the three
macro names sitting over the knobs they belong to. `CLK` appears at the left when
a TAP clock is locked.

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

**99 Echomatic.** A PT2399 is not a delay line, it is a converter whose clock is
the delay control: longer delay means a slower internal rate, less bandwidth,
coarser quantisation and more of the chip's own noise. That is modelled rather
than faked — a sample-and-hold at the internal rate with an anti-alias pair
tracking it, and a word length that falls from ten bits at 30 ms to seven at
1 s. TIME is 30 ms to 1 s, or the TAP clock. FEEDBACK goes past unity into a
soft ceiling, so it self-oscillates without running away. LEVEL is the echo's
own output level, ahead of MIX.

**100 Little Angel.** The same chip, run short: 5–30 ms modulated, nine bits, a
5.5 kHz roll-off. MODE is one macro carrying both of the board's switches, in
four positions across the knob's travel: chorus/normal, chorus/warble,
vibe/normal, vibe/warble. VIBE kills the dry path so only the pitch modulation
is left; WARBLE adds the slower, irregular drift that makes it sound like a tape
motor rather than a chorus pedal.

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

**104 MW Bitcrusher.** An eight-bit quantiser at a sample rate from 200 Hz to
48 kHz. BITS masks the word from the bottom, which on the board is what leaving
a data jack unpatched does. **Swap MSB and LSB** in the context menu reverses
the whole eight-bit word before the ladder sees it — the extreme of the board's
joke, which is that the patch cable between the converter and the ladder *is*
the effect. **Reconstruction filter** is the board's LM358 second-order lowpass
at 5 kHz; turning it off is the raw staircase.

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
| **Reconstruction filter** | 104 only. The board's 5 kHz output lowpass, in or out of circuit. Default on |
| **Smooth (analogue product)** | 105 only. Analogue multiplication instead of the exclusive-or. Default off |

All three are saved with the patch.

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

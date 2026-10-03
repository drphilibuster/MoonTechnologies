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
* **Spring reverb** (Day 12, *Spring reverb with speaker and piezo*, rev 1.0,
  2020-08-24) — a 2N2222 driver into a small speaker, and a piezo into an NE5532
  gain stage. The drawing has no tank; see program 101.
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
| 101 | SPRING TANK | MiaW spring reverb | DRIVE (RP1) / DWELL (RT60) / TONE (loop damping) |
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
| 101 spring tank | gate: KNOCK, a 1 ms velocity pulse into the driven end of the spring | — |
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

**100 Little Angel.** Rick Holt's PT2399 mini chorus as Jack Orman drew it in his
"New Year's Edition" rev 2 (AMZ-FX, Dec 2015), not the MiaW board: the course
folder had no schematic for this program, and the published one is the better
source. The chip's clock is **not swept**. Pin 6 is held to ground by a J112, the
shortest delay the PT2399 has, and the LFO goes to the chip's **REF** pin (pin 2,
1/2 Vcc) through the Depth pot and 10k. REF is the analog ground of every stage in
the data sheet's block diagram, so signals, which are relative to it, do not care;
the one thing that does is the VCO, whose control current is REF over the pin-6
path (`Pt2399::delaySecondsForVref`). So there is no 5–30 ms range and no floor:
the delay sits near the data sheet's minimum (about 28 ms at the 6 V this board
runs from, 30 ms at 5 V) and REF moves it by about ±0.7 ms at full DEPTH, which at
5 Hz is ±2 % of pitch. DEPTH is a 500k reverse-log pot in series with 10k, so
almost all of its effect is in the top third of the knob.

Around the chip is the drawing: an inverting input stage (−1M/470k, gain −2.13,
within its rails), LPF1 on pins 16/15 (three 10k and 3.3 nF: a first-order
low-pass at 1.6 kHz behind a 0.1 µF), the integrators' 0.1 µF, LPF2 on pins 13/14
(10 nF: 531 Hz), and dry and wet summed through 10k each into 100k — a fixed mix,
there is no mix pot, and the whole board inverts. The LFO is the single-op-amp
triangle: a Schmitt trigger (220k feedback on a 220k/220k bias) driving a 10 µF node
through 4k7 plus SPEED (100k reverse-log), so SPEED runs from about 0.4 Hz to 15 Hz;
the Depth path loads the ramp node and is solved with it. Because REF is also the
wet node's DC level, the LFO leaks to the output through the 0.1 µF as a slow
swell of a couple of hundred millivolts at full DEPTH; that is the circuit, not a
bug. MODE is one macro carrying the board's two switches in four positions:
chorus/normal, chorus/warble, vibe/normal, vibe/warble. VIBE opens the dry
branch's switch (the wet then reaches the output node unloaded, so it is 5.6 dB
louder); WARBLE takes the LFO through a 10 µF to the J112's gate, so the pin-6
resistance moves with it. The board is mono; the second chip, with the LFO
inverted, is this module's stereo addition.

**101 Spring tank.** Kristian's board from the drawing, solved, and a tank built
from the physics of a real spring.

*The board.* A 2N2222 biased by its own collector feedback (R1 10k from
collector to base, R2 1k base to ground) drives a small speaker from VCC. The
quiescent point is solved, not set: about 88 mA through the voice coil, the
collector 0.65 V under the rail. The speaker is a Visaton K 50 SQ — 7.3 Ω,
480 Hz resonance, Qes 7.32, Qms 6.42, Bl 1.12 T·m — as a voice coil in series
with its motional impedance (the cone's mechanical resonance, a parallel RLC),
solved together with the transistor at the sample rate (the nodal solver, `Mna`).
The cone's velocity (motional voltage ÷ Bl) is what drives the spring's end.
DRIVE is RP1, the 10k input pot, linear. The stage is hot: at RP1 halfway the
speaker current reaches zero, and the stage clips, from a 2.4 V input (a Rack
±5 V signal clips it; turn DRIVE down). At 20 % it is 4.0 V. The input is
1:1 volts, a Rack volt being a volt at the jack (ASSUMED).

The pickup is the piezo into C3 and R3, then U1.1, an NE5532 whose gain is
1 + RP2/R4 = 1 to 101× (RP2 100k, R4 1k), then C4 and U1.2, a follower into R5.
The NE5532 is `OpAmp.hpp` with its datasheet (12 MHz, 5 V/µs, 100 V/mV), on
±12 V rails, so it clips at 10.4 V into R5. RP2 is in the context menu, not on
the panel.

The tank. Parker and Bilbao's model of the helical spring (a curved rod; their
equation 1) gives a dispersion relation; solving it for the audio-band mode
gives two branches. **Below a transition frequency Fc** (about 4.6–5.0 kHz for
the default springs) the group velocity falls from c·r/2R to zero, so the delay
of a wave through the spring *rises* with frequency, from TD/2 (about 20–26 ms)
to well over 100 ms near Fc: an impulse comes back as a **rising chirp**, low
frequencies first, which piles up at Fc. **Above** that, a second, faster branch
returns a falling chirp over the whole band at much lower level. Each branch is
a waveguide loop: a pure delay and a cascade of 140 (LF) or 36 (HF) first-order
allpass sections whose group delay is fitted to the delay the relation predicts
(the fit is within 2–4 % on the LF branch and within 13 % on the HF one; the
test measures the delay of the actual DSP chain against the relation). The echo
spacing TD = 4LR/(rc) comes from the spring itself: L the wire length (4.0–5.1 m
for the Leem springs), R the helix radius, r the wire radius, c the speed of
sound in steel. The default tank is the Leem Pro KA-1210's three springs in
parallel (echoes 40.5, 39.2 and 51.3 ms apart); the **Tank** menu item swaps it
for the Olson X-82's two (29.8 and 34.2 ms). The springs' dimensions are
Parker and Bilbao's own measurements of those two tanks, and sit in one place,
`leem1210()` and `olsonX82()` in `src/SpringTank.hpp`, with the number of
allpass sections and the loop rates beside them; change those and the design
(`designSpring`) is redone from the dispersion relation.

The piezo senses the displacement of the spring's end (the waveguide carries
velocity, so it is integrated, with a 5 Hz leak). Piezo capacitance changes the
first-stage highpass: Cp (20 nF, the Murata 7BB-27-4) is in series with C3, so
the corner is 9.5 kHz, not the 1.6 kHz of R3·C3 alone; the integration offsets it,
so the net response is not thin.

DWELL is the tank's reverberation time (0.4 s to 6 s, set per loop by the
round-trip time) and TONE the loss in the loop (a damping pole from 1 kHz to
6 kHz); neither is on the drawing. AUX (a gate) is a knock on the box: a 1 ms
half-sine velocity pulse into the driven end. The output is mono — there is one
piezo — and wet only.

*What is drawn differently from how it can be built.* U1.2's (+) input, behind
C4, has no DC path to ground in the drawing (R5 hangs on the output). A real
NE5532 would integrate its bias current into the rail within seconds. We add
100k to ground (ASSUMED). *What is assumed* is listed at the head of
`src/Diversified/SpringBoard.hpp`: the +12 V rail and ±12 V op-amp supply (ideal,
so C2's 1 µF does nothing), the speaker's 0.1 mH voice-coil inductance (the
datasheet has none), the transistor's SPICE set without its leakage, high-current
roll-off and capacitances, the piezo-per-metre sensitivity (set so that the
default patch is about unity: 76× on 1 V rms noise gives 0.65 V rms), the
sensing law, the loss (an RT60 and a lowpass, as Parker and Bilbao themselves
use for lack of a physical loss law), the HF series' level (0.3 of the LF), and
the NE5532's output swing, which the current datasheet no longer gives. The
tank is not tension-dependent: the model has no tension term.

Cost: about 1.3 µs a sample (6 % of one core at 48 kHz, 11 % at 96 kHz, scalar),
of which the transistor is 0.7 µs and the tank 0.57 µs. The tank is designed once
per process on first use (about 0.3 s).

**102 MXR Distortion+.** One op-amp in a non-inverting stage. The gain leg is a
1 MΩ pot to 4.7 kΩ in series with 47 nF, so the boost is 1 + Rf/4.7k above about
720 Hz and unity below it; the 1 nF across the pot rolls the top of that band
off again, which is why a Distortion+ is thick rather than fizzy. The op-amp is
the datasheet's 741 (`src/OpAmp.hpp`, `docs/OpAmps.md`) in that network, solved as
drawn rather than as two first-order sections: 1 MHz of gain-bandwidth, which makes
a gain of 200 a 5 kHz amplifier and costs the boost at 3 kHz 3 % at full gain; a slew rate of 0.5 V/µs, which turns the clipper's edges into
ramps -- the part of the pedal's sound that a tanh does not have; and a swing of
±3.70 V (the ±4.5 V rails less the 741's 0.73 V, into the clamp's 10 kΩ). A germanium
pair then clamps the output through 10 kΩ at roughly a 0.35 V knee. The op-amp runs
sixteen times oversampled (1.3 µs, what its loop needs) and the clipper two times,
where the harmonics that would alias are made. TONE is an addition — the pedal has
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
| **Pickup gain (RP2)** | 101 only. U1.1's gain pot: 1×, 11×, 26×, 51×, 76× or 101× (1 + RP2/R4). Default 76× |
| **Tank** | 101 only. Leem KA-1210 (three springs) or Olson X-82 (two). Default Leem |

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
Little Angel is about 5 % of a core: two chips at the chip's fastest clock, about
1.5 Mbit/s each. A crossfade into or out of it briefly runs both engines.

## Known approximations

* The DSP99 board's own algorithms are not documented anywhere; its sheet names
  categories, not implementations. These are honest implementations of the
  categories, not a clone of that board's DSP.
* **49 AMBIENT FX** puts the pitch shifter after the reverb rather than inside
  its feedback loop, so the shimmer is an octave over the tail rather than a
  tail that climbs. The two-block chain is what the whole bank is built on and
  this is where the shape shows.
* The **Distortion+**'s network is now the exact op-amp network (the earlier
  first-order sections, and the 500 Hz floor on the boost lowpass that kept the
  maximum-gain setting from being a subwoofer, are gone: the real 1 nF across a 1 MΩ
  pot is a 159 Hz pole, and the real top-of-pot gain is a few tens at 1 kHz, not 200).
  The 741's swing on ±4.5 V is the datasheet's ±14 V at ±15 V with the same 0.73 V
  headroom carried down to ±4.5 V, below the ±5 V the part is specified at: an
  extrapolation. The germanium clamp after it is still an ideal tanh knee.
* The pitch shifter is the cheap two-tap granular kind, so sustained tone
  warbles. That is the sound the DSP99's "transposition" programs have, and it
  is deliberate.
* The 4011's inputs are modelled as comparators with hysteresis rather than as
  CMOS gate thresholds against a real +12 V rail, so BIAS spans a useful range
  at Eurorack levels instead of the tiny one the actual chip would give.
* **The Echomatic's own circuit was not available.** The course folder has the
  panel number (12.3) but no schematic. The chip is the data sheet's; what
  surrounds it is assumed, and every assumed value is in one place,
  `miaw_assumed` in `src/Diversified/MiawFx.hpp`: Rack volts to chip volts (5 V
  is the chip's 2.4 V full scale) and the two poles on each side of the chip
  (7 kHz, a midpoint of the data sheet application circuit's range, not a
  reading). With the real schematic these become readings.
* **The Little Angel is Orman's NYE rev 2 drawing; what is still assumed** (all in
  `src/Diversified/LittleAngel.hpp`, under `la`, or `Pt2399::assumed`):
  * *How REF moves the clock.* The data sheet and the Valve Wizard's notes say
    pin 6 is a current source held at REF, so the VCO current is REF/R; the delay
    law (Electric Druid, `11.46 ms/kΩ + 29.70 ms`) fixes the ramp's share of the
    delay only for R large. How much of the 29.7 ms intercept is the chip's own
    series resistance in the pin-6 path is **not on record**; 1 kΩ (the Valve
    Wizard's speculative "1k?") is the smallest value for which the documented
    pin-2 hack (REF 2.5 V to 2 V) moves the delay by milliseconds at all. With 0 Ω
    the model moves it by 0.14 ms and the chorus is inaudible, so the depth of the
    effect scales with this one number. Whether the fixed part of the delay also
    shortens with the supply, beyond the ramp's share, is not modelled; Orman says
    the clock runs faster at 6 V, and this model gives about 2 ms of it.
  * *The J112* as an ohmic 50 Ω (the data sheet's worst case) and, for WARBLE, a
    cutoff of −3 V (its range is −1 to −5 V). **The NYE drawing has no Space/Warbler
    switch**; the v4 board's wires the LFO, through a 10 µF, to the base of its
    anti-lock transistor, and WARBLE here assumes the same wiring to the J112's
    gate. The v4's pin 2 resistor is 33k, not 10k, and its transistor is not
    modelled. Unit-to-unit spread of the J112 alone makes WARBLE anything from
    nothing to a dropout.
  * The signal op-amp and the LFO op-amp swing ±3 V about 4.5 V (the layouts use an
    NE5532; the drawing names none), the chip's internal REF divider is 5.6k/5.6k
    (the Valve Wizard measured the resistors, the data sheet says 4.7k; the
    integrators here keep 4.7k, which is what the ADM's step sizes were fitted
    with), the Speed and Depth taper is an audio taper mirrored (15 % at the middle),
    and 5 Rack volts is 1 V at the board's input.
  * Not modelled: the power-on transient (the J112's 3.9 nF/1M network, the
    PT2399's latch-up below 1 kΩ at start), the few milliseconds of each LFO edge
    that the 220k/10 nF around the op-amp's inverting input make, the 78L06's
    ripple, the chip's supply noise, and the part-to-part spread of the PT2399
    itself (25 ms to 31 ms for the same pin-6 resistance).
* **The BitCrusher's op-amps are drawn on a single supply, and cannot be.** U7.1
  inverts a positive current into its input, so its output has to go negative;
  with pin 4 on ground, as drawn, it would sit at 0 V and the board would be
  silent. Both LM358s are modelled as running from ±12 V. Their swing is the
  datasheet's and depends on the load (about +10.4 V unloaded; +8.7 V from U7.1 into
  its 1 kΩ and the SW1 filter; down to −10.9 V, 0.62 V off V− plus the PNP follower's
  47 Ω × I), and U7.2's class-B output stage has its crossover distortion: the
  Darlington sources, the PNP follower sinks, and the node that drives them slews
  3 Vbe between the two (0.38 % THD for a 1 V sine into a ground-referred 10 kΩ; none
  with a pull-down to V−).
* **The ADC0809's pins are modelled as ideal 0 V and 5 V.** Its data pins are only
  guaranteed to 4.5 V at 360 µA, and a ladder leg draws up to 2.5 mA, so the real
  high level sags by an amount the datasheet does not give. The chip's own error
  (±1 LSB total unadjusted), its input sample-and-hold droop, and its 10 kHz to
  1.28 MHz clock limits are not modelled either: a clock outside that range is
  simply obeyed. ALE and OUTPUT ENABLE are drawn unconnected, and are modelled
  as tied the usual way.
* **The START-to-EOC gap is 8 clocks**, the datasheet's worst case (0 to 8 plus
  2 µs, no typical given), so a cycle is 72 clocks. The LM358s' slew rate (0.3 V/µs)
  now rounds the converter's steps: U7.1, SW1 and U7.2 run eight times per audio
  sample and the output is the mean of the eight, so a code change that lands
  anywhere in a sample is slewed -- 17 µs for 5 V. It matters at the middle of RATE,
  where a conversion arrives every 70–200 µs with volts between codes (the output
  changes by 5 % rms at about 14 k conversions/s and 25 % at 4 k, against a board
  with ideal op-amps), and not at the top, where the codes differ by an LSB.
  U7.1's load (its 1 kΩ and SW1 in parallel) and U7.2's ((RP3 + 2.2 k) with C1 into
  100 k) are assumed from the parts the schematic draws.
* **RP2's taper is this module's own.** The board's pot taper isn't on record; RATE
  sweeps R_SET logarithmically from 3k to 1.003M so that it is playable.
  C1 is loaded by an assumed 100k, which puts its corner at 0.16 Hz, so a level
  change takes a second or so to settle at the output, as it would on the board.

# Racketeer — noise voice · FORM 211

**Electric Intonarumori.** A PT2399 delay chip run as a self-sustaining noise
voice: feedback echo past unity, a low-pass in or after the loop, a chopper, and
a delay time that bends under CV through an optocoupler's lag. 14 HP, mono.

Form 211 is the IRS's *Application for Award for Original Information* — the
whistleblower form. A racketeer is what it is filed against.

## What it is based on

Racketeer models the **PB701 Electric Intonarumori** by Wolfgang Spahn
(paperpcb.dernulleffekt.de, 2017–20, CC BY-NC-SA 4.0), which is itself Urs
Gaudenz's **Chaos Looper** (Gaudi Labs) with voltage-controlled delay time and
extra functions added. Both are named for Luigi Russolo's *intonarumori*, the
noise-intoning machines of his 1913 *The Art of Noises*.

The hardware is a PT2399 echo chip with, in Spahn's words, everything that
would help get rid of the chip's own noise left out. The chip's quantisation
noise, its falling sample rate at long delays and its op-amps clipping in the
feedback path *are* the instrument. The board has three pots (echo, delay,
low-pass), a 0–5 V CV input for delay time through a TLP521 optocoupler, an
audio input, three mini switches, three pushbuttons (noise on/off, boost, mute)
and one LED.

### How the chip is modelled

The PT2399 is not a buffer of samples. The datasheet's block diagram is an input
low-pass, a comparator and a **1-bit adaptive delta modulator** writing **44 kbit
of RAM**, and a demodulator reading it back, all on a VCO whose rate is set by
the resistance on pin 6. Racketeer runs that, bit by bit, with the same chip
model as [Amortization](Amortization.md) (`src/Pt2399.hpp`):

* **TIME is the clock.** Delay = 44 kbit ÷ bit rate: 1.47 Mbit/s at the 30 ms
  minimum, 129 kbit/s at 340 ms (the top of the datasheet), 37 kbit/s at the 1.2 s
  the pot can be pushed to. There is no read pointer. Moving TIME changes the
  rate the stored bits are replayed at, so a sweep bends pitch the way the chip
  does, and a halved delay plays what is already in the RAM an octave up.
* **The converter is 1 bit.** What it adds is the modulator's: slope overload on
  loud fast signals, granular hiss on quiet ones, and a bandwidth that falls with
  the clock. A fixed RC pair (read from the PB701 schematic as 2k and 4n7, twice)
  sits ahead of it; nothing in the filtering tracks the clock.
* **ECHO past unity clips, it does not saturate softly.** The chip's op-amps run
  from 5 V, so the loop is bounded by a hard clip at ±2.4 V at the modulator's
  input. The datasheet's −90 dBV noise floor at the comparator is what a loop
  past unity grows from when nothing is plugged in; **Chip noise** scales it.
* The loop is DC-blocked, the output is clamped to ±12 V and the loop resets
  itself if it ever goes non-finite (it should not).

An earlier version modelled the chip as a 1365-sample ring with a sample-and-hold,
a word length that fell with delay time and a reconstruction filter that tracked
the clock. That is not the architecture in the datasheet, and it was replaced.

The **LED** on the original is the loop light beside the RACKET caption; it
follows the loop's envelope.

## Panel

### RACKET — the loop

| control | what it does |
|---|---|
| **TIME** | Delay time, exponential over the range RANGE selects: 30–340 ms (SHORT) or 30 ms–1.2 s (LONG). Passes through LAG. The read-out shows the resulting delay, and that number is TIME too: hold it and drag. |
| **ECHO** (primary) | Feedback, 0–150 %. Past 100 % the loop self-oscillates; the `tanh` keeps it bounded. This is the knob the module is about. |
| **CUTOFF** | Low-pass cutoff, 40 Hz–18 kHz. In the loop or after it, per FILTER. |
| **LAG** (read-out) | The optocoupler's slew on TIME and its CV. 0 is instant; up to a 2 s time constant. Like the TLP521, it moves faster toward *shorter* delays (the LED lights faster than it dims), so sweeps are asymmetric. |
| **DRIVE** (read-out) | Input gain, 0–24 dB, ahead of the loop. |
| **SEED** (read-out) | White noise injected into the loop, 0–100 % (square law: nothing at zero, an audible floor at the top). This is what a self-oscillating loop grows from when nothing is plugged in. |
| **RATE** (read-out) | Chopper rate, 0.1–60 Hz. |
| **RES** (read-out) | Resonance of the low-pass, 0–100 %, clamped just below self-oscillation. The original has none. |
| **THRESH** (read-out) | The level, 0–10 V on the ENV output, at which the GATE output fires. Releases at 80 % of it. |

### ENFORCEMENT — buttons and switches, on the read-out

The middle line of the read-out. The switches are clicks (Ctrl-click steps back);
the buttons are held while the mouse is down, and their keys light while they or
their gate jacks are pressing.

| control | what it does |
|---|---|
| **NOISE** (momentary) | Injects loud white noise into the loop while held — the original's "noise on" button. The context menu can make it kill the feedback instead. |
| **BOOST** (momentary) | Doubles the loop gain and the output level (+6 dB) while held. |
| **MUTE** (momentary) | Silences OUT and DIRTY while held, with a 2 ms declick. The loop keeps running underneath. |
| **POL** | Feedback polarity. Inverted feedback favours odd harmonics and a different set of pitches when the loop sings. |
| **FILTER** | Low-pass *in the loop* (each pass through the memory is filtered again — the loop darkens as it recirculates) or *after* it (only the output is filtered; the loop stays bright). |
| **RANGE** | TIME's range: SHORT (datasheet, 30–340 ms) or LONG (30 ms–1.2 s, as far as the PB701's pot pushes the chip). |
| **CHOP** | Chopper on/off. A square LFO at RATE presses MUTE inside the loop for you — half on, half off — with a 0.3 ms edge. Its ON ticks with the LFO. |

The three mini switches on the PB701 are unlabelled bus/function selectors set at
build time; Racketeer gives them the three functions that matter in a patch.

### SKIM — what the CV inputs may take

A trimpot directly over its jack, one label serving both. Each trim is an
attenuverter (−100 % to +100 %).

| jack | law |
|---|---|
| **TIME** | ±10 V spans the full TIME range at 100 %. Goes through LAG. |
| **ECHO** | ±10 V spans 0–150 % at 100 %. |
| **CUTOFF** | 1 V/oct, scaled by the trim. |
| **RATE** | 1 V/oct on the chopper, scaled by the trim. |
| **RES** | ±10 V spans 0–100 % at 100 %. Swept against a high ECHO this is what turns the delay into a voice. |
| **LAG** | ±10 V spans 0–100 % at 100 %. Modulating the optocoupler's slew smears the pitch of whatever the loop is whistling. |
| **NOISE / BOOST / MUTE** | Gates that press the button of the same name, in the footer band with IN. High above 1 V, low below 0.1 V (Schmitt). Button and gate are ORed. |

### Footer and OUT rail

The band holds what comes in; everything that leaves stands in the OUT rail down
the right-hand edge.

| jack | signal |
|---|---|
| **IN** | Audio in, ±5 V nominal (a polyphonic cable is summed). Through DRIVE, into the loop. Nothing need be plugged in. |
| **NOISE / BOOST / MUTE** | The button gates (see SKIM). |
| **GATE** | See below. |
| **ENV** | The loop's envelope, 0–10 V. 5 ms attack, 100 ms release. |
| **DIRTY** | The loop's pre-filter tap: the memory's output after reconstruction and DC blocking, before the low-pass. ±5 V nominal. |
| **OUT** | The filtered output, ±5 V nominal, clamped to ±12 V. |

**GATE** is 0/10 V, high while ENV is above THRESH. With a self-oscillating loop this is a
chaotic gate.

Audio is ±5 V; ECHO at 150 % with BOOST will run into the clamp. Bypass routes
IN to OUT.

### Read-out

Every value on it is a control. Values (the delay, and LAG to THRESH) are held
and dragged up or down -- Ctrl fine, Shift coarse; choices (RANGE, CHOP, POL,
FILTER) are clicked; NOISE, BOOST and MUTE are held. Right-click any of them for
the usual parameter menu: typed entry, MIDI-Map, reset.

Top line: the delay actually running (after LAG) -- the TIME control -- then the
chip's bit clock at that delay (what TIME is actually setting; not a control),
and RANGE, SHORT or LONG. Middle line: ENFORCEMENT, above. Bottom line: LAG,
DRIVE, SEED, RATE, RES and THRESH, the set-and-leave controls of RACKET.

## Context menu

| option | choices |
|---|---|
| **Oversampling** | Off, 2x. The loop, its S&H and its saturation run at twice the engine rate; the input is upsampled and both outputs decimated. Costs about double. Switching it does not allocate. |
| **Chip noise** | None, Subtle, Stock (default), Filthy, Ruined. The comparator's noise floor: Stock is the datasheet's −90 dBV (40 µV); each step is ×4.5 either way, Ruined is 800 µV. None leaves only the modulator's own granular noise and whatever SEED adds. |
| **NOISE button** | *Injects noise* (default) or *Kills the loop* — opens the feedback path while held, the other reading of an "on/off" button. |

All three are saved with the patch.

## What was approximated or left out

* The optocoupler is a one-pole slew with asymmetric rates, not a TLP521 model;
  the original's 0–5 V CV input is here a bipolar ±10 V input with an
  attenuverter.
* The modulator's step sizes and syllabic filter are not in the datasheet; they
  are the ones calibrated against its THD, swing and gain figures (see
  Amortization's tests). Gain lost per pass through the chip is the biggest
  unknown: it sets how far below 100 % ECHO the loop stops ringing and where it
  starts to sing.
* The input stage's filter values are read off a hand-drawn schematic and are
  not unambiguous.
* Resonance, the chopper, LAG as a control, the DIRTY tap, ENV and GATE outputs
  and the three CV inputs are additions the original does not have. Their
  defaults (RES 0, chopper off, LAG 30 %) leave the original character in place.
* Mono. The original is mono.

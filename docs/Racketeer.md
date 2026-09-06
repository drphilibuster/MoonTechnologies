# Racketeer — FORM 211

**Electric Intonarumori.** A PT2399 delay chip run as a self-sustaining noise
voice: feedback echo past unity, a low-pass in or after the loop, a chopper, and
a delay time that bends under CV through an optocoupler's lag. 16 HP, mono.

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

A PT2399 is a fixed-length memory clocked at a variable rate, so the delay time
sets the chip's sample rate: about 45 kHz at the 30 ms minimum, 4 kHz at 340 ms
(the top of the datasheet), just over 1 kHz at the 1.2 s the pot can be pushed
to. Racketeer runs a sample-and-hold at that internal rate ahead of the memory
and a two-pole reconstruction low-pass that tracks it, so a long delay is dark
and stair-stepped the way the real thing is. The converters get noisier as the
chip is pushed: word length falls and a noise floor rises with delay time,
scaled by the **Chip noise** menu setting. The memory is read with linear
interpolation, so a moving TIME bends pitch. The loop is DC-blocked, and a
`tanh` at the memory input stands in for the op-amps, so ECHO past unity
saturates rather than blowing up. The output is clamped to ±12 V and the loop
resets itself if it ever goes non-finite (it should not).

The **LED** on the original is the loop light beside the RACKET caption; it
follows the loop's envelope.

## Panel

### RACKET — the loop

| control | what it does |
|---|---|
| **TIME** | Delay time, exponential over the range the RANGE switch selects: 30–340 ms (SHORT) or 30 ms–1.2 s (LONG). Passes through LAG. The read-out shows the resulting delay. |
| **ECHO** (primary) | Feedback, 0–150 %. Past 100 % the loop self-oscillates; the `tanh` keeps it bounded. This is the knob the module is about. |
| **CUTOFF** | Low-pass cutoff, 40 Hz–18 kHz. In the loop or after it, per the FILTER switch. |
| **LAG** | The optocoupler's slew on TIME and its CV. 0 is instant; up to a 2 s time constant. Like the TLP521, it moves faster toward *shorter* delays (the LED lights faster than it dims), so sweeps are asymmetric. |
| **DRIVE** | Input gain, 0–24 dB, ahead of the loop. |
| **SEED** | White noise injected into the loop, 0–100 % (square law: nothing at zero, an audible floor at the top). This is what a self-oscillating loop grows from when nothing is plugged in. |
| **RATE** | Chopper rate, 0.1–60 Hz. |
| **RES** (trim) | Resonance of the low-pass, 0–100 %, clamped just below self-oscillation. The original has none. |
| **THRESH** (trim) | The level, 0–10 V on the ENV output, at which the GATE output fires. Releases at 80 % of it. |

### ENFORCEMENT — buttons and switches

| control | what it does |
|---|---|
| **NOISE** (momentary) | Injects loud white noise into the loop while held — the original's "noise on" button. The context menu can make it kill the feedback instead. |
| **BOOST** (momentary) | Doubles the loop gain and the output level (+6 dB) while held. |
| **MUTE** (momentary) | Silences OUT and DIRTY while held, with a 2 ms declick. The loop keeps running underneath. |
| **POL** | Feedback polarity. Inverted feedback favours odd harmonics and a different set of pitches when the loop sings. |
| **FILTER** | Low-pass *in the loop* (each pass through the memory is filtered again — the loop darkens as it recirculates) or *after* it (only the output is filtered; the loop stays bright). |
| **RANGE** | TIME's range: SHORT (datasheet, 30–340 ms) or LONG (30 ms–1.2 s, as far as the PB701's pot pushes the chip). |
| **CHOP** | Chopper on/off. A square LFO at RATE presses MUTE inside the loop for you — half on, half off — with a 0.3 ms edge. Its light ticks with the LFO. |

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
| **NOISE / BOOST / MUTE** | Gates that press the button of the same name. High above 1 V, low below 0.1 V (Schmitt). Button and gate are ORed. |

### Footer

| jack | signal |
|---|---|
| **IN** | Audio in, ±5 V nominal (a polyphonic cable is summed). Through DRIVE, into the loop. Nothing need be plugged in. |
| **ENV** | The loop's envelope, 0–10 V. 5 ms attack, 100 ms release. |
| **GATE** | 0/10 V, high while ENV is above THRESH. With a self-oscillating loop this is a chaotic gate. |
| **DIRTY** | The loop's pre-filter tap: the memory's output after reconstruction and DC blocking, before the low-pass. ±5 V nominal. |
| **OUT** | The filtered output, ±5 V nominal, clamped to ±12 V. |

Audio is ±5 V; ECHO at 150 % with BOOST will run into the clamp. Bypass routes
IN to OUT.

### Read-out

Top line: the delay actually running (after LAG), and SHORT or LONG. Bottom line:
the chip's internal sample rate at that delay, and the word length it has left.

## Context menu

| option | choices |
|---|---|
| **Oversampling** | Off, 2x. The loop, its S&H and its saturation run at twice the engine rate; the input is upsampled and both outputs decimated. Costs about double. Switching it does not allocate. |
| **Chip noise** | None, Subtle, Stock (default), Filthy, Ruined. How bad the converters are: at Stock the loop keeps about 14.5 bits at the shortest delay and 10.5 at the longest; Ruined halves the damage again; None is a clean, if still sample-rate-starved, delay. |
| **NOISE button** | *Injects noise* (default) or *Kills the loop* — opens the feedback path while held, the other reading of an "on/off" button. |

All three are saved with the patch.

## What was approximated or left out

* The optocoupler is a one-pole slew with asymmetric rates, not a TLP521 model;
  the original's 0–5 V CV input is here a bipolar ±10 V input with an
  attenuverter.
* The PT2399's internal compander and clock modulator are not modelled beyond
  their audible consequences (word length, noise floor, sample rate).
* Resonance, the chopper, LAG as a control, the DIRTY tap, ENV and GATE outputs
  and the three CV inputs are additions the original does not have. Their
  defaults (RES 0, chopper off, LAG 30 %) leave the original character in place.
* Mono. The original is mono.

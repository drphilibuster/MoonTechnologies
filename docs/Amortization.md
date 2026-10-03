# Amortization — triple-PT2399 reverb

The Pittsburgh Modular Verbtronic, as a circuit. Filed as PUB 535.

A debt paid off in reflections: FEEDBACK is how long it runs, TILT leans the tail
dark or bright, MODE swaps which of two clock settings the three delay chips run
at, and the two outputs are the blend and the wet-only signal, as on the
original. Part of the [Moon Technologies](../README.md) plugin. 9 HP.

## What it is based on

The [Verbtronic](https://pittsburghmodular.com/verbtronic) by Pittsburgh
Modular — a 10 HP "big digital electronic reverb", discontinued, whose
**schematic was released into the public domain** (*PT2399 Verbtronic Reverb,
created by Micheal Johnsen for Pittsburgh Modular*, the image at the foot of the
product page).

It is not a reverb algorithm. It is **three PT2399 echo chips** wired into a
recirculating network, with analog stages around them. This module is that
schematic, solved block by block, with the datasheet's chip behind each PT2399.
An earlier version of this module was a Dattorro plate and a feedback-delay
network built to the manual's *description*; the circuit is nothing like that,
and this replaces it.

## The circuit

```
IN ─┬─ attenuator ─ tone ─ 2u2 ─┬─ chip 1 ─┐
    │   (÷45, inv)  (tilt)       ├─ chip 2 ─┤→ SUM ─┬→ back into chips 1, 2, 3
    │                            │          │ (chip 1's LPF2 as a summer)
    │                            └─ chip 3 ←┘ ──────┘
    │                                  │
    │                       chip 3's output filter ─ 10u ─ make-up ×10 ─ WET
    │                                                                    │
    │       ┌─ feedback pot ─ 10k stage + zener limiter ─ 100k ─ atten ◄─┤
    └─ DRY ─┴──────────────── SSM2164 wet/dry mix ◄───────────────────────┘
```

* **Input.** An inverting attenuator (2k2 against 100k, ÷45) takes the signal
  down to the 100 mV the PT2399 expects, and sums the feedback with it. Then the
  tone stage and a 2u2 coupling cap.
* **Tone.** A 100k pot between two 68k resistors with two 22k + 3.3 nF legs: a
  first-order shelf, **+7.5 dB bass / −6.7 dB treble to the reverse**, pivoting
  near 1 kHz. Centre is flat. Derived from the nodal equations; the module's
  digital version is within 0.04 dB of them.
* **The three PT2399s.** Each has its own input stage — a multiple-feedback
  low-pass, 12.5 kHz, Q 0.72–0.86 — then the chip. Chips 1 and 2 hear the input;
  **chip 3 hears only their sum**. All three feed their own output back to their
  own input (12k1), and all three hear the sum (12k1, 12k1, 10k): a mesh. The sum
  is chip 1's second op-amp wired as a summer (6k8 against 3×10k). Only chip 3's
  output leaves the loop: through its own 30 kHz multiple-feedback filter, a 10 µF
  coupling, and a ×10 make-up amplifier. That is the **wet bus**.
* **Feedback.** The FEEDBACK pot sits on the wet bus; its wiper goes through a
  10k inverting stage with a **5V1 zener in a diode bridge** across it — the
  limiter — and a 1k resistor, back to the attenuator. Because the pot is loaded
  by that 10k, the feedback is useful only in the top half of its travel: at
  mid-position the loop gain is about a seventh of what the label suggests.
* **Mix.** The dry signal and the wet bus are subtracted, run through one
  SSM2164 VCA and added back to the dry: out = (1 − g)·dry + g·wet. A second VCA
  in the control op-amp's feedback makes g **linear** in its control current:
  the MIX pot (12 V across 100k, loaded by 100k into the summing node), plus the
  CV (through a 100k/100k attenuverter and 30k1: **3.6 V sweeps the whole
  range**), minus a 10 MΩ bias to −12 V that keeps g at zero at the bottom of the
  pot and holds it **1 % shy of unity** at the top. A diode stops g exceeding 1.
  VERB OUT is the wet bus before all of this and does not move with MIX.
* **MODE.** Three DG202 analog switches each put a second resistor across a
  chip's clock resistor on pin 6. Closed — **Verb**, "shorter" — every clock
  speeds up and every delay shortens; open — **Tronic**. The delays, from the
  PT2399's measured delay-against-resistance relation:

  | | chip 1 | chip 2 | chip 3 |
  |---|---|---|---|
  | **Verb** (switch closed) | 60.2 ms | 48.5 ms | 39.6 ms |
  | **Tronic** (open) | 123.7 ms | 93.9 ms | 73.6 ms |

  (Clocks 5.5 to 17.3 MHz, inside the VCO's measured 2–22 MHz.) At no feedback the
  first sound out is the shorter of chips 1 and 2 plus chip 3: **88.1 ms** in Verb,
  **167.5 ms** in Tronic. Those are not multiples of one another.

### The chip

A PT2399 is not a buffer of samples. From its datasheet block diagram: LPF1, a
comparator, a 1-bit adaptive delta modulator whose integrator is an op-amp with
4.7k inside and 0.047 µF across pins 9–10, **44 kbit of RAM**, and a demodulator
with the same integrator across pins 11–12. The clock is a VCO set by the
resistance on pin 6. The RAM is a fixed number of bits, so the **delay is the
RAM's length over the bit rate, and the bit rate is the clock**. This module does
exactly that, a bit per clock — 0.13 to 1.4 million per second per chip.

That is what a mode change *is*: the DG202s change the clock, and whatever is in
the RAM comes out at the new rate, shifted in pitch by the ratio (2.05× for chip
1). Nothing crossfades. A tone stored at the Tronic clock and played at the Verb
clock comes out a little over an octave higher — and the loop, if it is ringing,
goes on ringing in a different shape.

## Controls

| Control | |
|---|---|
| **FEEDBACK** | 0–100 %, a linear pot. Only the upper part of the travel feeds back much (see above). As modelled the mesh is just under unity even at zero — tails of several seconds — and the loop starts to sing at about 70 % in either mode; past that the limiter, the chips and the make-up stage clip it into a sustained wail. **How long the tail runs at zero, and where it starts to sing, depend on the chip's loss per pass, which the datasheet fixes only to about ±0.5 dB** — see the table below, and measure those two on a unit. |
| **TILT** | The tone pot. −100 % to +100 %; flat at centre. See the note on direction below. |
| **MODE** | **Verb** (switch closed) or **Tronic**. The original's toggle is the TRONIC/VERB word at the top right of the read-out: click it to flip. The word shows what is actually running once the gate has had its say; a small clay GATE under it means the gate is holding Verb while the toggle is at Tronic. |
| **MIX** | The OUTPUT MIX pot. 0 is dry, 100 % is 99 % wet. |
| **MIX CV** (trimmer) | The attenuverter on the MIX CV jack. Centre: no effect. Clockwise passes the CV, anticlockwise inverts it. |
| **MODE GATE** | While high, Tronic is flipped to Verb. It only ever pulls the mode *down* to Verb, so with the switch at Verb it does nothing. |
| **LIMIT** light | The feedback zener conducting, or any stage of the loop clipping. |

| Jack | |
|---|---|
| **IN** | Audio, ±5 V nominal; a polyphonic cable is summed. |
| **MIX** | The blend. |
| **VERB** | 100 % wet. Independent of MIX. |
| **MIX CV**, **MODE GATE** | See above. Mix CV reaches the control at 3.6 V for the whole range. |

The read-out shows the first chip's delay and clock for the mode that is live,
and whether the limiter is working. Its bottom line is FEEDBACK, TILT and MIX:
hold any of them and drag up or down to set it (Ctrl fine, Shift coarse) -- the
same params as the knobs, which stay. Right-click a value or the mode word for
the usual parameter menu: typed entry, MIDI-Map, reset. Outputs are ±10.5 V at most — the op-amps clip
inside their rails. Bypass routes IN to MIX.

Mono, as the original is. Everything the previous version added around it —
stereo, SIZE, PREDELAY, FREEZE, MOD, CV over FEEDBACK/TILT/SIZE — has no
counterpart in the circuit and has been removed.

## Context menu

* **TILT, clockwise is** — *Dark* (default) or *Bright*. See below.

## What is the schematic, and what is assumed

Everything in `src/Amortization/Verbtronic.hpp` is a value off the schematic, a
figure from a datasheet or a measurement of the chip, except what is collected in
the `assumed` namespace there. Measuring each against a real unit is how the model
improves; every one is a single constant.

| Assumed | Why it is open |
|---|---|
| **PT2399 modulator internals** — the step size and its adaptation | Not in the datasheet. The leak (4.7k × 0.047 µF = 221 µs) is. The step sizes (0.15 V, adapting up to 5 V on runs of identical bits, 0.1 ms run filter) are set so the model meets the datasheet's gain (−0.3 dB against −0.5 typical), THD (0.40 % at 2 MHz against 0.3 % typical, 1 % max) and output swing (1.9 Vrms at 10 % THD+N against 2 typical). Those specs do not determine them uniquely. |
| **The chip's gain per pass** | Taken as what the modulator model gives (−0.1 to −0.3 dB at audio). The mesh sits just under unity, so **this is the most sensitive number in the module**: at −0.5 dB per pass the Verb tail at zero feedback falls from over ten seconds to about four, and the loop does not start to sing until the top of the knob (about 90 % and up) instead of about 70 %. Two measurements on a unit fix it — the tail length at FEEDBACK 0 in Verb, and the FEEDBACK position at which it starts to sing. |
| **Demodulator polarity** | +1 (the delayed copy has the polarity of what went in). Flipping it changes which frequencies the loop reinforces, not whether it is stable. |
| **TILT direction** | The schematic labels the tone pot's lug 1 "CW" and the resulting stage is bass-up at that end. The manual's text reads "low to flat to high". The menu option flips it; one look at a unit settles it. |
| **FEEDBACK taper** | Linear, as a B100k. |
| **DG202 in the circuit** | The part itself is now from its datasheet (Maxim, Rev 3): 115 Ω on-resistance typical, logic low 0.8 V / high 2.4 V, logic 1 = ON. Open: the on-resistance at ±12 V rather than the tabulated ±15 V, and the mode-gate circuit, which is not on the schematic (assumed to OR onto the toggle's logic line). |
| **Op-amp and zener parts** | Not named. The ±12 V stages clip at ±10.5 V, the chips' at ±2.4 V; the bridge-and-5V1 limiter is an exponential knee at 6.2 V. |
| **Noise** | The chip's −90 dBV noise floor is injected at each comparator, so a loop past unity starts from silence. |
| **The 0.08 ms** in the measured delay relation, and the chip's 400 ms power-on reset | Not modelled. |

## Voltage conventions

Audio ±5 V. MIX CV is a raw voltage on an attenuverter: 3.6 V at full CW covers
the MIX range. The gate is read as a DG202 logic input: high above 2.4 V, low
below 0.8 V.

## Building

Amortization ships inside the Moon Technologies plugin, so it is built with the
rest of the family — see [BUILDING.md](BUILDING.md). The circuit is
`src/Amortization/Verbtronic.hpp`, self-contained, and `tests/Amortization` is 96
checks of it against independent statements of the schematic and the datasheets —
the nodal equations solved numerically, the datasheet's gain, THD and swing, and
the measured clock relation — not against the code's own expression of them.
About 2–4 % of one core at 48 kHz.

## The panel

The panel is generated. Edit `tools/panels/Amortization.py` — the spec — never
the SVG, and never `src/PanelTheme.hpp` or `src/Amortization/Panel.hpp`, all of
which are written from it:

```bash
make panel-Amortization      # artwork, the two headers, the browser mock
make preview-Amortization    # ... and open the mock
make vcv-Amortization        # ... build, then render the panel through VCV Rack
```

## Licence

GPL-3.0-or-later, with the rest of the plugin. The Verbtronic schematic is
public domain. See [../LICENSE](../LICENSE).

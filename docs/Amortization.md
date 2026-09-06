# Amortization

A Verbtronic-style digital reverb for VCV Rack 2. Filed as PUB 535.

A debt paid off in reflections: two reverb algorithms behind one switch, a tail
whose length is set by FEEDBACK, a tonal tilt that leans the tail dark or bright,
and two outputs — the blend and the wet-only signal — the way the original had
them. It is built to be overwhelmed by: the Tronic loop is allowed past unity,
and a soft limiter inside it is what keeps "relentless" from becoming "exploded".

Part of the [Moon Technologies](../README.md) plugin. 14 HP.

## What it is based on

The [Verbtronic](https://pittsburghmodular.com/verbtronic) by Pittsburgh Modular
(Richard Nicol): a 10 HP digital reverb described as designed for modular
synthesis rather than realism — lush, artificial spaces that blend with electronic
sound rather than imitating rooms. The module is discontinued and has been
released as public domain. Its panel was TONAL TILT, FEEDBACK, a MODE switch
(Verb / Tronic), OUTPUT MIX with a MIX CV attenuverter, and five jacks: IN,
MIX CV, MODE GATE, MIX OUT and VERB OUT.

No algorithm listing for the hardware is available, so the two modes are built
to the description rather than transcribed:

- **VERB** — the shorter, more natural one. A Dattorro plate (J. Dattorro,
  "Effect Design Part 1: Reverberator and Other Filters", JAES 1997): four
  input-diffusion allpasses into a Griesinger figure-eight tank of modulated
  allpasses and long delays, tapped at the paper's fourteen points for stereo.
  FEEDBACK is the tank's decay.
- **TRONIC** — the much larger, synthetic one. An eight-line feedback delay
  network with a Householder reflection as its mixing matrix and prime-length
  lines from 35 to 114 ms. The reflection is lossless, so the only loss in the
  loop is FEEDBACK and the tilt — and FEEDBACK is allowed past unity, with a soft
  limiter on every line.

Both tanks always run. A mode change is a 30 ms crossfade between two live tails,
so flipping the switch or the gate never cuts into a cold tank, and the tail you
left keeps ringing down for when the gate brings you back.

## Sections

### TERM — how long the debt runs

| Control | Range | Default | |
|---|---|---|---|
| FEEDBACK | 0 – 100 % | 50 % | The primary control. Verb: tank decay, linear up to 0.97 (about 2.5 s at 50 %, 13 s at 90 %). Tronic: loop gain on a bowed curve, reaching unity near 70 % and 1.10 at full — past unity, into the limiter, for the rest of the travel. |
| SIZE | ×0.5 – ×2, logarithmic | ×1 | Scales every delay in the live tank. Moving it bends pitch, as a size control should. |
| PREDELAY | 0 – 250 ms | 0 | Delay before the first reflection. Slewed, so it can be played. |
| MOD | 0 – 100 % | 35 % | Trimmer. Depth of the tank modulation: four slow sines moving the modulated allpasses. |

The **LIMIT** light beside the caption is the loop limiter working: the live
tank's level against its knee.

### SCHEDULE — the character of the repayments

| Control | Range | Default | |
|---|---|---|---|
| TILT | −6 – +6 dB | 0 | Tonal tilt about 1 kHz, dark to bright. Applied twice: inside the loop, where it only ever *cuts* the side it leans away from (so the loop gain never rises above FEEDBACK), and on the wet output as a full see-saw. |
| MODE | Verb / Tronic | Verb | The lit TRONIC label shows which algorithm is actually live once the gate has had its say. |
| MIX | 0 – 100 % | 50 % | Dry-to-wet crossfade at the MIX outputs only. VERB OUT ignores it. |
| FREEZE | latch button | off | Holds the tank: input muted, loop gain forced to unity, in-loop tilt bypassed. Modulation keeps running, so a frozen tail still moves. |

### PAYMENTS — what CV may adjust on the way in

Four attenuverting trimmers, each directly over its own jack, sharing one label:
**FDBK**, **TILT**, **MIX**, **SIZE**. Each is ±100 %; at full, 10 V of CV
covers the whole range of its control (TILT and SIZE are bipolar, so ±5 V
covers them). Two gates sit in the same row, beside the trims:

- **MODE GATE** — while high, Tronic flips to Verb. Exactly as the original: the
  gate only ever pulls the mode *down* to Verb, so with the switch at Verb it does
  nothing.
- **FREEZE** — gate; ORed with the button.

### Footer

| Jack | |
|---|---|
| IN L, IN R | Audio in, ±5 V. IN R normals to IN L. Polyphonic cables are summed. |
| MIX L, MIX R | The MIX blend. |
| VERB L, VERB R | 100 % wet, regardless of MIX. |

Outputs are clamped to ±12 V. Bypass routes IN L/R to MIX L/R.

#### Stereo, from a mono original

The Verbtronic is mono in, mono out. Here the input is stereo and both tanks
give a stereo pair:

- VERB is a mono-in tank by construction, so it is fed the *sum* of L and R
  after diffusion, and its stereo comes from the paper's left and right output
  taps — the original design's own stereo, which the hardware never had the
  jacks for.
- TRONIC takes a genuine stereo pair: L and R are diffused separately (the right
  chain 13 % longer, so they do not smear identically) and injected into
  alternate lines of the network; even lines sum to the left output, odd lines to
  the right.

With a mono input at IN L, both modes give a decorrelated stereo tail from a dual-mono
dry signal.

## The read-out

Top row: the tail's estimated RT60 (the time to fall 60 dB, from the loop gain
per mean loop length) in seconds, and the algorithm that is live. It reads
**OVER** when the loop is at or past unity — the tail does not fall — and
**HELD** while frozen. Bottom row: the pre-delay in ms, and **LIMIT** while the
loop limiter is working, **CLEAR** otherwise.

## Voltage conventions

Audio ±5 V. CV inputs are attenuverted: 10 V spans the full range of the control
at a trimmer of ±100 %. Gates are 0/10 V, read with a Schmitt trigger (rises at
1 V, falls at 0.1 V). Internally everything runs in ±1 = ±5 V.

## Context menu

- **Quality** — *Standard (linear)* or *High (cubic)* interpolation for every
  fractional delay read. Cubic is smoother under heavy modulation and SIZE moves
  and costs about twice the reads.
- **Original ranges** — on by default. Keeps FEEDBACK where the hardware could
  reach: the Verb tail finite (decay ≤ 0.97) and the Tronic loop just past unity
  (gain ≤ 1.10). Off, Verb reaches 1.00 — an indefinite hold without the FREEZE
  button — and Tronic reaches 1.35, well into the limiter.

Both are saved with the patch.

## Notes and approximations

- The two algorithms are built to the hardware's *description*; nothing about
  the original firmware was available to transcribe. Verb is Dattorro's plate,
  Tronic is a Householder FDN. The character — natural and shorter versus large
  and synthetic — is the design target, not the exact tail.
- The original had no SIZE, PREDELAY, FREEZE, MOD, or CV over feedback and
  tilt. At their defaults (SIZE ×1, PREDELAY 0, FREEZE off, MOD 35 %) the
  module behaves as the original's five controls describe.
- Sample-rate independent: every delay length is scaled to the running rate,
  and the buffers are reallocated on a rate change, which clears the tails.
- Monophonic by design — a reverb carries seconds of state per voice.

## Building

Amortization ships inside the Moon Technologies plugin, so it is built with the
rest of the family — see [BUILDING.md](BUILDING.md).

## The panel

The panel is generated. Edit `tools/panels/Amortization.py` — the spec — never
the SVG, and never `src/PanelTheme.hpp` or `src/Amortization/Panel.hpp`, all of
which are written from it:

```bash
make panel-Amortization      # artwork, the two headers, the browser mock
make preview-Amortization    # ... and open the mock
make vcv-preview             # ... build, then render every panel through VCV Rack
```

Amortization's panel runs at panelkit's `compact` density: three sections, a
read-out and a six-jack audio row at 14 HP.

## Licence

GPL-3.0-or-later, with the rest of the plugin. See [../LICENSE](../LICENSE).

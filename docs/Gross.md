# Gross

Schedule C: profit or loss from business. A black-box / gray-box distortion for
VCV Rack 2 -- a Wiener–Hammerstein chain with every block on the panel: input
EQ, drive, a static and a dynamic bias shift, a parametric mapping function,
dry/wet, post gain and output EQ. 18 HP, stereo.

Part of the [Moon Technologies](../README.md) plugin.

## What it is based on

Two papers, and the module is the union of their block diagrams.

1. **Felix Eichas and Udo Zölzer, "Black-Box Modeling of Distortion Circuits
   with Block-Oriented Models", Proc. 19th Int. Conf. on Digital Audio Effects
   (DAFx-16), Brno, Czech Republic, Sept. 5–9, 2016.** An *extended Wiener
   model*: an LTI filter followed by a nonlinear block. The nonlinear block
   (their Fig. 6) is a pre-gain `g_pre`, a side-chain envelope (`|x|` through a
   5 Hz low-pass) scaled by `g_bias` and subtracted from the signal before the
   mapping -- a feed-forward emulation of the bias-point shift in tubes and
   transistors -- then the mapping `m(x)`, a dry/wet mix with `g_dry = 1 −
   g_wet`, and a post gain `g_post`. The mapping (their eq. 6) is `tanh(x)`
   between the knees `−kn ≤ x ≤ kp`, and beyond each knee a second, flatter
   tanh joined so the derivative is continuous:

   ```
   m(x) = tanh(kp) + (1 − tanh²kp) / gp · tanh(gp (x − kp))      x >  kp
   m(x) = tanh(x)                                              −kn ≤ x ≤ kp
   m(x) = −tanh(kn) − (1 − tanh²kn) / gn · tanh(gn (−x − kn))   x < −kn
   ```

   They fit this to three circuits and plot the results in their Fig. 7: a
   diode clipper (hard, symmetric, no dry), the Big Muff Pi BJT stage (steep
   centre, knee around 0.3, a little dry) and the Tube Screamer op-amp stage
   (steep centre, a lot of dry, a softer negative knee).

2. **Marco Comunità, Christian J. Steinmetz and Joshua D. Reiss,
   "Differentiable Black-box and Gray-box Modeling of Nonlinear Audio
   Effects", arXiv:2502.14405, Feb. 2025** (Centre for Digital Music, Queen
   Mary University of London). Their gray-box chain for amp / overdrive /
   distortion / fuzz (their Fig. 1b) is *Parametric EQ → Gain → Offset →
   Nonlinearity → Gain → Parametric EQ*, where the offset is static for
   overdrive and distortion and **dynamic** for fuzz: it follows the input
   envelope with its own attack and release (their Fig. 2, 3a and 4a), which
   is what captures a fuzz's hysteresis and its behaviour through a note's
   attack and decay. Their nonlinearity is either an MLP or a rational
   function; the module offers a rational family as one of its curves.

Gross puts Comunità's EQ on either side of Eichas's nonlinear block, gives the
bias shift Comunità's attack and release, and exposes every number as a knob.

## The chain

```
IN ─ INPUT EQ ─ DRIVE ─┬─ (+ OFFSET + DYN · env) ─ m(x) ─┬─ WET ─ POST ─ TONE ─ LO CUT ─ DC ─ OUT
      LOW MID HIGH     │                                 │
                       └── |x| → attack / release ── env ┘ (also ENV out, DYN light)
                       └────────────── dry ──────────────┘
```

Inside the block the audio is divided by 5 V, so the mapping's knees and the
bias shifts are in the units the papers plot: a knee of 1.0 sits where a 5 V
peak lands with DRIVE at 0 dB. The mapping runs oversampled (2x by default) and
is decimated with a windowed-sinc half-band low-pass; the EQ, the envelope and
the gains run at the engine rate.

## Controls

### GROSS RECEIPTS -- the Wiener half

| Control | What it does |
|---|---|
| LOW | Low shelf at 100 Hz, ±12 dB. |
| MID | Peaking band, ±12 dB, Q 0.8. |
| MID F | The peaking band's centre, 100 Hz – 5 kHz. |
| HIGH | High shelf at 3 kHz, ±12 dB. |
| **DRIVE** | `g_pre`, −12 to +36 dB. The primary control. With the DRIVE CV at full trim, 10 V adds 24 dB (the sum is clamped to −24…+48 dB). |

### ADJUSTMENTS -- the nonlinear block

| Control | What it does |
|---|---|
| OFFSET | Static bias, −1 to +1 knee units, added to the driven signal before the mapping. Comunità's static offset. The BIAS CV adds to it (10 V at full trim = 1 unit). |
| DYN | Dynamic bias, −100 to +100 %: how much of the input envelope is added to the operating point. Negative is Eichas's feed-forward subtraction; positive follows Comunità's fuzz offsets. The light beside the label shows the envelope. |
| ATTACK | Envelope attack, 1 ms – 1 s. |
| RELEASE | Envelope release, 1 ms – 3 s. The Eichas 5 Hz low-pass corresponds to 32 ms both ways, which is the default. |
| CURVE | Which mapping (see below). Snaps to five positions. |
| KNEE+ / KNEE− | `kp` / `kn`, 0.05 – 3: where the curve leaves tanh on the positive and negative side. Unequal knees make the clipping asymmetric. |
| SHAPE+ / SHAPE− | `gp` / `gn`, 0.1 – 10: the slope factor beyond each knee. Large is a flat ceiling; small is a long, nearly linear tail (the mapping's ceiling is `tanh(k) + (1 − tanh²k)/g`, so below about 0.3 the tail runs well past unity -- WET and POST are the remedy, and the output is clamped at ±12 V regardless). |

The envelope is taken from the equalised input *before* DRIVE. Eichas takes it
after `g_pre`, but with `g_bias` a free parameter the two differ only by a
constant, and this way DYN is in the same units as OFFSET whatever DRIVE is
doing.

#### The curves

| CURVE | m(x) |
|---|---|
| TANH | Eichas & Zölzer eq. 6, exactly as above. The default. |
| RATIO | Rational soft clip, `x / (1 + |x|^g)^(1/g)`: `x / (1 + |x|)` at SHAPE 1, a hard clip as SHAPE rises. |
| HARD | Hard clip at the knees. SHAPE has no effect. |
| DIODE | Exponential clipper, `(1 − e^(−|x|^g))^(1/g)`: `1 − e^(−|x|)` at SHAPE 1, squarer corners as it rises. |
| CUBIC | `x − x³/3`, held at 2/3 beyond ±1. SHAPE has no effect. |

For the four non-Eichas curves KNEE+ / KNEE− scale the half-curve so it
saturates at +kp / −kn, and SHAPE+ / SHAPE− set its hardness; all four trims do
something on every curve except the two noted.

### NET -- the Hammerstein tail

| Control | What it does |
|---|---|
| WET | `g_wet`, 0 – 100 %; dry is `1 − wet`. The WET CV adds to it. |
| POST | `g_post`, ±24 dB. |
| TONE | A tilt about 1 kHz: −6 dB on one shelf and +6 dB on the other at either extreme. The TONE CV adds to it. |
| LO CUT | 12 dB/oct high-pass, 10 Hz – 1 kHz. A fixed 5 Hz DC blocker follows it whatever it is set to. |

The dry path taps the equalised input before DRIVE, so WET is a clean parallel
blend. Eichas's Fig. 6 draws the branch after `g_pre`; the context menu can
move it there.

## Jacks

| Jack | Direction | Convention |
|---|---|---|
| DRIVE, BIAS, WET, TONE | in | CV, nominally ±10 V, each with its own attenuverter (the trimpot above the jack; the pair shares one label). |
| ENV | out | The input envelope, 0 – 10 V; 10 V is a 5 V-peak input. The larger of the two channels' envelopes. |
| IN L / IN R | in | Audio, ±5 V nominal. IN R is normalled to IN L. A polyphonic cable is summed. |
| OUT L / OUT R | out | Audio, clamped to ±12 V. |

Bypass passes IN L to OUT L and IN R to OUT R. The module is monophonic per
side; there is filter and resampler state in every block.

## The read-out

The display draws the transfer curve as it stands -- DRIVE, OFFSET and the live
dynamic bias included, the dry blend included, before POST and the output EQ --
over an input of ±5 V, so a knob move is visible before it is audible and the
curve slides sideways as the envelope pushes the operating point. Beside it: the
preset name (or CUSTOM once any knob has moved), the curve family, DRIVE in
dB, the operating point BIAS as it stands, and the envelope and wet fraction.

## Context menu

* **Filing status** -- the presets. Applying one sets every knob on the panel;
  the read-out names it until something moves.
  * *Diode Clipper* -- Eichas Fig. 7a. Symmetric, vertical at the origin, flat
    past the knee, no dry; the 7.2 kHz RC before the diodes is the HIGH shelf.
  * *Big Muff* -- Fig. 7b. Steep centre to about ±0.3, then the dry path's slope
    out to ±0.5 at 1 V (WET 80 %).
  * *Tube Screamer* -- Fig. 7c. A lot of dry (WET 25 %), a gentler negative
    knee, and the pedal's 720 Hz mid hump.
  * *Fuzz* -- Comunità's dynamic offset: fast attack, 300 ms release, DYN +70 %.
  * *Flat* -- unity: DRIVE 0 dB, knees at 3, everything else off.
* **Oversampling** -- Off, 2x (default) or 4x, for the mapping only.
* **Dry path taps after DRIVE** -- move the dry branch to where Eichas's Fig. 6
  draws it, after `g_pre`. Off by default.

The oversampling and dry-tap settings are saved with the patch.

## Notes on fidelity

* The paper reports the model, not its fitted parameter vectors; the three
  circuit presets are read off the plotted curves in Fig. 7 and reproduce their
  shape, not quote their numbers.
* Eq. 6 as typeset writes the outer argument as `gp·x − kp`, which is only
  continuous at the knee when `gp = 1`; the property the paper claims -- a
  continuous derivative at the joins -- holds for `gp (x − kp)`, which is what
  the module computes.
* The Fig. 7 curves cannot be reproduced with the dry branch after `g_pre`
  (the dry slope would be `g_pre` times too steep), which is why the default
  taps it before DRIVE.
* Comunità's nonlinearity is a learned MLP or rational function; the RATIO
  curve is a hand-shaped rational family, not a fitted one. Their parametric EQ
  is a full multi-band; here it is shelf / peak / shelf in and tilt / low-cut
  out.
* Control changes are read every 8 samples and the gains slewed over about
  1.5 ms, so audio-rate CV into DRIVE, BIAS or WET is sampled, not tracked.

# Kickback — FORM 1099-NEC

**A Modular in a Week drum bank.** Eight lo-fi percussion circuits pulled from
Modular in a Week's Day 9 folder — kick, snare and hat from one three-in-one
schematic, a bridged-T bass drum, a twin-T tom, an XOR-stacked bell, a
percussive noise voice and the noise circuit it descends from — filed on one
panel, one trigger each, one mix. 30 HP, monophonic (drums; no per-channel
processing).

Form 1099-NEC is the IRS's *Nonemployee Compensation* form — what a payer
files for a contractor who isn't on payroll. Eight contractors, one payer,
one form.

## What it is based on

Every voice comes from [Modular in a Week](https://modularinaweek.tv)'s Day 9
folder, "drums." None of these are simulated component-for-component; each
is modelled on what its circuit actually does — its topology, its ranges and
its quirks — the way the rest of this plugin's voltage-controlled filters are
modelled on their donor hardware rather than transistor-level. Where a knob
below is described as an "approximation," that is the specific liberty taken
and why.

| voice | source | designer |
|---|---|---|
| KICK, SNARE, HAT | `BaSnaHi.pdf` | kristian.borgstedt (EFM DBop) |
| SMURF | `SmurfDrum_BassDrumish.jpg`, "Smurf Drum" half | Tiny Dazzler Electronics |
| TOM | `TomTomTom.pdf` | Kristian Blåsol / SourceryStudios, gate-to-trigger by Ken Stone (CGS24) |
| BELL | `XORbell.pdf` | Kristian Blåsol / SourceryStudios, design and idea from Elliot Williams' *Logic Noise* series |
| NOISE | `Percussive Noise Voice.pdf` | Kristian Blåsol, noise core by A_Magic_Pulsewave |
| DAZZLER | `Tiny Dazzler Schematic.png` | Tiny Dazzler Electronics |

### KICK — BaSnaHi's bassdrum

The BC547 stage (Q1) and its RC pair (R1–R8, C1–C5) is a single resonant
partial that a trigger edge shocks into ringing rather than an oscillator
that runs continuously — a twin-T-style *ping*. Kickback models it as a
two-pole modal resonator (a difference equation with a complex pole pair)
struck with an impulse on every trigger: **PITCH** sets the ring frequency
(35–220 Hz), **DECAY** its ring time, and **DRIVE** pushes the same
single-ended transistor clip (soft one way, harder the other) that the real
BC547 stage runs into as the strike gets loud.

### SNARE — BaSnaHi's snare stage plus its avalanche noise tap

Q2's resonant pair (the snare-pitched twin of the kick's) is summed with the
noise cascade around Q3–Q6 and D3/D4 — two base-emitter junctions run in
forward-biased positive feedback, which is where the original's "Snare
noise?" output comes from. **PITCH** tunes the resonant partial (150–420 Hz),
**DECAY** sets both components' ring time (the noise a little shorter, as a
snare's rattle dies faster than its shell), and **SNAP** crossfades between
them the way the original's two output taps (P4 tone, P6 noise) let you pick
by hand.

### HAT — the same noise tap, read differently

BaSnaHi's "Output HH?" node is the identical avalanche-noise cascade through
a tighter high-pass. Kickback reuses a noise source through a two-pole
high-pass whose corner **TONE** sweeps (2–11 kHz), struck by the trigger and
decaying over **DECAY**. *Approximation:* the six-transistor cascade shared
with SNARE is not literally re-derived per branch — HAT gets its own
independent noise generator through its own filter, which is the practical
equivalent for a circuit whose only real per-branch difference is where the
high-pass corner sits.

### SMURF — the "Smurf Drum" half of the Tiny Dazzler sheet

*Distinct from KICK.* A two-transistor astable (the sheet's 1M PITCH pot, the
10 k/22 k cross-feedback, the 0.01 µF cap) runs not off a rail but off the
trigger's own decaying envelope, so both loudness *and* pitch sag together as
the strike dies — the "zippy splat" the schematic's own notes describe.
**PITCH** sets the astable's base frequency (70–700 Hz), **DECAY** the
envelope's fall time, and **SWEEP** is how much of that pitch-sag is audible:
at 0 it is a plain decaying tone, at 1 the pitch dives a full octave-and-change
as the envelope empties, matching the sheet's "switch to bring a cap in/out
of the upper part of the circuit to change the pitch decay."

### TOM — TomTomTom's three twin-T rings, one switch

Three physically separate CD4069-buffered twin-T filter branches, each
shocked by its own Ken Stone gate-to-trigger pulse (CGS24) and captioned
"change these 3 resistors as you'd like your sound." Rather than three TRIGs,
Kickback exposes one modal resonator (as KICK's) with **RANGE** — a
three-position switch — standing in for which of the three tuned branches is
sounding (LOW ≈ 90 Hz, MID ≈ 160 Hz, HIGH ≈ 280 Hz center), **PITCH** trimming
±a fifth around whichever band is selected, and **DECAY** setting ring time.
This is the fold the brief allows when three near-identical designs do not
fit as three separate voices at 30 HP: one voice, one switch, documented here.

### BELL — XORbell

Six 40106 relaxation oscillators (their own R/C pair each) run into three
4070 XOR gates, gated by a decay envelope — Elliot Williams' *Logic Noise*
XOR-drone trick, concatenated. XOR of two square waves in their bipolar (±1)
encoding is exactly their *product*, so Kickback runs three band-limited
(polyBLEP) square oscillators and multiplies them: **PITCH** sets the lowest
partial (180–1800 Hz), **TIMBRE** spreads the upper two partials away from it
at inharmonic ratios for the metallic/bell/cowbell character, and **DECAY**
is the envelope the original's diode-RC network gives its output stage.
*Approximation:* three oscillators rather than six — a third partial already
supplies the inharmonic, clangy character the extra three mostly reinforce.

### NOISE — Percussive Noise Voice

The trigger's own RC decay drives a vactrol (a photoresistor lit by an LED),
whose slow-following resistance sets a low-pass corner over noise from a
three-transistor avalanche tap (T1–T3). **TONE** stands in for the pair of
capacitors (C5/C6) the original swaps by hand between "Snare" and "HiHat"
values, as one continuous sweep of how bright the vactrol opens the filter
(300 Hz–9 kHz on top of the ~150 Hz floor), **DECAY** is the base envelope
time, and the **DEC CV** jack is the sheet's own "Decay CV in" (P2), adding
directly to the envelope's time constant.

### DAZZLER — the Tiny Dazzler noise voice it grew from

A transistor wired backwards (base and emitter swapped — "EBC," in the
sheet's own notation, avalanching its reverse-biased junction for noise) into
a decay envelope and a switched two-pole low-pass; the sheet gives two named
capacitor pairs, "Snare" and "HiHat." **KIT** is that switch (a lower, wider
corner for snare-like noise; a higher, tighter one for hi-hat-like), **DECAY**
is the envelope time, and **CRACKLE** is the sheet's own margin note —
"higher values for C1 create more crackle in the decay" — implemented as a
continuous blend that both brightens the corner and lets a second, slightly
detuned pole beat against the first rather than rolling off flat.

## Panel

Each voice is a column: its name labels its TRIG jack (lighting on strike),
its controls stack beneath, and its OUT sits at the foot of the same column
in the footer band. ACCENT and LEVEL flank the strike row and reappear in the
footer as the ACCENT CV jack and the MIX out.

| control | what it does |
|---|---|
| **ACCENT** (knob, strike row) | How much the ACCENT CV jack (footer) can raise a strike's level above unity. At 0 the CV does nothing; every strike is always full-velocity if nothing is patched. |
| **LEVEL** (knob, strike row) | Sums all eight voice outputs into MIX at this level. |
| **KICK / SNARE / HAT / SMURF / TOM / BELL / NOISE / DAZZLER TRIG** (input) | ≥1 V rising edge strikes the voice (Schmitt trigger, low 0.1 V / high 1 V). Retriggerable. |
| **PITCH / TONE** (big knob, row 1) | Each voice's fundamental — see above for range. DAZZLER has none; its column carries **KIT** here instead (2-position: Snare / Hi-hat). |
| **DECAY** (knob, row 2) | Every voice's ring or envelope time — see above for range. |
| third knob (row 3) | **DRIVE** (KICK), **SNAP** (SNARE), **SWEEP** (SMURF), **RANGE** (TOM, 3-position), **TIMBRE** (BELL), **CRACKLE** (DAZZLER). HAT has none. NOISE's row-3 slot is its **DEC CV** input instead. |
| **OUT** (output, footer) | Each voice, ±5 V nominal, individually. |
| **MIX** (output, footer) | All eight voices summed at LEVEL. |
| **ACCENT** (input, footer) | 0–10 V unipolar CV, scaled by the ACCENT knob. |

## Voltage conventions

Triggers are ≥1 V rising edges (Schmitt hysteresis, low 0.1 V / high 1 V) —
plain gates work too, since a gate's leading edge is a rising edge. Audio
outputs are ±5 V nominal, hard-clamped to ±12 V. ACCENT CV is 0–10 V
unipolar. NOISE's DEC CV is unipolar as well, adding up to roughly a second
to the DECAY knob's own time.

## What was left out

Kickback is monophonic and does not implement per-channel polyphony — the
brief's own rule for drum/percussion voices. There is no context menu: every
control the six circuits expose already has a knob, switch or CV jack on the
panel, so there is nothing left over that belongs in one.

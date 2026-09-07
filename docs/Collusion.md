# Collusion

Six LFOs that listen to each other, for VCV Rack 2. Form 211 — the IRS
whistleblower claim, the form you file about people who have agreed among
themselves.

Part of the [Moon Technologies](../README.md) plugin.

Most modulation sources are independent by construction: six LFOs are six LFOs,
and if you want them related you patch one into another. Collusion starts from
the opposite premise. The six oscillators here are a *population*, each with its
own natural rate, and one knob — **COUPLING** — says how hard each is pulled
toward the others. At zero they free-run and drift apart. Past a threshold they
spontaneously fall into step, and stay there.

That threshold is real and it is sharp. It is Kuramoto's phase transition, and
where it sits depends only on how far apart the six natural rates are, which is
what **SPREAD** sets. COUPLING and SPREAD are therefore not two gain controls
but a phase diagram, and the module is the business of moving around inside it.

## Layout

21 HP. **FILINGS** is what each filer would do unsupervised; **AGREEMENT** is
what they do about each other and the record they keep of it; **PARTIES** is the
six of them, each with a lamp; the footer carries the collective outputs.

## The idea

Each filer *i* has a phase and a natural frequency, and its phase advances by

```
dtheta_i/dt  =  omega_i  +  K * omega_0 * (1/N) * sum_j sin(theta_j - theta_i - alpha)
```

* `omega_i` is that filer's own rate — `RATE`, detuned by its place in the
  `SPREAD` fan.
* `K` is **COUPLING**, in units of the swarm's centre rate `omega_0`. Measuring
  it that way is what makes the coupling mean the same thing at 0.05 Hz and at
  4 kHz: `RANGE` moves the whole phase diagram without moving where on it you
  are standing.
* `alpha` is **EVASION**, a phase lag: the coupling aims a little *behind* where
  the others actually are, so the population can never quite settle.

Kuramoto's 1975 result is that this has a genuine phase transition. Below a
critical coupling `Kc` the population stays incoherent; above it, a macroscopic
fraction locks to one collective frequency, and that fraction grows continuously
from zero as `K` passes `Kc`. For an evenly spread fan of natural rates,
`Kc ≈ 0.9 × SPREAD` in octaves — so with SPREAD at a half octave the transition
is around a fifth of COUPLING's travel, and at full SPREAD it is around
two thirds of it.

You can watch it happen on the six lamps, and you can measure it: the **ORDER**
output is Kuramoto's order parameter `r`, 0 V when the population is scattered
and 10 V when it is in unison.

## FILINGS

| Control | Type | Description |
|---|---|---|
| **RATE** | large knob | The centre of the swarm. `V/OCT` tracks it |
| **SPREAD** | knob | How far the six natural rates fan out around `RATE`, up to 1.5 octaves either side. Zero makes six identical oscillators, which lock at any coupling at all |
| **SHAPE** | knob | Warps each cycle from a sine toward a relaxation spike (below) |
| **RANGE** | switch | `LO` 0.02–40 Hz, `HI` 20 Hz–4 kHz |
| **DEAL** | button | Re-rolls the fan: six random natural rates instead of the even spread. The fan is saved with the patch |

**SHAPE** is a phase warp, not a waveform crossfade. It makes the cycle dwell
near its minimum and then snap through the rest — the shape a relaxation
oscillation has, which is what van der Pol's equation produces as its
nonlinearity grows and what every RC-and-a-comparator LFO in a DIY case actually
puts out. It is a family resemblance and not an integration of van der Pol's
ODE; the population model needs a phase for each member, and an ODE oscillator
does not have one to give.

How much of the knob is usable falls with frequency, so sweeping the swarm up
into the audio band softens the spike back toward a sine rather than folding a
stack of aliases down over the fundamental. There is deliberately no hard
band-limit — this is a modulation source pushed into the audio range on purpose,
and some grit up there is the reason to do it. What the ceiling prevents is
SHAPE quietly becoming a noise control.

## AGREEMENT

| Control | Type | Description |
|---|---|---|
| **COUPLING** | large knob, bipolar | How hard each filer is pulled toward the others. Left of centre they *repel* and spread into maximal disagreement; right of centre they attract and lock |
| **EVASION** | knob | Sakaguchi's phase lag, 0 to 0.4 turns. See below |
| **SCHEME** | 4-position snap knob | Who each filer can hear |
| **LEVERAGE** | knob, bipolar | How much of the ledger's own voltage goes back into the rates |
| **TERM** | 8-position snap knob | Shift-register length: 2, 3, 4, 5, 6, 8, 12 or 16 steps |
| **AUDIT** | knob | How often a step is rewritten from the swarm rather than recirculated |
| ORDER lamp | beside the caption | How much the population currently agrees |

Below the knobs, six attenuverted CV inputs: **V/OCT**, **COUPLE**, **EVADE**,
**SHAPE**, **SPREAD**, **AUDIT**. V/OCT's depth starts at full, so the swarm
tracks pitch without being asked; the rest start at zero, which is the only safe
default for an attenuverter.

### SCHEME

| Setting | Wiring | What it does |
|---|---|---|
| **CARTEL** | all to all | Kuramoto's own arrangement, and the only one with a clean phase transition. Every filer is pulled toward the population's mean phase |
| **RING** | each to its two neighbours | Local coupling cannot impose one phase on everybody. What forms instead is a travelling wave: the six lamps chase each other round rather than flashing together |
| **PYRAMID** | a one-way cascade | Filer 1 hears nobody, 2 hears 1, 3 hears 2. The head of the structure is unaffected by anything below it, so `1` stays a clean LFO and `2`–`6` are a chain of increasingly late, increasingly distorted copies of it |
| **FIREFLY** | pulse coupled | Nothing happens between fires; a fire jumps every other phase at once. Locks harder and faster than the continuous schemes and stays locked — the rhythmic setting |

FIREFLY is Mirollo and Strogatz's model of the synchronously flashing fireflies
of south-east Asia: each oscillator, on firing, pulls the others toward its own
phase, and for almost any starting condition the population ends up flashing
together. Turn COUPLING negative and the same mechanism drives them apart
instead, which is a good way to get six evenly-distributed phases out of one
clock.

### EVASION

With the coupling aimed exactly at the others, the population has only two
things it can do: lock, or not. The phase lag buys everything in between —
clusters, travelling waves, and populations where part is locked and part is
incoherent *at the same time* and stays that way. It is the single knob that
turns COUPLING from a switch into a landscape, and it is worth exploring at
every SCHEME.

## PARTIES

Six outputs, ±5 V, one per filer, each with the lamp that shows its own cycle.
The lamps are the instrument's read-out as much as its output: six of them
beating against each other below the critical coupling, and one lamp six times
over above it.

## LEDGER

The rungler. Rob Hordijk's Benjolin clocks an eight-bit shift register from one
oscillator, feeds it from the comparator of another, reads three of its bits
through a resistor ladder, and sends the resulting "stepped havoc wave" back
into both oscillators' frequencies. Music Thing Modular's Turing Machine adds
the control that makes it playable: how likely a bit is to be rewritten rather
than recirculated.

This is both, with one difference. The bit that gets written is not a comparator
on one oscillator but on the whole population's mean field, so what the register
fills with depends on how much the swarm agrees — incoherent below the critical
coupling, and a short repeating figure above it. **The melody is the phase
transition, read out one bit at a time.**

* **TERM** is how many bits go round. Short terms repeat audibly; 16 is long
  enough to sound through-composed.
* **AUDIT** fully left seals the loop and the pattern repeats forever. Fully
  right, every step is dictated by the swarm. In between it mutates, a bit at a
  time, and the pattern evolves without ever losing its shape.
* **CLK**, patched, takes the register off filer 1 and puts it on your clock —
  which is the setting that makes the module playable rather than merely alive.
  Unpatched, filer 1 clocks it, which is the Benjolin's own arrangement.
* **LEVERAGE** closes the loop the other way: the ledger's voltage goes back
  into the rates. Deliberately *not* common mode — it takes a different amount
  off each filer, weighted by where that filer sits in the fan, so a step of the
  register moves the population through the phase transition rather than just
  changing its tempo. This is the Benjolin's per-oscillator rungler attenuators,
  ganged to one knob, and it is what stops a locked swarm from staying locked.

## Footer

| Jack | Direction | Description |
|---|---|---|
| **SYNC** | input | Aligns every filer to phase zero. The population starts in perfect agreement and you watch it come apart — the same transition as COUPLING run backwards, and much easier to hear |
| **CLK** | input | Clocks the ledger. Its lamp is the register stepping |
| **CONSENS** | output | The population's mean wave, ±5 V. Its amplitude collapses toward silence when the swarm is incoherent and swells to a full-scale wave when it locks — a modulation source that is loud exactly when the swarm agrees |
| **ORDER** | output | Kuramoto's `r`, 0–10 V. Slow, smooth, and unlike anything else on the panel: it reports a *statistic* of the patch rather than a signal in it |
| **LEDGER** | output | The rungler's stepped CV, ±5 V |
| **PULSE** | output | 0/10 V, a gate every time any filer fires, held for a twentieth of a cycle. Incoherent, the six fires are spread around the cycle and you get six separate gates; locked, they merge into one. It is the phase transition as *rhythm* — patch it to a drum and COUPLING collapses a six-against-one polyrhythm into a unison hit |

## Menu

* **Ledger resolution** — 3 bits (8 steps, the Benjolin's ladder and the setting
  that reads as a tune), 5 bits, or 8 bits.
* **Deal a new fan** / **Restore the even fan** — the same as the DEAL button,
  and its undo. The even fan is the one the `Kc ≈ 0.9 × SPREAD` rule of thumb is
  actually true for.

## Patches to start from

**Watch the transition.** Nothing patched but `ORDER` to a scope. SPREAD at
half, SCHEME on CARTEL, and sweep COUPLING slowly up from zero. The lamps drift,
then hesitate, then lock; ORDER climbs from about 4 V to 10 V over a small part
of the knob's travel. That knee is `Kc`. Now raise SPREAD and find it again
further along.

**Six-voice detune that breathes.** RANGE to `HI`, patch `V/OCT` from a
keyboard, take the six outputs to six VCAs. SPREAD is your detune width, and
COUPLING is how much the six voices agree to be one voice — a chorus control
made of physics rather than of delay lines.

**Generative melody.** SCHEME on FIREFLY, COUPLING just above the knee, `CLK`
from your clock, `LEDGER` to a quantizer. AUDIT around 20% mutates the line
slowly; SPREAD is now a "how strange should the melody be" control, because it
decides whether the swarm agrees enough to write a repeating figure.

**Runaway.** LEVERAGE hard right, AUDIT at half, COUPLING just above the knee.
The ledger detunes the swarm, the swarm rewrites the ledger, and the patch
never repeats. This is the Benjolin path; add `PULSE` as a gate source and
`CONSENS` as the audio.

**Six drums becoming one.** `PULSE` to a percussion voice, RANGE on `LO`, RATE
around 4 Hz, SPREAD at half. At COUPLING zero the six filers fire at six
different times and you hear a loose six-against-one; bring COUPLING up through
the knee and the six hits slide together into a single stroke.

**Polyrhythm from one clock.** SCHEME on FIREFLY, COUPLING hard *left*, SYNC
from your clock. The filers repel each other into evenly distributed phases, and
the six outputs become six related-but-offset LFOs locked to your tempo.

## Prior art

The module is an instrument, not a paper, but it is built out of published work
and it is worth saying which:

* Y. Kuramoto, "Self-entrainment of a population of coupled non-linear
  oscillators", *Int. Symposium on Mathematical Problems in Theoretical
  Physics*, Lecture Notes in Physics **39** (Springer, 1975), 420–422 — the
  model and the critical coupling.
* S. H. Strogatz, "From Kuramoto to Crawford: exploring the onset of
  synchronization in populations of coupled oscillators", *Physica D* **143**
  (2000), 1–20 — the review that makes the transition legible.
* H. Sakaguchi and Y. Kuramoto, "A soluble active rotator model showing phase
  transitions via mutual entrainment", *Prog. Theor. Phys.* **76** (1986),
  576–581 — the phase lag EVASION implements.
* R. E. Mirollo and S. H. Strogatz, "Synchronization of pulse-coupled biological
  oscillators", *SIAM J. Appl. Math.* **50** (1990), 1645–1662 — the FIREFLY
  scheme.
* B. van der Pol and J. van der Mark, "Frequency demultiplication", *Nature*
  **120** (1927), 363–364 — relaxation oscillation, the shape SHAPE reaches
  toward, and the paper whose "irregular noise" is probably the first
  experimental observation of deterministic chaos.
* Rob Hordijk's Benjolin and its rungler; Music Thing Modular's Turing Machine
  (Tom Whitwell) — the LEDGER section.

## Notes

* The engine lives in [`src/Collusion/Swarm.hpp`](../src/Collusion/Swarm.hpp)
  and includes nothing from Rack, so
  [`tests/Collusion`](../tests/Collusion) compiles and measures the shipping
  signal path with a host compiler and no SDK — including the phase transition
  itself.
* Coupling uses a fast sine approximation, accurate to about 7 × 10⁻⁴. The
  coupling term is a force, not a signal, and that is a hundred times finer than
  the panel can be set.
* `LEDGER` and the fan are saved with the patch.

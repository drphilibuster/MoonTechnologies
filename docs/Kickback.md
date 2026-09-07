# Kickback — FORM 1099-NEC

**A Modular in a Week drum bank that is also a drum machine.** Six percussion
voices from Modular in a Week's Day 9 folder, each with its own trigger and
output; a clock; a Euclidean pattern engine that plays any voice whose trigger
jack is empty; and a grid mode where each voice instead runs at its own multiple
or division of the clock, from /256 to ×256. 31 HP, monophonic (drums; no
per-channel processing).

Form 1099-NEC is the IRS's *Nonemployee Compensation* form — what a payer files
for a contractor who isn't on payroll. Six contractors, one payer, one form.

## What it is based on

Every voice comes from [Modular in a Week](https://modularinaweek.tv)'s Day 9
folder, "drums." None of these are simulated component-for-component; each is
modelled on what its circuit actually does — its topology, its ranges and its
quirks — the way the rest of this plugin's voltage-controlled filters are
modelled on their donor hardware rather than transistor-level.

| voice | source | designer |
|---|---|---|
| KICK (BRIDGE), HAT, BELL | `BaSnaHi.pdf` | kristian.borgstedt (EFM DBop) |
| KICK (SMURF) | `SmurfDrum_BassDrumish.jpg`, "Smurf Drum" half | Tiny Dazzler Electronics |
| TOM I / II / III | `TomTomTom.pdf` | Kristian Blåsol / SourceryStudios, gate-to-trigger by Ken Stone (CGS24) |
| SNARE — XOR mode | `XORbell.pdf` | Kristian Blåsol / SourceryStudios, design and idea from Elliot Williams' *Logic Noise* series |
| SNARE — VACTROL mode | `Percussive Noise Voice.pdf` | Kristian Blåsol, noise core by A_Magic_Pulsewave |
| SNARE — DAZZLE mode, HAT's top end | `Tiny Dazzler Schematic.png` | Tiny Dazzler Electronics |

Two circuits are not where their name would suggest, and deliberately. BaSnaHi's
snare stage — a modal shell with wires against it — reads as a struck metal pipe
rather than as a snare, so it is not here at all: it became **[Toll](Toll.md)**,
a module of its own, where being a struck metal pipe is the point. The **SNARE**
column took the three circuits that do sound like snares, under one selector.
And the Tiny Dazzler is no longer a voice either: its two halves went where they
belong, the long end of its delay line as a snare mode and the short end as the
hat's third texture.

### How the drums are synthesised

The circuits say what each voice is *made of*. How a struck drum is modelled at
all comes from the drum-synthesis literature, and it is the part that changed
most in this version — the earlier Kickback struck a single two-pole resonator
with a one-sample impulse, and a single resonator with no attack layer and no
pitch glide is a plucked bass note, which is what it sounded like.

- **Kirby & Sandler, "Advanced Fourier Decomposition for Realistic Drum
  Synthesis"** (DAFx-20). A circular membrane's modal frequencies are
  `f(m,n) = x(m,n)/(2πr)·√(T/σ)` — so their *ratios* are the ratios of the
  Bessel zeros and depend on nothing else. Mode amplitudes follow from where
  the stick lands. High strike intensity raises the local tension and produces
  the characteristic downward pitch glide. And a drum's attack is a separate,
  dense, stochastic component between roughly 1 and 8 kHz: their listening test
  found a modelled fundamental *indistinguishable* from a real tom (49.6% in
  ABX, below chance), while removing everything above 2 kHz was caught 98.4% of
  the time. The attack is not a garnish; it is most of what a listener uses.

- **Bilbao, "Time domain simulation and sound synthesis for the snare drum"**
  (JASA 131(1), 2012). The contact force of a stick on a head is a raised
  cosine of duration T₀, where "F₀ increases, and T₀ decreases as strike
  velocity increases." The Berger averaged-nonlinearity term behind the pitch
  glide is "quite important perceptually in the case of certain drums, such as
  toms." And snare wires meet the head through a one-sided power-law collision
  — they push back only while actually in contact.

- **Shier et al., "Differentiable Modelling of Percussive Audio with Transient
  and Spectral Synthesis"** (Forum Acusticum 2023). Transients belong in the
  model as their own signal component alongside sines and noise, rather than
  being hoped for as a by-product; their onset error on kick drums falls by a
  fifth the moment a transient path exists at all.

- **Karplus & Strong, "Digital Synthesis of Plucked-String and Drum Timbres"**
  (CMJ 7(2), 1983). The probabilistic drum recurrence, its blend factor b and
  its stretch factor S — the whole of the snare's DAZZLE mode and the top of the
  hat's RATTLE.

Every voice takes 1 V/oct in the code, but there is no V/OCT jack on the panel
any more: that row became RATIO. Kickback is a rhythm instrument, and the pitched
voice that wanted a keyboard is [Toll](Toll.md).

Four things follow from all that, and they are shared by every voice:

**The strike is a pulse, not an impulse.** A raised cosine of controllable
width, three milliseconds of felt at one end and a third of a millisecond of
wood at the other, shortening further as velocity rises. A finite contact time
is a lowpass on the excitation: a long, soft contact simply cannot put energy
into a mode whose period is shorter than the contact. That is why hitting
harder gets brighter here rather than just louder.

**The click is summed in directly.** A mode bank is a set of narrow resonators
well under a kilohertz, so nearly none of the strike's own bandwidth reaches
the output through it — the bank filters the stick out. The derivative of the
contact pulse is one cycle of a sine at 1/T₀, so the click's pitch tracks
contact time for free: a 3 ms felt beater thumps at 330 Hz, a hard stick cracks
at 2.9 kHz. On top of that sits a short stochastic attack layer standing in for
the dense 1–8 kHz partials the paper describes.

**The pitch glide is driven by the drum's own energy.** Not by a separate
envelope: a rimshot dives and a ghost note does not, and that falls out of
feeding the tension term from the mode bank's own output. BEND sets how
nonlinear the head is; velocity does the rest.

**Modes are a bank, and where you hit decides which ones sound.** Eight
resonators at the Bessel-zero ratios (1.000, 1.594, 2.136, 2.296, 2.653, 2.918,
3.155, 3.500), with the upper ones decaying faster. Mode (m,n)'s displacement
at radius ρ is `J_m(x(m,n)·ρ)`, so a dead-centre strike wakes only the circular
m = 0 modes and sounds like one boomy partial, while a strike toward the rim
brings in the radial modes and goes hollow and complex.

## The voices

Every voice takes the same five controls in the same order, so the panel is one
grid rather than six special cases: **TUNE**, **V/OCT**, **DECAY**, **BEND**,
and one **character** control that is whatever that circuit is actually for.

On KICK and SNARE that fifth control is a *selector* rather than a knob, because
that is the control those two circuits have. Where that happens the voice's own
character rides on BEND, and the panel says so: a column with detents engraved
in its bottom well is a column whose BEND does double duty.

### KICK — two models, one set of controls

**MODEL** switches between them. *Bridge* is BaSnaHi's bassdrum stage (Q1,
R1–R8, C1–C5): a diode-coupled trig charges the base network and shocks the
transistor's RC feedback pair into ringing, modelled as a mode bank struck dead
centre. *Smurf* is the "Smurf Drum" half of `SmurfDrum_BassDrumish.jpg`: a
two-transistor astable (the 1M PITCH pot, 10 k/22 k cross-feedback, 0.01 µF cap)
running off the trigger's own decaying envelope rather than a rail, so loudness
and pitch sag together — the "zippy splat" its notes describe.

They share controls because they answer the same question two ways: a ringing
filter and a starved oscillator both make a bass drum, and which one a patch
wants is a switch, not two columns of panel. **TUNE** 32–190 Hz. **BEND** is the
pitch dive, and on the Smurf model it is the sheet's own switched cap on the
upper half of the circuit; it also sets how hard the transistor stage is driven,
since MODEL has taken the character knob.

*Approximation:* the astable is rounded off by two poles that track its own
pitch, rather than left as a hard square. A two-transistor astable is
slew-limited by the very RC pair that sets its period, and leaving it square
costs the voice its beater — a square's harmonics run past 9 kHz for the whole
note and mask any click sitting on them.

### SNARE — three circuits under one selector

**MODE** picks which is running. They are three genuinely different sounds
rather than one sound with a knob, because a snare is the one drum here with no
single right answer: what people want from it runs from a tuned crack to a wash
of noise to a rattle.

- **XOR** — XORbell's six 40106 relaxation oscillators through three 4070 XOR
  gates. XOR of two square waves in their bipolar (±1) encoding is exactly their
  *product*, so three band-limited squares multiplied give the inharmonic,
  clangy crack, with a short modal ring underneath for body. **TUNE** 110–900 Hz.
  *Approximation:* three oscillators rather than six — a third partial already
  supplies the character the extra three mostly reinforce.
- **VACTROL** — the Percussive Noise Voice. The trigger's own RC decay drives a
  vactrol (a photoresistor lit by an LED), whose slow-following resistance sets
  a lowpass corner over noise from a three-transistor avalanche tap (T1–T3).
  **TUNE** is the C5/C6 pair the original swaps by hand between "Snare" and
  "HiHat" values, made continuous: 500 Hz–9 kHz.
- **DAZZLE** — Karplus & Strong's 1983 drum recurrence at the long end of its
  delay line, which the paper itself calls "the effect of a snare drum". Fed by
  the Tiny Dazzler's backwards-wired transistor (base and emitter swapped —
  "EBC", in the sheet's own notation, avalanching its reverse-biased junction
  for noise). **TUNE** is the table length, 70–460 Hz.

**BEND** does double duty here, because MODE has taken the character knob — and
it carries the one control each mode actually wants: the inharmonic spread on
XOR, the vactrol's lag on VACTROL, and on DAZZLE both of the paper's own
parameters at once, the blend factor *b* running down from ½ toward the
"plucked bottle" and the stretch factor *S*, which the paper notes "increases
the snare sound" as it rises.

### HAT — BaSnaHi's "Output HH?" node, read the way an 808 reads it

The original is the same noise cascade through a tighter highpass. A pure
filtered-noise hat is the one everybody writes and nobody keeps: what makes a
hi-hat a hi-hat is that two lumps of metal are ringing. Three square oscillators
multiplied together supply the metal.

**RATTLE** runs through all three of the hat's textures rather than switching
between them: **metal** at the bottom, **noise** in the middle, and the Tiny
Dazzler's Karplus-Strong rattle at the *short* end of its delay — what the 1983
paper calls a brushed tom-tom — at the top. It is a sweep rather than a switch
because the hat had no control to spare for one and RATTLE was already the
texture control; the upside is that both boundaries are playable.

**TONE** sets the highpass corner and the metal's pitch together, 1.8–11 kHz;
**BEND** sweeps that corner down as the hat dies, which is what a pitch envelope
amounts to on a voice with no pitch. Two envelopes, not one: a fast metallic
burst and a longer shimmer, because a hat's stick attack and its ring die at
very different rates.

**ACCENT opens the hat.** There is only one hi-hat here, so how hard it is
struck is what decides whether it reads as closed or open, exactly as a pedal
would — velocity multiplies the ring time, better than three to one from a
ghost tick to a full accent. Patch the ACCENT CV, or let the pattern engine's
own per-step velocities do it.

### TOM I / TOM II / TOM III — TomTomTom's three rings, all at once

The schematic is three physically separate CD4069-buffered twin-T branches, each
shocked by its own Ken Stone gate-to-trigger pulse (CGS24), captioned "change
these 3 resistors as you'd like your sound." That caption is the point: they are
the *same circuit built at three sizes*, and the size has to move more than the
pitch or you have one drum transposed three ways.

So each tom is a different drum, not a different note:

| | TUNE range | ring, longest | upper-mode damping | beater |
|---|---|---|---|---|
| **TOM I** (floor) | 42–150 Hz | 3.0 s | least — the shell holds its overtones | softest |
| **TOM II** (rack) | 80–290 Hz | 2.2 s | middling | middling |
| **TOM III** (high rack) | 150–520 Hz | 1.4 s | most — all fundamental, and gone | hardest |

The ranges overlap by about a fourth at each join — 80–150 Hz between I and II,
150–290 between II and III — so a kit can be tuned across them without a gap and
without all three landing on the same note.

This is the voice the mode bank was built for. **STRIKE** moves the stick from
dead centre — where only the circular modes are displaced and the drum is one
boomy partial — out toward the rim, where the radial modes come in and it goes
hollow and complex, hardening the stick as it goes. **BEND** is the
tension-modulation dive.

## The payroll — clock and pattern

The two columns to the left of the gutter are the module's own transport, so
Kickback plays without anything else in the rack.

**The normalling rule is the whole design.** A voice whose TRIG jack is empty is
played by the pattern engine; a voice with something patched into its TRIG is
played by that and nothing else. An unpatched Kickback runs a kit, patching one
TRIG takes that one voice over, and patching all six leaves the engine driving
only the clock and gate outputs. Nothing has to be switched to move between
those.

| control | what it does |
|---|---|
| **RUN** (lit button, latching) | Starts and stops the internal clock. Lit while running, including off an external clock. Stopping parks the grid at the top of the bar, so RUN always starts on 1. |
| **RATE** | 30–300 BPM, log-spaced. Ignored when CLK is patched. |
| **CLK** (input) | External clock. Each edge is a *beat*: Kickback measures the period and subdivides it by DIV, so a quarter-note clock still drives a sixteenth-note grid. The grid resyncs to every edge, so it cannot drift. |
| **DIV** (6 detents) | 1/4, 1/8, 1/8 triplet, 1/16, 1/16 triplet, 1/32 — how the beat is subdivided. |
| **SWING** | Pushes the odd sixteenths later, up to about 62% of a step. The even ones never move, so CLK OUT stays where a downbeat should be. |
| **FILL** | Every voice's Euclidean onset count at once, and at its bottom stop, grid mode. Monotone: turning it up may only ever add onsets. |
| **BURST** (switch) | Hands each voice's steps to its own RATIO: an onset becomes a ratchet at that multiple, or thins out at that division. Ignored at FILL 0, which is grid mode already. |
| **SEED** (16 detents) | Rotates each voice's necklace by a different amount. Same density, different beat. **Takes effect at the top of the next bar**, not under your hand — see below. |
| **HUMAN** | Velocity spread and microtiming, together. Downbeats move least, as a player's do. Works in both modes. |
| **GATE** | How long the gate outputs stay high: 5 to 100 ms. |
| **RST** (input) | Resets the grid, and every voice's grid-mode phase, to the top. |
| **ACC** (input) | 0–10 V, scaled by the ACCENT knob, raising a strike above unity. |
| **CLK** (output) | A 1 ms pulse on every grid step, so Kickback can be the clock for the rest of the rack. |
| **KICK / SNARE / HAT / TOM I / TOM II / TOM III** (outputs) | One gate per voice, down the right-hand column. Held high for GATE and **scaled by that hit's velocity**, so the accent travels with the trigger instead of on a second cable — and a ghost note still clears a 1 V trigger threshold. Six separate jacks rather than one polyphonic bus: a drum machine that needs a split module to drive six external voices is not self-contained. |

### The patterns are Euclidean

Each voice gets `E(k,16)` — Bjorklund's algorithm, which places k onsets as
evenly as possible across sixteen steps. It is the same sequence Euclid's
algorithm produces for `gcd(k,n)`, and Toussaint's observation is that E(k,n)
for small integers is most of the world's ostinati: E(3,8) is the tresillo,
E(5,8) the cinquillo, E(2,5) the khafif-e-ramal.

Euclid says how to spread k onsets; it does not say what k should be, and a kit
where every voice has the same k is a polyrhythm demo rather than a beat. So
each voice has a *role*: the FILL at which it first speaks, how much of the
remaining knob it takes to reach its fullest, and the rotation that puts its
default pattern where a player would — E(4,16) unrotated is four on the floor,
E(2,16) rotated by four is the backbeat. Turning FILL up fills a kit out in the
order a kit fills out, and all six voices speak by the middle of the knob.

The engine emits a velocity with every trigger, not just a gate. A grid of
identical hits is not a performance — the point the symbolic drum-generation
work makes from one side (Soiledis et al., *Drum Synthesis from Expressive Drum
Grids via Neural Audio Codecs*, arXiv:2605.10281, take an expressive grid to be
one *with microtiming and velocity in it*, and train on E-GMD because that is
what human playing has) and Kirby & Sandler from the other, on avoiding "the
machine gun effect."

### SEED waits for the bar line

A new seed rotates every voice at once. Taken the instant the knob moves it cuts
the figure off wherever your hand happened to be and starts a different one out
of phase with the bar, which sounds like a mistake rather than a change — so
Kickback holds the new seed and swaps patterns at the top of the next bar. The
swap happens *before* step 0 is armed, so the first thing you hear of the new
figure is its own downbeat.

FILL and HUMAN are not held back, deliberately. FILL only ever adds onsets to or
removes them from the pattern already playing, and HUMAN is a spread applied at
the moment a voice speaks; those are the two you want to hear yourself moving.
And a stopped module takes a seed at once — there is no bar to wait for, and the
next thing anyone hears is step 0 regardless.

### Grid mode: FILL at zero

Turn FILL all the way down and the pattern engine switches off. Every voice now
hits on **every step**, and each voice's **RATIO** knob multiplies or divides
that for itself — which is where the beat comes from instead.

RATIO has thirty-nine detents from **/256 to ×256**, symmetric about ×1, in
steps of 2, 3, 5 and 7:

```
/256 /192 /128 /96 /64 /48 /32 /24 /16 /14 /12 /10 /8 /7 /6 /5 /4 /3 /2
                                  x1
   x2 x3 x4 x5 x6 x7 x8 x10 x12 x14 x16 x24 x32 x48 x64 x96 x128 x192 x256
```

The odd factors are the point. Powers of two stay on the grid and never produce
anything the Euclidean engine could not; a voice on /5 against a voice on /7
takes thirty-five steps to come back round, and one on /256 is a hit every
sixteen bars. Each voice keeps its own free-running phase rather than counting
steps, so those long patterns really do drift rather than resetting each bar —
RST is what puts them back in line.

The top of the range is deliberate rather than generous: at 120 BPM on a
sixteenth-note grid, ×256 is two kilohertz. That is the point at which dividing
a clock stops being rhythm and starts being a tone, and there is no reason to go
further; the engine clamps at a quarter of the sample rate so it cannot try.

HUMAN still spreads the velocities in grid mode, so even ×1 on everything is not
a machine gun unless you ask for one.

A wire on the face runs from FILL's own zero mark down to the box around the
RATIO knobs, because a mode you can only find by turning a knob to its stop and
noticing the module behaves differently is a mode nobody finds.

### BURST: the ratios drive the pattern

Grid mode is all or nothing — you get the ratios *instead of* the patterns. The
**BURST** switch, beside the RATIO row, gives you both: the Euclidean pattern
still says *when* each voice speaks, and its RATIO says how fast it repeats
while it is speaking.

| RATIO | with BURST on |
|---|---|
| **×1** | one hit per onset — exactly what the module does with BURST off |
| **×N** | N evenly spaced hits inside that step, the first on the beat: a ratchet |
| **/N** | the voice's clock ticks once every N steps and speaks only when that tick lands on an onset the pattern has lit |

Multiplying is counted off the step's own phase, so the hits land at 0, 1/N …
(N−1)/N of the step — a ratchet that starts *on* the beat, and cannot drift or
double-count however the samples fall. Dividing keeps a phase that deliberately
spans steps, so a tick falling on a step that voice does not play is spent
rather than saved; that is what keeps a divided voice in step with the bar
instead of sliding out of it, and it is where figures far longer than sixteen
steps come from. Two voices on /5 and /7 against the same pattern do not agree
again for thirty-five bars.

This is what makes the top of the RATIO range worth reaching. On its own a fast
ratio is only good for grid mode, where the six voices become six drones; handed
a pattern to run against, ×16 on TOM III is a roll on the steps that tom already
plays. BURST is ignored at FILL 0 — there is no pattern left down there to burst
against.

## Panel

Nine columns. The two at the left are the payroll; the six in the middle are one
voice each; the one at the right is what the rest of the rack gets.

| row | voices | payroll | gate column |
|---|---|---|---|
| strike | **TRIG** input, lit when the voice fires | **RUN**, **LEVEL** | **KICK** gate |
| tune | **TUNE** / **TONE** | **RATE**, **ACCENT** | **SNARE** gate |
| colour | the voice's character, or its model selector | **CLK** in, **RST** in | **HAT** gate |
| decay | **DECAY** | **DIV**, **FILL** | **TOM I** gate |
| bend | **BEND** | **SWING**, **SEED** | **TOM II** gate |
| ratio | **RATIO** | **HUMAN**, **GATE** | **TOM III** gate |
| footer | **OUT** | **CLK** out, **MIX** out | **ACC** in |

Two things about that order are load-bearing rather than taste. RATIO sits at
the bottom because it is a rhythm control rather than a sound one, and putting
it there leaves the row carrying the CLK and RST jacks as the one already made
tall by a three-position toggle — a jack in a trimpot row makes the row taller
*and* gives it a second line of labels, which is about four millimetres this
panel has not got. The gate column's labels stand *beside* its jacks rather than over them, which
is what makes six individual outputs fit where a polyphonic bus used to be:
three of those six sit in rows whose height is set by a trimpot, and a label
above or below in such a row is a whole extra line of text on it. Alongside, a
label costs the row nothing and is paid for once, in width.

The three rows under the knobs are trimpots. Nine columns of five controls do
not fit on a 3U face at knob pitch, and the honest place to spend the
millimetres is the one control per column you reach for while playing — which is
TUNE.

KICK's **MODEL** and SNARE's **MODE** are real toggles, because a control whose
whole job is to say which of two or three circuits is running should look like
the switch it is, and you should be able to see where it is standing.

**LEVEL** sums all six voices into MIX. The mix is trimmed so a full kit lands
within a couple of decibels of its loudest single voice at the default LEVEL —
patching MIX should not be quieter than patching the kick. **ACCENT** is how
much the ACC CV jack can raise a strike above unity; at 0 the jack does nothing,
and with nothing patched every external strike lands at full velocity.

## Voltage conventions

Triggers are ≥1 V rising edges (Schmitt hysteresis, low 0.1 V / high 1 V) —
plain gates work too, since a gate's leading edge is a rising edge. Audio
outputs are ±5 V nominal, hard-clamped to ±12 V; drum voices are peaky, so a
full-velocity strike can transiently exceed 5 V, which is what a drum does.
ACCENT CV is 0–10 V unipolar. CLK OUT is a 10 V, 1 ms pulse. The six gate
outputs are 0–10 V, scaled by that hit's velocity.

## What was left out

Kickback is monophonic throughout, and nothing on it is polyphonic. Polyphony
belongs where the channels are notes of one voice — a quantizer, a V/oct bus —
not where they are six separate drums; a gate bus that needs a split module to
be usable would make the module depend on one to do its own job.

There is no V/OCT jack. Every voice still takes 1 V/oct internally, but that row
became RATIO: Kickback is a rhythm instrument, and the pitched voice that wanted
a keyboard is [Toll](Toll.md).

There is no context menu — every control the circuits expose has a knob, toggle
or jack on the panel.

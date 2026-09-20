# Dependents — FORM 8812

**A chord claimed as the harmonics of a note you cannot hear.**

Part of the [Moon Technologies](../README.md) plugin.

## Credit where it is due

This module exists because of **[Astrobear Music](https://www.youtube.com/watch?v=O0QLnR406pQ)**
— *"This distortion plays chords using Chebyshev harmonic exciters"* — and the
per-harmonic shaping in their **[Black Diamond Distortion](https://aspeninstruments.com/plugins/black-diamond-distortion/)**
(Aspen Instruments, free). Their video is the demonstration that makes the idea
obvious: a synth playing one inaudible sub-octave, and every note of the chord
progression coming out of the *distortion*. Turn the distortion off and the
chords disappear.

In their own words:

> These chords are generated purely by distorting a single sine wave.

The mathematics under it is public and decades old. This module is that
mathematics built as a Rack voice; it is not a copy of their plugin, which is a
general transfer-function editor and does a great deal more than this. If you
want the tool that demonstration was made with, download theirs.

## How it works

Three facts, in the order they matter.

**1. Chebyshev polynomials of the first kind satisfy Tₙ(cos x) = cos(nx).** So a
unit-amplitude sine through the nth Chebyshev polynomial comes out as exactly
the nth harmonic. Not mostly — exactly. Measured with a DFT, T₄ on a sine puts
1.000 at the fourth harmonic and nothing above the noise floor anywhere else.

**2. A waveshaper is therefore a harmonic recipe.** Sum aₙ·Tₙ and the output is
the harmonic series with the amplitudes you chose. A distortion curve breaks
down into a Chebyshev series the way a waveform breaks down into sines.

**3. Harmonic numbers in small whole ratios are chords.** 4:5:6 is a just major
triad, 10:12:15 a minor, 4:5:6:7 a dominant seventh with the flat 7:4 that no
keyboard has. Put the root two octaves below hearing, excite 4, 5 and 6, and
what you hear is a major triad with no fundamental under it.

### The catch, which is the whole design

Fact 1 holds **only at unit amplitude**. At half amplitude the same T₄+T₅+T₆
measures as harmonics 1, 2, 3, 4 and 5 at wrong weights — the chord folds down
into a muddle. That is why the demonstration's sine is at 0 dB.

**HOLD** is the switch for it, and it only ever has something to hold when
something is patched into **IN**. On, a peak-hold normaliser keeps whatever is
coming in at unity so the chord stays in tune with itself: a signal at 12 % of
full scale comes out as a clean triad, 0.006 of stray against 0.334 of member.
Off, the chord dissolves as the input quietens, which is an instrument in its
own right — play CHORDS (see below) or an envelope into DRIVE's CV and the
harmony comes apart as the level falls.

With **IN unpatched** — the module playing its own root, which is the usual
way to run it — HOLD has nothing to do: that path sums the chord straight from
the oscillator's phase rather than driving a waveshaper (see "Two paths"
below), and the identity holds there at any amplitude already. HOLD's position
makes no audible difference until something is patched into IN.

The normaliser catches peaks instantly and releases over a second, which is the
opposite of the usual instinct. Smoothing the attack makes it *worse*: with the
members of a major triad at 0.33, stray energy goes 0.11 at a 2 ms attack, 0.23
at 10 ms, 0.41 at 120 ms, because a follower that lags never reaches the true
peak and everything under unity collapses the chord. Accuracy of the peak beats
smoothness of the gain here by a wide margin.

## Two paths, and why

**Nothing patched into IN** and the module plays its own root. Here it does not
waveshape at all: since Tₙ(cos p) *is* cos(np) and the phase is already known,
the chord is summed straight from that phase — exact, and with any member above
Nyquist simply left out rather than folded back as an alias. No amount of
oversampling beats not making the alias in the first place.

**Something patched into IN** and it is a real waveshaper, run at 4× and
filtered back down, because the 20th harmonic of anything much above a few
hundred hertz is past Nyquist.

That second path is where the fun is. **The clean chord only happens with a
sine.** Feed it anything else — a saw, a voice, a drum loop — and every partial
already in the signal gets its own set of Chebyshev harmonics, which is chaos.
That is a feature and it is not defended against.

## Controls

| Control | What it does |
|---|---|
| **ROOT** | The note nobody hears. Default is two octaves under the chord; V/OCT adds to it. Reads out in Hz, or as a note name when QUANT is on. |
| **QUANT** | Snaps ROOT (+ V/OCT) to the scale SCALE picks, rooted at C, instead of sweeping it continuously. |
| **SCALE** | Which of five scales QUANT snaps to — chromatic, major, minor, and their pentatonics. Only read while QUANT is on. |
| **CHORD A / CHORD B** | The two recipes, from the table below. Each has its own CV, a volt a step through the table. |
| **MORPH** | A to B. Not a switch — the *weights* interpolate, so major to minor is the third fading down while the flat third comes up, and every position between is a real spectrum with no name. |
| **TILT** | Leans the voicing toward its lowest member. 0 is even, which on a five-note chord sounds like an organ stop; up around a third is where it sounds played. |
| **CHORDS** (was DRIVE) | How hard the shaper is driven. Renamed because what a player hears is the chord fading in over the root, not a distortion character, even though the circuit really is driving a waveshaper harder. With HOLD off this is the harmony control. |
| **MIX** | Dry to wet. With IN unpatched, dry is the root, so at low mix you get the chord over its own fundamental; with IN patched, dry is that external signal instead. |
| **LEVEL** | Output, with its own CV — a CV on LEVEL is an amplitude control with its own attenuverter, which is what an internal VCA is. Patching a CV here is what removes the need for a VCA after this module in a chain. Default 100 %, up from an earlier, quieter default. |
| **HOLD** | Normalise the input to unity, or don't. Only audible with something patched into IN — see above. |

### The chords

| | ratios | |
|---|---|---|
| OCTAVE | 2:4 | |
| FIFTH | 2:3 | |
| SUS4 | 6:8:9 | |
| MAJOR | 4:5:6 | the third is 5:4, fourteen cents flatter than a piano's |
| MINOR | 10:12:15 | |
| DIM | 5:6:7 | |
| MAJ 6 | 12:15:18:20 | |
| DOM 7 | 4:5:6:7 | the 7:4 seventh, flat and sweet |
| MIN 7 | 10:12:15:18 | |
| MAJ 7 | 8:10:12:15 | |
| ADD 9 | 4:5:6:9 | |
| DOM 9 | 4:5:6:7:9 | |
| STACK | 1:2:3:4:5 | not a chord — the plain harmonic series, for using it as an exciter |
| CUSTOM | -- | reads ITEMIZED HARMONICS' twelve trims instead of a fixed ratio |

These are just ratios, not equal temperament, because that is what falls out of
a harmonic series. It is the sound of the thing, not a compromise in it.

## Itemized harmonics

Twelve trims, one per harmonic (1st through 12th), read whenever CHORD A or
CHORD B is set to CUSTOM. The presets above are shorthand for particular
settings of these twelve; morph a named triad against your own spectrum (put
CHORD B on CUSTOM and turn MORPH) and the difference is audible as a chord
becoming something that has no name. They are raw weights rather than a scale
degree each, so there is no separate quantize switch for them the way there is
for ROOT — a "quantized" itemized spectrum would mean rounding a harmonic's
*number* to the nearest scale member, which is a different and stranger
control than anything else on this panel, and isn't implemented.

## Jacks

| Jack | Notes |
|---|---|
| **IN** | audio. Unpatched, the internal root is used and the alias-free path runs |
| **V/OCT** | 1 V/oct on the root, before QUANT |
| **MORPH** | CV on the morph, through its own attenuverter |
| **TILT** | CV on the voicing tilt |
| **CHORD A / CHORD B** | CV on each chord selector, a volt a step, through its own attenuverter |
| **CHORDS** | CV on drive, through its own attenuverter |
| **LEVEL** | CV on output level, through its own attenuverter — the internal VCA |
| **ROOT** (out) | the root on its own, in case you want to hear what you are not hearing |
| **OUT** | the chord |

## Patch ideas

- **ROOT at its lowest, MIX full.** A chord from nothing, exact at any level
  since nothing is patched into IN — HOLD makes no difference here either way.
- **QUANT on, SCALE set, V/OCT sequenced.** The chord's root snaps to the
  scale note by note instead of sliding continuously between them.
- **MORPH from a slow LFO.** Major breathing into minor, through spectra that
  are neither.
- **Something into IN, HOLD off, CHORDS from an envelope.** The chord
  assembles as the note gets loud and falls apart as it decays.
- **LEVEL from an envelope, nothing after this module.** The CV on LEVEL is
  doing a VCA's job.
- **A drum loop into IN.** Not a chord. Worth doing anyway.

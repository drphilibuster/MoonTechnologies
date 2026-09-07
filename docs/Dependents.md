# Dependents — FORM 8812

**A chord claimed as the harmonics of a note you cannot hear.** 16 HP.

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

**HOLD** is the switch for it. On, a peak-hold normaliser keeps whatever is
coming in at unity so the chord stays in tune with itself: a signal at 12 % of
full scale comes out as a clean triad, 0.006 of stray against 0.334 of member.
Off, the chord dissolves as the input quietens, which is an instrument in its
own right — play the drive and the harmony comes apart.

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
| **ROOT** | The note nobody hears. Default is two octaves under the chord; V/OCT adds to it. |
| **CHORD A / CHORD B** | The two recipes, from the table below. |
| **MORPH** | A to B. Not a switch — the *weights* interpolate, so major to minor is the third fading down while the flat third comes up, and every position between is a real spectrum with no name. |
| **TILT** | Leans the voicing toward its lowest member. 0 is even, which on a five-note chord sounds like an organ stop; up around a third is where it sounds played. |
| **DRIVE** | How hard the shaper is driven. With HOLD off this is the harmony control. |
| **MIX** | Dry to wet. Dry is the root, so at low mix you get the chord over its own fundamental. |
| **LEVEL** | Output. |
| **HOLD** | Normalise the input to unity, or don't. See above. |

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

These are just ratios, not equal temperament, because that is what falls out of
a harmonic series. It is the sound of the thing, not a compromise in it.

## Jacks

| Jack | Notes |
|---|---|
| **IN** | audio. Unpatched, the internal root is used and the alias-free path runs |
| **V/OCT** | 1 V/oct on the root |
| **MORPH** | CV on the morph, through its own attenuverter |
| **TILT** | CV on the voicing tilt |
| **ROOT** (out) | the root on its own, in case you want to hear what you are not hearing |
| **OUT** | the chord |

## Patch ideas

- **ROOT at its lowest, MIX full, HOLD on.** A chord from nothing. Sequence
  V/OCT and the whole harmony transposes with it, in tune with itself always,
  because it is one series.
- **MORPH from a slow LFO.** Major breathing into minor, through spectra that
  are neither.
- **HOLD off, DRIVE from an envelope.** The chord assembles as the note gets
  loud and falls apart as it decays.
- **A drum loop into IN.** Not a chord. Worth doing anyway.

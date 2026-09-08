# Projection

A video generator for VCV Rack 2, driven by audio and CV. FORM 1120-W is the
estimated tax worksheet — a projection — and it is also the other kind.

It makes a picture out of what the rack is already doing and publishes it to the
plugin's video bus, where **[Transmittal](Transmittal.md)** picks it up exactly
as it picks up Repossession. Nothing new is needed downstream: a Syphon Spout In
TOP in TouchDesigner sees this the same way it sees a seized video.

It is also an analyser in its own right. Three band envelopes and an onset
trigger leave on the footer, and those are worth patching whether or not
anything is looking at the video.

Part of the [Moon Technologies](../README.md) plugin.

## The three pictures

**MODE** picks one. It has a CV jack, so a sequencer can cut between them.

| Mode | What it draws |
|---|---|
| **SCOPE** | An XY plot of L against R, consecutive samples joined so a sparse signal reads as a line rather than as dots. A signal past the edge of the frame piles up on the edge the way an oscilloscope's does — it does not wrap round. |
| **BARS** | The spectrum, sixteen log-spaced bands growing from the bottom. Hue walks across the bands, so *which* band is loud is readable as colour and not only as height. |
| **FIELD** | A per-pixel field warped by the band energies — low, middle and top each bending it differently. Computed on a 4×4 lattice, because the honest per-pixel version at this size is most of a million transcendental functions a frame and this shares a machine with an audio engine. |

## Controls

| Control | What it does |
|---|---|
| **L**, **R** (inputs) | What it listens to. R is normalled to L, so a mono source still works — the scope then draws a diagonal, which is correct and is what a mono signal looks like on an oscilloscope. |
| **SENS** | Input gain into the analysis, not a threshold. The spectrum is the instrument here and it wants to be driven. Also sets the onset detector's threshold, inversely. |
| **TILT** | Leans the weighting up the spectrum. Music is roughly pink — power falls about 3 dB per octave — so at 0 the bass end of the picture is permanently lit and the top never moves. Centre is the weighting that flattens pink; full clockwise over-corrects and favours the top. |
| **MODE** | Scope / Bars / Field, with CV. |
| **SCALE** | Zoom in SCOPE, bar height in BARS, spatial frequency in FIELD. With CV. |
| **WARP** | Hue spread across the bands in BARS, and how hard the bands bend the field in FIELD. With CV. |
| **HUE** | Where on the wheel the picture sits. With CV, so the colour can be sequenced. |
| **TRAIL** | How long the picture keeps what it drew. In **seconds, not frames** — the same setting looks the same at any output rate, which is what that control ought to mean. |
| **SAT** | Saturation, down to monochrome. |
| **FLASH** (input) | A gate adds light over everything, decaying over about 80 ms. |
| **FREEZE** (input) | Holds the last frame for as long as it is high. Nothing is redrawn and nothing is published, so what a compositor is showing simply stops. |

## What leaves

| Output | |
|---|---|
| **LOW**, **MID**, **HIGH** | 0–10 V envelopes for the bottom, middle and top thirds of the spectrum. Each is the mean of its third rather than a single band: one band is narrow enough that a note falling between two reads as silence. Fast up, slow down — about 180 ms of release, the ballistics a meter wants and for the same reason. |
| **ONSET** | A 1 ms trigger when total energy rises sharply above its own running average. Deliberately not a spectral-flux detector with a median filter: this is for firing a flash on a picture, where being early and occasionally wrong beats being late and correct. SENS sets how easily it fires. |

The video itself does not leave through a jack. It goes on the video bus, and
**Transmittal** publishes it — see that manual for how to receive it.

## Where the work happens

Three threads, and which does what is the whole design:

- The **audio thread** fills a ring buffer and reads the band envelopes back out
  as CV. Nothing else. A 2048-point transform is not going to happen inside a
  twenty-microsecond deadline.
- A **worker** takes a window when one is ready, transforms it, follows the
  bands, draws the frame and publishes it. Everything expensive.
- The **UI thread** copies the last frame into the panel's preview. Frames are
  rendered at 640 × 360 whether or not the panel is on screen, so scrolling the
  rack does not interrupt what a projector is showing.

The band envelopes are written by the worker and read by the audio thread
without a lock. That is a race in the strict sense and a deliberate one: they
are floats being read for a control voltage, a torn read lands somewhere between
the old value and the new, and paying a lock every sample to avoid an error
smaller than the CV's own quantisation would be the wrong trade.

## Approximated or left out

- **Sixteen bands, log-spaced from 40 Hz to 12 kHz.** Linear spacing would put
  three quarters of the resolution above 5 kHz, where almost nothing in a mix
  lives, and the picture would end up driven by cymbals and nothing else.
- **No feedback or frame-buffer effects.** TRAIL is a decay of what is already
  drawn, not a warped resample of the previous frame; the latter is a different
  and much heavier module.
- **The field is a lattice, not per pixel.** At 4 × 4 the field is smooth enough
  that the difference does not show, and it is sixteen times less arithmetic.
- **No text, no sprites, no geometry beyond the three modes.** This makes
  signal-derived pictures. Anything compositional belongs downstream, which is
  what TouchDesigner is for and why the frames go there.

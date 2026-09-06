# Schedule A

**FORM 1099-A, itemized attachment — the Repossession expander.** 16 HP.

Eight rows, one per seized asset. Each carries the four charges that can be
varied against that step — **SPEED**, **GAIN**, **START**, **LENGTH** — and the
audio that step produces.

Repossession already carries all of this polyphonically, on four jacks with
channel N addressing step N. That costs no panel space and patches badly. This
is the same eight steps with somewhere to plug a cable into.

---

## Attaching it

Put it **immediately to Repossession's right**. Nothing else is needed: there is
no setting, no menu and no saved state. If it is not next to a Repossession its
lights are dark and its outputs are silent.

The rows are step 1 at the top to step 8 at the bottom. They are not numbered,
because they do not need to be: each row's light wears the same colour that
step's button wears on the host, and the same colour its span is drawn in on the
timeline. Look at the host to see which row is which.

A row's light is bright while that step is playing, half-lit when it is the
selected one, dim when it merely holds a region, dark when the slot is empty,
and clay when the step is disabled — exactly what the host's own button does,
because the host sends the colour rather than the expander guessing it.

---

## The inputs

| column | expects | what it does to that step |
|---|---|---|
| **SPEED** | ±5 V | 0.4 octaves per volt, added to the region's own speed. The sum is clamped to ±3 octaves |
| **GAIN** | ±5 V | scales the region's gain: +5 V doubles it, −5 V silences it |
| **START** | ±5 V | shifts the window's start by up to a tenth of the clip either way, keeping its length |
| **LENGTH** | ±5 V | scales the window's length from its start: +5 V doubles, −5 V halves |

These are the same laws the host's polyphonic jacks use, because they are the
same code — the expander only decides *which* value a step sees.

### Patching both at once

A jack here takes priority over the host's polyphonic one **only where a cable
is actually in it**. So a poly LFO into the host's SPEED can drive all eight
steps while one hand-patched envelope here takes over step 5 and nothing else.

That is why an empty jack overrides nothing, and why the two cannot simply be
summed: summing would make every unpatched jack a silent zero, and the expander
would clamp the host to nothing the moment it was attached.

**START and LENGTH never write to the stored region.** A modulated window
springs back the moment the cable is pulled, and what the patch saves is what
you drew on the timeline.

Windows are still bounded by the host's memory budget. CV that would push a
window past what a step has been granted is clamped, exactly as dragging it
would be — see the budget section of the [Repossession manual](Repossession.md).

---

## The outputs

**OUT** is that step's audio, summed to mono, silent unless that step is the one
sounding. One slot plays at a time, so the eight outputs are the stereo pair
routed by which step made it — which is what lets a step have a chain of its own
without a mixer having to guess where the sound came from.

The host's own **STEPS** jack carries the same eight signals polyphonically, so
you can use either, or both.

---

## What is going on inside

* **The whole module is a frame of message passing.** Rack gives each side of a
  seam two buffers and flips them once a frame, so neither module ever reads a
  buffer the other is writing.
* **Nothing is cached across frames.** An expander can be deleted between one
  frame and the next, so the host checks for it every frame by model pointer,
  and the expander does the same in return.
* **Nothing here allocates or has state.** There is no `dataToJson`, because
  there is nothing to save: the patch cables are the whole configuration.
* **A lone expander is silent, not stale.** The host sets a flag each frame it
  writes; without it the outputs go to zero and the lights go dark rather than
  holding whatever they last saw.

## Credit

The idea of an expander that itemises what the main module can only address in
bulk is ordinary Eurorack practice, and the debt is to the idiom rather than to
any one module.

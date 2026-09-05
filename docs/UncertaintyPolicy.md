# Uncertainty Policy

A signal-aware knob and cable randomizer for VCV Rack 2.

What The Jack picks a random output and a random input and joins them. It has no
idea what any signal *is* or where the audio goes, so a good half of its rolls
kill the sound and you spend the session going roll → silence → ctrl+Z → roll.

Uncertainty Policy files the same kind of speculative return, but it reads the
patch first and withdraws the filings that don't survive review.

Part of the [Moon Technologies](../README.md) plugin.

## What it does differently

**It listens before it patches.** Every output port in the patch is sampled at
sample rate for 200 ms and classified from the voltage itself — range, RMS,
zero-crossing rate and the share of time spent above +1 V separate audio, pitch
CV, slow modulation, gates and triggers without trusting anybody's port labels.
Names are consulted only where a voltage window genuinely cannot decide (telling
1V/oct from any other slow CV). Sources are then matched to inputs by affinity,
so audio never lands on a V/Oct input and a trigger never lands on a filter's
audio in.

**It knows what your modules are for.** Rack's tag vocabulary is a taxonomy of
modular synthesis, and the module reads it: oscillators, samplers and noise are
sources; filters, VCAs, low-pass gates and effects are processors; clocks,
sequencers and logic are the timing spine; envelopes and function generators
articulate; LFOs, random and sample-and-hold modulate. Tag IDs are explicitly not
part of Rack's ABI, so an ID is never compared against a literal — it is resolved
to its canonical name at runtime and matched on that. A module that declares no
tags falls back to its own name and description, which is weaker; the right-click
menu will tell you how much of your patch is in which case.

**Transfers are chosen by class, not by census.** A cable edit belongs to exactly
one of four classes — signal path, modulation, pitch, timing — and the class is
drawn first, from your appetite for each, before a candidate within it is picked.
This matters more than it sounds. Scoring every (source × input) pair in one pool
hands the roll to whichever class has the most ports, and in any real patch that
is modulation by a wide margin: nearly every module has a fistful of CV inputs.
That is why an unweighted randomizer drifts every patch toward the same slow
wobble. Choosing the class first makes the setting mean what it says whatever the
shape of the patch.

**It can be pointed at just the front panel.** A patch downloaded from
Patchstorage is somebody's instrument: forty modules with a control surface on
top, whether that is PatchMaster, a CV mapper, or a set of labels the creator
stuck on with Stoermelder Glue. Randomising all forty is not the same experience
as randomising the twelve controls they decided you should touch.

*Modules a filing may touch* reads the chosen list in either direction — as
exemptions, as the whole permitted patch, or not at all. Two of the selections
are one click, because they are the ones that matter:

- **Select the control surfaces** — every module Rack's own tags call a
  `Controller`: PatchMaster, CV-Map, a bank of manual knobs, a fader bank.
- **Select what the creator named** — every module the patch's author put a
  label on. On a Subharmonicon build that is `Rhythm 1–4`, `SEQ-1/2`, `VCO-1/2`,
  `SUB-1/2`, `PWM 1/2`, `VCF`, `VCA` — the instrument's own front panel, in its
  own words.

Those names are also what the module picker lists, so you are choosing between
`Rhythm 1` and `SEQ-2` rather than between four identical rows of `VC Frequency
Divider MkII`. They come from the mapper's own label for a mapped control, and
from annotation modules' saved state, read by the shape of the data rather than
by knowing who wrote it.

Knobs a mapper is already driving are left alone by default. Moving one
accomplishes nothing — the mapper writes it back on the next frame — so the roll
would look like it had done nothing while quietly spending part of SPREAD doing
it. Reach the patch through its surface instead, which is the point.

**It knows what holds a patch together.** Cables carrying gates, triggers and
clocks into trigger inputs — and anything at all leaving a clock generator — are
the articulation spine, the thing that makes a patch play rather than hold. SAFE
HARBOR is how much of it is off limits, and it is a dial rather than a switch:
at 100% the spine is inviolable, at 0% it is ordinary cable. Removals are
weighted rather than uniform, so a redundant CV cable is a likelier cut than the
only thing feeding a module.

**It protects the path to the output.** The module graph is walked before every
cut, and any removal that would leave the audio device with nothing upstream of
it is refused outright.

**It reviews its own work.** After applying a change it measures the audio
device's input. If the patch went silent, pinned to clipping or latched to DC,
the whole change is rolled back and a different one is tried. You only ever
*hear* filings that survived. Ctrl+Z remains for "it works but I don't like it" —
an entire roll is one `history::ComplexAction`, so it undoes in a single step.

**And it can hear whether the patch is still moving.** A drone is loud and
unclipped, so a level test passes it on the first attempt: that is how a
randomizer quietly turns a sequenced patch into a held chord and reports success.
BASIS decides what the review is listening for, using the block-by-block shape of
the output rather than one number for its level:

| BASIS | Withdraws a filing when | For |
| --- | --- | --- |
| **GOING CONCERN** | the patch has stopped moving | keeping a patch playing |
| **NEUTRAL** | only silence or clipping | the original behaviour |
| **WIND-DOWN** | the patch is *still* playing | hunting a drone on purpose |

WIND-DOWN is the point worth dwelling on: it does not merely leave drones
reachable, it makes one the thing successive attempts converge on. Turn SAFE
HARBOR down alongside it and the module will cut the clock to get there.

Two different questions, so two different measurements. "Is this still alive"
counts brightness — a drone with an LFO on the cutoff is moving, and going
concern accepts it. "Is this a drone yet" does not — that same sound is a drone,
and a good one, so wind-down lands on it instead of hunting past it for something
completely static. See `tests/` for the signals both are calibrated against.

Judging rhythm needs longer than judging level: at 120 BPM an eighth note is
250 ms, which is the whole of the level window. So the review runs for 1.8 s —
about a bar — but **only** when a movement basis is armed, and the level and
clipping test still answers at 0.25 s and rejects a doomed trial there rather
than sitting through the rest of the window. On NEUTRAL nothing costs any more
than it used to.

**Its knob deviation is honest at the rails.** VARIANCE is a percentage of each
param's full range, applied as an offset from where the knob currently sits. The
naive form of that — `clamp(x ± U(0, d))` — quietly biases against knobs near an
endpoint: half the rolls for a knob at max clamp back onto max and change
nothing, so it moves half as often as a centred knob and piles up on the rail.
Instead the direction is drawn in proportion to the headroom on each side (at the
top rail, travel is always downward), the magnitude is drawn from `[floor, d]`
with a non-zero floor so no roll resolves to "no audible change", and only then
is it capped to that direction's headroom. Stepped params move at least one whole
step, so a switch actually flips rather than rounding back onto itself.

Params whose author set `randomizeEnabled = false` are left alone, as Rack's own
randomize does. Level, volume, gain, master and mix knobs are skipped by default,
since turning the master down is the most boring way to kill a patch.

## Panel

| Control | Does |
| --- | --- |
| **VARIANCE** | How far each knob may travel, as a share of its full range |
| **SPREAD** | How many of the eligible knobs move at all |
| **TRANSFERS** | How many cable edits per filing (0–8) |
| **BASIS** | What the review listens for: wind-down / neutral / going concern |
| **SAFE HARBOR** | How much of the patch's timing is off limits |
| **AMEND: CONTROLS / CABLES / BOTH** | File a change touching only controls, only cables, or both |
| **RESCIND** | Withdraw the last filing (and only a filing — it will not eat your own edits) |
| **OPINION** | Green when the last filing was accepted, amber when it was withdrawn |
| **TRIG** | Files a full amendment on a trigger, so the randomizer can be clocked |

CONTROLS means every control, not only the round ones. Rack's `configButton()`
clears `randomizeEnabled` on every button it creates, momentary and latching
alike, so a randomizer that takes that flag at face value cannot see a single
button in any plugin — which on a patch played through an 8×8 matrix or a grid
of sequencer steps means it cannot see the instrument at all. A **latching**
button holds state the patch saves and reloads, so it is rolled like anything
else. A **momentary** one is a press that `app::Switch` puts back on release, so
moving it would write a no-op into the filing; those are left alone. The two are
told apart by the widget, which is the only thing that knows.

Set *Flip latching buttons* to 0% for a knobs-only roll.

VARIANCE and SPREAD are different questions and it is worth having both: "nudge
everything slightly" and "yank three things hard" are unrelated gestures, and
only the first is reachable with a depth control alone. How far each knob travels
is also scaled by what it does — a coarse tune moves least, because taking a
voice out of the patch's key is the commonest way a randomizer ruins something
that was working, while timbre controls run free. Discrete selectors (a waveform,
a filter type, an algorithm) are held out of the roll most of the time, since
flipping one dwarfs any amount of knob travel.

The glass reports the verdict, the evidence it rested on, and the mandate the
next filing will be made under.

Right-click for the five filing postures — conservative, standard, aggressive,
wind-down, total reconstruction, which set everything at once — and then, if you
want them, the individual settings behind them: where transfers land, how far to
follow each module's declared function, how often to flip mode switches, how
freely to patch unlabelled ports, the movement thresholds, the materiality
threshold, audition on/off, output-path protection, circular references, whether
level knobs are fair game, bypassed modules, and mapper-driven knobs. "What
this patch looks like" reports the role the module inferred for everything in
your rack.

*Total reconstruction* reproduces the behaviour of the version before all of
this, deliberately: nothing has been taken away.

## Build

Uncertainty Policy ships inside the Moon Technologies plugin, so it is built
with the rest of the family — see [BUILDING.md](BUILDING.md). Its own unit tests
run standalone:

```bash
cd tests/UncertaintyPolicy && make
```

## The panel

The panel is generated. Edit `tools/panels/UncertaintyPolicy.py` — the spec — never the
SVG, and never `src/PanelTheme.hpp` or `src/UncertaintyPolicy/Panel.hpp`, all of which are
written from it along with both previews:

```bash
make panel-UncertaintyPolicy      # artwork, the two headers, the browser mock
make preview-UncertaintyPolicy    # ... and open the mock
make vcv-preview             # ... build, then render every panel through VCV Rack
```

`make panel` does the same for all three panels at once, which is what you want
after a change to `panelkit/`.

The palette, the shared hardware, the layout rules and the two constraints Rack's
renderer imposes are documented once, in [`../panelkit/README.md`](../panelkit/README.md),
which is also where the other two panels in the plugin get theirs. There is no
per-module copy of any of it.


## Known limits

Port classification is only as good as a 200 ms window: a source that is silent
at probe time reads as silent and is skipped.

There is no way to ask Rack which module owns a given mapping, only whether a
param is mapped at all — so "scope to the control surface" is a tag match plus
your judgement, not something the module can infer with certainty.

Role knowledge is only as good as what plugin authors declared. Plenty of modules
ship no function tags, and the fallback to name-and-description is a guess; a
patch built entirely from untagged modules behaves closer to the old build. The
same goes for ports — `configInput()` is optional, and an unnamed port tells the
matcher nothing, which is what "patch unlabelled ports" in the menu is for.

A movement basis makes a filing take up to a few seconds. TRIG ignores triggers
that arrive while a filing is in progress, so **do not clock the module on GOING
CONCERN or WIND-DOWN** — the readout shows UNDER REVIEW while it is busy. NEUTRAL
is the one to clock. The module holds raw `Module*`
pointers across the probe window, which is safe because Rack takes the engine
write lock to add or remove a module and `process()` runs under that lock, but it
is an assumption rather than a guarantee the API makes explicit.

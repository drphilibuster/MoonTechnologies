# Changelog

Versions follow the VCV convention: the major number is the Rack major version
these modules run on, so a Rack 2 plugin is always `2.x.y`.

## Unreleased

### Fixed: Consolidation crashed Rack the moment it was added

`configBypass` was called once per mixer channel with the same `OUT` as the
destination. Rack allows each output to be bypass-routed exactly once and
asserts on the second (`Rack-SDK/include/engine/Module.hpp:240`), so adding the
module aborted the process — every time, on every platform. The mix output now
carries channel 1 through on bypass, and the two multiples fan their input out
to their own legs, which the one-route-per-output rule permits.

### Fixed: Gross output a constant 12 V from the moment it was created

An unpatched Gross sat at 12 V on both audio outputs with ENV pinned at 10 V,
and poisoned anything downstream of it.

Rack default-constructs a `BiquadFilter` by calling
`setParameters(LOWPASS, f=0, Q=0, V=1)`, and that branch computes
`1/(1 + K/Q + K*K)` — with `K = tan(0) = 0` and `Q = 0` that is `0/0`, so every
coefficient of a freshly constructed biquad is NaN
(`Rack-SDK/include/dsp/filter.hpp:307,335`). Gross designs its six filters in
`updateControls()`, which runs behind a `ClockDivider` of 8 and so does not fire
until the eighth sample. The seven samples before it were enough: NaN entered
the filters' state history and never left, and `clamp()` returns its upper bound
for NaN — hence exactly 12 V, forever, whatever was patched in.

The controls are now designed once before the first sample goes through them.

### Fixed: quitting Rack with a Repossession in the patch could segfault

Rack deletes the window before the scene (`Rack/src/context.cpp:19` and `:27`),
so `VideoScreen`'s destructor ran with an already-freed `NVGcontext` and called
`nvgDeleteImage` on it. The image is now released only while the window that
owns the context is still alive.

### Fixed: Consolidation started silent

The four channel levels defaulted to 0, so a fully patched mixer made no sound
and looked broken. Rack's convention is the opposite — every Fundamental level
defaults to unity — and it also matches the topology being modelled: 10k in
against 10k feedback is unity gain per channel. They now default to 100%.

### Faster: the per-sample audio paths no longer recompute what has not changed

Filter and envelope coefficients are expensive functions — `exp`, `tan`, `pow`,
`cos` — of things that barely move: a knob, or the sample rate. Written inline
in `process()` that cost was paid on every sample. `src/DspCache.hpp` adds a
one-float staleness check, so a transcendental call becomes a float compare
until its input actually moves, and an exact recompute the moment it does — a
knob sweep sounds precisely as it did before.

Kickback, which ran eight voices through several of these each sample, is the
clearest case: **0.875% of one core down to 0.477% at 96 kHz, 1.83x faster**,
with its output identical to four significant figures. SixFigures was
evaluating three `exp()` per voice per sample — eighteen per sample — for two
values that depend only on the sample rate. Racketeer had five `pow()` per
sample keyed on knobs; Deduction a `tan()` per polyphonic channel; Dividend and
Installment used `pow(2, x)` where `exp2` does. Gross, Diversified and
Amortization already updated at control rate behind a divider and are unchanged.

### New module: Schedule A, the Repossession expander

16 HP, eight rows, one per seized asset: discrete **SPEED / GAIN / START /
LENGTH** inputs and a per-step audio **OUT**, for the four controls Repossession
otherwise carries polyphonically. Attaches to Repossession's right.

- **A jack here wins over the host's poly jack only where a cable is in it.** A
  poly LFO can drive all eight steps while one hand-patched envelope takes over
  step 5 and nothing else. Summing them would make every unpatched jack a silent
  zero, which is why the message carries a `has` flag per step and not just a
  voltage.
- **The rows are named by colour, not by number.** Each row's light is sent the
  colour the host's own step button is wearing, disabled steps included, so the
  expander needs to know neither the palette nor the state.
- **No state, no menu, no `dataToJson`.** The patch cables are the whole
  configuration. A lone expander goes dark and silent rather than holding what
  it last saw.

### panelkit: a panel with no footer band now fills its face

`slack` — the room the justify pass shares out among the gaps — was only ever
computed inside the `if panel.footer:` branch. A panel without a footer got
`slack = 0`, never justified, and packed its rows against the masthead with the
whole lower face left empty. Schedule A's eight rows of jacks stopped
three-quarters of the way down the panel.

Footerless panels now measure their slack against the foot ribbon between the
bottom screws, like every other panel measures it against its band. Only two
panels in the family have no footer; PatchAudit had no slack to share, so its
artwork is byte-identical, and the other nineteen never entered this branch.

### Repossession: only the windows are in memory

The module held the whole decoded clip in RAM — 230 MB for ten minutes of stereo
float at 48 kHz, which is why the import length was a menu item rather than a
number. Almost none of it was ever played. The module plays eight windows cut out
of the clip; everything between them was memory spent on audio nobody asked for.

The decode still lands on disk as a `.pcm`. Only the windows are read into RAM,
and what is kept for the whole clip is its length, its rate and a
thousand-bucket waveform — about four kilobytes, whatever the source.

- **`Windows.hpp`.** A worker reads windows off the `.pcm` and hands each to the
  audio thread as an atomic pointer, the same handover `Media.hpp` uses. Newest
  request per slot wins, because dragging an edge queues one a frame and only
  the last matters. A step whose window has not arrived is silent rather than
  stalling.
- **`Media` no longer holds audio.** `scanPcm` reads the file once, forwards, in
  a fixed half-megabyte buffer, folding every frame into its peak bucket.
- **A budget, and a meter that reads it off the buffers.** 16 MB to 512 MB,
  default 64. The meter shows one segment per step in that step's own colour, so
  the bar says who is holding what.
- **The eight steps share the budget.** Trimming a step returns seconds to a
  common pool; growing one takes from it and simply stops when it is empty, the
  placed start staying put while the length gives. Disabling hands a whole share
  back. Re-arming takes up to an even share *of what is left* — and when nothing
  is left the step stays disabled and blinks. That refusal is the mechanic.
- **"Eight equal spans" follows the budget, not the clip.** Eight windows of the
  largest length the budget allows, spread evenly end to end. The old behaviour
  made window length a function of clip length, so a ten-minute video produced
  eight seventy-five-second regions whether or not there was memory for them.
- **The crossfade names the buffer it is fading out of.** A seam between two
  steps now reads two different buffers, so the old one is held past the longest
  fade rather than freed four samples later.
- **`tests/Repossession/test_windows.cpp`** — 63 checks under ASan/UBSan: the
  allocator's rules including the refusal, conservation of the pool across a
  20 000-operation random walk, windows read to the exact sample, reads past the
  end of file, a missing file, newest-wins under a 200-request burst, and a
  clean join with every slot still queued.

### Repossession: an instrument, not only a sequencer

The module could be clocked, and that was all it could be. Everything here is
additive — the clock input, the region editing and the whole existing patch
format still behave exactly as they did.

- **An internal clock.** TEMPO, 30–300 BPM. Patching CLOCK silences it rather
  than racing it, and pulling the cable hands the knob back; the light beside
  CLOCK follows whichever clock is actually in charge.
- **RUN is a three-position switch: RUN / STOP / LATCH.** Latching one step used
  to be reachable only by accident — patch a clock, stop it, and whichever step
  you landed on looped forever. It is a position now, and the selected step is
  the one it loops.
- **The step buttons are playable.** With the transport stopped, a tap fires a
  step; holding one past 250 ms loops it for as long as it is held, overriding
  the region's own LOOP and handing it back on release. Nothing that is saved
  changes.
- **Steps can be disabled.** Ctrl-click (cmd on a Mac) parks a step: it keeps
  its span, speed and gain, and the sequencer passes over it as if it were
  empty. Distinct from releasing a slot, which throws the region away.
- **Every step has its own colour**, a lime-to-mint ramp across the eight, drawn
  from one function so a step's button, its span on the timeline and its line in
  the report cannot disagree. Disabled steps go to clay *and* are hatched, since
  colour alone is the one channel a reader may not have.
- **The timeline zooms.** Scroll about the pointer, shift-scroll to pan, with a
  bar showing where the view sits in the clip. Rack's own zoom enlarges the
  whole panel and runs out long before a boundary can be placed accurately on a
  three-minute clip. The "dragged to nothing" threshold follows the zoom, so a
  span trimmed at 100× is not deleted for being small on screen.
- **Four per-step CV inputs — SPEED, GAIN, START, LENGTH.** Polyphonic, channel
  N addressing slot N, so one cable carries all eight; a monophonic cable
  applies to every step at once. START and LENGTH never write to the stored
  region, so a modulated window springs back when the cable is pulled.
- **A polyphonic STEPS output**, whichever step is sounding on its own channel.
- **`panelkit` gains `RgbLight`**, a light whose colour the module picks rather
  than the panel — the general capability the per-step colours needed, so it
  lives in the kit and every panel has it.

### Repossession: a tool that cannot start says so

`yt-dlp failed (exit 127)` was, in at least one real case, a lie. The launcher
forked, `execvp()` failed, and the child called `_exit(127)` — which is exactly
what a shell reports for a command it could not find, so the parent had no way
to tell "the tool ran and exited 127" from "the tool never ran at all". The
panel then quoted an exit code the tool had never chosen, next to the path it
had just successfully found on disk.

The case that produced it: a `yt-dlp` on `PATH` that was a `pip` console script
whose `#!` line named a Homebrew Python since removed. The file is present and
executable, so `which()` resolves it and the log prints its path; `execvp()`
then reports `ENOENT` about the *interpreter*, not the script. Being told that
path is "not found" sends you looking in the wrong place.

- **An exec-status pipe.** Its write end is `FD_CLOEXEC`, so a successful
  `execvp()` closes it and the parent reads EOF, while a failed one leaves the
  child alive just long enough to write its `errno` down it. A launch failure
  now leaves `started` false and fills `ProcessResult::execError`; the child is
  reaped rather than left as a zombie.
- **The message names the likely cause.** `ENOENT` on a file that is provably
  there and executable is reported as a broken `#!` wrapper, not as a missing
  file. `ENOEXEC` and `EACCES` get their own wording.
- **A program is still allowed to exit 127.** A tool that genuinely returns 127
  is reported as having run, unchanged — the distinction is the point.
- **`tests/Repossession/`.** The launcher tested against a host compiler through
  a five-call Rack stub: success, separate streams, non-zero exit, a genuine
  127, a missing file, a stale `#!` shim, a file without the executable bit,
  `execvp`'s documented `/bin/sh` fallback, and no zombies after fifty failures.

### Panels: every layout re-solved

Every panel in the family had some version of the same fault, and it was one
fault: the layout solver derived every vertical coordinate and no horizontal one.
Rows were given evenly spaced *centres* — `P.cols(n, margin)`, with the margin
typed by hand — which is the wrong quantity to hold constant, because a centre
says nothing about how much of the panel a widget actually covers. One margin
had to serve a big knob and a switch alike, so the knob hung over its own block
frame while the switches floated in dead air. Dividend's FREQ knob through the
left edge of PAYOUT was the clearest case; there were nineteen others.

- **The horizontal solver.** A section's rows share a column grid, solved from
  extents rather than counts: a column is as wide as the widest thing any row
  puts in it — well, ring, primary seal, or label — measured on each side
  separately, since a lit label's light hangs off one end only. Comparable
  columns are spaced evenly as a *run*; where a row changes gear the run breaks
  and the slack collects in a gutter between the runs.
- **`hp` defaults to `"auto"`.** The panel comes out exactly as wide as its rows
  need. Uncertainty Policy 16 → 12 HP, Diversified 18 → 15, Dividend 16 → 15;
  the panels that were quietly a millimetre short of their own contents grew.
- **Paired controls read as pairs.** A trimpot over its own jack now sets the
  label they share in the gap *between* them, equidistant from each, instead of
  above the trimpot where it could be read as naming the row above. Pairing is
  per control, so a jack sharing that row without owning anything below keeps its
  name over its head, where a cable cannot cover it.
- **Stepped knobs look stepped.** A knob that is really a selector carries a
  detent for every position and an arc through its real travel, engraved into the
  dark of its own well — so it costs the layout nothing.
- **Real minimum clearances, and a linter that knows them.** Label to well, label
  to label, ink to block frame: each has a floor that holds at both densities.
  The linter now measures gaps rather than only overlaps, and counts the primary
  ring as the ink it is — the check it was missing is exactly the one that let
  FREQ through its own frame.
- **The read-out well's geometry is the spec's.** `panel::GLASS_X/Y/W/H` are
  emitted into each `Panel.hpp`; nine modules had been positioning their display
  by re-typing the same millimetres in C++.
- **`make vcv-preview` builds after regenerating, not before.** It used to build
  from whatever headers were on disk and only then re-run the specs, so a spec
  change reached the artwork and not the widget positions — a panel whose jacks
  sat beside their own wells, for as long as it took to notice.

## 2.1.0

Seventeen new modules, and a new face for all of them.

### The look

The family is now a banknote rather than a green form: the five-colour "High Contrast" palette, a pale engraved face with dark masthead and footer bands, sage guilloche ribbons, a hairline frame with corner scrolls on every section, wells ringed like seals, a double ring on the primary control, and traces engraved as waves. Nothing in any panel spec changed; the solver tags each label's ground and the emitter resolves its ink.

### Modules

- **Dividend** (16 HP, Oscillator, Synth voice) — A pulsar-synthesis VCO after Curtis Roads: trains of pulsarets whose formant is set independently of the fundamental, six pulsaret waveforms, six windows, burst, stochastic and channel masking, an overlap voice pool, and a read-out of fundamental and formant.
- **Tax Bracket** (12 HP, Utility, Mixer, Attenuator) — The Olegtron R2R as a genuine resistor network. Every jack is an in/out pair on a passive 8-bit ladder, so it is a DAC, a weighted mixer, a programmable attenuator and a labile multiple at once; checked against the manual's attenuator table.
- **Racketeer** (16 HP, Noise, Delay, Synth voice) — Wolfgang Spahn's PB701 Electric Intonarumori: a PT2399 delay run as a self-sustaining noise voice, with the chip's clock and word length falling as the delay grows, an optocoupler lag on TIME, a chopper, three enforcement buttons with gate inputs, and DIRTY, ENV and GATE outputs.
- **Gross** (18 HP, Distortion, Waveshaper, Effect) — A Wiener–Hammerstein distortion built from the Eichas–Zölzer piecewise-tanh mapping and the dynamic bias of Comunità, Steinmetz and Reiss: input EQ, drive, static and dynamic bias, knees and slopes per polarity, five curve families, output EQ, device presets, and a live transfer-curve read-out.
- **Amortization** (14 HP, Reverb, Effect) — A Verbtronic-style reverb: a Dattorro plate for VERB and an eight-line FDN with a limiter for TRONIC, tonal tilt inside the loop, feedback past unity, predelay, freeze, the mode gate, and wet-only outputs beside the mix.
- **Repossession** (34 HP, Sampler, Sequencer, Visual) — Paste a YouTube link. The module fetches it with yt-dlp and ffmpeg, shows the video on the panel, lets you drag regions on the timeline, and sequences those regions — audio and picture together — by clock, CV, scan and fire, with position, gate, end-of-region and region outputs.
- **Six Figures** (24 HP, Oscillator) — Modular in a Week, Day 1, folded into one bank: six voices, each a 40106 Schmitt square, a 4069 triangle core, a 4046 PLL that locks to the SIGNAL input, or a reverse-avalanche saw, with sync, capture, drift and a mix.
- **Garnishment** (10 HP, VCA, Low-pass gate, Dual) — Day 2: two VCA channels, each an LM13700 OTA, a vactrol low-pass gate or the I-AM-O JFET multiplier, with bias, lag and CV amount.
- **Consolidation** (12 HP, Mixer, Multiple, Utility) — Day 3: the ASMR four-channel mixer with normal and inverted sums, and two 1:3 buffered multiples with B normalled to A.
- **Installment** (16 HP, Envelope generator, LFO, Function generator, Dual) — Days 4 and 5: two function generators, each LFO, AR or AD with loop, range, bias and CV, plus the Day 12 tape-motor PWM driver with duty CV.
- **Volatility** (14 HP, Noise, Sample and hold, Random) — Day 6: an 18-bit 4006-style shift-register noise source, the YASH sample and hold, and the PHObos random gate, on one shared clock.
- **Deduction** (10 HP, Filter, Distortion) — Day 7: six filters under one MODEL knob with CV — PAiA 2720-3L, Escobedo Q&D, Korg35, MS-20 OTA, EFM Moog-type high-pass, Synthrotek DIRT — with LP and HP inputs, a CV response switch and a read-out.
- **Audit Logic** (22 HP, Logic, Switch, Clock modulator) — Day 8 and the 4066: four logic gates with selectable functions and the 0 V / 12 V reference, two gated switches, and the Emiz CV2 clock divider.
- **Kickback** (30 HP, Drum, Synth voice) — Day 9: eight drum voices with a mix — BaSnaHi kick, snare and hat, SmurfDrum, TomTomTom, XORbell, the percussive noise voice and the Tiny Dazzler.
- **Payment Schedule** (28 HP, Sequencer, Switch, Quantizer) — Day 10: the Baby8 with the 4017 sequential switch falling out of the same counter in both directions, the 4031 tap looper, and the varimode quantizer.
- **Sign Here** (20 HP, Controller, Utility) — Day 11: the button and pedal, the offset-scaler joystick as an XY pad, and four touch pads.
- **Diversified** (18 HP, Effect, Delay, Reverb, Chorus, Distortion) — Days 12 and 13: a stereo multi-effect with 106 programs — the DSP99 board's categories as twenty algorithms, then the Echomatic echo, Little Angel chorus, spring reverb, MXR Distortion+, Talk Funny, the MW bitcrusher and the 4011 ring modulator, each faithful to its schematic.

### Under the hood

- `panelkit/palette.py` derives every colour from five anchors; `palette.ink(role, ground)` is the one place a label's hex is chosen.
- The layout solver measures every clearance from a widget's well and ring,
  not its art; rows carrying lit labels make room for the light; sparse
  panels are justified to the footer. The linter now catches wells that
  overlap, wells straddling a block frame, and anything entering the foot
  ribbon. Retroactive grew to 14 HP in the process.
- `panelkit/render.py` draws the ornament as polyline paths, which is what Rack's nanosvg keeps.
- Repossession spawns `yt-dlp` and `ffmpeg` through a small portable process wrapper; nothing new is linked, so the VCV Library rules still hold.

## 2.0.0

First release of Moon Technologies as a single plugin.

PatchAudit, Retroactive and Uncertainty Policy previously existed as three
separate Rack plugins sharing a brand. They are now one plugin with three
modules, which is how the VCV Library expects a brand to ship: one entry, one
install, one version.

**If you have patches built with the old separate plugins**, the module slugs are
unchanged but the plugin slug is not, so Rack will not resolve them
automatically — it shows a placeholder where each module was and keeps the rest
of the patch intact. Re-place the module and reconnect its cables.

### Modules

- **PatchAudit** (26 HP, Utility/Visual) — browse Patchstorage from inside Rack,
  audit patches against your installed modules before opening them, and import
  straight into your rack or save to disk.
- **Retroactive** (14 HP, Effect/Delay/Granular) — windowed sample permutation
  with eight modes, clock sync, subdivision, crossfade and freeze.
- **Uncertainty Policy** (16 HP, Utility/Random) — signal-aware knob and cable
  randomizer that auditions each roll and reverts the ones that kill the sound.

### Under the hood

- Windows builds now work. PatchAudit's optional libcurl fast path looked its
  symbols up through `dlsym(RTLD_DEFAULT, …)`, which does not exist on Windows;
  it now goes through a small shim that uses `GetProcAddress` over the loaded
  modules there. Where the lookup fails on any platform the audit downloads
  large zipped uploads instead of streaming them, exactly as before.
- Panels are generated into two headers rather than one: a shared
  `src/PanelTheme.hpp` holding the vocabulary every panel draws through, and a
  per-module `src/<Module>/Panel.hpp` holding that panel's own numbers. Three
  panels can now be linked into one plugin without their silkscreen tables
  colliding.
- Prebuilt binaries for `win-x64`, `mac-arm64`, `mac-x64` and `lin-x64`, built
  by CI on every push against the official Rack SDK.

# Moon Technologies

Twenty-four modules for [VCV Rack 2](https://vcvrack.com), by **Taxxess**.

They share a panel language borrowed from money — a pale engraved note, sage
guilloche round every field, a scroll in every corner, a form number in the
masthead — because all of them are, one way or another, about filing something
and finding out what it cost you.

## Where they come from

**Eleven are built for this plugin.** Some are original; some are a piece of
hardware or a paper worked out properly — the Olegtron R2R as a real resistor
ladder, Curtis Roads' pulsar synthesis, Partch's tonality diamond. Where one is
modelled on something, it says so and says what.

**Eleven start from [Modular in a Week](https://www.youtube.com/@modularinaweek)**,
Kristian Blåsol's DIY course. Each takes a day's worth of separate builds and
puts them on one panel with their character selectable and their originals as
the defaults — so Day 2's three VCAs are one dual VCA with a MODE switch rather
than three modules. These are marked below with the day they came from.

**They are starting points, not destinations.** Every one of them will keep
growing, and some already have: [Six Figures](docs/SixFigures.md) is still a
faithful consolidation of its day, while [Kickback](docs/Kickback.md) has a
clock, a Euclidean pattern engine, per-voice clock ratios and a burst mode that
the drum folder it came from has nothing to say about. Each entry says which it
is. The credit belongs to the course either way; the deviations are ours, and
so is anything wrong with them.

**Two grew out of the family** — an expander, and a voice that turned out to
deserve its own panel.

## All twenty-four

*(Every panel below is rendered by Rack itself, not mocked up.)*

| <img src="tools/previews/PatchAudit.png" width="218"> | <img src="tools/previews/Retroactive.png" width="126"> | <img src="tools/previews/UncertaintyPolicy.png" width="92"> | <img src="tools/previews/Dividend.png" width="126"> |
|---|---|---|---|
| **[Patch Audit](docs/PatchAudit.md)** · 26 HP | **[Retroactive](docs/Retroactive.md)** · 15 HP | **[Uncertainty Policy](docs/UncertaintyPolicy.md)** · 11 HP | **[Dividend](docs/Dividend.md)** · 15 HP |

| <img src="tools/previews/TaxBracket.png" width="101"> | <img src="tools/previews/Racketeer.png" width="143"> | <img src="tools/previews/Gross.png" width="168"> | <img src="tools/previews/Amortization.png" width="126"> |
|---|---|---|---|
| **[Tax Bracket](docs/TaxBracket.md)** · 12 HP | **[Racketeer](docs/Racketeer.md)** · 18 HP | **[Gross](docs/Gross.md)** · 20 HP | **[Amortization](docs/Amortization.md)** · 15 HP |

| <img src="tools/previews/Repossession.png" width="286"> | <img src="tools/previews/Collusion.png" width="151"> | <img src="tools/previews/Reconciliation.png" width="160"> | <img src="tools/previews/SixFigures.png" width="210"> |
|---|---|---|---|
| **[Repossession](docs/Repossession.md)** · 34 HP | **[Collusion](docs/Collusion.md)** · 18 HP | **[Reconciliation](docs/Reconciliation.md)** · 19 HP | **[Six Figures](docs/SixFigures.md)** · 25 HP |

| <img src="tools/previews/Garnishment.png" width="84"> | <img src="tools/previews/Consolidation.png" width="109"> | <img src="tools/previews/Installment.png" width="134"> | <img src="tools/previews/Volatility.png" width="118"> |
|---|---|---|---|
| **[Garnishment](docs/Garnishment.md)** · 10 HP | **[Consolidation](docs/Consolidation.md)** · 13 HP | **[Installment](docs/Installment.md)** · 16 HP | **[Volatility](docs/Volatility.md)** · 14 HP |

| <img src="tools/previews/Deduction.png" width="101"> | <img src="tools/previews/AuditLogic.png" width="210"> | <img src="tools/previews/Kickback.png" width="252"> | <img src="tools/previews/PaymentSchedule.png" width="244"> |
|---|---|---|---|
| **[Deduction](docs/Deduction.md)** · 12 HP | **[Audit Logic](docs/AuditLogic.md)** · 25 HP | **[Kickback](docs/Kickback.md)** · 30 HP | **[Payment Schedule](docs/PaymentSchedule.md)** · 29 HP |

| <img src="tools/previews/SignHere.png" width="168"> | <img src="tools/previews/Diversified.png" width="118"> | <img src="tools/previews/Toll.png" width="92"> | <img src="tools/previews/ScheduleA.png" width="118"> |
|---|---|---|---|
| **[Sign Here](docs/SignHere.md)** · 20 HP | **[Diversified](docs/Diversified.md)** · 14 HP | **[Toll](docs/Toll.md)** · 11 HP | **[Schedule A](docs/ScheduleA.md)** · 14 HP |

## Built for this plugin

### [Patch Audit](docs/PatchAudit.md) — 26 HP · *Utility, Visual*

Browse [Patchstorage](https://patchstorage.com) without leaving Rack. Search the ~9,200 published VCV patches, and before you open one, get an audit of it against the modules you actually have: what is installed, what is available in the Library, and what is genuinely gone. Import straight into your rack, or save to disk.

### [Retroactive](docs/Retroactive.md) — 15 HP · *Effect, Delay, Granular*

A windowed sample-permutation effect. Take a window of consecutive samples and emit them in a different order, leaving the windows themselves in time order — so the melody, rhythm and phrasing play forward while every micro-chunk is rearranged. Short windows are a timbral effect; long ones give the familiar "reversed but still moving forward" sound. Clock-syncable, with eight permutation modes and a freeze.

### [Uncertainty Policy](docs/UncertaintyPolicy.md) — 11 HP · *Utility, Random*

A knob and cable randomizer that reads the patch before it rolls. It knows what each port carries and where the audio goes, auditions every roll, and quietly withdraws the ones that killed the sound — so you are not spending the session on roll → silence → undo → roll.

### [Dividend](docs/Dividend.md) — 15 HP · *Oscillator, Synth voice*

*After Curtis Roads' pulsar synthesis.*

Trains of pulsarets whose formant is set independently of the fundamental, six pulsaret waveforms, six windows, burst, stochastic and channel masking, an overlap voice pool, and a read-out of fundamental and formant.

### [Tax Bracket](docs/TaxBracket.md) — 12 HP · *Utility, Mixer, Attenuator*

*After the Olegtron R2R.*

The R2R as a genuine resistor network. Every jack is an in/out pair on a passive 8-bit ladder, so it is a DAC, a weighted mixer, a programmable attenuator and a labile multiple at once; checked against the manual's attenuator table.

### [Racketeer](docs/Racketeer.md) — 18 HP · *Noise, Delay, Synth voice*

*After Wolfgang Spahn's PB701 Electric Intonarumori.*

A PT2399 delay run as a self-sustaining noise voice, with the chip's clock and word length falling as the delay grows, an optocoupler lag on TIME, a chopper, three enforcement buttons with gate inputs, and DIRTY, ENV and GATE outputs.

### [Gross](docs/Gross.md) — 20 HP · *Distortion, Waveshaper, Effect*

*After Eichas–Zölzer and Comunità–Steinmetz–Reiss.*

A Wiener–Hammerstein distortion built from the piecewise-tanh mapping and a dynamic bias: input EQ, drive, static and dynamic bias, knees and slopes per polarity, five curve families, output EQ, device presets, and a live transfer-curve read-out.

### [Amortization](docs/Amortization.md) — 15 HP · *Reverb, Effect*

*After the Verbtronic.*

A Dattorro plate for VERB and an eight-line FDN with a limiter for TRONIC, tonal tilt inside the loop, feedback past unity, predelay, freeze, the mode gate, and wet-only outputs beside the mix.

### [Repossession](docs/Repossession.md) — 34 HP · *Sampler, Sequencer, Visual*

Paste a YouTube link. The module fetches it with yt-dlp and ffmpeg, shows the video on the panel, lets you drag regions on the timeline, and sequences those regions — audio and picture together — by clock, CV, scan and fire, with position, gate, end-of-region and region outputs.

### [Collusion](docs/Collusion.md) — 18 HP · *LFO, Oscillator, Random*

*After Kuramoto, and Mirollo–Strogatz.*

Six LFOs that listen to each other. Each has its own natural rate; COUPLING says how hard each is pulled toward the rest, and past a threshold set entirely by SPREAD the population stops drifting and locks — a phase transition on one knob and six lamps. Four wirings (all-to-all, ring, one-way cascade, and pulse coupling), a phase lag that buys partial order, LFO and audio ranges, and a Benjolin rungler whose bits are written from how much the swarm currently agrees, so the melody it plays *is* the phase transition.

### [Reconciliation](docs/Reconciliation.md) — 19 HP · *Quantizer, Polyphonic, Utility*

*After Partch, Wilson, Tenney, Barlow and Sethares.*

A polyphonic just-intonation quantizer that splits "which note?" into two questions and puts a knob on each. Which pitches exist: Partch's eleven-limit tonality diamond, his 43-tone scale, one Otonality or Utonality hexad, Wilson's hexany and eikosany, or the raw harmonic series — transposed onto any of Partch's six identities and pruned by prime limit. How one gets chosen: Euler's *gradus suavitatis*, Tenney's harmonic distance, Barlow's harmonicity, sensory dissonance against an assumed timbre, or adaptive tuning from the last note played, which tunes every interval pure and lets the tonal centre drift by the comma it costs. The read-out names the ratio.

## Out of Modular in a Week

Marked with the day each came from, and how far it has moved since.

### [Six Figures](docs/SixFigures.md) — 25 HP · *Oscillator*

*Day 1, consolidated.*

Six voices, each a 40106 Schmitt square, a 4069 triangle core, a 4046 PLL that locks to the SIGNAL input, or a reverse-avalanche saw, with sync, capture, drift and a mix. The four cores are the four oscillator builds of that day, selectable per voice.

### [Garnishment](docs/Garnishment.md) — 10 HP · *VCA, Low-pass gate, Dual*

*Day 2, consolidated.*

Two VCA channels, each an LM13700 OTA, a vactrol low-pass gate or the I-AM-O JFET multiplier, with bias, lag and CV amount. Three builds, one per MODE position.

### [Consolidation](docs/Consolidation.md) — 13 HP · *Mixer, Multiple, Utility*

*Day 3, consolidated.*

The ASMR four-channel mixer with normal and inverted sums, and two 1:3 buffered multiples with B normalled to A.

### [Installment](docs/Installment.md) — 16 HP · *Envelope generator, LFO, Function generator, Dual*

*Days 4, 5 and 12, consolidated.*

Two function generators, each LFO, AR or AD with loop, range, bias and CV, plus the tape-motor PWM driver with duty CV riding on channel one.

### [Volatility](docs/Volatility.md) — 14 HP · *Noise, Sample and hold, Random*

*Day 6, consolidated.*

An 18-bit 4006-style shift-register noise source, the YASH sample and hold, and the PHObos random gate — three separate builds put on one shared clock.

### [Deduction](docs/Deduction.md) — 12 HP · *Filter, Distortion*

*Day 7, consolidated.*

Six filters under one MODEL knob with CV — PAiA 2720-3L, Escobedo Q&D, Korg35, MS-20 OTA, EFM Moog-type high-pass, Synthrotek DIRT — with LP and HP inputs, a CV response switch and a read-out.

### [Audit Logic](docs/AuditLogic.md) — 25 HP · *Logic, Switch, Clock modulator*

*Day 8, plus the 4066 and the Emiz CV2.*

Four logic gates with selectable functions and the 0 V / 12 V reference, two gated analogue switches, and a clock divider. Three boards, three blocks, read top to bottom.

### [Kickback](docs/Kickback.md) — 30 HP · *Drum, Synth voice, Sequencer, Clock generator*

*Day 9 — and a long way past it.*

Started as the six drum voices of that folder and is now a drum machine. The voices are modal banks struck by a real contact pulse: the BaSnaHi kick with the SmurfDrum as its second model, a snare switching between XORbell, the percussive noise voice and Karplus–Strong, a hi-hat whose one knob sweeps metal to noise to the Tiny Dazzler, and all three TomTomTom rings at once as three differently-sized drums. **None of the rest is in the original:** its own clock, a Euclidean pattern engine, per-voice clock ratios from /256 to ×256, a BURST mode that hands each voice's steps to its own ratio, six velocity-scaled gate outputs, and a SEED that swaps patterns on the bar line. The physics is out of the literature rather than the schematic — Bilbao's contact force, Bessel-zero mode ratios, Karplus–Strong's drum recurrence.

### [Payment Schedule](docs/PaymentSchedule.md) — 29 HP · *Sequencer, Switch, Quantizer*

*Day 10, consolidated.*

The Baby8 with the 4017 sequential switch falling out of the same counter in both directions, the 4031 tap looper, and the varimode quantizer.

### [Sign Here](docs/SignHere.md) — 20 HP · *Controller, Utility*

*Day 11, consolidated.*

The button and pedal, the offset-scaler joystick as an XY pad, and four touch pads. Three gesture controllers that share nothing electrically in the original.

### [Diversified](docs/Diversified.md) — 14 HP · *Effect, Delay, Reverb, Chorus, Distortion*

*Days 12 and 13, consolidated.*

A stereo multi-effect with 106 programs — the DSP99 board's categories as twenty algorithms, then the Echomatic echo, Little Angel chorus, spring reverb, MXR Distortion+, Talk Funny, the MW bitcrusher and the 4011 ring modulator, each faithful to its schematic.

## Grown out of the family

### [Toll](docs/Toll.md) — 11 HP · *Drum, Synth voice, Physical modeling*

*Grew out of Kickback's bell.*

BaSnaHi's snare stage was always a better bell than a snare, so it has a module of its own: sixteen modal partials over a membrane, a bar, a tuned bell or a bare harmonic series, with strike position, mallet hardness, per-partial damping, an inharmonicity stretch, a buzzing second layer and a choke. No clock and no patterns — Kickback has those.

### [Schedule A](docs/ScheduleA.md) — 14 HP · *Expander, Sequencer*

*The Repossession expander.*

Eight rows, one per seized asset, each carrying the four charges that can be varied against that step — SPEED, GAIN, START, LENGTH — and the audio that step produces. Repossession already carries all of this polyphonically, on four jacks with channel N addressing step N; that costs no panel space and patches badly. This is the same eight steps with somewhere to plug a cable in.
---

## Install

Download the `.vcvplugin` for your platform from the
[latest release](https://github.com/drphilibuster/MoonTechnologies/releases/latest),
drop it in your Rack plugins folder, and restart Rack.

**Full step-by-step for Windows, macOS and Linux — including where that folder
is on each — is in [docs/INSTALL.md](docs/INSTALL.md).**

Builds are published for `win-x64`, `mac-arm64`, `mac-x64` and `lin-x64` — the
four platforms VCV ships Rack for.

> Not yet in the VCV Library. See [Library status](#library-status) below.

## Build

```bash
make -j8 && make install     # then restart Rack
```

Needs the [Rack 2 SDK](https://vcvrack.com/downloads) and `jq`. Per-platform
toolchain setup is in [docs/BUILDING.md](docs/BUILDING.md).

## Panels

Every panel is generated from a single spec — the artwork, the runtime
silkscreen, the C++ geometry and both previews all come out of
`tools/panels/<Module>.py`, so the panel art and the code that draws over it
physically cannot drift apart. `res/*.svg`, `src/PanelTheme.hpp` and
`src/<Module>/Panel.hpp` are outputs; never hand-edit them.

```bash
make panel          # regenerate every panel
make vcv-preview    # ... and render each through Rack itself
```

The design language, the spec API and what the linter enforces are documented in
[`panelkit/README.md`](panelkit/README.md).

## Library status

Not yet submitted to the VCV Library. The repository is set up for it —
one plugin, one slug, manifest complete, GPL-3.0, reproducible builds through
VCV's own toolchain — and what remains is tracked in
[docs/LIBRARY.md](docs/LIBRARY.md).

Until then, install from [Releases](https://github.com/drphilibuster/MoonTechnologies/releases).

## Licence

Source code is **GPL-3.0-or-later**; the visual design of the panels is
**CC BY-NC-ND 4.0**; the "Moon Technologies" and "Taxxess" names and the maker's
mark are reserved. This is the same split VCV uses for its own Fundamental
plugin — fork the code freely, under your own brand.

See [LICENSE.md](LICENSE.md) for the exact terms and
[LICENSE-GPLv3.txt](LICENSE-GPLv3.txt) for the full GPL text.

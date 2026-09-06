# Moon Technologies

Twenty modules for [VCV Rack 2](https://vcvrack.com), by **Taxxess**.

They share a panel language borrowed from money — a pale engraved note, sage
guilloche round every field, a scroll in every corner, a form number in the
masthead — because all of them are, one way or another, about filing something
and finding out what it cost you.

| | | |
| --- | --- | --- |
| <img src="tools/previews/PatchAudit.png" width="260"> | <img src="tools/previews/Retroactive.png" width="140"> | <img src="tools/previews/UncertaintyPolicy.png" width="160"> |
| **[PatchAudit](docs/PatchAudit.md)** · 26 HP | **[Retroactive](docs/Retroactive.md)** · 14 HP | **[Uncertainty Policy](docs/UncertaintyPolicy.md)** · 16 HP |

*(Panels above are rendered by Rack itself, not mocked up.)*

---

## The modules

### [PatchAudit](docs/PatchAudit.md) — *Utility, Visual*

Browse [Patchstorage](https://patchstorage.com) without leaving Rack. Search the
~9,200 published VCV patches, and before you open one, get an audit of it against
the modules you actually have: what is installed, what is available in the
Library, and what is genuinely gone. Import straight into your rack, or save to
disk.

### [Retroactive](docs/Retroactive.md) — *Effect, Delay, Granular*

A windowed sample-permutation effect. Take a window of consecutive samples and
emit them in a different order, leaving the windows themselves in time order — so
the melody, rhythm and phrasing play forward while every micro-chunk is
rearranged. Short windows are a timbral effect; long ones give the familiar
"reversed but still moving forward" sound. Clock-syncable, with eight permutation
modes and a freeze.

### [Uncertainty Policy](docs/UncertaintyPolicy.md) — *Utility, Random*

A knob and cable randomizer that reads the patch before it rolls. It knows what
each port carries and where the audio goes, auditions every roll, and quietly
withdraws the ones that killed the sound — so you are not spending the session on
roll → silence → undo → roll.

## The new filings (2.1.0)

Six devices built to order, and the whole of Kristian Blåsol's [Modular in a Week](https://www.youtube.com/@modularinaweek) DIY course folded into eleven banks — one module per day's theme rather than one per circuit. Every bank keeps each original's character selectable and default.

*(Rendered by Rack itself, not mocked up.)*

| <img src="tools/previews/Dividend.png" width="144"> | <img src="tools/previews/TaxBracket.png" width="108"> | <img src="tools/previews/Racketeer.png" width="144"> | <img src="tools/previews/Gross.png" width="162"> |
|---|---|---|---|
| **[Dividend](docs/Dividend.md)** · 16 HP | **[Tax Bracket](docs/TaxBracket.md)** · 12 HP | **[Racketeer](docs/Racketeer.md)** · 16 HP | **[Gross](docs/Gross.md)** · 18 HP |

| <img src="tools/previews/Amortization.png" width="126"> | <img src="tools/previews/Repossession.png" width="306"> | <img src="tools/previews/SixFigures.png" width="216"> | <img src="tools/previews/Garnishment.png" width="90"> |
|---|---|---|---|
| **[Amortization](docs/Amortization.md)** · 14 HP | **[Repossession](docs/Repossession.md)** · 34 HP | **[Six Figures](docs/SixFigures.md)** · 24 HP | **[Garnishment](docs/Garnishment.md)** · 10 HP |

| <img src="tools/previews/Consolidation.png" width="108"> | <img src="tools/previews/Installment.png" width="144"> | <img src="tools/previews/Volatility.png" width="126"> | <img src="tools/previews/Deduction.png" width="90"> |
|---|---|---|---|
| **[Consolidation](docs/Consolidation.md)** · 12 HP | **[Installment](docs/Installment.md)** · 16 HP | **[Volatility](docs/Volatility.md)** · 14 HP | **[Deduction](docs/Deduction.md)** · 10 HP |

| <img src="tools/previews/AuditLogic.png" width="198"> | <img src="tools/previews/Kickback.png" width="270"> | <img src="tools/previews/PaymentSchedule.png" width="252"> | <img src="tools/previews/SignHere.png" width="180"> |
|---|---|---|---|
| **[Audit Logic](docs/AuditLogic.md)** · 22 HP | **[Kickback](docs/Kickback.md)** · 30 HP | **[Payment Schedule](docs/PaymentSchedule.md)** · 28 HP | **[Sign Here](docs/SignHere.md)** · 20 HP |

| <img src="tools/previews/Diversified.png" width="162"> |
|---|
| **[Diversified](docs/Diversified.md)** · 18 HP |

### [Dividend](docs/Dividend.md) — 16 HP · *Oscillator, Synth voice*

A pulsar-synthesis VCO after Curtis Roads: trains of pulsarets whose formant is set independently of the fundamental, six pulsaret waveforms, six windows, burst, stochastic and channel masking, an overlap voice pool, and a read-out of fundamental and formant.

### [Tax Bracket](docs/TaxBracket.md) — 12 HP · *Utility, Mixer, Attenuator*

The Olegtron R2R as a genuine resistor network. Every jack is an in/out pair on a passive 8-bit ladder, so it is a DAC, a weighted mixer, a programmable attenuator and a labile multiple at once; checked against the manual's attenuator table.

### [Racketeer](docs/Racketeer.md) — 16 HP · *Noise, Delay, Synth voice*

Wolfgang Spahn's PB701 Electric Intonarumori: a PT2399 delay run as a self-sustaining noise voice, with the chip's clock and word length falling as the delay grows, an optocoupler lag on TIME, a chopper, three enforcement buttons with gate inputs, and DIRTY, ENV and GATE outputs.

### [Gross](docs/Gross.md) — 18 HP · *Distortion, Waveshaper, Effect*

A Wiener–Hammerstein distortion built from the Eichas–Zölzer piecewise-tanh mapping and the dynamic bias of Comunità, Steinmetz and Reiss: input EQ, drive, static and dynamic bias, knees and slopes per polarity, five curve families, output EQ, device presets, and a live transfer-curve read-out.

### [Amortization](docs/Amortization.md) — 14 HP · *Reverb, Effect*

A Verbtronic-style reverb: a Dattorro plate for VERB and an eight-line FDN with a limiter for TRONIC, tonal tilt inside the loop, feedback past unity, predelay, freeze, the mode gate, and wet-only outputs beside the mix.

### [Repossession](docs/Repossession.md) — 34 HP · *Sampler, Sequencer, Visual*

Paste a YouTube link. The module fetches it with yt-dlp and ffmpeg, shows the video on the panel, lets you drag regions on the timeline, and sequences those regions — audio and picture together — by clock, CV, scan and fire, with position, gate, end-of-region and region outputs.

### [Six Figures](docs/SixFigures.md) — 24 HP · *Oscillator*

Modular in a Week, Day 1, folded into one bank: six voices, each a 40106 Schmitt square, a 4069 triangle core, a 4046 PLL that locks to the SIGNAL input, or a reverse-avalanche saw, with sync, capture, drift and a mix.

### [Garnishment](docs/Garnishment.md) — 10 HP · *VCA, Low-pass gate, Dual*

Day 2: two VCA channels, each an LM13700 OTA, a vactrol low-pass gate or the I-AM-O JFET multiplier, with bias, lag and CV amount.

### [Consolidation](docs/Consolidation.md) — 12 HP · *Mixer, Multiple, Utility*

Day 3: the ASMR four-channel mixer with normal and inverted sums, and two 1:3 buffered multiples with B normalled to A.

### [Installment](docs/Installment.md) — 16 HP · *Envelope generator, LFO, Function generator, Dual*

Days 4 and 5: two function generators, each LFO, AR or AD with loop, range, bias and CV, plus the Day 12 tape-motor PWM driver with duty CV.

### [Volatility](docs/Volatility.md) — 14 HP · *Noise, Sample and hold, Random*

Day 6: an 18-bit 4006-style shift-register noise source, the YASH sample and hold, and the PHObos random gate, on one shared clock.

### [Deduction](docs/Deduction.md) — 10 HP · *Filter, Distortion*

Day 7: six filters under one MODEL knob with CV — PAiA 2720-3L, Escobedo Q&D, Korg35, MS-20 OTA, EFM Moog-type high-pass, Synthrotek DIRT — with LP and HP inputs, a CV response switch and a read-out.

### [Audit Logic](docs/AuditLogic.md) — 22 HP · *Logic, Switch, Clock modulator*

Day 8 and the 4066: four logic gates with selectable functions and the 0 V / 12 V reference, two gated switches, and the Emiz CV2 clock divider.

### [Kickback](docs/Kickback.md) — 30 HP · *Drum, Synth voice*

Day 9: eight drum voices with a mix — BaSnaHi kick, snare and hat, SmurfDrum, TomTomTom, XORbell, the percussive noise voice and the Tiny Dazzler.

### [Payment Schedule](docs/PaymentSchedule.md) — 28 HP · *Sequencer, Switch, Quantizer*

Day 10: the Baby8 with the 4017 sequential switch falling out of the same counter in both directions, the 4031 tap looper, and the varimode quantizer.

### [Sign Here](docs/SignHere.md) — 20 HP · *Controller, Utility*

Day 11: the button and pedal, the offset-scaler joystick as an XY pad, and four touch pads.

### [Diversified](docs/Diversified.md) — 18 HP · *Effect, Delay, Reverb, Chorus, Distortion*

Days 12 and 13: a stereo multi-effect with 106 programs — the DSP99 board's categories as twenty algorithms, then the Echomatic echo, Little Angel chorus, spring reverb, MXR Distortion+, Talk Funny, the MW bitcrusher and the 4011 ring modulator, each faithful to its schematic.

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

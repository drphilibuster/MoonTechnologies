# Changelog

Versions follow the VCV convention: the major number is the Rack major version
these modules run on, so a Rack 2 plugin is always `2.x.y`.

## 2.1.0

Seventeen new modules, and a new face for all of them.

### The look

The family is now a banknote rather than a green form: the five-colour "High Contrast" palette, a pale engraved face with dark masthead and footer bands, sage guilloche ribbons, a hairline frame with corner scrolls on every section, wells ringed like seals, a double ring on the primary control, and traces engraved as waves. Nothing in any panel spec changed; the solver tags each label's ground and the emitter resolves its ink.

### Modules

- **Dividend** (16 HP, Oscillator, Synth voice) — A pulsar-synthesis VCO after Curtis Roads: trains of pulsarets whose formant is set independently of the fundamental, six pulsaret waveforms, six windows, burst, stochastic and channel masking, an overlap voice pool, and a read-out of fundamental and formant.
- **Tax Bracket** (12 HP, Utility, Mixer, Attenuator) — The Olegtron R2R as a genuine resistor network. Every jack is an in/out pair on a passive 8-bit ladder, so it is a DAC, a weighted mixer, a programmable attenuator and a labile multiple at once; checked against the manual's attenuator table.
- **Racketeer** (16 HP, Noise, Delay, Synth voice) — Wolfgang Spahn's PB701 Electric Intonarumori: a PT2399 delay run as a self-sustaining noise voice, with the chip's clock and word length falling as the delay grows, an optocoupler lag on TIME, a chopper, three enforcement buttons with gate inputs, and DIRTY, ENV and GATE outputs.
- **Gross** (18 HP, Distortion, Waveshaper, Effect) — A Wiener–Hammerstein distortion built from the Eichas–Zölzer piecewise-tanh mapping and the dynamic bias of Comunità, Steinmetz and Reiss: input EQ, drive, static and dynamic bias, knees and slopes per polarity, five curve families, output EQ, device presets, and a live transfer-curve read-out.
- **Amortization** (12 HP, Reverb, Effect) — A Verbtronic-style reverb: a Dattorro plate for VERB and an eight-line FDN with a limiter for TRONIC, tonal tilt inside the loop, feedback past unity, predelay, freeze, the mode gate, and wet-only outputs beside the mix.
- **Repossession** (34 HP, Sampler, Sequencer, Visual) — Paste a YouTube link. The module fetches it with yt-dlp and ffmpeg, shows the video on the panel, lets you drag regions on the timeline, and sequences those regions — audio and picture together — by clock, CV, scan and fire, with position, gate, end-of-region and region outputs.
- **Six Figures** (24 HP, Oscillator) — Modular in a Week, Day 1, folded into one bank: six voices, each a 40106 Schmitt square, a 4069 triangle core, a 4046 PLL that locks to the SIGNAL input, or a reverse-avalanche saw, with sync, capture, drift and a mix.
- **Garnishment** (10 HP, VCA, Low-pass gate, Dual) — Day 2: two VCA channels, each an LM13700 OTA, a vactrol low-pass gate or the I-AM-O JFET multiplier, with bias, lag and CV amount.
- **Consolidation** (12 HP, Mixer, Multiple, Utility) — Day 3: the ASMR four-channel mixer with normal and inverted sums, and two 1:3 buffered multiples with B normalled to A.
- **Installment** (16 HP, Envelope generator, LFO, Function generator, Dual) — Days 4 and 5: two function generators, each LFO, AR or AD with loop, range, bias and CV, plus the Day 12 tape-motor PWM driver with duty CV.
- **Volatility** (12 HP, Noise, Sample and hold, Random) — Day 6: an 18-bit 4006-style shift-register noise source, the YASH sample and hold, and the PHObos random gate, on one shared clock.
- **Deduction** (10 HP, Filter, Distortion) — Day 7: six filters under one MODEL knob with CV — PAiA 2720-3L, Escobedo Q&D, Korg35, MS-20 OTA, EFM Moog-type high-pass, Synthrotek DIRT — with LP and HP inputs, a CV response switch and a read-out.
- **Audit Logic** (18 HP, Logic, Switch, Clock modulator) — Day 8 and the 4066: four logic gates with selectable functions and the 0 V / 12 V reference, two gated switches, and the Emiz CV2 clock divider.
- **Kickback** (30 HP, Drum, Synth voice) — Day 9: eight drum voices with a mix — BaSnaHi kick, snare and hat, SmurfDrum, TomTomTom, XORbell, the percussive noise voice and the Tiny Dazzler.
- **Payment Schedule** (28 HP, Sequencer, Switch, Quantizer) — Day 10: the Baby8 with the 4017 sequential switch falling out of the same counter in both directions, the 4031 tap looper, and the varimode quantizer.
- **Sign Here** (20 HP, Controller, Utility) — Day 11: the button and pedal, the offset-scaler joystick as an XY pad, and four touch pads.
- **Diversified** (18 HP, Effect, Delay, Reverb, Chorus, Distortion) — Days 12 and 13: a stereo multi-effect with 106 programs — the DSP99 board's categories as twenty algorithms, then the Echomatic echo, Little Angel chorus, spring reverb, MXR Distortion+, Talk Funny, the MW bitcrusher and the 4011 ring modulator, each faithful to its schematic.

### Under the hood

- `panelkit/palette.py` derives every colour from five anchors; `palette.ink(role, ground)` is the one place a label's hex is chosen.
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
- **Retroactive** (12 HP, Effect/Delay/Granular) — windowed sample permutation
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

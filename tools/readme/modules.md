<!-- The prose behind README.md. tools/readme.py reads this, plugin.json and the
     generated panel headers, and writes the README. One `## Slug` block per module:
     `group:` is built | miaw | grew, `credit:` is optional, the rest is the blurb.
     Order here is the order in the README. Do not write an HP figure or a count in
     a blurb -- the generator supplies the HP, and a number typed here is one that
     goes stale. -->

## PatchAudit
group: built

Browse [Patchstorage](https://patchstorage.com) without leaving Rack. Search the published VCV patches, and before you open one, get an audit of it against the modules you actually have: what is installed, what is available in the Library, and what is genuinely gone. Import straight into your rack, or save to disk.

## Retroactive
group: built

A windowed sample-permutation effect. Take a window of consecutive samples and emit them in a different order, leaving the windows themselves in time order — so the melody, rhythm and phrasing play forward while every micro-chunk is rearranged. Short windows are a timbral effect; long ones give the familiar "reversed but still moving forward" sound. Clock-syncable, with a choice of permutation modes and a freeze.

## UncertaintyPolicy
group: built

A knob and cable randomizer that reads the patch before it rolls. It knows what each port carries and where the audio goes, auditions every roll, and quietly withdraws the ones that killed the sound — so you are not spending the session on roll → silence → undo → roll.

## Dividend
group: built
credit: *After Curtis Roads' pulsar synthesis.*

Trains of pulsarets whose formant is set independently of the fundamental, a choice of pulsaret waveforms and windows, burst, stochastic and channel masking, an overlap voice pool, and a read-out of fundamental and formant.

## TaxBracket
group: built
credit: *After the Olegtron R2R.*

The R2R as a genuine resistor network. Every jack is an in/out pair on a passive 8-bit ladder, so it is a DAC, a weighted mixer, a programmable attenuator and a labile multiple at once; checked against the manual's attenuator table.

## Racketeer
group: built
credit: *After Wolfgang Spahn's PB701 Electric Intonarumori.*

A PT2399 delay run as a self-sustaining noise voice, with the chip's clock and word length falling as the delay grows, an optocoupler lag on TIME, a chopper, three enforcement buttons with gate inputs, and DIRTY, ENV and GATE outputs.

## Gross
group: built
credit: *After Eichas–Zölzer and Comunità–Steinmetz–Reiss.*

A Wiener–Hammerstein distortion built from the piecewise-tanh mapping and a dynamic bias: input EQ, drive, static and dynamic bias, knees and slopes per polarity, several curve families, output EQ, device presets, and a live transfer-curve read-out.

## Amortization
group: built
credit: *After the Pittsburgh Modular Verbtronic.*

The module's public-domain schematic, solved: three PT2399 echo chips (a 1-bit delta modulator and 44 kbit of RAM each) in a recirculating network, a tone shelf, a zener feedback limiter, and a linearised VCA mixer. VERB and TRONIC change the chips' clocks, so a mode change replays what is stored at a new rate.

## Apportionment
group: hardware
credit: *An Ensoniq DP/4, running its own firmware. EPROMs not included.*

ESP effect units under the DP/4's real operating system and DSP code -- every algorithm, preset and Config is Ensoniq's -- on an emulated 68B03 and four ES5510s. The DP/4's own front panel is here button for button, and the thing the hardware hid in its menus is on the panel: every Config parameter that routes the four units (source count, A-B and C-D serial/parallel/feedback, AB into CD, amounts, mono/stereo inputs, output selects) is a control, and moving one makes the module work the Config pages for you. Per-unit stereo taps for patching the units into the rest of the rack.

## Contagion
group: hardware
credit: *An Access Virus C, running all of its own firmware. OS image not included.*

Both processors run the unit's own OS: the 80C515 that owns the front panel, the LCD, MIDI and preset memory -- which every other Virus emulation replaces with C++ -- and the DSP56362 it boots and drives, on the dsp56300 core gearmulator uses. Every knob reaches the firmware through the microcontroller's own A/D converter and every control that is not a pot through its key matrix -- the unit's button pairs and cycles are endless and detented knobs here, each a press of the real key with the real LEDs read back; the dot-matrix LCD shows what the unit shows. Played over MIDI, or from cables: polyphonic pitch, gate and velocity, wheels, clock and run turn into MIDI, eight CV inputs move any knob and gates press the navigation buttons. The patch keeps the battery RAM.

## NordicBanking
group: hardware
credit: *A Clavia Nord Lead 2X, running its own firmware. OS image not included.*

The real MC68331 operating system, instruction by instruction, driving two emulated DSP56362s on the dsp56300 core -- the whole Nord Lead 2X from its 512 KB OS image. The front panel is the unit's: every knob on the 68331's converter, every button on its key lines, and the LED multiplex and three-digit display drawn at the brightness the firmware drives them, LED pairs and all. Played over MIDI, with Clavia's factory programs loaded as SysEx; the patch keeps the program flash.

## Rebate
group: hardware
credit: *An Alesis MIDIverb (or MIDIFEX), running its own firmware. EPROMs not included.*

Keith Barr's 1986 discrete-logic reverb: the real microcode on an emulation of the TTL signal processor, verified sample for sample against MAME's, and the real 80C31 firmware behind the real front panel -- two digits, UP, DOWN, CHANNEL, DEFEAT, MIDI program change. The analog board is from the schematic, down to the "Clipping?" diode on the converter's input filter, the sample-and-hold, and the DAC's hold capacitors. A patch keeps program, channel and defeat, and plays them back into the firmware after power-on.

## Depreciation
group: hardware
credit: *A Lexicon PCM 70, running its own firmware. ROMs not included.*

The 1986 reverb, chorus, delay and resonant-chord machine on an emulated pair of Z80s and its own signal processor, with the real converter filters around them -- every program, parameter and MIDI patch is Lexicon's, because nothing here re-implements them. The hardware hid its parameters behind a key matrix and one soft knob; here they are a field of knobs that are the machine's own cells, each with the firmware's name and printed value on a plate, following the firmware when it moves a value itself. a single detented preset selector (FACTORY programs or your USER registers, named in the display before you press LOAD), mod wheel / aftertouch / note / gate / sustain / soft-knob / program / bypass / clock / run jacks, the headroom meter, a register bank that travels with the patch and imports and exports as SysEx, and wet taps. Both software versions are supported.

## Repossession
group: built

Paste a YouTube link. The module fetches it with yt-dlp and ffmpeg, shows the video on the panel, lets you drag regions on the timeline, and sequences those regions — audio and picture together — by clock, CV, scan and fire, with position, gate, end-of-region and region outputs. The picture is not just a thumbnail: it decodes up to 720p and hands every frame to [Transmittal](docs/Transmittal.md), so the video you are sequencing is the video going to the projector.

## Collusion
group: built
credit: *After Kuramoto, and Mirollo–Strogatz.*

LFOs that listen to each other. Each has its own natural rate; COUPLING says how hard each is pulled toward the rest, and past a threshold set entirely by SPREAD the population stops drifting and locks — a phase transition on one knob and six lamps. Four wirings (all-to-all, ring, one-way cascade, and pulse coupling), a phase lag that buys partial order, LFO and audio ranges, and a Benjolin rungler whose bits are written from how much the swarm currently agrees, so the melody it plays *is* the phase transition.

## Reconciliation
group: built
credit: *After Partch, Wilson, Tenney, Barlow and Sethares.*

A polyphonic just-intonation quantizer that splits "which note?" into two questions and puts a knob on each. Which pitches exist: Partch's eleven-limit tonality diamond, his 43-tone scale, one Otonality or Utonality hexad, Wilson's hexany and eikosany, or the raw harmonic series — transposed onto any of Partch's six identities and pruned by prime limit. How one gets chosen: Euler's *gradus suavitatis*, Tenney's harmonic distance, Barlow's harmonicity, sensory dissonance against an assumed timbre, or adaptive tuning from the last note played, which tunes every interval pure and lets the tonal centre drift by the comma it costs. The read-out names the ratio.

## Dependents
group: built
credit: *After [Astrobear Music](https://www.youtube.com/watch?v=O0QLnR406pQ) and Aspen Instruments' Black Diamond Distortion.*

A chord made by distorting one sine you cannot hear. Chebyshev polynomials of the first kind satisfy Tₙ(cos x) = cos(nx), so a unit-amplitude sine through the nth of them comes out as exactly the nth harmonic — which makes a waveshaper a harmonic recipe, and harmonic numbers in small whole ratios are chords. 4:5:6 is a just major triad, 10:12:15 a minor. Put the root two octaves below hearing and the only thing audible is the chord. Two chord slots and a MORPH that interpolates the *weights* between them, so major to minor passes through spectra with no name; per-harmonic trims as a CUSTOM slot to morph against; and a HOLD that keeps the input at unity, because the identity is only true there — switch it off and the chord dissolves as the signal quietens. Feed it anything but a sine and it is chaos, which is not defended against.

## Calculation
group: built
credit: *After Count Modula's Event Timer (Countdown 3 and 5).*

A phrase counter for arranging a patch. One base length N and six lines that each fall due at N times their own ratio, from ÷8 to ×32, with a trigger, a gate and a counter apiece. It does the work of a chain of countdown timers in one module. Every line counts from the same downbeat, and a START that arrives a cable's delay after its clock still claims that clock, so chained sections stay on the beat without setting anything to N−1. Lines can repeat instead of firing once, and a Countdown-compatible start mode is in the menu.

## Ledger
group: built
credit: *After Ormer Modular's Shoal (MIT) and Squarp's Hermod+.*

An eight-track generative sequencer. Each track's melody is grown from a seed, never written down. Every step is worked out from the seed the moment it plays, so a knob turned back returns the exact line it left, and a seed number recalls a melody anywhere. Its generator is Shoal's engine, ported line for line and tested frame by frame against the original. Ledger therefore keeps all of Shoal: chance, bipolar note and octave variation, fifteen walks, twenty-nine exact clock ratios, Follow (tracks sharing a seed through their own filters, which gives harmonies and canons), Evolve, Breathe, Weight, Slop, Freeze, Currents and end-of-sequence pulses. On top of it sit Hermod-style written patterns: a polyphonic piano roll with modulation lanes, sixteen slots per track launched as sequences, and CAPTURE, which writes a generated loop down so it can be edited. Each track runs its notes through up to eight of Hermod's nineteen effects (arpeggiator, echo, ratchet, Turing register, LFO and the rest), with per-slot values and mutes. MIDI in plays and records into the tracks (overdub, replace or looper), its CCs drive the mod matrix, and a transpose leader moves them. Two MIDI outputs carry every track on its own channel, along with clock and transport. Rows of slots launch together on a loop's end, a grid, or when every loop comes round at once, and a song strings rows together, each played its number of times. Rack's undo covers the pattern, slot, effect and song edits.

## SixFigures
group: miaw
credit: *Day 1, consolidated.*

Voices, each a 40106 Schmitt square, a 4069 triangle core, a 4046 PLL that locks to the SIGNAL input, or a reverse-avalanche saw, with sync, capture, drift and a mix. The four cores are the four oscillator builds of that day, selectable per voice.

## Garnishment
group: miaw
credit: *Day 2, consolidated.*

Dual VCA channels, each an LM13700 OTA, a vactrol low-pass gate or the I-AM-O JFET multiplier, with bias, lag and CV amount. One build per MODE position.

## Consolidation
group: miaw
credit: *Day 3, consolidated.*

The ASMR four-channel mixer with normal and inverted sums, and two 1:3 buffered multiples with B normalled to A.

## Bailout
group: grew
credit: *Day 3, doubled.*

Consolidation's mixer at eight channels, grouped as two banks of four with their own mixes and a main that sums whichever banks are free — one 8-into-1 mixer, two independent 4s, or eight VCAs, depending only on what is patched. Two 1:7 multiples instead of two 1:3.

## Projection
group: video

Video made out of what the rack is already doing. An XY scope, a banded spectrum, or a field warped by those bands — driven by audio and by CV on every control, with a gate for flash and one for freeze. Band envelopes and an onset trigger leave on the footer, so it earns its space as an analyser even when nothing is watching the picture.

## Transmittal
group: video

The plugin's video output. Takes frames from another module here — Repossession, so far — and publishes them as a GPU texture over **Syphon**, which TouchDesigner reads with a Syphon Spout In TOP. No encoder and no player in the path, so the latency is one Rack frame plus one TouchDesigner frame: fast enough to perform to, which is the whole point of it. An **HLS** transport is there as well, for capture and for platforms without texture sharing, at the two or three seconds that format costs. Standalone Rack only — video does not work in VST for anyone, LZX included.

## Installment
group: miaw
credit: *Days 4, 5 and 12, consolidated.*

Two function generators, each LFO, AR or AD with loop, range, bias and CV, plus the tape-motor PWM driver with duty CV riding on channel one.

## Volatility
group: miaw
credit: *Day 6, consolidated.*

An 18-bit 4006-style shift-register noise source, the YASH sample and hold (an LF398 with its 3 µs trigger-pulse circuit), and the PHObos random gate — three separate builds put on one shared clock.

## Deduction
group: miaw
credit: *Day 7, consolidated.*

Filters under one MODEL knob with CV — PAiA 2720-3L, Escobedo Q&D, Korg35, MS-20 OTA, EFM Moog-type high-pass, Synthrotek DIRT — with LP and HP inputs, a CV response switch and a read-out.

## AuditLogic
group: miaw
credit: *Day 8, plus the 4066 and the Emiz CV2.*

Four logic gates with selectable functions and the 0 V / 12 V reference, gated analogue switches, and a clock divider. Separate boards, separate blocks, read top to bottom.

## Kickback
group: miaw
credit: *Day 9 — and a long way past it.*

Started as the six drum voices of that folder and is now a drum machine. The voices are modal banks struck by a real contact pulse: the BaSnaHi kick with the SmurfDrum as its second model, a snare switching between XORbell, the percussive noise voice and Karplus–Strong, a hi-hat whose one knob sweeps metal to noise to the Tiny Dazzler, and all three TomTomTom rings at once as three differently-sized drums. **None of the rest is in the original:** its own clock, a ranked pattern engine (a metric spine plus Euclidean necklaces, with per-voice lengths and an EVOLVE that moves the loop pass to pass), per-voice clock ratios, a BURST mode that hands each voice's steps to its own ratio, velocity-scaled gate outputs, and a SEED that swaps patterns on the bar line. The physics is out of the literature rather than the schematic — Bilbao's contact force, Bessel-zero mode ratios, Karplus–Strong's drum recurrence.

## PaymentSchedule
group: miaw
credit: *Day 10, consolidated.*

The Baby8 with the 4017 sequential switch falling out of the same counter in both directions, the 4031 tap looper, and the varimode quantizer.

## SignHere
group: miaw
credit: *Day 11, consolidated.*

The button and pedal, the offset-scaler joystick as an XY pad, and four touch pads. Gesture controllers that share nothing electrically in the original.

## Diversified
group: miaw
credit: *Days 12 and 13, consolidated.*

A stereo multi-effect with a long program list — the DSP99 board's categories as algorithms, then the Echomatic echo, Little Angel chorus, spring reverb, MXR Distortion+, Talk Funny, the MW bitcrusher and the 4011 ring modulator, each faithful to its schematic.

## Toll
group: grew
credit: *Grew out of Kickback's bell.*

BaSnaHi's snare stage was always a better bell than a snare, so it has a module of its own: modal partials over a membrane, a bar, a tuned bell or a bare harmonic series, with strike position, mallet hardness, per-partial damping, an inharmonicity stretch, a buzzing second layer and a choke. No clock and no patterns — Kickback has those.

## Obfuscation
group: grew
credit: *After Kilohearts' Disperser.*

A three-band dispersion matrix in the manner of Disperser: each band runs through up to ninety-six second-order allpass stages in series, with FREQ, PINCH (the stage Q) and STAGES, then the bands are summed, saturated, tilted, clipped and boosted. RANDOM redraws every stage's cutoff and pinch on a clock or on the input's peaks, and the RANDOM gate also freezes the current set while it is held.

## ScheduleA
group: grew
credit: *The Repossession expander.*

A row per seized asset, each carrying the charges that can be varied against that step — SPEED, GAIN, START, LENGTH — and the audio that step produces. Repossession already carries all of this polyphonically, on four jacks with channel N addressing step N; that costs no panel space and patches badly. This is the same steps with somewhere to plug a cable in.


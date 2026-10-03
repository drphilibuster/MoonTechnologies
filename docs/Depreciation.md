# Depreciation — PCM 70 digital reverb

**FORM 4562 — Depreciation and Amortization, the form on which value is written down on a schedule.** 18 HP.

A Lexicon PCM 70 digital effects processor (1986) — reverb, chorus, delay and
resonant chords — running its own operating system. The module emulates the
board: the two Z80 processors and their firmware, the 128-word signal processor
that does the audio, and the converter filters around it. Every program,
parameter and MIDI patch is the machine's own, because nothing here
re-implements them; the module only presses the machine's keys, turns its
parameters with the routine its soft knob calls, and listens to what it says. It
is based on a 1986 Lexicon PCM 70; it is not made or endorsed by Lexicon.

What the module adds is what the hardware hid behind a key matrix and one soft
knob: the whole **parameter matrix** is on the read-out, every cell the
firmware's own name over its printed value and every cell a control, and the
dedicated jacks into the machine's own MIDI patch system.

**Everything is set on the read-out.** Hold a value and drag up or down (the
pointer turns into up/down arrows; Ctrl drags fine, Shift coarse); click a
choice -- PRESET, CLK / -- to pick from its list; click a switch -- BANK, the
pads -- to flip it; click LOAD, STORE or BYPASS to press it. Right-click any cell
for the param's own menu: typed entry, reset, MIDI-Map.

---

## Before anything works: the ROMs

The firmware is Lexicon's and is **not included**. You need the five images from
a PCM 70 (or dumps of them):

| Image | Size | What |
|---|---|---|
| U62 | 32 KB | master firmware |
| U95 | 16 KB | slave firmware (it also holds many of the factory programs) |
| U67 | 8 KB | opcode ROM |
| U48 | 512 B | control PROM |
| U49 | 32 B | sequencer PROM |

Right-click the module and choose **Load ROM folder…**. The module tells the
files apart by their hashes, not their names, and the display says what it found:

| Display | Meaning |
|---|---|
| `LOAD ROMS` | nothing loaded yet |
| `V2.0 ROMS OK`, `V3.01 ROMS OK` | a complete set of one software version |
| `MISSING V3.01 U95` | the folder lacks that image |
| `U67 IS V2.0, NEEDS V3.01` | a good dump of the wrong version: U62, U95 and U67 must come from the **same** software version (the V2 and V3 opcode ROMs differ in two pages) |
| `UNKNOWN U95 1a2b3c4d` | a file of the right size that is not a dump this module knows; the number is the start of its SHA-256 hash |

A folder may hold both the V2 and the V3 set; the newer wins. The module
remembers the last set that loaded (in `MoonTechnologies/settings.json` in Rack's
user folder), so the next Depreciation you add finds it by itself. A patch stores
the *paths* to the images, never their contents.

After the images load, the machine **powers up like the hardware does**: about
nine seconds of its own time, during which the outputs are silent and the display
walks through the firmware's start-up messages. It is built and run on a worker
thread, so Rack's audio never stalls for it. A different sample rate rebuilds the
machine (keeping its registers).

## What is different between V2.0 and V3.01

| | V2.0 | V3.01 |
|---|---|---|
| power-up program | 0.0 CHORUS | 0.0 MOD WOBBLE |
| factory program rows | 0 to 6 (row 6 holds the MIDI programs: MIDI Echo BPM, Cascade BPM, Filtr Pan BPM …) | 0 to 6 (row 6 is Inverse Room, Inverse 2, Head Banger, Ski Jump, Atom Smasher, Gated Room, kept in the master ROM) |
| MIDI clock | **ignored** (the firmware has no handler) | tempo from clock; the BPM programs' RATE parameter becomes an *offset* to the clock tempo (64 to 191 BPM), and the tempo is held when the clock stops |
| display | 16 digits | the first two digits carry a symbol; programs may show glyphs outside the font table (drawn as `?`) |

Both versions load every register of their own library bank, and the same
panel, tests and MIDI behaviour apply.

---

## The panel

### Read-out well

The machine's own **16-digit display**, drawn live from the firmware's display
RAM, and nothing else of the firmware's is drawn: the display holds a few
symbols its font has no letter for (the MIDI-sync mark in the first two digits),
and they are left blank rather than drawn as `?`. It shows the display the
firmware settled on, not the instant-by-instant RAM, which the module's own
parameter probes briefly rewrite.

Under it, one line:

* what the **PRESET** selector points at and what LOAD will do with it —
  `FACTORY 13  MIDI MOD PAN   PRESS LOAD`, `USER 07  SINGLE DELAY   RUNNING`,
  `USER 01  EMPTY` (the number is the row and column run together, so `13` is the
  firmware's `1.3`);
* for four seconds after you touch a parameter cell, that parameter with the
  firmware's printed value (`1.1  RT MID  1.2 S`);
* for three seconds after a refused LOAD or STORE, why it was refused.

At the right of that line the **HEADROOM** bar (0, −6, −12, −18, −24 dB of the converter's
full scale, from the same peak detector the firmware's gates read). If the
firmware ever fails to come back from one of the module's calls the well says
`FIRMWARE STALLED` and the call is abandoned.

### Presets

The strip under the display: PRESET, BANK, LOAD, STORE and BYPASS.

**PRESET** picks a slot, 0 upward -- click it for the list of slots by name, or
drag through them. **BANK** (FACTORY | USER) says what a slot is:

* **FACTORY** — the machine's own programs, the effects: Chorus, Concert Hall,
  Gated Room … Ten to a row, slot = 10 × row + column, so slot 13 is the
  firmware's `1.3`. PRESET stops at the last program the firmware has
  (both versions have rows 0 to 6, with fewer than ten in most), and the
  names come from the machine itself: a few seconds after power-up a scratch
  machine reads every slot's name, off the audio thread, so the well can say what
  you are about to load before you load it. The row of housekeeping programs
  (clear memory, MIDI reset …) is not offered here; **Clear memory**, the MIDI
  settings and the rest are in the context menu.
* **USER** — your 50 registers, 0 to 49: a program with the parameters you left it
  at. Each shows its own name, or `EMPTY`.

**LOAD** loads the slot. Loading an empty register would show `UNUSED` and do
nothing useful, so the module does not send it: the well says `EMPTY: NOTHING
STORED THERE`. **STORE** files the running program as the USER slot; with FACTORY
selected it is refused (`SWITCH TO USER TO STORE`) because there is nowhere to
file it. The module presses the machine's own keys, so the display shows what the
hardware showed (`LOADING PROGRAM`, `1.3 CIRCULAR DLYS`).

**BYPASS** is the BYP key (the light shows the machine's own bypass flag): in
bypass the firmware mutes the wet path and keeps the dry. The flag lives in the
battery RAM, which a patch keeps, so a patch saved while bypassed used to come back
bypassed and silent-looking; the module now always powers up live.

### Parameters

Five rows by nine cells on the read-out: **the machine's own parameter
matrix**, row and column as the hardware's PARAM mode counts them, whatever the
running program is -- which is the shape the real unit's front panel has. Each
cell shows the firmware's name for it (`MIX % WET`, `HC`, `RT MID S`, `DLY MST`
…) over its printed value (`5.75 KHZ`, `1.2 S`, `17.7 M`); the cell you last
touched lights. A cell the program does not have shows `--`.

* A cell's range is the parameter's own range, which the firmware reports; a few
  limits move with the program (pre-delay, the delay taps), and the cell follows.
* Dragging a cell asks the firmware to move the parameter; the firmware clamps and
  applies it exactly as it would for its soft knob, and **the cell follows the
  firmware's word back** — when a program loads, when a *master* parameter moves
  its children, when a limit clamps. It never moves under your hand.
* **Cell 0.2 is SOFT KNOB**, the parameter MIDI patches use as a source; cell 0.0
  is MIX and 0.1 FX ADJ on every program.
* Some programs use fewer cells (Concert Hall 29); the most any program uses is 36.
* The tooltip of every cell is the firmware's name and value.

### No CV lanes

Earlier versions had eight CV lanes (jack, attenuverter, SET button) that could be
assigned to any parameter. They are gone. Every lane was a stream of edits into
the master processor through the firmware's own soft-knob routine, which is slow
enough that a handful of lanes kept the machine busy most of the time; nothing in
the module could make that routine cheaper, so the lanes could not be made to
cost less, only fewer. What remains is the parameter matrix by hand, the
**SOFT** jack (one parameter, the firmware's own MIDI source) and the registers'
own Dynamic MIDI patches, driven from MOD, AT, NOTE and the rest.

### Dedicated jacks

| Jack | What it does |
|---|---|
| **MOD** | 0 to 10 V → MIDI CC 1 (mod wheel) |
| **AT** | 0 to 10 V → channel pressure (aftertouch) |
| **NOTE** | 1 V/oct, 0 V = middle C → note number |
| **GATE** | rising edge sends a note on (velocity = voltage, 10 V = 127) with the NOTE jack's note, falling edge a note off |
| **SUST** | gate above 1 V → CC 64 (sustain pedal) |
| **SOFT** | 0 to 10 V adds to cell 0.2, SOFT KNOB, over its whole range |
| **CLOCK** | clock edges, at the rate **CLK /** (on the setup strip) selects: 1, 2, 4, 8 or 24 per quarter note, 24 being the hardware's own; *V3 firmware only* |
| **RUN** | rising edge restarts the firmware's tempo measurement (MIDI start); *V3 only* |
| **PGM** | 0.1 V per register (0 to 4.9 V = registers 0 to 49); turns **program change** on while it is patched |
| **BYP** | rising edge toggles bypass |

These feed the firmware's own MIDI patches: a register's patches (a
Mod Wobble's mod wheel, an Infinite A T's aftertouch) work exactly as on the
hardware. The jacks send on MIDI channel 1, which is also the firmware's default
channel (see the menu to change both together).

### Levels

The setup strip under the matrix. **INPUT** is the input level (full up = the
converter's full scale). **IN +4 / −20** takes 15 dB off the input as the
hardware's switch does; **OUT +4 / −20** takes
24.7 dB off the output. **FULL SCALE** sets the voltage that means converter full
scale (default 5 V peak; the hardware's is 10 V at its jack), so a full-scale
reverb is a full-scale Rack signal. The dry path and the wet path keep the
hardware's relative level (dry and wet are equal at equal mix codes).

### Jacks

**IN** and **IN R** (summed: the hardware is mono-in), **OUT L** and **OUT R**
(after the mix), and **WET L** and **WET R**: the wet signal after the output
filter and before the mix — a convenience the hardware lacked.

### Context menu

Besides the ROM entries: **MIDI channel**, **OMNI mode**, **Program change
enabled**, **Auto load** and **Memory protect** (the firmware's own system
options, stored in its battery RAM — see *Registers and the patch*);
**Import SysEx bank…**, **Export SysEx bank…**, **Power cycle**, and **Clear memory
(factory fresh)**.

---

## Registers and the patch

The PCM 70 keeps 50 registers (rows 0 to 4 by columns 0 to 9) and its system
settings in battery RAM. So does the module: **the patch stores the 8 KB RAM
image**, which holds your registers and settings and none of Lexicon's code, and
gives it back to the firmware at power-up. A new module is a factory-fresh machine
(all registers `UNUSED`, MIDI channel 1, OMNI off, program change **off**, auto
load off, memory protect off). Program change being off in a fresh machine is the
firmware's own default: a MIDI program change does nothing until you enable it in
the menu (or patch the PGM jack, which enables it).

* **Store** a register: set USER, choose the slot, and press **STORE** (the machine's
  own gesture: hold F3, press LOAD).
* **Export SysEx bank** writes every used register as the machine's own bulk
  dump (one message per register, stored form); **Import SysEx bank** feeds a
  bank file to the firmware through its MIDI port, one message at a time with the
  gaps the firmware needs (a bank of 50 takes about 15 seconds). Importing files
  registers; it does not load a program. Loading is LOAD, a program change, or the
  sysex "active program" form.
* **Clear memory** zeroes the image and power-cycles; **Power cycle** keeps it.

### Dynamic MIDI

A register can carry up to **ten MIDI patches**: a controller (CC 0–31 or 64–94,
pitch bend, channel pressure, note number, note velocity, or the soft knob)
driving a parameter with a signed scaling of ±1 to ±128, applied as steps of the
parameter's value (a patch reaches at most ±127 steps from the stored value, and
controllers are 7-bit, as on the hardware). A patch moves what the slave hears,
not the stored value. These are the firmware's, shown in PARAM mode row 5 on the
hardware.

---

## How much the machine can take

The master processor's main loop serves about **100 events per second in total**
(parameter edits and MIDI messages share it). The module therefore never queues
without limit: each parameter has one pending target (the latest wins), changes
under three quarters of a step are ignored, targets are served round-robin by
how long they have waited and how far they are from where they are going,
**master** parameters (the REFL and DELAY MASTER cells, which move six children
at once and cost 50–100 ms) wait their turn, and the firmware is never busy with
the module more than about 65% of the time. **SIZE** and the masters are slow by
nature on the hardware (SIZE rescales the whole program in 0.4 to 0.6 seconds):
a fast move on them is served as fast as the firmware can take it, no faster. The
tests flood every parameter at 1 kHz with a different program loaded in the
middle; the firmware stays healthy. The panel no longer carries the load meter
the lanes needed: with the lanes gone there is nothing for it to warn about, and
at rest it only showed the module's own housekeeping.

---

## What is exact, and what is not

**Exact (checked against the firmware's own behaviour or the recordings):**

* The firmware is Lexicon's own, run as is; programs load, parameters apply,
  captions render, MIDI patches and the clock behave as the machine's do.
* Edits through the panel equal what the soft knob does: every non-master
  parameter of the tested programs gives, after an edit, the same control store as
  a fresh load of that value (the master parameters differ in a few words, as the
  hardware's own edit and load paths do); one call equals the same total made of
  single steps.
* The signal processor is bit-identical to a tick-accurate model of the HSP
  (random programs, every program of both library banks, and live chorus, flange
  and hall programs whose control store the firmware retunes sample by sample).
* The converter filters are the machine's real complex responses (a 9-pole
  elliptic anti-alias and reconstruction filter, the DAC hold and the aperture
  stage), built into the sample-rate conversion; the machine's clock is exactly
  13 MHz / 384 = 33 854.1667 Hz whatever Rack's rate is.
* Against the "PCM70 V2 True Stereo" impulse-response library: delay and
  resonant-chord echo **timing within one sample of the recordings** (a constant
  0.3 ms later in the Pan Delays, the converter chain's own latency), echo
  **levels within about ±1 dB**, chord pitches within 0.1 cent.
* Against the library's reverb folders (median difference of the 50 ms decay
  envelope, whole machine through the module, many settings per folder): Small
  Room 0.15 dB, Infinite Reverb 0.27, MIDI Infinite Reverb 0.22, Tiled Room 0.63,
  Gymnasium 0.90, MIDI CT Hall 1.0, Small Plate 1.1, Rich Chamber 1.1, Rich Plate
  2.4, Long Hall 3.1, Concert Hall 4.3; the Ver-3 presets mostly 0.1 to 1 dB. The
  gated programs agree to 0.1–0.5 dB when the input is loud (see below).

**Not exact, or not verified — stated plainly:**

* **Programs with no recording** to check against run on the exact machine but are
  not validated: Mod Wobble, Echorus, Power Phlange, 6 Voice Combo, Flange o Echo,
  Auto Chorus, the BPM programs, and a few others of row 0 and 1.
* **Rhythm in C** (the time-varying programs) matches 6 to 10 of 16 pulses of the
  recording's deconvolved response; a deconvolved sweep is not an impulse
  response for a time-varying program.
* **Long Hall and Concert Hall at long reverb times** decay about 10% faster than
  the recordings (RT60 4.3 s against 4.7 s for Concert Hall at 11.0:5.9). The
  recordings are sweep measurements of a modulated reverb, which smears late
  energy; that is the likely reason, not a proven one.
* **Gated programs** follow the signal level the way the machine's gate does (it
  works on absolute detector codes), so they respond to *your* input level, not the
  library authors' unknown test level. The detector's front end is derived from
  the schematic, not measured.
* **Infinite A T**: any edit clamps its 528 to 527, as the hardware does; the
  library recording of it is a sweep artefact of its lossless hold state.
* **Converters** are modelled ideal (literal 16-bit truncation fitted the
  recordings worse). The input is not dithered.
* **The DC blocker** after the core has a 3 Hz corner that was not measured (the
  real unit is AC coupled; the corner is not known).
* **The library's Rich Plate labels** use another display table than the
  firmware's; the module's captions are the firmware's.
* **Latency** is about 0.5 ms at 48 kHz (the look-ahead of the two band-limited
  converter stages); the dry path carries the same delay so dry and wet keep the
  relative timing the hardware has.
* A V3 edit within ±3 steps of a program's loaded **DEFINITION** value leaves 8
  coefficient bytes alone — the firmware's own behaviour, for the knob as for the
  module.

## CPU

About 8 to 12% of one core at 48 kHz for the whole machine; more at 192 kHz
(the input conversion's kernel grows with the host rate). The firmware's two
processors cost about 1%.

## Where this comes from

The emulation was built from the service manual, the schematics, the owner's
manual, the library's recordings and the firmware itself; the machine and the panel
logic are tested without Rack in `tests/Depreciation` against the user's own
ROMs (`PCM70_ROMS`), banks (`PCM70_SYX`) and recordings, which are never in this
repository. Nothing derived from the ROMs — parameter tables, names, captions,
factory programs — is stored here: the module rebuilds all of it at run time from
the images you load. The Z80 core is
[superzazu/z80](https://github.com/superzazu/z80) (MIT).

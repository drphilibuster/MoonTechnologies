# Contagion — Virus C synthesizer

**FORM 8300 — the report filed when cash moves. Contagion is what spreads.** 63 HP.

An Access Virus C, running its own operating system — all of it. The Virus has
two processors. The sound comes from a Motorola DSP56362; everything else comes
from a Siemens 80C515 microcontroller: the front panel, the LCD, MIDI, the preset
memory. The 80C515 also boots the DSP and feeds it every note and parameter
change. Contagion runs both from the Virus's own flash image:

* **The 80C515 is emulated instruction by instruction**, its firmware untouched.
  It scans the knobs through its A/D converter and the buttons through its key
  matrix, drives the LCD, receives MIDI on its serial port, and talks to the DSP
  through the DSP's host port, as on the board.
* **The DSP56362 is dsp56300**, the emulator gearmulator's Osirus runs on
  (GPLv3). It runs the DSP code the 80C515 loads into it at power-on.

gearmulator, and every other Virus emulation, replaces the 80C515 with C++ written
to behave like it. Contagion does not, which is why the LCD shows what the real
unit shows, and why the knobs and buttons do what the firmware decides they do.

---

## Before anything works: the OS image

The Virus OS is Access's and is **not included**. You need the 512 KB flash image
of a Virus A, B or C (the `am29f040b` dumps). OS 5.5 and 6.6 are both known to
work. Right-click the module and choose **Load OS image…**. The module remembers
the last image that booted (in `MoonTechnologies/settings.json` in Rack's user
folder), so the next Contagion you add finds it by itself. A patch stores the
image's *path*, never its contents.

Loading the image powers the unit on. The firmware shows its version on the LCD,
boots the DSP, and comes up on program A0. The very first time, with empty
memory, you also see "Initialize Global Memory" and "Initialize Edit-Buffers":
the firmware setting up battery RAM it found blank, as a new unit does.

## Playing it

Choose a **MIDI input** in the context menu: the Virus is played over MIDI, as
the desktop unit is. Notes, controllers, program changes and SysEx go into the
80C515's serial port at 31,250 baud, and the firmware handles them.

### Playing it from cables

The unit only understands MIDI, so the jacks along the bottom turn patch cables into the
MIDI a keyboard would send. Nothing reaches the firmware but those bytes.

* **PITCH**, **GATE**, **VEL**: polyphonic. Each cable channel is a voice. A rising gate is a
  note-on at the pitch (0 V is middle C, 1 V/octave) and velocity (0-10 V; 100 if VEL is
  unpatched) read a sample later, so a sequencer that moves pitch and gate together is
  heard right. A pitch change under a held gate is a legato note. Unpatching the cable
  releases what it held.
* **BEND** (±5 V), **MOD** (0-10 V, CC 1), **AT** (0-10 V, channel pressure), **SUS**
  (gate, CC 64): sent when they move, and put back to rest when unpatched.
* **CLK**, **RUN**, **RST**: a clock is turned into MIDI clock (24 per quarter note),
  spread evenly across the interval between pulses; RUN sends Start and Stop, RST starts
  again from the top. With no RUN cable the first clock starts it. Set the unit's
  Global clock to Auto or MIDI for the arpeggiator and delay to follow.
* **Triggers are notes.** A gate that comes and goes in a sample or two still plays a note, and the
  context menu's **Minimum note length** (off, 25 ms to 2 s) holds each note at least that long
  before its gate can end it. The unit's envelopes only open and decay while a note is held, so a
  sequencer's one-millisecond trigger needs this to be heard; a gate longer than the minimum is
  untouched.
* The context menu sets the **MIDI channel**, whether polyphonic channel *n* plays
  channel *n* (to play the multi's parts), and how many clock pulses make a quarter note.

### CV and gates

* **CV 1-8** each move one of the 32 knobs, chosen in the context menu (starting as
  cutoff, resonance, pulse width, FM amount, soft knobs 1 and 2, effect mix and
  delay send) with an attenuverter: 10 V is the knob's full travel. CV adds to where the
  knob stands; several onto one knob add together. They reach the firmware as the knob's
  own A/D reading.
* **PRT -/+**, **PRM </>**, **VAL -/+**, **ARP**, **RND**: a gate presses that button, and
  presses it once per rising edge: a press is 45 ms down and 45 ms up, because the firmware's key scan needs that (measured: 10 or 20 ms gaps lose presses).

Outputs **1–3** are the Virus's three stereo pairs; the patch decides which
pair a part plays on (normally 1). **IN L/R** are its two audio inputs (for the
vocoder, the input-follower filters and so on). A signal only into IN L is
copied to IN R.

## The panel

The unit's 32 knobs are all here, grouped as the Virus groups them. A knob is read through
the 80C515's A/D converter as the hardware's pot would be, and the firmware only acts when it
moves. Turn one and the LCD shows the parameter, as on the Virus. A program with an edit in it
shows its number in lower case ("a0"). **STORE**, pressed twice, writes the edit into the user
banks.

**The knobs follow the sound.** On the unit the knobs are not motorised: load a program and they
stay where they were, and the first one you touch makes its parameter jump to the knob's position.
Here the module reads the sound the unit has loaded and moves each knob to match, on screen only.
The firmware is never told (a knob reaching it is a knob someone turned, which would mark the
program edited and put the parameter on the LCD), so a knob you touch afterwards starts from the
sound's value. This covers every knob that edits the sound, including the ones whose meaning
follows the selectors (LFO RATE for the selected LFO, SHAPE, SEMITONE and the rest for the
selected oscillator, TYPE/MIX and INTENSITY for the selected effect, RESO and ENV AMT for the
selected filter). **SOFT 1**, **SOFT 2** and **VOLUME** are not part of the sound and are left
alone, as is any knob with a CV patched to it. Single mode only; in MULTI the knobs stay put.
`tests/Contagion` checks the map and every knob's curve against the real firmware.

The unit's 35 buttons and 69 LEDs are not all on the face. The hardware has no encoders or
selectors, only buttons, and a modular face can do better: the minus/plus pairs are step buttons,
the cycles are lists you pick from, and most of them are on the display, where the unit shows
their answer. Every one of them still reaches the firmware as a press of the unit's own key, and
the answer comes back as the unit's own LEDs: nothing is emulated around the firmware.

### The display is the control surface

The read-out is one piece of glass, and everything on it is a control. Under the pointer a
control shows a highlight, and the pointer says how it works: up/down arrows over a value you
hold and drag, a hand over a word you click. Right-click any of them for the usual parameter
menu (typed entry, MIDI-Map, reset); Ctrl-Z undoes them like any knob.

* **PRESET** is the last program screen the unit showed, kept while its LCD is busy with a knob
  or a menu. **Click it** for the list of sounds: the bank, then sixteen at a time, each named as
  the unit's display names it (the same list as the context menu's **Presets**). Banks A and B
  are the unit's battery RAM, so a sound you stored shows under its own name; C and D are the
  factory copies of A and B, and E to H are the OS image's other banks. The names are read from
  your own OS image and battery RAM when the list opens.
* Beside it, three **step pairs**, up over down, named underneath: **PRESET** steps to the next or
  previous of all 1024 sounds, wrapping from bank H back to A. It sends MIDI (bank select, then
  program change, on the MIDI channel set in the context menu), so it works from any screen and
  starts from whatever the display shows. **CAT** and **IN CAT** are the unit's two-handed
  category gestures (below).
* **PARAMETER** is the unit's 2 x 16 LCD as it is now, with its step pairs: **PARAM** (PARAMETER
  </>: in play mode, bank), **VALUE** (VALUE -/+: in play mode, program) and **PAGE** (below).
* Under PRESET, **TRANSPOSE**: its step pair (TRANS -/+) and the unit's five octave lamps.
* Under PARAMETER, **BPM**: hold it and drag up or down (Ctrl finely, Shift coarsely). It sets the
  sound's clock tempo, 63 to 190. The unit keeps it in a menu (EDIT, then CLOCK) and has no knob
  for it; the module sets it with a SysEx parameter change to the edit buffer, the form the
  firmware answers, and it follows the tempo of whatever sound is loaded. The lamp beside it blinks
  at the tempo the unit is running. Following an external clock (the CLOCK jack, or MIDI clock)
  overrides it, as on the unit.
* **SELECTED** is the unit's selectors, a line each: **LFO** (LFO 1, 2, 3, MOD), **SHAPE** (sine,
  triangle, saw, square, wave), **OSC** (1, 2, 3: which oscillator the knobs edit), **EFFECT**
  (distortion, phaser, chorus: what TYPE/MIX and INTENSITY control), **FILT 1** and **FILT 2**
  (low-, high-, band-pass, band-stop), each position lit as the unit's LED is. **Click the
  positions** to list them and pick one, or hold and drag through them. The module presses the
  unit's key until its LEDs agree, then checks; if the unit will not go there the line returns to
  what the unit shows. Load a program and the lines follow it.
* The names **LFO**, **OSC** and **EFFECT** are their section's **EDIT** key: click one to press
  it. It lights while the unit's EDIT lamp for that section does. The delay/reverb, arpeggiator,
  filter and program EDITs are buttons on the face.
* **AMOUNT** lists what each LFO and the modulation matrix are routed to, lit while the amount is
  not zero and flashing while selected. **Click the list** to press the unit's AMOUNT key, which
  steps through the selected LFO's destinations; set the amount with **VALUE** or **SOFT 2/VALUE**.

A step pair's top half is one press up, its bottom half one press down; hold either to repeat, at
about eleven presses a second. The pairs bind the same parameters the panel's step buttons did,
so patches made before they moved are unchanged.

### On the face

**PART** stays a step pair on the face (PART -/+; both together call up the demo song, which this
OS image does not have: the unit shows "NO DEMOSONG but 1024 Sounds!" for a moment, so there is no
control for it). Each click is one press of the unit's key.

**Buttons**: **SYNC**, **OSC 3 ON**, **DLY/REV** edit, **ARP ON**, **ARP EDIT**, **EDIT**,
**GLOBAL** (global / multi edit), **RANDOM** (**UNDO** takes it back), **UNDO**, **STORE**,
**MULTI**, **SINGLE**, the filter **EDIT** and **SEL 1 / SEL 2** (which filter RESO and ENV AMT
act on). The two **RATE** lamps follow LFO 1 and LFO 2/3.

### Gestures that need two hands

**Gestures that need two keys at once, or one held while another is pressed**, which a mouse cannot
do, are controls that do it for you:

* **MULTI+SINGLE**, on the face, presses MULTI and SINGLE together, which puts the unit in
  Multi-Single mode (both LEDs lit). Pressed again it presses SINGLE alone, which leaves it, as the
  unit does.
* **CAT**, the step pair beside PRESET. A click holds SINGLE and presses PARAMETER once, which
  steps the category (Off, Acid, Arpeggiator, Bass, ...), and keeps SINGLE held for a second and a
  half after the last click so the category stays on the display; the category is kept when SINGLE
  is let go. **IN CAT** is the same with VALUE: with SINGLE held, VALUE steps to the next or
  previous sound *in the chosen category*, skipping the rest.
* **PAGE**, beside PARAMETER, scrolls the parameters a group at a time: a click up holds
  PARAMETER > and presses PARAMETER <, which jumps forward; a click down does the reverse. (The
  unit's own law is not a mirror: forward from CLOCK passes the whole COMMON group, back lands on
  the start of it.)

Holding EDIT while turning SELECT, which the manual gives as the way to choose a modulation
destination, needs no control: the LFO line presses SELECT for you and **AMOUNT** steps the
destinations, and the display shows the page each one lands on.

The LEDs are the 80C515's own multiplex, drawn at the brightness the firmware
drives them, so a flashing one flashes. The two RATE LEDs are not the
microcontroller's: the DSP drives those itself from its timers, and they are read
from there. The manual's two-button shortcuts work, because the firmware does them:
OSC EDIT + SYNC plays a note (audition), STORE + SINGLE sends a dump, ARP EDIT + ARP
ON holds the arpeggiator, and so on. Not every shortcut has a button to press now; the
**gate inputs** are one press of a button each.

**Master volume** is the VOLUME knob, as on the hardware. With it low the Virus
is quiet: the firmware sends the DSP whatever the knob says.

## Memory

The Virus keeps its global settings, edit buffers and user banks A and B in
battery-backed RAM. The patch saves it, about 160 KB. A stored program comes
back with the patch, and so do the global settings. **Clear battery RAM**
returns the unit to factory state: the firmware reinitialises its global memory,
and banks A and B go back to what the image holds.

## Cost

About a fifth of a core per instance on Apple Silicon. The DSP runs on its own
thread, the 80C515 on the audio thread. Rack has to allow JIT compilation, which
Rack 2 on macOS does.

## What is emulated, and what is assumed

Everything below comes from the firmware, by running it (`VirusResearch/NOTES.md`
in the research workspace has every step):

* the 12 MHz clock, from the MIDI baud-rate setting;
* code and data in 32 KB flash banks selected by port 5;
* the DSP's host port at $0400, selected only while P3.3 is low, with its
  interrupt request on INT0;
* the LCD in 4-bit mode on port 1, including the controller's busy time, without
  which the firmware's start-up sequence cannot work;
* the key matrix, the LED multiplex and the 32 pots on four groups of the A/D
  converter.

Assumed, because the firmware cannot say:

* that banks $40000–$5FFFF are RAM rather than flash. STORE writes them with
  plain writes and never with flash commands, and nothing else is ever written;
* the 80C515's A/D reading the exact pot position, with no noise.

There is no public schematic of the Virus.

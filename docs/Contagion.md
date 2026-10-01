# Contagion

**FORM 8300 — the report filed when cash moves. Contagion is what spreads.** 54 HP.

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

Outputs **1–3** are the Virus's three stereo pairs; the patch decides which
pair a part plays on (normally 1). **IN L/R** are its two audio inputs (for the
vocoder, the input-follower filters and so on). A signal only into IN L is
copied to IN R.

## The panel

These are the unit's own 32 knobs and 35 buttons. A knob is read through the
80C515's A/D converter as the hardware's pot would be, and the firmware only acts
when it moves. Turn one and the LCD shows the parameter, as on the Virus. A
program with an edit in it shows its number in lower case ("a0"). **STORE**,
pressed twice, writes the edit into the user banks. The labels are what the
firmware itself calls each control, checked against the owner's manual where it
draws the section. A few buttons are still named by what they do: **PAGE** (two
LFO edit pages), **SEL 1/SEL 2** (filter select), **RND SND**, and **2,6**,
whose meaning is not yet confirmed.

The LEDs beside the select buttons are the unit's: which LFO, oscillator and
effect is selected, the filter modes, and Sync, Osc 3, Arp and the effect
buttons. They are drawn from the 80C515's own LED multiplex. The Virus has more
LEDs than this panel shows; the others are not yet mapped.

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

# Nordic Banking

**FORM 8938 — the statement of foreign financial assets. Nordic banking is where the money goes to be discreet.** 52 HP.

A Clavia Nord Lead 2X, running its own operating system. The 2X has a Motorola MC68331
microcontroller and two DSP56362 signal processors. The 68331 owns the front panel, MIDI
and program memory; it boots the DSPs and tells them what to play. Nordic Banking runs both
from the 2X's own OS image:

* **The MC68331 is emulated instruction by instruction** (Musashi), with the chip's own
  timer, serial and system-integration modules. It scans the knobs through the panel's
  converter and the buttons on their key lines, drives the LEDs and display, receives MIDI
  on its serial port, and keeps the programs in the 64 KB flash.
* **The two DSP56362 are dsp56300**, the emulator gearmulator's Nodal Red 2x runs on
  (GPLv3). DSP A computes half the voices and passes them to DSP B, which adds its own and
  drives the four outputs at 98.2 kHz, as on the board.

The machine is gearmulator's Nord Lead 2X (its `n2xLib`), vendored with the few changes
`vendor/README.md` lists. Nordic Banking adds the front panel: gearmulator drives the
synth over MIDI and never models the panel's buttons or LEDs.

---

## Before anything works: the OS image

The OS is Clavia's and is **not included**. You need the 512 KB OS image of a Nord Lead 2X
(one image has been tested; it carries the 2X's own signature, which the module checks). The original Nord Lead 2 will not run here: it has four of the older
DSP56002s, not two 56362s, and nothing emulates those yet. Right-click the module and choose
**Load OS image…**. The module remembers the last image that booted (in
`MoonTechnologies/settings.json` in Rack's user folder), so the next Nordic Banking you add
finds it by itself. A patch stores the image's *path*, never its contents.

Loading the image powers the unit on; about a second later the display shows the first
program, **1**.

## Programs

The 2X keeps its programs in a 64 KB flash chip, not in the OS image. A new Nordic Banking
starts with that flash **erased**, as a unit fresh from the factory line would: every
program is blank. Clavia publishes the 2X's factory programs as SysEx (nordkeyboards.com,
the Nord Lead 2X's legacy downloads); choose **Send SysEx file…** and the firmware receives
them exactly as it would from a sequencer. **STORE** writes the current program into the
flash, and the patch saves the flash, so your programs travel with it. **Erase program
memory** clears it again.

## Playing it

Choose a **MIDI input** in the context menu: the 2X is played over MIDI, as the rack unit
is. Notes, controllers, program changes and SysEx go into the 68331's serial port, and the
firmware handles them; bend and the mod wheel arrive that way too. **PEDAL** is the
expression pedal input (0–10 V). **OUT A–D** are the four outputs; the program's output
mode decides what goes where (normally A and B, a stereo pair).

## The panel

These are the unit's own 26 knobs and 28 buttons, grouped as the manual draws them. A knob
is read through the panel's converter as the hardware's pot would be. A button press is the
key line going low. The LEDs and the three digits are one multiplex the firmware refreshes
slot by slot, and each is drawn at its share of the refresh, so a flashing LED flashes and
the half-lit decimal points stay half lit.

The selectors light one LED, or a pair of neighbours for the setting between them, as on
the unit:

* **LFO 1 WAVE**: soft random, triangle, random; the top two lit is square, the bottom two
  sawtooth. **DEST**: FM, OSC 2, PW; FM + OSC 2 is OSC 1+2, OSC 2 + PW is the filter.
* **LFO 2 DEST/MODE**: with **ARP** lit, the arpeggiator modes (ECHO, UP, DWN; the pairs
  RND and U&D); without it, LFO 2's destinations (OSC 1+2, AMP, FILTER).
* **MOD ENV DEST**: FM, OSC 2; both is PW; neither is none.
* **FILTER TYPE**: HP 24, LP 24, LP 12; HP + LP 24 is band-pass, LP 24 + LP 12 notch + LP.
  **KBD TRACK**: 1/3, 2/3; both is full.
* **SHIFT/WHEEL** sets the mod wheel's destination: MORPH, OSC 2, FILTER; MORPH + OSC 2 is
  LFO 1, OSC 2 + FILTER is FM. Held, it is SHIFT, for every second function the manual lists
  (MIDI channel on UNISON, demo on RING/SYNC, dumps on the OCT buttons, and so on).
* **RING/SYNC** steps off, sync, ring mod, both.

### What this firmware does not light

Two LEDs on the unit's panel are drawn here but never light: OSC 1's **sine** and KBD
TRACK's **2/3**. Selecting either changes the sound, but no position of the multiplex the
firmware drives answers for it; every one of its 48 positions was checked. Either this OS
drives them by some means not found yet, or the image behaves this way on the hardware too.
One multiplex position (row 1, bit 4) was never seen lit, and the unit has one key line
(gearmulator's "Trigger") whose control is not identified; it is not on this panel.

## Cost

About a quarter of a core on Apple Silicon: the 68331 and each DSP run on threads of their
own. Rack has to allow JIT compilation, which Rack 2 on macOS does.

## Sources

Everything above was found by running the firmware (`NordLead2Research/NOTES.md` in the
research workspace): pressing every button and setting every selector over MIDI while
watching the 68331 write the panel, and reading the firmware's own refresh routine and LED
buffer. Names and grouping are the owner's manual's (Nord Lead 2X manual v1.0, chapter 8).

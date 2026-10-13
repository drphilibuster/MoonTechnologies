# Payment Schedule — drum sequencer

FORM 1040-V. Slug `PaymentSchedule`. 19 HP.

An eight-track drum sequencer whose screen is the grid, in the line of Voxglitch's
VX Drum Sequencer, and taken further:

* Every track has its own **length**, up to 64 steps, and its own **rate** against the
  clock.
* Every step holds six values: **gate**, **velocity**, **chance**, **ratchet**, a
  **micro-timing offset** and a free **CV**.
* There are **sixteen pattern memories**.

Each track has a trigger output and a held velocity. The triggers and the CV lanes also
come out as eight-channel cables.

> Until 2026-10 Payment Schedule was the Modular in a Week Day 10 board: a Baby8, a
> 4031 tap looper and a varimode quantizer. A drum sequencer replaced it. See
> [the change](#what-happened-to-the-baby8) at the end.

## The screen

**Top row: what the grid shows, and the transport.**

* **GATE · VEL · CHNC · RTCH · OFFS · CV** pick which of a step's values the grid
  shows and edits.
* **RUN** starts and stops the sequencer. A cable in the **RUN** jack takes over:
  high runs, low stops.
* **RST** rewinds, so the next clock plays step 1.

**Second row: the pattern and the selected track.** Each field is a control: drag a
value up or down, or click a choice to pick from a list.

* **PAT**: the pattern, 1–16. A new pattern takes over at the next bar (16 clocks).
  While it waits, the field reads `3>5`. The menu can make it change on the next clock
  instead. A cable in **PATT** chooses the pattern (0–10 V across 16) and the field
  follows it.
* **TRK**: the selected track. Clicking a track's number on the grid also selects it.
* **LEN**: the selected track's length, 1–64.
* **RATE**: the selected track's steps per clock:
  * faster: ×4, ×3, ×2;
  * a 3:2 ratio: ×3/2, three steps over two clocks;
  * once a clock: 1;
  * slower: /2, /3, /4, /6, /8, /16.
* **STEP**: which sixteen of the 64 steps are on screen.
* **SWG**: swing, 50 % (straight) to 75 %. Every other step of every track is
  pushed late by that share of a pair of steps.

The screen edits the pattern shown in **PAT**. That is the one asked for, so you can
write the next pattern while the current one plays. The playhead is drawn while the
pattern shown is the one playing.

## The grid

Each row is a track. On the left is its number, and its **M** box, which mutes it.
Mutes are kept with the patch, not with a pattern. Columns past a track's length are
dots: shortening a track hides its steps but does not delete them.

**GATE view**

* Click a step to set or clear a hit, and drag across to paint. One drag is one undo
  step.
* A lower chance draws a hit dimmer.
* Ratchets show as notches across the pad. **Shift-click** a hit to step its ratchet
  ×1 → ×2 → ×3 → ×4.

**Value views (VEL, CHNC, RTCH, OFFS, CV)**

* Each hit is a bar. Drag up and down to set it, and across to set the row's other
  steps to where the mouse is.
* Setting a value on an empty step makes it a hit. CV is the exception: a CV lane can
  hold a value on a rest.
* OFFS is drawn from a centre line: above is late, below is early.

| Lane | Range | What it does |
|---|---|---|
| VEL | 1–127 | The track's **VEL** output is held at vel / 127 × 10 V from each hit. |
| CHNC | 0–100 % | Rolled once per step. A failed roll loses its ratchets too. |
| RTCH | 1–4 | Hits evenly spaced through the step. |
| OFFS | −50…+50 % | Moves the step's hits that share of a step, early or late. |
| CV | 0–127 | Held at **CV** (channel = track) from each hit, 0–10 V. |

**Right-click a step** for its ratchet and chance, *Clear this step* and *Set the
track's length to end here*.

**Right-click a track's number** for:

* mute;
* rotate one step either way, within its length;
* randomize;
* clear.

## Time

**CLOCK** is the only time source. Each rising edge (2 V, back below 0.1 V) is one
clock. At rate 1 a track plays a step each clock, so patch sixteenths.

* **Faster tracks.** ×2, ×3, ×4 and ×3/2 place their extra steps by the *measured*
  clock: the gap between the last two edges.
* **Ratchets, offsets and swing** are also fractions of the step, so they need that
  measurement.
* **Until two edges have been seen**, all of these play straight and single, as VX
  does.
* **Early hits.** A negative offset plays before its step. It is queued one step
  ahead, when the step before it starts.
* **Reset.** **RESET** (or **RST**) means the next clock plays step 1. A reset and a
  clock on the same sample play step 1 then.
* **Stopping.** While stopped, the clock is ignored, and a pattern change takes effect
  at once.

## Outputs

| Jack | |
|---|---|
| **TRIG 1–8** | Each hit is a 10 V trigger, 1 ms by default (menu: 1–200 ms). A hit that lands on a trigger still high drops it for a sample first, so every hit has its own edge. |
| **VEL 1–8** | Velocity of the track's last hit, 0–10 V, held. |
| **TRIGS** | All eight triggers on one cable, channel n = track n. |
| **CV** | All eight CV lanes on one cable, held from each hit. |
| **EOC** | A trigger each time track 1 comes back round to its first step. |

## Patterns

The context menu acts on the pattern shown:

* **Copy** and **Paste** use the system clipboard as text, so a pattern moves between
  instances and patches.
* **Randomize** writes a beat by role:
  1. kick;
  2. snare with ghosts;
  3. closed hats, with velocity and the odd ratchet;
  4. open hat;
  5–8. sparse percussion.
* **Rotate** moves the pattern one step either way.
* **Clear** empties it.

A new module starts with a beat in pattern 1. Every edit is in Rack's undo history.

## Voltage conventions

Triggers and gates are 0/10 V. The **CLOCK**, **RESET** and **RUN** inputs go high at
2 V (RUN at 1 V) and low below 0.1 V. **PATT** is 0–10 V across the sixteen patterns.

## Building

Payment Schedule ships inside the Moon Technologies plugin; see
[BUILDING.md](BUILDING.md).

* The engine is `src/PaymentSchedule/DrumSeq.hpp`, with no Rack dependency.
* `tests/PaymentSchedule/test_drumseq.cpp` drives it with trigger and square clocks
  and reads the trigger outputs, covering:
  * step timing;
  * reset;
  * lengths and every kind of rate;
  * ratchets, offsets in both directions, and swing;
  * chance statistics;
  * pattern changes at the bar;
  * mutes, stopping and retriggering;
  * saving.

## What happened to the Baby8

Until 2026-10 this module was the Modular in a Week Day 10 board, fused onto one
clock:

* a Baby8 (a 74HC4017 counter);
* a sequential switch;
* a CD4031B tap looper;
* a PIC16F684 running a varimode quantizer's firmware.

It was replaced by the drum sequencer above. The slug stayed, so patches that used the
old module still load: their parameters are set back to the defaults and they get the
starter beat. Nothing else of the old patch is used.

The circuit models are still in the tree, still built and tested, but no module uses
them:

* `src/PaymentSchedule/Sequencer.hpp`, `src/Cd4017.hpp` and `src/Cd4031.hpp`;
* the PIC emulator and its firmware: `src/Pic16f684.hpp`,
  `src/PaymentSchedule/Varimode*.hpp`, `firmware/varimode`, `tests/Pic16`.

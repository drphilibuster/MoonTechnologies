# Apportionment

**FORM 1116 — foreign tax credit, the form on which income is apportioned among categories.** 34 HP.

An Ensoniq DP/4 parallel effects processor, running its own operating system.
Four Ensoniq ESP (ES5510) signal processors, one Motorola 68B03 host, and the
real firmware in charge of both: every algorithm, preset, Config and System page
is the DP/4's own, because nothing here re-implements them. The module emulates
the board and lets the firmware do what it did in 1993.

What the module adds is the one thing the hardware hid behind menus: the
**routing**. On a DP/4 the way the four units are wired to each other and to the
jacks is a *Config* — a source count and a handful of parameters on pages you
reach through EDIT, CONFIG and the arrow keys. Here every one of those parameters
is a control on the panel.

---

## Before anything works: the EPROMs

The DP/4's firmware is Ensoniq's and is **not included**. You need the two EPROM
images from a DP/4 (or a DP/4+ running the DP/4 OS): the **OS** (32 KB, U56) and
the **UCODE** (128 KB, U54). Dumps of versions 1.06 and 1.15 are both known to
work, but **use 1.15**: it is the version the DP/4+ manual documents, and 1.06
sounds different in six of the fifty Config presets. A bug in 1.06 collapses
Config 41 "Box Room LongDDL"'s 3.3-second delay to almost nothing, and 1.06's
pitch shifter lacks the regen damping 1.15 added (Configs 1, 7, 17, 35 and 46
ring differently). The module reads the version off the firmware's own boot
screen: anything older than 1.15 shows **OLD OS** on the display for a few
seconds after power-on, and the context menu says what differs.

Right-click the module and choose **Load EPROM folder…** and point it at a folder
holding both files: it tells them apart by size. **Load OS EPROM…** and **Load
UCODE EPROM…** load them one at a time. The module remembers the last pair that
booted (in `MoonTechnologies/settings.json` in Rack's user folder), so the next
Apportionment you add finds them by itself. A patch stores the *paths* to the
EPROMs, never their contents.

Until both are loaded the display says so and the module is silent. Loading
them powers the machine on; the DP/4 takes about three seconds of machine time
to boot, which the module runs off the audio thread in about a second.

---

## The display

The read-out well is the DP/4's own front panel, drawn from the byte stream the
firmware sends to it: the **2 × 16 LCD** (a flashing field is the one the knob
will change), the **two-digit LED** (parameter number; the right-hand point is
the MIDI light) and, on the right, a **map** of the routing the machine is
actually running — inputs on the left, the four units as the DP/4 pairs them
(A–B above, C–D below), outputs on the right. Feedback is drawn as an arc from
the second unit of a pair back to the first.

## FRONT PANEL

The DP/4's controls, button for button, numbered as the service manual numbers
them. Everything in the DP/4 manual can be done here.

| control | on the DP/4 |
|---|---|
| **A B C D** | unit buttons; lit as the firmware lights them. Pressing an active unit's button again **bypasses** it, as on the DP/4; the small light beside its name is that unit's red bypass LED |
| **CONFIG**, **SYSTEM**, **EDIT** | Config, System•MIDI, Edit•Compare |
| **SELECT**, **<**, **>**, **CANCEL**, **WRITE** | Select, the parameter arrows, Cancel•Undo, Write•Copy |
| **A B/K … D B/K** | what bypass does to each unit — the Config's bypass/kill page. **B** (down) passes the dry signal through a bypassed unit; **K** (up) mutes it. Like the CONFIG controls, moving one makes the module set it on the firmware's own page, and it follows the firmware back |
| **DATA** | the big knob. It is endless, as on the hardware: drag it (up or right turns it up) or use the scroll wheel. It reports detents, not a position, so it has no value to reset |

## CONFIG — the routing

Each control is one Config parameter. Moving one makes the module work the
Config pages for you — EDIT, CONFIG, the arrows and the data knob, at a human
pace — until the firmware's own copy of that parameter says what the control
says, then it returns to Select. The light beside the caption is on while it is
working (a source-count change rebuilds all four ESP programs and takes a couple
of seconds). The firmware builds every routing itself; the module only asks.

The controls also follow the firmware back. Select a Config preset, or change a
Config parameter on the DP/4's own pages, and the controls move to match.

| control | parameter | exists with |
|---|---|---|
| **SOURCES** | input configuration: 1 (1,2 > ABCD), 2 (12 > AB, 34 > CD), 3 (1 > A, 2 > B, 34 > CD), 4 (one input per unit) | always |
| **A-B** | how A and B are joined: serial, parallel, feedback 1, feedback 2 | 1, 2 sources |
| **C-D** | the same for C and D | 1, 2, 3 sources |
| **AB>CD** | whether the AB pair feeds the CD pair (serial) or runs beside it (parallel) | 1 source |
| **AB AMT** | 0-99: the dry path around A-B when serial, the B-to-A feedback when feedback | 1, 2 sources |
| **CD AMT** | the same for C-D | 1, 2, 3 sources |
| **AB IN** | stereo (1, 2) or mono (1) into A-B | 1, 2 sources |
| **CD IN** | stereo (3, 4) or mono (3) into C-D | 2, 3 sources |
| **AB OUT** | A > 1 and B > 2 as dual mono, or both mixed to 1-2 in stereo | 3, 4 sources |
| **CD OUT** | C > 3 and D > 4 as dual mono, or mixed to 3-4 | 4 sources |
| **IN LEVEL**, **OUT LEVEL** | the rear-panel level pots, all four channels at once | — |

A control whose parameter does not exist in the current source count is left
where it is and has no effect until it does.

Feedback 1 and feedback 2 differ only in how the dry signal is mixed into the
wet one; see the DP/4 manual's Config section.

## The jacks

| jack | |
|---|---|
| **IN 1-4** | the four inputs, ±5 V to full scale at IN LEVEL 100 %. As on the DP/4's own jacks, IN 2 follows IN 1 when nothing is plugged into it, and IN 4 follows IN 3 |
| **PEDAL** | the CV pedal, 0-10 V; unplugged reads as "no pedal", as on the hardware |
| **FS L**, **FS R** | the two footswitches, as gates (above 1 V is pressed) |
| **OUT 1-4** | the four outputs. As on the DP/4: with OUT 3 unplugged, 3/4 are mixed onto 1/2, and with OUT 2 (or OUT 4) unplugged, its pair is summed to mono on OUT 1 (or OUT 3) |
| **TAP A-D** | each unit's own output port, stereo on a two-channel polyphonic cable (A and C: their SER3 port; B and D: their SER1 port, which carries the pair's result). Use them to take a unit out on its own, or to patch the units into each other and the rest of the rack beyond what the Configs offer |

## The battery

The DP/4's 32 KB of battery-backed RAM — your presets, Configs and System
settings — is saved in the patch, as the battery would keep it. **Reinitialize**
in the context menu clears it: the firmware reinitialises and reloads its ROM
presets, as it does after a battery change. **Power cycle** restarts the machine
with its memory intact.

## How faithful is it

The firmware and the DSP code are the real ones, so the algorithms, presets and
Configs cannot drift from the hardware's. The emulation around them was
reconstructed from the firmware and Ensoniq's ESP specification, without a
schematic:

* **The DSP rate is 34.875 kHz.** At that rate the service-mode "1 kHz" test
  tone measures exactly 1 kHz. The module resamples to and from Rack's rate.
* The ESP core is MAME's, with fixes taken from the ESP specification: step 0
  of every program runs on every pass; halting empties the DOL FIFO; a skipped
  step's fate is decided as its results are written, so a condition mask set by
  one step governs the next (without this the programs' noise gates never
  opened and sustained sound went silent); the host's table loads are
  left-justified, so the compressors find their gain tables; each ESP has the
  64K words of delay memory the firmware configures it for; and host writes to
  a running ESP land once per sample period with the handshake the firmware
  polls for.
* The wiring between the four ESPs, and the C/D input switch the firmware sets
  per Config, were read off the I/O code of all fifty ROM Config presets.
* Analog stages (converters, the rear-panel pots, the jack switching) are
  modelled by what they do, not how.

The details are in the research notes this module was built from.

# Audit Logic — logic, switches & divider

16 HP. FORM 886-A, the IRS's "Explanation of Items" -- the form an examiner
attaches a finding to. Audit Logic consolidates three *Modular in a Week*
boards into one panel, laid out as three felt blocks read top to bottom:

* **FINDINGS** -- the Quad Inverter, the Hex Inverter and the Quad Logic
  Module (Day 8: a 4071 quad-OR IC with diode input steering and two TL074
  comparator stages). Four selectable two-input logic gates.
* **REFERRAL** -- the 4066 Quad Gated Switch (Day 10). Two gated analogue
  switches.
* **INSTALLMENTS** -- the Emiz Instruments CV2 clock divider (Day 8). One
  free-running binary counter, six taps.

Credit: *Modular in a Week* / Kristian Blåsol (Quad Inverter, Hex Inverter,
Quad Logic Module, 4066 Quad Gated Switch), and Emiz Instruments (the CV2
clock divider). Every function here is a reinterpretation for VCV Rack, not a
1:1 port -- see "What was approximated" below for exactly where and why.

## The read-out

Every setting on the panel is a word on the glass under the masthead, and the
word is the control. The top line is the four gates' functions -- **GATE 1**
to **GATE 4** -- each a choice from a list: click it to pick one of the seven,
or hold and drag to step through them. The bottom line is the four switches the
whole panel shares, each showing its state: **POLARITY** (HI ON / LO ON),
**REF V** (0 V / 12 V), **ROUTE** (A-B / A-B/A-C) and **MODE** (BINARY /
MUSICAL). Click one to flip it (Ctrl-click flips it back the other way, which
on a two-way switch is the same thing). Right-click any of them for the usual
parameter menu -- MIDI-Map, reset, and the setting's full name.

## FINDINGS

Four independent two-input logic gates, each polyphonic (up to 16 channels,
following whichever of its A/B inputs carries more).

Per gate:

* **A**, **B** -- inputs. Gates 0/10 V; any voltage works, but the Schmitt
  comparator (rising 2 V, falling 0.1 V -- Rack's usual gate hysteresis, not
  the CD40106 hex Schmitt inverter's own 1 V falling threshold, even though
  that chip is literally what the original Hex Inverter board built its gates
  from) reads anything above 2 V as true and anything below 0.1 V as false,
  with the last state held in between.
* **Function** -- on the glass, as that gate's word on the top line: Invert
  A, AND, OR, XOR, NAND, NOR, XNOR (shown as NOT A, AND, XOR and so on).
  Invert A ignores B entirely. Default per gate is Invert A / AND / OR / XOR,
  left to right -- the order the plugin's own registered description lists
  them in, which is the closest thing the surviving Quad Logic Module
  schematic offers to a documented per-channel assignment. NAND, NOR and XNOR
  are additional selections this module adds; treat them as this build's
  extension, not a claim about which diode network the original board used.
* **OUT** -- 0/10 V gate output (mint-inked).

Gates 1 and 2 share the first row of jacks, 3 and 4 the second, each reading
A, B, OUT.

Between **A** and **B** sits that gate's own status light, lit MINT while its
output is currently high -- the per-gate counterpart to VERDICT below.

An unpatched A or B does not float: it reads **REF V**, on the glass
(between **POLARITY** and **ROUTE**, described below), 0 V or 12 V,
exactly as the original Quad Inverter's own "Trigger Voltage" switch set the
reference for its unpatched "in a" pins.

The **VERDICT** light beside the section caption is lit whenever any gate's
channel 0 is currently true.

### Why it is the width it is

The panel used to be height-bound: four gates side by side, each with a function
knob and its plate under it, and a row of switches over REFERRAL, filled the
face, and laying the gates out one per row needed more rows than a 3U face
holds. With the functions and the switches on the glass, the knob row and the
switch row are gone, and that height is what lets the gates fold into two rows
of two and the switch channels into one row each. The width is now set by the
FINDINGS rows and the six divider taps.

## REFERRAL

Two mono 4066-style gated analogue switches. The original board ganged four
switches off one quad-bilateral IC; this keeps two channels so FINDINGS and
INSTALLMENTS have panel room. Per channel:

* **1A** / **2A** -- input.
* **1G** / **2G** -- gate input, lit LIME while that channel is passing
  signal. Comparator: high >= 1 V, low < 0.1 V (general Eurorack gate
  convention, with hysteresis).
* **1B**/**1C**, **2B**/**2C** -- outputs.

**POLARITY** (shared by both channels here; on the board each of the four channels
has its own toggle) chooses whether the channel is on while its gate
is high (**Hi On**) or low (**Lo On**).

**REF V**, between **POLARITY** and **ROUTE** on the glass, is not part of either
gated switch: it is FINDINGS' reference for an unpatched A or B input, 0 V or
12 V, exactly as the original Quad Inverter's own "Trigger Voltage" switch set
the reference for its unpatched "in a" pins. It stands with them because it
is one setting shared across all four FINDINGS gates, the same way POLARITY
and ROUTE are each one setting shared across both REFERRAL channels.

**ROUTE** chooses the topology: **A-B** passes A to B while the channel is on
and leaves C at 0 V -- a plain gated switch, matching the original board.
**A-B/A-C** turns the same channel into a two-way router: A still goes to B
while on, and now also goes to C whenever the channel is *off* -- a
complement a bare pass-gate does not need, but that a 1-to-2 CV router does.
This mode is this module's own addition.

The **1 ms declick crossfade** (context menu, off by default) ramps the
on/off transition over about a millisecond instead of switching instantly.
Off is the panel's native character: an instant, and audibly clickable,
bilateral switch -- exactly what the 4066 does with no debounce network
around it. Turn the option on for a quieter switch at the cost of that
character.

**ACTIVE** (section caption light) is lit while either channel is passing
more than a trace of signal.

## INSTALLMENTS

A CD4024B seven-stage ripple counter (`src/Cd4024.hpp`), the chip the Emiz board is
built on. It counts on the **falling** edge of **CLOCK** (Schmitt levels: rising 2 V,
falling 0.1 V), as the datasheet and the board's own note ("all divisions happen on the
falling edge") say, so the first /2 gate goes high after the first clock has come and gone,
not on its rising edge. **RESET** is a level: while it is high the counter is held at zero
and the clock is ignored. Six gate outputs tap it, Q1..Q6 (the chip's Q7, /128, has no jack): **/2 /4 /8 /16 /32 /64**, each lit MINT while high.

**MODE**, the last word on the glass, chooses the division set. In
**BINARY** mode (its default) each tap is one bit of the counter, which is why
the duty cycle is exactly 50 % on every one of them: a single bit toggles at
exactly half its own period, always.

In **MUSICAL** mode the same six jacks are retuned to thirds instead of
halves -- **/3 /6 /12 /24 /48 /96** -- rather than doubling the panel with a
second row of outputs. The panel's own silkscreen still reads /2../64 in both
modes; this is a deliberate simplification, disclosed here the way
Retroactive discloses its own output latency in its manual rather than in
silkscreen that would have to change at runtime. Because /3 (and, doubled,
/6) has an odd tap count, its gate cannot split into an exact half; it is
high for 2 of every 3 counts (66 % duty) rather than 50 %. Every other
musical tap (/6, /12, /24, /48, /96 by their doubled/quadrupled counts) is
even and so is an exact 50 % duty gate, same as binary.

In MUSICAL mode the thirds run from a second counter that follows the same falling edges
and the same RESET, because the chip itself wraps at 128 and /3../96 do not divide it.

**CLOCKED** (section caption light) is lit while a cable is patched into
CLOCK.

## Footer

**CLOCK**, **RESET** -- the divider's own transport, shared with nothing else
on the panel. Everything else the panel needs shared across sections --
**POLARITY**, **REF V**, **ROUTE** and **MODE** -- is on the glass, described
above.

## What was approximated or left out

* **REFERRAL is two channels, not four.** The original 4066 Quad Gated
  Switch board used one quad-bilateral IC for four independent channels
  (silkscreened 1A/1B .. 4A/4B, Gate 1..4). This panel keeps two so FINDINGS
  and INSTALLMENTS both fit the panel; POLARITY and ROUTE are one setting shared by
  both channels. **That is a panel economy, not the board's way:** the Day 10 schematic
  ("Quad gate controlled switch", 2020-03-16) gives each of the four channels its own
  Hi/Lo polarity toggle (L1-L4). Each channel is a TL074 comparator (the gate against about
  1.09 V, a 100k / 10k divider from VCC) through a 1N4448 and 1k to the HEF4066's enable
  pin, or, with the toggle the other way, through a BC549 inverter with a 1k pull-up. An
  LED per channel shows the connection. ROUTE (A-B/A-C) is this module's own. The
  HEF4066BT is supplied from VCC (12 V) with VSS on ground, so the switch only passes
  0 to +12 V: the negative half of a bipolar signal is clipped, a property the ideal
  switch here does not have (and its on-resistance, tens to a hundred ohms at 12 V, is
  not modelled either).
* **FINDINGS' gates are not what the Quad Logic Module has.** That board's schematic is in the
  course folder (Day 8, "MiaW Quad Logic Module") and it is **four OR gates**: eight TL074
  comparators, each comparing one input against about 1.1 V (a 100k / 10k divider from +12 V),
  each through a 1N4448 and a 2k7 pull-down into a 4071's inputs, in pairs; each OR gate drives
  a BC547C emitter follower and an output jack and LED. There is no inversion, AND or XOR on
  that board. The module's per-gate functions -- Invert A, AND, OR, XOR by default, NAND, NOR
  and XNOR besides -- are this module's own, kept from the plugin's registered description (the
  Quad Inverter and Hex Inverter boards are the source of the inverter). The OR is on the
  board; the rest are additions. The comparators' threshold (1.1 V, not Rack's 0.1/2 V
  hysteresis) and the buffered output levels are not modelled. The board's outputs are BC547 emitter followers
  giving under 5 V; the jacks here stay at 10 V gates.
* **MUSICAL division reuses the printed /2../64 jacks** rather than adding a
  second bank of six outputs for /3../96; see INSTALLMENTS above. /3 and /6
  are consequently ~66 % duty rather than 50 %, an unavoidable consequence of
  an odd tap count, not a bug.
* **No state is written to the patch beyond the panel controls themselves.**
  The divider's counter phase and the switches' crossfade position are
  transport-like DSP state, the same category Retroactive's own window phase
  falls into, and both simply restart at zero on patch load like any other
  running clock.

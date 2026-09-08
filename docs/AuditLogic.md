# Audit Logic

29 HP. FORM 886-A, the IRS's "Explanation of Items" -- the form an examiner
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

## FINDINGS

Four independent two-input logic gates, each polyphonic (up to 16 channels,
following whichever of its A/B inputs carries more).

Per gate:

* **A**, **B** -- inputs. Gates 0/10 V; any voltage works, but the Schmitt
  comparator (rising 2 V, falling 1 V -- the CD40106 hex Schmitt inverter's
  own thresholds, since that chip is literally what the original Hex Inverter
  board built its gates from) reads anything above 2 V as true and anything
  below 1 V as false, with the last state held in between.
* **FN** -- the gate's function: Invert A, AND, OR, XOR, NAND, NOR, XNOR.
  The knob is unlabelled and wears a small lit plate instead, which the module
  writes the current function into -- NOT A, AND, XOR and so on. Seven meanings
  cannot be engraved on one knob, and "FN" told you the knob's job but never
  its setting.
  Invert A ignores B entirely. Default per gate is Invert A / AND / OR / XOR,
  left to right -- the order the plugin's own registered description lists
  them in, which is the closest thing the surviving Quad Logic Module
  schematic offers to a documented per-channel assignment. NAND, NOR and XNOR
  are additional selections this module adds; treat them as this build's
  extension, not a claim about which diode network the original board used.
* **OUT** -- 0/10 V gate output (mint-inked).

Between **A** and **B** sits that gate's own status light, lit MINT while its
output is currently high -- the per-gate counterpart to VERDICT below.

An unpatched A or B does not float: it reads **REF V**, the switch in
REFERRAL (between **POLARITY** and **ROUTE**, described below), 0 V or 12 V,
exactly as the original Quad Inverter's own "Trigger Voltage" switch set the
reference for its unpatched "in a" pins.

The **VERDICT** light beside the section caption is lit whenever any gate's
channel 0 is currently true.

### Why it is 25 HP and not narrower

Laid out vertically -- one gate per row rather than four side by side, and the
two switch channels stacked -- this panel measures **18 HP**. It is not built
that way because it does not fit: vertical needs eight section rows and a 3U
face holds about six, so it overruns by 44 mm. The width only drops if both
FINDINGS and REFERRAL go from eight columns to four, and that is precisely what
costs the rows.

The module is height-bound, not width-bound. 25 HP is close to its floor for
this much content.

## REFERRAL

Two mono 4066-style gated analogue switches. The original board ganged four
switches off one quad-bilateral IC; this keeps two channels so FINDINGS and
INSTALLMENTS have panel room. Per channel:

* **1A** / **2A** -- input.
* **1G** / **2G** -- gate input, lit LIME while that channel is passing
  signal. Comparator: high >= 1 V, low < 0.1 V (general Eurorack gate
  convention, with hysteresis).
* **1B**/**1C**, **2B**/**2C** -- outputs.

**POLARITY** (shared by both channels, as the original's own jumper was one
setting for the whole board) chooses whether the channel is on while its gate
is high (**Hi On**) or low (**Lo On**).

**REF V**, centred between **POLARITY** and **ROUTE**, is not part of either
gated switch: it is FINDINGS' reference for an unpatched A or B input, 0 V or
12 V, exactly as the original Quad Inverter's own "Trigger Voltage" switch set
the reference for its unpatched "in a" pins. It lives on this block because it
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

One free-running binary counter, incremented on every **CLOCK** rising edge
(comparator: rising 2 V, falling 1 V) and zeroed by **RESET**. Six gate
outputs tap it: **/2 /4 /8 /16 /32 /64**, each lit MINT while high.

**MODE**, at the end of the tap row past **/64**, chooses the division set. In
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

**CLOCKED** (section caption light) is lit while a cable is patched into
CLOCK.

## Footer

**CLOCK**, **RESET** -- the divider's own transport, shared with nothing else
on the panel. Everything else the panel needs shared across sections --
**REF V** and **MODE** -- lives in REFERRAL and INSTALLMENTS respectively,
described above.

## What was approximated or left out

* **REFERRAL is two channels, not four.** The original 4066 Quad Gated
  Switch board used one quad-bilateral IC for four independent channels
  (silkscreened 1A/1B .. 4A/4B, Gate 1..4). This panel keeps two so FINDINGS
  and INSTALLMENTS both fit at 29 HP; POLARITY and ROUTE stay one shared
  setting for both channels, matching how the original's own jumpers were one
  setting for its whole board rather than per-channel.
* **FINDINGS' default function per gate is a documented best guess, not a
  traced schematic.** The Quad Logic Module's surviving BOM (a 4071 quad-OR,
  two TL074 comparator packages, eight steering diodes and four transistors)
  implies real per-channel diode logic ahead of a shared OR gate, but tracing
  which channel got which function from a component list and an unlabelled
  schematic sheet was not reliable enough to assert as fact. The order this
  module defaults to -- Invert A, AND, OR, XOR -- follows the plugin's own
  registered description; NAND, NOR and XNOR are offered as additional
  selections rather than claimed history.
* **MUSICAL division reuses the printed /2../64 jacks** rather than adding a
  second bank of six outputs for /3../96; see INSTALLMENTS above. /3 and /6
  are consequently ~66 % duty rather than 50 %, an unavoidable consequence of
  an odd tap count, not a bug.
* **No state is written to the patch beyond the panel controls themselves.**
  The divider's counter phase and the switches' crossfade position are
  transport-like DSP state, the same category Retroactive's own window phase
  falls into, and both simply restart at zero on patch load like any other
  running clock.

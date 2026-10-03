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

### Quad Logic board mode

Context menu: **FINDINGS: Quad Logic board**. Off by default, and with it off nothing above
changes (Invert A, AND, OR, XOR, NAND, NOR, XNOR, the 0/10 V gates, REF V). On, FINDINGS
becomes the Day 8 *MiaW Quad Logic Module* worked out as parts (`src/QuadLogicBoard.hpp`,
`src/Cd4071.hpp`, `src/Tl074Comparator.hpp`; the schematic is Sourcery Studios'
2019-09-11 sheet and its BOM). The setting is saved with the patch.

* **Every gate is the board's OR**; the FN knobs are ignored and the glass reads OR. The
  board has no invert, AND or XOR.
* **Inputs are comparators.** Each jack goes through 1k to a TL074's (+), with 100k to ground
  on the (+) pin; every (-) sits on 12 V x 10k / 110k = 1.0909 V. The jack therefore reads
  true above **1.102 V**, not Rack's 0.1/2 V. An unpatched jack is pulled to 0 V by its
  100k (so REF V does not apply; the glass shows `--`). Negative voltages read false.
* **The comparator's high is not a rail.** It is 11.21 V behind 526 ohm (the TL074's
  headroom, see below) through a 1N4448 into 2k7 and an LED to ground, which puts the 4071's
  input at about **9.2 V** high and about 0 V low. That is above the CD4071B's guaranteed
  VIH of 8.6 V at 12 V, which is why the board works; the model reproduces it, and a test
  fails if the level drops below the chip's threshold.
* **The 4071** switches at VDD/2 (6 V), drives 0 or 12 V through ~200 ohm into a BC547C base.
* **The output is the emitter divided by two.** The follower's emitter feeds 1k to the Y jack
  and 1k to ground, so Y is about **5.67 V when true, 0 V when false**, not a 10 V gate. The
  Y LED branch (2k7 and a red LED off the emitter) is in the circuit and solved with the
  nodal solver (`Mna.hpp`); the jack itself is unloaded, as every Rack input is.
* **Polyphonic**, following the larger channel count of the pair's two jacks.
* **The pairing follows the panel, not the board's crossed wiring.** On the Day 8 board the
  4071's pins 12/13 take U2.3/U2.4 (A4, B4) into Y3, and pins 8/9 take U2.1/U2.2 (A3, B3) into
  Y4: the lower two pairs are crossed. That is a quirk of the drawing, so in this mode
  OUT*n* is A*n* OR B*n* for all four. `AuditLogic::quadPair()` is the one line that restores
  the board's literal wiring.

Still assumed (nothing on the sheet or datasheets fixes them): the LED's orientation (the
symbols are ambiguous; it is taken anode toward the 2k7, as the board's purpose requires) and
Vf (red, 2.0 V at 20 mA); the 1N4448 as the 1N4148 SPICE set; the BC547C as Is 7.6e-14,
BF 520, BR 10, VAF 100; the TL074 output as a 526 ohm source 0.79 V under each rail (fitted
to the datasheet's typical +-13.5 V at 10k and its +-10 V minimum at 2k, both quoted at
+-15 V, so at +-12 V it is an extrapolation) and gain 200 V/mV; the 4071's output
resistance as its typical drive currents (400 / 192 / 221 ohm at 5 / 10 / 15 V); the 4071's
input clamp holding a low node near 0 V. Not modelled: input offset, bias current, the
TL074's phase reversal below its common-mode range (the datasheet is silent), the residual
~1.6 V the LED knee leaves on the 4071's input after a falling edge (it still reads low),
and propagation delay. The rail is fixed at +12 V.

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

### The HEF4066 board model (context menu, on by default)

REFERRAL runs the Day 10 board rather than an ideal switch (`src/ReferralBoard.hpp`,
`src/Hef4066.hpp`; menu item "REFERRAL: HEF4066 board"; switch it off for the ideal
bipolar switch this module started with, which is what a +-5 V CV router wants).
From the Nexperia HEF4066B datasheet (Rev. 12) and the board:

* **Gate.** One comparator threshold, 1.0909 V (12 V * 10k / 110k), no hysteresis: the
  earlier 0.1 V / 1 V pair was this module's, not the board's. An unpatched gate reads 0 V.
* **Range.** VDD = 12 V, VSS = ground: the signal pins have clamp diodes (VI limited to
  -0.5 V .. VDD + 0.5 V), so -5 V comes out near -0.6 V and +17 V near +12.6 V. The negative
  half of a bipolar signal is clipped. The signal is driven through an assumed 1 k source
  resistance and loaded by an assumed 100 k.
* **Ron** is a function of the signal level (Fig. 6, digitised: 49 ohm at 0 V, a hump near
  2.8 V and a larger one near 8.9 V at 10 V supply; 12 V is the conductance-weighted
  interpolation between the 10 V and 15 V curves, about 44 to 59 ohm) and of temperature
  (from RCA's CD4066B maxima, because Nexperia gives 25 C only; fixed at 25 C here). It
  gives distortion at the datasheet's order of magnitude (0.1 % against 0.04 % at 0.5 VDD
  p-p into 10 k), so a loud signal is slightly squarer through the chip.
* **OFF** leaks the signal through the switch's 0.5 pF (-50 dB at 1 MHz into 1 k, as in the
  isolation and crosstalk rows; -70 dB at 1 kHz into 100 k), and the other channel's A pin
  couples the same way.
* **Each enable edge** leaves a small charge on the output (25 mV of spike per edge on
  23 pF at 10 V, scaled by VDD): a click of about 0.7 nV s, far below audibility, but real and
  signed (+ on turning ON, - on turning OFF).
* **Timing.** The enable arrives after the comparator's slew (20 V/us) to the E node's
  mid-swing plus the chip's tPZH/tPHZ (about 20 / 65 ns at 12 V); the edge lands part-way
  into a sample (the gate is interpolated across the sample) and the sample carries the
  matching fraction of conduction. Hi On drives the E pin from the diode node and Lo On
  from the BC549 collector, so their delays differ (0.8 and 0.3 us against 0.5 and 0.6 us).
  The 4066 itself is **make-before-break** (tPHZ is longer than tPZH by about 45 ns), but
  in A-B/A-C the direct side is the slower one, so the pair as built is break-before-make by
  about 150 ns for Hi On. All three nodes are the same A signal, so neither is audible.
* **The E pin sits in the datasheet's undefined band.** The comparator, 1N4448 and 1k/LED
  load bring nE to about 5.8 V (Hi On) or 7.0 V (Lo On, LED lit) at 12 V, between VIL max
  (3.4 V) and VIH min (8.6 V). The model switches on the comparator's logic; whether a real
  chip trips there is part-dependent and needs the board measured.

Still assumed: the clamp diodes' saturation current; the 1k / 100k terminations; the 5 pF + 8 pF
node capacitance; RCA's temperature ratios for the Nexperia part; the TL074 as the 526 ohm
Thevenin source of `Tl074Comparator.hpp`; the 1N4448, BC549C and LED parameter sets; no Q1
storage time, no comparator offset. Fig. 6 at 5 V implies 1.7 % THD where Table 12 says 0.25 %.

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
  LED per channel shows the connection. ROUTE (A-B/A-C) is this module's own: the chip
  has one switch per channel and no complementary output, so the second path would be a
  second channel with the opposite toggle (modelled that way). The HEF4066BT is supplied
  from VCC (12 V) with VSS on ground, so the switch only passes 0 to +12 V; this is now
  modelled (see "The HEF4066 board model"), with the ideal bipolar switch kept as a menu
  option. POLARITY being shared, the two channels, and ROUTE are kept: the board does not
  make them wrong, it only has more of them.
* **FINDINGS' gates are not what the Quad Logic Module has.** That board's schematic is in the
  course folder (Day 8, "MiaW Quad Logic Module") and it is **four OR gates**: eight TL074
  comparators, each comparing one input against about 1.1 V (a 100k / 10k divider from +12 V),
  each through a 1N4448 and a 2k7 pull-down into a 4071's inputs, in pairs; each OR gate drives
  a BC547C emitter follower and an output jack and LED. There is no inversion, AND or XOR on
  that board. The module's per-gate functions -- Invert A, AND, OR, XOR by default, NAND, NOR
  and XNOR besides -- are this module's own, kept from the plugin's registered description (the
  Quad Inverter and Hex Inverter boards are the source of the inverter). The OR is on the
  board; the rest are additions. With the default functions the comparators'
  threshold (1.1 V, not Rack's 0.1/2 V hysteresis) and the buffered output levels are not
  modelled and the jacks stay 0/10 V gates; **Quad Logic board mode** (above) models the board
  itself, comparators, 4071 and followers included.
* **MUSICAL division reuses the printed /2../64 jacks** rather than adding a
  second bank of six outputs for /3../96; see INSTALLMENTS above. /3 and /6
  are consequently ~66 % duty rather than 50 %, an unavoidable consequence of
  an odd tap count, not a bug.
* **No state is written to the patch beyond the panel controls and the Quad Logic board
  switch.**
  The divider's counter phase and the switches' crossfade position are
  transport-like DSP state, the same category Retroactive's own window phase
  falls into, and both simply restart at zero on patch load like any other
  running clock.

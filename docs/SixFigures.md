# Six Figures — oscillator bank

A six-voice oscillator bank for VCV Rack 2, form W-2 — six sources of income, one
line each.

Part of the [Moon Technologies](../README.md) plugin.

It consolidates the whole *Modular in a Week* oscillator day into one module: the
40106 hex Schmitt-trigger bank, the All About Circuits 4069-integrator VCO, the
4046 PLL VCO and the Kassutronics reverse-avalanche oscillator become four
selectable **cores**, and the panel gives you six of them side by side rather
than four different 3-8 HP modules. Credit to Kristian Blåsol (the 40106 bank
and the 4046 and avalanche schematics) and to All About Circuits / Kassutronics
for the circuits the other two cores are drawn from.

None of the four originals is a tracking VCO — they are RC relaxation
oscillators, and their "CV in" is a photocell or a summing resistor landing
directly on the timing capacitor's pot, not a exponential converter. Six
Figures keeps that crudeness as the default behaviour and adds a real 1 V/oct
mode as a menu option, the way the brief for this family asks: keep the
original character available and default, and let VCV do the rest.

## Layout

24 HP, six identical voice columns and a seventh "totals" column on the right
that holds the controls shared by all six voices.

## The four cores

Every voice can be set to any core independently, so you can run six of the
same oscillator, six different ones, or anything between.

| Core | Original circuit | Main output (`OUT`) | Auxiliary output (`AUX`) |
|---|---|---|---|
| **40106 Schmitt** | 40106 hex Schmitt-trigger astable (six voices sharing one IC in the original) | the astable's output, band-limited: a slightly lopsided square (about 49 % high at 12 V), edges placed between samples | the timing capacitor itself, centred on the hysteresis window and scaled so the thresholds sit at ±5 V (it overshoots a little by the propagation delay) |
| **4069 AAC** | All About Circuits' 4069-integrator VCO (`SQU` and `TRI` in the original schematic) | band-limited square | band-limited triangle, from a leaky integration of the same square |
| **4046 PLL** | 4046 phase-locked-loop VCO | band-limited square, free-running or pulled toward `SIGNAL` | the raw XOR phase-comparator bit, as a 0/10 V logic signal — exactly what the 4046's `PC1` pin actually outputs |
| **Avalanche** | Kassutronics reverse-avalanche oscillator (a BC337 run in reverse breakdown, charging a cap through the vactrol's LDR) | band-limited saw: the capacitor's own exponential charge between the junction's release and strike voltages (see below) | a 1 ms, 10 V pulse once per cycle — the avalanche breakdown pulse itself |

**The 40106 core is the chip.** `src/Cd40106.hpp` (the CD40106B and its astable) with
`src/SixFigures/Schmitt.hpp` (this module's pot, capacitors and supply), tested in
`tests/SixFigures/test_cd40106.cpp`. The board (Day 1, "40106 Hex Oscillator Bank") is six
inverters, each with a "Value N" pot from output to input, a "Value N" capacitor from input to ground
and an "In N" jack through a 1N4448 and 1k into that same node. The voice is that circuit stepped, not
a phase counter:

- **Thresholds are the datasheet's** (TI CD40106B, SCHS097F, 25 C, typical): VP = 2.9 / 5.9 / 8.8 V and
  VN = 1.9 / 3.9 / 5.8 V at VDD = 5 / 10 / 15 V, interpolated in between (7.06 V and 4.66 V at the
  assumed +12 V). The astable law is T = R C [ ln((VDD - VN)/(VDD - VP)) + ln(VP/VN) ]. Because both
  thresholds scale with VDD, the frequency hardly moves with the supply (the constant is 0.812 at 5 V,
  0.811 at 12 V, 0.812 at 15 V); what moves is the duty cycle (48 % high at 5 V, 49 % at 15 V), the
  capacitor's swing, and, on fast settings, the chip's output resistance (datasheet: 0.4 V at 1 mA,
  0.5 V at 2.6 mA, 1.5 V at 6.8 mA at 5 / 10 / 15 V) and propagation delay (140 / 70 / 60 ns), which
  lengthen the period. All three are in the model.
- **RATE keeps its dial.** At 12 V, typical thresholds, the pot is the resistor for which the ideal law
  gives the frequency the knob has always printed (an exponential sweep over the range); the chip's
  own output resistance and delay then pull it off by a few percent at the top, and the display shows the
  frequency actually measured from the output. **Assumed:** the schematic leaves the capacitors to the
  course, so the range switch picks 68 nF (AUDIO: the pot runs 4.5 k at 4 kHz to 0.9 M at 20 Hz) or
  47 uF (LFO: 3.3 k at 8 Hz to 0.52 M at 0.05 Hz); the pot taper is exponential; the supply is the
  board's VCC, taken as +12 V. **40106 supply (VDD)** in the context menu offers 5, 9, 12 and 15 V
  (default 12).
- **CV is the In jack.** In the default response the voice's CV (after its trim) goes through the 1N4448
  and the 1k into the timing node. The diode only conducts once the jack is within about a diode
  drop of the node, which swings between VN and VP, so a CV below about VN does nothing at all; above
  that it fights the discharge, lengthens the low phase (a few volts higher it holds the oscillator
  stopped with its output low); with a signal it is the board's sync/gate input, not a pitch CV. Negative
  voltages are blocked by the diode. **1 V/oct tracking** (context menu) is not on the board: it scales
  the pot by 2^-CV with nothing injected, and is the setting to use for pitch.
- **Not modelled:** temperature, the corner (typical thresholds only in the module; the library header
  has the datasheet's min and max rows), the other five inverters sharing the die and the TL07x
  followers and 100 ohm output resistors (the output is the logic level, bipolar ±5 V as for the other
  cores). The LED follows the output. **One sample of latency:** the band-limiting correction at an
  edge reaches the sample before it.
- **SYNC** restarts the astable at the trough of its steady-state cycle, the same instant the old
  phase counter started its cycle.

**On the 4046 core.** The course's 4046 board (Day 1, "4046 Simple VCO") uses the chip as a VCO
alone: the inhibit pin is grounded, a 10 nF capacitor is across CX, R1 (pin 11) is 100k to ground,
R2 (pin 12) is not fitted, both phase comparators are unused, and the control voltage comes from an
LM358 stage into VCOIN (pin 9). The jack the schematic calls "Signal In / Sync / RingMod" goes to pin
11, the R1 pin, not to the 4046's signal input (pin 14). So **the phase-locked loop in this core, the
`SIGNAL` input, `CAPT`, `LOCK` and the PC1 bit on `AUX` are this module's addition**, built on the same
chip's two sections rather than taken from the board.

**The 4046 core's VCO is the CD4046B's.** `src/SixFigures/Cd4046.hpp`, tested in
`tests/SixFigures/test_cd4046.cpp`. For a 4046 voice `RATE` and `CV` make **VCOIN** (0 V to the
assumed 12 V supply; the board makes it with an LM358 stage) and the frequency is the chip's law
with the board's parts: R1 = 100k, R2 not fitted, C1 = 10 nF in the AUDIO range and 5 uF in the LFO
range (the range switch is the module's; the board has one C1).

- f(VCOIN) is a straight line, zero at 0 V, `f0` at VDD/2 and `2 f0` at VDD (the data sheet's design
  rule f0 = 0.5 fmax, "VCO without offset"). It is *linear*, not the old exponential `RATE` taper:
  `RATE` at 0 stops the oscillator; with 100k / 10 nF at 12 V the middle of the knob is about 1.01 kHz
  and the top about 2.03 kHz (LFO range: 2.1 Hz and 4.3 Hz). The 20 Hz to 4 kHz audio and 0.05 Hz to
  8 Hz LFO spans the other cores share do not apply to this one.
- f0 is the nine lines of Fig. 7 of the Nexperia HEF4046B data sheet (centre frequency against C1 for
  R1 = 10k / 100k / 1M and VDD = 5 / 10 / 15 V), read from the PDF's vector graphic. At 100k / 10 nF
  it is about 884 Hz at 10 V and about 1.2 kHz at 15 V; at 12 V it is interpolated between them.
  The speed limit (1.0 / 2.0 / 2.7 MHz at 5 / 10 / 15 V) never bites at audio rates.
- **Interpolated, not read:** between the nine lines (log-linear in VDD and R1: the board's 100k is
  on a line, 12 V is not) and past the lines' ends (the LFO's 5 uF is beyond the last knot at about
  1 uF; the last segment is continued, slope -1). Part-to-part spread (+-30 to 50 % in the data
  sheet) and temperature are not modelled, and neither is the 1 % linearity error.
- **The R1 pin.** The board's "SIGIN / Sync / RingMod" jack is wired straight to pin 11, no series
  resistor. The VCO's frequency follows the current the chip drives out of that pin, so a signal
  patched there is a **current modulation**: frequency is exactly *linear* in the patched voltage
  (linear FM, not exponential), a signal above VCOIN stops the VCO, and a signal below the node's own
  voltage speeds it up, without bound but for the speed limit. A low-impedance source makes R1
  irrelevant. The data sheets say nothing about the pin's impedance: the model's 2.2 kohm drive
  resistance is an **estimate** (it explains why the 10 kohm lines of Fig. 7 sit about 15 % under the
  100 kohm and 1 Mohm lines), the patched source is taken as 1 kohm, the drive is taken as
  source-only and the node cannot go below -0.6 V. Treat injected behaviour as shaped like the chip,
  not calibrated to it. The context menu's **SIGNAL also on the 4046 R1 pin** (off by default)
  patches `SIGNAL` there for every 4046 voice; with it off `SIGNAL` is only the PLL reference.
- **Kept as the module's addition:** the PLL (`SIGNAL` as comparator reference, `CAPT`, `LOCK`, the
  PC1 bit on `AUX`). It pulls the frequency the chip law gives, in octaves, as before. It was not
  changed: nothing in the VCO law makes it wrong, it is documented as an addition, and it is off
  the board's signal path.
- In **1 V/oct** mode a 4046 voice's wanted frequency is turned into the VCOIN that produces it (an
  exponential converter ahead of the chip), so tracking is exact within the chip's range
  (0 to about 2 kHz audio) and clips at the rails.

All four are band-limited with polyBLEP (the 40106 core by the same correction at each edge of the
circuit's own output, at the edge's exact position within the sample). The 4069 core's triangle is
still a leaky integral of its square.

**The avalanche core.** The board's circuit is a relaxation oscillator: the capacitor charges through
the rate resistance toward the supply less the LED until the reversed BC337 junction strikes, the
junction dumps it until it lets go, and it charges again. `OUT` is that charge,
`src/SixFigures/Avalanche.hpp`: an exponential between the release and strike voltages, which are
0.9 V apart against a charging source about 10 V away, so the saw is nearly straight with a mild bow
(0.5 + 0.06 at mid-cycle), not the strong bend the old reshaping drew. The frequency is still the
module's RATE; this module asks for a frequency and the circuit's contribution is the shape. The
numbers (strikes at 8.2 V, lets go at 7.3 V, needs about 5 mA to hold) are measured on a **2N2222** by
lcamtuf ("Cursed circuits #6", blog.coredump.cx), whose 14 V, 1k, 1 mF circuit the model reproduces
to within 20 % (4.9 Hz against 5.8 Hz; electrolytic tolerance covers the gap). **No BC337 has been
measured**: they are assumed to carry over, and a different part will strike somewhat higher or
lower. With the board's 12 V supply the circuit stops oscillating (the supply alone holds the junction
on) below about 580 ohms; the module's 4 kHz audio top at 1 µF needs 673 ohms, just above it. The
flyback, the junction's on-resistance times the capacitor, is taken as instantaneous.

**Strike jitter (added 2026-10-03).** The junction does not strike at a fixed voltage: it breaks
down through microplasmas whose turn-on rate climbs exponentially with the voltage across them
(Haitz 1964; see Microplasma.hpp and the Kickback manual), so each cycle's strike voltage is the first
passage of a Poisson process whose rate rises as the capacitor charges. Every cycle draws its own
strike voltage (`Strike` in Avalanche.hpp, by inverting the cumulative hazard
`Lambda(V) = r0 RC G(V)`), and with it the cycle's length and height: a late strike is a longer cycle
that peaks higher. The rate is pinned by the published measurement (median strike 8.2 V at the
measured 1 k x 1 mF); the e-fold, 20 mV, is **assumed**. Results: about 3 % rms cycle-to-cycle jitter
in period at every pitch, a tail of early strikes (the mean cycle is a little shorter than the
median), and a median strike that moves with the pitch (8.29 V at a 10 ms time constant, 8.20 V at 1 s,
8.11 V at 100 s: charging faster, the junction strikes later). The cycle length is normalised to the
median, so RATE still sets the median pitch. DRIFT is unchanged and is the slower wander. The per-cycle
randomness is seeded per voice and is deterministic from the module's creation.

**The board's output stage.** Read from the schematic: the oscillator node N goes through R4 100k to
P2 ("Output from Osc circuit"); from P2, C3 1 µF goes to the TL072's POS input, which R7 1k holds to
ground. C3 into R4 + R7 is a 1.58 Hz high-pass, and R4 over R7 is a divider that passes 1k / 101k = 0.99 %
of the node's swing. The stage is then non-inverting, gain 1 + 220k / 1k = 221, with the part's 3 MHz
gain-bandwidth putting a pole at 13.6 kHz (rail ASSUMED at +-10.5 V on +-12 V supplies). So the net gain is
221 x 0.99 % = 2.19 and the 0.9 V swing comes out as about 2 V peak to peak: **it does not clip** (an
earlier version of this page, and of `Tl072Stage`, had the divider the wrong way and called it a clipped
square wave; the PDF does not say that). `Tl072Stage` in Avalanche.hpp models it and is tested.
The default core has no output for it; the optional board core (below) does.

**Avalanche core: board (menu option, off by default).** With it on, the avalanche voices are the
schematic's parts instead of a frequency asked for. `src/SixFigures/AvalancheBoard.hpp`:

- **RATE** is R2, the board's 10k pot (assumed linear, with RATE = 1 at 0 ohms, the fast end);
  **RANGE** is SW1: AUDIO is C2 1 µF, LFO is C1 10 µF. The charging resistance is
  R1 1k + R2 || the vactrol's LDR (R3), so 1k to 11k with no CV.
- **CV** goes through R8 330 into R9 (the **CV amount** trimpot's positive half is the 100k pot, 0 to full;
  its negative half is the pot at ground), then LED2 (the vactrol's LED), through the optional LED3 if you
  fit it (menu), and the LDR is the VTL5C3 of Vactrol.hpp with its memory. A negative CV lights nothing.
  The 100k pot starves the LED below full travel (5 V at half travel is a few microamps), and at full travel
  10 V gives 25 mA: the LDR falls to about 1.5k and the pot-at-10k pitch goes up 4.5 times.
  The first milliseconds of a CV step show the cell's own lag.
- **The frequency is not set anywhere.** The capacitor charges toward VCC less LED1 through that resistance
  and the junction strikes when its hazard (the microplasma law above, integrated along the real charge, so a
  resistance that moves mid-cycle is handled exactly) says so; it releases at 7.3 V. The median cycle is
  `T = RC ln((Vs - Vrel) / (Vs - V_m))` with V_m the median strike at that RC; checked against an independent
  stepped simulation with its own calibration and against the strike table, to under 1 %.
- **Spans, measured by the test (median frequency, no CV):** AUDIO 218 Hz (R2 10k) to 2.26 kHz (R2 0);
  LFO 23.1 Hz to 239 Hz. These are not the default core's 20 Hz-4 kHz and 0.05-8 Hz: the board's parts
  cannot reach those. The 10:1 capacitor ratio gives 9.45:1 in frequency because the junction strikes later
  when it is charged faster (8.34 V at the fastest, 8.24 V at the slowest, against 8.2 V at the
  calibration), and the fixed-threshold law is 6-19 % higher in frequency than the circuit's, so the abstract
  core's RATE-to-pitch law is not the board's. Jitter is the ~3 % of the default core, per cycle, and
  there is nothing else to wander the pitch except DRIFT (the module's addition, kept: it speeds or slows the
  whole time constant by up to half an octave, as before).
- **OUT carries** (menu): *saw (module)*, the default, is the capacitor's own charge normalised as in the
  default core (+-5 V; here the peak sits above +1 at the fast end, where the median strike is later);
  *TL072 x221 amplified (P3)* is about +-1.1 V at the board's own gain, in volts; *raw oscillator node (P2)* is
  P2 exactly: N through R4 into C3 + R7, so it carries N's DC level (about 9.6 V) and only about 1 % of the
  swing on top of it (~10 mV at audio rates, a few percent more at the slowest LFO), which is what the schematic's jack does; *capacitor node
  (N, before R4)* is N itself, 9.1 to 10.1 V, an addition of mine (the schematic has no jack there). The
  AUX pulse, the LED and the display (the median frequency) follow the board. MIX always sums the saw.
  1 V/oct mode does not apply to board voices.
- **Assumed** (not in the course files or the sheets): VCC +12 V, LED1 1.8 V and constant, the junction numbers
  of the default core (a 2N2222's, strike 8.2 V, release 7.3 V; no BC337 measured), the 20 mV strike e-fold,
  R2 linear, the vactrol a VTL5C3 with a 20 Mohm dark resistance, LED3 the same LED as LED2, C1/C2 exact, the
  TL072 ideal apart from its gain-bandwidth and rail, no input offset (3 mV typ x 221 would be 0.7 V of DC
  at P3).

## Per-voice controls (×6)

| Control | Type | Description |
|---|---|---|
| **CORE** | name on the read-out | Which of the four cores this voice runs. Click the voice's name on the read-out to pick one; the frequency it is running at is printed under it |
| **RATE** | knob | The voice's frequency. For a 40106 voice it is the timing pot of the astable; for the other cores RC-pot style by default (see [CV response](#cv-response) below) |
| **CV** | trim, paired with the jack below it | How much the `CV` jack affects `RATE` |
| **CV** (jack) | input | CV for this voice's rate |
| **OUT** | output, footer | The core's main waveform, ±5 V |
| **AUX** | output | The core's secondary signal — see the table above |
| rate LED | light, beside `RATE` | Blinks with the voice's own cycle **only in LO range** — a visual clock/LFO indicator. In HI range it stays off; blinking at audio rate would just read as flicker. |

## Shared controls (the totals column)

| Control | Type | Description |
|---|---|---|
| **RANGE** | click on the read-out | `LO` ≈ 0.05–8 Hz (LFO range), `HI` ≈ 20 Hz–4 kHz (audio range). Applies to every voice. |
| **CAPT** | knob | For voices set to the PLL core: the loop filter's bandwidth. Low = narrow/slow lock, high = wide capture range and faster lock, at the cost of more audible ripple from the phase comparator leaking into the pitch. |
| **LOCK** | light, beside `CAPT` | Lit when at least one PLL-core voice is locked to `SIGNAL`. See [PLL lock](#pll-lock-heuristic) below. |
| **SYNC** | input | A rising edge (Schmitt, 0.1 V/2 V thresholds) hard-resets every voice's phase, drift and PLL state at once, whatever core each is set to. |
| **DRIFT** | trim | For voices set to the Avalanche core: depth of a slow random wander added to the rate, modelling the vactrol's LDR never quite settling. Up to ±0.5 octave of smoothed, one-pole-filtered noise at maximum. Has no effect on the other three cores. |
| **SIGNAL** | input | The reference signal PLL-core voices compare against — the 4046's comparator input. Unconnected, PLL voices simply free-run at their own `RATE`. |
| **MIX** | output, footer, primary | The sum of all six voices' `OUT` signals, soft-clipped (`tanh`) rather than hard-clamped, so stacking six voices thickens and eventually saturates rather than just clipping flat. |

## CV response

(**40106 voices** do not use the response described in this paragraph: their CV is the board's In jack,
through a 1N4448 and 1k into the timing node, or, in 1 V/oct mode, an exponential converter on the pot;
see [the 40106 core](#the-four-cores).)

By default every other voice's CV response is the "crude" one every circuit here
actually has: the CV jack (attenuated by that voice's trim) is summed directly
into the `RATE` knob's own 0–1 travel, *before* the knob's exponential taper —
matching the "CV summing mixer" landing on the frequency pot's wiper in the
AAC sketch this module is partly drawn from. It is not 1 V/oct, and doubling a
CV's swing does not double the pitch shift the same way at both ends of the
knob's travel.

The context menu's **1 V/oct tracking (all voices)** option replaces this,
module-wide, with a real exponential converter: `RATE` becomes a coarse offset
from the range's centre frequency, and CV tracks pitch at 1 V/oct. None of the
four original circuits could do this — it is the "bells and whistles" VCV
Rack allows, added on top of the default, characterful behaviour rather than
instead of it.

## PLL lock heuristic

The 4046 core's `LOCK` light is not a cycle-accurate lock detector — this
module doesn't have a discrete phase counter to check — but a heuristic: it
low-passes the phase comparator's XOR bit twice, once quickly (the same
`CAPT`-controlled corner the pitch pull itself uses) and once slowly
(around 0.5 Hz), and calls the loop "locked" once the fast average stops
drifting away from the slow one. It reads correctly in practice — steady
while genuinely tracking `SIGNAL`, dark while beating against it or free-running
— but treat it as an indicator of stability, not of exact frequency or phase.

## Voltages

- `OUT` and the triangle/ramp `AUX` signals: ±5 V.
- The PLL `AUX` (raw comparator bit) and the avalanche `AUX` (breakdown pulse):
  0/10 V, logic-style — these are comparator and trigger outputs, not audio.
- `SYNC`: a gate/trigger, 0/10 V convention, Schmitt thresholds 0.1 V/2 V.
- `SIGNAL`: bipolar audio-range signal; only its zero-crossings matter (it is
  squared internally by a Schmitt comparator, thresholds ±0.1 V).
- `MIX`: ±5 V nominal, soft-clipped above that as more voices stack up.
- CV jacks: no fixed convention (see [CV response](#cv-response) — the crude
  mode treats CV as a 0–10 V-ish trim on the pot, the 1 V/oct mode treats it
  literally).

## Context menu

- **1 V/oct tracking (all voices)** — off by default. See
  [CV response](#cv-response).
- **Avalanche core: board** — off by default. Turns the avalanche voices into the schematic's circuit,
  with **Avalanche board: OUT carries** (saw, P3, P2, N) and **Avalanche board: CV LED (LED3) fitted**.
  Saved in the patch only when on. See [the avalanche core](#the-four-cores).
- **40106 supply (VDD)** — 5, 9, 12 (default) or 15 V: the supply of the 40106 voices.
  See [the 40106 core](#the-four-cores).
- **SIGNAL also on the 4046 R1 pin (board's RingMod jack)** — off by default. See
  [the 4046 core](#the-four-cores).

## What was approximated or left out

- The 40106 core is the circuit (above). Still assumed there: the capacitor values, the pot taper and
  range, VDD = 12 V, typical thresholds, the on resistance as the secant to the datasheet's test points
  (a little above the true small-signal value), the generic input-protection diodes (matter only when the
  In jack drives the node outside the rails). The avalanche core is the relaxation oscillator's own charge curve, but
  its breakdown voltages are a 2N2222's, not a measured BC337's, the strike jitter's e-fold (20 mV) is
  an assumption of Haitz's microplasma shape (the rate itself is calibrated to the published strike), the TL072
  x221 stage is routed only in the optional board mode, and the flyback is instantaneous.
- The PLL only locks to the fundamental, not to harmonics or subharmonics of
  `SIGNAL` the way a real 4046 can be coaxed into doing (the "or its harmonics
  or subharmonics" the brief mentions) — implementing genuine N:M lock was out
  of scope for this pass.
- The 4046 core: the CD4046B VCO law is the data sheet's (see above). Assumed, not data-sheet:
  VDD = 12 V, the R1-pin drive resistance (2.2 kohm), the patched source impedance (1 kohm), the
  source-only drive and the -0.6 V node floor, and the LFO range's 5 uF. Not modelled: part spread,
  temperature, the data sheet's linearity error, R2 (the board leaves it out) and the source follower.
- `DRIFT` and `CAPT` are each one shared control across all six voices
  rather than per-voice, matching the panel's single totals column; every
  voice running that core gets its own independent noise generator or PLL
  state, so six avalanche voices drift independently even though they share
  one `DRIFT` depth.

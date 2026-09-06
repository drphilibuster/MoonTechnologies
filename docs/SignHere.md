# Sign Here

FORM 8879. Slug `SignHere`. 20 HP, monophonic.

> *Modular in a Week*, Day 11: Kristian Blåsol's button-and-pedal, the
> offset-scaler joystick, and the touch circuit built for drum
> triggers/gates. Three separate builds, one panel.

## What it is

Three gesture controllers that share nothing electrically in the original
course -- they're grouped here because they're all "read a human doing
something, output a voltage," not because one circuit feeds another. The
joystick and the four touch pads are drawn live: a custom widget over a
recessed well, exactly the idiom `Repossession`'s timeline strip uses,
with its position and touch state kept in the module (a hidden pair of
params for the joystick, so its position automates and saves like any real
knob) rather than in the widget itself.

## Joystick

The offset-scaler joystick schematic is two identical op-amp channels (one
per axis): a pot's wiper is offset by a trim and scaled by a second trim,
buffered out. Here the pot is a mouse-driven XY pad instead of a physical
joystick, and the offset/scale trims are real knobs.

- **The pad itself** (top left) -- click or drag anywhere on it to set the
  joystick's X/Y position; it recentres on release only if spring return is
  turned on (right-click menu). The dot is lit while held.
- **X SCALE** / **Y SCALE** (trim, 0-200%) and **X OFFSET** / **Y OFFSET**
  (trim, ±5 V) -- shape each axis before it reaches its jack: `X CV` =
  `position x SCALE x 5 V + OFFSET`, clamped to ±12 V. Same for Y.
- **GLIDE** (knob, 0-500 ms) -- a slew on both axes' output, so a flicked
  joystick can portamento into its new position instead of jumping.
- **X CV** / **Y CV** (jacks, out) -- the shaped, glided position.
- **TOUCH** (jack, out) -- 10 V while the pad is being touched, 0 V
  otherwise.
- **IN** / **OUT** (jacks, footer) and **AMOUNT** (knob, ±100%) -- the
  schematic's own plain signal-through path, kept as a general-purpose
  bipolar attenuverter rather than wired into either axis: `OUT = IN x
  AMOUNT`. See "What was approximated" below.

## Touch pads (x4)

The touch-drum schematic is a transistor amplifier per pad feeding a
comparator, so a fingertip's leakage current becomes a clean logic edge; the
"pressure" isn't sensed here (there's no way for a mouse click to report
force), so it's played as an RC-style ramp instead, echoing the comparator
bias network's own capacitors.

- **1**-**4** (the pads) -- mouse down is a touch; the number lights while
  held, and the pad glows the more of its pressure ramp has built up.
- **GATE** -- 10 V while held.
- **TRIG** -- a 1 ms pulse on the moment of touch.
- **CV** -- ramps from 0 V toward 10 V over about 150 ms while held, and
  back down over about 80 ms on release -- a pressure envelope rather than a
  true pressure sensor.

## Button and pedal

- **BUTTON** (big bezel, momentary) and **PEDAL** (jack, gate in) -- either
  one presses it; the bezel lights while pressed.
- **LEVEL** (knob, 0-10 V) -- the voltage `CV` reaches while pressed.
- **INV** (switch) -- Non-inverted / Inverted. Swaps the idle and active
  voltage on `GATE`, `FLIP` and `CV` (idle 0 V/active high becomes idle
  high/active 0 V); `TRIG` is always a clean positive pulse regardless.
- **GATE** -- 10 V while pressed (0 V if `INV`).
- **TRIG** -- a 1 ms pulse on the moment of press.
- **FLIP** -- a latch that toggles every press: press once for on, again for
  off.
- **CV** -- `LEVEL` volts while pressed, 0 V otherwise (polarity per `INV`).

## What was approximated or left out

- The button schematic's separate buffered/inverted jacks for every output
  (`Gate Out`, `Inv Gate Out`, `Flip Out`, `Inv Flip Out`, `Trig Out`,
  `Inv Trig Out` -- six jacks on the original board) collapsed into four
  outputs and one `INV` switch, per the brief. The hardware's own 4013
  flip-flop stage is annotated directly on the schematic as unreliable on
  the built board ("this flip flop works on breadboard, but might not work
  as shown here"); `FLIP` here is a straightforward software T-latch instead.
- The joystick schematic wires its "IN" jack into the *same* op-amp stage as
  the joystick pot for each axis (a fixed 10:1 external-CV-to-joystick
  mix baked into two resistor values), not a separate path. This module
  gives that external input its own generic in/out utility jack pair with a
  variable `AMOUNT` instead of mixing it into X or Y -- the brief's
  instruction to add a knob where the hardware had a fixed ratio, kept as
  its own path rather than guessing which axis it should join.
  The joystick's 2x5 pin header (for an external physical joystick module)
  has no software equivalent and was left out.
- The touch pads' pressure CV is a played envelope, not a sensed pressure --
  a mouse click has no analogue force to report. See "Touch pads" above.
- Polyphony: kept monophonic. None of the three source circuits are
  voice-per-channel designs, and a joystick or a touch pad is a single
  physical gesture at a time.

## Voltage conventions

All outputs clamp to 0-10 V (gates, triggers, the button's `CV`) or ±12 V
(the joystick's `X CV`/`Y CV` and the utility `OUT`). Triggers are 1 ms
pulses. `PEDAL` reads as a gate at a 1 V threshold. The joystick's own
position (and the button's `FLIP` state) persist in the patch; every knob
persists automatically as a normal Rack param.

# Payment Schedule

FORM 1040-V. Slug `PaymentSchedule`. Monophonic.

> *Modular in a Week*, Day 10: Kristian Blåsol's Baby8 (a 4017 decade counter
> read out as eight CV/gate steps), the sequential switch built from the same
> family of counter (a HEF4516 up/down counter driving a CD4051 8:1 analog
> mux in the original MiaW design), the dual 4031 tap sequencer/looper, and a
> PIC16F684 varimode CV quantizer (`varimodequantizer_100.asm`, Rev 1.0,
> 2010-05-29 -- the source carries no author name beyond the MiaW course
> material it ships in, so credit goes to the course and to whoever wrote
> that firmware, uncredited in the file itself). Payment Schedule fuses all
> four into one module: one counter drives the steps, the switch and the
> looper all at once, and the quantizer sits on the CV path leaving it.

## What it is

Eight steps, each with its own knob and a **CV IN** / **GATE OUT** jack pair.
The counter behind them is a decade counter exactly like the Baby8's 4017: on
each `CLOCK` edge it advances one step, up or down, and wraps at whatever
length `STEPS` is set to. Read one way that counter is a sequencer (each step
holds a CV level); read the other way, it's a sequential switch (only the
current step's pair of jacks is live) -- which is why `SWITCH IN` / `CV OUT`
do double duty as the module's plain audio/CV in and out **and** as the
common pole of an 8-way switch. A second, independent circuit -- the 4031 tap
looper -- lays a freely-tapped gate pattern over the top of the same eight
slots, clocked from the same source. A varimode quantizer (five scales) sits
on the CV leaving `CV OUT`.

## Per-step controls (x8, columns 1-8)

- **CV IN** (jack) -- normalled to that step's own `STEP` knob. Patch a CV
  here and it overrides the knob for that step only.
- **STEP** (knob) -- that step's raw CV level, 0 to the `RANGE` switch's full
  scale, before the global `ATTEN` attenuverter and the quantizer.
- **GATE** (lit button, latching) -- on by default. Off makes the step a
  rest: its `GATE OUT` stays at 0 V and `CV OUT` holds whatever it last was,
  rather than jumping to silence. The count still visits a muted step on its
  way round; it just has nothing to say when it gets there. Lit dim when the
  step is enabled but not current, full brightness when it is both enabled
  and current, dark when muted.
- **GATE OUT** (jack) -- `SWITCH IN` (normalled to +10 V) routed here while
  this step is current and its gate is on, 0 V otherwise. With `SWITCH IN`
  unpatched this is a plain per-step gate output; patch something else into
  `SWITCH IN` and it becomes an 8-way demultiplexer.

## Transport and chaining

- **UP** / **DN** (lights flanking the STEPS/DIR/SCALE trio, one knob's-width
  out from **DIR** on each side) -- show which way the count is currently
  running.
- **DIR** (switch) -- Down / Up.
- **DIR CV** (jack) -- 1 V or more flips whatever `DIR` says.
- **STEPS** (knob, 1-8) -- how many of the eight steps the count cycles
  through before wrapping -- the software equivalent of patching a step's
  own reset line back to itself on the original hardware.
- **RUN** (lit button, primary) -- toggles whether `CLOCK` moves the count at
  all. Starts on, so a freshly-patched module runs the moment it has a clock,
  same as before this existed; press it to stop the count wherever it is and
  again to resume.
- **RUN** (jack, in the row below) -- a rising edge here toggles `RUN` too,
  exactly like the button, so a gate or a manual switch can start and stop
  the sequence from elsewhere in a patch.
- **CYCLE** (jack) -- a trigger here starts the count from step 1, running,
  and stops it again automatically the moment it completes one full pass
  (whichever way `DIR` is set) -- a one-shot run rather than a free-running
  one. Patch a previous module's `EOC` into this to chain a one-shot run from
  one counter into the next.
- **CLOCK** (jack, lit) -- advances the count, while `RUN` is on.
- **RESET** (jack) -- jumps to step 1, regardless of `RUN`.
- **EOC** (jack, out) -- "end of cycle": fires a 1 ms trigger whenever the
  count wraps (in whichever direction it is currently running). Patch it into
  the next module's `CYCLE` to chain a second eight-step run onto the first,
  or into its own `RESET` for a longer cycle through a single module.

## Tap looper (4031)

A second gate track, independent of the eight steps' own `GATE` buttons, but
sharing their eight slots and their counter -- there is no separate length
control, because a loop that could run longer than the steps it plays against
had no way to display or play back the extra slots in the first place.

- **SCALE** / **ROOT** knobs and the **TAP** / **REC** / **CLR** bezels share
  the row with the transport controls above; **TAP IN** and **LOOP GATE** are
  jacks in the row below.
- **TAP** (bezel) / **TAP IN** (jack) -- either one marks the current slot.
- **REC** (lit button, latching) -- while lit, a tap ORs a hit into the
  current slot (overdub: existing hits are never erased by tapping elsewhere).
  Off, tapping does nothing to the pattern.
- **CLR** (bezel) -- clears every slot.
- **LOOP GATE** (jack, out) -- 10 V while the current slot holds a hit, 0 V
  otherwise; 0 V on a slot with no hit even if that step's own `GATE` is on --
  the two gate tracks (per-step and tap-recorded) are independent of one
  another and only ever share a position, not a value.

## Quantizer (varimode)

- **SCALE** (knob) -- Chromatic, Major, Minor, Major pentatonic, Minor
  pentatonic -- the firmware's own five modes. Shared with Dependents' own
  root quantizer (`src/Quantizer.hpp`).
- **ROOT** (knob) -- C through B.
- **QUANT** (switch) -- On quantizes `CV OUT`; Off passes the raw per-step CV
  straight through.
- **RANGE** (switch) -- 1 V / 5 V, the full-scale voltage a `STEP` knob (or an
  unpatched `CV IN`) reaches at full turn, before `ATTEN`. At 1 V/oct this is
  one octave or five octaves of range for the eight steps -- there is no 10 V
  position; nothing plays 10 V/oct.
- **ATTEN** (knob) -- a global attenuverter on the per-step CV, applied
  before the quantizer. Full clockwise is unity (the default, so existing
  patches are unaffected); turning it down tames a `RANGE` of 5 V without
  ever quantizing to a "wrong" voltage, since the quantizer runs on whatever
  comes out the other side. Full counterclockwise inverts the CV.
- **TRIG** (jack, out) -- a 1 ms trigger whenever the quantized value at
  `CV OUT` changes from what it last was (repeats on the same note don't
  retrigger).

## Main I/O

- **SWITCH IN** (jack) -- the sequential switch's common input; normalled to
  +10 V, which is what makes an unpatched `GATE OUT` read as a plain gate.
  Not related to the tap looper -- it feeds the per-step demultiplexer only.
- **CV OUT** (jack) -- the switch's common output *and* the sequencer's CV
  out: whichever step is current and enabled, attenuverted and quantized as
  above. This is the module's V/oct output.

## What was approximated or left out

- The original Baby8's per-step manual gate buttons and per-step
  reset-jump buttons (the `RESET`s row on the hardware panel, one push button
  per step) collapsed into the single `STEPS` knob and the single `RESET`
  jack -- patching a knob is the software equivalent of the wire link the
  hardware used.
- The sequential switch's own chips (HEF4516 up/down counter, CD4051 8:1
  mux) aren't emulated as discrete parts; the merged design already has one
  counter driving everything, so the switching is direct routing in
  `process()` rather than a second counter and an analog mux.
- The varimode quantizer's scale *tables* are reimplemented from standard
  music theory (major/minor/pentatonic degree sets), not disassembled from
  the PIC's fixed-point ADC math; the five modes and their names match the
  firmware's own header comment exactly. The firmware has no root control
  (it quantizes to a fixed reference); `ROOT` is this module's own addition,
  in the spirit of the brief's "bells and whistles."
- `EOC` and `CYCLE` are a synthesis of the hardware's "cascade in/reset in"
  and "cascade out/reset out" jacks (which, on the original board, are
  literally the same wire as reset, broken by a jack normalling when a cable
  is inserted) into a same-effect, cleaner pair -- but retargeted at
  Payment Schedule's own `RUN` transport rather than at `RESET`, so chaining
  a one-shot cycle across modules doesn't require a second cable back to
  `RESET` as well.
- Polyphony: kept monophonic throughout, matching the brief and the
  originals -- none of the source circuits are voice-per-channel designs.

## Voltage conventions

Audio/CV I/O clamps to ±12 V. Gates and triggers are 0/10 V, 1 ms
pulses for triggers. `CLOCK`, `RESET`, `RUN`, `CYCLE` and `TAP IN` read as
gates/triggers with Schmitt hysteresis (low below 0.1 V, high at 2 V or
above); `RUN` and `CYCLE` react to a rising edge, not to a held level.
1 V/oct at `CV OUT` when `QUANT` is on, `ATTEN` is full clockwise and `RANGE`
is set to 1 V.

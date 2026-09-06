# Payment Schedule

FORM 1040-V. Slug `PaymentSchedule`. 28 HP, monophonic.

> *Modular in a Week*, Day 10: Kristian Blåsol's Baby8 (a 4017 decade counter
> read out as eight CV/gate steps), the sequential switch built from the same
> family of counter (a HEF4516 up/down counter driving a CD4051 8:1 analog
> mux in the original MiaW design), the dual 4031 tap sequencer/looper, and a
> PIC16F684 varimode CV quantizer (`varimodequantizer_100.asm`, Rev 1.0,
> 2010-05-29 -- the source carries no author name beyond the MiaW course
> material it ships in, so credit goes to the course and to whoever wrote
> that firmware, uncredited in the file itself). Payment Schedule fuses all
> four into one module: one counter drives the steps, the switch and the
looper all at once, and the quantizer sits on the CV path leaving it.

## What it is

Eight steps, each with its own knob and a **B** jack pair. The counter behind
them is a decade counter exactly like the Baby8's 4017: on each `CLOCK` edge
it advances one step, up or down, and wraps at whatever length `STEPS` is set
to. Read one way that counter is a sequencer (each step holds a CV level);
read the other way, it's a sequential switch (only the current step's pair of
jacks is live) -- which is why `A IN` / `A OUT` do double duty as the
module's plain audio/CV in and out **and** as the common pole of an 8-way
switch. A second, independent circuit -- the 4031 tap looper -- lays a
longer, freely-tapped gate pattern over the top, clocked from the same
source. A varimode quantizer (the PIC firmware's five scales) sits on the CV
leaving `A OUT`.

## Per-step controls (x8, columns 1-8)

- **B IN** (jack) -- normalled to that step's own `STEP` knob. Patch a CV
  here and it overrides the knob for that step only.
- **STEP** (knob) -- that step's raw CV level, 0 to the `RANGE` switch's full
  scale.
- **GATE** (lit button, latching) -- on by default. Off makes the step a
  rest: its `B OUT` stays at 0 V and `A OUT` holds whatever it last was,
  rather than jumping to silence. The count still visits a muted step on its
  way round; it just has nothing to say when it gets there. Lit dim when the
  step is enabled but not current, full brightness when it is both enabled
  and current, dark when muted.
- **B OUT** (jack) -- `A IN` (normalled to +10 V) routed here while this step
  is current and its gate is on, 0 V otherwise. With `A IN` unpatched this is
  a plain per-step gate output; patch something else into `A IN` and it
  becomes an 8-way demultiplexer.

## Transport and chaining

- **UP** / **DN** (lights flanking the STEPS/DIR/SCALE trio, one knob's-width
  out from **DIR** on each side) -- show which way the count is currently
  running.
- **DIR** (switch) -- Down / Up.
- **DIR CV** (jack) -- 1 V or more flips whatever `DIR` says.
- **STEPS** (knob, 1-8) -- how many of the eight steps the count cycles
  through before wrapping -- the software equivalent of patching a step's
  own reset line back to itself on the original hardware.
- **CLOCK** (jack, lit) -- advances the count.
- **RESET** (jack) -- jumps to step 1.
- **ENABLE** (jack) -- unpatched, the count runs freely on `CLOCK`. Patched,
  the count only advances while this is 1 V or higher; low holds it wherever
  it is, gate/enable in the Baby8 sense.
- **CH IN** / **CH OUT** (jacks) -- cascading two modules together, after
  Part 4 of the Baby8 build. `CH OUT` fires a 1 ms trigger whenever the count
  wraps (in whichever direction it is currently running); patch it into the
  next module's `CH IN` (wired the same as `RESET`) to chain a second
  eight-step run onto the first, or into its own `RESET` for a longer cycle
  through a single module.

## Tap looper (4031)

A second gate track, independent of the eight steps, up to 64 slots long.

- **SCALE** / **ROOT** / **LENGTH** knobs and the **TAP** / **REC** / **CLR**
  bezels share the row with the transport controls above; **TAP IN** and
  **LOOP GT** are jacks in the row below.
- **LENGTH** (knob) -- 16, 32 or 64 slots. The write/playback head advances
  one slot per `CLOCK` edge and wraps at this length -- "quantised to the
  clock" rather than free-running the way the original 4031's own clock
  input could be.
- **TAP** (bezel) / **TAP IN** (jack) -- either one marks the current slot.
- **REC** (lit button, latching) -- while lit, a tap ORs a hit into the
  current slot (overdub: existing hits are never erased by tapping elsewhere).
  Off, tapping does nothing to the pattern.
- **CLR** (bezel) -- clears every slot.
- **LOOP GT** (jack, out) -- 10 V while the current slot holds a hit, 0 V
  otherwise.

## Quantizer (varimode)

- **SCALE** (knob) -- Chromatic, Major, Minor, Major pentatonic, Minor
  pentatonic -- the firmware's own five modes.
- **ROOT** (knob) -- C through B.
- **QUANT** (switch) -- On quantizes `A OUT`; Off passes the raw per-step CV
  straight through.
- **RANGE** (3-way switch) -- 1 V / 5 V / 10 V, the full-scale voltage a
  `STEP` knob (or an unpatched `B IN`) reaches at full turn. At 1 V/oct this
  is one octave, five octaves, or ten octaves of range for the eight steps.
- **TRIG** (jack, out) -- a 1 ms trigger whenever the quantized value at
  `A OUT` changes from what it last was (repeats on the same note don't
  retrigger).

## Main I/O

- **A IN** (jack) -- the sequential switch's common input; normalled to
  +10 V, which is what makes an unpatched `B OUT` read as a plain gate.
- **A OUT** (jack) -- the switch's common output *and* the sequencer's CV
  out: whichever step is current and enabled, quantized if `QUANT` is on.

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
- `CH IN` / `CH OUT` are a synthesis of the hardware's "cascade in/reset in"
  and "cascade out/reset out" jacks (which, on the original board, are
  literally the same wire as reset, broken by a jack normalling when a cable
  is inserted) into a same-effect, cleaner pair: `CH OUT` pulses on wrap
  rather than mirroring reset outward.
- Polyphony: kept monophonic throughout, matching the brief and the
  originals -- none of the four source circuits are voice-per-channel
  designs.

## Voltage conventions

Audio/CV I/O clamps to ±12 V. Gates and triggers are 0/10 V, 1 ms
pulses for triggers. `CLOCK`, `RESET`, `ENABLE`, `CH IN` and `TAP IN` read as
gates/triggers with Schmitt hysteresis (low below 0.1 V, high at 2 V or
above). 1 V/oct at `A OUT` when `QUANT` is on and `RANGE` is set to 10 V.

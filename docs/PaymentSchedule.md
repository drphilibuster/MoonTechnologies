# Payment Schedule — 8-step sequencer

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
> looper all at once, and the quantizer sits on the CV path leaving it. The
> quantizer is a PIC16F684 running firmware, emulated instruction by instruction;
> the firmware it runs by default is this project's own (see below), and the
> course's can be loaded from the context menu.

## What it is

Eight steps, each with its own knob and a **CV IN** / **GATE OUT** jack pair.
The counter behind them is the Baby8's 74HC4017, from its datasheet
(`src/Cd4017.hpp`): a Johnson decade counter that advances on each `CLOCK` rising
edge and wraps at whatever length `STEPS` is set to. Read one way that counter is a sequencer (each step
holds a CV level); read the other way, it's a sequential switch (only the
current step's pair of jacks is live) -- which is why `SWITCH IN` / `CV OUT`
do double duty as the module's plain audio/CV in and out **and** as the
common pole of an 8-way switch. A second, independent chip -- the CD4031B of the 4031 tap
looper, a 64-stage shift register -- records a freely-tapped gate pattern 64
clocks long, clocked from the same source. A varimode quantizer (five scales), a PIC16F684 running firmware, sits
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
- **DIR** (switch) -- Down / Up. A 74HC4017 only counts up; down is this
  module's addition (the sequential switch's HEF4516 was an up/down counter, and
  this one counter does both jobs), stepping the Johnson counter backwards.
- **DIR CV** (jack) -- 1 V or more flips whatever `DIR` says.
- **STEPS** (knob, 1-8) -- how many of the eight steps the count cycles
  through before wrapping. On the Baby8 this is a reset jumper: the output Qn
  is wired back to the chip's MR pin, so the counter is in states 0 to n-1 and
  is reset the instant it reaches n.
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
- **CLOCK** (jack, lit) -- advances the count, while `RUN` is on. `RUN` is the
  AND gate in front of the 74HC4017's CP0 on the Baby8 (its Start/Stop switch),
  so starting with the clock already high is a rising edge and counts once.
- **RESET** (jack) -- the 74HC4017's MR pin: a *level*, not an edge. While it is
  high the counter is held at step 1 and the clock is ignored, whatever `RUN`
  says; the first clock after it falls goes to step 2. It resets the steps and
  not the tap loop, which is a separate chip.
- **EOC** (jack, out) -- "end of cycle": fires a 1 ms trigger whenever the
  count wraps (in whichever direction it is currently running). Patch it into
  the next module's `CYCLE` to chain a second eight-step run onto the first,
  or into its own `RESET` for a longer cycle through a single module.

## Tap looper (4031)

The Tiny Dazzler / Magic Pulsewave gate recorder: a **CD4031B** (`src/Cd4031.hpp`),
a 64-stage static shift register, wired as its schematic wires it. MODE CONTROL
and RECIRCULATE are on ground; DATA IN is a diode OR of the tap and Q; CLEAR is a
normally-closed switch in Q's path. The clock is the sequencer's own, so **the loop
is 64 clocks long whatever `STEPS` is** -- "clock speed sets pattern time and
resolution". At a sixteenth-note clock that is four bars. The counter and the loop
are separate chips: they share a clock, not a position, so the loop need not line
up with the eight steps (with `STEPS` at 8, a pattern comes round once every eight
passes), and `RESET` and `DIR` leave it alone.

- **SCALE** / **ROOT** knobs and the **TAP** / **REC** / **CLR** bezels share
  the row with the transport controls above; **TAP IN** and **LOOP GATE** are
  jacks in the row below.
- **TAP** (bezel) / **TAP IN** (jack) -- either one is the tap. It is read as a
  *level* when the clock rises, because that is when DATA IN is read: a tap that
  has gone again before the clock rises is not recorded. Hold the button for a
  clock period, or send a gate at least that long.
- **REC** (lit button, latching) -- while lit, the tap reaches DATA IN, and is ORed
  with what is already circulating (overdub: existing hits are never erased by
  tapping elsewhere). Off, tapping does nothing to the pattern. The hardware has
  no REC; its tap is always live. This gate is the module's.
- **CLR** (bezel) -- the Clear switch: while it is held Q is not fed back. It does
  not empty the register -- nothing can -- so the old pattern drains out over the
  next 64 clocks, and a clear released sooner leaves whatever had not reached Q.
- **LOOP GATE** (jack, out) -- the 4031's Q, stage 64: 10 V while the pattern has
  a hit under the current clock, for the whole clock period, 0 V otherwise. The
  schematic also has a gated output (Q ANDed with the clock) that this panel has no
  jack for.

## Quantizer (varimode)

The quantizer is a **PIC16F684**, the part the Day 10 board uses, emulated instruction by
instruction (`src/Pic16f684.hpp`: the whole mid-range instruction set, the A/D converter, TMR2
and the CCP1 PWM, with the chip's own timing) and running real firmware. The board's pin
contract is the module's: the CV goes to the A/D converter on AN4 as 0-5 V, the mode to AN5, and
the chip's PWM on CCP1 is the quantized output, five volts times duty over 1024, so a note is
n/12 volts.

**The built-in firmware is this project's own** (`firmware/varimode/varimode_fixed.asm`, assembled
by `tools/pic16asm.py` and embedded). It does the board's job and fixes what the course's does
not: its PWM steps are exactly 1/12 V (the course's table is uneven and up to four counts, about
26 cents, off); it reads the input at 10 bits rather than 8; every input has an output (the
course's major, minor and minor pentatonic tables keep the last note above 253 of 255, and its
major pentatonic table runs on into the minor one); and each scale picks the true nearest degree,
an exact tie going up. `tests/Pic16` checks it against an independent quantizer on all 1024 input
codes in all five modes.

**Any other firmware that keeps the pin contract runs here.** *Load PIC firmware (.HEX)...* in the
context menu loads one, the course's `varimodequantizer_100.HEX` included; the patch stores its
path and never its bytes, and *Use the built-in firmware* puts ours back. The course's firmware is
third-party, with no licence stated, so it is never carried in this repository. The menu says
when a firmware does something the emulator does not model (an interrupt, `SLEEP`, a prescaled
TMR2, a CCP mode other than PWM).

- **SCALE** (knob) -- Chromatic, Major, Minor, Major pentatonic, Minor
  pentatonic -- the firmware's five modes, selected as the board selects them, by a voltage on
  AN5: the switch puts the middle of each mode's band there (0-52 major, 53-104 major
  pentatonic, 105-153 minor, 154-204 minor pentatonic, 205-255 chromatic, in the top 8 bits).
- **ROOT** (knob) -- C through B. The board has no root; this module's is the input moved down
  and the output back up by the root, so the scale is rooted there. Near the ends of the 0-5 V
  range the converter's clamp is in the way (a root of B turns a 0.3 V input into a negative one,
  which reads as 0 V).
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
- **GATE LEN** (knob) -- how long each `GATE OUT` stays high, as a fraction
  of the step: 1% to 100% of the clock period last measured. Full clockwise is
  100%, the default: the gate holds until the next step, as it always did, so
  existing patches are unaffected. Turned down, the gates become shorter pulses
  that track the clock tempo. It shapes `GATE OUT` only; the step's CV, `TRIG`
  and `EOC` are unchanged. The first step after a start has no period to
  measure yet, so its gate waits for the second clock when `GATE LEN` is
  below 100%.
- **TRIG** (jack, out) -- a 1 ms trigger whenever the quantizer picks a new note (repeats on the
  same note don't retrigger). With QUANT off it fires when `CV OUT` moves. The output itself
  glides to the new note over a few milliseconds (see the next section), but the trigger is on the
  firmware's decision, not on the voltage.

### What the quantizer does in time

The chip is not instant. A pass of the loop takes 794 cycles, 159 microseconds, and a new note is
on the PWM within about 220 microseconds of the input changing. The PWM is 19.5 kHz, and what
comes out of `CV OUT` is that after a filter: two one-pole low-passes at 160 Hz, about a
millisecond each, which is what takes the ripple to under a millivolt. **That filter is assumed:**
the course folder has the firmware but not the board's schematic, so the board's own filter (and
whether the converter's input has a buffer or a divider ahead of it) is not on record.

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
  hardware used. The Baby8 can also run with no reset jumper, which leaves the
  74HC4017 counting all ten of its states (two of them silent); `STEPS` stops at 8.
- **The HEF4516 and CD4051 of the sequential switch are not modelled as chips.**
  The merged module has one counter driving everything, so the 4516's job -- a
  binary up/down count whose Q0 to Q2 address the 4051 -- is the counter's step
  number, and the mux is direct routing in `process()`. What that leaves out has no
  jack to reach it: the 4516's preset (PL, D0-D3), count enable and terminal count,
  the 4051's inhibit pin (high turns every channel off) and its break-before-make
  switching, its on-resistance, and the high-impedance output of an unselected
  channel, which here is 0 V.
- **Down is not a chip behaviour.** The 74HC4017 counts one way, the 4516 counts
  down through 16 states with no way to wrap at `STEPS`; the module's down
  direction, wrapping to `STEPS` - 1, is its own.
- **The 4017's outputs are exclusive and glitch-free here.** On the Baby8 the output
  Qn that resets the counter pulses for a few nanoseconds as the counter reaches it;
  that pulse is not modelled (it would only show up as a spike on a gate output
  nothing can see). The datasheet's automatic code correction, which returns an
  illegal code to a proper count within 11 clocks, only matters from a random
  power-up state and is not modelled; the counter starts at step 1.
- **The 4031's own pins are used as drawn, not exhaustively.** DATA IN 1 is the
  only data input in use; RECIRCULATE and MODE CONTROL are on ground, as in the
  schematic. The 1/2 stage (Q') and the inverted output (Q-bar) are in the chip
  model and not on the panel, and CLD, the delayed clock for cascading, is not
  modelled. Timing (setup and hold, propagation delay) is not modelled either.
- **TAP is level-sensitive as the schematic has it, with no stretching**, so a short
  trigger arriving between clocks is lost. The tap comparator and its debounce
  (SW1, R3, C1) are not in the model; the Schmitt levels are Rack's (0.1 V and
  2 V) rather than the comparators' roughly one volt.
- A patch saved before the loop was a register holds eight slots, one per step. They
  are tiled along the 64 stages on load so each gate lands on the step it was saved
  on; a `STEPS` that does not divide 64 (3, 5, 6, 7) cannot tile exactly, and the
  pattern is cut at 64.
- The sequential switch's own chips (HEF4516 up/down counter, CD4051 8:1
  mux) aren't emulated as discrete parts; see above.
- **The quantizer's analogue surroundings are assumed.** The chip, its firmware and its timing are
  the real thing; the board around it is not on record. Nothing conditions the input (the
  converter's own 0-5 V range is the range: below ground reads 0, above 5 V reads full scale);
  the PWM goes through two poles at 160 Hz (`Varimode.hpp`, `assumed::PWM_FILTER_HZ`); the chip
  runs from an ideal 5 V and a 20 MHz clock, and its configuration word is the programmer's, not
  the firmware's. Not modelled: the converter's real accuracy (about a count), the channel-change
  acquisition error the course's firmware leaves by not waiting after switching from AN4 to AN5,
  interrupts, `SLEEP`, the watchdog, and every peripheral the two firmwares do not touch.
- **ROOT is this module's, not the board's.** See above.
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

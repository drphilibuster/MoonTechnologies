# Calculation — WORKSHEET

**A phrase counter for arranging a patch.** You set one base length, N, at the
top. Each of the six lines below it falls due at N times its own ratio, from ÷8
to ×32, and each line has its own trigger, gate and counter. 16 HP, monophonic.

## Why it exists

Count Modula's Event Timer (Countdown 3 and 5) is a good way to change a patch
after a set number of clocks. People build whole arrangements out of it: one
timer's EOC starts the next, and RESET re-arms a timer so its section can come
back. Two things make those arrangements clumsy, and Calculation fixes both.

- **Most counts are multiples of one length.** A section is 16 bars, the next
  is 32, the drop is 64. With separate timers, each of those takes another
  module. Here, one N feeds six lines.
- **Chained timers need N−1.** An Event Timer counts the START beat only when
  START lands on *the same sample* as a clock edge. Every cable in Rack delays
  its signal by one sample, so a START that comes from another timer's EOC
  always misses the clock edge that caused it. As a result, the first timer in
  a chain lasts N−1 clocks and every chained timer lasts N, so the two never
  agree.

## Counting

Calculation counts clocks from a **downbeat**, which is tick 0. A line whose
length is L falls due on the clock edge L clocks after the downbeat. With N =
16 and a line set to ×2, that line fires on the 32nd clock after you press
START, which is the downbeat of bar 9 if each clock is a bar. Anything that
line starts begins on that same downbeat.

Which clock edge becomes the downbeat depends on when START arrives:

- **Up to 1 ms after a clock edge** (including on the same sample), START
  claims that edge. This covers the cable delay between one Calculation's TRIG
  and another's START, so chains don't drift.
- **Between clocks**, Calculation waits, and the next clock edge becomes the
  downbeat. The RUN light glows dim while it waits.

A line's length is **N × ratio**, rounded to the nearest whole clock (halves
round up), and is never shorter than one clock. With N = 6, ÷4 gives 2.

Lengths are read live. If you shorten a line to a length the phrase has
already passed, the line falls due on the next clock.

## Controls

| | |
|---|---|
| **Glass** | Shows N, the phrase's state (READY, ARMED, RUN, HOLD, DONE) and how many clocks have passed. **Click the glass to type a number for N** and press Enter. Cmd+Z undoes it. |
| **STEPS** | N, from 1 to 9999. The knob moves about one step per four pixels, so it is for nudging. For a big change, type the number into the glass or into the knob's right-click field. |
| **− / +** | Change N by one. |
| **START** | Starts the phrase. After a STOP, it resumes from where the phrase stopped, without a new downbeat. Once the phrase is DONE, START does nothing until RESET, unless *Retrigger* is on. |
| **STOP** | Pauses. The count is kept. |
| **RESET** | Sets the count back to zero, clears every gate and stops. RESET and START together restart the phrase. |

Each line, read left to right:

| | |
|---|---|
| **MULT** | The line's ratio of N: ÷8 ÷4 ÷3 ÷2 ×1 ×2 … ×32. The defaults are ×1 to ×6. |
| **RATIO** | Shows the ratio the line is actually using, including any CV. |
| **CV** | Added to MULT at 1 V per step. |
| **DUE IN** | How many clocks until the line next falls due. |
| **TRIG** | A 1 ms trigger when the line falls due. The light beside it flashes. |
| **GATE** | Goes high when the line falls due and stays high until RESET, the same as Countdown's END gate. On a repeating line it toggles instead (see below). |

Footer jacks:

- **CLOCK**, **START**, **STOP** and **RESET** are inputs that work the same as
  the buttons.
- **RUN** is high while the phrase is counting.

## Right-click menu

- **Start: on the downbeat** (the default) works as described above.
- **Start: Countdown-compatible** follows the Event Timer's rules exactly. A
  START on the same sample as a clock counts that clock as tick 1, and a later
  START counts from the next clock. Use it to rebuild an old Countdown patch
  one-to-one.
- **Retrigger.** A START while the phrase is running, stopped or DONE restarts
  it from a new downbeat.
- **Repeat, per line.** The line falls due every N × ratio clocks instead of
  once, and its GATE toggles each time. This is useful for switching between A
  and B. A phrase with any repeating line keeps running until you STOP or
  RESET it. Otherwise it stops by itself once every line has fallen due.

## Patching it

- **One arrangement.** Clock into CLOCK and a start into START, with N set to
  one section. Each line's TRIG or GATE then drives one change: line 1 after
  one section, line 4 after four, and so on.
- **Longer than ×32.** Patch line 6's TRIG into the START of a second
  Calculation. It starts on the same downbeat that line 6 fell due on.
- **Coming back.** Patch a line's TRIG into RESET and START. The phrase
  restarts on the same clock, so it loops with no gap or overlap.

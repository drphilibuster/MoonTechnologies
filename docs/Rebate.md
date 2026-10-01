# Rebate

**FORM 843 — claim for refund. Some of what you put in, paid back to you later, a little less each time.** 12 HP.

An Alesis MIDIverb, Keith Barr's 1986 reverb, running its own firmware. The
MIDIverb has no DSP chip. Its signal processor is a few dozen TTL parts
stepping through a 16 KB microcode EPROM: 63 programs of 128 two-byte
instructions each, against 16K words of delay RAM, 23,437.5 times a second. An
80C31 microcontroller reads the buttons, drives the two digits, listens to
MIDI and picks the program. Rebate emulates both on one 6 MHz clock and lets
them do what they did in 1986. It also runs the **MIDIFEX**, which is the same
board with a different microcode EPROM: echoes, flanges and choruses instead of
rooms and halls.

---

## Before anything works: the EPROMs

The firmware is Alesis's and is **not included**. You need two images:

| EPROM | what | size |
|---|---|---|
| U54 "MVOP 4-7-86" | the 80C31's program (shared by MIDIverb and MIDIFEX) | 8 KB (2764) |
| U51 "MVOBJ 2-6-86" | the MIDIverb's microcode | 16 KB (27128), or a 32 KB 27256 dump, of which the board uses the upper half |
| U51 "MIDIFEX 7-17-86" | the MIDIFEX's microcode | 16 KB |

Right-click the module and choose **Load EPROM folder as MIDIverb…** (or **as
MIDIFEX…**). Point it at a folder holding the CPU image and a microcode image.
The module tells the CPU image from the microcode by size, and the MIDIverb's
microcode from the MIDIFEX's by checksum. **Load CPU EPROM…** and **Load DSP
EPROM…** load them one at a time. The module remembers the last pair that
booted (in `MoonTechnologies/settings.json` in Rack's user folder), so the next
Rebate you add finds them by itself. A patch stores the *paths*, never the
contents. Until both are loaded the status line says so and the module is
silent.

## The front panel

It is the MIDIverb's own.

* **The two digits** show the program, 1 to 63, exactly as the firmware lights
  them. They show `--` when the effect is defeated.
* **UP / DOWN** step through the programs, and auto-repeat when held. Every
  change mutes the effect for about a tenth of a second while it switches.
  That is the firmware: it runs the silent program 64 in between, so the old
  program's delay memory does not spill into the new one.
* **CHANNEL**: hold it and the digits show the MIDI receive channel. UP and DOWN
  change the channel while you hold it.
* **DEFEAT** mutes the effect (program 64). Press it again to bring the program
  back.
* **-12 dB / 0 dB** are the unit's input level LEDs. They watch the signal on
  its way into the converter. Red means you are close to the converter's full
  scale.
* **MIX** is the dual-gang pot on the unit's back: dry at one end, wet at the
  other.

**MIDI**: choose a MIDI input in the context menu. The firmware answers Program
Change on its channel (program 0 is the first), and nothing else. Running status
is not understood, as on the unit. Rack's MIDI is turned back into a serial line
at 31,250 baud and the 80C31's UART receives it bit by bit.

**What the patch keeps.** The real unit keeps nothing: it always wakes on
program 22, channel 1. A patch remembers the program, the channel and defeat,
and after power-on the module plays them back into the firmware: it holds
CHANNEL and taps UP, sends a Program Change, presses DEFEAT. Watch the digits
and you will see it happen. The panel buttons are ignored for those couple of
seconds.

## Levels

A 10 V peak-to-peak Rack signal is taken as 1 V peak at the unit's jack, a hot
line level. The output is scaled back the same way, so fully dry the module has
the unit's own gain of 0.975. The unit has no input level control. With both
inputs at ±5 V the converter clips, and the red LED says so before you hear it.
The two inputs are summed to mono into the effect, as on the unit, and are not
normalled to each other: a signal only into IN L comes out dry only on OUT L.

## What is emulated

* **The 80C31**, instruction by instruction with its cycle counts: timers,
  interrupts, and the serial port receiving MIDI a bit at a time. It runs the
  real firmware.
* **The DSP**, from MAME's driver (BSD-3, m1macrophage). It is verified sample
  for sample against MAME's own loop on all 64 programs of both EPROMs. It runs
  interleaved with the CPU on the oscillator clock, so a program change reaches
  the microcode at the instruction it would on the board.
* **The analog board**, from Eric Brombaugh's schematic (MIDIVerb_RE, MIT), at
  four times the DSP's rate:
  * the input high-pass and ×5.17 buffers;
  * three Sallen-Key filters, together a 6-pole low-pass with a +11 dB peak near
    9.8 kHz, which is pre-emphasis for the 23 kHz converter;
  * the diode on the third filter that the schematic marks "Clipping?". It clamps
    positive peaks to the analog switch's +5 V supply plus a diode drop. That
    supply is a 1k/0.1 µF RC, so a long clip lifts it: the clamp is soft and has
    a memory.
  * the ADC's sample-and-hold, and the one sample it takes to convert;
  * the DAC's two hold capacitors, right then left, 5 µs apart;
  * the output RC and Sallen-Key, solved as the single third-order network they
    are (there is no buffer between them), exactly for the staircase the DAC
    feeds them;
  * the mix pot, loaded by the output divider.
* **Not modelled, or assumed** (wanted: measurements of a real unit): the mix
  pot's value (10k assumed, which makes the middle of its travel quieter than
  either end), the TL082s' output swing (±10.5 V), the analog switch's
  on-resistance, D1 taken as a 1N4148, and how the level LEDs' second
  transistor stage really behaves. The LEDs are modelled from the 0 dB stage's
  threshold, and the -12 dB LED 12 dB below it.

The Microverb is a different machine, not this board with another EPROM, and is
not supported.

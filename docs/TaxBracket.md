# Tax Bracket

*TAX RATE SCHEDULE X — Moon Technologies, 12 HP*

An emulation of the **Olegtron R2R**, the passive "true multi-function utility
module": a string of resistors with jacks hanging off it, and nothing else. It
is an 8-bit digital-to-analog converter, an eight-channel weighted mixer, a
programmable attenuator and a rather unreliable multiple — all at once,
depending only on what you plug where. Design and manual by
[Olegtron](https://www.olegtron.com/) (Finland); this module reproduces the
resistor network, not the circuit board, and adds the things a modular
emulation can that a passive panel cannot.

## The network

Read off the schematic in the R2R manual:

```
GND ─20k─ N1 ─10k─ N2 ─10k─ N3 ─10k─ N4 ─10k─ N5 ─10k─ N6 ─10k─ N7 ─10k─ N8 ── I/O
          │        │        │        │        │        │        │        │
         20k      20k      20k      20k      20k      20k      20k      20k
          │        │        │        │        │        │        │        │
          1        2        4        8        16       32       64      128
```

Eight ladder nodes. Jacks **1** to **128** each reach their node through a
20 k branch resistor; the **I/O** jack *is* the top node, with no branch — which
is why the manual says I/O to 128 is "only a 20 k impedance". The bottom of the
string is tied to ground through 20 k, the standard R-2R termination. The
manual's three worked circuits (I/O → 4 is 40 k below and 50 k above; 64 → 32
is 70 k to ground, 20 k branch, 30 k on to 64; 2 + 16 + 128 + I/O) and its
attenuator table all come out of exactly these values.

Every jack on the hardware is bidirectional. Rack ports are not, so **each
hardware jack is a pair here**: a sage **input** (the jack being driven) and a
mint **output** (the same jack being read), sharing one number. Rules:

* A jack with a cable in its **input** is a voltage source, through its branch
  resistor (I/O: directly, since it is the node).
* A jack with nothing in its input is open, or — with GROUND on — tied to 0 V
  through its branch resistor.
* Every **output** reads its jack's node voltage, unloaded. (Rack inputs are
  ideal, so the 20 k output impedance the manual warns about drops nothing.)
  The output of a jack that is also being driven reads the node, i.e. what the
  rest of the ladder sees of that input — its voltage less the drop across the
  branch. For I/O the two are the same thing.

Each time the pattern of connections changes the module re-solves the network
(nodal analysis on the 8×8 conductance matrix, once); per sample the node
voltages are then a fixed linear combination of the input voltages. It is a
genuine resistor-network solution, not a table of the four "modes" — so every
combination the manual calls the *Goofproof Whatever Station* works exactly as
the resistors would have it.

## The four uses

**8-bit DAC.** Gates or logic into some of **1 … 128** — the number is the
bit's binary weight — read the result at **I/O**. With all eight inputs
connected, I/O = (b₀·1 + b₁·2 + … + b₇·128) / 256 of the gate voltage: 10 V
gates give 0 … 9.96 V in 256 steps. Bits you leave unconnected float rather
than reading as zero, which biases the conversion upward (the manual's point
about only-plugged jacks having real weights); switch **GROUND** on to have
them count as LOW. Reading from a lower jack instead of I/O gives the same
sequence at a smaller, differently-shaped scale.

**Weighted mixer.** The same thing with signals that are not gates: the module
"does accept other signals, AC/DC, you name it". No level knobs — a channel's
level is which jack you put it in. 1 is the quietest, 128 the loudest, and
jacks near each other hear each other more. Any unused jack is a mix output,
each a different blend.

**Programmable attenuator.** Feed **I/O**, read the taps. With nothing else
connected the ratios are exactly the manual's:

| jack | 1 | 2 | 4 | 8 | 16 | 32 | 64 | 128 |
|---|---|---|---|---|---|---|---|---|
| fraction of I/O | 0.22 | 0.33 | 0.44 | 0.56 | 0.67 | 0.78 | 0.89 | 1.00 |

(N_k / N_8 = (k + 1) / 9 for the k-th node.) Any higher jack into any lower one
is likewise a divider — 64 into 32 gives 0.70 — and a set of taps from one
signal makes "lively harmonic intervals" on a bank of oscillators.

**Labile duplex multiple.** I/O into 128 is a unity multiple (0 mV drop); a
lower jack into higher ones is "more or less" a multiple — more or less, as the
manual says. Everything affects everything else; that is the instrument.

## Controls

| control | what it does |
|---|---|
| **GROUND** (switch) | The official Olegtron mod. Down: unplugged jacks 1 … 128 float, as the stock unit does. Up: they are normalized to ground through their branch resistor, which tames the module, makes a DAC read a reliable 0 V with all bits low, and makes the jack numbers the true weights. A jack with a cable in *either* of its ports counts as plugged, as on the hardware. |
| **SCALE** (trim) | Output gain for every output, 0 – 200 %, default 100 %. Not on the original: the passive ladder cannot make up what it divides away. |

## Jacks

| jack | as input | as output |
|---|---|---|
| **1, 2, 4, 8, 16, 32, 64, 128** | a source through 20 k into node 1 … 8 | that node, unloaded |
| **I/O** | pins node 8 outright | node 8 |

All 18 ports are polyphonic: the module runs one ladder per channel, channel
count = the widest connected input, and monophonic inputs are copied to every
channel. Outputs are clamped to ±12 V. Bypass passes each input straight to its
own output.

## Read-out

**DUE** is the voltage at I/O (channel 1, after SCALE). To its right is the
8-bit word the ladder is being handed, MSB first: `1`/`0` for a driven jack
above or below 1 V, `0` for a grounded one, `-` for one left floating.

## Context menu

* **Ground I/O when unplugged** — the manual's "same principle with the I/O
  jack, for even more attenuation". Because I/O is the top node itself, this
  pins the far end of the string to 0 V, so every other input becomes a divider
  between two grounds. Off by default; saved with the patch.

## Voltage conventions

Anything goes — it is a resistor network. Gates of 0/10 V make a 0 … 10 V DAC,
±5 V audio mixes as audio, 1 V/oct in I/O comes out at 0.22 … 1.00 of itself.
Nothing inside has a threshold; the read-out's 1 V line is purely cosmetic.

## What is approximated

* Resistors are exact; the hardware's are ±2 %.
* Outputs are read unloaded. A real destination with a finite input impedance
  loads the tap slightly; Rack's do not.
* The hand-made "component plugs" (a capacitor across a jack for glide) are not
  modelled — patch a slew limiter after the tap instead.

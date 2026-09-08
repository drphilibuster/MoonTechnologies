# Toll — FORM 2290

**A struck-metal voice.** Sixteen modal partials over a membrane, a bar, a bell
or a bare harmonic series, with the parameters that decide *what kind of object
is being hit* brought out onto the panel: where it is struck, with what, how
fast its upper partials die, and how far its partials are stretched from where
they belong. 12 HP, monophonic.

Form 2290 is the IRS's *Heavy Highway Vehicle Use Tax* — the toll, filed.

## Where it came from

Toll was a voice in [Kickback](Kickback.md) and left. BaSnaHi's snare stage — a
resonant pair shocked by a trigger edge with a second layer buzzing against it —
modelled as a membrane read as a struck metal pipe rather than as a snare. That
is a fine thing to be and a bad snare, so it took its own panel and the room to
have controls for what it actually is.

Everything structural is shared with the drum bank: `src/Drum.hpp` holds the
contact pulse, the click, the attack layer, the tension term and the wire bed,
and [Kickback's manual](Kickback.md#how-the-drums-are-synthesised) has the
papers behind them. In short:

- **The strike is a pulse, not an impulse** — Bilbao's raised cosine, whose
  duration is the mallet. A long, soft contact cannot put energy into a partial
  whose period is shorter than the contact, so HARD is what decides how far up
  sixteen partials the strike reaches.
- **The click is summed in directly**, because a bank of narrow resonators
  filters the mallet out of its own strike.
- **The pitch glide is driven by the object's own energy**, so a hard strike
  dives and a soft one does not.

## What it is made of

**SET** chooses the partial series. This is the control the module exists for:
the same sixteen resonators, the same strike, and four different objects.

| | partials | what it is |
|---|---|---|
| **MEMBRANE** | 1, 1.593, 2.136, 2.295, 2.653 … | The Bessel-zero ratios of an ideal circular head — Kickback's toms, with twice as many partials. |
| **BAR** | 1, 2.756, 5.404, 8.933, 13.34 … | Free-free flexural modes, spreading as the squares of the roots of the beam equation. A glockenspiel or a marimba bar before its underside is carved. |
| **BELL** | 1, 2, 2.4, 3, 4, 5, 5.333, 6 … | A tuned bell's own named partials — hum, prime, tierce, quint, nominal, deciem, undeciem, duodeciem. The tierce at 2.4 is the minor third that makes a bell sound like a bell and not like a pipe. |
| **HARMONIC** | 1, 2, 3, 4, 5 … | Not an object at all. It is the thing a resonator bank can do that no struck object does, and the reason to have a mode bank on a panel rather than buried inside a drum. |

**SPREAD** then bends whichever set is chosen away from itself: partial *n*
moves to *n*^(1+s), the standard inharmonicity stretch. A harmonic bank slides
into bell-like territory, and a bell stretches into something that was never
cast. At zero it is exactly the printed ratios.

## The controls

| control | what it does |
|---|---|
| **TUNE** | The fundamental, 27.5 Hz to 1760 Hz — A0 to A6. |
| **V/OCT** (input) | 1 V per octave on top of TUNE, clamped to ±6 V. |
| **SET** (4 detents) | Membrane / Bar / Bell / Harmonic. |
| **BUZZ** | The loose layer against the body: Bilbao's one-sided power-law collision, which only speaks once the body swings far enough to reach it. So a soft strike is a clean ring and a hard one clatters, and at 0 there is nothing there at all. |
| **DECAY** | Ring time, 30 ms to 12 seconds. |
| **DAMP** | How much faster the upper partials die than the fundamental. At 0 the object rings out whole; at 1 only the fundamental survives. This is most of what separates bronze from lead. |
| **BEND** | The tension term — how far the pitch falls as the strike's energy leaves the object. Driven by the object's own amplitude, so it follows how hard you hit. |
| **STRIKE** | Where it is hit, from the middle outward. On a membrane this is physics: partial (m,n)'s displacement at radius ρ is `J_m(x(m,n)·ρ)`, so a central strike wakes only the circular modes. A bar and a bell have no radial modes, so there it is the rule every struck object obeys — hit it at a node and that partial does not sound. |
| **HARD** | The mallet: three milliseconds of felt at 0, a third of a millisecond of wood at 1. |
| **STRK**, **DECAY** (inputs, footer) | CV for strike position and ring time. |
| **SPRD**, **BEND** (inputs) | CV for the partial stretch and the tension bend. |
| **CHOKE** (input) | The hand on the bell. A gate — a level, not an edge — that shortens the ring to 35 ms for as long as it is held. It damps rather than mutes, because a damped object still rings briefly and a gated one conspicuously does not. |
| **TRIG** (input) | ≥1 V rising edge strikes it (Schmitt hysteresis, low 0.1 V / high 1 V). Lights on strike. Retriggerable. |
| **VEL** (input) | How hard, 0–10 V, read at the strike and nowhere else — a strike is an instant, so what the jack does between two of them cannot matter. Unpatched is full force, exactly what the voice did before the jack existed, so 10 V and no cable are the same sound. 0 V is not silence but a ghost note: a velocity of literally zero is a strike that never lands, and a trigger arriving while some modulation happens to be resting at zero would read as a broken patch. |
| **OUT** | ±5 V nominal, hard-clamped to ±12 V. |

The four CV jacks add to their knobs at a tenth of a volt per percent, so ±5 V
covers a whole control and a unipolar 0–10 V reaches from its bottom to its top.

### What velocity actually changes

Mostly the level — but with BUZZ up it changes the sound. The loose layer is a
one-sided collision, so it only speaks once the body swings far enough to reach
it: level-matched against a full-force strike, a strike at 15% has about a
quarter of the energy above 2 kHz, because it never reaches the layer at all.
With BUZZ at 0 there is nothing to reach and velocity is a level control and
little else. Contact time does shorten with velocity as well (1/(0.6 + 0.4·v),
so a hard strike rests on the object for about two thirds as long), but that is
a factor of 1.5 against HARD's factor of nine, and HARD is what you reach for
if you want the mallet to change.

## What it does not have

**No clock and no patterns.** Kickback has those, and a voice you want to play
from a keyboard should not come with a sequencer attached to it.

It is monophonic. The context menu has one option, **Trigger height sets
velocity**: with nothing patched to VEL, TRIG's own voltage is read as the
velocity instead of a fixed full force. An accented trigger out of a sequencer
is then one cable rather than two — the Schmitt that detects the edge throws
that height away otherwise. Off by default, and a cable in VEL always wins.

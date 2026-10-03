#!/usr/bin/env python3
"""The Obfuscation panel, declared once.

A three-band allpass matrix: the input is split into low, mid and high, each
band runs through up to ninety-six first-order allpass stages inside its own
feedback loop, and the bands are summed, saturated, tilted, clipped and boosted.
RANDOM redraws every stage's cutoff on a clock or on the input's peaks; the
RANDOM gate also freezes the matrix, holding whatever is ringing in it.

The read-out is the matrix made visible: every stage's cutoff as a tick on a
log-frequency axis, one lane per band, with the crossovers marked -- so SPREAD
and RANDOM can be seen doing what they do. FREQ, PINCH, STAGES, RANDOM and
CLK/ENV are fields on it (drag the values, click the switches); FREQ keeps its
knob, because it is the one you play.
"""

import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
from panelkit import *   # noqa: E402

GLASS_H = 32.0

P = Panel(
    slug="Obfuscation",
    title="OBFUSCATION",
    what="DISPERSION FILTER",
    form="FORM 1099-B",
    density="compact",
    glass=Glass(h=GLASS_H, grid=(6, 6), fields=[
        Field("freq_field", cell=(0, 0), span=(1, 2), kind="value"),
        Field("pinch",      cell=(0, 2), span=(1, 2), kind="value"),
        Field("stages",     cell=(0, 4), span=(1, 2), kind="value"),
        Field("random",     cell=(5, 0), span=(1, 2), kind="toggle"),
        Field("mode",       cell=(5, 2), span=(1, 2), kind="toggle"),
    ]),
)

P.sections = [
    Section("THE MATRIX", rows=[
        Row([BigKnob("freq", "FREQ", primary=True),
             Knob("spread", "SPREAD", light="fire_led")]),
        Row(items=[Trim("freq_cv", "FREQ"), Trim("pinch_cv", "PINCH"),
                   Trim("stages_cv", "STAGES")], pair=True),
        Row([Jack("freq_in"), Jack("pinch_in"), Jack("stages_in")]),
    ]),

    Section("FINISHING", rows=[
        Row([Knob("drive", "DRIVE"), Knob("bright", "BRIGHT"),
             Knob("clip", "CLIP"), Knob("boost", "BOOST")]),
    ]),
]

# The band carries everything patched: the audio in and out, and the two jacks
# that drive the randomizer -- a clock to roll on, a gate to freeze.
P.footer = [
    Row([Jack("in", "IN"), Jack("clock", "CLOCK"), Jack("gate", "FREEZE"),
         Jack("out", "OUT", ink="MINT")], y=118.6),
]

if __name__ == "__main__":
    raise SystemExit(build(P, root=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))))

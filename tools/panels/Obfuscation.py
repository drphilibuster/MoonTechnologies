#!/usr/bin/env python3
"""The Obfuscation panel, declared once.

A three-band allpass matrix: the input is split into low, mid and high, each
band runs through up to ninety-six first-order allpass stages inside its own
feedback loop, and the bands are summed, saturated, tilted, clipped and boosted.
RANDOM redraws every stage's cutoff on a clock or on the input's peaks; the
RANDOM gate also freezes the matrix, holding whatever is ringing in it.
"""

import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
from panelkit import *   # noqa: E402

P = Panel(
    slug="Obfuscation",
    title="OBFUSCATION",
    form="FORM 1099-B",
    density="compact",
)

P.sections = [
    Section("THE MATRIX", rows=[
        Row([Jack("in", "IN", ink="PAPER"),
             BigKnob("freq", "FREQ", primary=True),
             Knob("pinch", "PINCH"),
             Knob("stages", "STAGES")]),
        Row(items=[Trim("freq_cv", "FREQ"), Trim("pinch_cv", "PINCH"),
                   Trim("stages_cv", "STAGES")], pair=True),
        Row([Jack("freq_in"), Jack("pinch_in"), Jack("stages_in")]),
    ]),

    Section("RANDOMIZATION", rows=[
        Row([Switch("random", "RANDOM"), Switch("mode", "CLK/ENV"),
             Knob("spread", "SPREAD", light="fire_led"),
             Jack("clock", "CLOCK"), Jack("gate", "FREEZE")]),
    ]),

    Section("FINISHING", rows=[
        Row([Knob("drive", "DRIVE"), Knob("bright", "BRIGHT"),
             Knob("clip", "CLIP"), Knob("boost", "BOOST")]),
    ]),
]

P.footer = [
    Row([Jack("out", "OUT", ink="MINT")], y=118.6),
]

if __name__ == "__main__":
    raise SystemExit(build(P, root=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))))

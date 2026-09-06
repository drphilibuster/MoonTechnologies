#!/usr/bin/env python3
"""The Garnishment panel, declared once.

Running this file regenerates the artwork, the shared hardware, src/PanelTheme.hpp
and the previews. See ../../panelkit/README.md for the pipeline.

Garnishment is a dual VCA: two identical channels, each a MODE switch away from
being a different Modular-in-a-Week circuit -- LM13700 OTA, vactrol LED/LDR low-pass
gate, or a 2N5457 JFET amplitude modulator. One set of controls (BIAS, LAG, CV IN,
CV AMOUNT) drives whichever circuit MODE selects, so the panel repeats once per
channel rather than growing a row per topology. CV AMOUNT sits directly over its own
CV IN jack -- the paired idiom -- so the pair reads as one thing wearing two knobs.
"""

import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
from panelkit import *   # noqa: E402

P = Panel(
    slug="Garnishment",
    title="GARNISHMENT",
    form="FORM 668-W",
    hp=10,
)

C2 = P.cols(2, 17.0)     # 17.00, 33.80
C3 = P.cols(3, 12.0)     # 12.00, 25.40, 38.80 -- C3[1] is the midpoint of C2
C4 = P.cols(4, 8.0)      # 8.00, 19.60, 31.20, 42.80


def channel(n, suffix):
    """One channel: BIAS and LAG flank the MODE switch, which shares its axis
    with the CV amount/CV in pair below it -- same geometry as Retroactive's
    TIME/OVERDRAFT/DIV row, reused here for a switch instead of a light."""
    return Section("CHANNEL %d" % n, rows=[
        Row([Knob("bias%s" % suffix, C2[0], "BIAS"),
             Switch3("mode%s" % suffix, C3[1], "MODE"),
             Knob("lag%s" % suffix, C2[1], "LAG")]),
        Row([Trim("cvamt%s" % suffix, C3[1], "CV AMT")], label_side="above"),
        Row([Jack("cvin%s" % suffix, C3[1])], silent=True),
    ])


P.sections = [
    channel(1, "1"),
    channel(2, "2"),
]

# The audio the two VCAs actually pass is the module's main I/O, so it sits on
# the footer band like Retroactive's IN L/IN R/OUT L/OUT R, grouped by type.
P.footer = [
    Row([Jack("in1", C4[0], "IN 1"), Jack("in2", C4[1], "IN 2"),
         Jack("out1", C4[2], "OUT 1", ink="MINT"),
         Jack("out2", C4[3], "OUT 2", ink="MINT")], y=118.6),
]

if __name__ == "__main__":
    raise SystemExit(build(P, root=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))))

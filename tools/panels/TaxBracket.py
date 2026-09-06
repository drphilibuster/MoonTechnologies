#!/usr/bin/env python3
"""The Tax Bracket panel, declared once.

Running this file regenerates the artwork, the shared hardware, src/PanelTheme.hpp
and the previews. See ../../panelkit/README.md for the pipeline.

Tax Bracket is the Olegtron R2R: a passive 8-bit resistor ladder whose every
jack is at once an input and an output. Rack ports only go one way, so each
hardware jack becomes a pair here -- a sage input and a mint output that share
one number, side by side, the number being the bit's weight (1 .. 128) and, in
this office, the bracket it files under. The I/O jack -- the top of the ladder,
the one the DAC reads from -- is the pair on the footer band.

The read-out under the masthead shows what the ladder currently adds up to at
I/O, and the 8-bit word it is being asked to convert.

Twelve HP because the masthead's form number needs it: "TAX RATE SCHEDULE X" and
the brand together do not fit under anything narrower, and the form number is
the panel's identity. The width buys two ladders side by side rather than one
long one, which is what makes the rows shallow enough to carry labels above
every jack at regular density.
"""

import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
from panelkit import *   # noqa: E402

P = Panel(
    slug="TaxBracket",
    title="TAX BRACKET",
    form="TAX RATE SCHEDULE X",
    hp=12,
    glass=Glass(h=6.4),
)

C4 = P.cols(4, 8.0)       # 8.00, 22.99, 37.97, 52.96
C2 = P.cols(2, 17.0)      # 17.00, 43.96


def pair(bit, col):
    """One hardware jack as a Rack input and output that share a number."""
    return [Jack("in%d" % bit, C4[col], str(bit)),
            Jack("out%d" % bit, C4[col + 1], str(bit), ink="MINT")]


P.sections = [
    # The ladder. Low-order bits down the left, high-order down the right; each
    # row is two hardware jacks, each hardware jack is an [in, out] pair.
    Section("BRACKETS", rows=[
        Row(pair(1, 0) + pair(16, 2)),
        Row(pair(2, 0) + pair(32, 2)),
        Row(pair(4, 0) + pair(64, 2)),
        Row(pair(8, 0) + pair(128, 2)),
    ]),

    # What the ladder does with jacks nobody has plugged, and how loud it files.
    Section("ADJUSTMENTS", rows=[
        Row([Switch("ground", C2[0], "GROUND"),
             Trim("scale", C2[1], "SCALE")]),
    ]),
]

# The I/O jack is the top of the ladder and the DAC's output, so it goes where
# the family keeps what leaves the module. Same rule as the bits: one number,
# one pair, the ink says which way it faces.
P.footer = [
    Row([Jack("io_in", C2[0], "I/O"),
         Jack("io_out", C2[1], "I/O", ink="MINT")], y=118.6),
]

if __name__ == "__main__":
    raise SystemExit(build(P, root=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))))

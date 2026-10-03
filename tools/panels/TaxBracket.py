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
I/O, and the 8-bit word it is being asked to convert -- and both are controls.
The voltage is SCALE: hold it and drag. The word is GROUND: click it to choose
whether unplugged jacks float or are tied to ground, which is what decides
whether their digits read '-' or '0'. The switch and trimpot that used to stand
under the ladder are gone, and the glass has their height.

The width is two ladders side by side rather than one long one, which is what
makes the rows shallow enough to carry labels above every jack at regular
density.
"""

import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
from panelkit import *   # noqa: E402

P = Panel(
    slug="TaxBracket",
    title="TAX BRACKET",
    what="R2R DAC & MIXER",
    form="TAX RATE SCHEDULE X",
    # Two halves, each a field over two lines: the sum at I/O (SCALE) on the
    # left, the 8-bit word (GROUND) on the right, each with what it sets under it.
    glass=Glass(h=22.0, grid=(2, 2), fields=[
        Field("scale", cell=(0, 0), span=(2, 1), kind="value"),
        Field("ground", cell=(0, 1), span=(2, 1), kind="toggle"),
    ]),
)


def pair(bit, col):
    """One hardware jack as a Rack input and output that share a number."""
    return [Jack("in%d" % bit, str(bit), col=col),
            Jack("out%d" % bit, str(bit), ink="MINT", col=col + 1)]


P.sections = [
    # The ladder. Low-order bits down the left, high-order down the right; each
    # row is two hardware jacks, each hardware jack is an [in, out] pair. The
    # two halves are named as runs so they are spaced as pairs, with the gutter
    # between them saying which output belongs to which input.
    Section("BRACKETS", groups=(2, 2), rows=[
        Row(pair(1, 0) + pair(16, 2)),
        Row(pair(2, 0) + pair(32, 2)),
        Row(pair(4, 0) + pair(64, 2)),
        Row(pair(8, 0) + pair(128, 2)),
    ]),
]

# The I/O jack is the top of the ladder and the DAC's output, so it goes where
# the family keeps what leaves the module. Same rule as the bits: one number,
# one pair, the ink says which way it faces.
P.footer = [
    Row([Jack("io_in", "I/O"),
         Jack("io_out", "I/O", ink="MINT")], y=118.6),
]

if __name__ == "__main__":
    raise SystemExit(build(P, root=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))))

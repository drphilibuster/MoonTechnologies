#!/usr/bin/env python3
"""The Consolidation panel, declared once.

Running this file regenerates the artwork, the shared hardware, src/PanelTheme.hpp
and the previews. See ../../panelkit/README.md for the pipeline.

Consolidation is a Modular in a Week mixer and multiples on one panel: a
four-channel inverting-summer mixer (ASMR's own gain structure -- unity per
channel, a second inverting stage to bring the sum back to normal polarity, so
both polarities come out) over a pair of 1-in-3-out multiples, B normalled to A
so one cable gives 1:3, two give 1:6.
"""

import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
from panelkit import *   # noqa: E402

P = Panel(
    slug="Consolidation",
    title="CONSOLIDATION",
    form="FORM 1040",
)

P.sections = [
    Section("MIXER", rows=[
        Row([Knob("lvl1", "LVL 1"), Knob("lvl2", "LVL 2"),
             Knob("lvl3", "LVL 3"), Knob("lvl4", "LVL 4")]),
        Row([Jack("in1", "IN 1"), Jack("in2", "IN 2"),
             Jack("in3", "IN 3"), Jack("in4", "IN 4")]),
    ]),

    # Two 1-in-3-out multiples. The three legs of each are electrically
    # identical, so only the input carries a label -- the legs read as one
    # group rather than three separate things to tell apart. The input takes
    # the middle column of the three its legs stand in, which is what centres
    # it over them without a millimetre being typed.
    Section("MULTIPLES", rows=[
        Row([Jack("mult_a_in", "MULT A", col=1)]),
        Row([Jack("mult_a_out1", ink="MINT"),
             Jack("mult_a_out2", ink="MINT"),
             Jack("mult_a_out3", ink="MINT")], silent=True),
        Row([Jack("mult_b_in", "MULT B", col=1)]),
        Row([Jack("mult_b_out1", ink="MINT"),
             Jack("mult_b_out2", ink="MINT"),
             Jack("mult_b_out3", ink="MINT")], silent=True),
    ], divide_after=(1,)),
]

# The mix leaving the module is its main audio I/O, so -- like every other
# panel in the family -- it sits on the footer band, mint for the outputs.
P.footer = [
    Row([Jack("out", "OUT", ink="MINT"),
         Jack("inv_out", "INV OUT", ink="MINT")], y=118.6),
]

if __name__ == "__main__":
    raise SystemExit(build(P, root=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))))

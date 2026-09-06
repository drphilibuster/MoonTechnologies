#!/usr/bin/env python3
"""The Six Figures panel, declared once.

Running this file regenerates the artwork, the shared hardware, src/PanelTheme.hpp
and the previews. See ../../panelkit/README.md for the pipeline.

Six Figures is the W-2: six sources of income, one line each. It consolidates
the Modular in a Week oscillator day -- the 40106 hex Schmitt bank, the All
About Circuits / 4069 integrator VCO, the 4046 PLL VCO and the Kassutronics
reverse-avalanche oscillator -- into one bank of six voices. Each column is one
voice, top to bottom: which CORE it runs, its RATE, its CV amount over its CV
jack, and an AUX output that is whatever that core has left over (the cap
voltage, the triangle, the phase comparator, the avalanche pulse). The seventh
column is the totals column: RANGE, the PLL's CAPTURE and LOCK, SYNC, DRIFT,
SIGNAL and, on the footer, MIX.
"""

import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
from panelkit import *   # noqa: E402

P = Panel(
    slug="SixFigures",
    title="SIX FIGURES",
    form="FORM W-2",
)

# Seven columns: six voices and a totals column on the right. Every row names
# the columns it stands in, so the footer puts each voice's OUT under its own
# voice and MIX under the totals without a millimetre being typed.
V = list(range(6))
T = 6

P.sections = [
    Section("WAGES, TIPS, OTHER COMPENSATION", rows=[
        Row([Knob("core%d" % (i + 1), "CORE %d" % (i + 1), steps=4, col=i)
             for i in V]
            + [Switch("range", "RANGE", col=T)]),
        Row([BigKnob("rate%d" % (i + 1), "RATE", light="led%d" % (i + 1), col=i)
             for i in V]
            + [Knob("capture", "CAPT", light="lock", light_side="left",
                    primary=True, col=T)]),
        # Each voice's CV trim owns the jack directly below it; the label they
        # share sits between the two. SYNC has nothing under it, so it keeps
        # its own name above its head.
        Row([Trim("cv%d" % (i + 1), "CV", pair=True, col=i) for i in V]
            + [Jack("sync", "SYNC", col=T)]),
        Row([Jack("cv%d_in" % (i + 1), col=i) for i in V]
            + [Trim("drift", "DRIFT", col=T)]),
        Row([Jack("aux%d" % (i + 1), "AUX", ink="MINT", col=i) for i in V]
            + [Jack("signal", "SIGNAL", col=T)]),
    ], divide_after=(1,)),
]

# The audio row sits as low as the bottom screws allow: RACK_GRID_HEIGHT -
# RACK_GRID_WIDTH puts their top edge at 123.61 mm, so a jack collar centred
# below 118.6 would run under one.
P.footer = [
    Row([Jack("out%d" % (i + 1), "OUT %d" % (i + 1), ink="MINT", col=i) for i in V]
        + [Jack("mix", "MIX", ink="MINT", col=T)], y=118.6),
]

if __name__ == "__main__":
    raise SystemExit(build(P, root=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))))

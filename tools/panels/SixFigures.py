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
    hp=24,
)

# Seven columns: six voices and a totals column on the right. The footer puts
# each voice's OUT under its own column and MIX under the totals.
C7 = P.cols(7, 10.5)      # the end big knobs' wells clear the frame
V = C7[:6]
T = C7[6]

# Row 4 leads with the DRIFT trim rather than a jack so the solver places the
# row as a below-labelled one -- which keeps the CV jacks tight under their
# trims, the paired idiom, while DRIFT still gets its own label.
P.sections = [
    Section("WAGES, TIPS, OTHER COMPENSATION", rows=[
        Row([Knob("core%d" % (i + 1), V[i], "CORE %d" % (i + 1)) for i in range(6)]
            + [Switch("range", T, "RANGE")]),
        Row([BigKnob("rate%d" % (i + 1), V[i], "RATE", light="led%d" % (i + 1))
             for i in range(6)]
            + [Knob("capture", T, "CAPT", light="lock", light_side="left")]),
        Row([Trim("cv%d" % (i + 1), V[i], "CV") for i in range(6)]
            + [Jack("sync", T, "SYNC")], label_side="above"),
        Row([Trim("drift", T, "DRIFT")]
            + [Jack("cv%d_in" % (i + 1), V[i]) for i in range(6)]),
        Row([Jack("aux%d" % (i + 1), V[i], "AUX", ink="MINT") for i in range(6)]
            + [Jack("signal", T, "SIGNAL")]),
    ], divide_after=(1,)),
]

# The audio row sits as low as the bottom screws allow: RACK_GRID_HEIGHT -
# RACK_GRID_WIDTH puts their top edge at 123.61 mm, so a jack collar centred
# below 118.6 would run under one.
P.footer = [
    Row([Jack("out%d" % (i + 1), V[i], "OUT %d" % (i + 1), ink="MINT") for i in range(6)]
        + [Jack("mix", T, "MIX", ink="MINT", primary=True)], y=118.6),
]

if __name__ == "__main__":
    raise SystemExit(build(P, root=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))))

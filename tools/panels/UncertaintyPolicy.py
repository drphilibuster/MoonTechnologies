#!/usr/bin/env python3
"""The Uncertainty Policy panel, declared once.

Running this file regenerates the artwork, the shared hardware, src/PanelTheme.hpp
and the previews. Every vertical coordinate below is derived by panelkit's layout
solver -- the only numbers here are column positions and the one row the bottom
screws pin. See ../../panelkit/README.md.

The module rolls the dice on your patch and files the result. EXPOSURE sizes a
filing in all three of its dimensions: VARIANCE is how far a knob may move,
SPREAD how many of them move at all, TRANSFERS how many cables may be re-routed.
CONTROLS covers knobs, switches and latching buttons alike -- on a patch driven
by a grid of buttons those buttons are the instrument, not an afterthought.
MANDATE says what a filing is allowed to do: BASIS is what the review listens
for, SAFE HARBOR how much of the patch's timing is off limits. AMEND scopes and
commits. LAST FILING is the escape hatch and the opinion issued on the roll you
just made -- and the read-out carries three lines: the verdict, the evidence it
rested on, and the mandate the next filing will be made under.
"""

import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
from panelkit import *   # noqa: E402

P = Panel(
    slug="UncertaintyPolicy",
    title="UNCERTAINTY POLICY",
    form="SCHEDULE UTP",
    density="compact",
    glass=Glass(h=15.0),
)

P.sections = [
    # How much exposure a single filing may create.
    Section("EXPOSURE", rows=[
        Row([BigKnob("knob_amount", "VARIANCE"),
             BigKnob("spread", "SPREAD"),
             BigKnob("cable_count", "TRANSFERS")]),
    ]),
    # What a filing is allowed to do, as opposed to how much of it there is.
    # BASIS is what the review listens for once the roll has been made; SAFE
    # HARBOR is how much of the patch's timing is off limits while it is being
    # made. Turning SAFE HARBOR down is the sanctioned way to reach a drone.
    Section("MANDATE", rows=[
        Row([Switch3("basis", "BASIS"),
             Trim("spine", "SAFE HARBOR")]),
    ]),
    # The filing itself. One row rather than two: the ringed bezel sits between
    # the two narrower scopes and reads as the commit without needing a subtotal
    # rule under them to say so.
    Section("AMEND", rows=[
        Row([Button("roll_controls", "CONTROLS"),
             Bezel("roll_all", "BOTH", primary=True),
             Button("roll_cables", "CABLES")]),
    ]),
    # The escape hatch, and the opinion issued on the last filing.
    Section("LAST FILING", rows=[
        Row([Button("revert", "RESCIND"),
             Light("verdict", "OPINION", ink="SAGE", size=6.2)]),
    ]),
]

# The trigger jack sits as low as the bottom screws allow: RACK_GRID_HEIGHT -
# RACK_GRID_WIDTH puts their top edge at 123.61 mm, so the jack cannot go below
# 118.6 without its collar running under a screw.
P.footer = [
    Row([Jack("trig", "TRIG", ink="MINT")], y=118.6),
]

if __name__ == "__main__":
    raise SystemExit(build(P, root=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))))

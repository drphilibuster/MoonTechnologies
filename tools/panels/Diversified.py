#!/usr/bin/env python3
"""The Diversified panel, declared once.

Running this file regenerates the artwork, the shared hardware, src/PanelTheme.hpp
and the previews. See ../../panelkit/README.md for the pipeline.

Diversified is a portfolio of effects: the ninety-nine programs of the DSP99
board that Modular in a Week put behind a knob, followed by the seven dedicated
MiaW circuits -- the Echomatic PT2399 echo, the Little Angel chorus, the spring
tank, the MXR Distortion+, Talk Funny, the MW Bitcrusher and the 4011 ring
modulator. PORTFOLIO is the program and the three macro knobs whose meaning
changes with it; the read-out under the masthead names the holding and what
each macro is doing for it. ALLOCATION is what CV may take off each of those,
and off MIX. HOLDINGS is the mix and the two gates: AUX, whatever the program
wants a gate for (the spring's twang, the door on a gated reverb, Talk Funny's
FM source), and TAP, which sets every delay's time.

Compact density: five rows of hardware, a read-out and a six-jack audio row at
18 HP.
"""

import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
from panelkit import *   # noqa: E402

P = Panel(
    slug="Diversified",
    title="DIVERSIFIED",
    form="FORM 1099-B",
    density="compact",
    glass=Glass(h=9.2),
)

P.sections = [
    # The program is the one control you reach for, so it wears the ring. The
    # three macros beside it mean something different for every program; the
    # read-out above says what. PROGRAM has 106 positions, far too many to
    # engrave, which is exactly why the read-out names the one you are on.
    Section("PORTFOLIO", caption_light="active", rows=[
        Row([BigKnob("program", "PROGRAM", primary=True),
             Knob("p1", "P1"),
             Knob("p2", "P2"),
             Knob("p3", "P3")]),
    ]),

    # What CV may take off each control on the way in. Each trimpot sits
    # directly over its own jack, with the label they share set in the gap
    # between the two of them.
    Section("ALLOCATION", rows=[
        Row([Trim("program_cv", "PROG"),
             Trim("p1_cv", "P1"),
             Trim("p2_cv", "P2"),
             Trim("p3_cv", "P3"),
             Trim("mix_cv", "MIX")], pair=True),
        Row([Jack("program_in"), Jack("p1_in"),
             Jack("p2_in"), Jack("p3_in"),
             Jack("mix_in")], silent=True),
    ]),

    # The blend, and the two gates. AUX is whatever the running program wants a
    # gate or a CV for; TAP sets the time of every delay, and its light shows
    # the clock it has locked to.
    Section("HOLDINGS", rows=[
        Row([Knob("mix", "MIX"),
             Jack("aux_in", "AUX"),
             Jack("tap_in", "TAP", light="tap_led")]),
    ]),
]

# The audio row sits as low as the bottom screws allow: RACK_GRID_HEIGHT -
# RACK_GRID_WIDTH puts their top edge at 123.61 mm, so a jack collar centred
# below 118.6 would run under one. Stereo in, the Echomatic's TO FX / FROM FX
# loop, stereo out.
P.footer = [
    Row([Jack("in_l", "IN L"), Jack("in_r", "IN R"),
         Jack("send", "SEND", ink="MINT"),
         Jack("ret", "RETURN"),
         Jack("out_l", "OUT L", ink="MINT"),
         Jack("out_r", "OUT R", ink="MINT")], y=118.6),
]

if __name__ == "__main__":
    raise SystemExit(build(P, root=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))))

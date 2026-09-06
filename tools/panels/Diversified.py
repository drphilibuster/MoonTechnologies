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
    hp=18,
    density="compact",
    glass=Glass(h=9.2),
)

C4 = P.cols(4, 12.0)      # 12.00, 34.48, 56.96, 79.44
C5 = P.cols(5, 9.0)       # 9.00, 27.36, 45.72, 64.08, 82.44
CH = [16.0, 58.0, 79.44]  # MIX, then the two gates clear of the caption
C6 = P.cols(6, 8.0)       # 8.00, 23.09, 38.18, 53.26, 68.35, 83.44

P.sections = [
    # The program is the one control you reach for, so it wears the ring. The
    # three macros beside it mean something different for every program; the
    # read-out above says what.
    Section("PORTFOLIO", caption_light="active", rows=[
        Row([BigKnob("program", C4[0], "PROGRAM", primary=True),
             Knob("p1", C4[1], "P1"),
             Knob("p2", C4[2], "P2"),
             Knob("p3", C4[3], "P3")]),
    ]),

    # What CV may take off each control on the way in. Each trimpot sits
    # directly over its own jack and they share one label -- the paired idiom.
    Section("ALLOCATION", rows=[
        Row([Trim("program_cv", C5[0], "PROG"),
             Trim("p1_cv", C5[1], "P1"),
             Trim("p2_cv", C5[2], "P2"),
             Trim("p3_cv", C5[3], "P3"),
             Trim("mix_cv", C5[4], "MIX")], label_side="above"),
        Row([Jack("program_in", C5[0]), Jack("p1_in", C5[1]),
             Jack("p2_in", C5[2]), Jack("p3_in", C5[3]),
             Jack("mix_in", C5[4])], silent=True),
    ]),

    # The blend, and the two gates. AUX is whatever the running program wants a
    # gate or a CV for; TAP sets the time of every delay, and its light shows
    # the clock it has locked to.
    Section("HOLDINGS", rows=[
        Row([Knob("mix", CH[0], "MIX"),
             Jack("aux_in", CH[1], "AUX"),
             Jack("tap_in", CH[2], "TAP", light="tap_led")]),
    ]),
]

# The audio row sits as low as the bottom screws allow: RACK_GRID_HEIGHT -
# RACK_GRID_WIDTH puts their top edge at 123.61 mm, so a jack collar centred
# below 118.6 would run under one. Stereo in, the Echomatic's TO FX / FROM FX
# loop, stereo out.
P.footer = [
    Row([Jack("in_l", C6[0], "IN L"), Jack("in_r", C6[1], "IN R"),
         Jack("send", C6[2], "SEND", ink="MINT"),
         Jack("ret", C6[3], "RETURN"),
         Jack("out_l", C6[4], "OUT L", ink="MINT"),
         Jack("out_r", C6[5], "OUT R", ink="MINT")], y=118.6),
]

if __name__ == "__main__":
    raise SystemExit(build(P, root=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))))

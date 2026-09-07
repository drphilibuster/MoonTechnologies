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
    # PROGRAM wears the ring. The four macros mean something different for every
    # one of the hundred and six programs, so none of them is engraved: each
    # carries a little lit plate and the module writes the running program's own
    # name for that knob into it -- or "--" where that program has nothing for
    # the knob to do, which is the honest thing for a panel to say.
    Section("PORTFOLIO", caption_light="active", rows=[
        Row([BigKnob("program", "PROGRAM", primary=True),
             Knob("p1", ""), Knob("p2", ""), Knob("p3", ""), Knob("p4", "")]),
        Row([Readout("p1_name", col=1), Readout("p2_name", col=2),
             Readout("p3_name", col=3), Readout("p4_name", col=4)], silent=True),
    ]),

    # What CV may take off each control on the way in: a trimpot directly over
    # its own jack, in the same column as the knob it attenuates.
    Section("ALLOCATION", rows=[
        Row([Trim("program_cv", "PROG"), Trim("p1_cv", "1"), Trim("p2_cv", "2"),
             Trim("p3_cv", "3"), Trim("p4_cv", "4")], pair=True),
        Row([Jack("program_in"), Jack("p1_in"), Jack("p2_in"),
             Jack("p3_in"), Jack("p4_in")], silent=True),
    ]),

    # AUX is whatever the running program wants a gate or a CV for. CLOCK sets
    # the time of every delay and its light shows what it has locked to; DIV is
    # what that clock is worth -- a delay exactly on the beat is rarely the one
    # you want, and without this the clock's own tempo was the only one offered.
    Section("HOLDINGS", rows=[
        # MIX's CV sits beside it rather than stacked over its jack the way the
        # ALLOCATION block does: a second row here overruns the face by a
        # millimetre, and moving it up there instead costs two HP. Both are
        # named outright, which is what the paired idiom was buying.
        Row([Knob("mix", "MIX"),
             Trim("mix_cv", "CV AMT"), Jack("mix_in", "CV IN"),
             Jack("aux_in", "AUX"),
             Jack("tap_in", "CLOCK", light="tap_led"),
             Knob("clock_div", "DIV", steps=9)]),
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

#!/usr/bin/env python3
"""The Diversified panel, declared once.

Running this file regenerates the artwork, the shared hardware, src/PanelTheme.hpp
and the previews. See ../../panelkit/README.md for the pipeline.

Diversified is a portfolio of effects: the ninety-nine programs of the DSP99
board that Modular in a Week put behind a knob, followed by the seven dedicated
MiaW circuits -- the Echomatic PT2399 echo, the Little Angel chorus, the spring
tank, the MXR Distortion+, Talk Funny, the MW Bitcrusher and the 4011 ring
modulator.

The read-out is the portfolio, and everything on it is a control: click the
program to pick one of the hundred and six by name (or drag through them, round
the ring), click DIV for what the clock is worth, and hold and drag any of the
eight macros -- each shows the running program's own name for it over its
value, or "--" where that program has nothing for it to do. The eight macro
knobs that used to stand under the glass were the same eight numbers twice;
they went, and the two rows they stood in are what let ALLOCATION fold into two
rows of five. ALLOCATION is what CV may take off the program, each macro and
MIX. HOLDINGS is the mix and the two gates: AUX, whatever the program wants a
gate for (the spring's twang, the door on a gated reverb, Talk Funny's FM
source), and CLOCK, which sets every delay's time.

Compact density. The six-jack audio row is what sets the width now.
"""

import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
from panelkit import *   # noqa: E402

P = Panel(
    slug="Diversified",
    title="DIVERSIFIED",
    what="MULTI-EFFECTS",
    form="FORM 1099-B",
    density="compact",
    # The program and the clock's worth on the first line, then the eight
    # macros as a 2 x 4 matrix -- each a name over its value, laid out the way
    # the eight knobs used to stand -- with the clock state on the last line.
    glass=Glass(h=24.5, grid=(6, 4), fields=[
        Field("program",   cell=(0, 0), span=(1, 3), kind="select"),
        Field("clock_div", cell=(0, 3), kind="select"),
    ] + [Field("p%d" % n, cell=(1 + 2 * ((n - 1) // 4), (n - 1) % 4), span=(2, 1),
               kind="value") for n in range(1, 9)]),
)

P.sections = [
    # What CV may take off each control on the way in: a trimpot directly over
    # its own jack. Two rows of five, PROGRAM and the macros in order, then MIX.
    Section("ALLOCATION", caption_light="active", rows=[
        Row([Trim("program_cv", "PROG")]
            + [Trim("p%d_cv" % n, str(n)) for n in range(1, 5)], pair=True),
        Row([Jack("program_in")]
            + [Jack("p%d_in" % n) for n in range(1, 5)], silent=True),
        Row([Trim("p%d_cv" % n, str(n)) for n in range(5, 9)]
            + [Trim("mix_cv", "MIX")], pair=True),
        Row([Jack("p%d_in" % n) for n in range(5, 9)]
            + [Jack("mix_in")], silent=True),
    ]),

    # AUX is whatever the running program wants a gate or a CV for. CLOCK sets
    # the time of every delay and its light shows what it has locked to; what
    # that clock is worth (DIV) is on the glass.
    Section("HOLDINGS", rows=[
        Row([Knob("mix", "MIX"),
             Jack("aux_in", "AUX"),
             Jack("tap_in", "CLOCK", light="tap_led")]),
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

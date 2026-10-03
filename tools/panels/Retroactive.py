#!/usr/bin/env python3
"""The Retroactive panel, declared once.

Running this file regenerates the artwork, the shared hardware, src/PanelTheme.hpp
and the previews. See ../../panelkit/README.md for the pipeline.

Retroactive is the amended return: it goes back over audio you have already
played and re-files it. ASSESSMENT is the window and what comes back;
WITHHOLDING is what the CV inputs are allowed to take off each control. The
read-out is where the window is cut up: the window length (TIME, which keeps
its knob as well), MODE, SUBDIV, the CLK DIV ratio, CHAR and FREEZE are all
fields on it -- drag a value, click a list, click a switch.

The OVERDRAFT light is the panel drawing its own condition -- overdraft is
FADE > B/2 where B = TIME / SUBDIV, so a lime trace runs from the TIME knob
through the light to FADE, and SUBDIV's value on the read-out turns lime while
the condition holds.

This panel runs at "compact" density. Same rules, same idioms, tighter numbers
-- nothing about the design language changes, only the metric scale in
panelkit/layout.py.
"""

import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
from panelkit import *   # noqa: E402

P = Panel(
    slug="Retroactive",
    title="RETROACTIVE",
    what="SAMPLE PERMUTER",
    form="FORM 1040-X",
    density="compact",
    # Four lines, and every setting on them is a field. The top line is the
    # window length (hold and drag: TIME, which keeps its knob too) and the MODE
    # it runs. The second is the latency -- only read -- SUBDIV and the CLK DIV
    # ratio. The third is CHAR and FREEZE, which latches: a click freezes, a
    # click lets go, and the word says which. The fourth is FADE and MIX, which
    # keep their knobs; FADE is printed in milliseconds because that is the
    # number the overdraft condition compares against the sub-block. The glass
    # is as tall as it is because the knobs that moved onto it left the room.
    glass=Glass(h=26.0, grid=(4, 6), fields=[
        Field("time_field", cell=(0, 0), span=(1, 3), kind="value"),
        Field("mode",       cell=(0, 3), span=(1, 3), kind="select"),
        Field("subdiv",     cell=(1, 2), span=(1, 2), kind="select"),
        Field("div",        cell=(1, 4), span=(1, 2), kind="select"),
        Field("char",       cell=(2, 0), span=(1, 3), kind="toggle"),
        Field("freeze",     cell=(2, 3), span=(1, 3), kind="toggle"),
        Field("fade_field", cell=(3, 0), span=(1, 3), kind="value"),
        Field("mix_field",  cell=(3, 3), span=(1, 3), kind="value"),
    ]),
)

P.sections = [
    # The window and what comes back. TIME is the one you play and keeps its
    # knob; FADE and MIX keep theirs. MODE, SUBDIV, CLK DIV, CHAR and FREEZE
    # are on the read-out above. The WINDOW light beside the caption ticks once
    # per window.
    Section("ASSESSMENT", caption_light="window", rows=[
        Row([BigKnob("time", "TIME"),
             # The light's own label sits above it so the trace can run
             # straight through the light without crossing its own name.
             Light("overdraft", "OVERDRAFT", ink="LIME", size=5.4, side="above"),
             Knob("fade", "FADE"),
             Knob("mix", "MIX")]),
    ]),

    # What the CV inputs may take off each control. Each trimpot sits directly
    # over its own jack, with the label they share set in the gap between the
    # two. The clock and reset jacks stand in a row of their own below the
    # pairs, labels overhead where a cable cannot cover them. FREEZE is a gate
    # in, and gates in this family live on the band with the rest of the
    # patching. The CLOCK light beside the caption is the clock being heard.
    Section("WITHHOLDING", caption_light="clock_led", rows=[
        Row([Trim("time_cv", "TIME"),
             Trim("mode_cv", "MODE"),
             Trim("subdiv_cv", "SUB"),
             Trim("mix_cv", "MIX")], pair=True),
        Row([Jack("time_in"), Jack("mode_in"),
             Jack("subdiv_in"), Jack("mix_in")], silent=True),
        Row([Jack("clock_in", "CLOCK"),
             Jack("reset_in", "RESET")], own_grid=True),
    ]),
]


def overdraft_trace(pos, m):
    """The panel drawing its own condition.

    Overdraft is FADE > B/2 where B = TIME / SUBDIV, so a lime wire runs from the
    TIME knob through the light and on to FADE. SUBDIV, the third term, is on the
    read-out now, where its value turns lime while the condition holds -- the
    wire can only join controls that stand on the face. The wire breaks itself
    around any label it crosses, so this stays correct if a row moves.
    """
    tx, ty = pos["time"]
    lx, ly = pos["overdraft"]
    fx, fy = pos["fade"]
    return [
        Trace([(tx + RADIUS["knob_large"] + 0.5, ly), (lx - RADIUS["light"] - 0.6, ly)]),
        Trace([(lx + RADIUS["light"] + 0.6, ly), (fx - RADIUS["knob"] - 0.5, ly)]),
    ]


P.traces = [overdraft_trace]


# The audio row sits as low as the bottom screws allow: RACK_GRID_HEIGHT -
# RACK_GRID_WIDTH puts their top edge at 123.61 mm, so a jack collar centred
# below 118.6 would run under one.
P.footer = [
    Row([Jack("freeze_in", "FREEZE"),
         Jack("in_l", "IN L"), Jack("in_r", "IN R"),
         Jack("out_l", "OUT L", ink="MINT"),
         Jack("out_r", "OUT R", ink="MINT")], y=118.6),
]

if __name__ == "__main__":
    raise SystemExit(build(P, root=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))))

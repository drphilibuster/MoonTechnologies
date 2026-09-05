#!/usr/bin/env python3
"""The Retroactive panel, declared once.

Running this file regenerates the artwork, the shared hardware, src/PanelTheme.hpp
and the previews. See ../../panelkit/README.md for the pipeline.

Retroactive is the amended return: it goes back over audio you have already
played and re-files it. ASSESSMENT is the window and how it is cut up;
WITHHOLDING is what the CV inputs are allowed to take off each control. The
OVERDRAFT light is the panel drawing its own condition -- overdraft is
FADE > B/2 where B = TIME / SUBDIV, so a lime trace runs from the TIME knob
through the light and down to the two controls that complete the sum.

This panel runs at "compact" density: seven rows of controls at 12 HP leaves no
room for the regular scale. Same rules, same idioms, tighter numbers -- nothing
about the design language changes, only the metric scale in panelkit/layout.py.
"""

import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
from panelkit import *   # noqa: E402

P = Panel(
    slug="Retroactive",
    title="RETROACTIVE",
    form="FORM 1040-X",
    hp=12,
    density="compact",
    glass=Glass(h=9.2),
)

C4 = P.cols(4, 8.0)       # 8.00, 22.99, 37.97, 52.96
C3 = P.cols(3, 12.0)      # 12.00, 30.48, 48.96
C2 = P.cols(2, 17.0)      # 17.00, 43.96
# C3[1] == the midpoint of C2 == 30.48: SUBDIV, CHAR and the overdraft light all
# share one axis, which is what makes the trace geometry fall out cleanly.

P.sections = [
    # The window, how it is cut up, and what comes back. The WINDOW light beside
    # the caption ticks once per window.
    Section("ASSESSMENT", caption_light="window", rows=[
        Row([BigKnob("time", C2[0], "TIME"),
             # The light's own label sits above it so the trace can drop straight
             # down from the light without running through its own name.
             Light("overdraft", C3[1], "OVERDRAFT", ink="LIME", size=5.4,
                   side="above"),
             BigKnob("div", C2[1], "CLK DIV")]),
        Row([Knob("mode", C3[0], "MODE"),
             Knob("subdiv", C3[1], "SUBDIV"),
             Knob("fade", C3[2], "FADE")]),
        Row([Knob("mix", C3[0], "MIX"),
             Widget("char", C3[1], "switch", "CHAR"),
             Bezel("freeze", C3[2], "FREEZE", ink="PAPER")]),
    ], divide_after=(1,)),

    # What the CV inputs may take off each control. Each trimpot sits directly
    # over its own jack and they share one label -- the paired idiom.
    Section("WITHHOLDING", rows=[
        Row([Trim("time_cv", C4[0], "TIME"),
             Trim("mode_cv", C4[1], "MODE"),
             Trim("subdiv_cv", C4[2], "SUB"),
             Trim("mix_cv", C4[3], "MIX")], label_side="above"),
        Row([Jack("time_in", C4[0]), Jack("mode_in", C4[1]),
             Jack("subdiv_in", C4[2]), Jack("mix_in", C4[3])], silent=True),
        Row([Jack("clock_in", C3[0], "CLOCK", light="clock_led"),
             Jack("reset_in", C3[1], "RESET"),
             Jack("freeze_in", C3[2], "FREEZE")]),
    ]),
]

def overdraft_trace(pos, m):
    """The panel drawing its own condition.

    Overdraft is FADE > B/2 where B = TIME / SUBDIV, so a lime wire runs from the
    TIME knob into the light and on down to SUBDIV and FADE -- the three controls
    that decide it. The wire breaks itself around any label it crosses, so this
    stays correct if a row moves.
    """
    tx, ty = pos["time"]
    lx, ly = pos["overdraft"]
    sx, sy = pos["subdiv"]
    fx, fy = pos["fade"]
    # Below the row's labels, above the knobs the wire is heading for. The wire
    # breaks itself around anything it still crosses.
    bus = sy - RADIUS["knob"] - 0.9
    return [
        Trace([(tx + RADIUS["knob_large"] + 0.5, ty), (lx - RADIUS["light"] - 0.6, ty)]),
        Trace([(lx, ly + RADIUS["light"] + 0.6), (lx, bus)]),
        Trace([(lx, bus), (fx, bus)], dots=[(lx, bus), (fx, bus)]),
        Trace([(sx, bus), (sx, sy - RADIUS["knob"])]),
        Trace([(fx, bus), (fx, fy - RADIUS["knob"])]),
    ]


P.traces = [overdraft_trace]


# The audio row sits as low as the bottom screws allow: RACK_GRID_HEIGHT -
# RACK_GRID_WIDTH puts their top edge at 123.61 mm, so a jack collar centred
# below 118.6 would run under one.
P.footer = [
    Row([Jack("in_l", C4[0], "IN L"), Jack("in_r", C4[1], "IN R"),
         Jack("out_l", C4[2], "OUT L", ink="MINT"),
         Jack("out_r", C4[3], "OUT R", ink="MINT")], y=118.6),
]

if __name__ == "__main__":
    raise SystemExit(build(P, root=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))))

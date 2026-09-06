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

This panel runs at "compact" density: six rows of controls at 14 HP leaves no
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
    density="compact",
    glass=Glass(h=9.0),
)

P.sections = [
    # The window, how it is cut up, and what comes back. The WINDOW light beside
    # the caption ticks once per window. MODE, SUBDIV and CLK DIV all step, so
    # all three wear detent rings; CHAR is a two-position switch already.
    Section("ASSESSMENT", caption_light="window", rows=[
        Row([BigKnob("time", "TIME"),
             # The light's own label sits above it so the trace can drop straight
             # down from the light without running through its own name.
             Light("overdraft", "OVERDRAFT", ink="LIME", size=5.4, side="above"),
             BigKnob("div", "CLK DIV", steps=11)]),
        Row([Knob("mode", "MODE", steps=8),
             Knob("subdiv", "SUBDIV", steps=7),
             Knob("fade", "FADE")]),
        Row([Knob("mix", "MIX"),
             Switch("char", "CHAR"),
             Bezel("freeze", "FREEZE", ink="PAPER")]),
    ], divide_after=(1,)),

    # What the CV inputs may take off each control. Each trimpot sits directly
    # over its own jack, with the label they share set in the gap between the
    # two. The clock and reset jacks sit beside the pairs and own nothing below
    # them, so they keep their labels overhead where a cable cannot cover them.
    # FREEZE is a gate in, and gates in this family live on the band with the
    # rest of the patching, which keeps this block to two rows. The CLOCK light
    # beside the caption is the clock being heard.
    Section("WITHHOLDING", caption_light="clock_led", rows=[
        Row([Trim("time_cv", "TIME", pair=True),
             Trim("mode_cv", "MODE", pair=True),
             Trim("subdiv_cv", "SUB", pair=True),
             Trim("mix_cv", "MIX", pair=True),
             Jack("clock_in", "CLOCK"),
             Jack("reset_in", "RESET")]),
        Row([Jack("time_in"), Jack("mode_in"),
             Jack("subdiv_in"), Jack("mix_in")], silent=True),
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
    Row([Jack("freeze_in", "FREEZE"),
         Jack("in_l", "IN L"), Jack("in_r", "IN R"),
         Jack("out_l", "OUT L", ink="MINT"),
         Jack("out_r", "OUT R", ink="MINT")], y=118.6),
]

if __name__ == "__main__":
    raise SystemExit(build(P, root=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))))

#!/usr/bin/env python3
"""The Gross panel, declared once.

Running this file regenerates the artwork, the shared hardware, src/PanelTheme.hpp
and the previews. See ../../panelkit/README.md for the pipeline.

Gross is Schedule C: the profit-or-loss form, read top to bottom the way the
signal flows. GROSS RECEIPTS is what comes in and how hard it is pushed -- the
input equaliser and DRIVE, the Wiener half of the block chain. ADJUSTMENTS is
the nonlinear block itself: the static and dynamic bias that shifts the
operating point, and the four numbers that shape the mapping curve. NET is what
leaves -- the wet/dry ledger, post gain and the output tone, the Hammerstein
tail. The read-out under the masthead draws the mapping curve as it stands,
bias and all, so a knob move is visible before it is audible.

The footer band carries the CV withholding as the paired idiom -- a trimpot
directly over its jack, one label for both -- beside the envelope out and the
stereo I/O. At 18 HP and compact density there is exactly room for that, which
is why the CV lives on the band rather than in a fourth section.
"""

import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
from panelkit import *   # noqa: E402

P = Panel(
    slug="Gross",
    title="GROSS",
    form="SCHEDULE C",
    hp=18,
    density="compact",
    glass=Glass(h=10.0),
)

C5 = P.cols(5, 9.0)       # 9.00, 27.36, 45.72, 64.08, 82.44
C4 = P.cols(4, 11.0)      # 11.00, 34.15, 57.29, 80.44
C9 = P.cols(9, 6.0)       # 6.00 .. 85.44, 9.93 apart

P.sections = [
    # The Wiener half: the linear pre-emphasis, then the pre-gain. Left to
    # right is signal order; DRIVE wears the ring because it is the one knob
    # you reach for.
    Section("GROSS RECEIPTS", rows=[
        Row([Knob("low", C5[0], "LOW"),
             Knob("mid", C5[1], "MID"),
             Knob("midf", C5[2], "MID F"),
             Knob("high", C5[3], "HIGH"),
             Knob("drive", C5[4], "DRIVE", primary=True)]),
    ]),

    # The nonlinear block. Row one moves the operating point: a static offset,
    # how much of the envelope is subtracted from it, and how fast that
    # envelope moves. The DYN label lights with the envelope so the bias shift
    # can be seen working. Row two is the mapping function's own four numbers
    # -- the knees and the slopes beyond them -- with the subtotal rule
    # between, because the two rows answer different questions.
    Section("ADJUSTMENTS", rows=[
        Row([Knob("offset", C5[0], "OFFSET"),
             Knob("dyn", C5[1], "DYN", light="env_led"),
             Knob("attack", C5[2], "ATTACK"),
             Knob("release", C5[3], "RELEASE"),
             Knob("curve", C5[4], "CURVE")]),
        Row([Trim("kp", C4[0], "KNEE+"),
             Trim("kn", C4[1], "KNEE-"),
             Trim("gp", C4[2], "SHAPE+"),
             Trim("gn", C4[3], "SHAPE-")]),
    ], divide_after=(0,)),

    # The Hammerstein tail: the wet/dry ledger, the post gain, and the output
    # filter -- a tilt and a low cut.
    Section("NET", rows=[
        Row([Knob("wet", C4[0], "WET"),
             Knob("post", C4[1], "POST"),
             Knob("tone", C4[2], "TONE"),
             Knob("locut", C4[3], "LO CUT")]),
    ]),
]

# The band: four CV pairs (trim over jack, one label), the envelope out, and
# the stereo I/O. The audio row sits as low as the bottom screws allow --
# RACK_GRID_HEIGHT - RACK_GRID_WIDTH puts their top edge at 123.61 mm, so a
# jack collar centred below 118.6 would run under one. The trim row is pinned
# just clear of the jack wells beneath it.
P.footer = [
    Row([Trim("drive_cv", C9[0], "DRIVE"),
         Trim("bias_cv", C9[1], "BIAS"),
         Trim("wet_cv", C9[2], "WET"),
         Trim("tone_cv", C9[3], "TONE")], y=108.4, label_side="above"),
    Row([Jack("drive_in", C9[0]), Jack("bias_in", C9[1]),
         Jack("wet_in", C9[2]), Jack("tone_in", C9[3]),
         Jack("env_out", C9[4], "ENV", ink="MINT"),
         Jack("in_l", C9[5], "IN L"), Jack("in_r", C9[6], "IN R"),
         Jack("out_l", C9[7], "OUT L", ink="MINT"),
         Jack("out_r", C9[8], "OUT R", ink="MINT")], y=118.6),
]

if __name__ == "__main__":
    raise SystemExit(build(P, root=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))))

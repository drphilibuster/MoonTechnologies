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
    density="compact",
    glass=Glass(h=10.0),
)

P.sections = [
    # The Wiener half: the linear pre-emphasis, then the pre-gain. Left to
    # right is signal order; DRIVE wears the ring because it is the one knob
    # you reach for.
    Section("GROSS RECEIPTS", rows=[
        Row([Knob("low", "LOW"),
             Knob("mid", "MID"),
             Knob("midf", "MID F"),
             Knob("high", "HIGH"),
             Knob("drive", "DRIVE", primary=True)]),
    ]),

    # The nonlinear block. Row one moves the operating point: a static offset,
    # how much of the envelope is subtracted from it, and how fast that
    # envelope moves. The DYN label lights with the envelope so the bias shift
    # can be seen working. CURVE picks one of five waveshapers, so it wears the
    # detent ring. Row two is the mapping function's own four numbers -- the
    # knees and the slopes beyond them -- with the subtotal rule between,
    # because the two rows answer different questions. That is also why the
    # trims take a grid of their own: lining a knee up with a knob above it
    # would be a coincidence, not a relationship.
    Section("ADJUSTMENTS", rows=[
        Row([Knob("offset", "OFFSET"),
             Knob("dyn", "DYN", light="env_led"),
             Knob("attack", "ATTACK"),
             Knob("release", "RELEASE"),
             Knob("curve", "CURVE", steps=5)]),
        Row([Trim("kp", "KNEE+"),
             Trim("kn", "KNEE-"),
             Trim("gp", "SHAPE+"),
             Trim("gn", "SHAPE-")], own_grid=True),
    ], divide_after=(0,)),

    # The Hammerstein tail: the wet/dry ledger, the post gain, and the output
    # filter -- a tilt and a low cut.
    Section("NET", rows=[
        Row([Knob("wet", "WET"),
             Knob("post", "POST"),
             Knob("tone", "TONE"),
             Knob("locut", "LO CUT")]),
    ]),
]

# The band: four CV pairs (trim over jack, the label they share between them),
# the envelope out, and the stereo I/O. The audio row sits as low as the bottom
# screws allow -- RACK_GRID_HEIGHT - RACK_GRID_WIDTH puts their top edge at
# 123.61 mm, so a jack collar centred below 118.6 would run under one. The trim
# row is pinned just clear of the jack wells beneath it.
P.footer = [
    Row([Trim("drive_cv", "DRIVE"),
         Trim("bias_cv", "BIAS"),
         Trim("wet_cv", "WET"),
         Trim("tone_cv", "TONE")], pair=True),
    Row([Jack("drive_in"), Jack("bias_in"),
         Jack("wet_in"), Jack("tone_in"),
         Jack("env_out", "ENV", ink="MINT"),
         Jack("in_l", "IN L"), Jack("in_r", "IN R"),
         Jack("out_l", "OUT L", ink="MINT"),
         Jack("out_r", "OUT R", ink="MINT")], y=118.6),
]

if __name__ == "__main__":
    raise SystemExit(build(P, root=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))))

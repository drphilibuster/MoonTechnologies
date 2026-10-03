#!/usr/bin/env python3
"""The Gross panel, declared once.

Running this file regenerates the artwork, the shared hardware, src/PanelTheme.hpp
and the previews. See ../../panelkit/README.md for the pipeline.

Gross is Schedule C: the profit-or-loss form, read top to bottom the way the
signal flows. GROSS RECEIPTS is what comes in and how hard it is pushed -- the
input equaliser and DRIVE, the Wiener half of the block chain. ADJUSTMENTS is
the envelope that moves the nonlinear block's operating point. NET is what
leaves -- post gain and the output tone, the Hammerstein tail.

The read-out under the masthead draws the mapping curve as it stands, bias and
all, so a move is visible before it is audible -- and every number beside it is
a control. CURVE is a click that lists the five waveshapers. DRIVE, OFFSET and
WET are values you hold and drag (DRIVE keeps its knob too: it is the one you
play). The bottom line is the mapping function's own four numbers, the knees
and the slopes beyond them, which used to be trimpots under ADJUSTMENTS and
were only ever visible as the curve's shape. The preset name opens the preset
list: a preset is every control at once rather than a param, so it is a menu
field rather than a value.

With CURVE, OFFSET, WET and the four shape trims on the glass, the knobs that
are left re-flow three wide, and the panel narrows to what the stereo band
needs. The four CV pairs stand down the left-hand edge in a rail.
"""

import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
from panelkit import *   # noqa: E402

P = Panel(
    slug="Gross",
    title="GROSS",
    what="DISTORTION",
    form="SCHEDULE C",
    density="compact",
    # Three lines beside the curve plot, which takes the first two columns of
    # every line. Top: the preset (a menu) and CURVE. Middle: DRIVE, OFFSET, WET
    # and the envelope (a read-out, not a control). Bottom: the mapping's knees
    # and shapes.
    glass=Glass(h=23.5, grid=(3, 6), fields=[
        Field("preset",    cell=(0, 2), span=(1, 3), kind="menu"),
        Field("curve",     cell=(0, 5), kind="select"),
        Field("drive_val", cell=(1, 2), kind="value"),
        Field("offset",    cell=(1, 3), kind="value"),
        Field("wet",       cell=(1, 4), kind="value"),
        Field("kp",        cell=(2, 2), kind="value"),
        Field("kn",        cell=(2, 3), kind="value"),
        Field("gp",        cell=(2, 4), kind="value"),
        Field("gn",        cell=(2, 5), kind="value"),
    ]),
)

P.sections = [
    # The Wiener half: the linear pre-emphasis, then the pre-gain, in signal
    # order across and down. DRIVE wears the ring because it is the one knob
    # you reach for.
    Section("GROSS RECEIPTS", rows=[
        Row([Knob("low", "LOW"),
             Knob("mid", "MID"),
             Knob("midf", "MID F")]),
        Row([Knob("high", "HIGH"),
             Knob("drive", "DRIVE", primary=True)]),
    ]),

    # What moves the nonlinear block's operating point: how much of the
    # envelope is subtracted from the static OFFSET (which is on the read-out),
    # and how fast that envelope moves. The DYN label lights with the envelope
    # so the bias shift can be seen working. The curve itself -- which one, and
    # its four numbers -- is on the read-out.
    Section("ADJUSTMENTS", rows=[
        Row([Knob("dyn", "DYN", light="env_led"),
             Knob("attack", "ATTACK"),
             Knob("release", "RELEASE")]),
    ]),

    # The Hammerstein tail: the post gain and the output filter -- a tilt and
    # a low cut. The wet/dry ledger is WET on the read-out.
    Section("NET", rows=[
        Row([Knob("post", "POST"),
             Knob("tone", "TONE"),
             Knob("locut", "LO CUT")]),
    ]),
]

# The four CV pairs -- an attenuator over the jack it scales, the one label between them --
# stand down the left-hand edge in a rail of their own: they are what comes in, and a trim
# over its jack is two rows tall, which on the footer made the band a second row taller than
# every other panel's. On the rail it costs height the sections already have, and the band
# can start where it does everywhere else.
P.rail = Rail([RailPair(Trim("drive_cv", ""), Jack("drive_in"), "DRIVE"),
               RailPair(Trim("bias_cv", ""), Jack("bias_in"), "BIAS"),
               RailPair(Trim("wet_cv", ""), Jack("wet_in"), "WET"),
               RailPair(Trim("tone_cv", ""), Jack("tone_in"), "TONE")],
              caption="CV", side="left", ink="SAGE")

# The band: the stereo I/O and the envelope out. The audio row sits as low as the bottom
# screws allow -- RACK_GRID_HEIGHT - RACK_GRID_WIDTH puts their top edge at 123.61 mm, so a
# jack collar centred below 118.6 would run under one. Signal in at the left edge, signal out
# at the right: the family reads left to right, and audio that entered two thirds of the way
# along -- next to the outputs it was about to become -- read as just another modulation jack.
P.footer = [
    Row([Jack("in_l", "IN L"), Jack("in_r", "IN R"),
         Jack("env_out", "ENV", ink="MINT"),
         Jack("out_l", "OUT L", ink="MINT"),
         Jack("out_r", "OUT R", ink="MINT")], y=118.6),
]

if __name__ == "__main__":
    raise SystemExit(build(P, root=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))))

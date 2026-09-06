#!/usr/bin/env python3
"""The Sign Here panel, declared once.

Running this file regenerates the artwork, the shared hardware, src/PanelTheme.hpp
and the previews. See ../../panelkit/README.md for the pipeline.

Sign Here is FORM 8879 -- the e-file signature authorization -- because every
control on it is a gesture: a joystick, four touch pads, a stage button and a
pedal. The joystick and the four pads are custom widgets, not stock VCV
controls, so they are declared as Plates (recessed GLASS wells, the family's
usual seat for anything live) sized here and drawn by
src/SignHere/SignHere.cpp; everything that IS a stock control -- the scale and
offset trims, the level knob, the invert switch, every jack -- goes through
the ordinary row solver like any other panel in the family.

The plates sit in a reserved band under the masthead, the same trick
Repossession uses for its screen: a Glass whose height is claimed by
`cursor` before the first Section, with nothing actually drawn in it by the
panel renderer -- the live content is the custom widgets layered on top at
runtime.
"""

import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
from panelkit import *   # noqa: E402

P = Panel(
    slug="SignHere",
    title="SIGN HERE",
    subtitle="E-FILE SIGNATURE",
    form="FORM 8879",
    hp=20,
    density="compact",
    glass=Glass(h=24.5),
)

W = P.w                         # 101.60 mm
M = 6.0
IW = W - 2 * M                  # 89.60 mm

# --- the gesture surfaces: one big joystick pad, four small touch pads -----
JOY_X, JOY_Y, JOY_W, JOY_H = M, 11.2, 22.0, 22.0
TP_W, TP_H, TP_GAP = 11.0, 11.0, 1.5
TP_X0 = JOY_X + JOY_W + 4.0
TP_Y0 = JOY_Y
TP_X1 = TP_X0 + TP_W + TP_GAP
TP_Y1 = TP_Y0 + TP_H + TP_GAP

P.plates = [
    Plate("joy_pad", JOY_X, JOY_Y, JOY_W, JOY_H, r=1.4, tab="LIME"),
    Plate("pad1", TP_X0, TP_Y0, TP_W, TP_H, r=1.2, tab="MINT"),
    Plate("pad2", TP_X1, TP_Y0, TP_W, TP_H, r=1.2),
    Plate("pad3", TP_X0, TP_Y1, TP_W, TP_H, r=1.2),
    Plate("pad4", TP_X1, TP_Y1, TP_W, TP_H, r=1.2),
]

def _joy_knob_row():
    x = P.cols(6, 12.0)
    return [
        Knob("x_scale", x[0], "X SCALE"),
        Knob("x_offset", x[1], "X OFFSET"),
        Knob("y_scale", x[2], "Y SCALE"),
        Knob("y_offset", x[3], "Y OFFSET"),
        Knob("glide", x[4], "GLIDE"),
        Knob("amount", x[5], "AMOUNT"),
    ]


def _all_jacks_row():
    x = P.cols(8, 8.0)
    return [
        Jack("x_cv_out", x[0], "X CV", ink="MINT"),
        Jack("y_cv_out", x[1], "Y CV", ink="MINT"),
        Jack("touch_gate_out", x[2], "TOUCH", ink="MINT"),
        Jack("pedal_in", x[3], "PEDAL"),
        Jack("btn_gate_out", x[4], "GATE", ink="MINT"),
        Jack("btn_trig_out", x[5], "TRIG", ink="MINT"),
        Jack("btn_flip_out", x[6], "FLIP", ink="MINT"),
        Jack("btn_cv_out", x[7], "CV", ink="MINT"),
    ]


def _touch_row(a, b):
    x = P.cols(6, 9.0)
    return [
        Jack("gate%d" % a, x[0], "GATE"),
        Jack("trig%d" % a, x[1], "TRIG"),
        Jack("cv%d" % a, x[2], "CV", ink="MINT"),
        Jack("gate%d" % b, x[3], "GATE"),
        Jack("trig%d" % b, x[4], "TRIG"),
        Jack("cv%d" % b, x[5], "CV", ink="MINT"),
    ]


def _button_row():
    x = P.cols(3, 20.0)
    return [
        Bezel("button", x[0], "BUTTON", primary=True),
        Knob("level", x[1], "LEVEL"),
        Switch("inv", x[2], "INV"),
    ]



# One section for everything: the row solver's budget is fixed by the panel's
# height, not by how many logical sub-modules it holds, and three captioned
# blocks here cost three lots of clearance the layout can't spare once the
# gesture surfaces above have claimed their own share of it. Turn-or-press
# controls first, then every jack that isn't a touch pad's, then the touch
# pads' own jacks two pads to a row.
P.sections = [
    Section("SIGNATURE", rows=[
        Row(_joy_knob_row()),
        Row(_button_row()),
        Row(_all_jacks_row()),
        Row(_touch_row(1, 2)),
        Row(_touch_row(3, 4)),
    ]),
]

# The joystick's own IN/OUT utility path -- the schematic's plain
# signal-through jacks, on the footer band like any other module's main I/O.
P.footer = [
    Row([Jack("util_in", P.cols(2, 32.0)[0], "IN"),
         Jack("util_out", P.cols(2, 32.0)[1], "OUT", ink="MINT")], y=118.6),
]

P.metrics = dict(
    M=M, IW=IW,
    JOY_X=JOY_X, JOY_Y=JOY_Y, JOY_W=JOY_W, JOY_H=JOY_H,
    TP_X0=TP_X0, TP_X1=TP_X1, TP_Y0=TP_Y0, TP_Y1=TP_Y1, TP_W=TP_W, TP_H=TP_H,
)

if __name__ == "__main__":
    raise SystemExit(build(P, root=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))))

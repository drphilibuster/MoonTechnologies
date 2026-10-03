#!/usr/bin/env python3
"""The Sign Here panel, declared once.

Running this file regenerates the artwork, the shared hardware, src/PanelTheme.hpp
and the previews. See ../../panelkit/README.md for the pipeline.

Sign Here is FORM 8879 -- the e-file signature authorization -- because every
control on it is a gesture: a joystick, four touch pads, a stage button and a
pedal. The joystick, the four pads and the button are custom surfaces, not stock
VCV controls, so they are declared as Plates (recessed GLASS wells, the family's
usual seat for anything live) sized here and drawn by
src/SignHere/SignHere.cpp; everything that IS a stock control -- the scale and
offset trims, the level knob, the invert switch, every jack -- goes through
the ordinary row solver like any other panel in the family.

The plates sit in a reserved band under the masthead, the same trick
Repossession uses for its screen: a Glass whose height is claimed by
`cursor` before the first Section, with nothing actually drawn in it by the
panel renderer -- the live content is the custom widgets layered on top at
runtime.

The stage button is the fifth pad, a tall one down the right of the glass,
where there was 14 mm of well with nothing in it. It is a field (a ScreenButton
on BUTTON_PARAM), so it is the same momentary param the bezel was -- the pedal,
GATE, TRIG, FLIP and CV behave exactly as before -- and it lights lime while
the button or the pedal is down, as the bezel did. The row the bezel stood in
took the joystick's three outputs, and the button's four went to a row of their
own, which is what let the panel narrow: 20 HP to 17. Six knobs in a row is now
what sets the width.
"""

import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
from panelkit import *   # noqa: E402

P = Panel(
    slug="SignHere",
    title="SIGN HERE",
    what="CONTROLLERS",
    subtitle="E-FILE SIGNATURE",
    form="FORM 8879",
    # Pinned because the plates inside the well are laid out against the width;
    # 17 is what the rows below solve to on their own (the knob row sets it).
    hp=17,
    density="compact",
    glass=Glass(h=21.5, fields=[
        Field("button", plate="btn_pad", kind="button"),
    ]),
)

W = P.w                         # 86.36 mm
M = 6.0
IW = W - 2 * M                  # 74.36 mm

# --- the gesture surfaces: one big joystick pad, four small touch pads and
# the stage button, a tall pad down the right-hand end of the well ----------
JOY_X, JOY_Y, JOY_W, JOY_H = M, 10.3, 24.0, 20.6
TP_H, TP_GAP, BTN_W = 9.7, 1.4, 10.0
TP_X0 = JOY_X + JOY_W + 3.0
TP_W = (W - M - BTN_W - TP_GAP - TP_X0 - TP_GAP) / 2      # 17.28 mm
TP_X1 = TP_X0 + TP_W + TP_GAP
BTN_X = TP_X1 + TP_W + TP_GAP
TP_Y0 = JOY_Y
TP_Y1 = TP_Y0 + TP_H + TP_GAP

P.plates = [
    Plate("joy_pad", JOY_X, JOY_Y, JOY_W, JOY_H, r=1.4, tab="LIME"),
    Plate("pad1", TP_X0, TP_Y0, TP_W, TP_H, r=1.2, tab="MINT"),
    Plate("pad2", TP_X1, TP_Y0, TP_W, TP_H, r=1.2),
    Plate("pad3", TP_X0, TP_Y1, TP_W, TP_H, r=1.2),
    Plate("pad4", TP_X1, TP_Y1, TP_W, TP_H, r=1.2),
    Plate("btn_pad", BTN_X, JOY_Y, BTN_W, JOY_H, r=1.2, tab="LIME"),
]

# One section for everything: the row solver's budget is fixed by the panel's
# height, not by how many logical sub-modules it holds, and three captioned
# blocks here cost three lots of clearance the layout cannot spare once the
# gesture surfaces above have claimed their share of it. Turn controls first;
# then the joystick's three outputs beside the button's pedal input and its two
# settings; then the button's four outputs; then the touch pads' own jacks, two
# pads to a row -- and each of the first three rows is on a grid of its own,
# because they have no columns in common.
P.sections = [
    Section("SIGNATURE", groups=(3, 3), rows=[
        Row([Knob("x_scale", "X SCALE"),
             Knob("x_offset", "X OFFSET"),
             Knob("y_scale", "Y SCALE"),
             Knob("y_offset", "Y OFFSET"),
             Knob("glide", "GLIDE"),
             Knob("amount", "AMOUNT")], own_grid=True),
        Row([Jack("x_cv_out", "X CV", ink="MINT"),
             Jack("y_cv_out", "Y CV", ink="MINT"),
             Jack("touch_gate_out", "TOUCH", ink="MINT"),
             Jack("pedal_in", "PEDAL"),
             Knob("level", "LEVEL"),
             Switch("inv", "INV")], own_grid=True),
        Row([Jack("btn_gate_out", "GATE", ink="MINT"),
             Jack("btn_trig_out", "TRIG", ink="MINT"),
             Jack("btn_flip_out", "FLIP", ink="MINT"),
             Jack("btn_cv_out", "CV", ink="MINT")], own_grid=True),
        # The four touch pads' own jacks: three to a pad, and the section names
        # the runs so each pad's three read as one group rather than six in a line.
        Row([Jack("gate1", "GATE"), Jack("trig1", "TRIG"),
             Jack("cv1", "CV", ink="MINT"),
             Jack("gate2", "GATE"), Jack("trig2", "TRIG"),
             Jack("cv2", "CV", ink="MINT")]),
        Row([Jack("gate3", "GATE"), Jack("trig3", "TRIG"),
             Jack("cv3", "CV", ink="MINT"),
             Jack("gate4", "GATE"), Jack("trig4", "TRIG"),
             Jack("cv4", "CV", ink="MINT")]),
    ]),
]

# The joystick's own IN/OUT utility path -- the schematic's plain
# signal-through jacks, on the footer band like any other module's main I/O.
P.footer = [
    Row([Jack("util_in", "IN"),
         Jack("util_out", "OUT", ink="MINT")], y=118.6),
]

P.metrics = dict(
    M=M, IW=IW,
    JOY_X=JOY_X, JOY_Y=JOY_Y, JOY_W=JOY_W, JOY_H=JOY_H,
    TP_X0=TP_X0, TP_X1=TP_X1, TP_Y0=TP_Y0, TP_Y1=TP_Y1, TP_W=TP_W, TP_H=TP_H,
    BTN_X=BTN_X, BTN_W=BTN_W,
)

if __name__ == "__main__":
    raise SystemExit(build(P, root=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))))

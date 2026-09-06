#!/usr/bin/env python3
"""The Repossession panel, declared once.

Running this file regenerates the artwork, the shared hardware, src/PanelTheme.hpp
and the previews. See ../../panelkit/README.md for the pipeline.

Repossession is FORM 1099-A -- acquisition or abandonment of secured property.
You give it a link, it seizes the media: the video goes on the screen, the audio
goes into RAM, and the timeline strip under the screen is the schedule of what
has been taken. SEIZED ASSETS are the eight region slots; LIENS are the terms
charged against the selected one; COLLECTIONS is what drives the sequence.

The panel is laid out the way PatchAudit is -- a read-out well that fills the
top third, with plates for the fields inside it -- because the screen, the URL
box and the timeline are one instrument, not three controls. Everything inside
the well is drawn by RepossessionDisplay from the millimetre constants echoed
into src/Repossession/Panel.hpp below, so the artwork and the live widget cannot
disagree about where the video ends and the timeline begins.

Density is "compact": three rows of controls under a 39.0 mm read-out leaves no
room for the regular scale.
"""

import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
from panelkit import *   # noqa: E402

P = Panel(
    slug="Repossession",
    title="REPOSSESSION",
    subtitle="SECURED PROPERTY",
    form="FORM 1099-A",
    hp=34,
    density="compact",
    # The read-out is the module. 39.0 mm is everything the three control rows
    # and the footer band can spare -- see the arithmetic in COLLECTIONS below.
    glass=Glass(h=39.0),
)

W = P.w                         # 172.72 mm
M = 6.5                         # side margin, inside the glass well's own 4.2
IW = W - 2 * M                  # 159.72 mm of usable width

# --- inside the read-out well -----------------------------------------------
# The well runs 10.20 .. 49.80. The video sits left at 16:9; the URL box and the
# asset report share the column beside it; the timeline spans the full width
# underneath, because a region is a span of the whole clip and reads as one.
GLASS_Y0, GLASS_H = 10.2, 39.0

VID_X, VID_Y, VID_W, VID_H = M, 11.7, 56.0, 31.5        # 11.70 .. 43.20, 16:9
URL_X, URL_Y, URL_W, URL_H = 66.0, 11.7, W - 66.0 - M, 7.0
INFO_X, INFO_Y = URL_X, 19.6
INFO_W, INFO_H = URL_W, 43.2 - 19.6
TL_X, TL_Y, TL_W, TL_H = M, 44.0, IW, 4.9               # 44.00 .. 48.90

C8 = P.cols(8, 14.0)
C7 = P.cols(7, 14.0)
C6 = P.cols(6, 14.0)

P.plates = [
    # Glass on glass: only the sage hairline shows, which is the family's well
    # border and exactly what a screen wants round it.
    Plate("video", VID_X, VID_Y, VID_W, VID_H, r=0.8),
    Plate("url", URL_X, URL_Y, URL_W, URL_H, fill=BAND, r=1.0, tab="LIME"),
    Plate("info", INFO_X, INFO_Y, INFO_W, INFO_H, fill=BAND, r=1.0),
    Plate("timeline", TL_X, TL_Y, TL_W, TL_H, r=0.8),
]

P.sections = [
    # The eight slots. Unlabelled on purpose: the timeline directly above names
    # them far better than eight digits could, and each button lights when its
    # region is the selected one. The caption light is the fetch/decode state.
    Section("SEIZED ASSETS", caption_light="busy", rows=[
        Row([Bezel("slot1", C8[0]), Bezel("slot2", C8[1]),
             Bezel("slot3", C8[2]), Bezel("slot4", C8[3]),
             Bezel("slot5", C8[4]), Bezel("slot6", C8[5]),
             Bezel("slot7", C8[6]), Bezel("slot8", C8[7])], silent=True),
    ]),

    # What is charged against the selected region, plus the two controls that
    # decide how the schedule is worked through. RUN is the primary action.
    # There is no SKIP control because there is nothing to skip: a slot either
    # holds a region or it is empty, and the sequencer only visits the ones that
    # hold something. Emptying a slot IS skipping it.
    Section("LIENS", rows=[
        Row([Knob("region", C7[0], "REGION"),
             Knob("speed", C7[1], "SPEED"),
             Knob("gain", C7[2], "GAIN"),
             Switch("loop", C7[3], "LOOP"),
             Switch("rev", C7[4], "REV"),
             Knob("mode", C7[5], "MODE"),
             Bezel("run", C7[6], "RUN", primary=True)]),
    ]),

    # Everything that drives the sequence from outside.
    Section("COLLECTIONS", rows=[
        Row([Jack("clock_in", C6[0], "CLOCK", light="clock_led"),
             Jack("reset_in", C6[1], "RESET"),
             Jack("region_in", C6[2], "REGION"),
             Jack("scan_in", C6[3], "SCAN"),
             Jack("speed_in", C6[4], "SPEED"),
             Jack("fire_in", C6[5], "FIRE")]),
    ]),
]

# The audio row sits as low as the bottom screws allow: RACK_GRID_HEIGHT -
# RACK_GRID_WIDTH puts their top edge at 123.61 mm, so a jack collar centred
# below 118.6 would run under one. Everything that leaves the module is here,
# and all of it is an output -- the "video out" is the screen.
P.footer = [
    Row([Jack("out_l", C6[0], "OUT L", ink="MINT"),
         Jack("out_r", C6[1], "OUT R", ink="MINT"),
         Jack("pos_out", C6[2], "POS", ink="MINT"),
         Jack("gate_out", C6[3], "GATE", ink="MINT"),
         Jack("eor_out", C6[4], "EOR", ink="MINT"),
         Jack("reg_out", C6[5], "REGION", ink="MINT")], y=118.6),
]

# Echoed into src/Repossession/Panel.hpp so the live display and the artwork
# cannot disagree about where a field is.
P.metrics = dict(
    M=M, IW=IW,
    GLASS_Y0=GLASS_Y0, GLASS_H=GLASS_H,
    VID_X=VID_X, VID_Y=VID_Y, VID_W=VID_W, VID_H=VID_H,
    URL_X=URL_X, URL_Y=URL_Y, URL_W=URL_W, URL_H=URL_H,
    INFO_X=INFO_X, INFO_Y=INFO_Y, INFO_W=INFO_W, INFO_H=INFO_H,
    TL_X=TL_X, TL_Y=TL_Y, TL_W=TL_W, TL_H=TL_H,
    NUM_SLOTS=8,
)

if __name__ == "__main__":
    raise SystemExit(build(P, root=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))))

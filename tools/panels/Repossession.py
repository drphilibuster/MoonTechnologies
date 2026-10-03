#!/usr/bin/env python3
"""The Repossession panel, declared once.

Running this file regenerates the artwork, the shared hardware, src/PanelTheme.hpp
and the previews. See ../../panelkit/README.md for the pipeline.

Repossession is FORM 1099-A -- acquisition or abandonment of secured property.
You give it a link, it seizes the media: the video goes on the screen, the audio
goes into RAM, and the timeline strip under the screen is the schedule of what
has been taken. SEIZED ASSETS are the eight region slots; LIENS are the terms
charged against them and the transport that works the schedule; COLLECTIONS is
everything patched in from outside.

The panel is laid out the way PatchAudit is -- a read-out well that fills the
top third, with plates for the fields inside it -- because the screen, the URL
box and the timeline are one instrument, not three controls. Everything inside
the well is drawn by RepossessionDisplay from the millimetre constants echoed
into src/Repossession/Panel.hpp below, so the artwork and the live widget cannot
disagree about where the video ends and the timeline begins.

Density is "compact".

21 HP. The eight LIENS controls -- region, speed, gain, loop, direction,
sequence mode, tempo and transport -- used to be a row of knobs under a screen
that already printed half of them. They are fields on the screen now, in a strip
of their own between the video and the timeline, and the row they stood in went
to the read-out instead. The timeline is narrower than it was at 34 HP; it zooms
under the scroll wheel, and it is taller now, which is what carving at a fine
zoom wants.
"""

import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
from panelkit import *   # noqa: E402

P = Panel(
    slug="Repossession",
    title="REPOSSESSION",
    what="VIDEO SAMPLER",
    subtitle="SECURED PROPERTY",
    form="FORM 1099-A",
    # Pinned because the plates inside the well are laid out against the width;
    # 21 is what the rows below solve to on their own.
    hp=21,
    density="compact",
    # URL, the video beside the asset report, the LIENS strip and the timeline,
    # stacked; the arithmetic is in the plates below.
    glass=Glass(h=59.2, fields=[
        Field("region",  plate="liens", cell=(0, 0), kind="select"),
        Field("speed",   plate="liens", cell=(0, 1), kind="value"),
        Field("gain",    plate="liens", cell=(0, 2), kind="value"),
        Field("loop",    plate="liens", cell=(0, 3), kind="toggle"),
        Field("rev",     plate="liens", cell=(0, 4), kind="toggle"),
        Field("mode",    plate="liens", cell=(0, 5), span=(1, 2), kind="select"),
        Field("tempo",   plate="liens", cell=(0, 7), kind="value"),
        Field("runmode", plate="liens", cell=(0, 8), kind="select"),
    ]),
)

W = P.w                         # 106.68 mm
M = 6.5                         # side margin, inside the glass well's own 4.2
IW = W - 2 * M                  # 93.68 mm of usable width

# --- inside the read-out well ------------------------------------------------
# The well's own geometry is the spec's (Glass, above) and reaches the C++ as
# panel::GLASS_*; only what sits inside it is laid out here. Top to bottom: the
# URL box across the width; the video at 16:9 with the asset report beside it;
# the LIENS strip, whose cells are the fields above; and the timeline across the
# full width, because a region is a span of the whole clip and reads as one.
URL_X, URL_Y, URL_W, URL_H = M, 11.3, IW, 7.0           # 11.30 .. 18.30
VID_X, VID_Y, VID_W, VID_H = M, 19.1, 56.0, 31.5        # 19.10 .. 50.60, 16:9
INFO_X, INFO_Y = M + VID_W + 1.5, VID_Y
INFO_W, INFO_H = W - M - INFO_X, VID_H
LIEN_X, LIEN_Y, LIEN_W, LIEN_H = M, 51.4, IW, 7.4       # 51.40 .. 58.80
TL_X, TL_Y, TL_W, TL_H = M, 59.6, IW, 8.2               # 59.60 .. 67.80

P.plates = [
    # Glass on glass: only the sage hairline shows, which is the family's well
    # border and exactly what a screen wants round it.
    Plate("video", VID_X, VID_Y, VID_W, VID_H, r=0.8),
    Plate("url", URL_X, URL_Y, URL_W, URL_H, fill=BAND, r=1.0, tab="LIME"),
    Plate("info", INFO_X, INFO_Y, INFO_W, INFO_H, fill=BAND, r=1.0),
    Plate("liens", LIEN_X, LIEN_Y, LIEN_W, LIEN_H, fill=BAND, r=1.0, grid=(1, 9)),
    Plate("timeline", TL_X, TL_Y, TL_W, TL_H, r=0.8),
]

P.sections = [
    # The eight slots. Unlabelled on purpose: the timeline directly above names
    # them far better than eight digits could, and each button carries its slot's
    # own colour so a light and its span on the strip cannot be told apart. The
    # caption light is the fetch/decode state.
    Section("SEIZED ASSETS", caption_light="busy", rows=[
        Row([Bezel("slot1"), Bezel("slot2"), Bezel("slot3"), Bezel("slot4"),
             Bezel("slot5"), Bezel("slot6"), Bezel("slot7"), Bezel("slot8")],
            silent=True),
    ]),

    # Everything that drives the sequence from outside. The last four are
    # polyphonic and per-step: channel N addresses slot N, so one cable carries
    # all eight. A monophonic cable in any of them applies to every step at once,
    # which is what a single LFO into SPEED should obviously do.
    Section("COLLECTIONS", rows=[
        Row([Jack("clock_in", "CLOCK", light="clock_led"),
             Jack("reset_in", "RESET"),
             Jack("region_in", "REGION"),
             Jack("scan_in", "SCAN"),
             Jack("fire_in", "FIRE"),
             Jack("speed_in", "SPEED"),
             Jack("gain_in", "GAIN"),
             Jack("start_in", "START"),
             Jack("len_in", "LENGTH")]),
    ]),
]

# The audio row sits as low as the bottom screws allow: RACK_GRID_HEIGHT -
# RACK_GRID_WIDTH puts their top edge at 123.61 mm, so a jack collar centred
# below 118.6 would run under one. Everything that leaves the module is here,
# and all of it is an output. STEPS is polyphonic: channel N is slot N's own
# audio, summed to mono, so a step can be sent somewhere of its own without an
# expander in the rack.
P.footer = [
    Row([Jack("out_l", "OUT L", ink="MINT"),
         Jack("out_r", "OUT R", ink="MINT"),
         Jack("steps_out", "STEPS", ink="MINT"),
         Jack("pos_out", "POS", ink="MINT"),
         Jack("gate_out", "GATE", ink="MINT"),
         Jack("eor_out", "EOR", ink="MINT"),
         Jack("reg_out", "REGION", ink="MINT")], y=118.6),
]

# Echoed into src/Repossession/Panel.hpp so the live display and the artwork
# cannot disagree about where a field is.
P.metrics = dict(
    M=M, IW=IW,
    VID_X=VID_X, VID_Y=VID_Y, VID_W=VID_W, VID_H=VID_H,
    URL_X=URL_X, URL_Y=URL_Y, URL_W=URL_W, URL_H=URL_H,
    INFO_X=INFO_X, INFO_Y=INFO_Y, INFO_W=INFO_W, INFO_H=INFO_H,
    LIEN_X=LIEN_X, LIEN_Y=LIEN_Y, LIEN_W=LIEN_W, LIEN_H=LIEN_H,
    TL_X=TL_X, TL_Y=TL_Y, TL_W=TL_W, TL_H=TL_H,
    NUM_SLOTS=8,
)

if __name__ == "__main__":
    raise SystemExit(build(P, root=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))))

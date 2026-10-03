#!/usr/bin/env python3
"""The Projection panel, declared once.

Running this file regenerates the artwork, the shared hardware, src/PanelTheme.hpp
and the previews. See ../../panelkit/README.md for the pipeline.

Projection is FORM 1120-W -- the estimated tax worksheet, a projection -- and it
is also the other kind. It makes video out of what the rack is already doing:
audio in, spectrum out, and a picture driven by both that leaves through the
video bus for Transmittal to publish.

The read-out is the picture itself, small, so you can see what is being sent
without switching to the thing receiving it. MODE is a label in its bottom-left
corner, and clicking the label steps to the next mode.

Three sections. SIGNAL is what it listens to. PICTURE is what it draws and most
controls have a jack, because a video module whose look cannot be
sequenced is a screensaver. The footer is the analysis leaving as CV -- three
bands and an onset trigger -- which is worth having patched even when nothing is
looking at the video.
"""

import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
from panelkit import *   # noqa: E402

P = Panel(
    slug="Projection",
    title="PROJECTION",
    what="VIDEO GENERATOR",
    form="FORM 1120-W",
    density="compact",
    # Pinned: a video module's screen only ever grows. The rows would let it
    # narrow, and the picture would lose the width it is watched across.
    hp=12,
    # The picture fills the glass, as before. MODE is a label drawn over its
    # bottom-left corner -- the last line of a seven-line grid, two columns of
    # four -- and clicking that label steps the picture to the next mode. Only
    # the label takes the mouse; the rest of the picture is just the picture.
    # The glass cannot grow to give MODE a strip of its own: the rows below
    # leave about a millimetre and a half.
    glass=Glass(h=32.0, grid=(7, 4), fields=[
        Field("mode", cell=(6, 0), span=(1, 2), kind="toggle"),
    ]),
)

P.sections = [
    # What it listens to. SENS is input gain into the analysis rather than a
    # threshold: the spectrum is the instrument here, and it wants to be driven.
    # TILT leans the weighting up the spectrum -- music is roughly pink, so at 0
    # the bass end is permanently lit and the top never moves.
    Section("SIGNAL", rows=[
        Row([Jack("audio_l", "L"), Jack("audio_r", "R"),
             Knob("sens", "SENS"), Knob("tilt", "TILT")]),
    ]),

    # What it draws. MODE picks the picture -- the XY scope off L and R, the
    # spectrum as bars, or a field warped by the band energies -- and lives on
    # the picture itself now; its jack stands in the last row. SCALE, WARP and
    # HUE each have their own jack directly under them -- paired, so the panel
    # says which belongs to which -- because the whole point is that a patch can
    # play the look. TRAIL took the column MODE's knob left; the slot under it
    # is empty.
    Section("PICTURE", rows=[
        # TRAIL is how long the picture keeps what it drew, in seconds rather
        # than in frames, so it means the same at any output rate. FLASH adds
        # light on a gate; FREEZE holds the last frame for as long as it is high,
        # which is the one thing a video module needs that an audio one does not.
        Row([Knob("trail", "TRAIL"), Knob("scale", "SCALE"),
             Knob("warp", "WARP"), Knob("hue", "HUE")],
            pair=True),
        Row([Jack("scale_cv", col=1),
             Jack("warp_cv", col=2), Jack("hue_cv", col=3)], silent=True),
        Row([Knob("sat", "SAT"), Jack("mode_cv", "MODE"),
             Jack("flash", "FLASH"), Jack("freeze", "FREEZE")]),
    ]),
]

# The analysis, leaving as CV. Three bands and an onset: enough to drive
# envelopes, filters or another module's picture from whatever is playing,
# whether or not anything is watching the video.
P.footer = [
    Row([Jack("low_out", "LOW", ink="MINT"),
         Jack("mid_out", "MID", ink="MINT"),
         Jack("high_out", "HIGH", ink="MINT"),
         Jack("onset_out", "ONSET", ink="MINT")], y=118.6),
]

if __name__ == "__main__":
    raise SystemExit(build(P, root=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))))

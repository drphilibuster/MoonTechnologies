#!/usr/bin/env python3
"""The Projection panel, declared once.

Running this file regenerates the artwork, the shared hardware, src/PanelTheme.hpp
and the previews. See ../../panelkit/README.md for the pipeline.

Projection is FORM 1120-W -- the estimated tax worksheet, a projection -- and it
is also the other kind. It makes video out of what the rack is already doing:
audio in, spectrum out, and a picture driven by both that leaves through the
video bus for Transmittal to publish.

The read-out is the picture itself, small, so you can see what is being sent
without switching to the thing receiving it.

Three sections. SIGNAL is what it listens to. PICTURE is what it draws and every
control has a jack under it, because a video module whose look cannot be
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
    form="FORM 1120-W",
    density="compact",
    # 16:9 at the panel's own width, so the preview is the shape of the thing
    # being sent rather than a crop of it.
    glass=Glass(h=32.0),
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

    # What it draws. MODE picks the picture: the XY scope off L and R, the
    # spectrum as bars, or a field warped by the band energies. Every knob in
    # the first row has its own jack directly under it -- paired, so the panel
    # says which belongs to which -- because the whole point is that a patch can
    # play the look.
    Section("PICTURE", rows=[
        Row([Knob("mode", "MODE", steps=3), Knob("scale", "SCALE"),
             Knob("warp", "WARP"), Knob("hue", "HUE")],
            pair=True),
        Row([Jack("mode_cv"), Jack("scale_cv"),
             Jack("warp_cv"), Jack("hue_cv")], silent=True),
        # TRAIL is how long the picture keeps what it drew, in seconds rather
        # than in frames, so it means the same at any output rate. FLASH adds
        # light on a gate; FREEZE holds the last frame for as long as it is high,
        # which is the one thing a video module needs that an audio one does not.
        Row([Knob("trail", "TRAIL"), Knob("sat", "SAT"),
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

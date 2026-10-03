#!/usr/bin/env python3
"""The Amortization panel, declared once.

Running this file regenerates the artwork, the shared hardware, src/PanelTheme.hpp
and the previews. See ../../panelkit/README.md for the pipeline.

Amortization is the Pittsburgh Modular Verbtronic, so this is the Verbtronic's
panel: FEEDBACK, TILT, MIX, a MODE switch and a MIX CV attenuverter, with an
audio in, a MIX out and a wet-only VERB out. A debt paid off in reflections --
TERM is how long it runs (the feedback), SCHEDULE the character of the
repayments (the tonal tilt, which of the two clock settings the three PT2399s
run at, and the split between principal and interest) and PAYMENTS what CV may
adjust on the way in, with the gate that flips Tronic to Verb. The LIMIT light
beside the TERM caption is the feedback zener, or any stage of the loop,
clipping. Nothing here is added to the original: no SIZE, no stereo, no freeze.

The read-out is where MODE lives now: the original's toggle is the TRONIC/VERB
word at the top right, a click flipping it. It shows the mode that is running
once the gate has had its say, so a gate pulling Tronic down to Verb is seen
there too. FEEDBACK, TILT and MIX are printed on its bottom line, each a value
you hold and drag; their knobs stay, because they are what you play.
"""

import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
from panelkit import *   # noqa: E402

P = Panel(
    slug="Amortization",
    title="AMORTIZATION",
    what="TRIPLE-PT2399 REVERB",
    form="PUB 535",
    density="compact",
    # Three lines. The top is the first chip's delay and the mode (a click); the
    # middle its clock and the limiter; the bottom the three pots, each a field.
    glass=Glass(h=24.0, grid=(3, 6), fields=[
        Field("mode",         cell=(0, 4), span=(1, 2), kind="toggle"),
        Field("feedback_val", cell=(2, 0), span=(1, 2), kind="value"),
        Field("tilt_val",     cell=(2, 2), span=(1, 2), kind="value"),
        Field("mix_val",      cell=(2, 4), span=(1, 2), kind="value"),
    ]),
)

P.sections = [
    # How long the debt runs. FEEDBACK is the control you reach for, so it wears
    # the ring; the LIMIT light beside the caption is the loop's limiter working.
    Section("TERM", caption_light="limit", rows=[
        Row([BigKnob("feedback", "FEEDBACK", primary=True)]),
    ]),

    # The character of the repayments. MIX is the split between principal (dry)
    # and interest (wet) at MIX OUT. Which of the two clock settings runs is the
    # MODE word on the read-out.
    Section("SCHEDULE", rows=[
        Row([Knob("tilt", "TILT"), Knob("mix", "MIX")]),
    ]),

    # What CV may take off the mix on the way in, and the gate that flips
    # Tronic to Verb. The trimpot sits directly over its jack, one label serving
    # both; the gate jack owns nothing below it, so it keeps its name above it.
    Section("PAYMENTS", rows=[
        Row([Trim("mix_cv", "MIX"),
             Jack("mode_in", "MODE GATE")], pair=True),
        Row([Jack("mix_in")], silent=True),
    ]),
]

# The audio row sits as low as the bottom screws allow: RACK_GRID_HEIGHT -
# RACK_GRID_WIDTH puts their top edge at 123.61 mm, so a jack collar centred
# below 118.6 would run under one. The original's three: in, the MIX blend, and
# the wet-only VERB out.
P.footer = [
    Row([Jack("in", "IN"),
         Jack("mix_out", "MIX", ink="MINT"),
         Jack("verb_out", "VERB", ink="MINT")], y=118.6),
]

if __name__ == "__main__":
    raise SystemExit(build(P, root=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))))

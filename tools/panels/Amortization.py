#!/usr/bin/env python3
"""The Amortization panel, declared once.

Running this file regenerates the artwork, the shared hardware, src/PanelTheme.hpp
and the previews. See ../../panelkit/README.md for the pipeline.

Amortization is a debt paid off in reflections: a Verbtronic-style digital
reverb whose tail is the schedule and whose FEEDBACK is the interest rate.
TERM sets how long the debt runs -- feedback, the size of the space, and the
pre-delay before the first payment falls due. SCHEDULE is the character of the
repayments: the tonal tilt of the tail, which of the two algorithms carries
it (VERB, the shorter natural one, or TRONIC, the large synthetic one), the
depth of the tank modulation and the FREEZE that suspends payments entirely.
PAYMENTS is what CV may adjust on the way in, and the two gates. The LIMIT light
beside the TERM caption is the soft limiter inside the Tronic loop catching a
tail that has run past unity.

Compact density: five rows of controls, a read-out and a six-jack
audio row at 12 HP.
"""

import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
from panelkit import *   # noqa: E402

P = Panel(
    slug="Amortization",
    title="AMORTIZATION",
    form="PUB 535",
    density="compact",
    glass=Glass(h=9.2),
)

P.sections = [
    # How long the debt runs. FEEDBACK is the control you reach for, so it wears
    # the ring; the LIMIT light beside the caption is the loop limiter working.
    # MOD is the depth of the tank modulation -- a trimmer, as on a service
    # panel, because it is set once and left.
    Section("TERM", caption_light="limit", rows=[
        Row([BigKnob("feedback", "FEEDBACK", primary=True),
             Knob("size", "SIZE"),
             Knob("predelay", "PREDELAY"),
             Trim("mod", "MOD")]),
    ]),

    # The character of the repayments. The MODE switch's lit label shows which
    # algorithm is actually running once the gate has had its say; MIX is the
    # split between principal (dry) and interest (wet) at the MIX outputs.
    Section("SCHEDULE", rows=[
        Row([Knob("tilt", "TILT"),
             Switch("mode", "TRONIC", light="tronic_led"),
             Knob("mix", "MIX"),
             Bezel("freeze", "FREEZE", ink="PAPER")]),
    ]),

    # What CV may take off each control on the way in. Each trimpot sits
    # directly over its own jack and they share one label, set in the gap
    # between the two so it reads as belonging to both. The two gates share the
    # row without owning anything below them, so they keep their labels above
    # their heads, where a patch cable cannot cover them.
    Section("PAYMENTS", rows=[
        Row([Trim("fb_cv", "FDBK"),
             Trim("tilt_cv", "TILT"),
             Trim("mix_cv", "MIX"),
             Trim("size_cv", "SIZE"),
             Jack("mode_in", "MODE GATE"),
             Jack("freeze_in", "FREEZE")], pair=True),
        Row([Jack("fb_in"), Jack("tilt_in"),
             Jack("mix_in"), Jack("size_in")], silent=True),
    ]),
]

# The audio row sits as low as the bottom screws allow: RACK_GRID_HEIGHT -
# RACK_GRID_WIDTH puts their top edge at 123.61 mm, so a jack collar centred
# below 118.6 would run under one. Six jacks: stereo in, the MIX blend and the
# wet-only VERB out, both of which the original had -- in mono.
P.footer = [
    Row([Jack("in_l", "IN L"), Jack("in_r", "IN R"),
         Jack("mix_l", "MIX L", ink="MINT"),
         Jack("mix_r", "MIX R", ink="MINT"),
         Jack("verb_l", "VERB L", ink="MINT"),
         Jack("verb_r", "VERB R", ink="MINT")], y=118.6),
]

if __name__ == "__main__":
    raise SystemExit(build(P, root=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))))

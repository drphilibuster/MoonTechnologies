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
    hp=14,
    density="compact",
    glass=Glass(h=9.2),
)

C6 = P.cols(6, 8.0)       # 8.00 .. 63.12: the CV pairs, the gates, the audio row
C4 = P.cols(4, 10.0)      # 10.00, 27.04, 44.08, 61.12

P.sections = [
    # How long the debt runs. FEEDBACK is the control you reach for, so it wears
    # the ring; the LIMIT light beside the caption is the loop limiter working.
    # MOD is the depth of the tank modulation -- a trimmer, as on a service
    # panel, because it is set once and left.
    Section("TERM", caption_light="limit", rows=[
        Row([BigKnob("feedback", 13.0, "FEEDBACK", primary=True),
             Knob("size", 31.0, "SIZE"),
             Knob("predelay", 47.0, "PREDELAY"),
             Trim("mod", 62.5, "MOD")]),
    ]),

    # The character of the repayments. The MODE switch's lit label shows which
    # algorithm is actually running once the gate has had its say; MIX is the
    # split between principal (dry) and interest (wet) at the MIX outputs.
    Section("SCHEDULE", rows=[
        Row([Knob("tilt", C4[0], "TILT"),
             Switch("mode", C4[1], "TRONIC", light="tronic_led"),
             Knob("mix", C4[2], "MIX"),
             Bezel("freeze", C4[3], "FREEZE", ink="PAPER")]),
    ]),

    # What CV may take off each control on the way in. Each trimpot sits
    # directly over its own jack and they share one label -- the paired idiom.
    # Below the rule, the two gates.
    # The two gates sit beside the pairs, so the block is two rows, not three.
    Section("PAYMENTS", rows=[
        Row([Trim("fb_cv", C6[0], "FDBK"),
             Trim("tilt_cv", C6[1], "TILT"),
             Trim("mix_cv", C6[2], "MIX"),
             Trim("size_cv", C6[3], "SIZE"),
             Jack("mode_in", C6[4], "MODE GATE"),
             Jack("freeze_in", C6[5], "FREEZE")], label_side="above"),
        Row([Jack("fb_in", C6[0]), Jack("tilt_in", C6[1]),
             Jack("mix_in", C6[2]), Jack("size_in", C6[3])], silent=True),
    ]),
]

# The audio row sits as low as the bottom screws allow: RACK_GRID_HEIGHT -
# RACK_GRID_WIDTH puts their top edge at 123.61 mm, so a jack collar centred
# below 118.6 would run under one. Six jacks: stereo in, the MIX blend and the
# wet-only VERB out, both of which the original had -- in mono.
P.footer = [
    Row([Jack("in_l", C6[0], "IN L"), Jack("in_r", C6[1], "IN R"),
         Jack("mix_l", C6[2], "MIX L", ink="MINT"),
         Jack("mix_r", C6[3], "MIX R", ink="MINT"),
         Jack("verb_l", C6[4], "VERB L", ink="MINT"),
         Jack("verb_r", C6[5], "VERB R", ink="MINT")], y=118.6),
]

if __name__ == "__main__":
    raise SystemExit(build(P, root=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))))

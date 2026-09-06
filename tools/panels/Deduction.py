#!/usr/bin/env python3
"""The Deduction panel, declared once.

Running this file regenerates the artwork, the shared hardware, src/PanelTheme.hpp
and the previews. See ../../panelkit/README.md for the pipeline.

Deduction is Schedule A: the itemised deductions. Six ways of taking something
off a signal -- the PAiA 2720-3L, the Escobedo Q&D, the Korg35, the MS-20 OTA,
EFM's Moog-type high-pass and Synthrotek's DIRT -- one model at a time, chosen
by the MODEL knob or its CV. DEDUCTIONS holds the filter itself: which model,
where the cutoff sits, how much comes back round, and how hard the stage is
pushed. WITHHOLDING is what the CV inputs are allowed to take off each of
those, each trimpot over its own jack.

The read-out under the masthead names the model and shows the cutoff. The
light beside DRIVE is the stage reporting that it is clipping.
"""

import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
from panelkit import *   # noqa: E402

P = Panel(
    slug="Deduction",
    title="DEDUCTION",
    form="SCHEDULE A",
    density="regular",
    glass=Glass(h=9.2),
)

P.sections = [
    # The filter. CUTOFF is the one control you reach for, so it wears the seal;
    # MODEL is the six-position selector beside it, and wears the detent ring
    # that says so. The two big knobs take the outer columns of the three the
    # row below stands in, so the block reads as one grid rather than two.
    Section("DEDUCTIONS", rows=[
        Row([BigKnob("freq", "CUTOFF", primary=True, col=0),
             BigKnob("model", "MODEL", steps=6, col=2)]),
        Row([Knob("res", "RES"),
             # DRIVE means something different in every model -- input drive,
             # bias, clip depth -- and the light says when it is doing it.
             Knob("drive", "DRIVE", light="sat"),
             Switch("response", "NRM/INV")]),
    ]),

    # What the CV inputs may take off each control. Each trimpot sits directly
    # over its own jack, with the label they share set between the two. CUTOFF
    # is 1 V/oct at +100%; the rest are attenuverters over 10 V.
    Section("WITHHOLDING", rows=[
        Row([Trim("cv_amt", "CUTOFF"),
             Trim("res_cv", "RES"),
             Trim("drive_cv", "DRIVE"),
             Trim("model_cv", "MODEL")], pair=True),
        Row([Jack("cv_in"), Jack("res_in"),
             Jack("drive_in"), Jack("model_in")], silent=True),
    ]),
]

# The audio row sits as low as the bottom screws allow. LP IN and HP IN are the
# Korg35 / MS-20 convention: a signal at LP IN comes out low-passed, at HP IN
# high-passed, and the two are summed at OUT.
P.footer = [
    Row([Jack("lp_in", "LP IN"), Jack("hp_in", "HP IN"),
         Jack("out", "OUT", ink="MINT")], y=118.6),
]

if __name__ == "__main__":
    raise SystemExit(build(P, root=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))))

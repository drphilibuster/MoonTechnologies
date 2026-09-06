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
    hp=10,
    density="regular",
    glass=Glass(h=9.2),
)

C4 = P.cols(4, 7.0)       # 7.00, 19.27, 31.53, 43.80
C3 = P.cols(3, 10.0)      # 10.00, 25.40, 40.80
C2 = P.cols(2, 14.0)      # 14.00, 36.80

P.sections = [
    # The filter. CUTOFF is the one control you reach for, so it wears the seal;
    # MODEL is the six-position selector beside it.
    Section("DEDUCTIONS", rows=[
        Row([BigKnob("freq", C2[0], "CUTOFF", primary=True),
             BigKnob("model", C2[1], "MODEL")]),
        Row([Knob("res", C3[0], "RES"),
             # DRIVE means something different in every model -- input drive,
             # bias, clip depth -- and the light says when it is doing it.
             Knob("drive", C3[1], "DRIVE", light="sat"),
             Switch("response", C3[2], "NRM/INV")]),
    ]),

    # What the CV inputs may take off each control. Each trimpot sits directly
    # over its own jack and they share one label -- the paired idiom. CUTOFF is
    # 1 V/oct at +100%; the rest are attenuverters over 10 V.
    Section("WITHHOLDING", rows=[
        Row([Trim("cv_amt", C4[0], "CUTOFF"),
             Trim("res_cv", C4[1], "RES"),
             Trim("drive_cv", C4[2], "DRIVE"),
             Trim("model_cv", C4[3], "MODEL")], label_side="above"),
        Row([Jack("cv_in", C4[0]), Jack("res_in", C4[1]),
             Jack("drive_in", C4[2]), Jack("model_in", C4[3])], silent=True),
    ]),
]

# The audio row sits as low as the bottom screws allow. LP IN and HP IN are the
# Korg35 / MS-20 convention: a signal at LP IN comes out low-passed, at HP IN
# high-passed, and the two are summed at OUT.
P.footer = [
    Row([Jack("lp_in", C3[0], "LP IN"), Jack("hp_in", C3[1], "HP IN"),
         Jack("out", C3[2], "OUT", ink="MINT")], y=118.6),
]

if __name__ == "__main__":
    raise SystemExit(build(P, root=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))))

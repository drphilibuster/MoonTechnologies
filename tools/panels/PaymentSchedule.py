#!/usr/bin/env python3
"""The Payment Schedule panel, declared once.

Running this file regenerates the artwork, the shared hardware, src/PanelTheme.hpp
and the previews. See ../../panelkit/README.md for the pipeline.

Payment Schedule is FORM 1040-V -- the payment voucher that rides along with a
return, one line per instalment. It is a drum sequencer: eight tracks of up to 64
steps, each track its own length and clock rate, each step its own velocity,
chance, ratchet, micro-timing and a CV, in sixteen pattern memories. (It used to be
the Modular in a Week Baby8 / 4031 / varimode board; that is retired, see the
manual.)

The face is the screen. Its top row picks which of a step's values the grid shows
and edits -- GATE, VEL, CHNC, RTCH, OFFS, CV -- and holds RUN and RST; the second row
is the pattern, the selected track with its length and rate, which sixteen of the 64
steps are on screen, and the swing. Everything below is the grid, drawn and edited by
the module: click to set a hit, drag across to paint, drag a bar for a value.

Under the glass, each track's trigger and its velocity, a column a track, so a
drum voice takes its pair from one place; then the transport in and the buses out.
"""

import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
from panelkit import *   # noqa: E402

LANES = ["gate", "vel", "chnc", "rtch", "offs", "cv"]

# The glass is a 12 x 24 grid. Row 0: the lane keys, RUN and RST. Row 1: the pattern,
# the selected track's settings, the page and the swing. Rows 2-11: the grid.
_fields = ([Field("ln_" + n, cell=(0, 3 * i), span=(1, 3), kind="button") for i, n in enumerate(LANES)]
           + [Field("run", cell=(0, 18), span=(1, 3), kind="button"),
              Field("rst", cell=(0, 21), span=(1, 3), kind="button")]
           + [Field(n, cell=(1, 4 * i), span=(1, 4), kind=k) for i, (n, k) in enumerate(
               [("patt", "value"), ("trk", "select"), ("len", "value"),
                ("div", "select"), ("page", "select"), ("swng", "value")])])

P = Panel(
    slug="PaymentSchedule",
    title="PAYMENT SCHEDULE",
    what="DRUM SEQUENCER",
    subtitle="INSTALMENT VOUCHER",
    form="FORM 1040-V",
    density="compact",
    glass=Glass(h=66.0, grid=(12, 24), fields=_fields),
)

N = 8   # tracks

P.sections = [
    Section("TRACKS", rows=[
        Row([Jack("trig%d" % (t + 1), str(t + 1), ink="MINT") for t in range(N)],
            span=[(0, N - 1, "TRIG", 6.2, "MINT")]),
        Row([Jack("vel%d" % (t + 1), "", ink="MINT") for t in range(N)],
            span=[(0, N - 1, "VEL", 6.2, "MINT")]),
    ]),
]

# Transport in; then every track at once -- the triggers and the CV lane as 8-channel
# cables, channel n track n -- and the end of track 1's loop.
P.footer = [
    Row([Jack("clock_in", "CLOCK"),
         Jack("reset_in", "RESET"),
         Jack("run_in", "RUN"),
         Jack("patt_in", "PATT"),
         Jack("all_out", "TRIGS", ink="MINT"),
         Jack("cv_out", "CV", ink="MINT"),
         Jack("eoc_out", "EOC", ink="MINT")], y=118.6),
]

if __name__ == "__main__":
    raise SystemExit(build(P, root=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))))

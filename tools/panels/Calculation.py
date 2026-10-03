#!/usr/bin/env python3
"""The Calculation panel, declared once.

Running this file regenerates the artwork, the shared hardware, src/PanelTheme.hpp
and the previews. See ../../panelkit/README.md for the pipeline.

Calculation is a phrase counter: one base length N, set at the top, and six
rows that each fall due at N times their own multiplier -- or a division of it.
It does the work of a chain of Count Modula Event Timers in one module, and
because every row counts from the same downbeat, nothing needs to be set to N-1
to land on the beat.

The glass is the worksheet, and everything on it is a control. The top line is
N -- hold it and drag, or right-click it to type a number -- with what the
phrase is doing beside it. Under that, START / STOP / RESET, each a word you
click. Then a table of the six lines: each line's ratio of N (click it to pick
one, or drag through them) and how many clocks until it next falls due.
Beneath the glass each line keeps only what is patched: its ratio CV, its TRIG
with the light that fires with it, and its GATE, lines 1 to 6 top to bottom.
CLOCK and the transport inputs sit on the footer, where the family keeps what
is shared; RUN, the one shared output, under the last GATE.
"""

import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
from panelkit import *   # noqa: E402

P = Panel(
    slug="Calculation",
    title="CALCULATION",
    what="PHRASE COUNTER",
    form="WORKSHEET",
    density="compact",
    # N and the state on the first line, the transport on the second, then the
    # table of the six lines, three rows of two -- lines 1-3 down the left, 4-6
    # down the right -- each its ratio (a field) and the clocks until it falls
    # due (drawn, not a control). Two abreast because six lines one under the
    # other made a glass the face could only pay for at 12 HP; this way it is 9.
    glass=Glass(h=24.4, grid=(5, 12), fields=[
        Field("n",         cell=(0, 0), span=(1, 7), kind="value"),
        Field("start_btn", cell=(1, 0), span=(1, 4), kind="button"),
        Field("stop_btn",  cell=(1, 4), span=(1, 4), kind="button"),
        Field("reset_btn", cell=(1, 8), span=(1, 4), kind="button"),
    ] + [Field("mult%d" % n, cell=(2 + (n - 1) % 3, 0 if n <= 3 else 6),
               span=(1, 3), kind="select")
         for n in range(1, 7)]),
)


def line(n):
    # Only the first line is labelled; the five below it read from it, the way
    # a worksheet's column heads are printed once. The ratio and the count are
    # on the glass, in the table's row for this line.
    # The light that fires with TRIG hangs between TRIG and GATE rather than
    # on TRIG's label, because lines 2-6 have no label to hang it on.
    return Row([Jack("cv%d" % n, "CV", col=0),
                Jack("trig%d" % n, "TRIG", ink="MINT", col=1),
                Light("fire%d_led" % n, between=(1, 2)),
                Jack("gate%d" % n, "GATE", ink="MINT", col=2)], silent=n > 1)


P.sections = [
    # Six lines, each a multiple (or a division) of the base. RUN stands under the last GATE rather than on the footer: a fifth jack on
    # the band made the band, not the lines, what set the width (12 HP against
    # 9). It is an output, so it keeps the outputs' column.
    Section("LINES", rows=[line(n) for n in range(1, 7)]
            + [Row([Jack("run", "RUN", ink="MINT", col=2, side="left")])]),
]

P.footer = [
    Row([Jack("clock", "CLOCK"),
         Jack("start", "START"),
         Jack("stop", "STOP"),
         Jack("reset", "RESET")], y=118.6),
]

if __name__ == "__main__":
    raise SystemExit(build(P, root=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))))

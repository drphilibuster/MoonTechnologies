#!/usr/bin/env python3
"""The Calculation panel, declared once.

Running this file regenerates the artwork, the shared hardware, src/PanelTheme.hpp
and the previews. See ../../panelkit/README.md for the pipeline.

Calculation is a phrase counter: one base length N, set at the top, and six
rows that each fall due at N times their own multiplier -- or a division of it.
It does the work of a chain of Count Modula Event Timers in one module, and
because every row counts from the same downbeat, nothing needs to be set to N-1
to land on the beat.

The glass shows N (click it to type one). Beneath it, the controls that set N
and the transport buttons. Each row reads across: the multiplier with its plate
naming the ratio, the multiplier's CV, the row's own counter, its TRIG with the
light that fires with it, and its GATE. The transport jacks sit on the footer,
where the family keeps what is shared.
"""

import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
from panelkit import *   # noqa: E402

P = Panel(
    slug="Calculation",
    title="CALCULATION",
    form="WORKSHEET",
    density="compact",
    glass=Glass(h=7.4),
)


def line(n):
    # Only the first line is labelled; the five below it read from it, the way
    # a worksheet's column heads are printed once.
    # The ratio's plate stands beside its trim rather than under it: under it,
    # it costs every line a plate's height, and six lines cannot pay that.
    # The light that fires with TRIG hangs between TRIG and GATE rather than
    # on TRIG's label, because lines 2-6 have no label to hang it on.
    return Row([Trim("mult%d" % n, "MULT", col=0, side="above"),
                Readout("mult%d_name" % n, "RATIO", col=1, side="above"),
                Jack("cv%d" % n, "CV", col=2),
                Readout("count%d" % n, "DUE IN", col=3, side="above"),
                Jack("trig%d" % n, "TRIG", ink="MINT", col=4),
                Light("fire%d_led" % n, between=(4, 5)),
                Jack("gate%d" % n, "GATE", ink="MINT", col=5)], silent=n > 1)


P.sections = [
    # The base: N, and the three buttons that run the phrase. A worksheet's
    # first line is the figure every later line is a multiple of.
    Section("BASE", rows=[
        Row([Button("start_btn", "START", light="run_led"),
             Button("stop_btn", "STOP"),
             Button("reset_btn", "RESET"),
             Button("n_dn", "-"),
             Knob("n", "STEPS", primary=True),
             Button("n_up", "+")]),
    ]),

    # Six lines, each a multiple (or a division) of the base.
    Section("LINES", rows=[line(n) for n in range(1, 7)]),
]

P.footer = [
    Row([Jack("clock", "CLOCK"),
         Jack("start", "START"),
         Jack("stop", "STOP"),
         Jack("reset", "RESET"),
         Jack("run", "RUN", ink="MINT")], y=118.6),
]

if __name__ == "__main__":
    raise SystemExit(build(P, root=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))))

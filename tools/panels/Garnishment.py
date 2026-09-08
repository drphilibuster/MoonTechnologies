#!/usr/bin/env python3
"""The Garnishment panel, declared once.

Running this file regenerates the artwork, the shared hardware, src/PanelTheme.hpp
and the previews. See ../../panelkit/README.md for the pipeline.

Garnishment is a dual VCA: two identical channels, each a MODE switch away from
being a different Modular-in-a-Week circuit -- LM13700 OTA, vactrol LED/LDR low-pass
gate, or a 2N5457 JFET amplitude modulator. One set of controls (BIAS, LAG, CV IN,
CV AMOUNT) drives whichever circuit MODE selects, so the panel repeats once per
channel rather than growing a row per topology. CV AMOUNT sits directly over its own
CV IN jack -- the paired idiom -- so the pair reads as one thing wearing two knobs.
"""

import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
from panelkit import *   # noqa: E402

P = Panel(
    slug="Garnishment",
    title="GARNISHMENT",
    form="FORM 668-W",
)


N = 6

P.sections = [
    Section("GARNISHEE", rows=[
        Row(span=[(0, N - 1, "BIAS")],
            items=[Knob("bias%d" % k, "") for k in range(1, N + 1)]),
        Row(span=[(0, N - 1, "MODE")],
            items=[Switch3("mode%d" % k, "") for k in range(1, N + 1)]),
        Row(span=[(0, N - 1, "LAG")],
            items=[Knob("lag%d" % k, "") for k in range(1, N + 1)]),
        Row(span=[(0, N - 1, "CV AMT")],
            items=[Trim("cvamt%d" % k, "") for k in range(1, N + 1)], pair=True),
        Row([Jack("cvin%d" % k) for k in range(1, N + 1)], silent=True),
        Row(span=[(0, N - 1, "IN")],
            items=[Jack("in%d" % k, "") for k in range(1, N + 1)]),
    ]),
]

P.footer = [
    Row([Jack("out%d" % k, str(k), ink="MINT") for k in range(1, N + 1)], y=118.6),
]

if __name__ == "__main__":
    raise SystemExit(build(P, root=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))))

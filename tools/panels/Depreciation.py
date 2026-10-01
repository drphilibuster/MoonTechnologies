#!/usr/bin/env python3
"""The Depreciation panel, declared once.

Running this file regenerates the artwork, the shared hardware, src/PanelTheme.hpp
and the previews. See ../../panelkit/README.md for the pipeline.

Depreciation is a Lexicon PCM 70 digital effects processor -- a 1986 reverb,
chorus, delay and resonant-chord machine -- running its own firmware: every
program, parameter and MIDI patch is Lexicon's own. It is FORM 4562 because
depreciation is value that decays on a schedule, which is also what a reverb
tail is.

Everything the hardware needed menus for is a control here, and everything the
firmware shows is drawn: the 16-digit display, every parameter's own name and
value under its knob, the headroom bar. The knobs ARE the parameter matrix --
row 0 to 4 of the machine's own cell table, whatever program is running -- and
their ranges, names and printed values come from the firmware itself.
"""

import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
from panelkit import *   # noqa: E402

P = Panel(
    slug="Depreciation",
    title="DEPRECIATION",
    subtitle="REVERB",
    form="FORM 4562",
    hp="auto",
    glass=Glass(h=15.5),
)

def cell(r, c):
    return Trim("p%d%d" % (r, c), "", readout="cap%d%d" % (r, c))

def lane(rowi, i):
    n = i + 1
    return [Jack("cv%d" % n, "CV %d" % n), Trim("att%d" % n, "ATT"),
            Button("asg%d" % n, "SET", light="asg_led%d" % n), Readout("lane%d" % n, "")][rowi]

LEFT = [
    [Knob("row", "ROW", steps=8), Knob("col", "COL", steps=10)],
    [Switch("regmode", "PGM  REG"), Button("load", "LOAD", primary=True)],
    [Button("store", "STORE"), Button("bypass", "BYPASS", light="bypass_led")],
    [Knob("input", "INPUT"), Knob("trim", "VOLT TRIM")],
    [Switch("in_pad", "IN +4 -20"), Switch("out_pad", "OUT +4 -20")],
]
DED = [Jack("mod", "MOD"), Jack("at", "AT"), Jack("note", "NOTE"), Jack("gate", "GATE"),
       Jack("sustain", "SUST"), Jack("soft", "SOFT"), Jack("clock", "CLOCK"), Jack("run", "RUN")]

rows = []
for r in range(5):
    items = list(LEFT[r]) + [cell(r, c) for c in range(9)]
    items += [lane(r, i) for i in range(8)] if r < 4 else DED
    rows.append(Row(items))

P.sections = [Section("PARAMETERS AND CONTROL", rows=rows)]

P.footer = [
    Row([Jack("in", "IN"), Jack("in_r", "IN R"), Jack("pgm", "PGM"), Jack("bypass_cv", "BYP"),
         Trim("clk_div", "CLK /", steps=5, side="above"),
         Jack("out_l", "OUT L", ink="MINT"), Jack("out_r", "OUT R", ink="MINT"),
         Jack("wet_l", "WET L", ink="MINT"), Jack("wet_r", "WET R", ink="MINT")], y=118.6),
]

if __name__ == "__main__":
    raise SystemExit(build(P, root=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))))

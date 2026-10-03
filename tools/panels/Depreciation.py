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

Presets are one detented selector and one switch: PRESET picks a slot, and
FACTORY | USER says whether that slot is one of the machine's own programs
(effects) or one of your 50 stored registers. The well names what the selector
points at before LOAD is pressed. There are no CV lanes: they cost the machine
more than they gave.
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

LEFT = [
    [Stepper("slot", "PRESET"), Switch("regmode", "FACTORY  USER")],
    [Button("load", "LOAD", primary=True), Button("store", "STORE")],
    [Button("bypass", "BYPASS", light="bypass_led"), Knob("input", "INPUT")],
    [Knob("trim", "VOLT TRIM"), Switch("in_pad", "IN +4 -20")],
    [Switch("out_pad", "OUT +4 -20"), Trim("clk_div", "CLK /", steps=5)],
]
# the dedicated inputs: a column of pairs down the right edge, and the make-up gain under them
def jk(name, label):
    return Jack(name, label, side="right")

DED = [
    [jk("mod", "MOD"), jk("at", "AT")],
    [jk("note", "NOTE"), jk("gate", "GATE")],
    [jk("sustain", "SUST"), jk("soft", "SOFT")],
    [jk("clock", "CLOCK"), jk("run", "RUN")],
    [Knob("out_level", "OUT LEVEL")],
]

rows = []
for r in range(5):
    rows.append(Row(list(LEFT[r]) + [cell(r, c) for c in range(9)] + DED[r]))

P.sections = [Section("PARAMETERS AND CONTROL", rows=rows)]

P.footer = [
    Row([Jack("in", "IN"), Jack("in_r", "IN R"), Jack("pgm", "PGM"), Jack("bypass_cv", "BYP"),
         Jack("out_l", "OUT L", ink="MINT"), Jack("out_r", "OUT R", ink="MINT"),
         Jack("wet_l", "WET L", ink="MINT"), Jack("wet_r", "WET R", ink="MINT")], y=118.6),
]

if __name__ == "__main__":
    raise SystemExit(build(P, root=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))))

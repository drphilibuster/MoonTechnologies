#!/usr/bin/env python3
"""The Payment Schedule panel, declared once.

Running this file regenerates the artwork, the shared hardware, src/PanelTheme.hpp
and the previews. See ../../panelkit/README.md for the pipeline.

Payment Schedule is FORM 1040-V -- the payment voucher that rides along with a
return, one line per instalment. It is two MiaW circuits fused into one
counter: a Baby8 (4017 decade counter) gives eight CV/gate steps; the same
counter, read the other way, is a sequential switch (SWITCH IN routes to
whichever GATE OUT the count has landed on, and that step's CV IN routes out
CV OUT instead -- which is why CV OUT doubles as the sequencer's own V/oct
output). A 4031 tap looper shares the same eight slots rather than a buffer of
its own. A varimode quantizer sits on the CV path before it leaves the module.

The panel's height is fixed regardless of HP, so a per-step grid this size goes sideways:
STEPS is eight columns four rows deep -- what comes in on top, what goes out the bottom, the
knob and the gate switch riding between them, the GATE OUT row boxed so it reads as a bank of
outputs rather than half of one grid of sixteen jacks. That grid is what sets the panel's
width, and nothing else is allowed to be wider than it: the controls that are not per-step
sit in two rows beneath it. DIR CV stands beside the DIR switch and TAP IN beside the TAP
button, the two jacks that belong to a control on those rows; every other jack -- CLOCK,
RESET, the switch's common pair, the quantizer's TRIG, the cycle counter's EOC, RUN, CYCLE
and LOOP GATE -- rides in the footer with the module's other primary I/O, inputs first and
the mint-ringed outputs after them. (They used to be a row of jacks of their own inside
OPERATIONS, which left the controls above them one row of fourteen: 33 HP for a grid that
needs 22. Now 23.)
"""

import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
from panelkit import *   # noqa: E402

P = Panel(
    slug="PaymentSchedule",
    title="PAYMENT SCHEDULE",
    subtitle="INSTALMENT VOUCHER",
    form="FORM 1040-V",
    density="compact",
)

N = 8   # steps

# --- per-step grid: CV IN on top, the knob, the gate switch, GATE OUT on the
# bottom. The output row is boxed as a run, on top of its own mint jacks, so
# it reads as a bank of outputs rather than as half of one grid of sixteen
# jacks; the input row keeps a plain shared label -- a box on the very first
# row of the section would sit hard against the section's own caption.
cv_in = [Jack("b_in%d" % (i + 1)) for i in range(N)]
step = [Knob("step%d" % (i + 1), str(i + 1)) for i in range(N)]
gate = [Bezel("gate%d" % (i + 1)) for i in range(N)]
gate_out = [Jack("b_out%d" % (i + 1), ink="MINT") for i in range(N)]

# --- everything that isn't per-step: transport, the tap looper and the quantizer, in two rows
# of controls -- label below, the hand-clearance rule. The first is the counter and the
# quantizer's pitch side (which way it counts, how far, which scale and root); the second is
# levels and the transport. Big and small widgets alternate along each row so no two
# full-width knobs land shoulder to shoulder. RUN carries the panel's one primary ring --
# whether the counter moves at all matters more than any one step's value.
row1 = [
    Light("up_lit", "UP", ink="LIME"),
    Knob("steps", "STEPS", steps=8),
    Switch("dir", "DIR"),
    Jack("dir_cv_in", "DIR CV", side="below"),
    Knob("scale", "SCALE", steps=5),
    Light("dn_lit", "DN", ink="LIME"),
    Knob("root", "ROOT", steps=12),
    Switch("quant", "QUANT"),
    Switch("cv_range", "RANGE"),
]
row2 = [
    Knob("atten", "ATTEN"),
    Knob("gate_len", "GATE LEN"),
    Bezel("tap", "TAP"),
    Jack("tap_gate_in", "TAP IN", side="below"),
    Bezel("record", "REC"),
    Bezel("clear", "CLR"),
    Bezel("run", "RUN", primary=True),
]

P.sections = [
    Section("STEPS", rows=[
        Row(cv_in, shared="CV IN"),
        Row(step),
        Row(gate, silent=True),
        Row(gate_out, span=[(0, N - 1, "GATE OUT", 6.2, "MINT")]),
    ], divide_after=(1,)),

    Section("OPERATIONS", rows=[
        Row(row1, own_grid=True),
        Row(row2, own_grid=True),
    ]),
]

# Every jack that is not per-step. Inputs, then outputs (no gutter: the mint rings already
# say which). SWITCH IN normals to 10 V, and CV OUT doubles as the quantized sequencer
# output, so those two wires are what the whole module turns on; LOOP GATE is the tap
# loop's own read-out (10 V while the current slot holds a hit).
P.footer = [
    Row([Jack("clock_in", "CLOCK", light="clock_lit"),
         Jack("reset_in", "RESET"),
         Jack("a_in", "SWITCH IN"),
         Jack("run_in", "RUN"),
         Jack("cycle_in", "CYCLE"),
         Jack("a_out", "CV OUT", ink="MINT"),
         Jack("trig_out", "TRIG", ink="MINT"),
         Jack("eoc_out", "EOC", ink="MINT"),
         Jack("loop_gate_out", "LOOP GATE", ink="MINT")], y=118.6),
]
if __name__ == "__main__":
    raise SystemExit(build(P, root=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))))

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

This is one of the widest panels in the family, and every millimetre of it goes
on columns rather than rows -- the panel height is fixed regardless of HP, so
the only way to fit a per-step grid this size is sideways. STEPS is eight
columns four rows deep: what comes in on top, what goes out the bottom, the
knob and the gate switch riding between them, the GATE OUT row boxed so it
reads as a bank of outputs rather than half of one grid of sixteen jacks.
Everything else that isn't per-step lives in one wide row of its own; CLOCK,
RESET, the quantizer's TRIG and the cycle counter's EOC ride in the footer with
the module's other primary I/O, since none of the four is specific to any one
of the counter's three jobs.
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

# --- everything that isn't per-step: transport, the tap looper and the
# quantizer, fused into one block. The panel's row budget is fixed by its
# height, not by its HP, so this has to be wide rows rather than many of
# them -- one row of knobs/switches/bezels (label below, the hand-clearance
# rule) and one row of jacks (label above, the cable-clearance rule); mixing
# the two label sides on one row is what the layout solver gets wrong, so
# each row here is one side only. Big and small widgets alternate along the
# row so no two full-width knobs land shoulder to shoulder. RUN carries the
# panel's one primary ring -- whether the counter moves at all matters more
# than any one step's value.
row1 = [
    Light("up_lit", "UP", ink="LIME"),
    Knob("steps", "STEPS", steps=8),
    Switch("dir", "DIR"),
    Knob("scale", "SCALE", steps=5),
    Light("dn_lit", "DN", ink="LIME"),
    Knob("root", "ROOT", steps=12),
    Switch("quant", "QUANT"),
    Switch("cv_range", "RANGE"),
    Knob("atten", "ATTEN"),
    Bezel("tap", "TAP"),
    Bezel("record", "REC"),
    Bezel("clear", "CLR"),
    Bezel("run", "RUN", primary=True),
]

# Inputs first, then the one output this row still carries -- LOOP GATE is the
# tap loop's own read-out (10 V while the current slot holds a hit), boxed
# like the per-step outputs above since it isn't in the footer.
row2 = [
    Jack("dir_cv_in", "DIR CV"),
    Jack("tap_gate_in", "TAP IN"),
    Jack("run_in", "RUN"),
    Jack("cycle_in", "CYCLE"),
    Jack("loop_gate_out", "", ink="MINT"),
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
        Row(row2, own_grid=True, span=[(4, 4, "LOOP GATE", 6.2, "MINT")]),
    ]),
]

# CLOCK, RESET, the quantizer's TRIG and the counter's own EOC, alongside the
# switch's common pair -- SWITCH IN normals to 10 V, and CV OUT doubles as the
# quantized sequencer output, so the two wires the whole module turns on.
P.footer = [
    Row([Jack("clock_in", "CLOCK", light="clock_lit"),
         Jack("reset_in", "RESET"),
         Jack("a_in", "SWITCH IN"),
         Jack("a_out", "CV OUT", ink="MINT"),
         Jack("trig_out", "TRIG", ink="MINT"),
         Jack("eoc_out", "EOC", ink="MINT")], y=118.6),
]

if __name__ == "__main__":
    raise SystemExit(build(P, root=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))))

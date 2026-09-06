#!/usr/bin/env python3
"""The Payment Schedule panel, declared once.

Running this file regenerates the artwork, the shared hardware, src/PanelTheme.hpp
and the previews. See ../../panelkit/README.md for the pipeline.

Payment Schedule is FORM 1040-V -- the payment voucher that rides along with a
return, one line per instalment. It is three MiaW circuits fused into one
counter: a Baby8 (4017 decade counter) gives eight CV/gate steps; the same
counter, read the other way, is a sequential switch (A IN routes to whichever
B OUT the count has landed on, and that step's B IN routes back out A OUT --
which is why A OUT doubles as the sequencer's own CV output); a 4031 shift
register bolted on the side is the tap looper, a second, longer gate track
clocked from the same source. A varimode quantizer sits on the CV path before
it leaves the module.

Twenty-eight HP is the whole width the family allows, and every millimetre of
it goes on columns rather than rows -- the panel height is fixed regardless of
HP, so the only way to fit a per-step grid this size is sideways. STEPS is
eight columns four rows deep: what comes in on top, what goes out the bottom,
the knob and the gate switch riding between them. Everything else that isn't
per-step lives in one wide row of its own.
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

# --- per-step grid: B IN on top, the knob, the gate switch, B OUT on the
# bottom. One shared label serves each jack row; the knob row carries the
# step numbers themselves, so the two silent rows in between read from them.
b_in = [Jack("b_in%d" % (i + 1)) for i in range(N)]
step = [Knob("step%d" % (i + 1), str(i + 1)) for i in range(N)]
gate = [Bezel("gate%d" % (i + 1)) for i in range(N)]
b_out = [Jack("b_out%d" % (i + 1), ink="MINT") for i in range(N)]

# --- everything that isn't per-step: transport, the tap looper and the
# quantizer, fused into one block. The panel's row budget is fixed by its
# height, not by its HP, so this has to be wide rows rather than many of
# them -- one row of knobs/switches/bezels (label below, the hand-clearance
# rule) and one row of jacks (label above, the cable-clearance rule); mixing
# the two label sides on one row is what the layout solver gets wrong, so
# each row here is one side only. Big and small widgets alternate along the
# row so no two full-width knobs land shoulder to shoulder.
row1 = [
    Light("up_lit", "UP", ink="LIME"),
    Knob("steps", "STEPS", steps=8),
    Switch("dir", "DIR"),
    Knob("scale", "SCALE", steps=5),
    Light("dn_lit", "DN", ink="LIME"),
    Knob("root", "ROOT", steps=12),
    Switch("quant", "QUANT"),
    Knob("length", "LENGTH", steps=3),
    Switch3("cv_range", "RANGE"),
    Bezel("tap", "TAP", primary=True),
    Bezel("record", "REC"),
    Bezel("clear", "CLR"),
]

row3 = [
    Jack("clock_in", "CLOCK", light="clock_lit"),
    Jack("reset_in", "RESET"),
    Jack("gate_en_in", "ENABLE"),
    Jack("dir_cv_in", "DIR CV"),
    Jack("chain_in", "CH IN"),
    Jack("chain_out", "CH OUT", ink="MINT"),
    Jack("tap_gate_in", "TAP IN"),
    Jack("loop_gate_out", "LOOP GT", ink="MINT"),
    Jack("trig_out", "TRIG", ink="MINT"),
]

P.sections = [
    Section("STEPS", rows=[
        Row(b_in, shared="B IN"),
        Row(step),
        Row(gate, silent=True),
        Row(b_out, shared="B OUT", shared_ink="MINT"),
    ], divide_after=(1,)),

    # Transport, the 4031 tap looper and the varimode quantizer: everything
    # that isn't per-step, in one block -- the controls above the jacks that
    # feed and read them. Twelve controls over nine jacks, so the two rows keep
    # their own grids rather than pretending to a column each.
    Section("OPERATIONS", rows=[
        Row(row1, own_grid=True),
        Row(row3, own_grid=True),
    ]),
]

# A IN normals to 10 V and A OUT doubles as the CV out -- the sequential
# switch and the quantized sequencer output are the same two wires.
P.footer = [
    Row([Jack("a_in", "A IN"),
         Jack("a_out", "A OUT", ink="MINT")], y=118.6),
]

if __name__ == "__main__":
    raise SystemExit(build(P, root=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))))

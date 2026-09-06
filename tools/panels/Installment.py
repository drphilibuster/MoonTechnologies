#!/usr/bin/env python3
"""The Installment panel, declared once.

Running this file regenerates the artwork, the shared hardware, src/PanelTheme.hpp
and the previews. See ../../panelkit/README.md for the pipeline.

Installment is the dual function generator: two identical circuits, one per
column, each a Modular in a Week day filed onto one FORM 9465 -- an agreement
to pay off a balance over TERMS you set yourself. MODE picks which day it is:
the Simple LFO's triangle core (an OTA integrator with a shape-to-sine option),
Niklas Roennberg's diode-steered RC dual AR, or PHObos's AD, and ATTACK/RELEASE
are always the same two knobs underneath -- rise time and fall time of the
core, whichever core MODE selects. LOOP makes an AR retrigger itself and an AD
free-run as an LFO of its own. AUTOPAY is what a CV is allowed to add to that
pair, RANGE picks fast or slow, and BIAS gives it a manual nudge on the same
axis. MINIMUM DUE is the Day 12 tape-motor PWM driver, comparing channel one's
core against a threshold, so it always has a carrier even when channel one is
folded into an envelope rather than an LFO.

16 HP, compact density: two four-wide rows above a two-wide AUTOPAY block reads
easier as two debtors side by side than as one wide row of eight.
"""

import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
from panelkit import *   # noqa: E402

P = Panel(
    slug="Installment",
    title="INSTALLMENT",
    form="FORM 9465",
    density="compact",
)

P.sections = [
    # The shape of each channel's core: which day it is, whether it loops, the
    # two times that set both its rate/skew (LFO) and its envelope (AR/AD),
    # the range, and the manual nudge BIAS gives the same axis AUTOPAY reaches.
    Section("PAYMENT PLAN", rows=[
        Row([Switch3("mode1", "MODE"), Bezel("loop1", "LOOP"),
             Switch3("mode2", "MODE"), Bezel("loop2", "LOOP")]),
        Row([Knob("attack1", "ATTACK"), Knob("release1", "RELEASE"),
             Knob("attack2", "ATTACK"), Knob("release2", "RELEASE")]),
        Row([Switch("range1", "RANGE"), Knob("bias1", "BIAS"),
             Switch("range2", "RANGE"), Knob("bias2", "BIAS")]),
    ], divide_after=(0,)),

    # What each channel's own CV is allowed to add to ATTACK/RELEASE, and the
    # gate that drives it (a reset in LFO mode) -- the trim over its own jack,
    # the label they share set between them. The Day 12 tape-motor PWM driver
    # rides in the two middle columns: a duty knob and its own CV, compared
    # against channel one's core downstream in the footer.
    Section("AUTOPAY", rows=[
        Row([Trim("cv1_amt", "CV AMT", col=0),
             Trim("cv2_amt", "CV AMT", col=3)], pair=True),
        Row([Jack("cv1_in", col=0), Jack("cv2_in", col=3)], silent=True),
        Row([Jack("gate1_in", "GATE", col=0),
             Knob("pwm_duty", "DUTY", col=1),
             Jack("pwm_cv_in", "CV", col=2),
             Jack("gate2_in", "GATE", col=3)]),
    ]),
]

# Everything that leaves the module: both channels' outputs, then the PWM's.
P.footer = [
    Row([Jack("env1_out", "ENV1", ink="MINT"),
         Jack("inv1_out", "SQU1", ink="MINT"),
         Jack("eoc1_out", "EOC1", ink="MINT"),
         Jack("env2_out", "ENV2", ink="MINT"),
         Jack("inv2_out", "SQU2", ink="MINT"),
         Jack("eoc2_out", "EOC2", ink="MINT"),
         Jack("pwm_out", "PWM", ink="MINT")], y=118.6),
]

if __name__ == "__main__":
    raise SystemExit(build(P, root=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))))

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
    hp=16,
    density="compact",
)

C4 = P.cols(4, 9.0)      # ch1 pair, ch2 pair; knob wells clear the frame
# Each channel's AUTOPAY column sits under the midpoint of its own ATTACK/
# RELEASE pair above, so the trim, the jack and the gate line up with the pair
# they belong to rather than with an unrelated grid.
CH = [(C4[0] + C4[1]) / 2, (C4[2] + C4[3]) / 2]        # 18.88, 62.40
PW = [P.w / 2 - 9.0, P.w / 2 + 9.0]                    # 31.64, 49.64

P.sections = [
    # The shape of each channel's core: which day it is, whether it loops, the
    # two times that set both its rate/skew (LFO) and its envelope (AR/AD),
    # the range, and the manual nudge BIAS gives the same axis AUTOPAY reaches.
    Section("PAYMENT PLAN", rows=[
        Row([Switch3("mode1", C4[0], "MODE"), Bezel("loop1", C4[1], "LOOP"),
             Switch3("mode2", C4[2], "MODE"), Bezel("loop2", C4[3], "LOOP")]),
        Row([Knob("attack1", C4[0], "ATTACK"), Knob("release1", C4[1], "RELEASE"),
             Knob("attack2", C4[2], "ATTACK"), Knob("release2", C4[3], "RELEASE")]),
        Row([Switch("range1", C4[0], "RANGE"), Knob("bias1", C4[1], "BIAS"),
             Switch("range2", C4[2], "RANGE"), Knob("bias2", C4[3], "BIAS")]),
    ], divide_after=(0,)),

    # What each channel's own CV is allowed to add to ATTACK/RELEASE, and the
    # gate that drives it (a reset in LFO mode) -- the trim over its own jack
    # is the paired idiom, one label serving both. The Day 12 tape-motor PWM
    # driver rides underneath, a subtotal line below it: a duty knob and its
    # own CV, compared against channel one's core downstream in the footer.
    Section("AUTOPAY", rows=[
        Row([Trim("cv1_amt", CH[0], "CV AMT"), Trim("cv2_amt", CH[1], "CV AMT")],
            label_side="above"),
        Row([Jack("cv1_in", CH[0]), Jack("cv2_in", CH[1])], silent=True),
        Row([Jack("gate1_in", CH[0], "GATE"),
             Knob("pwm_duty", PW[0], "DUTY"), Jack("pwm_cv_in", PW[1], "CV"),
             Jack("gate2_in", CH[1], "GATE")]),
    ]),
]

# Everything that leaves the module: both channels' outputs, then the PWM's.
F = P.cols(7, 6.0)      # 6.00, 17.55, 29.09, 40.64, 52.19, 63.73, 75.28
P.footer = [
    Row([Jack("env1_out", F[0], "ENV1", ink="MINT"),
         Jack("inv1_out", F[1], "SQU1", ink="MINT"),
         Jack("eoc1_out", F[2], "EOC1", ink="MINT"),
         Jack("env2_out", F[3], "ENV2", ink="MINT"),
         Jack("inv2_out", F[4], "SQU2", ink="MINT"),
         Jack("eoc2_out", F[5], "EOC2", ink="MINT"),
         Jack("pwm_out", F[6], "PWM", ink="MINT")], y=118.6),
]

if __name__ == "__main__":
    raise SystemExit(build(P, root=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))))

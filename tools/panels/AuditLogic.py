#!/usr/bin/env python3
"""The Audit Logic panel, declared once.

Running this file regenerates the artwork, the shared hardware, src/PanelTheme.hpp
and the previews. See ../../panelkit/README.md for the pipeline.

Audit Logic is FORM 886-A, the IRS's "Explanation of Items" -- the form an
examiner attaches a finding to. It consolidates three Modular in a Week boards
into one panel: the Quad Inverter / Hex Inverter / Quad Logic Module family
becomes FINDINGS, a bank of four selectable two-input gates; the 4066 Quad
Gated Switch becomes REFERRAL, two gated analogue switches that route a signal
to one of two case files; and the Emiz CV2 clock divider becomes INSTALLMENTS,
a binary counter paying a clock out in six declining fractions. REF V and
CLOCK/RESET/MODE live on the footer band because they are the panel's shared
references, the way a form's filing details sit below the line items.

FINDINGS is polyphonic -- sixteen simultaneous audits. REFERRAL and
INSTALLMENTS stay mono, an analogue switch and a counter having no meaningful
per-channel state to keep.
"""

import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
from panelkit import *   # noqa: E402

P = Panel(
    slug="AuditLogic",
    title="AUDIT LOGIC",
    form="FORM 886-A",
    hp=22,
    density="compact",
)

# Four gate columns, reused for every row that lines up with them -- the A/B
# inputs offset either side, the function knob and verdict jack sharing the
# same row a little further apart -- and reused again for the footer band, so
# the whole panel reads on one grid.
GATE_X = P.cols(4, 16.0)     # the four gate columns
SW8 = P.cols(8, 8.0)         # the two switch channels, side by side
DIV7 = P.cols(7, 9.0)        # the six divider taps and their MODE switch
FOOT2 = P.cols(2, 20.0)      # the footer band: the divider's transport

P.sections = [
    # Four two-input gates, each a selectable function rather than the fixed
    # wiring the original boards hard-etched. The default per gate follows the
    # registered description's own order -- inverter, AND, OR, XOR -- which is
    # as close as the surviving schematics get to naming an assignment; NAND,
    # NOR and XNOR are additional selections, not a claim about the original
    # board. Each gate's light sits between its A and B inputs; VERDICT beside
    # the caption lights if any channel is currently true. REF V is the
    # reference an unpatched input reads, from the quad inverter.
    Section("FINDINGS", caption_light="verdict", rows=[
        Row([Jack("a1", GATE_X[0] - 7.5, "A"), Light("out1_led", GATE_X[0], "", well=True),
             Jack("b1", GATE_X[0] + 7.5, "B"),
             Jack("a2", GATE_X[1] - 7.5, "A"), Light("out2_led", GATE_X[1], "", well=True),
             Jack("b2", GATE_X[1] + 7.5, "B"),
             Jack("a3", GATE_X[2] - 7.5, "A"), Light("out3_led", GATE_X[2], "", well=True),
             Jack("b3", GATE_X[2] + 7.5, "B"),
             Jack("a4", GATE_X[3] - 7.5, "A"), Light("out4_led", GATE_X[3], "", well=True),
             Jack("b4", GATE_X[3] + 7.5, "B")]),
        Row([Knob("fn1", GATE_X[0] - 5.5, "FN"), Jack("out1", GATE_X[0] + 5.5, "OUT", ink="MINT"),
             Knob("fn2", GATE_X[1] - 5.5, "FN"), Jack("out2", GATE_X[1] + 5.5, "OUT", ink="MINT"),
             Knob("fn3", GATE_X[2] - 5.5, "FN"), Jack("out3", GATE_X[2] + 5.5, "OUT", ink="MINT"),
             Knob("fn4", GATE_X[3] - 5.5, "FN"), Jack("out4", GATE_X[3] + 5.5, "OUT", ink="MINT")]),
    ]),

    # Two 4066 gated switches -- the original board ganged four into one quad
    # IC; this keeps two, for panel room, each still choosing HI ON/LO ON
    # polarity and A-B (a plain gate) versus A-B/A-C (a two-way router) via
    # the pair of switches shared above both channels. ACTIVE lights if either
    # channel is currently passing signal. REF V, the panel-wide jumper for
    # FINDINGS' unpatched inputs, keeps them company.
    Section("REFERRAL", caption_light="active", rows=[
        Row([Switch("polarity", GATE_X[0] + 5.0, "POLARITY"),
             Switch("refv", P.w / 2, "REF V"),
             Switch("route", GATE_X[3] - 5.0, "ROUTE")]),
        Row([Jack("sw1_a", SW8[0], "1A"),
             Jack("sw1_gate", SW8[1], "1G", light="sw1_gate_led"),
             Jack("sw1_b", SW8[2], "1B", ink="MINT"),
             Jack("sw1_c", SW8[3], "1C", ink="MINT"),
             Jack("sw2_a", SW8[4], "2A"),
             Jack("sw2_gate", SW8[5], "2G", light="sw2_gate_led"),
             Jack("sw2_b", SW8[6], "2B", ink="MINT"),
             Jack("sw2_c", SW8[7], "2C", ink="MINT")]),
    ]),

    # The Emiz CV2 clock divider: one free-running binary counter, six taps.
    # BINARY reads the printed /2../64; MUSICAL retunes the same six jacks to
    # thirds -- /3 /6 /12 /24 /48 /96 -- rather than doubling the panel with a
    # second set of jacks. CLOCKED lights while a clock is present. MODE, the
    # division set, sits with the taps it retunes.
    Section("INSTALLMENTS", caption_light="clocked", rows=[
        Row([Jack("div2", DIV7[0], "/2", ink="MINT", light="div2_led"),
             Jack("div4", DIV7[1], "/4", ink="MINT", light="div4_led"),
             Jack("div8", DIV7[2], "/8", ink="MINT", light="div8_led"),
             Jack("div16", DIV7[3], "/16", ink="MINT", light="div16_led"),
             Jack("div32", DIV7[4], "/32", ink="MINT", light="div32_led"),
             Jack("div64", DIV7[5], "/64", ink="MINT", light="div64_led"),
             Switch("divmode", DIV7[6], "MODE")]),
    ]),
]

# The divider's transport, pinned to the footer band the way a form's filing
# block sits below its line items.
P.footer = [
    Row([Jack("clock_in", FOOT2[0], "CLOCK"), Jack("reset_in", FOOT2[1], "RESET")],
        y=118.6),
]

if __name__ == "__main__":
    raise SystemExit(build(P, root=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))))

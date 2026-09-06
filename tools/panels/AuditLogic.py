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
    hp=18,
    density="compact",
)

# Four gate columns, reused for every row that lines up with them -- the A/B
# inputs offset either side, the function knob and verdict jack sharing the
# same row a little further apart -- and reused again for the footer band, so
# the whole panel reads on one grid.
GATE_X = P.cols(4, 12.5)     # 12.50, 34.65, 56.79, 78.94
SW8 = P.cols(8, 6.0)         # the two switch channels, side by side
FOOT4 = P.cols(4, 14.0)      # the footer band, inset clear of both corner screws

P.sections = [
    # Four two-input gates, each a selectable function rather than the fixed
    # wiring the original boards hard-etched. The default per gate follows the
    # registered description's own order -- inverter, AND, OR, XOR -- which is
    # as close as the surviving schematics get to naming an assignment; NAND,
    # NOR and XNOR are additional selections, not a claim about the original
    # board. VERDICT lights if any channel is currently true.
    Section("FINDINGS", caption_light="verdict", rows=[
        Row([Jack("a1", GATE_X[0] - 5.0, "A"), Jack("b1", GATE_X[0] + 5.0, "B"),
             Jack("a2", GATE_X[1] - 5.0, "A"), Jack("b2", GATE_X[1] + 5.0, "B"),
             Jack("a3", GATE_X[2] - 5.0, "A"), Jack("b3", GATE_X[2] + 5.0, "B"),
             Jack("a4", GATE_X[3] - 5.0, "A"), Jack("b4", GATE_X[3] + 5.0, "B")]),
        # A jack leads the list so the row reserves an above-label's worth of
        # clearance from the A/B row above it; each knob's own label still
        # falls below, from its own kind, regardless of list order.
        Row([Jack("out1", GATE_X[0] + 5.5, "OUT", ink="MINT", light="out1_led"),
             Knob("fn1", GATE_X[0] - 5.5, "FN"),
             Jack("out2", GATE_X[1] + 5.5, "OUT", ink="MINT", light="out2_led"),
             Knob("fn2", GATE_X[1] - 5.5, "FN"),
             Jack("out3", GATE_X[2] + 5.5, "OUT", ink="MINT", light="out3_led"),
             Knob("fn3", GATE_X[2] - 5.5, "FN"),
             Jack("out4", GATE_X[3] + 5.5, "OUT", ink="MINT", light="out4_led"),
             Knob("fn4", GATE_X[3] - 5.5, "FN")]),
    ]),

    # Two 4066 gated switches -- the original board ganged four into one quad
    # IC; this keeps two, for panel room, each still choosing HI ON/LO ON
    # polarity and A-B (a plain gate) versus A-B/A-C (a two-way router) via
    # the pair of switches shared above both channels. ACTIVE lights if either
    # channel is currently passing signal.
    Section("REFERRAL", caption_light="active", rows=[
        Row([Switch("polarity", 30.0, "POLARITY"),
             Switch("route", 61.44, "ROUTE")]),
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
    # second set of jacks. CLOCKED lights while a clock is present.
    Section("INSTALLMENTS", caption_light="clocked", rows=[
        Row([Jack("div2", 7.0, "/2", ink="MINT", light="div2_led"),
             Jack("div4", 22.488, "/4", ink="MINT", light="div4_led"),
             Jack("div8", 37.976, "/8", ink="MINT", light="div8_led"),
             Jack("div16", 53.464, "/16", ink="MINT", light="div16_led"),
             Jack("div32", 68.952, "/32", ink="MINT", light="div32_led"),
             Jack("div64", 84.44, "/64", ink="MINT", light="div64_led")]),
    ]),
]

# The shared references, pinned to the footer band the way a form's filing
# block sits below its line items: the divider's own transport, and the two
# panel-wide jumpers (REF V for FINDINGS' unpatched inputs, MODE for
# INSTALLMENTS' division set).
P.footer = [
    Row([Jack("clock_in", FOOT4[0], "CLOCK"), Jack("reset_in", FOOT4[1], "RESET"),
         Switch("divmode", FOOT4[2], "MODE"), Switch("refv", FOOT4[3], "REF V")],
        y=118.6),
]

if __name__ == "__main__":
    raise SystemExit(build(P, root=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))))

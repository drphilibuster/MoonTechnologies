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
    density="compact",
)

P.sections = [
    # Four two-input gates, each a selectable function rather than the fixed
    # wiring the original boards hard-etched. The default per gate follows the
    # registered description's own order -- inverter, AND, OR, XOR -- which is
    # as close as the surviving schematics get to naming an assignment; NAND,
    # NOR and XNOR are additional selections, not a claim about the original
    # board. Each gate's light sits between its A and B inputs; VERDICT beside
    # the caption lights if any channel is currently true. REF V is the
    # reference an unpatched input reads, from the quad inverter.
    #
    # Twelve columns, in four runs of three: the grouping here is what the gates
    # are, not how wide they are, so the section says so outright rather than
    # leaving the solver to guess from twelve identical jacks.
    Section("FINDINGS", caption_light="verdict", groups=(3, 3, 3, 3), rows=[
        # Each gate's light sits between its own A and B, which is what says the
        # two inputs belong to one gate. It is an accessory to that pair, not a
        # column of the panel: `between` hangs it in the gap and widens only
        # that gap to hold it, where owning a column charged it the full pitch
        # of the knob below -- four columns of mostly empty panel.
        Row([Jack("a1", "A", col=0), Light("out1_led", between=(0, 1)),
             Jack("b1", "B", col=1),
             Jack("a2", "A", col=2), Light("out2_led", between=(2, 3)),
             Jack("b2", "B", col=3),
             Jack("a3", "A", col=4), Light("out3_led", between=(4, 5)),
             Jack("b3", "B", col=5),
             Jack("a4", "A", col=6), Light("out4_led", between=(6, 7)),
             Jack("b4", "B", col=7)]),
        Row([Knob("fn1", "FN", steps=7, col=0),
             Jack("out1", "OUT", ink="MINT", col=1),
             Knob("fn2", "FN", steps=7, col=2),
             Jack("out2", "OUT", ink="MINT", col=3),
             Knob("fn3", "FN", steps=7, col=4),
             Jack("out3", "OUT", ink="MINT", col=5),
             Knob("fn4", "FN", steps=7, col=6),
             Jack("out4", "OUT", ink="MINT", col=7)]),
    ]),

    # Two 4066 gated switches -- the original board ganged four into one quad
    # IC; this keeps two, for panel room, each still choosing HI ON/LO ON
    # polarity and A-B (a plain gate) versus A-B/A-C (a two-way router) via
    # the pair of switches shared above both channels. ACTIVE lights if either
    # channel is currently passing signal. REF V, the panel-wide jumper for
    # FINDINGS' unpatched inputs, keeps them company.
    Section("REFERRAL", caption_light="active", groups=(4, 4), rows=[
        Row([Switch("polarity", "POLARITY"),
             Switch("refv", "REF V"),
             Switch("route", "ROUTE")], own_grid=True),
        Row([Jack("sw1_a", "1A"),
             Jack("sw1_gate", "1G", light="sw1_gate_led"),
             Jack("sw1_b", "1B", ink="MINT"),
             Jack("sw1_c", "1C", ink="MINT"),
             Jack("sw2_a", "2A"),
             Jack("sw2_gate", "2G", light="sw2_gate_led"),
             Jack("sw2_b", "2B", ink="MINT"),
             Jack("sw2_c", "2C", ink="MINT")]),
    ]),

    # The Emiz CV2 clock divider: one free-running binary counter, six taps.
    # BINARY reads the printed /2../64; MUSICAL retunes the same six jacks to
    # thirds -- /3 /6 /12 /24 /48 /96 -- rather than doubling the panel with a
    # second set of jacks. CLOCKED lights while a clock is present. MODE, the
    # division set, sits with the taps it retunes.
    Section("INSTALLMENTS", caption_light="clocked", rows=[
        Row([Jack("div2", "/2", ink="MINT", light="div2_led"),
             Jack("div4", "/4", ink="MINT", light="div4_led"),
             Jack("div8", "/8", ink="MINT", light="div8_led"),
             Jack("div16", "/16", ink="MINT", light="div16_led"),
             Jack("div32", "/32", ink="MINT", light="div32_led"),
             Jack("div64", "/64", ink="MINT", light="div64_led"),
             Switch("divmode", "MODE")]),
    ]),
]

# The divider's transport, pinned to the footer band the way a form's filing
# block sits below its line items.
P.footer = [
    Row([Jack("clock_in", "CLOCK"), Jack("reset_in", "RESET")], y=118.6),
]

if __name__ == "__main__":
    raise SystemExit(build(P, root=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))))

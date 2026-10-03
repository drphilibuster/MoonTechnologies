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
a binary counter paying a clock out in six declining fractions. CLOCK and
RESET live on the footer band because they are the panel's shared
references, the way a form's filing details sit below the line items.

Every setting is on the read-out under the masthead: click a gate's function
to pick one of the seven, click POLARITY, REF V, ROUTE or MODE to flip it. The
four FN knobs and four toggles that used to hold them were the panel's width;
with them gone the gates fold into two rows and each switch into one, and the
panel narrowed to what the jack rows need.

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
    what="LOGIC, SWITCHES & DIVIDER",
    form="FORM 886-A",
    density="compact",
    # The settings, all of them, on two lines: the four gates' functions, then
    # the four panel-wide switches by their state words. Each cell is a small
    # caption over its value; the height is what the face had left once the
    # rows below folded.
    glass=Glass(h=20.0, grid=(2, 4), fields=[
        Field("fn%d" % n, cell=(0, n - 1), kind="select") for n in range(1, 5)
    ] + [
        Field("polarity", cell=(1, 0), kind="toggle"),
        Field("refv",     cell=(1, 1), kind="toggle"),
        Field("route",    cell=(1, 2), kind="toggle"),
        Field("divmode",  cell=(1, 3), kind="toggle"),
    ]),
)


def gate(n, label):
    # A gate reads across: A, its light, B, then what it found. The light sits
    # between its own A and B, which is what says the two inputs belong to one
    # gate; `between` hangs it in the gap rather than charging it a column.
    c = 3 * ((n - 1) % 2)
    return [Jack("a%d" % n, "A" if label else "", col=c),
            Light("out%d_led" % n, between=(c, c + 1)),
            Jack("b%d" % n, "B" if label else "", col=c + 1),
            Jack("out%d" % n, "OUT" if label else "", ink="MINT", col=c + 2)]


P.sections = [
    # Four two-input gates, each a selectable function rather than the fixed
    # wiring the original boards hard-etched. The default per gate follows the
    # registered description's own order -- inverter, AND, OR, XOR -- which is
    # as close as the surviving schematics get to naming an assignment; NAND,
    # NOR and XNOR are additional selections, not a claim about the original
    # board. VERDICT beside the caption lights if any channel is currently
    # true.
    #
    # Gates 1 and 2 on the first row, 3 and 4 on the second, each in a run of
    # three -- A, B, OUT -- because the grouping here is what the gates are,
    # not how wide they are. FN used to be a knob per gate wearing a plate the
    # module wrote AND, XOR, NAND into; the function is a word on the glass
    # now, and the row the knobs stood in is what let the four gates fold into
    # two rows.
    Section("FINDINGS", caption_light="verdict", groups=(3, 3), rows=[
        Row(gate(1, True) + gate(2, True)),
        Row(gate(3, False) + gate(4, False), silent=True),
    ]),

    # Two 4066 gated switches -- the original board ganged four into one quad
    # IC; this keeps two, for panel room, each still choosing HI ON/LO ON
    # polarity and A-B (a plain gate) versus A-B/A-C (a two-way router) via
    # the pair of switches shared by both channels, which are on the glass.
    # ACTIVE lights if either channel is currently passing signal. One switch
    # per row.
    Section("REFERRAL", caption_light="active", rows=[
        Row([Jack("sw1_a", "1A"),
             Jack("sw1_gate", "1G", light="sw1_gate_led"),
             Jack("sw1_b", "1B", ink="MINT"),
             Jack("sw1_c", "1C", ink="MINT")]),
        Row([Jack("sw2_a", "2A"),
             Jack("sw2_gate", "2G", light="sw2_gate_led"),
             Jack("sw2_b", "2B", ink="MINT"),
             Jack("sw2_c", "2C", ink="MINT")]),
    ]),

    # The Emiz CV2 clock divider: one free-running binary counter, six taps.
    # BINARY reads the printed /2../64; MUSICAL retunes the same six jacks to
    # thirds -- /3 /6 /12 /24 /48 /96 -- rather than doubling the panel with a
    # second set of jacks. CLOCKED lights while a clock is present. MODE, the
    # division set, is on the glass.
    Section("INSTALLMENTS", caption_light="clocked", rows=[
        Row([Jack("div2", "/2", ink="MINT", light="div2_led"),
             Jack("div4", "/4", ink="MINT", light="div4_led"),
             Jack("div8", "/8", ink="MINT", light="div8_led"),
             Jack("div16", "/16", ink="MINT", light="div16_led"),
             Jack("div32", "/32", ink="MINT", light="div32_led"),
             Jack("div64", "/64", ink="MINT", light="div64_led")]),
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

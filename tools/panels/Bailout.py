#!/usr/bin/env python3
"""The Bailout panel, declared once.

Running this file regenerates the artwork, the shared hardware, src/PanelTheme.hpp
and the previews. See ../../panelkit/README.md for the pipeline.

Bailout is Consolidation at twice the size, and not quite the same shape. Eight
strips instead of four, but arranged as two banks of four rather than one run of
eight: MIX A and MIX B are separate outputs, and MAIN sums whichever of them is
not already spoken for. That is the difference between a bigger mixer and a
submixer, and a submixer is what eight channels actually wants to be.

The multiples double too, and cost nothing to do it -- the mixer's eight columns
already set the width, so a multiple that was one-in-three on a four-column
panel is one-in-seven here for free, B still normalled to A, which makes the pair
1:14 on one cable.

25 HP. INV is the one thing Consolidation has that this does not: a fourth jack
on the footer band costs two more HP, and the second inverting stage is spent
here on giving each bank its own correct polarity instead, so MIX A and MIX B
read the same way round as MAIN rather than against it.
"""

import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
from panelkit import *   # noqa: E402

P = Panel(
    slug="Bailout",
    title="BAILOUT",
    form="FORM 1040-X",
)

P.sections = [
    # Eight channels in two groups of four. The section gutter between 4 and 5
    # is the whole reason for the grouping: it is the only thing on the panel
    # that says these are two mixers and not one, and the two MUTE and LEVEL CV
    # plates stopping at the boundary say it again.
    Section("MIXER", groups=(4, 4), rows=[
        # Each level knob wears its channel's meter as an arc in the seat ring
        # around it -- the dark band already there between the knob and its
        # well, so the read-out costs the panel nothing at all.
        Row([Knob("lvl1", "LVL 1"), Knob("lvl2", "LVL 2"),
             Knob("lvl3", "LVL 3"), Knob("lvl4", "LVL 4"),
             Knob("lvl5", "LVL 5"), Knob("lvl6", "LVL 6"),
             Knob("lvl7", "LVL 7"), Knob("lvl8", "LVL 8")]),
        Row(span=[(0, 3, "MUTE"), (4, 7, "MUTE")],
            items=[Button("mute1", "", light="mute1_led"),
                   Button("mute2", "", light="mute2_led"),
                   Button("mute3", "", light="mute3_led"),
                   Button("mute4", "", light="mute4_led"),
                   Button("mute5", "", light="mute5_led"),
                   Button("mute6", "", light="mute6_led"),
                   Button("mute7", "", light="mute7_led"),
                   Button("mute8", "", light="mute8_led")]),
        # The level CV, a jack on its own rather than a trim over one: the knob
        # is already the depth, because CV multiplies it rather than adding to
        # it, and a second attenuator on top of that is a row this panel cannot
        # spare any more than Consolidation's could.
        Row(span=[(0, 3, "LEVEL CV"), (4, 7, "LEVEL CV")],
            items=[Jack("cv1"), Jack("cv2"), Jack("cv3"), Jack("cv4"),
                   Jack("cv5"), Jack("cv6"), Jack("cv7"), Jack("cv8")]),
        Row([Jack("in1", "IN 1"), Jack("in2", "IN 2"),
             Jack("in3", "IN 3"), Jack("in4", "IN 4"),
             Jack("in5", "IN 5"), Jack("in6", "IN 6"),
             Jack("in7", "IN 7"), Jack("in8", "IN 8")]),
    ]),

    # Two 1-in-7-out multiples, each on one row: the input at the left with the
    # name, its seven legs to the right of it. The width is already paid for by
    # the mixer above, so the extra four legs are free.
    Section("MULTIPLES", rows=[
        Row([Jack("mult_a_in", "MULT A")] +
            [Jack("mult_a_out%d" % (i + 1), "", ink="MINT") for i in range(7)]),
        Row([Jack("mult_b_in", "MULT B")] +
            [Jack("mult_b_out%d" % (i + 1), "", ink="MINT") for i in range(7)]),
    ]),
]

# Everything that leaves the module, on the footer band, mint for outputs --
# the family's convention everywhere else and no different here. The eight
# direct outs work the way Consolidation's four do: patch one and that channel
# stops feeding its bank and goes there instead. MIX A and MIX B do the same
# thing one level up -- patch either and that bank stops feeding MAIN -- so the
# module is one 8-into-1 mixer, two independent 4s, eight VCAs, or any mixture,
# without a switch anywhere to say which it is being.
P.footer = [
    Row([Jack("out1", "1", ink="MINT"), Jack("out2", "2", ink="MINT"),
         Jack("out3", "3", ink="MINT"), Jack("out4", "4", ink="MINT"),
         Jack("out5", "5", ink="MINT"), Jack("out6", "6", ink="MINT"),
         Jack("out7", "7", ink="MINT"), Jack("out8", "8", ink="MINT"),
         Jack("mix_a_out", "MIX A", ink="MINT"),
         Jack("mix_b_out", "MIX B", ink="MINT"),
         Jack("main_out", "MAIN", ink="MINT")], y=118.6),
]

if __name__ == "__main__":
    raise SystemExit(build(P, root=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))))

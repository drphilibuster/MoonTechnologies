#!/usr/bin/env python3
"""The Consolidation panel, declared once.

Running this file regenerates the artwork, the shared hardware, src/PanelTheme.hpp
and the previews. See ../../panelkit/README.md for the pipeline.

Consolidation is a Modular in a Week mixer and multiples on one panel: a
four-channel inverting-summer mixer (ASMR's own gain structure -- unity per
channel, a second inverting stage to bring the sum back to normal polarity, so
both polarities come out) over a pair of 1-in-3-out multiples, B normalled to A
so one cable gives 1:3, two give 1:6.
"""

import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
from panelkit import *   # noqa: E402

P = Panel(
    slug="Consolidation",
    title="CONSOLIDATION",
    form="FORM 1040",
)

P.sections = [
    # Four channels, read top to bottom: what the level is, whether the channel
    # is passing at all, what is modulating it, what goes in, and where it comes
    # out. A channel whose own OUT is patched leaves the mix and goes there
    # instead, so the same four strips are a mixer, four VCAs, or any split of
    # the two -- which is the thing the original board could not do.
    Section("MIXER", rows=[
        # Each level knob wears its channel's meter as an arc in the seat ring
        # around it -- the dark band that is already there between the knob and
        # its well, so the read-out costs the panel nothing at all.
        Row([Knob("lvl1", "LVL 1"), Knob("lvl2", "LVL 2"),
             Knob("lvl3", "LVL 3"), Knob("lvl4", "LVL 4")]),
        Row(span=[(0, 3, "MUTE")],
            items=[Button("mute1", "", light="mute1_led"),
                   Button("mute2", "", light="mute2_led"),
                   Button("mute3", "", light="mute3_led"),
                   Button("mute4", "", light="mute4_led")]),
        # The level CV. A jack on its own rather than a trim over one: the knob
        # is already the depth, because CV multiplies it rather than adding to
        # it, and a second attenuator on top of that is a row this panel cannot
        # spare.
        Row(span=[(0, 3, "LEVEL CV")],
            items=[Jack("cv1"), Jack("cv2"), Jack("cv3"), Jack("cv4")]),
        Row([Jack("in1", "IN 1"), Jack("in2", "IN 2"),
             Jack("in3", "IN 3"), Jack("in4", "IN 4")]),
    ]),

    # Two 1-in-3-out multiples, each on one row: the input at the left with the
    # name, its three legs to the right of it. Four rows of three columns became
    # two rows of four, which is the same jacks in half the height -- and the
    # height is what this panel was short of.
    Section("MULTIPLES", rows=[
        Row([Jack("mult_a_in", "MULT A"),
             Jack("mult_a_out1", "", ink="MINT"),
             Jack("mult_a_out2", "", ink="MINT"),
             Jack("mult_a_out3", "", ink="MINT")]),
        Row([Jack("mult_b_in", "MULT B"),
             Jack("mult_b_out1", "", ink="MINT"),
             Jack("mult_b_out2", "", ink="MINT"),
             Jack("mult_b_out3", "", ink="MINT")]),
    ]),
]

# The mix leaving the module is its main audio I/O, so -- like every other
# panel in the family -- it sits on the footer band, mint for the outputs.
# The four direct outputs sit on the band with the mix rather than in a row of
# their own upstairs: they are audio leaving the module, which is what this band
# carries everywhere else in the family, and a fifth row in the mixer overran the
# face by four millimetres. Patch one and that channel stops feeding the mix and
# goes there instead; leave it empty and it sums, as it always did. All four
# patched, the module is four VCAs.
P.footer = [
    Row([Jack("out1", "1", ink="MINT"), Jack("out2", "2", ink="MINT"),
         Jack("out3", "3", ink="MINT"), Jack("out4", "4", ink="MINT"),
         Jack("out", "MIX", ink="MINT"),
         Jack("inv_out", "INV", ink="MINT")], y=118.6),
]

if __name__ == "__main__":
    raise SystemExit(build(P, root=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))))

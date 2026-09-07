#!/usr/bin/env python3
"""The Toll panel, declared once.

Running this file regenerates the artwork, the shared hardware, src/PanelTheme.hpp
and the previews. See ../../panelkit/README.md for the pipeline.

Toll is the struck-metal voice that came out of Kickback. BaSnaHi's snare stage
-- a resonant pair shocked by a trigger edge with a second layer buzzing against
it -- read as a struck metal pipe rather than as a snare, so it left the drum
bank and got the room to be what it actually was.

Three columns and five rows, plus the jacks along the bottom. The top row is what plays it. Below
that, reading down: what the object is made of and what is loose against it,
how long it rings and how fast the upper partials go, and where and with what
it is hit.

No clock and no patterns. Kickback has those, and a voice you want to play from
a keyboard should not come with a sequencer attached to it.
"""

import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
from panelkit import *   # noqa: E402

P = Panel(
    slug="Toll",
    title="TOLL",
    form="FORM 2290",
)

P.sections = [
    # 2290 is the Heavy Highway Vehicle Use Tax -- the toll, filed.
    Section("HIGHWAY USE", rows=[
        # What plays it.
        Row([Jack("trig", "TRIG", ink="PAPER", light="hit_led"),
             Jack("voct", "V/OCT")]),

        # What it is. TUNE is the one control you reach for, so it gets the big
        # seat and the lime ring. SET is what the thing is made of -- a
        # membrane, a bar, a bell, or a bare harmonic series -- on four detents
        # engraved into its own well. BUZZ is the loose layer against the body,
        # a one-sided collision that only speaks once the body swings far
        # enough to reach it.
        Row([BigKnob("tune", "TUNE", primary=True),
             Knob("set", "SET", steps=4),
             Knob("buzz", "BUZZ")]),

        # How it rings. DAMP is how much faster the upper partials go than the
        # fundamental, which is most of what separates bronze from lead. BEND is
        # the tension term: how far the pitch falls as the strike's energy
        # leaves the object.
        Row([Knob("decay", "DECAY"),
             Knob("damp", "DAMP"),
             Knob("bend", "BEND")]),

        # How it is hit. STRIKE moves from the middle of the object to its
        # edge; HARD from three milliseconds of felt to a third of one of wood.
        # SPREAD stretches whichever partial set SET chose away from itself.
        Row([Knob("strike", "STRIKE"),
             Knob("hard", "HARD"),
             Knob("spread", "SPREAD")]),

        # What a patch can play rather than set. Between these and the two in
        # the footer, every control that shapes the sound rather than choosing
        # the object has a jack: strike position, ring time, the partial
        # stretch, and the tension bend. CHOKE is the hand on the bell -- a gate
        # that damps whatever is ringing, the way a hi-hat pedal does.
        Row([Jack("spread_cv", "SPRD"),
             Jack("bend_cv", "BEND"),
             Jack("choke", "CHOKE")]),
    ]),
]

# The I/O row sits as low as the bottom screws allow. The two CV jacks play the
# two controls worth playing rather than setting: where it is hit, and for how
# long it rings.
P.footer = [
    Row([Jack("strike_cv", "STRK"),
         Jack("decay_cv", "DECAY"),
         Jack("out", "OUT", ink="MINT")], y=118.6),
]

if __name__ == "__main__":
    raise SystemExit(build(P, root=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))))

#!/usr/bin/env python3
"""The Rebate panel, declared once.

Running this file regenerates the artwork, the shared hardware, src/PanelTheme.hpp
and the previews. See ../../panelkit/README.md for the pipeline.

Rebate is an Alesis MIDIverb -- Keith Barr's 1986 discrete-logic reverb, 63
programs on a microcode EPROM -- running its own firmware on its own 80C31, or
the MIDIFEX, which is the same board with another EPROM. It is FORM 843 because
a reverb is a rebate: some of what you put in, paid back to you later and a
little less each time.

The read-out well holds the unit's two seven-segment digits, driven segment by
segment from the firmware's own multiplex, and a line for the module's status.
The unit's four buttons live on the read-out now: the top half of the digits is
UP and the bottom half DOWN, the word beside them is DEFEAT, and the channel
number on the other side is CHANNEL. Each is the param its panel button was, so
the firmware sees the same presses at the same times. FRONT PANEL keeps what a
screen cannot be: the MIDIverb's -12 dB and 0 dB level LEDs and the MIX pot
that is on the unit's back.
"""

import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
from panelkit import *   # noqa: E402

P = Panel(
    slug="Rebate",
    title="REBATE",
    what="MIDIVERB DIGITAL REVERB",
    form="FORM 843",
    # Five lines, six columns. The digits take the middle four columns of the
    # first four lines -- the top two are UP, the bottom two DOWN, so you press
    # the half of the number you want it to go -- with CHANNEL down the left
    # column and DEFEAT down the right. The last line is the status, which is
    # not a field: it says to right-click the module, and a field there would
    # answer that click with its own param menu instead.
    glass=Glass(h=26.0, grid=(5, 6), fields=[
        Field("channel", cell=(0, 0), span=(4, 1), kind="select"),
        Field("up",      cell=(0, 1), span=(2, 4), kind="button"),
        Field("down",    cell=(2, 1), span=(2, 4), kind="button"),
        Field("defeat",  cell=(0, 5), span=(4, 1), kind="button"),
    ]),
)

P.sections = [
    # What the screen cannot be: the unit's level LEDs, which watch the signal
    # on its way into the converter, and the MIX pot from the unit's back.
    Section("FRONT PANEL", rows=[
        Row([Light("meter_green", "-12 dB", ink="LIME"), Light("meter_red", "0 dB", ink="CLAY")], own_grid=True),
        Row([BigKnob("mix", "MIX", primary=True)], own_grid=True),
    ]),
]

P.footer = [
    Row([Jack("in_l", "IN L"), Jack("in_r", "IN R"),
         Jack("out_l", "OUT L", ink="MINT"), Jack("out_r", "OUT R", ink="MINT")],
        y=118.6),
]

if __name__ == "__main__":
    raise SystemExit(build(P, root=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))))

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
FRONT PANEL is the MIDIverb's: its -12 dB and 0 dB level LEDs, its four
buttons, and the MIX pot that is on the unit's back.
"""

import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
from panelkit import *   # noqa: E402

P = Panel(
    slug="Rebate",
    title="REBATE",
    subtitle="MIDI REVERB",
    form="FORM 843",
    hp=12,
    glass=Glass(h=20.0),
)

W = P.w                          # 60.96 mm at 12 HP
M = 6.5

# --- inside the read-out well -------------------------------------------------
# The two digits at about the MAN4710's own proportions, the status line below.
DIG_W, DIG_H = 22.0, 11.6
DIG_X, DIG_Y = (W - DIG_W) / 2, 10.9
TXT_X, TXT_Y, TXT_W, TXT_H = M, 23.3, W - 2 * M, 5.2

P.plates = [
    Plate("digits", DIG_X, DIG_Y, DIG_W, DIG_H, r=0.8),
    Plate("status", TXT_X, TXT_Y, TXT_W, TXT_H, r=0.8, fill=BAND),
]

P.sections = [
    # The unit's own controls. CHANNEL is a stepper: on the unit it is held while UP or DOWN is pressed,
    # which a mouse cannot do, so each click holds CHANNEL and presses the one key for you. DEFEAT mutes the effect (the display shows --). The level
    # LEDs watch the signal on its way into the converter.
    Section("FRONT PANEL", rows=[
        Row([Light("meter_green", "-12 dB", ink="LIME"), Light("meter_red", "0 dB", ink="CLAY")], own_grid=True),
        Row([Stepper("channel", "CHANNEL"), Button("up", "UP"),
             Button("down", "DOWN"), Button("defeat", "DEFEAT")], own_grid=True),
        Row([BigKnob("mix", "MIX", primary=True)], own_grid=True),
    ]),
]

P.footer = [
    Row([Jack("in_l", "IN L"), Jack("in_r", "IN R"),
         Jack("out_l", "OUT L", ink="MINT"), Jack("out_r", "OUT R", ink="MINT")],
        y=118.6),
]

# Echoed into src/Rebate/Panel.hpp so the live display and the artwork cannot
# disagree about where a field is.
P.metrics = dict(
    DIG_X=DIG_X, DIG_Y=DIG_Y, DIG_W=DIG_W, DIG_H=DIG_H,
    TXT_X=TXT_X, TXT_Y=TXT_Y, TXT_W=TXT_W, TXT_H=TXT_H,
)

if __name__ == "__main__":
    raise SystemExit(build(P, root=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))))

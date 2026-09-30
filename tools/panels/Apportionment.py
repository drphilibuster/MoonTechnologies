#!/usr/bin/env python3
"""The Apportionment panel, declared once.

Running this file regenerates the artwork, the shared hardware, src/PanelTheme.hpp
and the previews. See ../../panelkit/README.md for the pipeline.

Apportionment is an Ensoniq DP/4 -- four ESP effect units under one operating
system -- running the real firmware. It is FORM 1116 because the DP/4's whole
trick is apportionment: one or several input signals divided among four units,
and their results allocated back to four outputs.

The read-out well is the DP/4's own display: its 2 x 16 LCD and two-digit LED,
drawn live from the byte stream the firmware sends to its front-panel board,
beside a diagram of the routing the machine is actually running.

FRONT PANEL is the DP/4's own control surface, button for button, so everything
the manual describes can be done here. CONFIG is the point of the module: on the
hardware the routing lives in the Config pages, reached through EDIT, CONFIG and
the arrow keys. Here every one of those parameters is a control of its own, and
moving one makes the module play the pages for you -- the firmware still builds
the routing, and the controls follow it back when a Config preset changes it.
"""

import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
from panelkit import *   # noqa: E402

P = Panel(
    slug="Apportionment",
    title="APPORTIONMENT",
    subtitle="PARALLEL EFFECTS",
    form="FORM 1116",
    hp=34,
    glass=Glass(h=22.4),
)

W = P.w                          # 172.72 mm at 34 HP
M = 6.5                          # side margin, inside the glass well's own 4.2

# --- inside the read-out well --------------------------------------------------
# The LCD takes the left half at the proportions of the real 2 x 16 module; the
# LED digits sit to its right as on the DP/4's own panel; the routing diagram
# takes the rest.
GLASS_Y0 = 9.8
LCD_X, LCD_Y, LCD_W, LCD_H = M, 11.4, 74.0, 19.2
LED_X, LED_Y, LED_W, LED_H = LCD_X + LCD_W + 2.0, 11.4, 17.0, 19.2
MAP_X = LED_X + LED_W + 2.0
MAP_Y, MAP_W, MAP_H = 11.4, W - MAP_X - M, 19.2

P.plates = [
    Plate("lcd", LCD_X, LCD_Y, LCD_W, LCD_H, r=0.8),
    Plate("led", LED_X, LED_Y, LED_W, LED_H, r=0.8),
    Plate("map", MAP_X, MAP_Y, MAP_W, MAP_H, r=0.8, fill=BAND, tab="LIME"),
]

P.sections = [
    # The DP/4's own buttons, lit as the firmware lights them. The data knob is
    # endless, as on the hardware: it reports detents, not a position.
    Section("FRONT PANEL", rows=[
        Row([Bezel("unit_a", "A"), Bezel("unit_b", "B"), Bezel("unit_c", "C"),
             Bezel("unit_d", "D"), Bezel("config", "CONFIG"),
             Bezel("system", "SYSTEM"), Bezel("edit", "EDIT"),
             BigKnob("data", "DATA", primary=True)]),
        Row([Button("select", "SELECT"), Button("left", "<"),
             Button("right", ">"), Button("cancel", "CANCEL"),
             Button("write", "WRITE")]),
    ]),

    # The routing. Each control is one Config parameter; the caption light shows
    # the module working the Config pages to reach what the controls say.
    # A-B and C-D are serial, parallel, feedback 1, feedback 2. AB>CD is only
    # meaningful with one source; AB OUT and CD OUT only once the pairs are split.
    Section("CONFIG", caption_light="routing", rows=[
        Row([Knob("sources", "SOURCES", steps=4),
             Knob("ab_route", "A-B", steps=4),
             Knob("cd_route", "C-D", steps=4),
             Switch("ab_cd", "AB>CD"),
             Knob("ab_amount", "AB AMT"),
             Knob("cd_amount", "CD AMT")]),
        Row([Switch("ab_mono", "AB IN"), Switch("cd_mono", "CD IN"),
             Switch("ab_out", "AB OUT"), Switch("cd_out", "CD OUT"),
             Knob("in_level", "IN LEVEL"), Knob("out_level", "OUT LEVEL")]),
    ]),

]

# Everything that enters or leaves, inputs left and outputs right. The taps are
# each unit's own output port, stereo on one polyphonic cable, for patching the
# units into the rest of the rack as well as into each other.
P.footer = [
    Row([Jack("in1", "IN 1"), Jack("in2", "IN 2"), Jack("in3", "IN 3"),
         Jack("in4", "IN 4"), Jack("pedal_in", "PEDAL"), Jack("fs_l_in", "FS L"),
         Jack("fs_r_in", "FS R"),
         Jack("out1", "OUT 1", ink="MINT"), Jack("out2", "OUT 2", ink="MINT"),
         Jack("out3", "OUT 3", ink="MINT"), Jack("out4", "OUT 4", ink="MINT"),
         Jack("tap_a", "TAP A", ink="MINT"), Jack("tap_b", "TAP B", ink="MINT"),
         Jack("tap_c", "TAP C", ink="MINT"), Jack("tap_d", "TAP D", ink="MINT")],
        y=118.6),
]

# Echoed into src/Apportionment/Panel.hpp so the live display and the artwork
# cannot disagree about where a field is.
P.metrics = dict(
    LCD_X=LCD_X, LCD_Y=LCD_Y, LCD_W=LCD_W, LCD_H=LCD_H,
    LED_X=LED_X, LED_Y=LED_Y, LED_W=LED_W, LED_H=LED_H,
    MAP_X=MAP_X, MAP_Y=MAP_Y, MAP_W=MAP_W, MAP_H=MAP_H,
)

if __name__ == "__main__":
    raise SystemExit(build(P, root=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))))

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
over a map of the routing the machine is actually running.

FRONT PANEL is the DP/4's own control surface, button for button, so everything
the manual describes can be done here. The routing is the point of the module:
on the hardware it lives in the Config pages, reached through EDIT, CONFIG and
the arrow keys. Here every one of those parameters is a field on the map that
draws it -- click the source count, a pair's joint, an input or output to change
it; click a unit to choose between bypass and kill; drag an amount or a level --
and moving one makes the module play the pages for you. The firmware still
builds the routing, and the map follows it back when a Config preset changes
it. The two arrows at the ends of the LCD are the "whole screen" gestures, and
the wheel over the LCD turns DATA.

26 HP. The CONFIG knobs and switches and the four B/K switches were the routing
twice -- once as controls and once on the map -- and the map is where they went.
That freed the section they stood in, which the read-out took (the map now has the
well's full width under the LCD), and the outputs went to a rail.
"""

import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
from panelkit import *   # noqa: E402

P = Panel(
    slug="Apportionment",
    title="APPORTIONMENT",
    what="DP/4 PARALLEL EFFECTS",
    form="FORM 1116",
    # Pinned because the plates inside the well are laid out against the width;
    # 26 is what the rows below solve to on their own.
    hp=26,
    glass=Glass(h=42.0, fields=[
        # The "whole screen" gestures, as arrows at the two ends of the LCD.
        Field("screen_prev", plate="lcd", cell=(0, 0), span=(2, 1), kind="button"),
        Field("screen_next", plate="lcd", cell=(0, 17), span=(2, 1), kind="button"),
        # The routing map, on a 7 x 7 grid: inputs in column 0, A and C in 2,
        # the pairs' joints in 3, B and D in 4, outputs in 6; the A-B pair in
        # rows 1-2 under its own caption row, the C-D pair in rows 4-5 over its.
        Field("in_level",  plate="map", cell=(0, 0), kind="value"),
        Field("ab_route",  plate="map", cell=(0, 3), span=(3, 1), kind="select"),
        Field("ab_amount", plate="map", cell=(0, 4), kind="value"),
        Field("out_level", plate="map", cell=(0, 6), kind="value"),
        Field("ab_mono",   plate="map", cell=(1, 0), span=(2, 1), kind="toggle"),
        Field("kill_a",    plate="map", cell=(1, 2), span=(2, 1), kind="toggle"),
        Field("kill_b",    plate="map", cell=(1, 4), span=(2, 1), kind="toggle"),
        Field("ab_out",    plate="map", cell=(1, 6), span=(2, 1), kind="toggle"),
        Field("sources",   plate="map", cell=(3, 0), kind="select"),
        Field("ab_cd",     plate="map", cell=(3, 2), span=(1, 3), kind="toggle"),
        Field("cd_mono",   plate="map", cell=(4, 0), span=(2, 1), kind="toggle"),
        Field("kill_c",    plate="map", cell=(4, 2), span=(2, 1), kind="toggle"),
        Field("cd_route",  plate="map", cell=(4, 3), span=(3, 1), kind="select"),
        Field("kill_d",    plate="map", cell=(4, 4), span=(2, 1), kind="toggle"),
        Field("cd_out",    plate="map", cell=(4, 6), span=(2, 1), kind="toggle"),
        Field("cd_amount", plate="map", cell=(6, 4), kind="value"),
    ]),
)

W = P.w                          # 132.08 mm at 26 HP
M = 6.5                          # side margin, inside the glass well's own 4.2
IW = W - 2 * M                   # 119.08 mm of usable width

# --- inside the read-out well --------------------------------------------------
# The LCD and the LED digits across the top, as on the DP/4's own panel; the
# routing map across the whole width under them.
LED_W = 22.0
LCD_X, LCD_Y, LCD_W, LCD_H = M, 11.3, IW - LED_W - 2.0, 16.8     # 11.30 .. 28.10
LED_X, LED_Y, LED_H = LCD_X + LCD_W + 2.0, LCD_Y, LCD_H
MAP_X, MAP_Y, MAP_W, MAP_H = M, 29.3, IW, 20.8                   # 29.30 .. 50.10

P.plates = [
    Plate("lcd", LCD_X, LCD_Y, LCD_W, LCD_H, r=0.8, grid=(2, 18)),
    Plate("led", LED_X, LED_Y, LED_W, LED_H, r=0.8),
    Plate("map", MAP_X, MAP_Y, MAP_W, MAP_H, r=0.8, fill=BAND, tab="LIME", grid=(7, 7)),
]

P.sections = [
    # The DP/4's own buttons, lit as the firmware lights them. The data knob is
    # endless, as on the hardware: it reports detents, not a position.
    #
    # Pressing an active unit's button again bypasses it, as on the DP/4, and
    # the light beside its name is the unit's red bypass LED. What bypass does
    # is the unit's B/K -- the Config's bypass/kill page, a click on the unit's
    # box on the map: B passes the dry signal, K mutes the unit.
    Section("FRONT PANEL", rows=[
        Row([Bezel("unit_a", "A", light="bypass_a"), Bezel("unit_b", "B", light="bypass_b"),
             Bezel("unit_c", "C", light="bypass_c"), Bezel("unit_d", "D", light="bypass_d"),
             Bezel("config", "CONFIG"), Bezel("system", "SYSTEM"), Bezel("edit", "EDIT"),
             BigKnob("data", "DATA", primary=True)]),
        Row([Button("select", "SELECT"), Button("left", "<"),
             Button("right", ">"), Button("cancel", "CANCEL"),
             Button("write", "WRITE"),
             Bezel("copy", "COPY"), Bezel("swap", "SWAP")], own_grid=True),
        # The DP/4's two-handed combinations are controls that play them for you (a mouse has one pointer): SYSTEM held with
        # A (a soft reset) or B (initialise the RAM presets, WRITE to confirm); A and B, or C and D, pressed together (a
        # two-unit preset, then DATA); < held with CANCEL (the first page of a unit, its algorithm). > held with < (a whole
        # screen forward, the reverse back) is the pair of arrows on the LCD. COPY and SWAP, above, are armed, then two
        # Units are clicked: the unit's own way is EDIT, WRITE, one Unit button held while another is pressed, WRITE to
        # confirm.
        Row([Button("soft_reset", "SOFT RESET"), Button("init_ram", "INIT RAM"),
             Button("pair_ab", "A+B"), Button("pair_cd", "C+D"),
             Button("algorithm", "ALGORITHM")], own_grid=True),
    ]),
]

# Everything that enters, inputs left; what leaves stands down the right-hand
# edge. The taps are each unit's own output port, stereo on one polyphonic
# cable, for patching the units into the rest of the rack as well as into each
# other.
P.footer = [
    Row([Jack("in1", "IN 1"), Jack("in2", "IN 2"), Jack("in3", "IN 3"),
         Jack("in4", "IN 4"), Jack("pedal_in", "PEDAL"), Jack("fs_l_in", "FS L"),
         Jack("fs_r_in", "FS R")],
        y=118.6),
]

P.rail = Rail([Jack(n, t, ink="MINT") for n, t in
               [("out1", "OUT 1"), ("out2", "OUT 2"), ("out3", "OUT 3"), ("out4", "OUT 4"),
                ("tap_a", "TAP A"), ("tap_b", "TAP B"), ("tap_c", "TAP C"), ("tap_d", "TAP D")]],
              caption="OUT", cols=2, labels="above")

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

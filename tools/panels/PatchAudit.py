#!/usr/bin/env python3
"""The PatchAudit panel, declared once.

Running this file regenerates the artwork, the shared hardware, src/PanelTheme.hpp
and the previews. See ../../panelkit/README.md for the pipeline.

PatchAudit is a form you fill in and get a ruled result table back, so it is laid
out in plates rather than rows -- but the plates are drawn from the same palette,
with the same well borders and the same lime index tabs, so it reads as the same
family as Retroactive and Uncertainty Policy.

Everything below is a millimetre constant echoed into src/PanelTheme.hpp as
panel::NAME, and the browser display reads it from there. There is no second copy.
"""

import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
from panelkit import *   # noqa: E402

P = Panel(
    slug="PatchAudit",
    title="PATCHAUDIT",
    subtitle="PATCHSTORAGE RETURNS",
    form="FORM 4564",
    hp=26,
)

W = P.w                         # 132.08 mm
M = 5.2                         # side margin: 2.2 mm inside the felt block
IW = W - 2 * M                  # 121.68 mm of usable width

# --- the query block --------------------------------------------------------
# Its caption row carries the live status line rather than a fixed word: the
# block that holds the query is the one that reports on it. Drawn by
# BrowserDisplay, which reads STATUS_Y from the generated header.
QUERY_Y0 = 10.0
STATUS_Y = 13.6                 # caption baseline, same offset every block uses
SEARCH_Y, SEARCH_H = 15.6, 8.0
CHOICE_Y, CHOICE_H = 25.0, 7.4
CHOICE_W = (IW - 4.0) / 2       # 58.84 each, one 4 mm gutter
QUERY_Y1 = 34.0

# --- the result table, the centre of the panel ------------------------------
# Sized to a whole number of rows: a fractional row is sliced in half against the
# well's border, which looks like a rendering fault rather than a design.
ROWS_VISIBLE = 8
ROW_H = 8.6                     # one result: two lines of text
LIST_INSET = 0.8
LIST_Y = 35.6
LIST_H = ROWS_VISIBLE * ROW_H + 2 * LIST_INSET      # 70.4

# --- page nav and the action block ------------------------------------------
PAGE_Y, PAGE_H = 107.2, 6.5
NAV_W = 22.0
BAND_FOOTER = 114.6
BTN_Y, BTN_H = 115.4, 7.8       # ends at 123.2; the bottom screws start 123.61
BTN_W = (IW - 2.0) / 2
# The progress trough runs the width of the panel, so it has to be inset clear of
# the bottom screws, which occupy x 5.08-10.16 and 121.92-127.00 at this height.
PROG_Y, PROG_H = 124.4, 1.8
PROG_X = 11.0
PROG_W = W - 2 * PROG_X

P.band_footer = BAND_FOOTER
P.extra_blocks = [(QUERY_Y0, QUERY_Y1)]

P.plates = [
    Plate("search", M, SEARCH_Y, IW, SEARCH_H),
    Plate("category", M, CHOICE_Y, CHOICE_W, CHOICE_H, fill=BAND, r=1.0),
    Plate("sort", M + CHOICE_W + 4.0, CHOICE_Y, CHOICE_W, CHOICE_H, fill=BAND, r=1.0),
    Plate("list", M, LIST_Y, IW, LIST_H),
    Plate("prev", M, PAGE_Y, NAV_W, PAGE_H, fill=BAND, r=0.8),
    Plate("next", W - M - NAV_W, PAGE_Y, NAV_W, PAGE_H, fill=BAND, r=0.8),
    # The two actions sit on the footer band and wear the index tab, because on
    # this panel they are the sections -- there are no knobs to group.
    # Dark plates, because the button text the display draws on them is pale.
    Plate("import", M, BTN_Y, BTN_W, BTN_H, fill=GLASS_COLOUR, r=1.0, tab="LIME"),
    Plate("save", M + BTN_W + 2.0, BTN_Y, BTN_W, BTN_H, fill=GLASS_COLOUR, r=1.0, tab="LIME"),
    Plate("progress", PROG_X, PROG_Y, PROG_W, PROG_H, fill=GLASS_COLOUR, r=0.9,
          stroke=False),
]

# Echoed into src/PanelTheme.hpp so the live display and the artwork cannot
# disagree about where a field is.
P.metrics = dict(
    M=M, IW=IW,
    QUERY_Y0=QUERY_Y0, QUERY_Y1=QUERY_Y1, STATUS_Y=STATUS_Y,
    SEARCH_Y=SEARCH_Y, SEARCH_H=SEARCH_H,
    CHOICE_Y=CHOICE_Y, CHOICE_H=CHOICE_H, CHOICE_W=CHOICE_W,
    ROWS_VISIBLE=ROWS_VISIBLE, ROW_H=ROW_H, LIST_INSET=LIST_INSET,
    LIST_Y=LIST_Y, LIST_H=LIST_H,
    PAGE_Y=PAGE_Y, PAGE_H=PAGE_H, NAV_W=NAV_W,
    BAND_FOOTER=BAND_FOOTER,
    BTN_Y=BTN_Y, BTN_H=BTN_H, BTN_W=BTN_W,
    PROG_Y=PROG_Y, PROG_H=PROG_H, PROG_X=PROG_X, PROG_W=PROG_W,
)

if __name__ == "__main__":
    raise SystemExit(build(P, root=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))))

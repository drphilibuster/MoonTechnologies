#!/usr/bin/env python3
"""The Depreciation panel, declared once.

Running this file regenerates the artwork, the shared hardware, src/PanelTheme.hpp
and the previews. See ../../panelkit/README.md for the pipeline.

Depreciation is a Lexicon PCM 70 digital effects processor -- a 1986 reverb,
chorus, delay and resonant-chord machine -- running its own firmware: every
program, parameter and MIDI patch is Lexicon's own. It is FORM 4562 because
depreciation is value that decays on a schedule, which is also what a reverb
tail is.

The read-out is the machine. The real PCM 70 is a parameter matrix -- rows
and columns of cells, chosen and then adjusted -- and that is what the screen
is: nine columns by five rows, every cell the firmware's own name over its
printed value, and every cell a control. Hold one and drag up or down to adjust
it; the matrix follows the running program, so a cell the program does not use
is dimmed. Above it the 16-digit display and the headroom bar; between them the
preset strip (PRESET, FACTORY | USER, LOAD, STORE, BYPASS) and under the matrix
the setup strip (INPUT, VOLT TRIM, the two pads, CLK /, OUT LEVEL). Click a
choice to pick from its list, click LOAD / STORE / BYPASS to press them.

There are no knobs on the face. 45 trims and their plates were the panel's
width, and the screen now holds them in a third of it. There are no CV lanes:
they cost the machine more than they gave.
"""

import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
from panelkit import *   # noqa: E402

P = Panel(
    slug="Depreciation",
    title="DEPRECIATION",
    what="PCM 70 DIGITAL REVERB",
    form="FORM 4562",
    # Pinned because the plates inside the well are laid out against the width;
    # 18 is what the two jack rows and the footer solve to.
    hp=18,
    glass=None,
)

W = P.w                          # 91.44 mm
X0 = 5.0                         # the plates' left edge, inside the well
IW = W - 2 * X0                  # 81.44 mm

# --- inside the read-out well, top to bottom --------------------------------
DIG_Y, DIG_H = 10.6, 9.0         # the 16-digit display
STAT_Y = DIG_Y + DIG_H + 4.0     # baseline of the status line; the headroom bar sits right of it
PRE_Y, PRE_H = 25.0, 6.4         # the preset strip
MAT_Y, MAT_H = 32.2, 39.2        # the 9 x 5 parameter matrix
SET_Y, SET_H = 72.0, 7.4         # the setup strip
P.glass = Glass(h=SET_Y + SET_H + 0.8 - 9.8, inset=0.4, gap=0.4, fields=
    [Field("slot", plate="preset", cell=(0, 0), span=(1, 2), kind="select"),
     Field("regmode", plate="preset", cell=(0, 2), kind="toggle"),
     Field("load", plate="preset", cell=(0, 3), kind="button"),
     Field("store", plate="preset", cell=(0, 4), kind="button"),
     Field("bypass", plate="preset", cell=(0, 5), kind="button")]
    + [Field("p%d%d" % (r, c), plate="matrix", cell=(r, c), kind="value")
       for r in range(5) for c in range(9)]
    + [Field("input", plate="setup", cell=(0, 0), kind="value"),
       Field("trim", plate="setup", cell=(0, 1), kind="value"),
       Field("in_pad", plate="setup", cell=(0, 2), kind="toggle"),
       Field("out_pad", plate="setup", cell=(0, 3), kind="toggle"),
       Field("clk_div", plate="setup", cell=(0, 4), kind="select"),
       Field("out_level", plate="setup", cell=(0, 5), kind="value")])

P.plates = [
    Plate("preset", X0, PRE_Y, IW, PRE_H, fill=BAND, r=1.0, grid=(1, 6)),
    Plate("matrix", X0, MAT_Y, IW, MAT_H, r=0.8, grid=(5, 9)),
    Plate("setup", X0, SET_Y, IW, SET_H, fill=BAND, r=1.0, grid=(1, 6)),
]

# The dedicated inputs: the machine's MIDI-shaped controls, as CV and gates.
# The block goes uncaptioned -- it is the only one, and the height its caption
# would cost is the matrix's.
P.sections = [Section("", rows=[
    Row([Jack("mod", "MOD"), Jack("at", "AT"), Jack("note", "NOTE"), Jack("gate", "GATE")]),
    Row([Jack("sustain", "SUST"), Jack("soft", "SOFT"), Jack("clock", "CLOCK"), Jack("run", "RUN")]),
])]

P.footer = [
    Row([Jack("in", "IN"), Jack("in_r", "IN R"), Jack("pgm", "PGM"), Jack("bypass_cv", "BYP"),
         Jack("out_l", "OUT L", ink="MINT"), Jack("out_r", "OUT R", ink="MINT"),
         Jack("wet_l", "WET L", ink="MINT"), Jack("wet_r", "WET R", ink="MINT")], y=118.6),
]

P.metrics = dict(X0=X0, IW=IW, DIG_Y=DIG_Y, DIG_H=DIG_H, STAT_Y=STAT_Y,
                 MAT_X=X0, MAT_Y=MAT_Y, MAT_W=IW, MAT_H=MAT_H)

if __name__ == "__main__":
    raise SystemExit(build(P, root=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))))

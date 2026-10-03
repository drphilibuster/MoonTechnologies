"""Dependents -- FORM 8812. A chord claimed as harmonics of an inaudible root.

After Astrobear Music (Aspen Instruments): a waveshaper built from Chebyshev
polynomials excites exactly the harmonics you choose, so choosing them to be a
chord's just ratios makes a distortion play chords.

It has a read-out now, because what it does was invisible: the chord names were
tooltips and the twelve harmonic trims a row of identical knobs. The screen names
both chords, holds the root's scale and HOLD, and draws the harmonics as drawbars
over the spectrum actually sounding -- and every one of those is a control.
"""

import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
from panelkit import *   # noqa: E402

GLASS_H = 33.0

P = Panel(
    slug="Dependents",
    title="DEPENDENTS",
    what="CHORD WAVESHAPER",
    form="FORM 8812",
    density="compact",
    # The read-out: the root and its scale, the two chords by name, and the
    # twelve harmonics as drawbars -- the CUSTOM chord, which the named ones are
    # shorthand for. Every one of them is a control.
    glass=Glass(h=GLASS_H, grid=(7, 12), fields=[
        Field("root_field", cell=(0, 0), span=(1, 4), kind="value"),
        Field("root_quant", cell=(0, 4), span=(1, 2), kind="toggle"),
        Field("root_scale", cell=(0, 6), span=(1, 3), kind="select"),
        Field("norm",       cell=(0, 9), span=(1, 3), kind="toggle"),
        Field("chord_a",    cell=(1, 0), span=(1, 6), kind="select"),
        Field("chord_b",    cell=(1, 6), span=(1, 6), kind="select"),
    ] + [Field("h%d" % (n + 1), cell=(2, n), span=(5, 1), kind="value") for n in range(12)]),
)

P.sections = [
    # ROOT is the note nobody hears: two octaves under the chord by default, and
    # the module's whole joke is that it is the one being claimed for. It keeps
    # its knob because it is the one you play; QUANT, SCALE and the two chords
    # are on the read-out, where their names are.
    Section("QUALIFYING CHILD", rows=[
        Row([BigKnob("root", "ROOT", primary=True),
             Knob("morph", "MORPH"), Knob("tilt", "TILT")]),
        Row([Knob("drive", "CHORDS"), Knob("mix", "MIX"),
             Knob("level", "LEVEL")]),
    ]),

    # The identity only holds at unit amplitude, so whether the input is held
    # there is a switch (HOLD, on the read-out) and not a hidden decision. CHORD
    # A/B, CHORDS and LEVEL each get the same trim-over-jack CV pair as MORPH and
    # TILT -- a CV on LEVEL is an amplitude control with its own attenuverter,
    # which is what an internal VCA is.
    Section("SCHEDULE", rows=[
        Row(items=[Trim("morph_cv", "MORPH"), Trim("tilt_cv", "TILT"),
                   Trim("chord_a_cv", "CHORD A"), Trim("chord_b_cv", "CHORD B"),
                   Trim("drive_cv", "CHORDS"), Trim("level_cv", "LEVEL")], pair=True),
        Row([Jack("morph_in"), Jack("tilt_in"),
             Jack("chord_a_in"), Jack("chord_b_in"),
             Jack("drive_in"), Jack("level_in")]),
    ]),
]

P.footer = [
    Row([Jack("in", "IN"), Jack("voct", "V/OCT"),
         Jack("sub", "ROOT", ink="MINT"),
         Jack("out", "OUT", ink="MINT")], y=118.6),
]

if __name__ == "__main__":
    raise SystemExit(build(P, root=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))))

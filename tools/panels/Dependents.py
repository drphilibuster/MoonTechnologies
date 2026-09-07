"""Dependents -- FORM 8812. A chord claimed as harmonics of an inaudible root.

After Astrobear Music (Aspen Instruments): a waveshaper built from Chebyshev
polynomials excites exactly the harmonics you choose, so choosing them to be a
chord's just ratios makes a distortion play chords.
"""

import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
from panelkit import *   # noqa: E402

P = Panel(
    slug="Dependents",
    title="DEPENDENTS",
    form="FORM 8812",
    density="compact",
)

P.sections = [
    # ROOT is the note nobody hears: two octaves under the chord by default, and
    # the module's whole joke is that it is the one being claimed for.
    Section("QUALIFYING CHILD", rows=[
        Row([BigKnob("root", "ROOT", primary=True),
             Knob("chord_a", "CHORD A", steps=14),
             Knob("chord_b", "CHORD B", steps=14)]),
        Row([Knob("morph", "MORPH"), Knob("tilt", "TILT"),
             Knob("drive", "DRIVE"), Knob("mix", "MIX"),
             Knob("level", "LEVEL")]),
    ]),

    # The identity only holds at unit amplitude, so whether the input is held
    # there is a switch and not a hidden decision.
    Section("SCHEDULE", rows=[
        Row(items=[Trim("morph_cv", "MORPH"), Trim("tilt_cv", "TILT"),
                   Switch("norm", "HOLD")], pair=True),
        Row([Jack("morph_in"), Jack("tilt_in")]),
    ]),

    # One trim per harmonic, which is the CUSTOM chord. The presets above are
    # shorthand for particular settings of these; morph a named triad against
    # your own spectrum and the difference is audible as a chord becoming
    # something that has no name.
    Section("ITEMIZED", rows=[
        Row(span=[(0, 5, "HARMONIC 1 - 6")],
            items=[Trim("h%d" % n, "") for n in range(1, 7)]),
        Row(span=[(0, 5, "7 - 12")],
            items=[Trim("h%d" % n, "") for n in range(7, 13)]),
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

#!/usr/bin/env python3
"""The Reconciliation panel, declared once.

Running this file regenerates the artwork, the shared hardware, src/PanelTheme.hpp
and the previews. See ../../panelkit/README.md for the pipeline.

Reconciliation is a just-intonation quantizer. BASIS is which pitches exist --
Partch's tonality diamond and his 43-tone scale, one Otonality or Utonality
hexad out of that diamond, two of Erv Wilson's combination product sets, or the
raw harmonic series -- with NEXUS transposing the structure onto any of Partch's
six identities and PRIME pruning it down to whatever harmonic complexity you can
stand. RECONCILE is how a voltage becomes one of them: five published measures
of what "simpler" means, which disagree with each other, plus the plain nearest
neighbour to compare them against, and then what the chosen pitch does on the
way out. ALLOWANCES is what the CV inputs may take off the four controls that
are worth modulating.

BIAS wears the lime ring. At zero every rule collapses to NEAREST and the module
is an ordinary quantizer; turning it up is what lets a rule reach past the
nearest pitch for a better one, and it is the knob the module exists for.

The read-out is the point of the panel as much as the jacks are: it names the
ratio you are actually on -- 11/8, not "a bit flat of a tritone".

Compact density: three sections, four rows and a read-out. The width is solved,
not typed -- see panelkit/layout.py.
"""

import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
from panelkit import *   # noqa: E402

P = Panel(
    slug="Reconciliation",
    title="RECONCILIATION",
    # Schedule M-1 is the real reconciliation of income per books with income
    # per return -- two records of the same thing, made to agree. Which is what
    # a quantizer does to a voltage.
    form="SCHEDULE M-1",
    density="compact",
    glass=Glass(h=9.2),
)

P.sections = [
    # Which pitches exist. SET is the structure; NEXUS is which of Partch's six
    # identities it stands on, and for the hexad that is his numerary nexus
    # exactly -- an Otonality on 7 is the Otonality on 1 transposed by 7. UTONAL
    # is the mirror: Partch's major/minor. PRIME prunes by harmonic complexity,
    # and is the one control that means the same thing in every SET.
    Section("BASIS", rows=[
        Row([Knob("set", "SET", steps=6),
             Knob("nexus", "NEXUS", steps=6),
             Switch("utonal", "UTONAL"),
             Knob("prime", "PRIME", steps=4)]),
    ]),

    # How the choice is made. RULE is five published answers to "which of these
    # ratios is simpler" -- Euler's, Tenney's, Barlow's, Sethares' and the
    # adaptive one that measures from where it last landed rather than from the
    # root -- plus NEAREST, which is the control the other five are worth
    # comparing against. WINDOW is how far a rule may reach; BIAS is how hard it
    # pulls once it gets there.
    # Two runs of three, named rather than left to the solver: the first three
    # decide which pitch, the last three decide what happens to it on the way
    # out, and nothing about their widths says so. DEGREE transposes inside the
    # structure rather than moving it -- NEXUS changes which pitches exist,
    # DEGREE walks along the ones that do. HYST is how far past a boundary the
    # input has to go before the note may change, which is what stops a
    # forty-three tone scale chattering under a slow ramp.
    Section("RECONCILE", groups=(3, 3), rows=[
        Row([Knob("rule", "RULE", steps=6),
             BigKnob("bias", "BIAS", primary=True),
             Knob("window", "WINDOW"),
             Knob("degree", "DEGREE"),
             Knob("hyst", "HYST"),
             Knob("slew", "SLEW")]),
    ]),

    # What the CV inputs may take off the three controls worth modulating --
    # a trimpot directly over its jack, with the one label they share set
    # between the two so it cannot be read as naming the row above.
    Section("ALLOWANCES", rows=[
        Row([Trim("nexus_cv", "NEXUS"),
             Trim("window_cv", "WINDOW"),
             Trim("bias_cv", "BIAS"),
             Trim("degree_cv", "DEGREE")], pair=True),
        Row([Jack("nexus_in"), Jack("window_in"),
             Jack("bias_in"), Jack("degree_in")], silent=True),
    ]),
]

# The I/O row sits as low as the bottom screws allow: RACK_GRID_HEIGHT -
# RACK_GRID_WIDTH puts their top edge at 123.61 mm, so a jack collar centred
# below 118.6 would run under one. TRIG and DRIFT carry their own lamps: one
# is the note changing, the other is the tonal centre having wandered off the
# root, and both are things you want to see without patching them.
P.footer = [
    Row([Jack("pitch_in", "IN"),
         Jack("root_in", "ROOT"),
         Jack("reset_in", "RESET"),
         Jack("pitch_out", "OUT", ink="MINT"),
         Jack("trig_out", "TRIG", ink="MINT", light="trig_led"),
         Jack("purity_out", "PURITY", ink="MINT"),
         Jack("drift_out", "DRIFT", ink="MINT", light="drift_led")], y=118.6),
]

if __name__ == "__main__":
    raise SystemExit(build(P, root=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))))

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
ratio you are actually on -- 11/8, not "a bit flat of a tritone". And every
setting it names is set there: BASIS (SET, UTONAL, NEXUS, PRIME), the RULE,
DEGREE, HYST and SLEW are fields on the glass, so the face keeps only the two
knobs you play, the CV allowances and the jacks.

Compact density: two sections, a rail of outputs and a five-line read-out. The
width is solved, not typed -- see panelkit/layout.py.
"""

import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
from panelkit import *   # noqa: E402

P = Panel(
    slug="Reconciliation",
    title="RECONCILIATION",
    what="JUST-INTONATION QUANTIZER",
    # Schedule M-1 is the real reconciliation of income per books with income
    # per return -- two records of the same thing, made to agree. Which is what
    # a quantizer does to a voltage.
    form="SCHEDULE M-1",
    density="compact",
    # Five lines. The top one is the ratio you are on, and the bottom one how
    # many pitches survived and the TRIG and DRIFT lamps; those two are only
    # read. Every setting on the three between is a field. The second line is
    # BASIS and the rule: SET (click for the list), O/U (click flips the
    # polarity), the NEXUS identity and the RULE. The next two are the settings
    # that tune it -- PRIME and DEGREE, HYST and SLEW -- PRIME a list, the rest
    # held and dragged.
    glass=Glass(h=24.0, grid=(5, 8), fields=[
        Field("set",    cell=(1, 0), span=(1, 3), kind="select"),
        Field("utonal", cell=(1, 3), span=(1, 1), kind="toggle"),
        Field("nexus",  cell=(1, 4), span=(1, 1), kind="select"),
        Field("rule",   cell=(1, 5), span=(1, 3), kind="select"),
        Field("prime",  cell=(2, 0), span=(1, 4), kind="select"),
        Field("degree", cell=(2, 4), span=(1, 4), kind="value"),
        Field("hyst",   cell=(3, 0), span=(1, 4), kind="value"),
        Field("slew",   cell=(3, 4), span=(1, 4), kind="value"),
    ]),
)

P.sections = [
    # How hard the choice is made. BIAS is how hard a rule pulls once it gets
    # there, and WINDOW is how far it may reach -- the two you play, so they
    # keep knobs. Which pitches exist (SET, NEXUS, UTONAL, PRIME) and which rule
    # chooses among them are on the read-out above, where their names already
    # were; so are DEGREE, HYST and SLEW. DEGREE transposes inside the
    # structure rather than moving it -- NEXUS changes which pitches exist,
    # DEGREE walks along the ones that do. HYST is how far past a boundary the
    # input has to go before the note may change, which is what stops a
    # forty-three tone scale chattering under a slow ramp.
    Section("RECONCILE", rows=[
        Row([BigKnob("bias", "BIAS", primary=True),
             Knob("window", "WINDOW")]),
    ]),

    # What the CV inputs may take off the four controls worth modulating --
    # a trimpot directly over its jack, with the one label they share set
    # between the two so it cannot be read as naming the row above.
    Section("ALLOWANCES", rows=[
        Row([Trim("nexus_cv", "NEXUS"),
             Trim("window_cv", "WINDOW")], pair=True),
        Row([Jack("nexus_in"), Jack("window_in")], silent=True),
        Row([Trim("bias_cv", "BIAS"),
             Trim("degree_cv", "DEGREE")], pair=True),
        Row([Jack("bias_in"), Jack("degree_in")], silent=True),
    ]),
]

# The outputs stand down the right-hand edge, where seven jacks side by side on
# the band would have been what set the panel's width. Their names stand over
# them: a rail's side labels would cost the panel two HP. TRIG and DRIFT used
# to carry lamps on their labels; a lamp beside a name over a rail jack runs
# off the rail, so the two lamps are drawn on the read-out's bottom line
# instead, which is where you are looking anyway.
P.rail = Rail([Jack("pitch_out", "OUT", ink="MINT"),
               Jack("trig_out", "TRIG", ink="MINT"),
               Jack("purity_out", "PURITY", ink="MINT"),
               Jack("drift_out", "DRIFT", ink="MINT")],
              caption="OUT", labels="above")

# The inputs sit as low as the bottom screws allow: RACK_GRID_HEIGHT -
# RACK_GRID_WIDTH puts their top edge at 123.61 mm, so a jack collar centred
# below 118.6 would run under one.
P.footer = [
    Row([Jack("pitch_in", "IN"),
         Jack("root_in", "ROOT"),
         Jack("reset_in", "RESET")], y=118.6),
]

if __name__ == "__main__":
    raise SystemExit(build(P, root=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))))

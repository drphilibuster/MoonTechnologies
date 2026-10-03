#!/usr/bin/env python3
"""The Dividend panel, declared once.

Running this file regenerates the artwork, the shared hardware, src/PanelTheme.hpp
and the previews. See ../../panelkit/README.md for the pipeline.

Dividend is a pulsar-synthesis VCO (Curtis Roads, "Microsound", ch. 4): a train
of pulsarets, each one a few cycles of a waveform under an envelope, paid out at
the fundamental rate. PAYOUT is how often and at what formant the pulsarets are
issued; DISTRIBUTION is what each pulsaret is made of, and whether the L and R
channels take turns receiving them; WITHHOLDING is which pulsars are kept back
-- in bursts, at random -- and what the CV inputs may take off each control.
The FREQ knob is the one control you reach for, so it wears the lime ring.

Every setting is on the read-out, and set there: FREQ and FORMANT are its top
two lines as well as the two knobs on the face, and everything else --
TRACK, FM, WAVE, WINDOW, CYCLES, the burst mask, L/R, FINE and PROB -- lives
only on the glass. The face keeps the two knobs, the CV allowances and the
jacks.

Compact density: two sections, a rail of outputs and a five-line read-out.
The width is solved, not typed -- see panelkit/layout.py.
"""

import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
from panelkit import *   # noqa: E402

P = Panel(
    slug="Dividend",
    title="DIVIDEND",
    what="PULSAR VCO",
    form="FORM 1099-DIV",
    density="compact",
    # Five lines, and every value on them is a field. The top two are PAYOUT:
    # the fundamental and whether the formant tracks it (TRACK / ABS), the
    # formant -- with the duty ratio d/p it makes, which is only read -- and the
    # FM law. The next two are DISTRIBUTION and the burst mask: WAVE, WINDOW
    # and CYCLES; ON, OFF and L/R. The last is FINE and PROB, and the HELD
    # lamp. WAVE and WINDOW were six-position knobs and WINDOW's setting was
    # shown nowhere; on the glass both are their names, and a click lists them.
    glass=Glass(h=24.0, grid=(5, 6), fields=[
        Field("freq_field",    cell=(0, 0), span=(1, 4), kind="value"),
        Field("track",         cell=(0, 4), span=(1, 2), kind="toggle"),
        Field("formant_field", cell=(1, 0), span=(1, 4), kind="value"),
        Field("fm_mode",       cell=(1, 4), span=(1, 2), kind="toggle"),
        Field("wave",          cell=(2, 0), span=(1, 2), kind="select"),
        Field("window",        cell=(2, 2), span=(1, 2), kind="select"),
        Field("cycles",        cell=(2, 4), span=(1, 2), kind="value"),
        Field("burst_on",      cell=(3, 0), span=(1, 2), kind="value"),
        Field("burst_off",     cell=(3, 2), span=(1, 2), kind="value"),
        Field("stereo",        cell=(3, 4), span=(1, 2), kind="toggle"),
        Field("fine",          cell=(4, 0), span=(1, 2), kind="value"),
        Field("prob",          cell=(4, 2), span=(1, 2), kind="value"),
    ]),
)

P.sections = [
    # The pulsar rate and the pulsaret's formant: the two knobs you play, so
    # they keep their knobs as well as their fields on the read-out.
    Section("PAYOUT", rows=[
        Row([BigKnob("freq", "FREQ", primary=True),
             BigKnob("formant", "FORMANT")]),
    ]),

    # What the CV inputs may take off each control -- a trimpot directly over
    # its jack, with the one label they share set between the two of them so
    # it cannot be read as naming the row above. Two by two: the pairs stand
    # in the height the knobs that moved onto the glass left behind, which is
    # what lets the outputs take a rail and the panel lose three HP.
    Section("WITHHOLDING", rows=[
        Row([Trim("fm_cv", "FM"),
             Trim("fmt_cv", "FMT")], pair=True),
        Row([Jack("fm_in"), Jack("fmt_in")], silent=True),
        Row([Trim("prob_cv", "PROB"),
             Trim("burst_cv", "BURST")], pair=True),
        Row([Jack("prob_in"), Jack("burst_in")], silent=True),
    ]),
]

# The outputs stand down the right-hand edge, where six jacks side by side on
# the band were what set the panel's width; their names stand over them.
P.rail = Rail([Jack("out_l", "OUT L", ink="MINT"),
               Jack("out_r", "OUT R", ink="MINT"),
               Jack("trig", "TRIG", ink="MINT"),
               Jack("env", "ENV", ink="MINT")],
              caption="OUT", labels="above")

# The inputs sit as low as the bottom screws allow: RACK_GRID_HEIGHT -
# RACK_GRID_WIDTH puts their top edge at 123.61 mm, so a jack collar centred
# below 118.6 would run under one.
P.footer = [
    Row([Jack("voct", "V/OCT"),
         Jack("sync", "SYNC")], y=118.6),
]

if __name__ == "__main__":
    raise SystemExit(build(P, root=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))))

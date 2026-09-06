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

Compact density: three sections, five rows of controls and a read-out.
The width is solved, not typed -- see panelkit/layout.py.
"""

import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
from panelkit import *   # noqa: E402

P = Panel(
    slug="Dividend",
    title="DIVIDEND",
    form="FORM 1099-DIV",
    density="compact",
    glass=Glass(h=9.2),
)

P.sections = [
    # The pulsar rate and the pulsaret's formant. TRACK ties the formant to the
    # fundamental (a ratio) or lets it stand on its own (Hz); FM picks the
    # exponential or the linear through-zero law for the FM input. Three knobs
    # then two switches: the row changes gear once, so the solver spaces each
    # run evenly within itself and puts the slack in the gutter between them.
    Section("PAYOUT", rows=[
        Row([BigKnob("freq", "FREQ", primary=True),
             Knob("fine", "FINE"),
             BigKnob("formant", "FORMANT"),
             Switch("track", "TRACK"),
             Switch("fm_mode", "FM")]),
    ]),

    # What each pulsaret is made of: the waveform, the envelope that windows it
    # and how many cycles it carries. WAVE and WINDOW are six-position
    # selectors, so they wear detent rings -- a knob that steps has to look
    # like one. L/R hands alternate pulsarets to the two outputs.
    Section("DISTRIBUTION", rows=[
        Row([Knob("wave", "WAVE", steps=6),
             Knob("window", "WINDOW", steps=6),
             Knob("cycles", "CYCLES"),
             Switch("stereo", "L/R")]),
    ]),

    # Which pulsars are kept back. ON / OFF is burst masking (n issued, m
    # withheld); PROB is stochastic masking. HELD lights for every pulsar
    # withheld. Below, what the CV inputs may take off each control -- a
    # trimpot directly over its jack, with the one label they share set
    # between the two of them so it cannot be read as naming the row above.
    Section("WITHHOLDING", rows=[
        Row([Knob("burst_on", "ON"),
             Knob("burst_off", "OFF"),
             Knob("prob", "PROB"),
             Light("held", "HELD", ink="LIME", size=5.8)]),
        Row([Trim("fm_cv", "FM"),
             Trim("fmt_cv", "FMT"),
             Trim("prob_cv", "PROB"),
             Trim("burst_cv", "BURST")], pair=True),
        Row([Jack("fm_in"), Jack("fmt_in"),
             Jack("prob_in"), Jack("burst_in")], silent=True),
    ]),
]

# The I/O row sits as low as the bottom screws allow: RACK_GRID_HEIGHT -
# RACK_GRID_WIDTH puts their top edge at 123.61 mm, so a jack collar centred
# below 118.6 would run under one.
P.footer = [
    Row([Jack("voct", "V/OCT"),
         Jack("sync", "SYNC"),
         Jack("out_l", "OUT L", ink="MINT"),
         Jack("out_r", "OUT R", ink="MINT"),
         Jack("trig", "TRIG", ink="MINT"),
         Jack("env", "ENV", ink="MINT")], y=118.6),
]

if __name__ == "__main__":
    raise SystemExit(build(P, root=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))))

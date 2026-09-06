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

Compact density: three sections, five rows of controls and a read-out at 16 HP.
"""

import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
from panelkit import *   # noqa: E402

P = Panel(
    slug="Dividend",
    title="DIVIDEND",
    form="FORM 1099-DIV",
    hp=16,
    density="compact",
    glass=Glass(h=9.2),
)

C5 = P.cols(5, 10.5)      # the big knob's well clears the frame
C4 = P.cols(4, 10.0)      # 10.00, 30.43, 50.85, 71.28
C6 = P.cols(6, 8.0)       # 8.00, 21.06, 34.11, 47.17, 60.22, 73.28

P.sections = [
    # The pulsar rate and the pulsaret's formant. TRACK ties the formant to the
    # fundamental (a ratio) or lets it stand on its own (Hz); FM picks the
    # exponential or the linear through-zero law for the FM input.
    Section("PAYOUT", rows=[
        Row([BigKnob("freq", C5[0], "FREQ", primary=True),
             Knob("fine", C5[1], "FINE"),
             BigKnob("formant", C5[2], "FORMANT"),
             Switch("track", C5[3], "TRACK"),
             Switch("fm_mode", C5[4], "FM")]),
    ]),

    # What each pulsaret is made of: the waveform, the envelope that windows it
    # and how many cycles it carries. L/R hands alternate pulsarets to the two
    # outputs -- channel masking, the stereo pulsar.
    Section("DISTRIBUTION", rows=[
        Row([Knob("wave", C4[0], "WAVE"),
             Knob("window", C4[1], "WINDOW"),
             Knob("cycles", C4[2], "CYCLES"),
             Switch("stereo", C4[3], "L/R")]),
    ]),

    # Which pulsars are kept back. ON / OFF is burst masking (n issued, m
    # withheld); PROB is stochastic masking. HELD lights for every pulsar
    # withheld. Below, what the CV inputs may take off each control -- a
    # trimpot directly over its jack, one label serving both.
    Section("WITHHOLDING", rows=[
        Row([Knob("burst_on", C4[0], "ON"),
             Knob("burst_off", C4[1], "OFF"),
             Knob("prob", C4[2], "PROB"),
             Light("held", C4[3], "HELD", ink="LIME", size=5.8)]),
        Row([Trim("fm_cv", C4[0], "FM"),
             Trim("fmt_cv", C4[1], "FMT"),
             Trim("prob_cv", C4[2], "PROB"),
             Trim("burst_cv", C4[3], "BURST")], label_side="above"),
        Row([Jack("fm_in", C4[0]), Jack("fmt_in", C4[1]),
             Jack("prob_in", C4[2]), Jack("burst_in", C4[3])], silent=True),
    ]),
]

# The I/O row sits as low as the bottom screws allow: RACK_GRID_HEIGHT -
# RACK_GRID_WIDTH puts their top edge at 123.61 mm, so a jack collar centred
# below 118.6 would run under one.
P.footer = [
    Row([Jack("voct", C6[0], "V/OCT"),
         Jack("sync", C6[1], "SYNC"),
         Jack("out_l", C6[2], "OUT L", ink="MINT"),
         Jack("out_r", C6[3], "OUT R", ink="MINT"),
         Jack("trig", C6[4], "TRIG", ink="MINT"),
         Jack("env", C6[5], "ENV", ink="MINT")], y=118.6),
]

if __name__ == "__main__":
    raise SystemExit(build(P, root=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))))

#!/usr/bin/env python3
"""The Kickback panel, declared once.

Running this file regenerates the artwork, the shared hardware, src/PanelTheme.hpp
and the previews. See ../../panelkit/README.md for the pipeline.

Kickback is the Modular in a Week drum bank: eight lo-fi percussion circuits
from MiaW's Day 9 folder -- BaSnaHi's kick, snare and hat, the SmurfDrum, the
TomTomTom twin-T, the XORbell, the Percussive Noise Voice and the Tiny Dazzler
it descends from -- filed on one FORM 1099-NEC. Every voice is a column: its
name sits on its TRIG jack in the strike row (and lights when it is struck),
its knobs stack beneath it, and its OUT sits directly below in the footer band.
The two controls that reach every voice -- how much the ACCENT CV raises a
strike, and the MIX level -- flank the strike row at the panel's edges, over
the jacks they belong to.

Ten columns at 30 HP, regular density: two sections of four rows between them
and a footer of ten jacks fill the face without anything being squeezed.
"""

import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
from panelkit import *   # noqa: E402

P = Panel(
    slug="Kickback",
    title="KICKBACK",
    form="FORM 1099-NEC",
    hp=30,
)

# One column per voice, ACCENT at the far left and MIX at the far right, so the
# footer carries the same ten columns as the section above it.
C = P.cols(10, 8.0)       # 8.00, 23.16, 38.31, 53.47, 68.62, 83.78, 98.93, 114.09, 129.24, 144.40
ACC, KICK, SNARE, HAT, SMURF, TOM, BELL, NOISE, DAZZ, MIX = C

P.sections = [
    # The strike row: one TRIG per voice, named for the voice and lit when it
    # fires, with the two knobs that reach every voice at the ends -- ACCENT
    # over the ACCENT jack's column, LEVEL over the MIX.
    Section("PAYER", rows=[
        Row([Jack("kick_trig", KICK, "KICK", ink="PAPER", light="kick_led"),
             Jack("snare_trig", SNARE, "SNARE", ink="PAPER", light="snare_led"),
             Jack("hat_trig", HAT, "HAT", ink="PAPER", light="hat_led"),
             Jack("smurf_trig", SMURF, "SMURF", ink="PAPER", light="smurf_led"),
             Jack("tom_trig", TOM, "TOM", ink="PAPER", light="tom_led"),
             Jack("bell_trig", BELL, "BELL", ink="PAPER", light="bell_led"),
             Jack("noise_trig", NOISE, "NOISE", ink="PAPER", light="noise_led"),
             Jack("dazzler_trig", DAZZ, "DAZZLER", ink="PAPER", light="dazzler_led"),
             Knob("accent", ACC, "ACCENT"),
             Knob("level", MIX, "LEVEL")]),
    ]),

    # Box 1 of the 1099-NEC. One row of pitch -- or, for the noise voices, what
    # stands in for it -- on the big knobs, one row of decays with every voice's
    # DECAY on the same line, and one row of whatever else the circuit had a
    # pot for.
    Section("NONEMPLOYEE COMPENSATION", rows=[
        Row([BigKnob("kick_pitch", KICK, "PITCH"),
             BigKnob("snare_pitch", SNARE, "PITCH"),
             BigKnob("hat_tone", HAT, "TONE"),
             BigKnob("smurf_pitch", SMURF, "PITCH"),
             BigKnob("tom_pitch", TOM, "PITCH"),
             BigKnob("bell_pitch", BELL, "PITCH"),
             BigKnob("noise_tone", NOISE, "TONE"),
             Switch("dazzler_kit", DAZZ, "KIT")]),
        # Every decay on one line.
        Row([Knob("kick_decay", KICK, "DECAY"),
             Knob("snare_decay", SNARE, "DECAY"),
             Knob("hat_decay", HAT, "DECAY"),
             Knob("smurf_decay", SMURF, "DECAY"),
             Knob("tom_decay", TOM, "DECAY"),
             Knob("bell_decay", BELL, "DECAY"),
             Knob("noise_decay", NOISE, "DECAY"),
             Knob("dazzler_decay", DAZZ, "DECAY")]),
        # The third pot each circuit has, where it has one. The noise voice's
        # vactrol takes a CV here instead; it leads the row so the row's label
        # side is "above" and the jack's name clears the decays over it.
        Row([Jack("noise_cv", NOISE, "DEC CV"),
             Knob("kick_drive", KICK, "DRIVE"),
             Knob("snare_snap", SNARE, "SNAP"),
             Knob("smurf_sweep", SMURF, "SWEEP"),
             Switch3("tom_range", TOM, "RANGE"),
             Knob("bell_timbre", BELL, "TIMBRE"),
             Knob("dazzler_crackle", DAZZ, "CRACKLE")]),
    ]),
]

# The I/O row sits as low as the bottom screws allow: RACK_GRID_HEIGHT -
# RACK_GRID_WIDTH puts their top edge at 123.61 mm, so a jack collar centred
# below 118.6 would run under one. ACCENT in on the left; each voice's OUT
# under its own column; the MIX on the right.
P.footer = [
    Row([Jack("accent_in", ACC, "ACCENT"),
         Jack("kick_out", KICK, "OUT", ink="MINT"),
         Jack("snare_out", SNARE, "OUT", ink="MINT"),
         Jack("hat_out", HAT, "OUT", ink="MINT"),
         Jack("smurf_out", SMURF, "OUT", ink="MINT"),
         Jack("tom_out", TOM, "OUT", ink="MINT"),
         Jack("bell_out", BELL, "OUT", ink="MINT"),
         Jack("noise_out", NOISE, "OUT", ink="MINT"),
         Jack("dazzler_out", DAZZ, "OUT", ink="MINT"),
         Jack("mix_out", MIX, "MIX", ink="MINT")], y=118.6),
]

if __name__ == "__main__":
    raise SystemExit(build(P, root=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))))

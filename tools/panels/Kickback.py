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
)

# One column per voice, ACCENT at the far left and MIX at the far right, so the
# footer carries the same ten columns as the section above it. The columns are
# solved, not typed: every row declares which of the ten it stands in, and the
# solver makes them as wide as the widest thing any row puts there.
ACC, KICK, SNARE, HAT, SMURF, TOM, BELL, NOISE, DAZZ, MIX = range(10)

P.sections = [
    # The strike row: one TRIG per voice, named for the voice and lit when it
    # fires, with the two knobs that reach every voice at the ends -- ACCENT
    # over the ACCENT jack's column, LEVEL over the MIX.
    Section("PAYER", rows=[
        Row([Knob("accent", "ACCENT", col=ACC),
             Jack("kick_trig", "KICK", ink="PAPER", light="kick_led", col=KICK),
             Jack("snare_trig", "SNARE", ink="PAPER", light="snare_led", col=SNARE),
             Jack("hat_trig", "HAT", ink="PAPER", light="hat_led", col=HAT),
             Jack("smurf_trig", "SMURF", ink="PAPER", light="smurf_led", col=SMURF),
             Jack("tom_trig", "TOM", ink="PAPER", light="tom_led", col=TOM),
             Jack("bell_trig", "BELL", ink="PAPER", light="bell_led", col=BELL),
             Jack("noise_trig", "NOISE", ink="PAPER", light="noise_led", col=NOISE),
             Jack("dazzler_trig", "DAZZLE", ink="PAPER", light="dazzler_led", col=DAZZ),
             Knob("level", "LEVEL", col=MIX)]),
    ]),

    # Box 1 of the 1099-NEC. One row of pitch -- or, for the noise voices, what
    # stands in for it -- on the big knobs, one row of decays with every voice's
    # DECAY on the same line, and one row of whatever else the circuit had a
    # pot for.
    Section("NONEMPLOYEE COMPENSATION", rows=[
        Row([BigKnob("kick_pitch", "PITCH", col=KICK),
             BigKnob("snare_pitch", "PITCH", col=SNARE),
             BigKnob("hat_tone", "TONE", col=HAT),
             BigKnob("smurf_pitch", "PITCH", col=SMURF),
             BigKnob("tom_pitch", "PITCH", col=TOM),
             BigKnob("bell_pitch", "PITCH", col=BELL),
             BigKnob("noise_tone", "TONE", col=NOISE),
             Switch("dazzler_kit", "KIT", col=DAZZ)]),
        # Every decay on one line.
        Row([Knob("kick_decay", "DECAY", col=KICK),
             Knob("snare_decay", "DECAY", col=SNARE),
             Knob("hat_decay", "DECAY", col=HAT),
             Knob("smurf_decay", "DECAY", col=SMURF),
             Knob("tom_decay", "DECAY", col=TOM),
             Knob("bell_decay", "DECAY", col=BELL),
             Knob("noise_decay", "DECAY", col=NOISE),
             Knob("dazzler_decay", "DECAY", col=DAZZ)]),
        # The third pot each circuit has, where it has one. The noise voice's
        # vactrol takes a CV here instead, in its own voice's column.
        Row([Knob("kick_drive", "DRIVE", col=KICK),
             Knob("snare_snap", "SNAP", col=SNARE),
             Knob("smurf_sweep", "SWEEP", col=SMURF),
             Switch3("tom_range", "RANGE", col=TOM),
             Knob("bell_timbre", "TIMBRE", col=BELL),
             Jack("noise_cv", "DEC CV", col=NOISE),
             Knob("dazzler_crackle", "CRACKLE", col=DAZZ)]),
    ]),
]

# The I/O row sits as low as the bottom screws allow: RACK_GRID_HEIGHT -
# RACK_GRID_WIDTH puts their top edge at 123.61 mm, so a jack collar centred
# below 118.6 would run under one. ACCENT in on the left; each voice's OUT
# under its own column; the MIX on the right.
P.footer = [
    Row([Jack("accent_in", "ACCENT", col=ACC),
         Jack("kick_out", "OUT", ink="MINT", col=KICK),
         Jack("snare_out", "OUT", ink="MINT", col=SNARE),
         Jack("hat_out", "OUT", ink="MINT", col=HAT),
         Jack("smurf_out", "OUT", ink="MINT", col=SMURF),
         Jack("tom_out", "OUT", ink="MINT", col=TOM),
         Jack("bell_out", "OUT", ink="MINT", col=BELL),
         Jack("noise_out", "OUT", ink="MINT", col=NOISE),
         Jack("dazzler_out", "OUT", ink="MINT", col=DAZZ),
         Jack("mix_out", "MIX", ink="MINT", col=MIX)], y=118.6),
]

if __name__ == "__main__":
    raise SystemExit(build(P, root=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))))

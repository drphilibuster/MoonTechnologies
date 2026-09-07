#!/usr/bin/env python3
"""The Racketeer panel, declared once.

Running this file regenerates the artwork, the shared hardware, src/PanelTheme.hpp
and the previews. See ../../panelkit/README.md for the pipeline.

Racketeer is Wolfgang Spahn's PB701 Electric Intonarumori (after Urs Gaudenz's
Chaos Looper): a PT2399 delay chip run as a self-sustaining noise voice. RACKET
is the loop itself -- ECHO is the one knob you reach for, so it wears the ring;
TIME and CUTOFF flank it, and the second row is what feeds the loop and how it
smears. ENFORCEMENT is the muscle: the three pushbuttons of the original and
the three mini switches, plus the chopper that presses MUTE for you. SKIM is
what the CV inputs may take off each control -- a trimpot directly over its
jack, one label serving both -- with the three gate jacks that press the
buttons beside them. The read-out under the masthead is the chip's condition:
the delay it is running and the sample rate it has dropped to in order to run
it.

Compact density: five rows of hardware and a read-out at 16 HP, one row big
knobs and one row switches, leaves no room for the regular scale.
"""

import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
from panelkit import *   # noqa: E402

P = Panel(
    slug="Racketeer",
    title="RACKETEER",
    form="FORM 211",
    density="compact",
    glass=Glass(h=8.8),
)

P.sections = [
    # The loop. ECHO is the feedback -- past noon it self-oscillates, which is
    # the whole point. TIME is the chip's R control, CUTOFF the low-pass pot.
    # The row under them is what goes into the loop (DRIVE on the input, SEED
    # the noise floor that keeps it alive), how the time CV smears (LAG, the
    # optocoupler) and how fast the chopper runs. RES and THRESH are trims: a
    # resonance the original never had, and the level the GATE output fires at.
    Section("RACKET", caption_light="loop", rows=[
        Row([BigKnob("time", "TIME"),
             BigKnob("echo", "ECHO", primary=True),
             BigKnob("cutoff", "CUTOFF")]),
        # Secondary controls, so trimpots: set and left, like a service panel.
        # Six of them under three big knobs, on a grid of their own -- lining a
        # trim up with a knob above it would be a coincidence, not a meaning.
        Row([Trim("lag", "LAG"),
             Trim("drive", "DRIVE"),
             Trim("seed", "SEED"),
             Trim("rate", "RATE"),
             Trim("res", "RES"),
             Trim("thresh", "THRESH")], own_grid=True),
    ]),

    # The muscle. The three pushbuttons of the original (each also pressed by
    # the gate jack of the same name in SKIM), the three mini switches given
    # names, and the chopper: the mute button pushed rhythmically by a square
    # LFO, its light ticking with it.
    Section("ENFORCEMENT", rows=[
        Row([Bezel("noise", "NOISE"),
             Bezel("boost", "BOOST"),
             Bezel("mute", "MUTE"),
             Switch("chop", "CHOP", light="chop_led"),
             Switch("pol", "POL"),
             Switch("filt", "FILTER"),
             Switch("range", "RANGE")]),
    ]),

    # What the CV inputs may take off each control: trimpot directly over its
    # jack, the label they share set between the two -- the paired idiom, four
    # times across. The three gate jacks that press the buttons sit beside them,
    # named for the button each one presses, and own nothing below, so they keep
    # their names above their heads.
    Section("SKIM", rows=[
        Row([Trim("time_cv", "TIME"),
             Trim("echo_cv", "ECHO"),
             Trim("cutoff_cv", "CUTOFF"),
             Trim("rate_cv", "RATE"),
             Trim("res_cv", "RES"),
             Trim("lag_cv", "LAG")], pair=True),
        Row([Jack("time_in"), Jack("echo_in"),
             Jack("cutoff_in"), Jack("rate_in"),
             Jack("res_in"), Jack("lag_in")]),
    ]),
]

# The audio row sits as low as the bottom screws allow (their top edge is at
# 123.61 mm). IN on the left, everything that leaves the module to its right.
# The three gate jacks that press the buttons upstairs moved down here. They
# are external control coming in, which is what this band is for on every other
# panel in the family, and putting them on a row that already existed is what
# let SKIM grow two more CV pairs without the panel growing at all: a fourth row
# up there overran the face by a millimetre however wide the panel was made, and
# nine jacks on one row up there cost four HP.
#
# Inputs left, outputs right, as everywhere else.
P.footer = [
    Row([Jack("in", "IN"),
         Jack("noise_in", "NOISE"),
         Jack("boost_in", "BOOST"),
         Jack("mute_in", "MUTE"),
         Jack("env_out", "ENV", ink="MINT"),
         Jack("gate_out", "GATE", ink="MINT"),
         Jack("dirty_out", "DIRTY", ink="MINT"),
         Jack("out", "OUT", ink="MINT")], y=118.6),
]

if __name__ == "__main__":
    raise SystemExit(build(P, root=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))))

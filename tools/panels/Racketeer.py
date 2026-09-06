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
    hp=16,
    density="compact",
    glass=Glass(h=8.8),
)

C6 = P.cols(6, 8.0)       # trims: their wells clear the frame at 8
C7 = P.cols(7, 8.0)       # 8.00 .. 73.28, the centre column on the panel axis
C5 = P.cols(5, 8.0)       # 8.00, 24.32, 40.64, 56.96, 73.28
C3 = P.cols(3, 14.0)      # 14.00, 40.64, 67.28
# C7[3] == C5[2] == C3[1] == the panel centre: ECHO, the CHOP switch and the
# RATE CV pair all share one axis.

P.sections = [
    # The loop. ECHO is the feedback -- past noon it self-oscillates, which is
    # the whole point. TIME is the chip's R control, CUTOFF the low-pass pot.
    # The row under them is what goes into the loop (DRIVE on the input, SEED
    # the noise floor that keeps it alive), how the time CV smears (LAG, the
    # optocoupler) and how fast the chopper runs. RES and THRESH are trims: a
    # resonance the original never had, and the level the GATE output fires at.
    Section("RACKET", caption_light="loop", rows=[
        Row([BigKnob("time", C3[0], "TIME"),
             BigKnob("echo", C3[1], "ECHO", primary=True),
             BigKnob("cutoff", C3[2], "CUTOFF")]),
        # Secondary controls, so trimpots: set and left, like a service panel.
        Row([Trim("lag", C6[0], "LAG"),
             Trim("drive", C6[1], "DRIVE"),
             Trim("seed", C6[2], "SEED"),
             Trim("rate", C6[3], "RATE"),
             Trim("res", C6[4], "RES"),
             Trim("thresh", C6[5], "THRESH")]),
    ]),

    # The muscle. The three pushbuttons of the original (each also pressed by
    # the gate jack of the same name in SKIM), the three mini switches given
    # names, and the chopper: the mute button pushed rhythmically by a square
    # LFO, its light ticking with it.
    Section("ENFORCEMENT", rows=[
        Row([Bezel("noise", C7[0], "NOISE"),
             Bezel("boost", C7[1], "BOOST"),
             Bezel("mute", C7[2], "MUTE"),
             # CHOP takes the centre column so its light, which sits to the
             # right of the label, stays inside the block.
             Switch("chop", C7[3], "CHOP", light="chop_led"),
             Switch("pol", C7[4], "POL"),
             Switch("filt", C7[5], "FILTER"),
             Switch("range", C7[6], "RANGE")]),
    ]),

    # What the CV inputs may take off each control: trimpot directly over its
    # jack, one label serving both -- the paired idiom, four times across. The
    # three gate jacks that press the buttons sit beside them, named for the
    # button each one presses.
    Section("SKIM", rows=[
        Row([Trim("time_cv", C7[0], "TIME"),
             Trim("echo_cv", C7[1], "ECHO"),
             Trim("cutoff_cv", C7[2], "CUTOFF"),
             Trim("rate_cv", C7[3], "RATE")], label_side="above"),
        Row([Jack("time_in", C7[0]), Jack("echo_in", C7[1]),
             Jack("cutoff_in", C7[2]), Jack("rate_in", C7[3]),
             Jack("noise_in", C7[4], "NOISE"),
             Jack("boost_in", C7[5], "BOOST"),
             Jack("mute_in", C7[6], "MUTE")]),
    ]),
]

# The audio row sits as low as the bottom screws allow (their top edge is at
# 123.61 mm). IN on the left, everything that leaves the module to its right.
P.footer = [
    Row([Jack("in", C5[0], "IN"),
         Jack("env_out", C5[1], "ENV", ink="MINT"),
         Jack("gate_out", C5[2], "GATE", ink="MINT"),
         Jack("dirty_out", C5[3], "DIRTY", ink="MINT"),
         Jack("out", C5[4], "OUT", ink="MINT")], y=118.6),
]

if __name__ == "__main__":
    raise SystemExit(build(P, root=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))))

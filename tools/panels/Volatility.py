#!/usr/bin/env python3
"""The Volatility panel, declared once.

Running this file regenerates the artwork, the shared hardware, src/PanelTheme.hpp
and the previews. See ../../panelkit/README.md for the pipeline.

Volatility consolidates Modular in a Week's Day 6 random trio onto one FORM
SCHEDULE D: the 4006 shift-register noise generator, Rene Schmitz's YASH
sample and hold, and the PHObos random gate -- three circuits that all want a
clock, so they share one. RATE and CLOCK IN live in NOISE because NOISE is the
one whose character actually depends on it (a bitstream reads as noise at
audio rates and as a random gate at a crawl); SAMPLE & HOLD's TRIG and RND
GATE's own draw both fall back to the same edge when their own jacks are bare.

14 HP, compact density: the six outputs in the footer set the width, and the
three sections sit comfortably inside it.
"""

import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
from panelkit import *   # noqa: E402

P = Panel(
    slug="Volatility",
    title="VOLATILITY",
    form="SCHEDULE D",
    density="compact",
)

P.sections = [
    # The shared clock lives here because NOISE is the section whose whole
    # character rides on it. BITS picks which 8 of the register's 18 bits the
    # DAC output reads as a stepped random CV. The clock jack takes the middle
    # column of the three, between and below the two knobs.
    Section("NOISE", rows=[
        Row([Knob("rate", "RATE", col=0), Knob("color", "COLOR", col=1),
             Knob("bits", "BITS", col=2)]),
        Row([Jack("rate_cv_in", "RATE CV", col=0),
             Jack("clock_in", "CLOCK IN", col=1),
             Jack("bits_cv_in", "BITS CV", col=2)]),
    ]),

    # SRC normals to the module's own white noise, TRIG to the shared clock --
    # patching either overrides it. SLEW softens the stepped output.
    Section("SAMPLE & HOLD", rows=[
        Row([Jack("sh_src_in", "SRC"), Jack("sh_trig_in", "TRIG"),
             Trim("sh_slew", "SLEW")]),
    ]),

    # PROBABILITY sets the odds; SRC picks whether the draw is the module's
    # own RNG or a voltage compared against it, patched in below. PROB CV's
    # trim sits over its own jack, the label they share between them.
    Section("RND GATE", rows=[
        Row([Knob("probability", "PROB", col=0), Switch("rnd_src", "SRC", col=1),
             Trim("prob_cv_amt", "PROB CV", col=2, pair=True)]),
        Row([Trim("length", "LENGTH", col=0),
             Jack("rnd_src_in", "SRC IN", col=1), Jack("prob_cv_in", col=2)]),
    ]),
]

# Everything that leaves the module.
P.footer = [
    Row([Jack("rnd_out", "RND", ink="MINT"),
         Jack("dac_out", "DAC", ink="MINT"),
         Jack("sh_out", "S&H", ink="MINT"),
         Jack("noise_out", "NOISE", ink="MINT"),
         Jack("clock_out", "CLK", ink="MINT"),
         Jack("gate_out", "GATE", ink="MINT")], y=118.6),
]

if __name__ == "__main__":
    raise SystemExit(build(P, root=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))))

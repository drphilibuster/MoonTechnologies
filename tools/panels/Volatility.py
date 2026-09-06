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
The SHIFT light beside NOISE's caption ticks the register over.

12 HP, compact density: four rows is not many, but six outputs in the footer
want the room.
"""

import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
from panelkit import *   # noqa: E402

P = Panel(
    slug="Volatility",
    title="VOLATILITY",
    form="SCHEDULE D",
    hp=14,
    density="compact",
)

C3 = P.cols(3, 12.0)      # 12.00, 30.48, 48.96

P.sections = [
    # The shared clock lives here because NOISE is the section whose whole
    # character rides on it. BITS picks which 8 of the register's 18 bits the
    # DAC output reads as a stepped random CV.
    Section("NOISE", rows=[
        Row([Knob("rate", C3[0], "RATE"), Knob("bits", C3[2], "BITS")]),
        Row([Jack("clock_in", C3[1], "CLOCK IN")]),
    ]),

    # SRC normals to the module's own white noise, TRIG to the shared clock --
    # patching either overrides it. SLEW softens the stepped output.
    Section("SAMPLE & HOLD", rows=[
        Row([Jack("sh_src_in", C3[0], "SRC"), Jack("sh_trig_in", C3[1], "TRIG"),
             Trim("sh_slew", C3[2], "SLEW")]),
    ]),

    # PROBABILITY sets the odds; SRC picks whether the draw is the module's
    # own RNG or a voltage compared against it, patched in below. PROB CV's
    # trim sits over its own jack, paired with the row under it.
    Section("RND GATE", rows=[
        Row([Knob("probability", C3[0], "PROB"), Switch("rnd_src", C3[1], "SRC"),
             Trim("prob_cv_amt", C3[2], "PROB CV", side="above")]),
        Row([Jack("rnd_src_in", C3[1], "SRC IN"), Jack("prob_cv_in", C3[2])]),
    ]),
]

# Everything that leaves the module.
F = P.cols(6, 7.0)       # 7.00, 16.39, 25.78, 35.18, 44.57, 53.96
P.footer = [
    Row([Jack("rnd_out", F[0], "RND", ink="MINT"),
         Jack("dac_out", F[1], "DAC", ink="MINT"),
         Jack("sh_out", F[2], "S&H", ink="MINT"),
         Jack("gate_out", F[3], "GATE", ink="MINT"),
         Jack("noise_out", F[4], "NOISE", ink="MINT"),
         Jack("clock_out", F[5], "CLK", ink="MINT")], y=118.6),
]

if __name__ == "__main__":
    raise SystemExit(build(P, root=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))))

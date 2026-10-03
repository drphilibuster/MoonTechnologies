#!/usr/bin/env python3
"""The Racketeer panel, declared once.

Running this file regenerates the artwork, the shared hardware, src/PanelTheme.hpp
and the previews. See ../../panelkit/README.md for the pipeline.

Racketeer is Wolfgang Spahn's PB701 Electric Intonarumori (after Urs Gaudenz's
Chaos Looper): a PT2399 delay chip run as a self-sustaining noise voice. RACKET
is the loop itself -- ECHO is the one knob you reach for, so it wears the ring;
TIME and CUTOFF flank it. SKIM is what the CV inputs may take off each control
-- a trimpot directly over its jack, one label serving both.

Everything else is on the read-out under the masthead, and every value there is
a control. Its top line is the chip's condition: the delay it is running (drag
it: it is TIME), the bit clock it has dropped to in order to run it, and the
RANGE (click). The second line is ENFORCEMENT, the muscle: the chopper that
presses MUTE for you and the two mini switches of the original, each a click,
and its three pushbuttons, held while the mouse is down and lit while they or
their gate jacks press. The bottom line is the six set-and-leave controls that
used to be trimpots. Outputs stand in a rail on the right; the band holds IN and
the three gate jacks.

Compact density: a three-line read-out, three big knobs and two rows of CV
pairs leave no room for the regular scale.
"""

import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
from panelkit import *   # noqa: E402

P = Panel(
    slug="Racketeer",
    title="RACKETEER",
    what="NOISE VOICE",
    form="FORM 211",
    density="compact",
    # Three lines, every value on them a control. The top is the chip: the
    # delay it is running (TIME, held and dragged), its bit clock, and the RANGE
    # it runs in (a click). The second is the chopper and the two mini switches,
    # each a click, then the three pushbuttons, held while the mouse is down and
    # lit while they or their gate jacks are pressing. The last is the six
    # set-and-leave controls the trimpots used to be, each held and dragged.
    glass=Glass(h=23.5, grid=(3, 6), fields=[
        Field("time_val", cell=(0, 0), span=(1, 2), kind="value"),
        Field("range",    cell=(0, 4), span=(1, 2), kind="toggle"),
        Field("chop",     cell=(1, 0), kind="toggle"),
        Field("pol",      cell=(1, 1), kind="toggle"),
        Field("filt",     cell=(1, 2), kind="toggle"),
        Field("noise",    cell=(1, 3), kind="button"),
        Field("boost",    cell=(1, 4), kind="button"),
        Field("mute",     cell=(1, 5), kind="button"),
        Field("lag",      cell=(2, 0), kind="value"),
        Field("drive",    cell=(2, 1), kind="value"),
        Field("seed",     cell=(2, 2), kind="value"),
        Field("rate",     cell=(2, 3), kind="value"),
        Field("res",      cell=(2, 4), kind="value"),
        Field("thresh",   cell=(2, 5), kind="value"),
    ]),
)

P.sections = [
    # The loop. ECHO is the feedback -- past noon it self-oscillates, which is
    # the whole point. TIME is the chip's R control, CUTOFF the low-pass pot.
    # What goes into the loop (DRIVE on the input, SEED the noise floor that
    # keeps it alive), how the time CV smears (LAG, the optocoupler), how fast
    # the chopper runs, RES (a resonance the original never had) and THRESH
    # (the level the GATE output fires at) are set and left, so they live on
    # the read-out's bottom line rather than as trimpots.
    Section("RACKET", caption_light="loop", rows=[
        Row([BigKnob("time", "TIME"),
             BigKnob("echo", "ECHO", primary=True),
             BigKnob("cutoff", "CUTOFF")]),
    ]),

    # What the CV inputs may take off each control: trimpot directly over its
    # jack, the label they share set between the two -- the paired idiom, three
    # across and two deep. Six across was the panel's widest row once the
    # footer stopped being it; the height the read-out's fields freed pays for
    # the second pair of rows.
    Section("SKIM", rows=[
        Row([Trim("time_cv", "TIME"),
             Trim("echo_cv", "ECHO"),
             Trim("cutoff_cv", "CUTOFF")], pair=True),
        Row([Jack("time_in"), Jack("echo_in"), Jack("cutoff_in")]),
        Row([Trim("rate_cv", "RATE"),
             Trim("res_cv", "RES"),
             Trim("lag_cv", "LAG")], pair=True),
        Row([Jack("rate_in"), Jack("res_in"), Jack("lag_in")]),
    ]),
]

# The audio row sits as low as the bottom screws allow (their top edge is at
# 123.61 mm): IN and the three gate jacks that press the buttons on the
# read-out. External control coming in is what this band is for on every other
# panel in the family. Everything that leaves the module stands in the rail down
# the right-hand edge, GATE with them: with eight jacks the band was what set
# the panel's width, and with four it no longer is.
P.footer = [
    Row([Jack("in", "IN"),
         Jack("noise_in", "NOISE"),
         Jack("boost_in", "BOOST"),
         Jack("mute_in", "MUTE")], y=118.6),
]

P.rail = Rail([Jack("gate_out", "GATE", ink="MINT"),
               Jack("env_out", "ENV", ink="MINT"),
               Jack("dirty_out", "DIRTY", ink="MINT"),
               Jack("out", "OUT", ink="MINT")],
              caption="OUT", labels="auto", cols="auto")

if __name__ == "__main__":
    raise SystemExit(build(P, root=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))))

#!/usr/bin/env python3
"""The Ledger panel, declared once.

Running this file regenerates the artwork, the shared hardware, src/PanelTheme.hpp
and the previews. See ../../panelkit/README.md for the pipeline.

Ledger is an eight-track sequencer. Its generator is Shoal (Ormer Modular, MIT): every
track's melody is a pure function of a seed, the knobs only filter it, and nothing a
knob does cannot be undone by turning it back. It is SCHEDULE L because Schedule L is
the balance sheet *per books* -- eight ledgers kept in one scale, each line worked out
the moment it is read rather than written down.

The controls address the selected track (the eight track tabs on the glass choose which),
the way Shoal's knobs do: CHANCE / NOTE / OCTAVE are the performance trio, then the loop's
shape, its pitch, and how it lives -- evolving, breathing, gate, ties, slop. Scale, root,
weight and tempo are the books every track shares.

Everything the display prints is set where it is printed. The buttons are a strip of tabs
across the top of the glass, and the selected track's books on the TANK page are fields:
drag a number, click a rate, direction, octave, root or scale to pick it. The face keeps a
knob for what the books do not print (SHFT, GATE, TIE, SLOP, WEIGHT, BPM) and for the
performance trio, which is on the books too but is played with a hand; and a CV jack for
every one of them. The outputs stand in a four-column rail, a pitch and a gate per track,
two tracks a row: three columns would be two HP narrower, but read P1 G1 P2 / G2 P3 G3.
"""

import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
from panelkit import *   # noqa: E402

# The glass is cut into a 10 x 26 grid. Its top two rows are the control strip on every page,
# the buttons that used to stand on the face: the five pages, then the eight tracks; under them
# the transport, then what is done to the selected track. The rest of the glass is the page; on
# the TANK page its right-hand eight columns are the books, and every value the books print is
# the control that sets it.
_tabs = ([Field("pg_" + p, cell=(0, 2 * i), span=(1, 2), kind="button")
          for i, p in enumerate(["tank", "roll", "fx", "seq", "song"])]
         + [Field("trk%d" % (t + 1), cell=(0, 10 + 2 * t), span=(1, 2), kind="button")
            for t in range(8)]
         + [Field(n, cell=(1, 2 * i), span=(1, 2), kind="button")
            for i, n in enumerate(["run", "rset", "frze", "rec"])]
         + [Field(n, cell=(1, 10 + 4 * i), span=(1, 4), kind="button")
            for i, n in enumerate(["mute", "solo", "rsed", "capt"])])
# A header line, two columns of five as the books always printed them, the key, and a status.
_books = ([Field(n, cell=(3 + i, 18), span=(1, 4), kind=k) for i, (n, k) in enumerate(
              [("chance", "value"), ("note", "value"), ("octave", "value"),
               ("rate", "select"), ("leng", "value")])]
          + [Field(n, cell=(3 + i, 22), span=(1, 4), kind=k) for i, (n, k) in enumerate(
              [("dirn", "select"), ("evolve", "value"), ("breathe", "value"),
               ("octa", "select"), ("trns", "value")])]
          + [Field("root", cell=(8, 18), span=(1, 2), kind="select"),
             Field("scale", cell=(8, 20), span=(1, 3), kind="select")])

P = Panel(
    slug="Ledger",
    title="LEDGER",
    what="GENERATIVE SEQUENCER",
    subtitle="GENERAL LEDGER",
    form="SCHEDULE L",
    density="compact",
    glass=Glass(h=54.2, grid=(10, 26), fields=_tabs + _books),   # as tall as the rows below allow
    footer_groups=(6, 4),               # transport and reseed in; CV A-D
)

P.sections = [
    # The knobs left on the face are the ones the books do not print -- SHFT and the three
    # that say how a note lives, and the books' weight and tempo -- and the performance trio,
    # which you play with a hand rather than a pointer and which are on the books as well.
    Section("ENTRY · FEEL · BOOKS", groups=(3, 4, 2), rows=[
        Row([Knob("chance", "CHANCE", primary=True), Knob("note", "NOTE ±"), Knob("octave", "OCTAVE ±"),
             Knob("shft", "SHFT"), Knob("gate", "GATE"), Knob("tie", "TIE"), Knob("slop", "SLOP"),
             Knob("weight", "WEIGHT"), Knob("bpm", "BPM")], pair=True),
        # CV under the knob it moves. A track knob's jack is polyphonic -- channel n is track
        # n, and a mono cable moves every track -- and adds to the knob, the way Shoal's
        # expander does. BPM has none: patch CLOCK.
        Row([Jack("%s_cv" % n, "", col=c) for c, n in enumerate(
            ["chance", "note", "octave", "shft", "gate", "tie", "slop", "weight"])], silent=True),
        # CV for the values that live on the books, named, since no knob stands over them.
        # ROOT's jack is absolute (1 V/oct, 0 V = C).
        Row([Jack("%s_cv" % n, t) for n, t in [
            ("leng", "LENG"), ("rate", "RATE"), ("dirn", "DIRN"), ("trns", "TRNS"), ("octa", "OCTA"),
            ("evolve", "EVOLVE"), ("breathe", "BREATHE"), ("scale", "SCALE"), ("root", "ROOT")]]),
    ]),
]

P.footer = [
    Row([Jack("clk_in", "CLOCK"), Jack("rst_in", "RESET"), Jack("run_in", "RUN"),
         Jack("reseed_in", "RESEED"), Jack("freeze_in", "FREEZE"), Jack("seed_in", "SEED")]
        # four free CV inputs, routed to any track's knobs by its mod matrix
        + [Jack("cv_%s" % c, "CV %s" % c.upper()) for c in "abcd"],
        y=118.6),
]

# Every output stands down the right-hand edge, the whole height of the face, in as many
# columns as it takes: a pitch and a gate per track, then the buses. In the footer the
# sixteen voice jacks were what set the panel's width.
_voices = [j for t in range(1, 9)
           for j in (Jack("pitch%d" % t, "PITCH %d" % t, ink="MINT"),
                     Jack("gate%d" % t, "GATE %d" % t, ink="MINT"))]
_buses = [Jack("vel", "VEL", ink="MINT"), Jack("mod", "MOD", ink="MINT"),
          Jack("current", "CURRENT", ink="MINT"), Jack("eos", "EOS", ink="MINT"),
          Jack("clk_out", "CLOCK", ink="MINT")]

# Currents and EOS are one 8-channel cable each: channel n is track n. VEL and MOD are
# 8-channel too: channel n is track n's last velocity and MOD 1 lane.
P.rail = Rail(_voices + _buses, caption="OUT", tall=True, labels="auto", cols=4)

if __name__ == "__main__":
    raise SystemExit(build(P, root=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))))

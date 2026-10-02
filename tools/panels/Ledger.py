#!/usr/bin/env python3
"""The Ledger panel, declared once.

Running this file regenerates the artwork, the shared hardware, src/PanelTheme.hpp
and the previews. See ../../panelkit/README.md for the pipeline.

Ledger is an eight-track sequencer. Its generator is Shoal (Ormer Modular, MIT): every
track's melody is a pure function of a seed, the knobs only filter it, and nothing a
knob does cannot be undone by turning it back. It is SCHEDULE L because Schedule L is
the balance sheet *per books* -- eight ledgers kept in one scale, each line worked out
the moment it is read rather than written down.

The knobs address the selected track (the eight TRACK buttons choose which), the way
Shoal's do: CHANCE / NOTE / OCTAVE are the performance trio, then the loop's shape,
its pitch, and how it lives -- evolving, breathing, gate, ties, slop. The last four
knobs are the books every track shares: scale, root, weight and tempo.
"""

import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
from panelkit import *   # noqa: E402

P = Panel(
    slug="Ledger",
    title="LEDGER",
    subtitle="GENERAL LEDGER",
    form="SCHEDULE L",
    density="compact",
    glass=Glass(h=52.9),
    footer_groups=(6, 4, 14),           # transport and reseed in; CV A-D; seven voices' pairs
)

P.sections = [
    Section("ENTRY · SHAPE · PITCH · GROWTH · BOOKS", groups=(3, 3, 3, 5, 4), rows=[
        Row([Knob("chance", "CHANCE", primary=True), Knob("note", "NOTE ±"), Knob("octave", "OCTAVE ±"),
             Knob("leng", "LENG"), Knob("rate", "RATE", steps=29), Knob("dirn", "DIRN", steps=15),
             Knob("trns", "TRNS"), Knob("shft", "SHFT"), Knob("octa", "OCTA", steps=7),
             Knob("evolve", "EVOLVE"), Knob("breathe", "BREATHE"), Knob("gate", "GATE"),
             Knob("tie", "TIE"), Knob("slop", "SLOP"),
             Knob("scale", "SCALE", steps=13), Knob("root", "ROOT", steps=12),
             Knob("weight", "WEIGHT"), Knob("bpm", "BPM")], pair=True),
        # CV under the knob it moves. A track knob's jack is polyphonic -- channel n is track
        # n, and a mono cable moves every track -- and adds to the knob, the way Shoal's
        # expander does. ROOT's jack is absolute (1 V/oct, 0 V = C); BPM has none: patch CLOCK.
        Row([Jack("%s_cv" % n, "", col=c) for c, n in enumerate(
            ["chance", "note", "octave", "leng", "rate", "dirn", "trns", "shft", "octa",
             "evolve", "breathe", "gate", "tie", "slop", "scale", "root", "weight"])], silent=True),
        Row([Bezel("trk%d" % (i + 1), str(i + 1)) for i in range(8)]
            + [Button("mute", "MUTE", light="mute_led"), Button("solo", "SOLO", light="solo_led"),
               Button("rsed", "RESEED", light="rsed_led"),
               Button("run", "RUN", light="run_led"), Button("frze", "FREEZE", light="frze_led"),
               Button("rset", "RESET"),
               # what the display shows, and writing a generator's loop down
               Button("pg_tank", "TANK", light="pg_tank_led"), Button("pg_roll", "ROLL", light="pg_roll_led"),
               Button("pg_fx", "FX", light="pg_fx_led"), Button("pg_seq", "SEQ", light="pg_seq_led"),
               Button("pg_song", "SONG", light="pg_song_led"), Button("capt", "CAPTURE", light="capt_led"),
               # MIDI in, written into the playing patterns
               Button("rec", "REC", light="rec_led")],
            own_grid=True),
    ]),
]

# Thirty-one jacks in one row made this a 68 HP panel whose controls need 50. The last seven
# -- the eighth voice's pair and the buses that carry every track -- stand down the right-hand
# edge beside the display instead, a rail the whole height of the face; the footer keeps the
# rest, in the order it had.
_voices = [j for t in range(1, 9)
           for j in (Jack("pitch%d" % t, "PITCH %d" % t, ink="MINT"),
                     Jack("gate%d" % t, "GATE %d" % t, ink="MINT"))]
_buses = [Jack("vel", "VEL", ink="MINT"), Jack("mod", "MOD", ink="MINT"),
          Jack("current", "CURRENT", ink="MINT"), Jack("eos", "EOS", ink="MINT"),
          Jack("clk_out", "CLOCK", ink="MINT")]

P.footer = [
    Row([Jack("clk_in", "CLOCK"), Jack("rst_in", "RESET"), Jack("run_in", "RUN"),
         Jack("reseed_in", "RESEED"), Jack("freeze_in", "FREEZE"), Jack("seed_in", "SEED")]
        # four free CV inputs, routed to any track's knobs by its mod matrix
        + [Jack("cv_%s" % c, "CV %s" % c.upper()) for c in "abcd"]
        + _voices[:-2],
        y=118.6),
]

# Currents and EOS are one 8-channel cable each: channel n is track n. VEL and MOD are
# 8-channel too: channel n is track n's last velocity and MOD 1 lane.
P.rail = Rail(_voices[-2:] + _buses, caption="OUT", tall=True, labels="above")

if __name__ == "__main__":
    raise SystemExit(build(P, root=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))))

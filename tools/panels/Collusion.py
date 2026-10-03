#!/usr/bin/env python3
"""The Collusion panel, declared once.

Running this file regenerates the artwork, the shared hardware, src/PanelTheme.hpp
and the previews. See ../../panelkit/README.md for the pipeline.

Collusion is six LFOs that listen to each other. FILINGS is what each one would
do unsupervised -- one rate for the whole swarm, a SPREAD that fans their
natural rates apart in octaves, a SHAPE that runs the cycle from a sine to a
relaxation spike. AGREEMENT is what they do about each other: COUPLING is how
hard each is pulled toward the rest, SCHEME is who it can hear, EVASION is the
phase lag that stops them ever quite agreeing, LEVERAGE is the ledger borrowing
against the swarm that wrote it, and TERM and AUDIT are the shift register that
keeps the record. PARTIES is the six of them, each with the lamp you watch fall
into step.

COUPLING wears the lime ring: it is the one knob the module is about, and
turning it is the demonstration -- six lamps beating against each other, then a
phase transition, then one collective rhythm six times over.

The read-out holds what used to be detents: SCHEME and TERM by name, RANGE and
DEAL, with ORDER beside them -- every word on it a control or a measure.

Compact density: three sections, five rows. The width is solved, not typed --
see panelkit/layout.py.
"""

import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
from panelkit import *   # noqa: E402

GLASS_H = 10.5

P = Panel(
    slug="Collusion",
    title="COLLUSION",
    what="COUPLED LFO SWARM",
    # Form 211 is the IRS whistleblower award claim -- the form you file about
    # people who have agreed among themselves. ORDER is this panel's Form 211.
    form="FORM 211",
    density="compact",
    # The read-out: two lines, every word on them a control or a measure.
    # SCHEME and TERM by name (they were detents on knobs, named only in
    # tooltips), RANGE, DEAL, and ORDER -- how much the six agree right now.
    glass=Glass(h=GLASS_H, grid=(2, 6), fields=[
        Field("scheme", cell=(0, 0), span=(1, 4), kind="select"),
        Field("term",   cell=(0, 4), span=(1, 2), kind="select"),
        Field("range",  cell=(1, 0), span=(1, 2), kind="toggle"),
        Field("deal",   cell=(1, 2), span=(1, 2), kind="button"),
    ]),
)

P.sections = [
    # What each filer would do unsupervised. RATE is the swarm's centre; SPREAD
    # fans the six natural rates around it, and is the other half of the phase
    # diagram -- the coupling needed to lock the swarm is set by how far apart
    # their rates are and by nothing else. RANGE moves the whole swarm between
    # the LFO decades and the audio band, where the same coupling reads as FM
    # rather than as drift. DEAL re-rolls the fan.
    Section("FILINGS", rows=[
        Row([BigKnob("rate", "RATE"),
             Knob("spread", "SPREAD"),
             Knob("shape", "SHAPE")]),
    ]),

    # What they do about each other, and the record they keep of it. COUPLING is
    # bipolar: left of centre they repel and spread into maximal disagreement,
    # right of centre they attract and lock. SCHEME is the four wirings -- who
    # each filer can hear. EVASION is the Sakaguchi phase lag, the parameter
    # that buys partial order instead of all-or-nothing. TERM and AUDIT are the
    # shift register: how many bits go round, and how often one is rewritten
    # from what the swarm currently agrees on rather than recirculated.
    # ORDER beside the caption is how much they agree right now.
    #
    # The trims are what the CV inputs may take off each control -- a trimpot
    # directly over its jack, with the one label they share set between the two
    # so it cannot be read as naming the row above. V/OCT's trim is a tracking
    # depth and starts at full, so the swarm follows pitch without being asked.
    Section("AGREEMENT", caption_light="order_led", rows=[
        Row([BigKnob("couple", "COUPLING", primary=True),
             Knob("evade", "EVASION"),
             Knob("leverage", "LEVERAGE"),
             Knob("audit", "AUDIT")]),
        Row([Trim("rate_cv", "V/OCT"),
             Trim("couple_cv", "COUPLE"),
             Trim("evade_cv", "EVADE"),
             Trim("shape_cv", "SHAPE"),
             Trim("spread_cv", "SPREAD"),
             Trim("audit_cv", "AUDIT")], pair=True),
        Row([Jack("rate_in"), Jack("couple_in"), Jack("evade_in"),
             Jack("shape_in"), Jack("spread_in"), Jack("audit_in")], silent=True),
    ]),

    # The six of them. Each jack carries its own filer, and the lamp on its
    # label is that filer's own cycle: six lamps beating against each other
    # below the critical coupling, one lamp six times over above it.
    Section("PARTIES", rows=[
        Row([Jack("out%d" % k, str(k), ink="MINT", light="led%d" % k) for k in range(1, 7)]),
    ]),

]

# The I/O row sits as low as the bottom screws allow: RACK_GRID_HEIGHT -
# RACK_GRID_WIDTH puts their top edge at 123.61 mm, so a jack collar centred
# below 118.6 would run under one. CLK carries the ledger's own lamp: it is
# what that jack reports, and the register is the only thing on the panel with
# a rate of its own.
P.footer = [
    Row([Jack("sync_in", "SYNC"),
         Jack("clk_in", "CLK", light="ledger_led"),
         Jack("consensus", "CONSENS", ink="MINT"),
         Jack("order_out", "ORDER", ink="MINT"),
         Jack("ledger_out", "LEDGER", ink="MINT"),
         Jack("pulse_out", "PULSE", ink="MINT")], y=118.6),
]

if __name__ == "__main__":
    raise SystemExit(build(P, root=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))))

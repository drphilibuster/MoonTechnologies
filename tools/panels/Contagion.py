#!/usr/bin/env python3
"""The Contagion panel, declared once.

Running this file regenerates the artwork, the shared hardware, src/PanelTheme.hpp
and the previews. See ../../panelkit/README.md for the pipeline.

Contagion is an Access Virus C -- its own 80C515 firmware in charge of the panel,
the LCD, MIDI and preset memory, booting and driving its DSP56362 -- running the
OS image the user supplies. It is FORM 8300 because contagion is what spreads, and
FORM 8300 is how cash that moves gets reported.

Every control here is one of the unit's: the 32 pots, the 35 buttons of its key
matrix and the LEDs beside them. A knob reaches the firmware through the 80C515's
A/D converter and a button through its key matrix, exactly as the hardware's do; the
firmware decides what they mean. The labels are the names the firmware itself shows
when a control is touched, or the manual's where the two agree.
"""

import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
from panelkit import *   # noqa: E402

P = Panel(
    slug="Contagion",
    title="CONTAGION",
    subtitle="VIRUS C",
    form="FORM 8300",
    glass=Glass(h=11.0),
)

# --- inside the read-out well -------------------------------------------------
# The 2 x 16 dot-matrix LCD at about the module's own proportions, centred.
LCD_W, LCD_H = 66.0, 8.8
LCD_Y = 10.9
P.metrics = dict(LCD_W=LCD_W, LCD_H=LCD_H, LCD_Y=LCD_Y)

P.sections = [
    Section("LFO · OSCILLATORS · MIXER · EFFECTS", groups=(2, 6, 5, 3, 3), rows=[
        Row([Knob("rate", "RATE"), Knob("clock", "CLOCK"),
             Knob("shape", "SHAPE"), Knob("wave", "WAVE"), Knob("semitone", "SEMI"),
             Knob("detune", "DETUNE"), Knob("fm", "FM AMT"), Knob("osc_fb", "FEEDBK"),
             Knob("osc_bal", "OSC BAL"), Knob("sub", "SUB OSC"), Knob("osc_vol", "OSC VOL"),
             Knob("noise", "NOISE"), Knob("ring", "RING"),
             Knob("fx_mix", "TYPE/MIX"), Knob("fx_fb", "FEEDBACK"), Knob("fx_send", "SEND"),
             Knob("soft1", "SOFT 1"), Knob("soft2", "SOFT 2"), Knob("volume", "VOLUME")]),
        Row([Button("lfo_edit", "EDIT"), Button("lfo_select", "SELECT"),
             Button("lfo_page1", "PAGE"), Button("lfo_page2", "PAGE"),
             Light("lfo1", "1"), Light("lfo2", "2"), Light("lfo3", "3"), Light("lfo_mod", "MOD"),
             Button("osc_edit", "EDIT"), Bezel("sync", "SYNC"),
             Button("osc_select", "SELECT"),
             Light("osc1", "1"), Light("osc2", "2"), Light("osc3", "3"),
             Bezel("osc3_on", "OSC 3"),
             Button("fx_edit", "EFFECTS"), Bezel("fx_a", "DIST"), Bezel("fx_b", "PHA"), Bezel("fx_c", "CHO"),
             Button("dly_edit", "DELAY")], own_grid=True),
    ]),
    Section("FILTERS · ENVELOPES · PROGRAM", groups=(5, 4, 4), rows=[
        Row([Knob("cutoff", "CUTOFF", primary=True), Knob("cutoff2", "CUTOFF 2"),
             Knob("reso", "RESO"), Knob("env_amt", "ENV AMT"), Knob("flt_bal", "FLT BAL"),
             Knob("f_att", "ATTACK"), Knob("f_dec", "DECAY"), Knob("f_sus", "SUSTAIN"), Knob("f_rel", "RELEASE"),
             Knob("a_att", "ATTACK"), Knob("a_dec", "DECAY"), Knob("a_sus", "SUSTAIN"), Knob("a_rel", "RELEASE")]),
        Row([Button("flt_edit", "EDIT"),
             Button("flt1_mode", "FILT 1"), Light("f1m1", "HP"), Light("f1m2", "BP"), Light("f1m3", "BS"),
             Button("flt2_mode", "FILT 2"), Light("f2m1", "LP"), Light("f2m2", "HP"), Light("f2m3", "BP"), Light("f2m4", "BS"),
             Button("flt_sel1", "SEL 1"), Button("flt_sel2", "SEL 2"),
             Bezel("arp_on", "ARP"), Button("arp_edit", "EDIT"),
             Button("clock_edit", "CLOCK"), Button("random", "RANDOM"), Button("random_snd", "RND SND")], own_grid=True),
        Row([Button("undo", "UNDO"), Button("store", "STORE"), Button("multi", "MULTI"), Button("single", "SINGLE"),
             Button("prog_dn", "SK1 -"), Button("prog_up", "SK1 +"), Button("bank_dn", "SK2 -"), Button("bank_up", "SK2 +"),
             Button("search", "SEARCH"), Button("tr_dn", "TRANS -"), Button("tr_up", "TRANS +"),
             Button("key26", "2,6")], own_grid=True),
    ]),
]

P.footer = [
    Row([Jack("in_l", "IN L"), Jack("in_r", "IN R"),
         Jack("out1l", "OUT 1L", ink="MINT"), Jack("out1r", "OUT 1R", ink="MINT"),
         Jack("out2l", "OUT 2L", ink="MINT"), Jack("out2r", "OUT 2R", ink="MINT"),
         Jack("out3l", "OUT 3L", ink="MINT"), Jack("out3r", "OUT 3R", ink="MINT")],
        y=118.6),
]

if __name__ == "__main__":
    raise SystemExit(build(P, root=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))))

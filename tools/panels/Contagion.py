#!/usr/bin/env python3
"""The Contagion panel, declared once.

Running this file regenerates the artwork, the shared hardware, src/PanelTheme.hpp
and the previews. See ../../panelkit/README.md for the pipeline.

Contagion is an Access Virus C -- its own 80C515 firmware in charge of the panel,
the LCD, MIDI and preset memory, booting and driving its DSP56362 -- running the
OS image the user supplies. It is FORM 8300 because contagion is what spreads, and
FORM 8300 is how cash that moves gets reported.

Every control here is one of the unit's: the 32 pots, the 35 buttons of its key
matrix and the LEDs of its multiplex, plus the two LFO rate LEDs the DSP drives. A
knob reaches the firmware through the 80C515's A/D converter and a button through its
key matrix, exactly as the hardware's do; the firmware decides what they mean. Names
and grouping are the owner's manual's section drawings (LFOS/MOD, OSCILLATORS,
FILTERS, EFFECTS, DELAY/REVERB, the display buttons), matched control by control to
the firmware by pressing each one (VirusResearch/NOTES.md, "Panel map").
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
    glass=Glass(h=10.0),
)

# --- inside the read-out well -------------------------------------------------
# The 2 x 16 dot-matrix LCD at about the module's own proportions, centred.
LCD_W, LCD_H = 66.0, 8.6
LCD_Y = 10.5
P.metrics = dict(LCD_W=LCD_W, LCD_H=LCD_H, LCD_Y=LCD_Y)


def leds(prefix, labels):
    return [Light("%s%d" % (prefix, i + 1), text) for i, text in enumerate(labels)]


P.sections = [
    Section("LFOS/MOD · OSCILLATORS · MIXER · EFFECTS · DELAY/REVERB", groups=(1, 5, 5, 2, 3, 3), rows=[
        Row([Knob("rate", "RATE"),
             Knob("shape", "SHAPE"), Knob("wave", "WAVE SEL/PW"), Knob("semitone", "SEMITONE"),
             Knob("detune", "DETUNE 2/3"), Knob("fm", "FM AMOUNT"),
             Knob("osc_bal", "OSC BAL"), Knob("sub", "SUB OSC"), Knob("osc_vol", "OSC VOL"),
             Knob("noise", "NOISE"), Knob("ring", "RING MOD"),
             Knob("fx_mix", "TYPE/MIX"), Knob("fx_int", "INTENSITY"),
             Knob("dly_send", "SEND"), Knob("dly_time", "DLY/REV TIME"), Knob("dly_fb", "FDBK/DAMP"),
             Knob("soft1", "SOFT 1"), Knob("soft2", "SOFT 2/VALUE"), Knob("volume", "VOLUME")]),
        # The LFOS/MOD buttons and their LEDs; the oscillators'; the effects'.
        Row([Button("lfo_edit", "EDIT", light="lfo_edit_led"), Button("lfo_select", "SELECT")]
            + leds("lfo", ["1", "2", "3", "MOD"])
            + [Light("rate1", "RATE 1"), Light("rate23", "2/3"),
               Button("lfo_shape", "SHAPE")]
            + leds("shp", ["SIN", "TRI", "SAW", "SQR", "WAVE"])
            + [Button("lfo_amount", "AMOUNT"),
               Button("osc_edit", "EDIT", light="osc_edit_led"), Bezel("sync", "SYNC"),
               Button("osc1", "OSC 1", light="osc1_led"), Button("osc2", "OSC 2", light="osc2_led"),
               Button("osc3", "OSC 3", light="osc3_led"), Bezel("osc3_on", "OSC 3 ON"),
               Button("fx_edit", "EDIT", light="fx_edit_led"), Button("fx_select", "SELECT")]
            + leds("fx", ["DIST", "PHA", "CHO"])
            + [Button("dly_edit", "EDIT", light="dly_edit_led")], own_grid=True),
        # What AMOUNT steps through, for LFO 1, 2, 3 and MOD in turn (lit while the
        # amount is not zero, flashing while selected); then the arpeggiator and the
        # display's own buttons.
        Row(leds("d1_", ["OSC 1", "OSC 2", "PW 1+2", "RESO", "F GAIN", "ASSIGN"])
            + leds("d2_", ["FILT 1", "FILT 2", "SHAPE", "FM AMT", "PAN", "ASSIGN"])
            + leds("d3_", ["OSC 1", "OSC 2", "PW 1", "PW 2", "SYNC PH"])
            + leds("dm_", ["ASGN 1", "ASGN 2", "ASGN 3", "ASGN 4", "ASGN 5", "ASGN 6"])
            + [Bezel("arp_on", "ARP ON"), Button("arp_edit", "ARP EDIT", light="arp_edit_led"),
               Button("edit", "EDIT", light="edit_led"), Button("global", "GLOBAL", light="global_led"),
               Button("random", "RANDOM")],
            own_grid=True),
    ]),
    Section("FILTERS · ENVELOPES · PROGRAM", groups=(5, 4, 4, 4, 6), rows=[
        Row([Knob("cutoff", "CUTOFF", primary=True), Knob("cutoff2", "CUTOFF 2"),
             Knob("reso", "RESO"), Knob("env_amt", "ENV AMT"), Knob("flt_bal", "FLT BAL"),
             Knob("f_att", "ATTACK"), Knob("f_dec", "DECAY"), Knob("f_sus", "SUSTAIN"), Knob("f_rel", "RELEASE"),
             Knob("a_att", "ATTACK"), Knob("a_dec", "DECAY"), Knob("a_sus", "SUSTAIN"), Knob("a_rel", "RELEASE"),
             Button("undo", "UNDO"), Button("store", "STORE"),
             Button("multi", "MULTI", light="multi_led"), Button("single", "SINGLE", light="single_led"),
             Button("part_dn", "PART -"), Button("part_up", "PART +"),
             Button("param_dn", "PARAM <"), Button("param_up", "PARAM >"),
             Button("value_dn", "VALUE -"), Button("value_up", "VALUE +")]),
        Row([Button("flt_edit", "EDIT", light="flt_edit_led"), Button("flt1_mode", "FILT 1")]
            + leds("f1m", ["LP", "HP", "BP", "BS"])
            + [Button("flt2_mode", "FILT 2")]
            + leds("f2m", ["LP", "HP", "BP", "BS"])
            + [Button("flt_sel1", "SEL 1", light="sel1_led"), Button("flt_sel2", "SEL 2", light="sel2_led"),
               Button("tr_dn", "TRANS -")]
            + leds("tr", ["-2", "-1", "0", "+1", "+2"])
            + [Button("tr_up", "TRANS +"), Light("bpm", "BPM")], own_grid=True),
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

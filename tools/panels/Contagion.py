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
    glass=Glass(h=18.0),
    footer_groups=(2, 7, 3, 8, 8),      # audio in, a keyboard, a transport, CV, gates
)

# The display is one piece of glass: the unit's own 2 x 16 LCD as it is now (the parameter
# screen), the last program screen kept beside it (the preset), and the lamps the unit
# shows its selectors and its AMOUNT destinations with, grouped by what they answer. The
# module draws all of it; where it goes inside the glass is the widget's business.

# The unit has no encoders and no selectors, only buttons -- PART -/+, a key that steps through
# four filter modes with four LEDs. A modular face can do better, so here the pairs are endless
# knobs (a detent is a press) and the cycles are detented knobs that press until the unit's
# LEDs agree. Pushing a selector knob presses the section's EDIT. Everything still reaches the
# firmware as a key press and comes back as an LED (Controls.hpp).
#
# `steps` is the knob's detent count; an endless knob has none.
#
# Four gestures of the unit need one key held while another is pressed, or two pressed together, which a mouse
# cannot do, so each is a control that does it for you: MULTI+SINGLE (both together enter Multi-Single mode, and
# SINGLE or MULTI alone leaves it, so a second press leaves), CATEGORY and IN CATEGORY (SINGLE held, PARAMETER
# steps the category, VALUE steps the sounds in it) and PAGE (one PARAMETER button held, the other pressed, scrolls a
# page of parameters at a time in the direction of the held one).


def knob_row_1():
    return [Knob("rate", "RATE"),
            Knob("shape", "SHAPE"), Knob("wave", "WAVE SEL/PW"), Knob("semitone", "SEMITONE"),
            Knob("detune", "DETUNE 2/3"), Knob("fm", "FM AMOUNT"),
            Knob("osc_bal", "OSC BAL"), Knob("sub", "SUB OSC"), Knob("osc_vol", "OSC VOL"),
            Knob("noise", "NOISE"), Knob("ring", "RING MOD"),
            Knob("fx_mix", "TYPE/MIX"), Knob("fx_int", "INTENSITY"),
            Knob("dly_send", "SEND"), Knob("dly_time", "DLY/REV TIME"), Knob("dly_fb", "FDBK/DAMP"),
            Knob("soft1", "SOFT 1"), Knob("soft2", "SOFT 2/VALUE"), Knob("volume", "VOLUME")]


P.sections = [
    Section("LFOS/MOD · OSCILLATORS · MIXER · EFFECTS · DELAY/REVERB", groups=(1, 5, 5, 2, 3, 3), rows=[
        Row(knob_row_1()),
        Row([Knob("lfo_sel", "LFO", steps=4, light="lfo_edit_led"), Knob("lfo_shape", "LFO SHAPE", steps=5),
             Button("lfo_amount", "AMOUNT"), Light("rate1", "RATE 1"), Light("rate23", "2/3"),
             Knob("osc_sel", "OSC", steps=3, light="osc_edit_led"),
             Bezel("sync", "SYNC"), Bezel("osc3_on", "OSC 3 ON"),
             Knob("fx_sel", "EFFECT", steps=3, light="fx_edit_led"),
             Button("dly_edit", "DLY/REV", light="dly_edit_led"),
             Bezel("arp_on", "ARP ON"), Button("arp_edit", "ARP EDIT", light="arp_edit_led"),
             Button("edit", "EDIT", light="edit_led"), Button("global", "GLOBAL", light="global_led"),
             Button("random", "RANDOM")], own_grid=True),
    ]),
    Section("FILTERS · ENVELOPES · PROGRAM", groups=(5, 4, 4, 5), rows=[
        Row([Knob("cutoff", "CUTOFF", primary=True), Knob("cutoff2", "CUTOFF 2"),
             Knob("reso", "RESO"), Knob("env_amt", "ENV AMT"), Knob("flt_bal", "FLT BAL"),
             Knob("f_att", "ATTACK"), Knob("f_dec", "DECAY"), Knob("f_sus", "SUSTAIN"), Knob("f_rel", "RELEASE"),
             Knob("a_att", "ATTACK"), Knob("a_dec", "DECAY"), Knob("a_sus", "SUSTAIN"), Knob("a_rel", "RELEASE"),
             Stepper("preset", "PRESET"), Stepper("part", "PART"), Stepper("param", "PARAMETER"), Stepper("value", "VALUE"),
             Stepper("trans", "TRANSPOSE")]),
        Row([Button("flt_edit", "EDIT", light="flt_edit_led"),
             Knob("flt1_mode", "FILT 1", steps=4), Knob("flt2_mode", "FILT 2", steps=4),
             Button("flt_sel1", "SEL 1", light="sel1_led"), Button("flt_sel2", "SEL 2", light="sel2_led"),
             Button("undo", "UNDO"), Button("store", "STORE"),
             Button("multi", "MULTI", light="multi_led"), Button("single", "SINGLE", light="single_led"),
             Button("multisingle", "MULTI+SINGLE"),
             Stepper("category", "CATEGORY"), Stepper("incat", "IN CATEGORY"), Stepper("page", "PAGE")]
            + [Light("tr%d" % (i + 1), t) for i, t in enumerate(["-2", "-1", "0", "+1", "+2"])]
            + [Knob("tempo", "BPM", light="bpm")], own_grid=True),
    ]),
]

P.footer = [
    Row([Jack("in_l", "IN L"), Jack("in_r", "IN R"),
         # A keyboard's worth of MIDI, as cables: V/OCT, GATE and VEL are polyphonic.
         Jack("note_v", "PITCH"), Jack("note_gate", "GATE"), Jack("note_vel", "VEL"),
         Jack("bend", "BEND"), Jack("mod", "MOD"), Jack("touch", "TOUCH"), Jack("sustain", "SUSTAIN"),
         Jack("clk", "CLOCK"), Jack("run", "RUN"), Jack("rst", "RESET"),
         # Eight CV inputs, each assignable to any of the 32 knobs.
         Jack("cv1", "CV 1"), Jack("cv2", "CV 2"), Jack("cv3", "CV 3"), Jack("cv4", "CV 4"),
         Jack("cv5", "CV 5"), Jack("cv6", "CV 6"), Jack("cv7", "CV 7"), Jack("cv8", "CV 8"),
         # A gate presses a button.
         Jack("g_part_dn", "PART -"), Jack("g_part_up", "PART +"),
         Jack("g_param_dn", "PARAM <"), Jack("g_param_up", "PARAM >"),
         Jack("g_value_dn", "VALUE -"), Jack("g_value_up", "VALUE +"),
         Jack("g_arp", "ARP ON"), Jack("g_random", "RANDOM")],
        y=118.6),
]

# The three stereo outs stand down the right-hand edge, not along the footer: the footer's
# thirty-four jacks were what made this the widest panel in the family, wider than its
# nineteen knobs by sixteen HP. Inputs below, what leaves the module beside the controls.
P.rail = Rail([Jack(n, t, ink="MINT") for n, t in
               [("out1l", "1 L"), ("out1r", "1 R"), ("out2l", "2 L"),
                ("out2r", "2 R"), ("out3l", "3 L"), ("out3r", "3 R")]],
              caption="OUT")

if __name__ == "__main__":
    raise SystemExit(build(P, root=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))))

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
    what="VIRUS C SYNTHESIZER",
    form="FORM 8300",
    # The display is one piece of glass, and everything it shows is a control; see below.
    # The grid is about a millimetre a column, so the spans read as widths.
    glass=Glass(h=24.5, grid=(7, 180), fields=[
        # PRESET: the last program screen, which a click opens the sound list from, and
        # under it the three step pairs that walk the programs, each named beside itself.
        Field("preset_lcd", cell=(1, 0),  span=(2, 40), kind="menu"),
        Field("preset",     cell=(3, 0),  span=(2, 6),  kind="button"),
        Field("category",   cell=(3, 15), span=(2, 6),  kind="button"),
        Field("incat",      cell=(3, 27), span=(2, 6),  kind="button"),
        # PARAMETER: the unit's LCD as it is now (drawn over the same columns as its
        # step pairs), the tempo in its caption line, the pairs under it.
        Field("tempo",      cell=(0, 63), span=(1, 21), kind="value"),
        Field("param",      cell=(3, 43), span=(2, 6),  kind="button"),
        Field("value",      cell=(3, 57), span=(2, 6),  kind="button"),
        Field("page",       cell=(3, 71), span=(2, 6),  kind="button"),
        # The unit's mode keys, as the words its LEDs light: two lines of soft keys under
        # both screens, the program keys over the edit keys.
        Field("k_single",   cell=(5, 0),  span=(1, 11), kind="button"),
        Field("k_multi",    cell=(5, 12), span=(1, 11), kind="button"),
        Field("k_multisgl", cell=(5, 24), span=(1, 11), kind="button"),
        Field("k_edit",     cell=(5, 36), span=(1, 11), kind="button"),
        Field("k_global",   cell=(5, 48), span=(1, 11), kind="button"),
        Field("k_undo",     cell=(5, 60), span=(1, 11), kind="button"),
        Field("k_store",    cell=(5, 72), span=(1, 12), kind="button"),
        Field("k_arp_on",   cell=(6, 0),  span=(1, 16), kind="button"),
        Field("k_arp_edit", cell=(6, 17), span=(1, 16), kind="button"),
        Field("k_dly_edit", cell=(6, 34), span=(1, 16), kind="button"),
        Field("k_flt_edit", cell=(6, 51), span=(1, 16), kind="button"),
        Field("k_random",   cell=(6, 68), span=(1, 16), kind="button"),
        # SELECTED: one line per selector, its name then its positions. The positions
        # are the selector; the name, where the section has one, is its EDIT key (or,
        # for the filters, SELECT 1 and 2). OSC's line ends in SYNC and OSC 3 ON.
        Field("lfo_edit",   cell=(1, 86), span=(1, 8),  kind="button"),
        Field("lfo_sel",    cell=(1, 94), span=(1, 22), kind="select"),
        Field("lfo_shape",  cell=(2, 94), span=(1, 28), kind="select"),
        Field("osc_edit",   cell=(3, 86), span=(1, 8),  kind="button"),
        Field("osc_sel",    cell=(3, 94), span=(1, 17), kind="select"),
        Field("k_sync",     cell=(3, 111), span=(1, 5), kind="button"),
        Field("k_osc3_on",  cell=(3, 116), span=(1, 6), kind="button"),
        Field("fx_edit",    cell=(4, 86), span=(1, 8),  kind="button"),
        Field("fx_sel",     cell=(4, 94), span=(1, 17), kind="select"),
        Field("k_sel1",     cell=(5, 86), span=(1, 8),  kind="button"),
        Field("flt1_mode",  cell=(5, 94), span=(1, 22), kind="select"),
        Field("k_sel2",     cell=(6, 86), span=(1, 8),  kind="button"),
        Field("flt2_mode",  cell=(6, 94), span=(1, 22), kind="select"),
        # AMOUNT: the four sources' destinations, and the key that steps them; TRANSPOSE
        # and the unit's octave lamps under it.
        Field("lfo_amount", cell=(1, 124), span=(4, 56), kind="button"),
        Field("trans",      cell=(5, 124), span=(2, 6), kind="button"),
    ]),
)

# The display is one piece of glass: the unit's own 2 x 16 LCD as it is now (the parameter
# screen), the last program screen kept beside it (the preset), the unit's mode keys as the
# words their LEDs light, and the lamps the unit shows its selectors and its AMOUNT
# destinations with, grouped by what they answer. Each of those is also where you change it
# (Field, above): the selectors' positions are picked there, a selector's name is its
# section's EDIT, the AMOUNT lamps are the AMOUNT key, a lit word is its key, and the step
# pairs that walk the program, category, parameter, value, page and transpose stand under the
# screen they walk. The module draws all of it in the fields' own rectangles. The two LCDs are
# redrawn from their characters rather than shown dot for dot, which is what lets each be a
# third of the width the unit's glass would take.

# The unit has no encoders and no selectors, only buttons -- PART -/+, a key that steps through
# four filter modes with four LEDs. A modular face can do better, so here the pairs are step
# buttons (a click is a press) and the cycles are choices that press until the unit's LEDs
# agree. Everything still reaches the firmware as a key press and comes back as an LED
# (Controls.hpp).
#
# Four gestures of the unit need one key held while another is pressed, or two pressed together, which a mouse
# cannot do, so each is a control that does it for you: MULTI+SINGLE (both together enter Multi-Single mode, and
# SINGLE or MULTI alone leaves it, so a second press leaves), CATEGORY and IN CATEGORY (SINGLE held, PARAMETER
# steps the category, VALUE steps the sounds in it) and PAGE (one PARAMETER button held, the other pressed, scrolls a
# page of parameters at a time in the direction of the held one).
#
# With every key on the glass the face is the unit's 32 pots and PART, eleven to a row. A row of
# eleven is what sets the width now, so two labels are the manual's short forms (TIME under
# DELAY/REVERB, WAVE/PW); the tooltips keep the full names.

P.sections = [
    Section("LFOS/MOD · OSCILLATORS · MIXER", groups=(1, 5, 5), rows=[
        Row([Knob("rate", "RATE"),
             Knob("shape", "SHAPE"), Knob("wave", "WAVE/PW"), Knob("semitone", "SEMITONE"),
             Knob("detune", "DETUNE 2/3"), Knob("fm", "FM AMOUNT"),
             Knob("osc_bal", "OSC BAL"), Knob("sub", "SUB OSC"), Knob("osc_vol", "OSC VOL"),
             Knob("noise", "NOISE"), Knob("ring", "RING MOD")]),
    ]),
    # Two rows on one grid, so their columns agree; the rows change gear in different places,
    # so neither's gutters are drawn.
    Section("EFFECTS · DELAY/REVERB · FILTERS · SOFT · ENVELOPES", rows=[
        Row([Knob("fx_mix", "TYPE/MIX"), Knob("fx_int", "INTENSITY"),
             Knob("dly_send", "SEND"), Knob("dly_time", "TIME"), Knob("dly_fb", "FDBK/DAMP"),
             Knob("cutoff", "CUTOFF", primary=True), Knob("cutoff2", "CUTOFF 2"),
             Knob("reso", "RESO"), Knob("env_amt", "ENV AMT"), Knob("flt_bal", "FLT BAL"),
             Stepper("part", "PART")]),
        Row([Knob("soft1", "SOFT 1"), Knob("soft2", "SOFT 2/VALUE"), Knob("volume", "VOLUME"),
             Knob("f_att", "ATTACK"), Knob("f_dec", "DECAY"), Knob("f_sus", "SUSTAIN"), Knob("f_rel", "RELEASE"),
             Knob("a_att", "ATTACK"), Knob("a_dec", "DECAY"), Knob("a_sus", "SUSTAIN"), Knob("a_rel", "RELEASE")]),
    ]),
]

# Two rows of inputs: audio and a keyboard's worth of MIDI over the clock, then the eight CVs and
# the eight gates that press keys.
P.footer = [
    Row([Jack("in_l", "IN L"), Jack("in_r", "IN R"),
         # A keyboard's worth of MIDI, as cables: V/OCT, GATE and VEL are polyphonic.
         Jack("note_v", "PITCH"), Jack("note_gate", "GATE"), Jack("note_vel", "VEL"),
         Jack("bend", "BEND"), Jack("mod", "MOD"), Jack("touch", "TOUCH"), Jack("sustain", "SUSTAIN"),
         Jack("clk", "CLOCK"), Jack("run", "RUN"), Jack("rst", "RESET"),
         # Eight CV inputs, each assignable to any of the 32 knobs.
         Jack("cv1", "CV 1"), Jack("cv2", "CV 2")],
        y=106.0),
    Row([Jack("cv3", "CV 3"), Jack("cv4", "CV 4"),
         Jack("cv5", "CV 5"), Jack("cv6", "CV 6"), Jack("cv7", "CV 7"), Jack("cv8", "CV 8"),
         # A gate presses a button.
         Jack("g_part_dn", "PART -"), Jack("g_part_up", "PART +"),
         Jack("g_param_dn", "PARAM <"), Jack("g_param_up", "PARAM >"),
         Jack("g_value_dn", "VALUE -"), Jack("g_value_up", "VALUE +"),
         Jack("g_arp", "ARP ON"), Jack("g_random", "RANDOM")],
        y=118.6),
]

# The three stereo outs stand at the right-hand edge, not along the footer, two abreast: what
# leaves the module beside the controls, inputs below.
P.rail = Rail([Jack(n, t, ink="MINT") for n, t in
               [("out1l", "1 L"), ("out1r", "1 R"), ("out2l", "2 L"),
                ("out2r", "2 R"), ("out3l", "3 L"), ("out3r", "3 R")]],
              caption="OUT", cols="auto", labels="auto")

if __name__ == "__main__":
    raise SystemExit(build(P, root=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))))

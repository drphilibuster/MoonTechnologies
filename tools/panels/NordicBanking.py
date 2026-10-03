#!/usr/bin/env python3
"""The Nordic Banking panel, declared once.

Running this file regenerates the artwork, the shared hardware, src/PanelTheme.hpp
and the previews. See ../../panelkit/README.md for the pipeline.

Nordic Banking is a Clavia Nord Lead 2X -- its own MC68331 firmware in charge of the
panel, MIDI and program memory, driving two DSP56362s -- running the OS image the user
supplies. Nordic banking, because that is where the money goes to be discreet.

Every control here is one of the unit's: the 26 knobs on the 68331's ADC, the buttons
on its key lines and the LEDs of its multiplex, grouped and named as the owner's manual
draws them (chapter 8). The firmware decides what each one does. Selectors light one
LED or a pair of neighbours, as on the unit: LFO 1's square wave is the top two lit.
The selector buttons are their lamp groups on the display: click a group to press its
button once.
"""

import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
from panelkit import *   # noqa: E402

P = Panel(
    slug="NordicBanking",
    title="NORDIC BANKING",
    what="NORD LEAD 2X SYNTHESIZER",
    form="FORM 8938",
    # The display is one piece of glass: the unit's three seven-segment digits at the left, and
    # the lamps the unit answers its selector buttons with -- waveform, destination, filter type,
    # play mode, octave -- in four columns of three groups, each group in its own cell. A selector
    # button lives on its lamp group: click the group and the button is pressed once, the same
    # momentary param the panel button was, so the firmware does its own stepping exactly as it
    # does for the unit's key. The top half of the digits is PROGRAM UP, the bottom half DOWN.
    # OCT - and OCT + are the two halves of the octave group. WHEEL is not a field: its lamps are
    # stepped by tapping SHIFT, which stays on the panel (latching, so the second functions work).
    #
    # Six lines by ten columns: the digits take two columns, each lamp column two, and each group
    # two lines.
    glass=Glass(h=22.0, grid=(6, 10), fields=[
        Field("b_up",       cell=(0, 0), span=(3, 2), kind="button"),
        Field("b_down",     cell=(3, 0), span=(3, 2), kind="button"),
        Field("b_osc1",     cell=(0, 2), span=(2, 2), kind="button"),
        Field("b_osc2",     cell=(2, 2), span=(2, 2), kind="button"),
        Field("b_ringsync", cell=(4, 2), span=(2, 2), kind="button"),
        Field("b_lfo1wave", cell=(0, 4), span=(2, 2), kind="button"),
        Field("b_lfo1dest", cell=(2, 4), span=(2, 2), kind="button"),
        Field("b_lfo2dest", cell=(4, 4), span=(2, 2), kind="button"),
        Field("b_modenv",   cell=(0, 6), span=(2, 2), kind="button"),
        Field("b_ftype",    cell=(2, 6), span=(2, 2), kind="button"),
        Field("b_fkbd",     cell=(4, 6), span=(2, 2), kind="button"),
        Field("b_play",     cell=(0, 8), span=(2, 2), kind="button"),
        Field("b_octdn",    cell=(4, 8), span=(2, 1), kind="button"),
        Field("b_octup",    cell=(4, 9), span=(2, 1), kind="button"),
    ]),
    footer_groups=(3, 4),               # a keyboard's worth of cables, then the four outputs
)

# The buttons left on the face are the ones the screen has no lamp group for: those with a
# light of their own (ARP, OSC 2's KBD TRACK, VELOCITY, DISTORTION, UNISON, AUTO, the four slots,
# VEL/MORPH, PERF MODE), STORE, and SHIFT. A lamp that belongs to a button of its own stays beside
# that button.
#
# SHIFT is a latching bezel, lit while it is down: the unit's second functions (printed in blue on its panel, here after
# a dot in the label) need SHIFT held while another button is pressed, and a mouse has one pointer. A screen field
# presses its button under SHIFT the same as the panel button did.
#
# 42 HP to 36. With eleven selector buttons and UP/DOWN on the glass, the three button rows the
# panel had came down to one and a half, and the filter and amplifier knobs -- fourteen in a row,
# which was what set the width -- split over two rows, each finished with the buttons that are
# theirs or are left. The twelve knobs of the first section are now the widest row. The height the
# button rows gave back went to the glass: 12.5 mm to 22.

P.sections = [
    Section("LFO 1 · LFO 2/ARPEGGIATOR · MOD ENV · OSCILLATORS · PROGRAM", groups=(2, 2, 3, 5), rows=[
        Row([Knob("lfo1_rate", "RATE"), Knob("lfo1_amt", "AMOUNT"),
             Knob("lfo2_rate", "RATE"), Knob("lfo2_amt", "AMT/RANGE"),
             Knob("mod_a", "ATTACK"), Knob("mod_d", "DECAY"), Knob("mod_amt", "AMOUNT"),
             Knob("semi", "SEMITONES"), Knob("fine", "FINE TUNE"),
             Knob("fm", "FM AMOUNT"), Knob("pw", "PULSE WIDTH"), Knob("mix", "MIX")]),
        Row([Button("b_arp", "ARP · HOLD", light="arp"), Button("b_kbd2", "OSC 2 KBD", light="osc2_kbd"),
             Button("b_store", "STORE"),
             Button("b_slota", "A · TUNE", light="slot_a"), Button("b_slotb", "B · OUT MODE", light="slot_b"),
             Button("b_slotc", "C · LOCAL", light="slot_c"), Button("b_slotd", "D · PRG.CTRL", light="slot_d"),
             Button("b_velmorph", "VEL/MORPH · CLEAR", light="velmorph"), Button("b_perf", "PERF MODE", light="kbdsplit")],
            own_grid=True),
    ]),
    Section("FILTER · AMPLIFIER", groups=(3, 4, 2), rows=[
        Row([Knob("cutoff", "FREQUENCY", primary=True), Knob("reso", "RESONANCE"), Knob("f_env", "ENV AMOUNT"),
             Knob("f_a", "ATTACK"), Knob("f_d", "DECAY"), Knob("f_s", "SUSTAIN"), Knob("f_r", "RELEASE"),
             Button("b_velo", "VELOCITY", light="velocity"),
             Button("b_dist", "DISTORTION · PANIC", light="distortion")]),
        Row([Knob("a_a", "ATTACK"), Knob("a_d", "DECAY"), Knob("a_s", "SUSTAIN"), Knob("a_r", "RELEASE"),
             Knob("gain", "GAIN"), Knob("porta", "PORTA"), Knob("volume", "MASTER VOL"),
             Button("b_unison", "UNISON · MIDI CH", light="unison"), Button("b_auto", "AUTO · SYSTEM", light="auto"),
             Bezel("b_shift", "SHIFT/WHEEL")], own_grid=True),
    ]),
]

P.footer = [
    Row([Jack("voct", "V/OCT"), Jack("gate", "GATE"), Jack("sustain", "SUSTAIN"),
         Jack("out_a", "OUT A", ink="MINT"), Jack("out_b", "OUT B", ink="MINT"),
         Jack("out_c", "OUT C", ink="MINT"), Jack("out_d", "OUT D", ink="MINT")],
        y=118.6),
]

if __name__ == "__main__":
    raise SystemExit(build(P, root=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))))

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
"""

import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
from panelkit import *   # noqa: E402

P = Panel(
    slug="NordicBanking",
    title="NORDIC BANKING",
    subtitle="NORD LEAD 2X",
    form="FORM 8938",
    glass=Glass(h=12.5),
)

# The display is one piece of glass: the unit's three seven-segment digits, and the lamps the
# unit answers its selector buttons with -- waveform, destination, filter type, play mode, octave
# -- grouped by the button that steps them. The module draws all of it; where it goes inside the
# glass is the widget's business. A lamp that belongs to a button of its own (ARP, VELOCITY, the
# four slots) stays beside that button.

P.sections = [
    Section("LFO 1 · LFO 2/ARPEGGIATOR · MOD ENV · OSCILLATORS", groups=(2, 2, 3, 5), rows=[
        Row([Knob("lfo1_rate", "RATE"), Knob("lfo1_amt", "AMOUNT"),
             Knob("lfo2_rate", "RATE"), Knob("lfo2_amt", "AMT/RANGE"),
             Knob("mod_a", "ATTACK"), Knob("mod_d", "DECAY"), Knob("mod_amt", "AMOUNT"),
             Knob("semi", "SEMITONES"), Knob("fine", "FINE TUNE"),
             Knob("fm", "FM AMOUNT"), Knob("pw", "PULSE WIDTH"), Knob("mix", "MIX")]),
        Row([Button("b_lfo1wave", "LFO 1 WAVE"), Button("b_lfo1dest", "LFO 1 DEST"),
             Button("b_arp", "ARP", light="arp"), Button("b_lfo2dest", "DEST/MODE"),
             Button("b_modenv", "DEST"),
             Button("b_osc1", "OSC 1"), Button("b_osc2", "OSC 2"),
             Button("b_kbd2", "KBD TRACK", light="osc2_kbd"), Button("b_ringsync", "RING/SYNC")],
            own_grid=True),
    ]),
    Section("FILTER · AMPLIFIER · PROGRAM", groups=(3, 4, 4, 1, 2), rows=[
        Row([Knob("cutoff", "FREQUENCY", primary=True), Knob("reso", "RESONANCE"), Knob("f_env", "ENV AMOUNT"),
             Knob("f_a", "ATTACK"), Knob("f_d", "DECAY"), Knob("f_s", "SUSTAIN"), Knob("f_r", "RELEASE"),
             Knob("a_a", "ATTACK"), Knob("a_d", "DECAY"), Knob("a_s", "SUSTAIN"), Knob("a_r", "RELEASE"),
             Knob("gain", "GAIN"), Knob("porta", "PORTA"), Knob("volume", "MASTER VOL")]),
        Row([Button("b_ftype", "TYPE"),
             Button("b_velo", "VELOCITY", light="velocity"), Button("b_fkbd", "KBD TRACK"),
             Button("b_dist", "DISTORTION", light="distortion"), Button("b_play", "PLAY MODE"),
             Button("b_unison", "UNISON", light="unison"), Button("b_auto", "AUTO", light="auto"),
             Button("b_shift", "SHIFT/WHEEL")], own_grid=True),
        Row([Button("b_down", "DOWN"), Button("b_up", "UP"), Button("b_store", "STORE"),
             Button("b_slota", "A", light="slot_a"), Button("b_slotb", "B", light="slot_b"),
             Button("b_slotc", "C", light="slot_c"), Button("b_slotd", "D", light="slot_d"),
             Button("b_velmorph", "VEL/MORPH", light="velmorph"), Button("b_perf", "PERF MODE", light="kbdsplit"),
             Button("b_octdn", "OCT -"), Button("b_octup", "OCT +")], own_grid=True),
    ]),
]

P.footer = [
    Row([Jack("pedal", "PEDAL"),
         Jack("out_a", "OUT A", ink="MINT"), Jack("out_b", "OUT B", ink="MINT"),
         Jack("out_c", "OUT C", ink="MINT"), Jack("out_d", "OUT D", ink="MINT")],
        y=118.6),
]

if __name__ == "__main__":
    raise SystemExit(build(P, root=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))))

#!/usr/bin/env python3
"""The Accrual panel, declared once.

Running this file regenerates the artwork, the shared hardware, src/PanelTheme.hpp
and the previews. See ../../panelkit/README.md for the pipeline.

Accrual is the 4069 VCO from the Modular in a Week course's Unfinished projects
(kristian.borgstedt, 2019-12-06) solved as the circuit it is: a BC560/BC550
exponential converter feeding a CD4069UB Miller integrator, a two-inverter Schmitt
trigger that dumps it, and a second 4069 inverter turned into a pulse-width
comparator. Four voltages sum into the pitch (TUNE and FINE are the board's two
pots, CV1 and CV2 its two jacks); the PW pot and a PWM jack set the pulse.

The read-out is the integrator itself: the voltage on the board's sawtooth node over
one cycle, with the pulse comparator's threshold drawn across it. The three values
on its edge are controls -- the pitch in hertz (it is TUNE, so drag it), FINE and
PW -- which is why FINE and PW have no knobs of their own. TUNE keeps its knob, the
one you play.
"""

import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
from panelkit import *   # noqa: E402

P = Panel(
    slug="Accrual",
    title="ACCRUAL",
    what="4069 SAW & PULSE VCO",
    form="FORM 3115",
    density="compact",
    glass=Glass(h=42.0, grid=(6, 2), fields=[
        Field("tune_hz",    cell=(0, 0), span=(1, 2), kind="value"),
        Field("fine_field", cell=(5, 0), span=(1, 1), kind="value"),
        Field("pw_field",   cell=(5, 1), span=(1, 1), kind="value"),
    ]),
)

P.sections = [
    # The exponential converter's inputs: the pot that is the pitch, and the two jacks that
    # add to it. All three sum into one node at 100k each (FINE, on its screen field, at 1M).
    Section("PITCH", rows=[
        Row([BigKnob("tune", "TUNE", primary=True)]),
        Row([Jack("cv1", "CV1"), Jack("cv2", "CV2")]),
    ]),
]

# Everything patched that is not pitch: the width of the pulse, and the two outputs.
P.footer = [
    Row([Jack("pwm", "PWM"), Jack("saw", "SAW", ink="MINT"),
         Jack("pulse", "PULSE", ink="MINT")], y=118.6),
]

if __name__ == "__main__":
    raise SystemExit(build(P, root=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))))

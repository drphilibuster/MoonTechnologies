#!/usr/bin/env python3
"""The Transmittal panel, declared once.

Running this file regenerates the artwork, the shared hardware, src/PanelTheme.hpp
and the previews. See ../../panelkit/README.md for the pipeline.

Transmittal is FORM W-3, the transmittal that accompanies what is being filed.
It is the plugin's video output: it takes frames from a source inside the plugin
-- Repossession, so far -- and hands them to an ffmpeg it spawns, which writes a
live HLS playlist that TouchDesigner reads with a Video Stream In TOP.

The read-out is most of the panel because most of what this module has to say is
text: which source, what size, what rate, and above all the path to paste into
TouchDesigner. A jack cannot say any of that.

Three controls under the well and nothing else. SOURCE picks what is being sent;
SIZE and RATE describe the stream, and both are stepped rather than continuous
because ffmpeg is told them once on its command line and changing either has to
restart it. SEND is the transport.
"""

import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
from panelkit import *   # noqa: E402

P = Panel(
    slug="Transmittal",
    title="TRANSMITTAL",
    form="FORM W-3",
    density="compact",
    # The read-out takes every millimetre the three control rows and the footer
    # band can spare, because everything this module has to say is text and the
    # playlist path -- the longest string it ever shows, and the one that has to
    # be read character by character to be pasted into TouchDesigner -- wants
    # the room more than a fourth knob would.
    glass=Glass(h=52.0),
)

P.sections = [
    # SOURCE is stepped over however many sources have registered, so its
    # detents are decided at run time and the panel only reserves the seat. The
    # light beside the caption is the transport state: dark idle, lime running,
    # clay when ffmpeg has gone away.
    Section("FILING", caption_light="state", rows=[
        Row([Knob("source", "SOURCE", steps=8),
             Knob("size", "SIZE", steps=4),
             Knob("rate", "RATE", steps=4)]),
        Row([Button("send", "SEND")]),
    ]),
]

# Video does not leave through a jack -- it leaves through the playlist ffmpeg
# writes. What is on the band is the patchable part of the transport: a gate in
# to start and stop it from the sequencer that is driving everything else, and a
# gate out that is high while frames are actually being written, so a patch can
# tell the difference between asked-to-send and sending.
P.footer = [
    Row([Jack("send_in", "SEND"),
         Jack("sending_out", "LIVE", ink="MINT")], y=118.6),
]

if __name__ == "__main__":
    raise SystemExit(build(P, root=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))))

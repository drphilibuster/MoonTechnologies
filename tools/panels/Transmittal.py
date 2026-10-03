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

There are no controls on the face any more: the read-out is the whole panel and
every setting is a line on it. Click SRC, SIZE or RATE to pick from the list (or
drag through them); click the state line to start or stop sending. SIZE and RATE
are stepped rather than continuous because ffmpeg is told them once on its
command line and changing either has to restart it.
"""

import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
from panelkit import *   # noqa: E402

P = Panel(
    slug="Transmittal",
    title="TRANSMITTAL",
    what="VIDEO OUTPUT",
    form="FORM W-3",
    density="compact",
    # Pinned: a video module's screen only ever grows. The rows would let the
    # narrow masthead take this to 9 HP, and the read-out would lose 5 mm of
    # the width the playlist path is read across.
    hp=10,
    # The read-out takes the whole face down to the footer band, because
    # everything this module has to say is text -- above all the playlist path,
    # which has to be read character by character to be pasted into
    # TouchDesigner, and now has room to wrap instead of being ellipsized.
    # Twelve lines; the first three hold the fields, the rest the route and path.
    glass=Glass(h=99.0, grid=(12, 2), fields=[
        Field("source", cell=(0, 0), span=(1, 2), kind="select"),
        Field("size",   cell=(1, 0), kind="select"),
        Field("rate",   cell=(1, 1), kind="select"),
        Field("send",   cell=(2, 0), span=(1, 2), kind="button"),
    ]),
)

P.sections = []

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

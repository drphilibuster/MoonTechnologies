#!/usr/bin/env python3
"""The SCHEDULE A panel, declared once.

Running this file regenerates the artwork, the shared hardware, src/PanelTheme.hpp
and the previews. See ../../panelkit/README.md for the pipeline.

SCHEDULE A is the itemised attachment to Repossession's FORM 1099-A: eight rows,
one per seized asset, each with the four charges that can be varied against it
and the audio it produces. Repossession carries all of this polyphonically on
four jacks -- one cable, channel N addressing step N -- which costs no panel and
patches badly. This is the same eight steps with somewhere to plug a cable.

The panel is a table and is laid out as one: the column headings are the labels
of the first row and every row below it is silent, because eight copies of
SPEED GAIN START LENGTH OUT would be a wall of type saying one thing. The rows
are identified by their lights, which wear the same lime-to-mint ramp as the
host's step buttons and timeline spans -- so which row is step 1 is answered by
looking at the host, not by counting.

hp is "auto": the panel comes out exactly as wide as five columns of jacks and
their headings need. Density is "regular" rather than Repossession's "compact"
for the opposite reason to usual -- there is only one section here, and eight
rows on the tight scale leave a third of the panel empty.
"""

import os
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
from panelkit import *   # noqa: E402

P = Panel(
    slug="ScheduleA",
    title="SCHEDULE A",
    subtitle="ITEMIZED LIENS",
    form="FORM 1099-A",
    density="regular",
)

NUM = 8


def row(i):
    """One seized asset: its light, its four charges, and what it pays out."""
    return Row([
        Light("step%d" % (i + 1)),
        Jack("speed%d" % (i + 1), "SPEED"),
        Jack("gain%d" % (i + 1), "GAIN"),
        Jack("start%d" % (i + 1), "START"),
        Jack("len%d" % (i + 1), "LENGTH"),
        Jack("out%d" % (i + 1), "OUT", ink="MINT"),
    ], silent=(i > 0))


P.sections = [
    Section("ITEMIZED", rows=[row(i) for i in range(NUM)]),
]

P.metrics = dict(NUM_SLOTS=NUM)

if __name__ == "__main__":
    raise SystemExit(build(P, root=os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", ".."))))

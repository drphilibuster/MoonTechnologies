"""Imported artwork: the part of a panel that was drawn by hand.

`tools/art_import.py` takes an SVG saved from Illustrator, cleans it down to what
Rack can draw (panelkit/svgclean.py) and stores it as `art/panels/<Module>.svg`.
That file is source, not output: it is committed, and `render.panel_svg` reads it
back, replacing the generated drawing **one stage at a time**. A stage the file
does not have is still drawn by the generator, so art can arrive a layer at a
time and a panel is never half-blank.

The stages are the `EXPORT_NN_*` layers the Illustrator templates carry, named in
STAGES below and in the same order render.py draws them.

An imported panel is only right for the layout it was drawn against: a seat is
where the solver put a control *then*. So the file records a signature of the
solved layout, and `stale()` says when a spec change has moved things since.
"""

import hashlib
import os
import re

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
DIR = os.path.join(ROOT, "art", "panels")

#: The stages of a panel, bottom of the layer panel first -- render.py's draw order.
STAGES = [
    "01_Paper", "02_Masthead_band", "03_Footer_band", "04_Margin_ribbons",
    "05_Readout_glass", "06_Section_blocks", "07_Rules", "08_Plates",
    "09_Run_boxes", "10_Pair_ties", "11_Wells", "12_Detents",
    "13_Primary_rings", "14_Traces",
]

STAGE_RE = re.compile(r"^EXPORT_(\d\d)")


def stage_name(number):
    """'11' -> '11_Wells'; None for a number that is not a stage."""
    for s in STAGES:
        if s.startswith(number + "_"):
            return s
    return None


def path(slug):
    return os.path.join(DIR, slug + ".svg")


# --- the layout signature ----------------------------------------------------

def signature(panel, sol):
    """A short hash of everything imported art is drawn against: the panel's size,
    where every control, seat, block and band sits, and where every word stands."""
    r = lambda v: round(float(v), 3)
    parts = [r(panel.w), r(panel.h), sol.band_footer and r(sol.band_footer)]
    parts += [(n, r(x), r(y), k) for n, x, y, k in sol.widgets]
    parts += [(r(x), r(y), r(a), r(b)) for x, y, a, b, _ in sol.wells]
    parts += [(r(a), r(b)) for a, b in sol.blocks]
    parts += [(r(a), r(b)) for a, b in sol.block_x if abs(a - 3.0) > 1e-6 or abs(b - (panel.w - 3.0)) > 1e-6]
    parts += [tuple(r(v) for v in g) for g in sol.groups]
    parts += [tuple(r(v) for v in rg) for rg in sol.rings]
    parts += [sol.glass and tuple(r(v) for v in sol.glass)]
    parts += [sol.glass_x and tuple(r(v) for v in sol.glass_x)]
    parts += [(l["text"], r(l["x"]), r(l["y"])) for l in sol.labels]
    return hashlib.sha1(repr(parts).encode()).hexdigest()[:12]


# --- reading an imported file -----------------------------------------------

_GROUP = re.compile(r'^<g id="(EXPORT_\d\d_[A-Za-z_]*)"([^>]*)>\n([^\n]*)\n</g>$', re.M)
_DEFS = re.compile(r"^<defs>([^\n]*)</defs>$", re.M)
_SIG = re.compile(r'data-layout="([0-9a-f]+)"')


class Art:
    def __init__(self, stages, defs, sig):
        self.stages = stages        # '11_Wells' -> (extra attributes, body)
        self.defs = defs            # gradient definitions, or ''
        self.sig = sig

    def stale(self, panel, sol):
        return self.sig != signature(panel, sol)


_cache = {}


def load(slug):
    """The imported art for a panel, or None."""
    p = path(slug)
    if not os.path.exists(p):
        return None
    mtime = os.path.getmtime(p)
    if slug in _cache and _cache[slug][0] == mtime:
        return _cache[slug][1]
    text = open(p, encoding="utf-8").read()
    stages = {}
    for m in _GROUP.finditer(text):
        stages[m.group(1)[len("EXPORT_"):]] = (m.group(2).rstrip(), m.group(3))
    d = _DEFS.search(text)
    s = _SIG.search(text)
    art = Art(stages, d.group(1) if d else "", s.group(1) if s else "")
    _cache[slug] = (mtime, art)
    return art


def group(name, attrs, body, extra=""):
    """One stage as the SVG that goes into a panel."""
    return '<g id="EXPORT_%s"%s%s>%s</g>' % (name, extra, attrs, body)


# --- hardware ---------------------------------------------------------------

def hardware(name):
    """The imported SVG for res/<name>.svg as a whole document, or None."""
    p = os.path.join(DIR, "_Hardware_%s.svg" % name)
    if not os.path.exists(p):
        return None
    text = open(p, encoding="utf-8").read()
    head = re.search(r"<svg[^>]*>", text).group(0)
    d = _DEFS.search(text)
    body = "\n".join("  " + group(m.group(1)[len("EXPORT_"):], m.group(2), m.group(3))
                     for m in _GROUP.finditer(text))
    out = ['<?xml version="1.0" encoding="UTF-8"?>',
           re.sub(r'\s+data-[a-z]+="[^"]*"', "", head)]
    if d:
        out.append("  <defs>%s</defs>" % d.group(1))
    out += [body, "</svg>", ""]
    return "\n".join(out)

#!/usr/bin/env python3
"""Illustrator templates for the family's panel art.

    python3 tools/art_templates.py              # every panel, plus the hardware
    python3 tools/art_templates.py Kickback     # one panel
    make art                                    # same as the first

Writes art/templates/<Module>.svg for each tools/panels/<Module>.py spec,
art/templates/_Hardware_*.svg for the screw and the jack, and
art/Moon Technologies.ase, the palette as an Adobe swatch file.

Nothing here is a second source of truth. Every number -- the artboard, where a
control sits, where the bands end, which words stand on a dark ground -- is read
from the solved spec, exactly as `make panel` reads it, and the artwork itself is
what panelkit/render.py draws today, split into one layer per stage. Change a
spec, run this again, and the template follows. The files are generated; never
hand-edit one and never commit one (art/templates/ is in .gitignore).

Illustrator turns each top-level <g id="..."> of an SVG into a layer, and
nested groups into groups inside it. So, from the bottom of the layer panel up:

    EXPORT_01_Paper ... EXPORT_14_Traces   the panel, one layer per stage of
                                           render.py in the order it draws them.
                                           Edit these; they are the art.
    GUIDES_*                               what Rack and the generator put on
                                           top of the art. Look, don't touch.

Only EXPORT_* layers are the panel. The artboard is the panel in millimetres.
"""

import glob
import importlib.util
import os
import re
import struct
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
sys.path.insert(0, ROOT)

from panelkit import palette as PAL, spec as S          # noqa: E402
from panelkit.layout import solve, label_box, HEADER_H  # noqa: E402
from panelkit import emit, render                       # noqa: E402

OUT = os.path.join(ROOT, "art", "templates")

# Rack's own geometry: screws are 15 px squares at (15, 0) and its mirror, and the
# bottom pair sits RACK_GRID_WIDTH above the foot. Millimetres, from panelkit.
MM = S.MM_PER_PX
SCREW_CX = (15 + 7.5) * MM            # 7.62
SCREW_TOP_CY = 7.5 * MM               # 2.54
SCREW_BOT_CY = (380 - 15 + 7.5) * MM  # 126.15
SCREW_BOT_TOP = (380 - 15) * MM       # 123.61: nothing may span the full width below this
EDGE_CLEAR = 30 * MM                  # 10.16: full-width things stay inside this x

FONT = 'font-family="Helvetica, Arial, sans-serif"'
MONO = 'font-family="Menlo, Courier, monospace"'


# --- small helpers ----------------------------------------------------------

def load_specs(only=None):
    out = []
    for f in sorted(glob.glob(os.path.join(ROOT, "tools", "panels", "*.py"))):
        slug = os.path.splitext(os.path.basename(f))[0]
        if only and slug not in only:
            continue
        sp = importlib.util.spec_from_file_location("spec_" + slug, f)
        mod = importlib.util.module_from_spec(sp)
        sp.loader.exec_module(mod)
        out.append(mod.P)
    return out


def esc(s):
    return s.replace("&", "&amp;").replace("<", "&lt;")


def layer(name, body, hidden=False):
    return ('<g id="%s" inkscape:label="%s" inkscape:groupmode="layer"%s>\n%s\n</g>'
            % (name, name, ' style="display:none"' if hidden else "", body))


def widget_extent(kind):
    return S.EXTENT.get(kind, (S.RADIUS[kind],) * 2)


def dedupe(vals, tol=0.05):
    out = []
    for v in sorted(vals):
        if not out or v - out[-1] > tol:
            out.append(v)
    return out


# --- the guide layers (SVG bodies, in panel millimetres) ---------------------

def words(panel, sol):
    """Two bodies: the labels drawn in dark ink on the paper, and in pale ink on
    a dark ground. Each word is set in the ink it will really be drawn in, over
    the ground it needs, so the contrast you must keep is visible."""
    pale, dark = [], []
    for l in emit.masthead(panel) + sol.labels:
        if not l["text"]:
            continue
        x0, y0, x1, y1 = label_box(l)
        ground = l.get("ground", "light")
        ink = PAL.ink(l["ink"], ground)[1]
        anchor = {"left": "start", "right": "end", "center": "middle"}[l["align"]]
        box = ('<rect x="%.3f" y="%.3f" width="%.3f" height="%.3f" fill="%s" '
               'stroke="%s" stroke-width="0.14"/>'
               % (x0 - 0.3, y0 - 0.3, x1 - x0 + 0.6, y1 - y0 + 0.6,
                  PAL.PAPER if ground == "light" else PAL.BAND,
                  "#d6336c" if ground == "light" else "#2f6fed"))
        mirror = (' transform="translate(%.3f 0) scale(-1 1)"' % (2 * l["x"])
                  if l.get("mirror") else "")
        txt = ('<text x="%.3f" y="%.3f" %s font-weight="700" font-size="%.3f" '
               'text-anchor="%s" fill="%s"%s>%s</text>'
               % (l["x"], l["y"], FONT, l["size"] * MM, anchor, ink, mirror,
                  esc(l["text"])))
        (pale if ground == "light" else dark).append(box + txt)
    return "\n".join(pale), "\n".join(dark)


def controls(sol):
    colour = {"jack": "#00c853", "light": "#d500f9", "light_small": "#d500f9"}
    o = []
    for name, x, y, kind in sol.widgets:
        hw, hh = widget_extent(kind)
        c = colour.get(kind, "#ff1744")
        if abs(hw - hh) < 1e-6:
            o.append('<circle cx="%.3f" cy="%.3f" r="%.3f" fill="%s" fill-opacity="0.30" '
                     'stroke="%s" stroke-width="0.15"/>' % (x, y, hw, c, c))
        else:
            o.append('<rect x="%.3f" y="%.3f" width="%.3f" height="%.3f" rx="0.6" '
                     'fill="%s" fill-opacity="0.30" stroke="%s" stroke-width="0.15"/>'
                     % (x - hw, y - hh, 2 * hw, 2 * hh, c, c))
        o.append('<path d="M %.3f %.3f h 1.6 M %.3f %.3f v 1.6 M %.3f %.3f h -1.6 '
                 'M %.3f %.3f v -1.6" stroke="%s" stroke-width="0.12" fill="none"/>'
                 % (x - 0.8, y, x, y - 0.8, x + 0.8, y, x, y + 0.8, c))
        o.append('<text x="%.3f" y="%.3f" %s font-size="1.15" text-anchor="middle" '
                 'fill="%s">%s</text>' % (x, y + hh + 1.2, MONO, c, esc(name)))
    return "\n".join(o)


def seats(sol):
    o = []
    st = 'fill="none" stroke="#ff9100" stroke-width="0.14"'
    for x, y, hw, hh, _n in sol.wells:
        if abs(hw - hh) < 1e-6:
            o.append('<circle cx="%.3f" cy="%.3f" r="%.3f" %s/>' % (x, y, hw, st))
        else:
            o.append('<rect x="%.3f" y="%.3f" width="%.3f" height="%.3f" rx="%.2f" %s/>'
                     % (x - hw, y - hh, 2 * hw, 2 * hh, min(hw, hh) * 0.35, st))
    for x, y, r in sol.rings:
        o.append('<circle cx="%.3f" cy="%.3f" r="%.3f" %s/>' % (x, y, r, st))
    return "\n".join(o)


def screws(w):
    o = []
    for cx in (SCREW_CX, w - SCREW_CX):
        for cy in (SCREW_TOP_CY, SCREW_BOT_CY):
            o.append('<circle cx="%.3f" cy="%.3f" r="2.54" fill="#ffd600" fill-opacity="0.35" '
                     'stroke="#ffd600" stroke-width="0.15"/>' % (cx, cy))
            o.append('<circle cx="%.3f" cy="%.3f" r="3.6" fill="none" stroke="#ffd600" '
                     'stroke-width="0.1"/>' % (cx, cy))
    return "\n".join(o)


def limits(panel, sol, w, h):
    """Where the bands, the blocks and the read-out stand, with their names, and
    the hard limits Rack imposes on anything wide."""
    o = []
    line = 'fill="none" stroke="#00b8d4" stroke-width="0.16"'
    o.append('<rect x="0" y="0" width="%.3f" height="%.3f" %s/>' % (w, HEADER_H, line))
    if sol.band_footer is not None:
        o.append('<rect x="0" y="%.3f" width="%.3f" height="%.3f" %s/>'
                 % (sol.band_footer, w, h - sol.band_footer, line))
    for i, (y0, y1) in enumerate(sol.blocks):
        cap = " " + panel.sections[i].caption if i < len(panel.sections) else ""
        o.append('<rect x="%.3f" y="%.3f" width="%.3f" height="%.3f" rx="%.2f" %s/>'
                 % (render.BLOCK_INSET, y0, w - 2 * render.BLOCK_INSET, y1 - y0,
                    render.BLOCK_R, line))
        o.append('<text x="%.3f" y="%.3f" %s font-size="1.1" text-anchor="end" '
                 'fill="#00b8d4">BLOCK %d%s  %.2f - %.2f mm</text>'
                 % (w - render.BLOCK_INSET - 1.0, y1 - 0.6, MONO, i + 1, esc(cap), y0, y1))
    if sol.glass:
        gy, gh = sol.glass
        o.append('<rect x="4.2" y="%.3f" width="%.3f" height="%.3f" rx="%.2f" %s/>'
                 % (gy, w - 8.4, gh, render.WELL_R, line))
        o.append('<text x="%.3f" y="%.3f" %s font-size="1.1" text-anchor="end" '
                 'fill="#00b8d4">READ-OUT  %.2f - %.2f mm</text>'
                 % (w - 5.0, gy + gh - 0.5, MONO, gy, gy + gh))
    for pl in panel.plates:
        o.append('<rect x="%.3f" y="%.3f" width="%.3f" height="%.3f" rx="%.2f" %s/>'
                 % (pl.x, pl.y, pl.w, pl.h, pl.r, line))
    for x0, y0, x1, y1 in sol.groups:
        o.append('<rect x="%.3f" y="%.3f" width="%.3f" height="%.3f" rx="1.1" %s/>'
                 % (x0, y0, x1 - x0, y1 - y0, line))
    # Nothing may run across the full width below the bottom screws' top edge, and
    # a full-width line must stay inside the screw columns.
    red = 'stroke="#ff1744" stroke-width="0.16" fill="none" stroke-dasharray="1.2 0.8"'
    o.append('<path d="M 0 %.3f H %.3f" %s/>' % (SCREW_BOT_TOP, w, red))
    o.append('<text x="%.3f" y="%.3f" %s font-size="1.0" fill="#ff1744" '
             'text-anchor="middle">FULL-WIDTH FLOOR %.2f mm</text>'
             % (w / 2, SCREW_BOT_TOP + 1.2, MONO, SCREW_BOT_TOP))
    for x in (EDGE_CLEAR, w - EDGE_CLEAR):
        o.append('<path d="M %.3f %.3f V %.3f" %s/>' % (x, h - 6, h, red))
    return "\n".join(o)


def alignment(panel, sol, w, h):
    """Hairlines through every control's centre, the screws, the bands and the
    blocks. Illustrator has no guides in SVG; select this layer and press
    Cmd-5 (View > Guides > Make Guides) to turn them into real ones."""
    xs = [render.BLOCK_INSET, w - render.BLOCK_INSET, SCREW_CX, w - SCREW_CX,
          EDGE_CLEAR, w - EDGE_CLEAR]
    ys = [HEADER_H, SCREW_TOP_CY, SCREW_BOT_CY, SCREW_BOT_TOP]
    if sol.band_footer is not None:
        ys.append(sol.band_footer)
    for y0, y1 in sol.blocks:
        ys += [y0, y1]
    xs += [x for _, x, _, _ in sol.widgets]
    ys += [y for _, _, y, _ in sol.widgets]
    st = 'stroke="#00b8d4" stroke-width="0.08" fill="none"'
    o = ['<path d="M %.3f 0 V %.3f" %s/>' % (x, h, st) for x in dedupe(xs)]
    o += ['<path d="M 0 %.3f H %.3f" %s/>' % (y, w, st) for y in dedupe(ys)]
    return "\n".join(o)


# --- assembling a panel template -------------------------------------------

def svg_open(w, h, units):
    return ('<?xml version="1.0" encoding="UTF-8"?>\n'
            '<svg xmlns="http://www.w3.org/2000/svg" '
            'xmlns:inkscape="http://www.inkscape.org/namespaces/inkscape" '
            'width="%s" height="%s" viewBox="0 0 %.4f %.4f" version="1.1">'
            % (units(w), units(h), w, h))


def build_panel(panel, sol):
    w, h = panel.w, panel.h
    art = render.panel_svg(panel, sol, layers=True)
    # panel_svg opens its own <svg>; keep its layers, supply our own wrapper
    art = art[art.index(">", art.index("<svg")) + 1:art.rindex("</svg>")]
    pale, dark = words(panel, sol)
    guides = [
        layer("GUIDES_Alignment_lines", alignment(panel, sol, w, h)),
        layer("GUIDES_Limits", limits(panel, sol, w, h)),
        layer("GUIDES_Screws", screws(w)),
        layer("GUIDES_Seats", seats(sol)),
        layer("GUIDES_Controls", controls(sol)),
        layer("GUIDES_Words_on_DARK_ground", dark),
        layer("GUIDES_Words_on_PALE_ground", pale),
    ]
    return (svg_open(w, h, lambda v: "%.4fmm" % v) + "\n" + art + "\n"
            + "\n".join(guides) + "\n</svg>\n")


# --- hardware templates (the screw and the jack) ----------------------------

SHAPE = re.compile(r"^\s*(<(?:circle|polygon)[^>]*/>)\s*$")


def build_hardware():
    """Layered versions of res/ScrewHex.svg and the eight res/Port*.svg, one
    layer per shape, bottom first -- which is the order render.py draws them."""
    from panelkit import art as ART
    out = {}
    pcs = ["Rim", "Collar", "Throat_band", "Timing_line", "Glass", "Hole"]
    scs = ["Rim", "Face", "Dome", "Hex"]

    def layered(svg, names, w, h, rings, centre):
        shapes = [m.group(1) for m in map(SHAPE.match, svg.splitlines()) if m]
        names = [n for n in names]
        if len(shapes) < len(names):         # no timing line on a level port
            names.remove("Timing_line")
        ls = [layer("EXPORT_%02d_%s" % (i + 1, n), sh)
              for i, (n, sh) in enumerate(zip(names, shapes))]
        g = "".join('<circle cx="%.4f" cy="%.4f" r="%.2f" fill="none" stroke="#ff1744" '
                    'stroke-width="0.1"/>' % (centre[0], centre[1], r) for r in rings)
        g += ('<path d="M %.4f 0 V %.4f M 0 %.4f H %.4f" stroke="#00b8d4" '
              'stroke-width="0.06" fill="none"/>' % (centre[0], h, centre[1], w))
        ls.append(layer("GUIDES_Radii_and_centre", g))
        return (svg_open(w, h, lambda v: "%gpx" % v) + "\n" + "\n".join(ls)
                + "\n</svg>\n")

    out["ScrewHex"] = layered(render.screw_svg(art=False), scs, 15.0, 14.9989,
                              (6.9, 6.2, 5.3, 3.15), (7.5, 7.49945))
    for name, kw in (("PortIn", {}), ("PortOut", dict(accent=PAL.MINT)),
                     ("PortTrigIn", dict(event=True)),
                     ("PortTrigOut", dict(accent=PAL.MINT, event=True)),
                     ("PortInMain", dict(main=True)),
                     ("PortOutMain", dict(accent=PAL.MINT, main=True)),
                     ("PortTrigInMain", dict(event=True, main=True)),
                     ("PortTrigOutMain", dict(accent=PAL.MINT, event=True, main=True))):
        out[name] = layered(render.port_svg(art=False, **kw), pcs, 23.7, 23.7,
                            (11.10, 10.45, 9.60, 8.60, 7.70, 4.40), (11.85, 11.85))
    # Hardware that has been imported is edited where it stands: the stored file
    # is already one layer per shape, so hand it back with the guides on top.
    for name in list(out):
        p = os.path.join(ART.DIR, "_Hardware_%s.svg" % name)
        if os.path.exists(p):
            text = open(p, encoding="utf-8").read()
            guides = re.search(r'<g id="GUIDES_Radii_and_centre".*?</g>', out[name], re.S)
            out[name] = text.replace("</svg>", (guides.group(0) if guides else "") + "\n</svg>")
    return out


# --- swatches (Adobe Swatch Exchange) ---------------------------------------

def swatches():
    rows = [(n, h) for n, h, _ in PAL.CPP]
    rows += [("A_DARK_PLUM (anchor)", PAL.A_DARK_PLUM), ("A_PALE_PLUM (anchor)", PAL.A_PALE_PLUM),
             ("A_SAGE (anchor)", PAL.A_SAGE), ("A_DARK_GREEN (anchor)", PAL.A_DARK_GREEN),
             ("A_PALE_GREEN (anchor)", PAL.A_PALE_GREEN)]
    for n in ("BRASS_RIM", "BRASS", "BRASS_MID", "BRASS_DARK", "GOLD", "GOLD_RIM"):
        rows.append((n, getattr(PAL, n)))
    seen, out = set(), []
    for n, h in rows:
        if n not in seen:
            seen.add(n)
            out.append(("%s %s" % (n, h.upper()), h))
    return out


def ase(colors):
    """Adobe Swatch Exchange: opens in Illustrator via Window > Swatches > menu >
    Open Swatch Library > Other Library."""
    blocks = b""
    for name, h in colors:
        h = h.lstrip("#")
        r, g, b = (int(h[i:i + 2], 16) / 255.0 for i in (0, 2, 4))
        nm = (name + "\0").encode("utf-16-be")
        body = struct.pack(">H", len(name) + 1) + nm + b"RGB " + struct.pack(">fff", r, g, b) \
            + struct.pack(">H", 2)
        blocks += struct.pack(">HI", 0x0001, len(body)) + body
    return b"ASEF" + struct.pack(">HHI", 1, 0, len(colors)) + blocks


# --- driver -----------------------------------------------------------------

def main(argv):
    only = [a for a in argv if not a.startswith("-")]
    os.makedirs(OUT, exist_ok=True)
    for p in load_specs(only or None):
        sol = solve(p)
        svg = build_panel(p, sol)
        with open(os.path.join(OUT, p.slug + ".svg"), "w") as f:
            f.write(svg)
        print("  wrote %-18s %4d HP  %6.1f x %5.1f mm  %4d KB"
              % (p.slug, p.hp, p.w, p.h, len(svg) // 1024))
    if not only:
        for name, svg in build_hardware().items():
            with open(os.path.join(OUT, "_Hardware_%s.svg" % name), "w") as f:
                f.write(svg)
            print("  wrote _Hardware_%s" % name)
        with open(os.path.join(ROOT, "art", "Moon Technologies.ase"), "wb") as f:
            f.write(ase(swatches()))
        print("  wrote art/Moon Technologies.ase")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

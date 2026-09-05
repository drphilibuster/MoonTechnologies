"""SVG output: the panel face, plus the hardware every panel shares.

Two hard constraints this file is written around, both inherited from Rack's
renderer and both still binding:

1. Rack draws panels through nanosvg and then its own svgDraw(). Between them
   they discard <text>, <pattern>, <use>, <clipPath>, <mask>, <image>,
   <style>/class=, stroke-dasharray, and every gradient stop but the first and
   last. So a panel is flat fills and explicit geometry only, and every label is
   drawn at runtime from the table in the generated header. `display:none` IS
   honoured, which is what keeps the components layer out of the render.

2. Nothing may straddle an edge or a screw. lint.py enforces it; this file just
   has to give it honest geometry to check.
"""

import math
from . import spec as S
from . import palette as P
from .layout import cap_h, desc_h, label_box, HEADER_H

TAB_W = 7.0             # the index tab on every block: a form's thumb index
TAB_H = 0.5
BLOCK_INSET = 3.0       # felt blocks sit this far in from the panel edge
BLOCK_R = 1.6
WELL_R = 1.2
TRACE_W = 0.32
TRACE_CLEAR = 0.9       # how far a trace breaks around a label's box


def panel_svg(panel, sol):
    w, h = panel.w, panel.h
    o = []
    a = o.append
    a('<?xml version="1.0" encoding="UTF-8"?>')
    a('<svg xmlns="http://www.w3.org/2000/svg" '
      'xmlns:inkscape="http://www.inkscape.org/namespaces/inkscape" '
      'width="%.4fmm" height="%.4fmm" viewBox="0 0 %.4f %.4f" version="1.1">'
      % (w, h, w, h))
    a('<g inkscape:label="panel" inkscape:groupmode="layer" id="panel">')
    a('  <rect x="0" y="0" width="%.4f" height="%.4f" fill="%s"/>' % (w, h, P.INK))

    # --- masthead: a double rule, the way an official form heads a page
    a('  <rect x="0" y="0" width="%.4f" height="%.4f" fill="%s"/>' % (w, HEADER_H, P.BAND))
    a('  <rect x="0" y="%.4f" width="%.4f" height="0.45" fill="%s"/>'
      % (HEADER_H - 0.45, w, P.LIME))
    a('  <rect x="0" y="%.4f" width="%.4f" height="0.18" fill="%s"/>'
      % (HEADER_H + 0.5, w, P.RULE))

    # --- footer band: the signature block, tabbed in mint because what leaves
    # the module leaves from here
    if sol.band_footer is not None:
        a('  <rect x="0" y="%.4f" width="%.4f" height="%.4f" fill="%s"/>'
          % (sol.band_footer, w, h - sol.band_footer, P.BAND))
        a('  <rect x="0" y="%.4f" width="%.4f" height="0.18" fill="%s"/>'
          % (sol.band_footer, w, P.RULE))
        a('  <rect x="%.4f" y="%.4f" width="%.4f" height="%.4f" rx="0.25" fill="%s"/>'
          % (BLOCK_INSET, sol.band_footer, TAB_W, TAB_H, P.MINT))

    # --- read-out well
    if sol.glass:
        gy, gh = sol.glass
        a('  <rect x="4.2" y="%.4f" width="%.4f" height="%.4f" rx="%.2f" fill="%s" '
          'stroke="%s" stroke-width="0.3"/>'
          % (gy, w - 8.4, gh, WELL_R, P.GLASS, P.RULE))

    # --- felt section blocks
    for y0, y1 in sol.blocks:
        a('  <rect x="%.4f" y="%.4f" width="%.4f" height="%.4f" rx="%.2f" fill="%s"/>'
          % (BLOCK_INSET, y0, w - 2 * BLOCK_INSET, y1 - y0, BLOCK_R, P.FELT))
        a('  <rect x="%.4f" y="%.4f" width="%.4f" height="%.4f" rx="0.25" fill="%s"/>'
          % (BLOCK_INSET, y0, TAB_W, TAB_H, P.LIME))

    # --- subtotal rules inside blocks
    for y in sol.rules:
        a('  <rect x="%.4f" y="%.4f" width="%.4f" height="0.22" fill="%s"/>'
          % (BLOCK_INSET + 3.0, y, w - 2 * BLOCK_INSET - 6.0, P.RULE))

    # --- rectangular plates: fields, list wells, buttons on panels that are
    # mostly one live display
    for pl in panel.plates:
        stroke = (' stroke="%s" stroke-width="0.3"' % P.RULE) if pl.stroke else ""
        a('  <rect x="%.4f" y="%.4f" width="%.4f" height="%.4f" rx="%.2f" fill="%s"%s/>'
          % (pl.x, pl.y, pl.w, pl.h, pl.r, pl.fill or P.GLASS, stroke))
        if pl.tab:
            a('  <rect x="%.4f" y="%.4f" width="%.4f" height="%.4f" rx="0.25" fill="%s"/>'
              % (pl.x, pl.y, TAB_W, TAB_H, getattr(P, pl.tab)))

    # --- recessed seats behind every widget
    for x, y, hw, hh in sol.wells:
        if abs(hw - hh) < 1e-6:
            a('  <circle cx="%.4f" cy="%.4f" r="%.4f" fill="%s"/>' % (x, y, hw, P.BAND))
        else:
            a('  <rect x="%.4f" y="%.4f" width="%.4f" height="%.4f" rx="%.2f" fill="%s"/>'
              % (x - hw, y - hh, 2 * hw, 2 * hh, min(hw, hh) * 0.35, P.BAND))

    # --- the primary action: one lime ring per panel, so the control you reach
    # for is the one the eye lands on first
    for x, y, r in sol.rings:
        a('  <circle cx="%.4f" cy="%.4f" r="%.4f" fill="none" stroke="%s" '
          'stroke-width="0.32"/>' % (x, y, r, P.LIME))

    # --- traces, broken around any label they would otherwise cross
    boxes = _label_boxes(sol)
    for tr in panel.traces:
        for seg in _break_polyline(tr.points, boxes):
            _emit_seg(a, seg)
        for dx, dy in tr.dots:
            a('  <circle cx="%.4f" cy="%.4f" r="0.62" fill="%s"/>' % (dx, dy, P.LIME))

    a('</g>')

    # --- components layer: hidden, read by `helper.py createmodule`
    a('<g inkscape:label="components" inkscape:groupmode="layer" id="components" '
      'style="display:none">')
    colour = {"jack": "#00ff00", "light": "#ff00ff"}
    for name, x, y, kind in sol.widgets:
        a('  <circle inkscape:label="%s" cx="%.4f" cy="%.4f" r="1" fill="%s"/>'
          % (name, x, y, colour.get(kind, "#ff0000")))
    a('</g>')
    a('</svg>')
    return "\n".join(o) + "\n"


# --- trace routing ----------------------------------------------------------

def _label_boxes(sol):
    out = []
    for l in sol.labels:
        if not l["text"]:
            continue
        x0, y0, x1, y1 = label_box(l)
        out.append((x0 - TRACE_CLEAR, y0 - TRACE_CLEAR,
                    x1 + TRACE_CLEAR, y1 + TRACE_CLEAR))
    return out


def _break_polyline(points, boxes):
    """Split each axis-aligned segment wherever it enters a label's box."""
    segs = []
    for (x0, y0), (x1, y1) in zip(points, points[1:]):
        segs.extend(_break_seg(x0, y0, x1, y1, boxes))
    return segs


def _break_seg(x0, y0, x1, y1, boxes):
    vertical = abs(x1 - x0) < 1e-6
    if vertical:
        lo, hi = min(y0, y1), max(y0, y1)
        cuts = [(max(lo, b[1]), min(hi, b[3])) for b in boxes
                if b[0] <= x0 <= b[2] and b[3] > lo and b[1] < hi]
    else:
        lo, hi = min(x0, x1), max(x0, x1)
        cuts = [(max(lo, b[0]), min(hi, b[2])) for b in boxes
                if b[1] <= y0 <= b[3] and b[2] > lo and b[0] < hi]
    spans = [(lo, hi)]
    for c0, c1 in sorted(cuts):
        nxt = []
        for s0, s1 in spans:
            if c1 <= s0 or c0 >= s1:
                nxt.append((s0, s1))
                continue
            if c0 > s0:
                nxt.append((s0, c0))
            if c1 < s1:
                nxt.append((c1, s1))
        spans = nxt
    if vertical:
        return [(x0, s0, x0, s1) for s0, s1 in spans if s1 - s0 > 0.4]
    return [(s0, y0, s1, y0) for s0, s1 in spans if s1 - s0 > 0.4]


def _emit_seg(a, seg):
    x0, y0, x1, y1 = seg
    if abs(x1 - x0) < 1e-6:
        a('  <rect x="%.4f" y="%.4f" width="%.4f" height="%.4f" fill="%s"/>'
          % (x0 - TRACE_W / 2, y0, TRACE_W, y1 - y0, P.LIME))
    else:
        a('  <rect x="%.4f" y="%.4f" width="%.4f" height="%.4f" fill="%s"/>'
          % (x0, y0 - TRACE_W / 2, x1 - x0, TRACE_W, P.LIME))


# --- shared hardware --------------------------------------------------------
# Every plugin gets byte-identical copies of these, generated rather than copied,
# so "the family screw" is a fact rather than a convention.

def screw_svg():
    """A hex-head brass screw on the stock ScrewSilver canvas.

    The 15 x 14.9989 px canvas is not a typo and not negotiable: SvgScrew takes
    its box size straight from the SVG, so matching the stock canvas is what lets
    the screw drop in without moving anything.
    """
    w, h = 15.0, 14.9989
    cx, cy = w / 2, h / 2
    pts = " ".join("%.4f,%.4f" % (cx + 3.15 * math.cos(math.radians(t)),
                                  cy + 3.15 * math.sin(math.radians(t)))
                   for t in range(0, 360, 60))
    return "\n".join([
        '<?xml version="1.0" encoding="UTF-8"?>',
        '<svg xmlns="http://www.w3.org/2000/svg" width="%gpx" height="%gpx" '
        'viewBox="0 0 %g %g" version="1.1">' % (w, h, w, h),
        '  <circle cx="%.4f" cy="%.4f" r="6.9" fill="%s"/>' % (cx, cy, P.BRASS_RIM),
        '  <circle cx="%.4f" cy="%.4f" r="6.2" fill="%s"/>' % (cx, cy, P.BRASS),
        '  <circle cx="%.4f" cy="%.4f" r="5.3" fill="%s"/>' % (cx, cy, P.BRASS_MID),
        '  <polygon points="%s" fill="%s"/>' % (pts, P.BRASS_DARK),
        '</svg>', ""])


def port_svg(accent=None):
    """A brass-collared jack on the stock PJ301M canvas (23.7 px square).

    The stock port's chrome collar is the loudest off-palette object on a green
    panel; brass puts it in the same drawer as the screws. `accent` rings the
    throat in mint to mark an output.
    """
    d = 23.7
    c = d / 2
    ring = accent or P.BRASS_MID
    return "\n".join([
        '<?xml version="1.0" encoding="UTF-8"?>',
        '<svg xmlns="http://www.w3.org/2000/svg" width="%gpx" height="%gpx" '
        'viewBox="0 0 %g %g" version="1.1">' % (d, d, d, d),
        '  <circle cx="%.4f" cy="%.4f" r="11.85" fill="%s"/>' % (c, c, P.BRASS_DARK),
        '  <circle cx="%.4f" cy="%.4f" r="11.10" fill="%s"/>' % (c, c, P.BRASS),
        '  <circle cx="%.4f" cy="%.4f" r="9.60" fill="%s"/>' % (c, c, ring),
        '  <circle cx="%.4f" cy="%.4f" r="7.70" fill="%s"/>' % (c, c, P.BAND),
        '  <circle cx="%.4f" cy="%.4f" r="4.40" fill="#000000"/>' % (c, c),
        '</svg>', ""])


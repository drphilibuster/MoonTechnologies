"""SVG output: the panel face, plus the hardware every panel shares.

The face is a banknote: pale paper, a dark masthead and footer band, section
blocks framed in sage guilloche with a scroll in each corner, braided ribbons
running the margins, and every wire between controls drawn as an engraved
wave rather than a straight rule. Everything is explicit geometry -- filled
rects, circles and polyline paths -- because of the two constraints below.

Two hard constraints this file is written around, both inherited from Rack's
renderer and both still binding:

1. Rack draws panels through nanosvg and then its own svgDraw(). Between them
   they discard <text>, <pattern>, <use>, <clipPath>, <mask>, <image>,
   <style>/class=, stroke-dasharray, and every gradient stop but the first and
   last. So a panel is flat fills and explicit geometry only, and every label is
   drawn at runtime from the table in the generated header. `display:none` IS
   honoured, which is what keeps the components layer out of the render.
   Paths with straight segments and opacity attributes ARE honoured, which is
   what the guilloche is made of.

2. Nothing may straddle an edge or a screw. lint.py enforces it; this file just
   has to give it honest geometry to check.
"""

import math
from . import spec as S
from . import palette as P
from .layout import cap_h, desc_h, label_box, HEADER_H, SCALE, FOOT_Y

TAB_W = 7.0             # the index tab on every block: a form's thumb index
TAB_H = 0.5
BLOCK_INSET = 3.0       # felt blocks sit this far in from the panel edge
BLOCK_R = 1.6
WELL_R = 1.2
TRACE_W = 0.30
TRACE_AMP = 0.28        # how far a trace wave strays from its centreline
TRACE_PERIOD = 3.2
TRACE_CLEAR = 0.9       # how far a trace breaks around a label's box
FRAME_W = 0.22          # the engraved frame round every block
RIBBON_W = 0.16         # a guilloche strand
SCREW_CLEAR = 11.2      # x at which the corner screws stop, plus a millimetre


def panel_svg(panel, sol):
    w, h = panel.w, panel.h
    m = SCALE[panel.density]
    o = []
    a = o.append
    a('<?xml version="1.0" encoding="UTF-8"?>')
    a('<svg xmlns="http://www.w3.org/2000/svg" '
      'xmlns:inkscape="http://www.inkscape.org/namespaces/inkscape" '
      'width="%.4fmm" height="%.4fmm" viewBox="0 0 %.4f %.4f" version="1.1">'
      % (w, h, w, h))
    a('<g inkscape:label="panel" inkscape:groupmode="layer" id="panel">')
    a('  <rect x="0" y="0" width="%.4f" height="%.4f" fill="%s"/>' % (w, h, P.PAPER))

    # --- masthead: the dark band a note prints its denomination on, edged in
    # an engraved braid between the two screws and closed by a sage rule
    a('  <rect x="0" y="0" width="%.4f" height="%.4f" fill="%s"/>' % (w, HEADER_H, P.BAND))
    _ribbon(a, SCREW_CLEAR, w - SCREW_CLEAR, 1.35, horizontal=True, amp=0.55,
            period=5.0, ink=P.RULE, opacity=0.9)
    a('  <rect x="0" y="%.4f" width="%.4f" height="0.35" fill="%s"/>'
      % (HEADER_H - 0.35, w, P.RULE))

    # --- footer band: the signature block, tabbed in mint because what leaves
    # the module leaves from here. A braid runs under the jacks, between the
    # bottom screws.
    if sol.band_footer is not None:
        a('  <rect x="0" y="%.4f" width="%.4f" height="%.4f" fill="%s"/>'
          % (sol.band_footer, w, h - sol.band_footer, P.BAND))
        a('  <rect x="0" y="%.4f" width="%.4f" height="0.3" fill="%s"/>'
          % (sol.band_footer, w, P.RULE))
        a('  <rect x="%.4f" y="%.4f" width="%.4f" height="%.4f" rx="0.25" fill="%s"/>'
          % (BLOCK_INSET, sol.band_footer, TAB_W, TAB_H, P.MINT))
    _ribbon(a, SCREW_CLEAR, w - SCREW_CLEAR, 126.4, horizontal=True, amp=0.55,
            period=5.0, ink=P.RULE, opacity=0.9)

    # --- the margins: two braided strands running the height of the face, the
    # way a note's border runs round its engraving. They stop short of the
    # bands so the frame reads as one closed figure.
    top = HEADER_H + 1.4
    bot = (sol.band_footer if sol.band_footer is not None else 123.0) - 1.4
    if bot - top > 12.0:
        for x in (1.55, w - 1.55):
            _ribbon(a, top, bot, x, horizontal=False, amp=0.5, period=6.0,
                    ink=P.RULE, opacity=0.55)

    # --- read-out well
    if sol.glass:
        gy, gh = sol.glass
        a('  <rect x="4.2" y="%.4f" width="%.4f" height="%.4f" rx="%.2f" fill="%s" '
          'stroke="%s" stroke-width="0.3"/>'
          % (gy, w - 8.4, gh, WELL_R, P.GLASS, P.RULE))

    # --- section blocks: pale plates framed in sage, a scroll curled into each
    # free corner and the thumb-index tab where a scroll would be
    scroll = max(1.2, min(2.1, m["BOT_CLEAR"] - 0.5))
    for y0, y1 in sol.blocks:
        a('  <rect x="%.4f" y="%.4f" width="%.4f" height="%.4f" rx="%.2f" fill="%s" '
          'stroke="%s" stroke-width="%.2f"/>'
          % (BLOCK_INSET, y0, w - 2 * BLOCK_INSET, y1 - y0, BLOCK_R, P.FELT,
             P.RULE, FRAME_W))
        a('  <rect x="%.4f" y="%.4f" width="%.4f" height="%.4f" rx="0.25" fill="%s"/>'
          % (BLOCK_INSET, y0, TAB_W, TAB_H, P.INK))
        x0, x1 = BLOCK_INSET, w - BLOCK_INSET
        _scroll(a, x1, y0, -1, 1, scroll)     # top-right
        _scroll(a, x0, y1, 1, -1, scroll)     # bottom-left
        _scroll(a, x1, y1, -1, -1, scroll)    # bottom-right

    # --- subtotal rules inside blocks, with a bead at each end
    for y in sol.rules:
        xa, xb = BLOCK_INSET + 3.0, w - BLOCK_INSET - 3.0
        a('  <rect x="%.4f" y="%.4f" width="%.4f" height="0.2" fill="%s"/>'
          % (xa, y - 0.1, xb - xa, P.RULE))
        for x in (xa, xb):
            a('  <circle cx="%.4f" cy="%.4f" r="0.42" fill="%s"/>' % (x, y, P.RULE))

    # --- rectangular plates: fields, list wells, buttons on panels that are
    # mostly one live display
    for pl in panel.plates:
        stroke = (' stroke="%s" stroke-width="0.3"' % P.RULE) if pl.stroke else ""
        a('  <rect x="%.4f" y="%.4f" width="%.4f" height="%.4f" rx="%.2f" fill="%s"%s/>'
          % (pl.x, pl.y, pl.w, pl.h, pl.r, pl.fill or P.GLASS, stroke))
        if pl.tab:
            a('  <rect x="%.4f" y="%.4f" width="%.4f" height="%.4f" rx="0.25" fill="%s"/>'
              % (pl.x, pl.y, TAB_W, TAB_H, getattr(P, pl.tab)))

    # --- recessed seats behind every widget: a dark seal ringed in sage
    for x, y, hw, hh, _name in sol.wells:
        if abs(hw - hh) < 1e-6:
            a('  <circle cx="%.4f" cy="%.4f" r="%.4f" fill="%s" stroke="%s" '
              'stroke-width="0.2"/>' % (x, y, hw, P.GLASS, P.RULE))
        else:
            a('  <rect x="%.4f" y="%.4f" width="%.4f" height="%.4f" rx="%.2f" fill="%s" '
              'stroke="%s" stroke-width="0.2"/>'
              % (x - hw, y - hh, 2 * hw, 2 * hh, min(hw, hh) * 0.35, P.GLASS, P.RULE))

    # --- a stepped knob is a switch wearing a knob's clothes, so the panel says
    # so: an engraved arc through the sweep it actually has, with a subdivider at
    # every detent, struck into the dark of its own well. Rack knobs sweep
    # -135 deg to +135 deg from straight up, so the ticks land exactly where the
    # pointer will -- and because the whole figure lives inside the well, a
    # selector takes up no more of the panel than the plain knob it replaces.
    for x, y, r, n in sol.steps:
        _detents(a, x, y, r, n)

    # --- the primary action: a double ring, the way a seal is struck twice, so
    # the control you reach for is the one the eye lands on first
    for x, y, r in sol.rings:
        a('  <circle cx="%.4f" cy="%.4f" r="%.4f" fill="none" stroke="%s" '
          'stroke-width="0.32"/>' % (x, y, r, P.RULE))
        a('  <circle cx="%.4f" cy="%.4f" r="%.4f" fill="none" stroke="%s" '
          'stroke-width="0.16"/>' % (x, y, r - 0.6, P.RULE))

    # --- traces, engraved as waves and broken around any label they would
    # otherwise cross; a rosette wherever a wire is tied off
    boxes = _label_boxes(sol)
    for tr in panel.traces:
        for seg in _break_polyline(tr.points, boxes):
            _emit_seg(a, seg)
        for dx, dy in tr.dots:
            _rosette(a, dx, dy)

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


# --- ornament ---------------------------------------------------------------
# All of it is polyline <path> data: nanosvg keeps M/L paths and their stroke
# attributes, and a wave sampled every 0.35 mm is indistinguishable from a
# curve at any zoom Rack offers.

def _wave(lo, hi, c, amp, period, phase, horizontal, step=0.35):
    """Points of a sine along [lo, hi] at cross-axis position c."""
    n = max(2, int((hi - lo) / step))
    pts = []
    for i in range(n + 1):
        t = lo + (hi - lo) * i / n
        d = amp * math.sin(2 * math.pi * (t - lo) / period + phase)
        pts.append((t, c + d) if horizontal else (c + d, t))
    return pts


def _path(a, pts, ink, width, opacity=1.0, close=False):
    d = "M %.3f %.3f " % pts[0] + " ".join("L %.3f %.3f" % p for p in pts[1:])
    if close:
        d += " Z"
    op = "" if opacity >= 0.999 else ' stroke-opacity="%.2f"' % opacity
    a('  <path d="%s" fill="none" stroke="%s" stroke-width="%.2f" '
      'stroke-linecap="round" stroke-linejoin="round"%s/>' % (d, ink, width, op))


def _ribbon(a, lo, hi, c, horizontal, amp, period, ink, opacity=1.0):
    """Two strands a half-cycle apart: the braid every banknote border is made of."""
    if hi - lo < period:
        return
    # Snap the run to whole periods so both strands meet at the ends.
    cycles = max(1, int(round((hi - lo) / period)))
    period = (hi - lo) / cycles
    for phase in (0.0, math.pi):
        _path(a, _wave(lo, hi, c, amp, period, phase, horizontal), ink, RIBBON_W, opacity)
    for t in (lo, hi):
        x, y = (t, c) if horizontal else (c, t)
        a('  <circle cx="%.4f" cy="%.4f" r="0.36" fill="%s"%s/>'
          % (x, y, ink, "" if opacity >= 0.999 else ' fill-opacity="%.2f"' % opacity))


def _scroll(a, cx, cy, sx, sy, size):
    """A scroll curled into a block corner: an Archimedean spiral whose outer
    turn is tangent to the frame. (cx, cy) is the corner; (sx, sy) the
    direction into the block."""
    turns = 1.85
    n = 40
    pts = []
    r_out = size * 0.5
    ox = cx + sx * (r_out + 0.45)
    oy = cy + sy * (r_out + 0.45)
    for i in range(n + 1):
        t = i / n
        th = 2 * math.pi * turns * t
        r = r_out * (1.0 - 0.82 * t)
        # start on the frame side and wind inward
        x = ox - sx * r * math.cos(th)
        y = oy - sy * r * math.sin(th)
        pts.append((x, y))
    _path(a, pts, P.RULE, RIBBON_W)
    a('  <circle cx="%.4f" cy="%.4f" r="0.22" fill="%s"/>' % (pts[-1][0], pts[-1][1], P.RULE))


KNOB_SWEEP = 270.0      # Rack's knob travel, -135 deg to +135 deg from vertical


def _detents(a, cx, cy, r, n):
    """The subdividers round a stepped knob: an arc over the sweep, a tick at
    every position it stops at, and a bead at each end of the arc. Drawn as a
    sampled polyline like every other ornament, because nanosvg keeps those and
    discards nearly everything else."""
    a0 = math.radians(90.0 + KNOB_SWEEP / 2)      # screen angles, y down
    a1 = math.radians(90.0 - KNOB_SWEEP / 2)
    pts = []
    steps = 48
    for i in range(steps + 1):
        th = a0 + (a1 - a0) * i / steps
        pts.append((cx + r * math.cos(th), cy - r * math.sin(th)))
    _path(a, pts, P.RULE, RIBBON_W * 1.2)
    for i in range(max(n, 2)):
        th = a0 + (a1 - a0) * i / (max(n, 2) - 1)
        c, s_ = math.cos(th), math.sin(th)
        # a longer tick at the two ends, so the extent of the travel reads first
        long = 0.80 if i in (0, max(n, 2) - 1) else 0.55
        _path(a, [(cx + (r - long) * c, cy - (r - long) * s_),
                  (cx + r * c, cy - r * s_)], P.RULE, 0.24)


def _rosette(a, x, y):
    a('  <circle cx="%.4f" cy="%.4f" r="0.75" fill="none" stroke="%s" '
      'stroke-width="0.18"/>' % (x, y, P.RULE))
    a('  <circle cx="%.4f" cy="%.4f" r="0.36" fill="%s"/>' % (x, y, P.RULE))


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
    """One run of wire, engraved as a wave along its own centreline. Short runs
    stay straight: a wave that cannot complete a cycle reads as a kink."""
    x0, y0, x1, y1 = seg
    vertical = abs(x1 - x0) < 1e-6
    lo, hi = (y0, y1) if vertical else (x0, x1)
    if hi - lo < TRACE_PERIOD:
        _path(a, [(x0, y0), (x1, y1)], P.RULE, TRACE_W)
        return
    cycles = max(1, int(round((hi - lo) / TRACE_PERIOD)))
    period = (hi - lo) / cycles
    pts = _wave(lo, hi, x0 if vertical else y0, TRACE_AMP, period, 0.0, not vertical)
    _path(a, pts, P.RULE, TRACE_W)


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

    The stock port's chrome collar is the loudest off-palette object on the
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
        '  <circle cx="%.4f" cy="%.4f" r="7.70" fill="%s"/>' % (c, c, P.GLASS),
        '  <circle cx="%.4f" cy="%.4f" r="4.40" fill="#000000"/>' % (c, c),
        '</svg>', ""])

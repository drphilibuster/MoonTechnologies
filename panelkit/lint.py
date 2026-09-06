"""Checks the solved panel before anything is written.

These are the mistakes that used to be caught only by rebuilding, restarting
Rack and squinting: a label running off the edge, a graphic under a screw, two
labels touching, a block overrunning the footer, two wells overlapping, a well
straddling a block's frame, a footer label running into the foot ribbon. Every
one of them was present in at least one panel when this check was written.
"""

from . import spec as S
from .layout import (cap_h, desc_h, label_box, HEADER_H, FOOT_Y, WELL_RING,
                     TEXT_CLEAR, TEXT_TEXT, RING_PAD, H_SCALE)

EDGE = 1.0              # minimum clearance from the panel edge
GRID_MM = S.GRID_W_PX * S.MM_PER_PX             # 5.08
SCREW_TOP = 0.0
SCREW_BOT = (S.GRID_H_PX - S.GRID_W_PX) * S.MM_PER_PX   # 123.6133
BLOCK_INSET = 3.0       # mirrors render.BLOCK_INSET; a well must sit inside the frame
#: A widget's ink has to stand clear of its block's frame, not merely stop short
#: of crossing it. Letting the ring touch the line is what put Dividend's FREQ
#: knob through the left-hand edge of PAYOUT: the check passed, and the panel
#: still read as broken.
FRAME_CLEAR = 0.7
#: A label's own clearance from the frame. Text hard against an engraved line
#: reads as an error even when nothing actually overlaps.
TEXT_FRAME_CLEAR = 0.5


def screw_rects(panel):
    xs = [(GRID_MM, 2 * GRID_MM), (panel.w - 2 * GRID_MM, panel.w - GRID_MM)]
    ys = [(SCREW_TOP, SCREW_TOP + GRID_MM), (SCREW_BOT, panel.h)]
    return [(x0, y0, x1, y1) for x0, x1 in xs for y0, y1 in ys]


def _overlap(a, b, slack=0.0):
    return (a[0] < b[2] - slack and b[0] < a[2] - slack
            and a[1] < b[3] - slack and b[1] < a[3] - slack)


def _gap(a, b):
    """Millimetres between two boxes along whichever axis they are separated on,
    or None when neither axis separates them (a corner-to-corner near miss, which
    is not what any of these rules is about). Negative means they overlap."""
    dx = max(b[0] - a[2], a[0] - b[2])
    dy = max(b[1] - a[3], a[1] - b[3])
    if dx >= 0 and dy >= 0:
        return None
    if dx < 0 and dy < 0:
        return max(dx, dy)
    return dx if dx >= 0 else dy


def _masthead_pair(a, b):
    """The masthead's three runs sit on one dark band by design -- the title
    across the top, the mark and the brand beneath it, the form number opposite.
    They are set as a block and spaced as one, so the row rule does not apply."""
    return max(a["y"], b["y"]) < HEADER_H + 0.5


def _ring_box(r):
    x, y, rad = r
    return (x - rad, y - rad, x + rad, y + rad)


def _ring_owner(sol, ring):
    for name, x, y, _k in sol.widgets:
        if abs(x - ring[0]) < 1e-6 and abs(y - ring[1]) < 1e-6:
            return name
    return "?"


def _well_box(w):
    x, y, hw, hh, name = w
    return (x - hw - WELL_RING, y - hh - WELL_RING, x + hw + WELL_RING, y + hh + WELL_RING)


def check(panel, sol, labels):
    """`labels` is the full table -- masthead included. Returns a list of strings."""
    bad = []
    bad.extend(sol.overflow)
    screws = screw_rects(panel)
    wells = [(_well_box(w), w[4]) for w in sol.wells]

    for l in labels:
        if not l["text"]:
            continue
        b = label_box(l)
        who = '"%s"' % l["text"]
        if b[0] < EDGE or b[2] > panel.w - EDGE:
            bad.append("%s runs off the side (x %.2f..%.2f, panel is %.2f wide)"
                       % (who, b[0], b[2], panel.w))
        if b[1] < 0.4 or b[3] > panel.h - 0.4:
            bad.append("%s runs off the top or bottom (y %.2f..%.2f)" % (who, b[1], b[3]))
        if b[3] > FOOT_Y:
            bad.append("%s runs into the foot ribbon (bottom %.2f, ribbon from %.2f); "
                       "label it above, or lift the row" % (who, b[3], FOOT_Y))
        for s in screws:
            if _overlap(b, s, 0.05):
                bad.append("%s collides with a corner screw" % who)
                break
        for wb, name in wells:
            gap = _gap(b, wb)
            if gap is None:
                continue
            if gap < -0.05:
                bad.append("%s overlaps the well of '%s'" % (who, name))
            elif gap < TEXT_CLEAR - 0.05:
                bad.append("%s sits %.2f mm off the well of '%s'; a label wants "
                           "%.2f mm of ground under it to read as a caption "
                           "rather than as part of the control"
                           % (who, gap, name, TEXT_CLEAR))

    real = [l for l in labels if l["text"]]
    for i, l in enumerate(real):
        for m in real[i + 1:]:
            gap = _gap(label_box(l), label_box(m))
            if gap is None:
                continue
            if gap < -0.05:
                bad.append('"%s" and "%s" overlap' % (l["text"], m["text"]))
            elif gap < TEXT_TEXT - 0.05 and not _masthead_pair(l, m):
                bad.append('"%s" and "%s" are %.2f mm apart; two runs closer '
                           "than %.2f mm read as one" % (l["text"], m["text"],
                                                         gap, TEXT_TEXT))

    # widgets against the edges, the screws, the foot ribbon and each other.
    # Rings and detent rings are ink like anything else and are checked with the
    # wells they stand round, which is the check that used to be missing.
    frame_boxes = list(wells)
    for r in sol.rings:
        frame_boxes.append((_ring_box(r), "the ring round '%s'" % _ring_owner(sol, r)))
    for (box, name) in frame_boxes:
        if box[0] < EDGE or box[2] > panel.w - EDGE:
            bad.append("widget '%s' runs off the side" % name)
        if box[3] > panel.h:
            bad.append("widget '%s' runs off the bottom" % name)
        for s in screws:
            if _overlap(box, s, 0.05):
                bad.append("widget '%s' collides with a corner screw" % name)
                break
        # inside a block, the well has to clear the frame; on the footer band it
        # only has to clear the edge and the ribbon
        y = (box[1] + box[3]) / 2
        on_band = sol.band_footer is not None and y > sol.band_footer
        if not on_band and sol.blocks:
            in_block = any(b0 - 0.2 <= y <= b1 + 0.2 for b0, b1 in sol.blocks)
            if in_block and (box[0] < BLOCK_INSET + FRAME_CLEAR
                             or box[2] > panel.w - BLOCK_INSET - FRAME_CLEAR):
                bad.append("widget '%s' is too close to its block's frame "
                           "(x %.2f..%.2f; keep ink inside %.2f..%.2f)"
                           % (name, box[0], box[2], BLOCK_INSET + FRAME_CLEAR,
                              panel.w - BLOCK_INSET - FRAME_CLEAR))
        if box[3] > FOOT_Y + 0.3:
            bad.append("widget '%s' runs into the foot ribbon" % name)
    for i, (a, an) in enumerate(wells):
        for b, bn in wells[i + 1:]:
            if _overlap(a, b, 0.3):
                bad.append("widgets '%s' and '%s' overlap (their wells touch)" % (an, bn))

    # a label pressed against its block's frame
    for l in labels:
        if not l["text"]:
            continue
        b = label_box(l)
        y = (b[1] + b[3]) / 2
        on_band = sol.band_footer is not None and y > sol.band_footer
        if on_band or not sol.blocks:
            continue
        if not any(b0 - 0.2 <= y <= b1 + 0.2 for b0, b1 in sol.blocks):
            continue
        if (b[0] < BLOCK_INSET + TEXT_FRAME_CLEAR
                or b[2] > panel.w - BLOCK_INSET - TEXT_FRAME_CLEAR):
            bad.append('"%s" runs into its block\'s frame (x %.2f..%.2f)'
                       % (l["text"], b[0], b[2]))

    # a row that mixes solved and pinned columns cannot be solved: the solver
    # would have to honour one and overwrite the other
    for rows in [sec.rows for sec in panel.sections] + ([panel.footer] if panel.footer
                                                        else []):
        items = [it for row in rows for it in row.items]
        pinned = [it for it in items if it.x is not None]
        if items and pinned and len(pinned) != len(items):
            bad.append("a section mixes controls with a pinned x and controls "
                       "without one; give the whole section its x positions, or "
                       "give it none and let the solver place the columns")

    seen_names = {}
    for name, x, y, kind in sol.widgets:
        if name in seen_names:
            bad.append("two widgets are both called '%s'; names have to be unique, "
                       "because each one becomes a constant in Panel.hpp" % name)
        seen_names[name] = kind

    if len(sol.rings) > 1:
        bad.append("%d controls are marked primary; the ring only means "
                   "something if exactly one control wears it" % len(sol.rings))

    for y0, y1 in sol.blocks:
        if y0 < HEADER_H + 0.5:
            bad.append("a section block runs into the masthead (top %.2f)" % y0)
        if sol.band_footer is not None and y1 > sol.band_footer + 0.05:
            bad.append("a section block runs into the footer band "
                       "(bottom %.2f, band starts %.2f)" % (y1, sol.band_footer))

    # de-duplicate but keep order: the same overlap gets reported once
    seen, out = set(), []
    for b in bad:
        if b not in seen:
            seen.add(b)
            out.append(b)
    return out


# --- the C++ side of the same rule ------------------------------------------
# Everything above keeps the *panel* from drifting. This keeps the widgets that
# draw on it from drifting, which is the failure that actually happened: three
# plugins each grew their own loadFont / nvgFontFaceId / nvgFontSize /
# nvgTextAlign / nvgTextLetterSpacing sequence, and they diverged -- including a
# letter-spacing leak that one of them had documented rather than fixed.
#
# panelkit emits that whole vocabulary into PanelTheme.hpp (TextStyle, text(),
# textWidth(), FittedText, segValue). A plugin calling the raw nanovg text API
# is therefore re-implementing something it already has, so say so.

RAW_TEXT_CALLS = (
    "nvgFontFaceId",
    "nvgFontSize",
    "nvgTextLetterSpacing",
    "nvgTextBounds",
    "nvgText(",
    "loadFont",
)


def check_sources(root):
    """Warns about raw nanovg text calls in a plugin's src/, outside the
    generated header. Advisory: it never blocks a build, because a panel can
    legitimately need something the kit does not cover yet -- but when that
    happens the fix is to add it to the kit, not to open-code it here."""
    import os

    out = []
    src = os.path.join(root, "src")
    if not os.path.isdir(src):
        return out
    for dirpath, _, names in os.walk(src):
        for n in sorted(names):
            # Both generated headers are exempt: they are the kit's own output,
            # and PanelTheme.hpp is where the nanovg calls are supposed to live.
            if not n.endswith((".cpp", ".hpp")) or n in ("PanelTheme.hpp", "Panel.hpp"):
                continue
            path = os.path.join(dirpath, n)
            rel = os.path.relpath(path, root)
            try:
                lines = open(path, encoding="utf-8").read().splitlines()
            except OSError:
                continue
            for i, line in enumerate(lines, 1):
                code = line.split("//", 1)[0]
                for call in RAW_TEXT_CALLS:
                    if call in code:
                        out.append("%s:%d draws text through %s directly; "
                                   "use panel::text / panel::textWidth / "
                                   "panel::FittedText from src/PanelTheme.hpp"
                                   % (rel, i, call.rstrip("(")))
                        break
    return out

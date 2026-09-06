"""The layout solver: turns a Panel spec into absolute millimetres.

Everything vertical is derived. You give the solver rows; it gives back a y for
every widget, a baseline for every label, and the extent of every felt block --
so a block always hugs its contents, and a label can never be closer to its
widget on one panel than on another.

The metric scale is the only thing a panel may vary, via Panel.density. Same
rules, two sizes: "regular" for panels with room, "compact" for panels at
capacity. Nothing else about the language changes between them.
"""

from . import spec as S

# --- the metric scale -------------------------------------------------------
# Every gap on every panel comes from one of these numbers.
SCALE = {
    "regular": dict(
        CAP_BASE=3.6,      # caption baseline below the block's top edge
        CAP_CLEAR=6.3,     # block top edge to the first widget's bounding box
        BELOW_GAP=3.6,     # widget bottom edge to a label baseline under it
        ABOVE_GAP=1.6,     # a label's descender to the widget top edge above it
        ROW_CLEAR=2.0,     # one row's lowest ink to the next row's bounding box
        BOT_CLEAR=4.9,     # lowest ink to the block's bottom edge
        BLOCK_GAP=2.0,     # between felt blocks
        BAND_PAD=1.6,      # footer band top above its highest label
    ),
    # Tuned against Retroactive, the densest panel in the family: seven rows of
    # controls and a read-out at 12 HP. If a panel does not fit at this scale it
    # has too many rows, and the answer is to lose a row rather than to invent a
    # third scale.
    "compact": dict(
        CAP_BASE=2.9,
        CAP_CLEAR=4.4,
        BELOW_GAP=2.2,
        ABOVE_GAP=1.2,
        ROW_CLEAR=1.1,
        BOT_CLEAR=2.6,
        BLOCK_GAP=1.6,
        BAND_PAD=1.3,
    ),
}

# Nunito Bold, measured: the fraction of the em above and below the baseline that
# actually carries ink. Used for clearance, so it has to be the real thing rather
# than the nominal 1.0 em.
CAP_FRAC = 0.72
DESC_FRAC = 0.25

# --- fixed rows -------------------------------------------------------------
#: How far the primary action's lime ring stands off the widget it circles.
RING_PAD = 2.2

HEADER_H = 9.0          # the masthead band
GLASS_Y = 10.2          # top of the read-out well, when a panel has one
GLASS_GAP = 1.2         # read-out well to the first block


def cap_h(size_px):
    """Height of a capital above the baseline, in mm, for a Rack-px font size."""
    return size_px * S.MM_PER_PX * CAP_FRAC


def desc_h(size_px):
    return size_px * S.MM_PER_PX * DESC_FRAC


def text_w(text, size_px, tracking=0.0):
    """Nunito Bold's advance width, close enough for clearance work."""
    return len(text) * (0.60 * size_px + tracking) * S.MM_PER_PX


def label_box(l):
    """(x0, y0, x1, y1) of a label's ink, in mm."""
    w = text_w(l["text"], l["size"], l["tracking"])
    # A mirrored label is drawn through a scale(-1, 1), which swaps which end of
    # the text its anchor pins. Measure it the way it lands, not the way it reads.
    align = l["align"]
    if l.get("mirror"):
        align = {"left": "left", "right": "right", "center": "center"}[align]
    if align == "center":
        x0 = l["x"] - w / 2
    elif align == "right":
        x0 = l["x"] - w
    else:
        x0 = l["x"]
    return (x0, l["y"] - cap_h(l["size"]), x0 + w, l["y"] + desc_h(l["size"]))


class Solved:
    """The resolved panel: flat lists the renderers and emitters consume."""

    def __init__(self, panel):
        self.panel = panel
        self.widgets = []   # (name, x, y, kind)
        self.labels = []    # dict(x, y, text, size, ink, align, tracking)
        self.wells = []     # (x, y, half-width, half-height) recessed seats
        self.rings = []     # (x, y, r) lime rings round the primary action
        self.blocks = []    # (y0, y1)
        self.rules = []     # (y,) divider rules inside blocks
        self.glass = None   # (y0, h)
        self.band_footer = None
        self.overflow = []  # complaints for the linter

    # -- convenience views ---------------------------------------------------
    def by_kind(self, *kinds):
        return [w for w in self.widgets if w[3] in kinds]


def solve(panel):
    m = SCALE[panel.density]
    out = Solved(panel)

    # masthead
    out.labels.append(dict(x=panel.w / 2 if not panel.subtitle else 0,
                           y=0, text="", size=0, ink="PAPER",
                           align="center", tracking=0))
    out.labels.pop()   # placeholder; the masthead is emitted by render, not here

    # read-out well
    if panel.glass:
        panel.glass.y = GLASS_Y
        out.glass = (GLASS_Y, panel.glass.h)
        cursor = GLASS_Y + panel.glass.h + GLASS_GAP
    else:
        cursor = HEADER_H + 1.8

    for sec in panel.sections:
        sec.y0 = cursor
        if sec.caption:
            cap = dict(x=panel.w / 2, y=sec.y0 + m["CAP_BASE"], text=sec.caption,
                       size=6.0, ink="SAGE", align="center", tracking=0.6,
                       ground="light")
            out.labels.append(cap)
            if sec.caption_light:
                _lit_label(out, cap, sec.caption_light)
            inner = sec.y0 + m["CAP_CLEAR"]
        else:
            inner = sec.y0 + 2.2

        lowest = inner
        for i, row in enumerate(sec.rows):
            lowest = _place_row(out, panel, m, row, inner)
            if i in sec.divide_after:
                out.rules.append(lowest + m["ROW_CLEAR"] / 2)
                inner = lowest + m["ROW_CLEAR"] + 0.6
            else:
                inner = lowest + m["ROW_CLEAR"]

        sec.y1 = lowest + m["BOT_CLEAR"]
        out.blocks.append((sec.y0, sec.y1))
        cursor = sec.y1 + m["BLOCK_GAP"]

    # The footer band holds rows pinned by an outside constraint -- the audio
    # jacks sit as low as the bottom screws allow. The band's top edge is derived
    # from the highest ink it contains, so it never has to be typed by hand.
    if panel.footer:
        highest = panel.h
        for row in panel.footer:
            top = _place_row(out, panel, m, row, None, pinned=True)
            highest = min(highest, top)
        # The band wants to sit BAND_PAD above its own highest ink, but if that
        # would crowd the last block it drops as low as it can while still
        # containing that ink -- so the gap between block and band stays visible
        # on a full panel instead of closing to a hairline.
        last = cursor - m["BLOCK_GAP"] if panel.sections else HEADER_H
        ceiling = highest - 0.7
        out.band_footer = min(ceiling, max(highest - m["BAND_PAD"],
                                           last + m["BLOCK_GAP"] * 0.7))
        if out.band_footer < last + 0.6:
            out.overflow.append(
                "the panel is %.2f mm over capacity: the footer band cannot start "
                "below %.2f without cutting its own labels, but the last block "
                "runs to %.2f" % (last + 0.6 - out.band_footer, ceiling, last))
    else:
        out.band_footer = panel.band_footer

    out.blocks.extend(panel.extra_blocks)

    # hand-placed lights (indicators that belong to a caption or a trace, not a row)
    for name, x, y in panel.lights:
        out.widgets.append((name, x, y, "light"))
        r = S.RADIUS["light"] + 1.0
        out.wells.append((x, y, r, r))

    # Traces may be declared as a function of the solved layout, because a wire
    # between two controls has to start where the solver actually put them. The
    # callable is handed a name -> (x, y) lookup and the metric scale.
    pos = {n: (x, y) for n, x, y, _ in out.widgets}
    resolved = []
    for t in panel.traces:
        resolved.extend(t(pos, m) if callable(t) else [t])
    panel.traces = resolved

    for fl in panel.extra_labels:
        out.labels.append(dict(x=fl.x, y=fl.y, text=fl.text, size=fl.size,
                               ink=fl.ink, align=fl.align, tracking=fl.tracking,
                               ground=fl.ground or ground_at(out, fl.y)))
    return out


def ground_at(sol, y):
    """Which ground a label at baseline y sits on: the pale face ("light") or a
    dark band ("dark"). The emitter resolves the label's ink role against this,
    so PAPER means "primary" everywhere and lands as dark ink on the face and
    pale ink on the bands."""
    if y < HEADER_H + 0.5:
        return "dark"
    if sol.band_footer is not None and y > sol.band_footer:
        return "dark"
    return "light"


def _ink_r(it):
    """A widget's vertical half-extent including anything drawn around it."""
    return it.r + (RING_PAD if it.primary else 0.0)


def _place_row(out, panel, m, row, inner, pinned=False):
    """Place one row. Returns the lowest ink it puts on the panel (or, for a
    pinned footer row, the highest -- the band has to be drawn above it)."""
    # The primary action's lime ring stands proud of the widget it circles, so
    # the row's own half-height is the larger of the two -- otherwise the ring
    # pokes up into the section caption, which is what it used to do.
    r = max(_ink_r(it) for it in row.items)
    side = row.label_side or row.items[0].label_side

    if pinned or row.y is not None:
        y = row.y
    elif side == "above" and not row.silent:
        size = row.shared_size if row.shared else max(it.size for it in row.items)
        base = inner + cap_h(size)
        y = base + desc_h(size) + m["ABOVE_GAP"] + r
    else:
        y = inner + r

    for it in row.items:
        out.widgets.append((it.name, it.x, y, it.kind))
        if it.well:
            hw, hh = it.extent
            pad = _seat(it.kind)
            out.wells.append((it.x, y, hw + pad, hh + pad))
        if it.primary:
            out.rings.append((it.x, y, it.r + RING_PAD))

    # The primary action wears a lime ring, and the ring is ink like any other:
    # it has to clear the row above and below and it has to clear its own label.
    # At regular density BELOW_GAP alone happened to be enough and this never
    # showed; at compact it is not, and the label lands on the ring.
    lowest = y + r
    highest = y - r
    if not row.silent:
        if row.shared:
            texts = [(panel.w / 2, row.shared, row.shared_size, row.shared_ink, side, r)]
        else:
            # Each label takes its side from its own widget, so an indicator can
            # sit above the row while the knobs beside it are labelled below.
            # One baseline per side for the whole row, taken from the tallest
            # widget on it. Labels sitting at their own widget's radius made a
            # row of mixed hardware -- a switch beside a trimpot -- read as
            # though the type had slipped.
            drop = max([_ink_r(it) for it in row.items
                        if it.label and (row.label_side or it.label_side) != "above"]
                       or [0.0])
            lift = max([_ink_r(it) for it in row.items
                        if it.label and (row.label_side or it.label_side) == "above"]
                       or [0.0])
            texts = [(it.x, it.label, it.size, it.ink,
                      row.label_side or it.label_side,
                      lift if (row.label_side or it.label_side) == "above" else drop)
                     for it in row.items if it.label]
        for x, text, size, ink, this_side, this_r in texts:
            if this_side == "above":
                base = y - this_r - m["ABOVE_GAP"] - desc_h(size)
                highest = min(highest, base - cap_h(size))
            else:
                base = y + this_r + m["BELOW_GAP"]
                lowest = max(lowest, base + desc_h(size))
            lab = dict(x=x, y=base, text=text, size=size, ink=ink,
                       align="center", tracking=0.0,
                       ground="dark" if pinned else "light")
            out.labels.append(lab)
            for it in row.items:
                if it.light and it.x == x:
                    _lit_label(out, lab, it.light)
    return highest if pinned else lowest


def _lit_label(out, lab, name):
    """The lit-label idiom: a small light sitting just past a label's last letter,
    on the label's own optical centre. Saves every panel a hand-placed coordinate."""
    b = label_box(lab)
    r = S.RADIUS["light_small"]
    x = b[2] + 0.9 + r
    y = lab["y"] - cap_h(lab["size"]) / 2
    out.widgets.append((name, x, y, "light_small"))
    out.wells.append((x, y, r + 0.9, r + 0.9))


def _seat(kind):
    """How far the recessed well extends past the widget's own art."""
    if kind == "jack":
        return 0.69          # matches the stock port's shoulder
    if kind == "light":
        return 1.00
    return 1.30

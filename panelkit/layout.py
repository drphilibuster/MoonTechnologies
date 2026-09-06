"""The layout solver: turns a Panel spec into absolute millimetres.

Everything vertical is derived. You give the solver rows; it gives back a y for
every widget, a baseline for every label, and the extent of every felt block --
so a block always hugs its contents, and a label can never be closer to its
widget on one panel than on another.

Every clearance is measured from a widget's *well* -- the ringed seat drawn
behind it -- not from its art, because the well is the thing the eye sees the
label against. A label that clears the knob but lands on the ring reads as a
collision, and did, until this was made the rule.

The metric scale is the only thing a panel may vary, via Panel.density. Same
rules, two sizes: "regular" for panels with room, "compact" for panels at
capacity. Nothing else about the language changes between them.

A panel whose rows do not fill the face is *justified*: the slack between the
last block and the footer band is shared out among the row gaps and block gaps,
up to a cap, so a sparse panel does not end in a dead pale strip above its jacks.
"""

from . import spec as S

# --- the metric scale -------------------------------------------------------
# Every gap on every panel comes from one of these numbers. All are measured to
# and from wells (or the primary ring, whichever is larger), never from art.
SCALE = {
    "regular": dict(
        CAP_BASE=3.6,      # caption baseline below the block's top edge
        CAP_CLEAR=5.2,     # block top edge to the first row's well top
        BELOW_GAP=0.8,     # well bottom edge to the cap top of a label under it
        ABOVE_GAP=0.8,     # a label's descender to the well top edge above it
        ROW_CLEAR=1.3,     # one row's lowest ink to the next row's well top
        BOT_CLEAR=3.0,     # lowest ink to the block's bottom edge
        BLOCK_GAP=2.0,     # between felt blocks
        BAND_PAD=1.6,      # footer band top above its highest label
        JUSTIFY_MAX=3.6,   # most a single gap may grow to fill the face
    ),
    # Tuned against Retroactive, the densest panel in the family: seven rows of
    # controls and a read-out at 12 HP. If a panel does not fit at this scale it
    # has too many rows, and the answer is to lose a row rather than to invent a
    # third scale.
    "compact": dict(
        CAP_BASE=2.9,
        CAP_CLEAR=3.4,
        BELOW_GAP=0.35,
        ABOVE_GAP=0.5,
        ROW_CLEAR=0.6,
        BOT_CLEAR=1.8,
        BLOCK_GAP=1.5,
        BAND_PAD=1.3,
        JUSTIFY_MAX=2.4,
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
#: The stroke drawn round every well, which is ink like any other.
WELL_RING = 0.2

HEADER_H = 9.0          # the masthead band
GLASS_Y = 10.2          # top of the read-out well, when a panel has one
GLASS_GAP = 1.2         # read-out well to the first block
#: Below this the foot ribbon runs between the bottom screws; nothing may enter it.
FOOT_Y = 124.3


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


def seat(kind):
    """How far the recessed well extends past the widget's own art."""
    if kind == "jack":
        return 0.69          # matches the stock port's shoulder
    if kind in ("light", "light_small"):
        return 0.80
    return 1.00


def well_extent(kind):
    """(half-width, half-height) of a widget's well including its ring."""
    hw, hh = S.EXTENT.get(kind, (S.RADIUS[kind],) * 2)
    p = seat(kind) + WELL_RING
    return hw + p, hh + p


class Solved:
    """The resolved panel: flat lists the renderers and emitters consume."""

    def __init__(self, panel):
        self.panel = panel
        self.widgets = []   # (name, x, y, kind)
        self.labels = []    # dict(x, y, text, size, ink, align, tracking, ground)
        self.wells = []     # (x, y, half-width, half-height, name) recessed seats
        self.rings = []     # (x, y, r) rings round the primary action
        self.blocks = []    # (y0, y1)
        self.rules = []     # (y,) divider rules inside blocks
        self.glass = None   # (y0, h)
        self.band_footer = None
        self.overflow = []  # complaints for the linter
        self.justified = 0.0   # how much every gap grew to fill the face

    # -- convenience views ---------------------------------------------------
    def by_kind(self, *kinds):
        return [w for w in self.widgets if w[3] in kinds]


def solve(panel):
    """Solve once at the panel's metric; if the rows leave slack above the
    footer band, solve again with the gaps grown to share it out."""
    base = SCALE[panel.density]
    traces = list(panel.traces)
    out, slack, gaps = _solve(panel, base)
    if slack > 0.4 and gaps > 0:
        extra = min(slack / gaps, base["JUSTIFY_MAX"])
        if extra > 0.15:
            m = dict(base)
            m["ROW_CLEAR"] += extra
            m["BLOCK_GAP"] += extra
            panel.traces = traces
            out, _, _ = _solve(panel, m)
            out.justified = extra
    return out


def _solve(panel, m):
    out = Solved(panel)
    rows_gaps = 0

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
            inner = sec.y0 + m["CAP_CLEAR"]
            if sec.caption_light:
                # The light's well is ink under the caption: the first row starts
                # below it, not below the caption's baseline.
                bottom = _lit_label(out, cap, sec.caption_light)
                inner = max(inner, bottom + 0.3)
        else:
            inner = sec.y0 + 2.2

        lowest = inner
        for i, row in enumerate(sec.rows):
            if i == 0 and sec.caption and not row.silent:
                # A first row labelled above would put its words right under
                # the caption's descenders; hold it off by a line's worth.
                side = row.label_side or row.items[0].label_side
                if side == "above" or any(
                        (row.label_side or it.label_side) == "above" and it.label
                        for it in row.items):
                    inner = max(inner, sec.y0 + m["CAP_BASE"] + desc_h(6.0) + 1.1)
            lowest = _place_row(out, panel, m, row, inner)
            if i in sec.divide_after:
                out.rules.append(lowest + max(m["ROW_CLEAR"] / 2, 1.0))
                inner = lowest + max(m["ROW_CLEAR"], 2.0) + 0.6
            else:
                inner = lowest + m["ROW_CLEAR"]
            if i + 1 < len(sec.rows):
                rows_gaps += 1

        sec.y1 = lowest + m["BOT_CLEAR"]
        out.blocks.append((sec.y0, sec.y1))
        cursor = sec.y1 + m["BLOCK_GAP"]

    # The footer band holds rows pinned by an outside constraint -- the audio
    # jacks sit as low as the bottom screws allow. The band's top edge is derived
    # from the highest ink it contains, so it never has to be typed by hand.
    slack = 0.0
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
        ideal = highest - m["BAND_PAD"]
        out.band_footer = min(ceiling, max(ideal, last + m["BLOCK_GAP"] * 0.7))
        if out.band_footer < last + 0.6:
            out.overflow.append(
                "the panel is %.2f mm over capacity: the footer band cannot start "
                "below %.2f without cutting its own labels, but the last block "
                "runs to %.2f" % (last + 0.6 - out.band_footer, ceiling, last))
        if panel.sections:
            slack = ideal - (last + m["BLOCK_GAP"])
    else:
        out.band_footer = panel.band_footer

    out.blocks.extend(panel.extra_blocks)

    # hand-placed lights (indicators that belong to a caption or a trace, not a row)
    for name, x, y in panel.lights:
        out.widgets.append((name, x, y, "light"))
        hw, hh = well_extent("light")
        out.wells.append((x, y, hw, hh, name))

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
    # gaps that may grow: between rows inside blocks, between blocks, and the
    # gap from the last block to the band
    gaps = rows_gaps + len(panel.sections)
    return out, slack, gaps


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
    """A widget's vertical half-extent including everything drawn around it:
    its well and ring, or the primary ring when that stands further out."""
    r = well_extent(it.kind)[1] if it.well else it.r
    if it.primary:
        r = max(r, it.r + RING_PAD)
    return r


def _place_row(out, panel, m, row, inner, pinned=False):
    """Place one row. Returns the lowest ink it puts on the panel (or, for a
    pinned footer row, the highest -- the band has to be drawn above it).

    The geometry of one labelled widget, top to bottom, is: [light or cap]
    label [descender or light] gap well. Every distance below is one of those
    terms, so a lit label, a tall well and a ring all get exactly the room
    they need and no more."""
    light_hh = well_extent("light_small")[1]

    def above_parts(it, size):
        """(well-to-baseline, baseline-to-top) for a label above `it`."""
        cap, desc = cap_h(size), desc_h(size)
        lit = it is not None and bool(it.light)
        off = max(m["ABOVE_GAP"] + desc, light_hh - cap / 2 if lit else 0.0)
        head = max(cap, cap / 2 + light_hh if lit else 0.0)
        return off, head

    def below_parts(it, size):
        """(well-to-baseline, baseline-to-bottom) for a label below `it`."""
        cap, desc = cap_h(size), desc_h(size)
        lit = it is not None and bool(it.light)
        off = max(m["BELOW_GAP"] + cap, light_hh + cap / 2 + 0.2 if lit else 0.0)
        foot = max(desc, light_hh - cap / 2 if lit else 0.0)
        return off, foot

    side = row.label_side or row.items[0].label_side
    # what each labelled item wants, by side
    if row.silent:
        texts = []
    elif row.shared:
        texts = [(panel.w / 2, row.shared, row.shared_size, row.shared_ink, side, None)]
    else:
        texts = [(it.x, it.label, it.size, it.ink, row.label_side or it.label_side, it)
                 for it in row.items if it.label]

    # The row's half-height above and below its centre line: the tallest well
    # or ring, plus whatever an above-label (and its light) needs. Labels on a
    # side share one baseline, taken from the item that needs the most room, so
    # mixed hardware on one row reads as one row.
    r = max(_ink_r(it) for it in row.items)
    # A shared label belongs to the whole row, so it clears the row's tallest well.
    lift = max([(_ink_r(it) if it is not None else r) + above_parts(it, sz)[0]
                for _, _, sz, _, sd, it in texts if sd == "above"] or [0.0])
    head = max([above_parts(it, sz)[1] for _, _, sz, _, sd, it in texts if sd == "above"]
               or [0.0])
    drop = max([(_ink_r(it) if it is not None else r) + below_parts(it, sz)[0]
                for _, _, sz, _, sd, it in texts if sd != "above"] or [0.0])
    foot = max([below_parts(it, sz)[1] for _, _, sz, _, sd, it in texts if sd != "above"]
               or [0.0])

    if pinned or row.y is not None:
        y = row.y
    else:
        y = inner + max(r, lift + head)

    for it in row.items:
        out.widgets.append((it.name, it.x, y, it.kind))
        if it.well:
            hw, hh = it.extent
            pad = seat(it.kind)
            out.wells.append((it.x, y, hw + pad, hh + pad, it.name))
        if it.primary:
            out.rings.append((it.x, y, it.r + RING_PAD))

    lowest = y + r
    highest = y - r
    for x, text, size, ink, this_side, it in texts:
        if this_side == "above":
            base = y - lift
            highest = min(highest, base - head)
        else:
            base = y + drop
            lowest = max(lowest, base + foot)
        lab = dict(x=x, y=base, text=text, size=size, ink=ink,
                   align="center", tracking=0.0,
                   ground="dark" if pinned else "light")
        out.labels.append(lab)
        if it is not None and it.light:
            _lit_label(out, lab, it.light, it.light_side)
    return highest if pinned else lowest


def _lit_label(out, lab, name, side="right"):
    """The lit-label idiom: a small light sitting just past a label's last letter
    (or, for a control in the last column, just before its first), on the
    label's own optical centre. Saves every panel a hand-placed coordinate.
    Returns the bottom edge of the light's well."""
    b = label_box(lab)
    r = S.RADIUS["light_small"]
    hw, hh = well_extent("light_small")
    x = (b[0] - 0.6 - hw) if side == "left" else (b[2] + 0.6 + hw)
    y = lab["y"] - cap_h(lab["size"]) / 2
    out.widgets.append((name, x, y, "light_small"))
    out.wells.append((x, y, hw - WELL_RING, hh - WELL_RING, name))
    return y + hh

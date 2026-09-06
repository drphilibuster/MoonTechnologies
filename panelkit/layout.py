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

Horizontally the same principle applies, and for the same reason. A row used to
be given evenly spaced *centres* -- P.cols(n, margin) -- with the margin typed by
hand. That is the wrong quantity to hold constant: a centre says nothing about
how much of the panel a widget actually covers, so a big knob and a switch on one
row got the same cell, and the big knob then hung over the block frame while the
switches floated in dead air. Every panel in the family had some version of it.

So columns are solved from *extents* instead. A column is as wide as the widest
thing that lands in it on any row of its section -- well, primary ring, detent
ring, or the label under it, whichever reaches furthest -- and the outermost
columns are placed by their minimum clearance from the frame, never by a typed
margin. Runs of comparable columns are spaced evenly, as they should be; where a
row changes gear -- three knobs, then two switches -- the two runs are spaced
evenly within themselves and the slack collects in a gutter between them, which
is the one place on a row where empty space reads as deliberate.
"""

from . import spec as S

# --- the metric scale -------------------------------------------------------
# Every gap on every panel comes from one of these numbers. All are measured to
# and from wells (or the primary ring, whichever is larger), never from art.
SCALE = {
    "regular": dict(
        CAP_BASE=3.2,      # caption baseline below the block's top edge
        CAP_CLEAR=4.6,     # block top edge to the first row's well top
        BELOW_GAP=0.8,     # well bottom edge to the cap top of a label under it
        ABOVE_GAP=0.8,     # a label's descender to the well top edge above it
        ROW_CLEAR=1.3,     # one row's lowest ink to the next row's well top
        BOT_CLEAR=2.5,     # lowest ink to the block's bottom edge
        BLOCK_GAP=1.8,     # between felt blocks
        BAND_PAD=1.6,      # footer band top above its highest label
        JUSTIFY_MAX=3.6,   # most a single gap may grow to fill the face
    ),
    # Tuned against Retroactive, the densest panel in the family: seven rows of
    # controls and a read-out at 12 HP. If a panel does not fit at this scale it
    # has too many rows, and the answer is to lose a row rather than to invent a
    # third scale.
    "compact": dict(
        CAP_BASE=2.5,
        CAP_CLEAR=3.4,
        BELOW_GAP=0.7,
        ABOVE_GAP=0.7,
        ROW_CLEAR=0.85,
        BOT_CLEAR=1.2,
        BLOCK_GAP=1.1,
        BAND_PAD=1.3,
        JUSTIFY_MAX=2.4,
    ),
}

#: The horizontal scale. Same two densities, and the same discipline: every
#: number is a clearance between two pieces of ink, never a position.
H_SCALE = {
    "regular": dict(
        EDGE_PAD=2.2,      # frame (or panel edge) to the nearest ink on a row
        ITEM_GAP=2.0,      # between neighbouring cells inside one run
        GROUP_GAP=4.4,     # the gutter where a row changes gear
        PITCH_CAP=2.00,    # most a run's pitch may grow to fill the width
        MARGIN_SHARE=0.55,  # how much of the left-over a side margin takes
    ),
    "compact": dict(
        EDGE_PAD=1.8,
        ITEM_GAP=1.5,
        GROUP_GAP=3.6,
        PITCH_CAP=1.85,
        MARGIN_SHARE=0.55,
    ),
}

#: Two columns belong to one evenly spaced run when they hold the same sort of
#: hardware, or when they are close enough in width that spacing them evenly
#: reads as even. Below this ratio the row has changed gear and wants a gutter.
GROUP_RATIO = 0.72

#: What a widget's class is, for the purpose of the rule above. Knobs of every
#: size are one class -- three knobs of different diameters evenly spaced is the
#: family idiom and looks right; a knob beside a switch does not.
CLASS = {
    "knob_large": "knob", "knob": "knob", "trim": "knob", "slider": "knob",
    "switch": "switch", "switch3": "switch",
    "jack": "jack",
    "button": "button", "bezel": "button",
    "light": "light", "light_small": "light",
}

#: A stepped knob's detent ring is engraved *inside* its well, in the dark seat
#: between the knob's edge and the well's ring. Putting it there rather than
#: outside is what keeps a selector exactly as big as the knob it is -- a ring
#: standing off the art would make every stepped control taller and wider than
#: its neighbours, for no reason the eye would thank it for.
STEP_INSET = 0.25

# Nunito Bold, measured: the fraction of the em above and below the baseline that
# actually carries ink. Used for clearance, so it has to be the real thing rather
# than the nominal 1.0 em.
CAP_FRAC = 0.72
DESC_FRAC = 0.25

#: Clearances that hold at every density, because below them the panel stops
#: being readable rather than merely tight. A label 0.35 mm off a knob's well
#: reads as touching it -- that is one Rack pixel at 1x zoom -- and two labels
#: 0.6 mm apart read as one paragraph rather than as two rows.
TEXT_CLEAR = 0.70       # a label's ink to any well
TEXT_TEXT = 1.55        # a label's ink to the next row's label

# --- fixed rows -------------------------------------------------------------
#: How far the primary action's lime ring stands off the widget it circles.
RING_PAD = 2.2
#: The stroke drawn round every well, which is ink like any other.
WELL_RING = 0.2

HEADER_H = 9.0          # the masthead band
GLASS_Y = 9.8           # top of the read-out well, when a panel has one
GLASS_GAP = 1.0         # read-out well to the first block
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


# --- the horizontal solver --------------------------------------------------
# A column's width is the widest thing that lands in it, and its position falls
# out of the widths of everything to its left plus the minimum clearances. There
# is no typed margin anywhere in here, which is the whole point: a margin typed
# for the widest knob on a row is wrong for the row above it, and that is how
# every one of these panels ended up with a knob over its own frame.

def ink_hw(it):
    """A widget's horizontal half-extent, counting everything drawn around it:
    the well and its ring, the primary seal, a stepped knob's detent ring."""
    hw = well_extent(it.kind)[0] if it.well else it.extent[0]
    if it.primary:
        hw = max(hw, it.r + RING_PAD)
    return hw


def label_ext(it):
    """(left, right) reach of the label under or over a widget, from its centre.

    Asymmetric on purpose: a lit label's light hangs off one end only, and
    treating it as if it stood on both sides is what used to make a column a
    couple of millimetres wider than anything in it actually was.
    """
    if not it.label:
        return 0.0, 0.0
    w = text_w(it.label, it.size) / 2
    l = r = w
    if it.light:
        reach = w + TEXT_CLEAR + 2 * well_extent("light_small")[0]
        if it.light_side == "left":
            l = reach
        else:
            r = reach
    return l, r


def _cells(rowset):
    """Column index -> (left reach, right reach, class), taking the widest of
    everything any row puts in that column."""
    cells = {}
    for row in rowset:
        for i, it in enumerate(row.items):
            c = i if it.col is None else it.col
            hw = ink_hw(it)
            ll, rr = (0.0, 0.0) if row.silent else label_ext(it)
            L, R, cls, top = cells.get(c, (0.0, 0.0, None, -1.0))
            if hw > top:
                cls, top = CLASS[it.kind], hw
            cells[c] = (max(L, hw, ll), max(R, hw, rr), cls, top)
    n = max(cells) + 1 if cells else 0
    return [cells.get(c, (0.0, 0.0, "knob", 0.0))[:3] for c in range(n)]


def _runs(cols, groups=()):
    """Split the columns into evenly spaced runs, breaking where a row changes
    gear. Comparable columns stay together even across classes, because three
    knobs and a jack of nearly the same size still want one even pitch, and an
    indicator light never breaks a run -- it belongs to whatever surrounds it.

    A section may name its runs outright, for a grouping the widths cannot see:
    four gates of three identical jacks are four runs, not one run of twelve."""
    if groups:
        runs, c = [], 0
        for n in groups:
            runs.append(list(range(c, min(c + n, len(cols)))))
            c += n
        if c < len(cols):
            runs.append(list(range(c, len(cols))))
        return [r for r in runs if r]
    runs = []
    for c, (L, R, cls) in enumerate(cols):
        pl, pr, prev_cls = cols[c - 1] if c else (0.0, 0.0, None)
        a, b = max(L, R), max(pl, pr)
        if c and (cls == prev_cls or "light" in (cls, prev_cls)
                  or min(a, b) / max(a, b, 1e-6) >= GROUP_RATIO):
            runs[-1].append(c)
        else:
            runs.append([c])
    return runs


def _run_pitch(cols, run, hm):
    """The tightest even pitch a run can be spaced at: wide enough for its
    widest neighbouring pair, so an even run really is even."""
    if len(run) < 2:
        return 0.0
    return max(cols[a][1] + cols[b][0] for a, b in zip(run, run[1:])) + hm["ITEM_GAP"]


def natural_span(cols, hm, groups=()):
    """How much width the columns need at their tightest -- the number that
    decides how many HP a panel actually is."""
    if not cols:
        return 0.0
    runs = _runs(cols, groups)
    total = 0.0
    for run in runs:
        total += (cols[run[0]][0] + cols[run[-1]][1]
                  + (len(run) - 1) * _run_pitch(cols, run, hm))
    return total + (len(runs) - 1) * hm["GROUP_GAP"] + 2 * hm["EDGE_PAD"]


def place_columns(rowset, x_lo, x_hi, hm, groups=()):
    """Column centres for one set of rows that share a grid. Returns
    (centres, shortfall) -- shortfall is how much width the rows are short of,
    and is zero on a panel that fits."""
    cols = _cells(rowset)
    if not cols:
        return [], 0.0
    runs = _runs(cols, groups)
    pitch = [_run_pitch(cols, r, hm) for r in runs]
    ends = [cols[r[0]][0] + cols[r[-1]][1] for r in runs]

    avail = (x_hi - x_lo) - 2 * hm["EDGE_PAD"]
    need = sum(ends) + sum(p * (len(r) - 1) for p, r in zip(pitch, runs)) \
        + (len(runs) - 1) * hm["GROUP_GAP"]
    if need > avail:
        return None, need - avail

    # 1. let every run breathe, in step, until each hits its own cap. A run that
    #    caps out stops taking slack; the rest keep growing.
    slack = avail - need
    caps = [p * hm["PITCH_CAP"] for p in pitch]
    live = [i for i, r in enumerate(runs) if len(r) > 1]
    while live and slack > 1e-6:
        steps = sum(len(runs[i]) - 1 for i in live)
        want = slack / steps
        room = min(caps[i] - pitch[i] for i in live)
        take = min(want, room)
        for i in live:
            pitch[i] += take
        slack -= take * steps
        if take >= want - 1e-9:
            break
        live = [i for i in live if caps[i] - pitch[i] > 1e-6]

    # 2. whatever is still over goes to the gutters between runs and, at a
    #    discount, to the two side margins -- so a row that changes gear collects
    #    its slack where the change happens instead of at one end.
    units = (len(runs) - 1) + 2 * hm["MARGIN_SHARE"]
    unit = slack / units if units > 1e-6 else 0.0
    gutter = hm["GROUP_GAP"] + unit
    margin = hm["EDGE_PAD"] + hm["MARGIN_SHARE"] * unit

    centres = [0.0] * len(cols)
    x = x_lo + margin
    for run, p in zip(runs, pitch):
        x += cols[run[0]][0]
        for k, c in enumerate(run):
            centres[c] = x + k * p
        x = centres[run[-1]] + cols[run[-1]][1] + gutter
    return centres, 0.0


def _rowsets(panel):
    """The groups of rows that share a column grid: each section, and the footer.
    Rows in one section line their columns up, which is what makes a trimpot sit
    over its own jack without either of them being given an x."""
    sets = []
    for sec in panel.sections:
        grid = [r for r in sec.rows if not r.own_grid]
        if grid:
            sets.append((grid, "block", sec.groups))
        for r in sec.rows:
            if r.own_grid:
                sets.append(([r], "block", ()))
    if panel.footer:
        sets.append((panel.footer, "band", ()))
    return sets


def _bounds(panel, kind, hm):
    from .render import BLOCK_INSET
    if kind == "block":
        return BLOCK_INSET, panel.w - BLOCK_INSET
    return 1.0, panel.w - 1.0


def solve_x(panel):
    """Give every widget that has not been pinned an x. Returns the shortfall in
    millimetres -- how much wider the panel would have to be for every row to fit
    at its tightest spacing. Zero means it fits."""
    hm = H_SCALE[panel.density]
    short = 0.0
    for rows, kind, groups in _rowsets(panel):
        items = [it for row in rows for it in row.items]
        if not items or all(it.x is not None for it in items):
            continue
        x_lo, x_hi = _bounds(panel, kind, hm)
        centres, miss = place_columns(rows, x_lo, x_hi, hm, groups)
        if centres is None:
            short = max(short, miss)
            continue
        for row in rows:
            for i, it in enumerate(row.items):
                it.x = centres[i if it.col is None else it.col]
    return short


def required_hp(panel, floor=4):
    """The narrowest panel these rows fit on, in HP. A spec passes hp="auto" and
    gets this; a spec that pins its HP is checked against it."""
    hm = H_SCALE[panel.density]
    need = 0.0
    from .render import BLOCK_INSET
    for rows, kind, groups in _rowsets(panel):
        cols = _cells(rows)
        pad = 2 * BLOCK_INSET if kind == "block" else 2.0
        need = max(need, natural_span(cols, hm, groups) + pad)
    # the masthead has to hold the title between the two top screws at its
    # smallest legible size, or the panel is too narrow to be named
    from .emit import SCREW_CLEAR, TITLE_MIN, TITLE_TRACK
    need = max(need, 2 * SCREW_CLEAR
               + len(panel.title) * (0.60 * TITLE_MIN + TITLE_TRACK) * S.MM_PER_PX)
    import math
    return max(floor, int(math.ceil(need / S.HP_MM - 1e-6)))


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
        self.steps = []     # (x, y, r, n) detent rings round stepped knobs
        self.justified = 0.0   # how much every gap grew to fill the face

    # -- convenience views ---------------------------------------------------
    def by_kind(self, *kinds):
        return [w for w in self.widgets if w[3] in kinds]


def solve(panel):
    """Solve once at the panel's metric; if the rows leave slack above the
    footer band, solve again with the gaps grown to share it out."""
    if panel.hp in (None, "auto"):
        panel.hp = required_hp(panel)
    panel.shortfall = solve_x(panel)
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
    if panel.shortfall > 0.005:
        out.overflow.append(
            "the rows are %.2f mm wider than the panel: %d HP is not enough for "
            "them at their tightest spacing, %d HP is"
            % (panel.shortfall, panel.hp, required_hp(panel)))

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
            # The caption is text, and the first row's ink has to clear it like
            # any other text -- so this is derived from where the caption's
            # descenders actually land, not typed next to CAP_BASE and left to
            # drift out of step with it.
            inner = sec.y0 + max(m["CAP_CLEAR"],
                                 m["CAP_BASE"] + desc_h(6.0) + TEXT_CLEAR)
            if sec.caption_light:
                # The light's well is ink under the caption: the first row starts
                # below it, not below the caption's baseline.
                bottom = _lit_label(out, cap, sec.caption_light)
                inner = max(inner, bottom + TEXT_CLEAR)
        else:
            inner = sec.y0 + 2.2

        for i, row in enumerate(sec.rows):
            nxt = sec.rows[i + 1] if i + 1 < len(sec.rows) else None
            row._pair_cols = _pair_cols(row, nxt)

        lowest, low_text = inner, False
        for i, row in enumerate(sec.rows):
            if i:
                prev = sec.rows[i - 1]
                if prev.pair:
                    # the shared label sits between the two rows: the same gap
                    # under it as over it, so it cannot be read as belonging to
                    # the row above instead
                    gap = max(m["BELOW_GAP"], TEXT_CLEAR)
                elif low_text and _tops_with_text(row):
                    gap = max(m["ROW_CLEAR"], TEXT_TEXT)
                elif low_text:
                    gap = max(m["ROW_CLEAR"], TEXT_CLEAR)
                else:
                    gap = m["ROW_CLEAR"]
                if i - 1 in sec.divide_after:
                    # the subtotal rule needs a line's worth of ground either
                    # side of it, taken from the scale like every other gap
                    gap = max(gap, m["ROW_CLEAR"] + 1.2)
                    out.rules.append(lowest + gap / 2)
                    inner = lowest + gap + 0.6
                else:
                    inner = lowest + gap
            if i == 0 and sec.caption and not row.silent:
                # A first row labelled above would put its words right under
                # the caption's descenders; hold it off by a line's worth.
                if _tops_with_text(row):
                    inner = max(inner,
                                sec.y0 + m["CAP_BASE"] + desc_h(6.0) + TEXT_TEXT)
            lowest, low_text = _place_row(out, panel, m, row, inner)
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
        for i, row in enumerate(panel.footer):
            nxt = panel.footer[i + 1] if i + 1 < len(panel.footer) else None
            row._pair_cols = _pair_cols(row, nxt)
        for i in range(len(panel.footer) - 1, -1, -1):
            row = panel.footer[i]
            if row.y is None and row._pair_cols:
                # the paired idiom on the band: this row's label sits in the gap
                # it shares with the pinned row below, equidistant from both, so
                # neither y has to be typed
                nxt = panel.footer[i + 1]
                gap = max(m["BELOW_GAP"], TEXT_CLEAR)
                sz = max([it.size for it in row.items if it.label] or [6.2])
                below_top = nxt.y - max(_ink_r(it) for it in nxt.items)
                row.y = (below_top - gap - desc_h(sz) - cap_h(sz) - gap
                         - max(_ink_r(it) for it in row.items))
        for row in panel.footer:
            top, _ = _place_row(out, panel, m, row, None, pinned=True)
            highest = min(highest, top)
        # The band wants to sit BAND_PAD above its own highest ink, but if that
        # would crowd the last block it drops as low as it can while still
        # containing that ink -- so the gap between block and band stays visible
        # on a full panel instead of closing to a hairline.
        last = cursor - m["BLOCK_GAP"] if panel.sections else HEADER_H
        ceiling = highest - TEXT_CLEAR
        ideal = highest - m["BAND_PAD"]
        # The band and the last block must not close up into one dark edge. Half
        # a block gap is the least that still reads as two things, and taking it
        # from the scale keeps it in step with every other gap on the panel.
        clear = m["BLOCK_GAP"] * 0.5
        out.band_footer = min(ceiling, max(ideal, last + m["BLOCK_GAP"] * 0.7))
        if out.band_footer < last + clear:
            out.overflow.append(
                "the panel is %.2f mm over capacity: the footer band cannot start "
                "below %.2f without cutting its own labels, but the last block "
                "runs to %.2f" % (last + clear - out.band_footer, ceiling, last))
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


def _pair_cols(row, nxt):
    """Which of this row's columns own the widget below them. A row may declare
    the whole idiom at once, or a single control may claim it -- which is what a
    row wants where one control has a partner underneath and its neighbour, on
    the same line, simply does not."""
    if nxt is None:
        return set()
    below = {(j if it.col is None else it.col) for j, it in enumerate(nxt.items)}
    if row.pair:
        return below
    return below & {(j if it.col is None else it.col)
                    for j, it in enumerate(row.items) if it.pair}


def _tops_with_text(row):
    """Does this row put a label above its widgets? Then the gap over it is a
    gap between two pieces of text, which needs more room than ink to a well."""
    if row.silent:
        return False
    paired = getattr(row, "_pair_cols", set())
    if row.shared:
        return (row.label_side or row.items[0].label_side) == "above"
    return any((i if it.col is None else it.col) not in paired
               and (row.label_side or it.label_side) == "above" and it.label
               for i, it in enumerate(row.items))


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
        off = max(max(m["ABOVE_GAP"], TEXT_CLEAR) + desc,
                  light_hh - cap / 2 if lit else 0.0)
        head = max(cap, cap / 2 + light_hh if lit else 0.0)
        return off, head

    def below_parts(it, size):
        """(well-to-baseline, baseline-to-bottom) for a label below `it`."""
        cap, desc = cap_h(size), desc_h(size)
        lit = it is not None and bool(it.light)
        off = max(max(m["BELOW_GAP"], TEXT_CLEAR) + cap,
                  light_hh + cap / 2 + 0.2 if lit else 0.0)
        foot = max(desc, light_hh - cap / 2 if lit else 0.0)
        return off, foot

    # A paired control's label sits below it, in the gap it shares with the widget
    # underneath -- that is what makes the pair read as a pair, rather than as a
    # label belonging to whatever happens to be above it. Only the columns that
    # really have a partner below move: a jack sharing the row but owning nothing
    # keeps its label over its head, where a patch cable cannot cover it.
    paired = getattr(row, "_pair_cols", set())

    def _side(it, i):
        if (i if it.col is None else it.col) in paired:
            return "below"
        return row.label_side or it.label_side

    side = _side(row.items[0], 0)
    # what each labelled item wants, by side
    if row.silent:
        texts = []
    elif row.shared:
        texts = [(panel.w / 2, row.shared, row.shared_size, row.shared_ink, side, None)]
    else:
        texts = [(it.x, it.label, it.size, it.ink, _side(it, i), it)
                 for i, it in enumerate(row.items) if it.label]

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
        if it.step_count:
            out.steps.append((it.x, y, it.r + seat(it.kind) - STEP_INSET,
                              it.step_count))

    lowest = y + r
    highest = y - r
    low_text = high_text = False
    for x, text, size, ink, this_side, it in texts:
        if this_side == "above":
            base = y - lift
            if base - head <= highest:
                high_text = True
            highest = min(highest, base - head)
        else:
            base = y + drop
            if base + foot >= lowest:
                low_text = True
            lowest = max(lowest, base + foot)
        lab = dict(x=x, y=base, text=text, size=size, ink=ink,
                   align="center", tracking=0.0,
                   ground="dark" if pinned else "light")
        out.labels.append(lab)
        if it is not None and it.light:
            _lit_label(out, lab, it.light, it.light_side)
    return (highest, high_text) if pinned else (lowest, low_text)


def _lit_label(out, lab, name, side="right"):
    """The lit-label idiom: a small light sitting just past a label's last letter
    (or, for a control in the last column, just before its first), on the
    label's own optical centre. Saves every panel a hand-placed coordinate.
    Returns the bottom edge of the light's well."""
    b = label_box(lab)
    r = S.RADIUS["light_small"]
    hw, hh = well_extent("light_small")
    x = (b[0] - TEXT_CLEAR - hw) if side == "left" else (b[2] + TEXT_CLEAR + hw)
    y = lab["y"] - cap_h(lab["size"]) / 2
    out.widgets.append((name, x, y, "light_small"))
    out.wells.append((x, y, hw - WELL_RING, hh - WELL_RING, name))
    return y + hh

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
        PITCH_CAP=1.30,    # most a run's pitch may grow to fill the width
        MARGIN_SHARE=0.55,  # how much of the left-over a side margin takes
    ),
    "compact": dict(
        EDGE_PAD=1.8,
        ITEM_GAP=1.5,
        GROUP_GAP=3.6,
        PITCH_CAP=1.25,
        MARGIN_SHARE=0.55,
    ),
}

#: VCV's Fundamental panels are laid out on a fixed column grid rather than on
#: clearances: measured across all 33 of them, every jack and trim column sits
#: at 10.84 mm (exactly 32 px at Rack's 75 dpi) and every knob column at 13.02,
#: with almost no variance. Ours are derived from ink plus a gap, which lands
#: *under* all three on compact density -- trims by a millimetre and a half.
#: These floors put the family on VCV's grid where our own arithmetic would
#: come out tighter, so nothing here is more crowded than a Fundamental module.
#: Keyed off ink size rather than class, because VCV put trims on the small
#: grid with the jacks and only true knobs on the wide one.
VCV_PITCH_SMALL = 10.84
VCV_PITCH_KNOB = 13.02
VCV_KNOB_INK = 5.0      # half-width at which a column counts as knob-sized
VCV_SMALL_INK = 4.2     # below this, a column holds no VCV-sized hardware


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
    # A readout is its own class: it is a plate rather than a component, and a
    # run of them should not be grouped with the knobs they name.
    "readout": "readout",
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


#: Glyphs that actually put ink below the baseline. Kept deliberately wide --
#: the punctuation and the tailed capitals are in here so a label that might
#: descend is never clipped, at the cost of the odd word paying for room it
#: does not use. Everything not listed sits on the baseline.
DESC_GLYPHS = frozenset("gjpqy Q,;()[]{}/@$&_".replace(" ", ""))


def desc_h(size_px, text=None):
    """Room below a label's baseline.

    A panel of uppercase words does not need any: not one control label in the
    family contains a descender, and reserving the space regardless cost every
    labelled row half a millimetre it never used. Pass the text and the room is
    reserved only when a glyph actually claims it -- captions in sentence case
    ("Explanation of Items") still get theirs."""
    if text is not None and not (DESC_GLYPHS & set(text)):
        return 0.0
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
    return (x0, l["y"] - cap_h(l["size"]), x0 + w,
            l["y"] + desc_h(l["size"], l["text"]))


def seat(kind):
    """How far the recessed well extends past the widget's own art.

    These are the numbers that decide how close two controls can sit, and they
    were set by eye. Measured against Rack's own Fundamental set they were too
    generous: a jack row there sits at a 10.81 mm pitch and a knob row at 13.02,
    and this kit could not go below 11.30 and 13.50. The seat is where that
    difference lived -- it is drawn *outside* the component's own art, so every
    control was wearing a ring nobody else in the rack wears.

    Trimmed to sit just inside VCV's floor, so a panel of this family packs at
    least as tightly as the modules it will be racked next to."""
    if kind == "readout":
        return 0.30          # a display, not a component: the well is its bezel
    if kind == "jack":
        return 0.28          # 10.5 mm minimum jack pitch; Fundamental's is 10.81
    if kind in ("light", "light_small"):
        return 0.55
    return 0.70              # 12.9 mm minimum knob pitch; Fundamental's is 13.02


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


#: Between a widget's ink and a label standing beside it.
SIDE_GAP = 1.1

#: How far the box round a named run stands off the ink it encloses.
GROUP_PAD = 0.9

#: How far the pair rule holds off the label ink it breaks around.
TIE_PAD = 0.7

#: A tie segment shorter than this is a speck, not a line: drop it.
TIE_MIN = 0.8

#: How much *visible* line a pair rule needs on each side of the label it breaks
#: around. Below this the tie is a speck under the seats -- measured at 0.20 mm
#: a side on Collusion before the paired gap was made to allow for it, which is
#: to say it was drawn, correct, and invisible.
TIE_SHOW = 1.4


def label_ext(it):
    """(left, right) reach of the label beside, under or over a widget, from its
    centre.

    Asymmetric on purpose: a lit label's light hangs off one end only, and
    treating it as if it stood on both sides is what used to make a column a
    couple of millimetres wider than anything in it actually was.

    A label placed to one *side* reaches its whole width that way and nothing
    the other -- and, more to the point, it costs the row no height at all. A
    column of jacks that would otherwise spend a line of text on every row can
    put its names alongside and pay for them once, horizontally, which is what
    a stack of outputs down the edge of a panel wants.
    """
    if not it.label:
        return 0.0, 0.0
    side = it.label_side
    if side in ("left", "right"):
        reach = ink_hw(it) + SIDE_GAP + text_w(it.label, it.size)
        return (reach, 0.0) if side == "left" else (0.0, reach)
    w = text_w(it.label, it.size) / 2
    l = r = w
    if it.light:
        reach = w + TEXT_CLEAR + 2 * well_extent("light_small")[0]
        if it.light_side == "left":
            l = reach
        else:
            r = reach
    return l, r


#: Between an interstitial widget's ink and the columns either side of it. It
#: is tighter than ITEM_GAP on purpose: the thing in the gap is an accessory to
#: its neighbours, and reads as one by sitting closer to them than they do to
#: each other.
INTER_GAP = 0.9


def _cols_of(row):
    """Column index for each item in a row; None for an interstitial one.

    Implicit indices skip the interstitial items, so a light dropped between
    two jacks does not push every column after it along by one."""
    out, nxt = [], 0
    for it in row.items:
        if getattr(it, "between", None):
            out.append(None)
            continue
        c = nxt if it.col is None else it.col
        out.append(c)
        nxt = c + 1
    return out


def _reach(row, it, side):
    """How far one item reaches to the left or right of its own centre."""
    if it is None:
        return 0.0
    hw = ink_hw(it)
    if row.silent:
        return hw
    ll, rr = label_ext(it)
    return max(hw, ll if side == "L" else rr)


def _inter(rowset):
    """(a, b) -> (half-extent, the gap that pair needs to hold it).

    The gap is measured against what *that row* puts either side of it, not
    against the columns' reaches. A column's reach is the widest thing any row
    puts in it, and on Kickback that is a lit "TOM III" on the trigger row and a
    side-label on the gate column -- 23 mm of text that the BURST switch, four
    rows below both, never has to clear. Sized off the columns the switch cost
    five HP; sized off its own row it costs none."""
    out = {}
    for row in rowset:
        at = _cols_of(row)
        by_col = {c: o for c, o in zip(at, row.items) if c is not None}
        for it in row.items:
            b = getattr(it, "between", None)
            if not b:
                continue
            hw = ink_hw(it)
            if not row.silent:
                ll, rr = label_ext(it)
                hw = max(hw, ll, rr)
            a0, b0 = min(b), max(b)
            ra = _reach(row, by_col.get(a0), "R")
            rb = _reach(row, by_col.get(b0), "L")
            phw, pra, prb = out.get((a0, b0), (0.0, 0.0, 0.0))
            out[(a0, b0)] = (max(phw, hw), max(pra, ra), max(prb, rb))
    return out


def _cells(rowset):
    """Column index -> (left reach, right reach, class), taking the widest of
    everything any row puts in that column."""
    cells = {}
    for row in rowset:
        for c, it in zip(_cols_of(row), row.items):
            if c is None:      # lives in a gap, not a column
                continue
            hw = ink_hw(it)
            ll, rr = (0.0, 0.0) if row.silent else label_ext(it)
            L, R, cls, top = cells.get(c, (0.0, 0.0, None, -1.0))
            if hw > top:
                cls, top = CLASS[it.kind], hw
            cells[c] = (max(L, hw, ll), max(R, hw, rr), cls, top)
    n = max(cells) + 1 if cells else 0
    # the 4th element is the widest *ink* on the column -- label overhang is in
    # L and R, and a floor keyed off ink must not see it
    return [cells.get(c, (0.0, 0.0, "knob", 0.0)) for c in range(n)]


def _runs(cols, groups=(), inter=None):
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
    for c, (L, R, cls, _ink) in enumerate(cols):
        pl, pr, prev_cls = (cols[c - 1][:3] if c else (0.0, 0.0, None))
        a, b = max(L, R), max(pl, pr)
        if c and (cls == prev_cls or "light" in (cls, prev_cls)
                  or min(a, b) / max(a, b, 1e-6) >= GROUP_RATIO):
            runs[-1].append(c)
        else:
            runs.append([c])
    return runs


def _gutters(cols, runs, hm, inter=None):
    """The width of each boundary between runs.

    A gutter is where a row changes gear, and it is usually the same everywhere
    on a row. It is not, when something is placed in one: Kickback's BURST
    switch sits between the ratio knobs and the column of gate outputs, which
    are deliberately separate runs. Sizing the boundaries individually is what
    lets a widget live in a gutter rather than only in a gap inside a run."""
    out = []
    for r, nxt in zip(runs, runs[1:]):
        g = hm["GROUP_GAP"]
        a, b = r[-1], nxt[0]
        held = (inter or {}).get((min(a, b), max(a, b)))
        if held:
            hw, ra, rb = held
            spare = (cols[a][1] - ra) + (cols[b][0] - rb)
            g = max(g, 2 * INTER_GAP + 2 * hw - spare)
        out.append(g)
    return out


def _run_pitch(cols, run, hm, inter=None):
    """The tightest even pitch a run can be spaced at: wide enough for its
    widest neighbouring pair, so an even run really is even."""
    if len(run) < 2:
        return 0.0
    return max(_run_gaps(cols, run, hm, inter))


def _run_gaps(cols, run, hm, inter=None):
    """The tightest each gap in a run may be, one figure per gap.

    A run used to be spaced at a single pitch -- the widest neighbouring pair,
    applied to every gap in it. That reads well when the run really is a row of
    like things, and costs a great deal when it is not: on AuditLogic the four
    indicator lights between the gate inputs sit in columns of their own, and
    charging each of them the pitch a *knob* needs bought six HP of empty panel.
    Sizing every gap to the two columns it actually separates keeps a run of
    like things exactly as even as it was -- their gaps are all equal anyway --
    and lets a row that mixes hardware close up round the small parts.
    """
    out = []
    for a, b in zip(run, run[1:]):
        g = cols[a][1] + cols[b][0] + hm["ITEM_GAP"]

        # VCV's grid is a floor between two pieces of real hardware -- jack to
        # jack, knob to knob. A light wedged between two jacks is not on that
        # grid and never was: forcing it there buys a column of empty panel and
        # costs the width of one.
        ia, ib = cols[a][3], cols[b][3]
        if min(ia, ib) >= VCV_SMALL_INK:
            g = max(g, VCV_PITCH_KNOB if max(ia, ib) >= VCV_KNOB_INK
                    else VCV_PITCH_SMALL)
        # something living in this gap has to fit in it
        held = (inter or {}).get((min(a, b), max(a, b)))
        if held:
            hw, ra, rb = held
            g = max(g, ra + INTER_GAP + 2 * hw + INTER_GAP + rb)
        out.append(g)
    return out or [0.0]


def natural_span(cols, hm, groups=(), inter=None):
    """How much width the columns need at their tightest -- the number that
    decides how many HP a panel actually is."""
    if not cols:
        return 0.0
    runs = _runs(cols, groups, inter)
    total = 0.0
    for run in runs:
        total += (cols[run[0]][0] + cols[run[-1]][1]
                  + sum(_run_gaps(cols, run, hm, inter)))
    return (total + sum(_gutters(cols, runs, hm, inter))
            + 2 * hm["EDGE_PAD"])


def place_columns(rowset, x_lo, x_hi, hm, groups=()):
    """Column centres for one set of rows that share a grid. Returns
    (centres, shortfall) -- shortfall is how much width the rows are short of,
    and is zero on a panel that fits."""
    cols = _cells(rowset)
    if not cols:
        return [], 0.0
    inter = _inter(rowset)
    runs = _runs(cols, groups, inter)
    gaps = [_run_gaps(cols, r, hm, inter) for r in runs]
    ends = [cols[r[0]][0] + cols[r[-1]][1] for r in runs]

    avail = (x_hi - x_lo) - 2 * hm["EDGE_PAD"]
    gutters = _gutters(cols, runs, hm, inter)
    need = sum(ends) + sum(sum(g) for g in gaps) + sum(gutters)
    if need > avail:
        return None, need - avail

    # 1. let every run breathe, in step, until each hits its own cap. A run that
    #    caps out stops taking slack; the rest keep growing. Every gap in a run
    #    grows by the same amount, so a run that started even stays even; the
    #    cap is set by the *tightest* gap, which is the one that would look
    #    stretched first.
    slack = avail - need
    grow = [0.0] * len(runs)
    room = [min(g) * (hm["PITCH_CAP"] - 1.0) if g else 0.0 for g in gaps]
    live = [i for i, r in enumerate(runs) if len(r) > 1]
    while live and slack > 1e-6:
        steps = sum(len(gaps[i]) for i in live)
        want = slack / steps
        take = min(want, min(room[i] - grow[i] for i in live))
        for i in live:
            grow[i] += take
        slack -= take * steps
        if take >= want - 1e-9:
            break
        live = [i for i in live if room[i] - grow[i] > 1e-6]

    # 2. whatever is still over goes to the gutters between runs and, at a
    #    discount, to the two side margins -- so a row that changes gear collects
    #    its slack where the change happens instead of at one end.
    units = (len(runs) - 1) + 2 * hm["MARGIN_SHARE"]
    unit = slack / units if units > 1e-6 else 0.0
    gutters = [g + unit for g in gutters]
    margin = hm["EDGE_PAD"] + hm["MARGIN_SHARE"] * unit

    centres = [0.0] * len(cols)
    x = x_lo + margin
    for i, run in enumerate(runs):
        x += cols[run[0]][0]
        centres[run[0]] = x
        for k, c in enumerate(run[1:]):
            x += gaps[i][k] + grow[i]
            centres[c] = x
        if i < len(gutters):
            x = centres[run[-1]] + cols[run[-1]][1] + gutters[i]
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
    solved = []          # the centres each block grid came out at
    for rows, kind, groups in _rowsets(panel):
        items = [it for row in rows for it in row.items]
        if not items or all(it.x is not None for it in items):
            continue

        # The footer may borrow the grid of a section instead of solving its
        # own. A band solved independently agrees with the block above it only
        # by luck: the two are inset differently and hold different numbers of
        # things, so their pitches differ and the columns walk apart across the
        # panel. Borrowing is the only thing that actually keeps a jack under
        # the control it belongs to.
        centres = None
        if kind == "band" and panel.footer_grid:
            want = {c for row in rows for c in _cols_of(row) if c is not None}
            for cs in solved:
                if want and max(want) < len(cs):
                    centres = cs
                    break
            if centres is None:
                out_of_range = sorted(want)
                raise ValueError(
                    "%s: footer_grid is set but no section grid covers "
                    "columns %s -- the footer's col= indices must be the "
                    "section's own" % (panel.slug, out_of_range))

        if centres is None:
            x_lo, x_hi = _bounds(panel, kind, hm)
            centres, miss = place_columns(rows, x_lo, x_hi, hm, groups)
            if centres is None:
                short = max(short, miss)
                continue
            if kind == "block":
                solved.append(centres)
        cols = _cells(rows)
        for row in rows:
            for c, it in zip(_cols_of(row), row.items):
                if c is None:
                    # Centred in the *clear* space, not between the two column
                    # centres: a column whose label reaches out sideways (a jack
                    # naming itself to its left, say) owns ground well past its
                    # own centre, and splitting centre to centre drops the
                    # widget straight onto that text.
                    a, b = it.between
                    at = _cols_of(row)
                    by_col = {k: o for k, o in zip(at, row.items) if k is not None}
                    lo = centres[a] + _reach(row, by_col.get(a), "R")
                    hi = centres[b] - _reach(row, by_col.get(b), "L")
                    it.x = (lo + hi) / 2.0
                else:
                    it.x = centres[c]
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
        need = max(need, natural_span(cols, hm, groups, _inter(rows)) + pad)
    # The masthead has to hold the title between the two top screws at its
    # smallest legible size, or the panel is too narrow to be named -- and it
    # has to hold the line *under* the title too. The brand sits bottom-left and
    # the form stub bottom-right on the same baseline, so a panel narrow enough
    # for them to meet is as unbuildable as one too narrow for its own name; it
    # just fails later, in the linter, as an overlap that looks like a spec
    # error rather than a width one.
    from .emit import (SCREW_CLEAR, TITLE_MIN, TITLE_TRACK, STUB_SIZE,
                       LOGO_W, LOGO_GAP)

    need = max(need, 2 * SCREW_CLEAR
               + len(panel.title) * (0.60 * TITLE_MIN + TITLE_TRACK) * S.MM_PER_PX)
    stub = 0.0
    if panel.brand:
        stub += SCREW_CLEAR + 0.4 + LOGO_W + LOGO_GAP + text_w(panel.brand, STUB_SIZE, 0.4)
    if panel.form:
        stub += text_w(panel.form, STUB_SIZE, 0.4) + 4.2
    if panel.brand and panel.form:
        stub += 3.0          # they must not merely miss, they must read apart
    need = max(need, stub)
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
        self.groups = []    # (x0, y0, x1, y1) the box round a run named once
        self.ties = []      # (x, y0, y1) hairline joining a control to its pair
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
                                 m["CAP_BASE"] + desc_h(6.0, sec.caption) + TEXT_CLEAR)
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
                    # the row above instead -- and wide enough for the pair rule
                    # to show either side of it, which is the same figure the
                    # label's own clearance uses above.
                    gap = max(m["BELOW_GAP"], TEXT_CLEAR, TIE_PAD + TIE_SHOW)
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
                                sec.y0 + m["CAP_BASE"] + desc_h(6.0, sec.caption) + TEXT_TEXT)
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
                gap = max(m["BELOW_GAP"], TEXT_CLEAR, TIE_PAD + TIE_SHOW)
                sz = max([it.size for it in row.items if it.label] or [6.2])
                txt = "".join(it.label for it in row.items if it.label)
                below_top = nxt.y - max(_ink_r(it) for it in nxt.items)
                row.y = (below_top - gap - desc_h(sz, txt) - cap_h(sz) - gap
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
        # A panel with no footer band still has a bottom edge: the foot ribbon
        # between the screws. Without this the justify pass below never runs for
        # such a panel -- slack stays 0 -- and its rows pack against the masthead
        # with the whole lower face left empty, which is what eight rows of jacks
        # on Schedule A looked like before this line existed.
        if panel.sections:
            last = cursor - m["BLOCK_GAP"]
            slack = FOOT_Y - m["BLOCK_GAP"] - last

    # --- the pair rule ------------------------------------------------------
    # VCV join a trimpot to the jack it attenuates with a hairline down the
    # column the two share (Fundamental's VCF, res/VCF.svg, is three of them
    # side by side). It says "these are one control" before you have read a
    # word, which is the whole reason the paired idiom exists -- and it is the
    # piece we were missing: the label in the gap told you the pair was a pair
    # only once you read it.
    #
    # Ours is derived rather than hand-placed. It runs from the upper widget's
    # centre to the lower one's and is drawn beneath the seats, so both ends
    # vanish under their widgets and only the span between them shows -- the
    # same trick VCV pull by ending their segment on the trimpot's centre. Where
    # the pair's own label sits in that gap the rule breaks around its ink,
    # since a line through a word costs more than the tie gains.
    for sec in panel.sections:
        for i, row in enumerate(sec.rows):
            cols = getattr(row, "_pair_cols", set())
            if not cols or i + 1 >= len(sec.rows):
                continue
            nxt = sec.rows[i + 1]
            y0, y1 = getattr(row, "_y", None), getattr(nxt, "_y", None)
            if y0 is None or y1 is None or y1 <= y0:
                continue
            below = {(j if it.col is None else it.col)
                     for j, it in enumerate(nxt.items)}
            for j, it in enumerate(row.items):
                c = j if it.col is None else it.col
                if c not in cols or c not in below or it.x is None:
                    continue
                # every label whose ink the rule would otherwise strike through
                cuts = []
                for lab in out.labels:
                    bx0, by0, bx1, by1 = label_box(lab)
                    if (bx0 - TIE_PAD <= it.x <= bx1 + TIE_PAD
                            and by1 > y0 and by0 < y1):
                        cuts.append((by0 - TIE_PAD, by1 + TIE_PAD))
                segs = [(y0, y1)]
                for c0, c1 in sorted(cuts):
                    kept = []
                    for s0, s1 in segs:
                        if c1 <= s0 or c0 >= s1:
                            kept.append((s0, s1))
                            continue
                        if s0 < c0:
                            kept.append((s0, c0))
                        if c1 < s1:
                            kept.append((c1, s1))
                    segs = kept
                for s0, s1 in segs:
                    if s1 - s0 >= TIE_MIN:
                        out.ties.append((it.x, s0, s1))

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
    below = {c for c in _cols_of(nxt) if c is not None}
    if row.pair:
        return below
    return below & {c for c, it in zip(_cols_of(row), row.items)
                    if c is not None and it.pair}


def _tops_with_text(row):
    """Does this row put a label above its widgets? Then the gap over it is a
    gap between two pieces of text, which needs more room than ink to a well."""
    if row.silent:
        return False
    paired = getattr(row, "_pair_cols", set())
    if row.shared:
        return (row.label_side or row.items[0].label_side) == "above"
    return any(c not in paired
               and (row.label_side or it.label_side) == "above" and it.label
               for c, it in zip(_cols_of(row), row.items))



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

    def above_parts(it, size, text=None):
        """(well-to-baseline, baseline-to-top) for a label above `it`."""
        cap, desc = cap_h(size), desc_h(size, text)
        lit = it is not None and bool(it.light)
        off = max(max(m["ABOVE_GAP"], TEXT_CLEAR) + desc,
                  light_hh - cap / 2 if lit else 0.0)
        head = max(cap, cap / 2 + light_hh if lit else 0.0)
        return off, head

    def below_parts(it, size, tied=False, text=None):
        """(well-to-baseline, baseline-to-bottom) for a label below `it`.

        A tied control needs more: the pair rule runs down this same gap and
        breaks around the label, so the gap has to hold the line's visible run
        and its standoff as well as the clearance the text alone would want."""
        cap, desc = cap_h(size), desc_h(size, text)
        lit = it is not None and bool(it.light)
        clear = max(m["BELOW_GAP"], TEXT_CLEAR)
        if tied:
            clear = max(clear, TIE_PAD + TIE_SHOW)
        off = max(clear + cap, light_hh + cap / 2 + 0.2 if lit else 0.0)
        foot = max(desc, light_hh - cap / 2 if lit else 0.0)
        return off, foot

    # A paired control's label sits below it, in the gap it shares with the widget
    # underneath -- that is what makes the pair read as a pair, rather than as a
    # label belonging to whatever happens to be above it. Only the columns that
    # really have a partner below move: a jack sharing the row but owning nothing
    # keeps its label over its head, where a patch cable cannot cover it.
    paired = getattr(row, "_pair_cols", set())
    #: (x0, x1, half-height) of each run this row names once, filled in beside
    #: the span's label and turned into a box once the row's y is known.
    spans = []

    at_col = _cols_of(row)

    def _side(it, i):
        if at_col[i] in paired:
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
        # A run named once. Its x is the midpoint of the run's own columns, not
        # of the panel, so it sits over what it names even when the row has
        # other things on it.
        by_col = {}
        for c, it in zip(at_col, row.items):
            if c is not None:
                by_col[c] = it
        for entry in getattr(row, "span", ()):
            c0, c1, text = entry[0], entry[1], entry[2]
            a, b = by_col.get(c0), by_col.get(c1)
            if a is None or b is None or a.x is None or b.x is None:
                continue
            size = entry[3] if len(entry) > 3 else row.shared_size
            ink = entry[4] if len(entry) > 4 else row.shared_ink
            texts.append(((a.x + b.x) / 2, text, size, ink, side, None))
            spans.append((min(a.x - ink_hw(a), b.x - ink_hw(b)),
                          max(a.x + ink_hw(a), b.x + ink_hw(b)),
                          max(_ink_r(by_col[c]) for c in range(c0, c1 + 1)
                              if c in by_col)))

    # The row's half-height above and below its centre line: the tallest well
    # or ring, plus whatever an above-label (and its light) needs. Labels on a
    # side share one baseline, taken from the item that needs the most room, so
    # mixed hardware on one row reads as one row.
    r = max(_ink_r(it) for it in row.items)
    # A shared label belongs to the whole row, so it clears the row's tallest well.
    # Labels standing to one side sit on their widget's own centre line and add
    # nothing to the row's height, so they take no part in any of these four.
    stacked = [t for t in texts if t[4] not in ("left", "right")]
    lift = max([(_ink_r(it) if it is not None else r) + above_parts(it, sz, tx)[0]
                for _, tx, sz, _, sd, it in stacked if sd == "above"] or [0.0])
    head = max([above_parts(it, sz, tx)[1]
                for _, tx, sz, _, sd, it in stacked if sd == "above"] or [0.0])
    def _tied(it):
        if it is None:
            return False
        i = next((k for k, o in enumerate(row.items) if o is it), None)
        return i is not None and at_col[i] in paired

    drop = max([(_ink_r(it) if it is not None else r)
                + below_parts(it, sz, _tied(it), tx)[0]
                for _, tx, sz, _, sd, it in stacked if sd != "above"] or [0.0])
    foot = max([below_parts(it, sz, _tied(it), tx)[1]
                for _, tx, sz, _, sd, it in stacked if sd != "above"] or [0.0])

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
        if this_side in ("left", "right"):
            # Vertically centred on the widget: the baseline sits half a cap
            # height below its centre line, which is what puts the word's
            # optical middle level with the jack's.
            hw = ink_hw(it) if it is not None else r
            lab = dict(x=(x - hw - SIDE_GAP) if this_side == "left"
                         else (x + hw + SIDE_GAP),
                       y=y + cap_h(size) / 2, text=text, size=size, ink=ink,
                       align="right" if this_side == "left" else "left",
                       tracking=0.0, ground="dark" if pinned else "light")
            out.labels.append(lab)
            if it is not None and it.light:
                _lit_label(out, lab, it.light, it.light_side)
            continue
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

    # Readouts: a plate that names its own control, placed where that control's
    # label would go. Sharing one baseline across the row, like labels, so a row
    # of them lines up -- but computed only from the controls that actually
    # carry one, so a big knob elsewhere on the row does not push them down.
    rds = [it for it in row.items if getattr(it, "readout", "")]
    if rds:
        rhw, rhh = well_extent("readout")
        rgap = max(m["BELOW_GAP"], TEXT_CLEAR)
        rdrop = max(_ink_r(it) + rgap + rhh for it in rds)
        for it in rds:
            out.widgets.append((it.readout, it.x, y + rdrop, "readout"))
            out.wells.append((it.x, y + rdrop, rhw, rhh, it.readout))
        lowest = max(lowest, y + rdrop + rhh)

    # The box round a run named once. It encloses the run's widgets and the one
    # word that names them, so the label is visibly a caption for those columns
    # rather than a stray word floating between two of them.
    #
    # Not on a paired row, though. There the label sits *between* two rows
    # because it names both -- the trim and the jack it owns -- and a box drawn
    # round only the upper one says the opposite of what the pair idiom is for.
    # Better no box than a box that draws the wrong line.
    for x0, x1, hh in (() if paired else spans):
        if side == "above":
            top, bot = y - lift - head - GROUP_PAD, y + hh + GROUP_PAD
        else:
            top, bot = y - hh - GROUP_PAD, y + drop + foot + GROUP_PAD
        out.groups.append((x0 - GROUP_PAD, top, x1 + GROUP_PAD, bot))

    # Where this row actually landed, for the pair rule -- which is drawn once
    # every row is placed, because it needs the row below this one as well.
    row._y = y

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

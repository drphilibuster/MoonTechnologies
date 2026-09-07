"""The panel description language.

A panel is declared, not drawn. You say which controls exist, which row they sit
on, and which section they belong to; the layout solver in layout.py works out
every y coordinate, every felt block extent, every label baseline and every
recessed well. Nothing in a plugin's tools/panel.py should contain a hand-tuned
millimetre unless it is a row's x position or a deliberate exception.

Units are millimetres throughout, except font sizes, which are Rack pixels --
because that is what nvgFontSize() takes, and keeping them in the same unit the
C++ uses is the only way the preview can be exact.
"""

from dataclasses import dataclass, field

#: The maker. One string for the whole family, here rather than in each spec, so
#: the mark on the panel cannot disagree with `brand` in plugin.json.
BRAND = "MOON TECHNOLOGIES"

# --- Rack's fixed grid ------------------------------------------------------
PX_PER_MM = 75.0 / 25.4
MM_PER_PX = 25.4 / 75.0
HP_MM = 5.08
PANEL_H = 128.5                     # the standard Eurorack 3U face
GRID_W_PX = 15.0                    # RACK_GRID_WIDTH
GRID_H_PX = 380.0                   # RACK_GRID_HEIGHT

#: Stock ComponentLibrary radii in mm, measured off the SVG canvases at SVG_DPI 75.
#: Used for wells, label placement and the browser preview, so a widget's art and
#: its clearance can never disagree.
RADIUS = {
    "knob_large": 6.10,             # RoundLargeBlackKnob   36 px
    "knob": 4.80,                   # RoundBlackKnob    28.348 px
    "trim": 3.02,                   # Trimpot           17.856 px
    "slider": 4.80,                 # a slider occupies a knob's cell
    "button": 3.05,                 # VCVButton             18 px
    "bezel": 3.60,                  # VCVLightBezel     21.26 px
    "jack": 4.01,                   # PJ301M              23.7 px
    "light": 1.60,                  # MediumLight
    "light_small": 1.19,            # SmallLight
    "switch": 3.50,                 # CKSS         14 x 20.64 px -- half its height
    "switch3": 4.80,                # CKSSThree 13.457 x 28.35 px -- ditto
    "readout": 2.20,                # a little lit plate that names its control
}

#: Half-extents in mm for the widgets that are not round. Everything else takes
#: its radius for both axes. Wells and clearance checks use this; label placement
#: uses RADIUS, which is always the vertical half-extent.
EXTENT = {
    # A control whose meaning changes cannot be engraved. Diversified's macro
    # knobs are a different parameter for every one of its programs, so each
    # wears a small lit plate where its label would go and the module writes
    # the current name into it -- or "--" when the running program has nothing
    # for that knob to do, which is the honest thing for a panel to say.
    # Exactly a knob's half-width, so a plate naming a knob never widens the
    # column it stands in -- and with the readout's thinner seat it comes out
    # narrower than the knob itself, which means free. A longer name ellipsizes
    # rather than the panel growing to hold it.
    "readout": (4.80, 2.20),
    "switch": (2.37, 3.50),
    "switch3": (2.28, 4.80),
    "slider": (2.37, 4.80),
}

#: Which side of a widget its label sits on. This is the family rule, and the
#: reason it is a table rather than a per-panel choice: a jack's label has to
#: clear the cable plugged into it, so it goes above; a knob's has to clear the
#: hand turning it, so it goes below.
#:
#: A spec may override it per widget with `side=` -- including "left" and
#: "right", which put the label alongside instead of over or under. That is the
#: escape hatch for a stack of controls in one column, where a label per row is
#: a line of text per row and the panel would rather spend the width once.
LABEL_SIDE = {
    "knob_large": "below", "knob": "below", "trim": "below",
    "slider": "below", "button": "below", "bezel": "below",
    "jack": "above", "light": "below", "light_small": "below",
    "switch": "below", "switch3": "below",
    "readout": "below",
}


@dataclass
class Widget:
    """One control. `kind` keys into RADIUS and LABEL_SIDE.

    `x` is optional, and leaving it out is the norm: the horizontal solver in
    layout.py works out every column centre from the widths of the things in the
    column, so a row's spacing follows from what is actually on it. Set `x` only
    where an outside constraint fixes it -- a control that has to line up with a
    field of a live display.
    """
    name: str                       # the component-layer id, matches the C++ enum
    x: float = None
    kind: str = "knob"
    label: str = ""
    ink: str = "PAPER"
    size: float = 7.0               # label size in Rack px
    well: bool = True               # draw the recessed seat behind it
    #: Override LABEL_SIDE; blank follows the family rule. "above" and "below"
    #: stack the label over or under the widget and cost the row a line of text
    #: each; "left" and "right" stand it alongside, on the widget's own centre
    #: line, and cost the row no height at all. A column of jacks running down
    #: the edge of a panel wants the latter -- it pays for its names once,
    #: horizontally, instead of once per row.
    side: str = ""
    #: The one control on the panel you actually reach for. Gets a lime ring, and
    #: there should never be more than one per panel -- lint checks.
    primary: bool = False

    #: A small light drawn immediately to the right of this widget's label, for
    #: the "lit label" idiom -- an indicator that belongs to a control by name.
    light: str = ""
    #: Which end of the label the light sits at: "right" (the rule) or "left",
    #: for a control in the last column whose light would otherwise run off
    #: the block.
    light_side: str = "right"

    #: Which column of its section this widget sits in. Defaults to its index in
    #: the row, which is right whenever a row is written left to right; set it
    #: only for a row that skips a column or lists its items out of order.
    col: int = None

    #: Sit in the *gap* between two columns, as (first, second), owning no
    #: column of its own. An indicator light tucked between a gate's two inputs
    #: is not a column of the panel -- it is an accessory to the pair either
    #: side of it -- and giving it one costs the full pitch of whatever the
    #: widest thing in that run happens to be. The solver widens just that one
    #: gap enough to hold it and centres it there. `col` is ignored when this
    #: is set; give every other item in the row an explicit `col`.
    between: tuple = None

    #: This control owns the widget directly below it and shares a label with
    #: it -- the paired idiom, opted into one control at a time. `Row(pair=True)`
    #: is the shorthand for every column that has a partner below.
    pair: bool = False

    #: A knob that is really a selector: how many detents it has. The renderer
    #: engraves that many subdividers into the dark of its own well and closes
    #: them with an arc through the knob's real travel, so a stepped control
    #: cannot be mistaken for a continuous one -- and, because the whole figure
    #: lives inside the well, costs the layout nothing. Meaningless on a switch,
    #: which already looks like what it is, and ignored there.
    steps: int = 0

    @property
    def r(self):
        return RADIUS[self.kind]

    @property
    def extent(self):
        return EXTENT.get(self.kind, (self.r, self.r))

    @property
    def label_side(self):
        return self.side or LABEL_SIDE[self.kind]

    @property
    def step_count(self):
        """Detents to engrave. A switch is already legibly a switch, so asking
        for them there is a no-op rather than an error: the spec may say what a
        control is without having to know how the kit draws it."""
        if self.kind in ("switch", "switch3"):
            return 0
        return int(self.steps or 0)


@dataclass
class Row:
    """A horizontal band of widgets that share one baseline."""
    items: list
    #: When set, one label serves the whole row and is centred on the panel
    #: rather than on each widget -- the paired trim/jack idiom.
    shared: str = ""
    #: Name a *run* of columns once instead of naming each of them, as
    #: (first_col, last_col, text) triples. Six identical DECAY trims side by
    #: side is the panel saying the same word six times; one word centred over
    #: the six says it better and leaves the rest of the row -- which is usually
    #: not six of anything -- to carry its own labels. The items in the run
    #: should be given `label=""`, and the span label lands on the row's own
    #: baseline, so it lines up with whatever else the row names.
    #:
    #: Unlike `shared`, this does not silence the row: it is a label for those
    #: columns, not for all of them.
    span: list = field(default_factory=list)
    shared_ink: str = "SAGE"
    shared_size: float = 6.2
    #: Rows normally get their y from the solver. Pin it only when an external
    #: constraint fixes it -- the audio row against the bottom screws, say.
    y: float = None
    #: Suppress this row's labels entirely; the row above already names it.
    #: This is the paired idiom -- a trimpot sitting directly over its jack,
    #: one label serving both.
    silent: bool = False
    #: Override LABEL_SIDE for every item in the row. Set "above" on the trim
    #: row of a pair so the shared label sits clear of both widgets.
    label_side: str = ""
    #: Solve this row's columns on their own rather than on the section's grid.
    #: For a row that answers a different question from the rows around it -- the
    #: four knee numbers under a subtotal rule -- where lining it up with the row
    #: above would be a coincidence rather than a relationship.
    own_grid: bool = False
    #: The paired idiom: this row's controls each own the widget directly below
    #: them (a trimpot over its CV jack), and one label serves both. The label is
    #: placed *between* the two rows, equidistant from each, so it cannot be read
    #: as belonging to the row above instead. The partner row is the next one and
    #: must be `silent=True`.
    pair: bool = False


@dataclass
class Section:
    """A felt block with a lime index tab and a caption, holding one or more rows."""
    caption: str
    rows: list
    #: A muted rule drawn between row i and row i+1, the "subtotal line".
    divide_after: tuple = ()
    #: A small light beside the caption, for a section that has a state of its own.
    caption_light: str = ""
    #: Where the row changes gear, when width alone cannot tell. The solver
    #: normally works the runs out from the column extents; a section whose
    #: grouping is semantic rather than physical -- four gates of three jacks,
    #: say, all the same size -- names the run lengths here instead. Must sum to
    #: the column count.
    groups: tuple = ()
    #: Solved:
    y0: float = 0.0
    y1: float = 0.0


@dataclass
class Glass:
    """The read-out well under the masthead."""
    h: float = 9.2
    name: str = "display"
    y: float = 0.0                  # solved


@dataclass
class Trace:
    """A lime routed connector, the panel drawing a relationship between controls.

    `points` is a polyline in mm. The renderer breaks it automatically wherever it
    would cross a label's box, so moving a label can never put a wire through its
    text -- which is what the hand-tuned GAP constants used to do.
    """
    points: list
    dots: list = field(default_factory=list)


@dataclass
class FreeLabel:
    """A label that belongs to no widget: a band caption, a footnote, a stub."""
    x: float
    y: float
    text: str
    size: float = 6.2
    ink: str = "SAGE"
    align: str = "center"           # center | left | right
    tracking: float = 0.0
    #: "light" (the pale face) or "dark" (a band or display). Blank lets the
    #: solver decide from y, which is right for anything not inside a plate.
    ground: str = ""


@dataclass
class Plate:
    """A rectangular field: a text box, a drop-down, a list well, a button.

    Panels whose face is mostly one live display are laid out in plates rather
    than rows -- but they are drawn from the same palette, with the same well
    borders and the same lime index tab, so they still read as the same family.
    """
    name: str
    x: float
    y: float
    w: float
    h: float
    fill: str = ""          # defaults to the glass colour
    r: float = 1.2
    tab: str = ""           # "LIME" / "MINT" to give the plate an index tab
    stroke: bool = True


@dataclass
class Panel:
    slug: str                       # matches plugin.json and res/<slug>.svg
    title: str                      # the masthead, drawn in PAPER
    form: str                       # the footer stub: "SCHEDULE UTP"
    #: Width in HP, or "auto" -- which is the norm. The horizontal solver knows
    #: how much room the rows need, so a panel that is asked for "auto" comes out
    #: exactly as wide as its contents, and never a hand-typed HP that happened
    #: to be a millimetre short. Pin it only where an outside constraint fixes
    #: the width, and the linter will still say if the rows do not fit.
    hp: object = "auto"
    #: "regular" or "compact". Same rules, tighter scale -- for panels at capacity.
    density: str = "regular"
    subtitle: str = ""              # optional masthead right-hand gloss
    brand: str = BRAND              # the mark at the masthead's bottom-left
    glass: Glass = None
    sections: list = field(default_factory=list)
    #: Rows that sit on the footer band rather than in a section.
    footer: list = field(default_factory=list)
    traces: list = field(default_factory=list)
    extra_labels: list = field(default_factory=list)
    lights: list = field(default_factory=list)   # (name, x, y) placed by hand
    #: Panels whose whole face is one live display (PatchAudit) declare their
    #: fields here instead of as rows. See Plate.
    plates: list = field(default_factory=list)
    #: Felt blocks a plate-laid-out panel wants, as (y0, y1). Row-laid-out panels
    #: get theirs from their sections and should leave this alone.
    extra_blocks: list = field(default_factory=list)
    #: Pin the footer band instead of deriving it from footer rows.
    band_footer: float = None

    #: Solve the footer band on the columns of the section above it, instead of
    #: on a grid of its own.
    #:
    #: The band is inset differently from a block and holds a different number
    #: of things, so two independent solves put column 2 in two different places
    #: -- on Kickback the OUT jacks drifted from 3.6 mm under their voice to
    #: 11.1 mm, which is more than a jack's width, so the rightmost OUT sat
    #: nearer the wrong voice than its own. Set this where the footer's `col=`
    #: indices are the section's own, and every jack lands under what it
    #: belongs to.
    footer_grid: bool = False
    #: Named millimetre constants echoed into the generated C++ header, for
    #: panels whose C++ needs to lay out its own sub-widgets.
    metrics: dict = field(default_factory=dict)
    #: The src/ subdirectory this module's C++ lives in, which is where its
    #: generated Panel.hpp is written. Defaults to the slug, which is what every
    #: module in this plugin uses; it exists so a module whose directory and slug
    #: differ does not have to be a special case in the writer.
    module: str = ""
    #: Solved: how many millimetres short of fitting its rows the panel is.
    shortfall: float = 0.0

    @property
    def src_dir(self):
        return self.module or self.slug

    @property
    def w(self):
        return self.hp * HP_MM

    @property
    def h(self):
        return PANEL_H

    def cols(self, n, margin):
        """n evenly spaced column centres, `margin` in from each edge."""
        if n == 1:
            return [self.w / 2]
        span = self.w - 2 * margin
        return [margin + span * i / (n - 1) for i in range(n)]

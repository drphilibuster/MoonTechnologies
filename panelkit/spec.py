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
    "stepper": 4.80,                # two buttons stacked: a knob's height, so it replaces one
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
    # Sized to a trimpot, which is what a panel dense enough to need runtime
    # labels is going to be using: the plate never widens the column it stands
    # in, and a name too long for it shortens rather than the panel growing.
    "readout": (4.30, 1.35),
    # A preset selector is a count, not an angle: UP and DOWN, one above the other
    # with a hair between, step it by one. Two buttons of 6.8 x 4.5 mm and a 0.6 mm
    # gap come to 9.6 mm, a standard knob's height, so it stands where a knob did
    # and costs a row nothing.
    "stepper": (3.40, 4.80),
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
    "readout": "below", "stepper": "below",
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

    #: A lit plate that names this control, placed where its label would go.
    #:
    #: For a control whose meaning is not fixed -- a macro knob that is DECAY on
    #: one program and SPREAD on the next. It is attached to the widget rather
    #: than laid out as a row of its own on purpose: on the grid every plate in
    #: a row has to clear the tallest widget in that row, so one big knob beside
    #: four small ones pushed all four plates a centimetre below the controls
    #: they name. A label does not behave that way and neither should this.
    #:
    #: The value is the widget name the plate is emitted under, so the module
    #: can find its position.
    readout: str = ""

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
        if self.kind in ("switch", "switch3", "stepper"):
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
class RailPair:
    """A jack and the control that scales it, side by side in a rail with the one label
    that names both standing over them: the jack on the outer side, where the cable
    comes in, and the trimpot after it, in signal order.

    The family's own pair idiom stacks the trim over its jack, and a rail would take that
    literally at the cost of the height the rail does not have -- four stacked pairs are
    twice as tall as four side by side, and a panel's height is the one thing it cannot
    grow. Side by side costs one trimpot's width instead."""
    trim: "Widget"
    jack: "Widget"
    label: str = ""
    size: float = 6.2
    ink: str = "SAGE"


@dataclass
class Rail:
    """A column of jacks standing down one edge of the face, in a felt block of its own
    beside the sections rather than under them.

    For a panel whose footer band is what sets its width -- a footer holds its jacks
    side by side, so thirty-four of them cost thirty-four columns however narrow the
    controls above are -- or whose footer is taller than one row of jacks. Moving them
    to a rail trades a few millimetres of width for the one dimension a panel never runs
    short of: the panel's height is fixed, and the sections already span it.

    It is drawn as the footer band is -- dark, with the band's mint tab -- because it
    is the band's own contents moved. Convention: outputs on the right, inputs on the
    left (`side`).

    `items` are jacks, whose labels stand to their left on the jack's own centre line
    (the family's side-label rule), or `RailPair`s, whose one label stands between the
    trim and the jack. The solver spaces them evenly down the block, below the caption.
    """
    items: list
    caption: str = ""
    side: str = "right"             # "right" for outputs, "left" for inputs
    ink: str = "MINT"               # the caption's role
    #: Stand from the top of the read-out well instead of from the top of the first
    #: section, beside the display as well as beside the sections, and take its width
    #: out of the display's. For a panel whose display is tall (Ledger's is 53 mm), the
    #: sections beside a rail are too short to hold more than three or four jacks; the
    #: whole height of the face holds seven.
    tall: bool = False
    #: Where a jack's label goes: "left" stands it on the jack's own centre line, which
    #: costs no height and the label's width; "above" is the family's rule for a jack
    #: and costs a line of text per jack instead, which is the cheaper currency in a
    #: tall rail and the dearer in a short one. "auto" lets space decide: the solver
    #: takes the narrowest arrangement that still fits the rail's height. A pair's
    #: label is the same choice -- over the pair, joined to both by a bracket, or
    #: beside it on its centre line.
    labels: str = "left"
    #: How many columns the rail's entries stand in: a matrix of jacks down the edge
    #: rather than a single file. "auto" takes the fewest that fit the height --
    #: every column is a jack's width off every row beside the rail, so the solver
    #: only adds one when a single file would run off the bottom.
    cols: object = 1
    #: Solved:
    x0: float = 0.0
    x1: float = 0.0


@dataclass
class Field:
    """A control that lives on the glass instead of on the face.

    Anything a display shows, a display can take: a value printed on the screen
    is the obvious place to change it, and the knob that used to stand below it
    was the same number twice and the panel's width spent on the duplicate. A
    field is that knob moved into the read-out -- bound to the same param, so a
    patch, a CV map, MIDI-Map, undo and the right-click menu are unchanged, and
    only its widget is different.

    The glass is cut into a grid (`Glass.grid`, rows x cols) and a field takes a
    `cell` of it, or a `span` of cells. The solver turns that into a rectangle
    and the emitter writes it into Panel.hpp as `FIELD_<NAME>`, which is where
    the display draws the value *and* where the hit region sits -- one number, so
    what you see and what you grab cannot drift apart.

    `kind` says how it behaves under the mouse, and picks the C++ widget:
      "value"   hold and drag up/down (the pointer turns into up/down arrows);
                the wheel nudges; double-click resets.         panel::ScreenKnob
      "select"  a choice among named options: click lists them to pick from,
                hold and drag scrolls through them.            panel::ScreenSelect
      "toggle"  click flips it (or steps a 3-way).             panel::ScreenSwitch
      "button"  momentary: held while the mouse is down.       panel::ScreenButton
      "menu"    click opens a menu the module fills -- for an
                action that is not a param (load a preset).    panel::ScreenMenu
    """
    name: str
    cell: tuple = (0, 0)            # (row, col) in the glass grid
    span: tuple = (1, 1)            # (rows, cols)
    kind: str = "value"
    #: On a glass laid out in plates (see Plate), the plate whose own `grid` the
    #: cell is counted in, instead of the glass's.
    plate: str = ""


#: The kinds a Field may be, and the C++ widget each one is.
FIELD_KINDS = {"value": "ScreenKnob", "select": "ScreenSelect",
               "toggle": "ScreenSwitch", "button": "ScreenButton", "menu": "ScreenMenu"}


@dataclass
class Glass:
    """The read-out well under the masthead."""
    h: float = 9.2
    name: str = "display"
    y: float = 0.0                  # solved
    #: The grid the glass is cut into for its fields, as (rows, cols). The
    #: display draws in the same cells, so its text lines are the grid's rows.
    grid: tuple = (1, 1)
    #: Controls that live on the screen. See Field.
    fields: list = field(default_factory=list)
    #: Margin inside the well before the first cell, and the gap between cells, mm.
    inset: float = 0.8
    gap: float = 0.5


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
    #: Cut into (rows, cols) for the Fields that name this plate.
    grid: tuple = (1, 1)


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
    #: What the module *is*, in plain words -- "MULTIMODE FILTER" -- on the line
    #: under the title, where you read it while patching. The names are puns on
    #: tax forms and say nothing about the circuit; this says what it does. With
    #: it set, the line is the descriptor's alone: the maker's wordmark runs up the
    #: left margin and the form number down the right.
    what: str = ""
    brand: str = BRAND              # the mark at the masthead's bottom-left
    glass: Glass = None
    sections: list = field(default_factory=list)
    #: Rows that sit on the footer band rather than in a section.
    footer: list = field(default_factory=list)
    #: A column of jacks down the right-hand edge. See Rail.
    rail: "Rail" = None
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
    #: Where the footer changes gear, as run lengths that sum to its column count --
    #: the same device as Section.groups, for a band that holds several kinds of
    #: jack. A gutter between runs is what tells a patch cable which jacks belong
    #: together; without it thirty identical circles read as one row.
    footer_groups: tuple = ()
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

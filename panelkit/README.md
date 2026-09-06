# panelkit

One panel pipeline for the whole plugin. A module describes its panel once, in
`tools/panels/<Module>.py`, and everything else is generated:

```
tools/panels/<Module>.py           the spec -- the only file you edit
      │
      ├── res/<Module>.svg              panel artwork + the hidden components layer
      ├── res/ScrewHex.svg              the family screw
      ├── res/PortIn.svg                the family jacks
      ├── res/PortOut.svg
      ├── src/PanelTheme.hpp            palette, hardware, text kit -- shared
      ├── src/<Module>/Panel.hpp        this panel's geometry and silkscreen
      ├── tools/previews/<Module>.html  browser mock at true radii and true fonts
      └── tools/previews/<Module>.png   the panel as VCV Rack renders it  (--vcv)
```

Nothing is typed twice, so nothing can drift. Before this existed, each module
carried its own copy of the palette, its own screw generator, its own silkscreen
table and its own idea of where a label goes, and all four had already diverged.

```bash
make panel                 # regenerate every panel
make panel-Retroactive     # just one
make preview-Retroactive   # ... and open the browser mock
make vcv-preview           # build, then render every panel through VCV Rack
```

## Two headers, split by linkage

The twenty modules ship as one plugin, so every panel's C++ is linked into one
binary. That is why the generated C++ comes out as two files rather than one:

| | linkage | why |
|---|---|---|
| `src/PanelTheme.hpp` | `inline` / templates — one definition for the whole plugin | The palette, the text kit, the hardware. Identical in every translation unit, so the One Definition Rule is satisfied by the definitions being the same. |
| `src/<Module>/Panel.hpp` | `static` — internal, one copy per translation unit | `HP`, `W`, the widget positions, the `LABELS` table. Three different silkscreens cannot collide. |

Both live in `namespace panel`, so a module's C++ says `panel::LIME` and
`panel::TIME_POS` exactly as it did when each module was its own plugin. The one
rule this imposes: **a translation unit may include one `Panel.hpp`, never two.**
Including two is a duplicate-definition error at compile time, which is the
failure mode you want.

Getting this wrong the obvious way — one header per module, all of it `inline` —
does not fail to build. It links, keeps one arbitrary copy of `Labels::draw`,
and draws one module's silkscreen on every panel.

## The design language

Every panel in the family is the same object: a banknote. Pale engraved paper,
dark bands top and bottom that the denomination and the signatures print on,
sage guilloche round every field, and a scroll curled into each corner. The
five colours are the "High Contrast" palette (icolorpalette.com/color/7b9a6d):
two near-blacks, two off-whites, one sage. Everything else is derived from them.

| element | rule |
|---|---|
| the paper | `PAPER`, the whole face |
| masthead | `BAND` to y=9.0, a braided ribbon between the top screws, closed by a sage rule. Title in `PAPER`, auto-sized to clear both screws. The form number sits under it on the right in `SAGE` |
| margins | a braided sage ribbon runs each side of the face between the bands, the way a note's border frames its engraving |
| section block | `FELT`, inset 3 mm, rounded 1.6, framed in a sage hairline, a **scroll** in three corners and a 7 × 0.5 mm **ink index tab** at the fourth (top-left) |
| section caption | secondary ink, 6 px, centred on the block, 3.3–3.6 mm below its top edge |
| control label | primary ink for a primary control, secondary for a secondary. **Below** knobs, buttons and switches; **above** jacks, where a cable would cover it |
| ink and ground | a label's role (`PAPER` primary, `SAGE` secondary, `LIME` accent, `MINT` output, `CLAY` warning) is what the spec says; what it lands as depends on where it sits. On the pale face it is `INK` / `SAGE_DARK` / `LIME_DARK` / `MINT_DARK` / `CLAY_DARK`; on a band or in a display it is `PAPER` / `SAGE` / `LIME` / `MINT` / `CLAY`. The solver tags every label with its ground; the emitter resolves it |
| paired control | a trimpot directly over its jack shares one label, set in the gap **between** the two and equidistant from each, so it cannot be read as naming the row above instead |
| stepped knob | a knob that is really a selector carries a detent for every position it has, plus an arc through its real travel, engraved into the dark of its own well — so it cannot be mistaken for a continuous control, and costs the layout nothing |
| lit label | a small light immediately right of a label names what it reports |
| the primary action | exactly one control per panel wears a double sage ring, a seal struck twice |
| trace | a sage wire between controls the panel wants to relate, engraved as a wave, tied off with a rosette, breaking itself around any label it crosses |
| well | every widget sits in a dark `GLASS` seat ringed in sage |
| read-out | `GLASS` well under the masthead, `RULE` border. Words in Share Tech Mono, numerals in DSEG7 |
| footer band | `BAND`, with a **mint** index tab, a braided ribbon under the jacks between the bottom screws. Holds the I/O and anything that leaves the module |
| hardware | brass hex screws and brass-collared jacks; outputs ring mint |
| masthead footing | the mark, then the brand, bottom-left; the form number bottom-right — a note's issuing office and its series |
| the mark | a dollar sign, backwards: the real ASCII `$` set in the panel's own face and drawn through a `scale(-1, 1)`, in `LIME` |
| form number | every panel carries a real IRS form number matching its verb — `FORM 1040-X` (amended return), `SCHEDULE UTP` (uncertain tax position), `FORM 4564` (document request) |

All of the ornament is polyline `<path>` data and filled primitives, because
that is what Rack's renderer keeps (see the two constraints below); a wave
sampled every 0.35 mm is indistinguishable from a curve at any zoom Rack has.

Colours live in `palette.py` and nowhere else. A label may only be inked in one of
the five named roles; the generator rejects any other.

The brand is `spec.BRAND` — one string for the whole family rather than a field
each spec repeats, so the mark on the panel cannot disagree with `brand` in
`plugin.json`. The logo beside it is not drawn: it is a `Label` with `mirror`
set, so it is the genuine glyph in the same face as the wordmark, stays sharp at
any zoom, and needs neither a raster asset nor a font parser. Any label can be
mirrored the same way, through `textMirrored()` in the text kit, which flips the
alignment with the axis so the glyph lands where it would have unmirrored.
All twenty modules publish under one plugin, brand **Moon Technologies**, author
**Taxxess**.

## The text kit

Rack's nanosvg discards `<text>`, so **every word on a family panel is drawn at
runtime**. That makes text drawing the most repeated thing these modules do, and
it used to be the most duplicated: each one carried its own `loadFont` /
`nvgFontFaceId` / `nvgFontSize` / `nvgTextAlign` / `nvgTextLetterSpacing`
sequence, and they had drifted. Retroactive even shipped a comment explaining
that letter spacing must not leak into the segment face — a bug the call site
should never have been able to write.

So the whole vocabulary is emitted into `src/PanelTheme.hpp`, and a module uses
nothing else:

| symbol | what it is |
|---|---|
| `Face` | `Ui`, `Mono`, `Seg` — the three faces, by role rather than by path |
| `TextStyle` | face + size + tracking + ink + alignment, as one value. `.inked()`, `.sized()`, `.aligned()` return variants |
| `text(vg, style, x, y, str)` | draws one run and clears the tracking it set. Returns the x advance |
| `textWidth(vg, style, str)` | measures, leaving no state behind |
| `textMirrored(...)` | a run reflected about its anchor — the maker's mark |
| `FittedText` | a string ellipsized to a width, **cached**. Binary-searches codepoint boundaries rather than trimming a character at a time |
| `segValue(...)` | the read-out idiom: numerals in DSEG7 over an all-segments-on ghost, unit in Share Tech Mono |

Two of the nanovg text settings are sticky global state. Going through
`TextStyle` is what makes it impossible for one run to restyle the next.

`FittedText` is also where the performance lives. A per-character trim costs one
text-shaping pass per character dropped — forty-odd per long title, per row, per
frame. The binary search takes that to about six, and the cache takes the
steady state to zero. Its key includes the transform scale, because
`nvgTextBounds` quantizes to the rasterized glyph grid and the same string
measures a shade wider at a different rack zoom.

**If a panel needs something the kit does not have, add it to the kit** and
regenerate every panel. Do not open-code it in one. `build()` prints every
place in `src/` that still calls the raw nanovg text API — advisory, never
fatal, because the gap it is pointing at might be a real one.

## Density

Two metric scales, in `layout.py`. Same rules, same idioms — only the gaps change.

* `regular` — panels with room. Uncertainty Policy, PatchAudit.
* `compact` — panels at capacity. Retroactive: six rows of controls and a
  read-out at 15 HP.

If a panel does not fit at `compact`, it has too many rows. Lose a row; do not
invent a third scale.

## Writing a spec

```python
from panelkit import *

P = Panel(slug="Example", title="EXAMPLE", form="FORM 1040", glass=Glass(h=9.2))

P.sections = [
    Section("EXPOSURE", rows=[
        Row([BigKnob("amount", "VARIANCE"),
             BigKnob("count",  "TRANSFERS")]),
    ]),
]
P.footer = [Row([Jack("trig", "TRIG", ink="MINT")], y=118.6)]

if __name__ == "__main__":
    raise SystemExit(build(P))
```

You give rows; the solver gives back everything else — every x, every y, and how
many HP the panel is. Column centres, block extents, label baselines and recessed
wells are all derived, so a block always hugs its contents, a knob is never nearer
its own frame on one panel than on another, and a label is never nearer its
control. The only number you should ever type is a row the bottom screws pin.

Shorthands: `BigKnob Knob Trim Slider Button Bezel Jack Light Switch Switch3`,
plus `Widget(...)` for anything else. `Switch` is the two-position `CKSS`,
`Switch3` the three-position `CKSSThree` — which is half again as tall, so a
row carrying one is taller than a row of knobs and the solver will tell you if
that no longer fits. Each takes a name and a label; a second positional *number*
pins an x, for the rare control an outside constraint fixes.

Useful keywords:

| | |
|---|---|
| `primary=True` | the lime ring; one control per panel |
| `light="name"` | a lit label |
| `steps=6` | a knob that is really a six-position selector, so it gets detents |
| `side="above"/"below"` | override the label rule for one widget |
| `col=3` | which column of its section this control stands in; defaults to its index in the row |
| `pair=True` | this control owns the widget directly below it and shares a label with it |
| `Row(..., silent=True)` | the row above already labels it |
| `Row(..., pair=True)` | every column with a partner below is a pair |
| `Row(..., own_grid=True)` | this row's columns are its own, not the section's |
| `Section(..., groups=(3, 3, 3, 3))` | name the runs, where width alone cannot see them |
| `Section(..., divide_after=(0,))` | a subtotal rule |
| `Section(..., caption_light="name")` | a light beside the caption |

## Columns, runs and gutters

`hp` defaults to `"auto"`, and that is how every panel in the family is written.
The horizontal solver works out what the rows need and the panel comes out exactly
that wide — so a panel is never a millimetre short of its own contents, and never
carries width it has no use for.

The unit is the **column**. A section's rows share one grid, which is what puts a
trimpot over its own jack without either of them being given a coordinate. A
column is as wide as the widest thing any row puts in it — the well and its ring,
the primary seal, or the label, whichever reaches furthest — and the reach is
measured on each side separately, because a lit label's light hangs off one end
only.

Columns are then spaced in **runs**. Comparable columns — the same class of
hardware, or near enough the same width — form one run and are spaced evenly,
since even spacing is only even if the things being spaced are the same size.
Where a row changes gear (three knobs, then two switches) the run breaks, each run
keeps its own even pitch, and the slack collects in a **gutter** between them.
That is the one place on a row where empty space reads as deliberate rather than
as a mistake, and it is what keeps the switches on a mixed row together instead of
drifting apart to match a knob's cell.

What this replaced was `P.cols(n, margin)`: evenly spaced centres, with the margin
typed by hand. A centre says nothing about how much of the panel a widget actually
covers, so one margin had to serve a big knob and a switch alike — and the knob
then hung over its own block frame while the switches floated in dead air. Every
panel in the family had a version of it.

`P.cols` is still there for a panel laid out against something outside the grid —
a control that has to line up with a field of a live display — but a section
either names every x or names none, and the linter says so.

## What the linter checks

`build()` refuses to write anything if the panel would be wrong, and says why:

* the panel is over capacity, in millimetres — vertically, or too narrow for its
  own rows, in which case it says how many HP would do
* a label or widget running off an edge, under a corner screw, or into the
  foot ribbon between the bottom screws
* two labels overlapping, **or closer than 1.55 mm**, which reads as one run
* a label overlapping a widget's **well**, or standing less than 0.7 mm off it
* two wells overlapping (their rings touching), including the lights that lit
  labels and captions carry
* a widget's ink within 0.7 mm of its block's frame — the well, the primary ring,
  everything drawn around it. Letting the ring merely *touch* the frame is what
  put Dividend's FREQ knob through the left edge of PAYOUT: the check passed, and
  the panel still read as broken
* a label within 0.5 mm of its block's frame
* a section block running into the masthead or the footer band
* a section mixing pinned and solved x positions
* two widgets sharing a name (each becomes a constant in the header)
* more than one control marked `primary`

Every clearance is measured from a widget's well and ring, not its art, and a
label's gap is to the top of its capitals, not its baseline. A lit label's
light is taller than its letters, so a row carrying one moves as far as the
light needs. A first row labelled above holds off the caption by a line. A
sparse panel is *justified*: slack above the footer band is shared out among
the row and block gaps, up to a cap, so no panel ends in a dead pale strip.

A spec that raises (rather than reports a problem) also writes nothing, and
`make panel` stops at it -- check the exit status, not just the output.

`--force` writes anyway. Only useful for looking at the damage.

Separately, and never fatally, `build()` scans `src/` for widgets
drawing text through the raw nanovg API instead of the kit, and names the file
and line. See **The text kit**.

## Two constraints everything is written around

1. **Rack discards most of SVG.** Between nanosvg and Rack's own `svgDraw()` the
   renderer drops `<text>`, `<pattern>`, `<use>`, `<clipPath>`, `<mask>`,
   `<image>`, `<style>`/`class=`, `stroke-dasharray`, and every gradient stop but
   the first and last. So a panel is flat fills and explicit geometry only, and
   every label is drawn at runtime from `LABELS` in the generated header.
   `display:none` **is** honoured, which is what keeps the components layer out of
   the render.

2. **The bottom screws fix the floor.** `RACK_GRID_HEIGHT - RACK_GRID_WIDTH` puts
   their top edge at 123.61 mm, so a jack centred below 118.6 runs its collar
   under one. Anything spanning the full width down there — a progress trough —
   has to be inset past x = 10.16 mm and its mirror.

## The VCV render

`--vcv` runs `Rack --screenshot 3 --user <scratch>`, which renders every model of
every loaded plugin through the real widget tree and exits. Pointing `--user` at
a scratch directory holding a single symlink to this plugin means Rack
loads Core, Fundamental and only that plugin: seconds rather than minutes, and
the real Rack install is never touched. The symlink points at the repo's own
build directory, so there is no install step either.

Rack occasionally aborts bringing up a second GL context alongside a running
Rack; `rack.py` retries and, if it still fails, says so rather than reporting an
empty directory. On Windows, close Rack first — only that build takes a
single-instance mutex.

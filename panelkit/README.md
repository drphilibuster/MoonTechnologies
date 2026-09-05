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

The three modules ship as one plugin, so all three panels' C++ is linked into one
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
and draws one module's silkscreen on all three panels.

## The design language

Every panel in the family is the same object: an official form, on a deep green
board, filled in on raised felt.

| element | rule |
|---|---|
| board | `INK`, the whole face |
| masthead | `BAND` to y=9.0, closed by a lime rule and a muted rule. Title in `PAPER`, auto-sized to clear both top screws. The form number sits under it on the right in `SAGE` |
| section block | `FELT`, inset 3 mm, rounded 1.6, with a 7 × 0.5 mm **lime index tab** at its top-left |
| section caption | `SAGE`, 6 px, centred on the block, 3.3–3.6 mm below its top edge |
| control label | `PAPER` for a primary control, `SAGE` for a secondary. **Below** knobs, buttons and switches; **above** jacks, where a cable would cover it |
| paired control | a trimpot directly over its jack shares one label, placed above the pair |
| lit label | a small light immediately right of a label names what it reports |
| the primary action | exactly one control per panel wears a lime ring |
| trace | a lime wire between controls the panel wants to relate, breaking itself around any label it crosses |
| read-out | `GLASS` well under the masthead, `RULE` border. Words in Share Tech Mono, numerals in DSEG7 |
| footer band | `BAND`, with a **mint** index tab. Holds the I/O and anything that leaves the module |
| hardware | brass hex screws and brass-collared jacks; outputs ring mint |
| masthead footing | the mark, then the brand, bottom-left; the form number bottom-right — a form's issuing office and its number |
| the mark | a dollar sign, backwards: the real ASCII `$` set in the panel's own face and drawn through a `scale(-1, 1)`, in `LIME` |
| form number | every panel carries a real IRS form number matching its verb — `FORM 1040-X` (amended return), `SCHEDULE UTP` (uncertain tax position), `FORM 4564` (document request) |

Colours live in `palette.py` and nowhere else. A label may only be inked in one of
the five named roles; the generator rejects any other.

The brand is `spec.BRAND` — one string for the whole family rather than a field
each spec repeats, so the mark on the panel cannot disagree with `brand` in
`plugin.json`. The logo beside it is not drawn: it is a `Label` with `mirror`
set, so it is the genuine glyph in the same face as the wordmark, stays sharp at
any zoom, and needs neither a raster asset nor a font parser. Any label can be
mirrored the same way, through `textMirrored()` in the text kit, which flips the
alignment with the axis so the glyph lands where it would have unmirrored.
All three modules publish under one plugin, brand **Moon Technologies**, author
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
regenerate all three panels. Do not open-code it in one. `build()` prints every
place in `src/` that still calls the raw nanovg text API — advisory, never
fatal, because the gap it is pointing at might be a real one.

## Density

Two metric scales, in `layout.py`. Same rules, same idioms — only the gaps change.

* `regular` — panels with room. Uncertainty Policy, PatchAudit.
* `compact` — panels at capacity. Retroactive: seven rows of controls and a
  read-out at 12 HP.

If a panel does not fit at `compact`, it has too many rows. Lose a row; do not
invent a third scale.

## Writing a spec

```python
from panelkit import *

P = Panel(slug="Example", title="EXAMPLE", form="FORM 1040", hp=12,
          glass=Glass(h=9.2))
C2 = P.cols(2, 17.0)

P.sections = [
    Section("EXPOSURE", rows=[
        Row([BigKnob("amount", C2[0], "VARIANCE"),
             BigKnob("count",  C2[1], "TRANSFERS")]),
    ]),
]
P.footer = [Row([Jack("trig", P.w / 2, "TRIG", ink="MINT")], y=118.6)]

if __name__ == "__main__":
    raise SystemExit(build(P))
```

You give rows; the solver gives back every y. Block extents, label baselines and
recessed wells are all derived, so a block always hugs its contents and a label
is never closer to its widget on one panel than on another. The only vertical
number you should ever type is a row the bottom screws pin.

Shorthands: `BigKnob Knob Trim Slider Button Bezel Jack Light Switch Switch3`,
plus `Widget(...)` for anything else. `Switch` is the two-position `CKSS`,
`Switch3` the three-position `CKSSThree` — which is half again as tall, so a
row carrying one is taller than a row of knobs and the solver will tell you if
that no longer fits. Useful keywords: `primary=True` (the lime ring),
`light="name"` (a lit label), `side="above"/"below"` (override the rule for one
widget), `Row(..., silent=True)` (the row above already labels it),
`Row(..., label_side="above")` (the paired idiom), `Section(..., divide_after=(0,))`
(a subtotal rule), `Section(..., caption_light="name")`.

Panels laid out as a form rather than as controls — PatchAudit — use `Plate`,
`extra_blocks` and `band_footer` instead of rows, and export their millimetres
through `Panel.metrics`, which become `panel::NAME` constants in the header.

## What the linter checks

`build()` refuses to write anything if the panel would be wrong, and says why:

* the panel is over capacity, in millimetres
* a label or widget running off an edge, or under a corner screw
* two labels overlapping, or a label overlapping a widget
* a section block running into the masthead or the footer band
* two widgets sharing a name (each becomes a constant in the header)
* more than one control marked `primary`

Labels within a row share one baseline, taken from the tallest widget on it, so a
row of mixed hardware — a switch beside a trimpot — does not read as though the
type had slipped. The `primary` ring counts as ink for every clearance: it has to
clear the caption above the row and its own label below it.

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

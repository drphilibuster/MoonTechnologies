# Repainting the panels in Illustrator

`make art` writes into `art/templates/`:

* `<Module>.svg` - one per panel spec, with the panel's **current artwork as
  editable vectors**, already split into layers, plus guide layers on top;
* `_Hardware_ScrewHex.svg` and `_Hardware_Port*.svg` - the screw and the eight jacks,
  one layer per shape;

and `art/Moon Technologies.ase`, the palette as Adobe swatches (Window > Swatches >
menu > Open Swatch Library > Other Library).

They are generated from `tools/panels/*.py` by `tools/art_templates.py`, so they
always match the *solved* layout: change a spec, run `make art`, and the template
follows. Never commit one (`art/templates/` is gitignored). Needs nothing but
Python; no Illustrator or Rack is involved in making them.

## Opening one

File > Open the `.svg`. The artboard is the panel at true size (HP x 5.08 mm by
128.5 mm); set Units to millimetres. Each top-level group becomes a layer, each
nested group a group inside it.

If Illustrator asks, choose *Convert* for fonts; the only text in a template is on
the GUIDES layers.

## The layers (bottom to top)

**`EXPORT_01` ... `EXPORT_14`** are the panel. One layer per stage of
`panelkit/render.py`, in the order it draws them, so the stack *is* the draw order:

| layer | what's on it |
|---|---|
| 01 Paper | the face |
| 02 Masthead band | the dark band, its braid and sage rule |
| 03 Footer band | the band, mint tab, and the braid under the jacks |
| 04 Margin ribbons | the braided strands down each side |
| 05 Readout glass | the display well, where the panel has one |
| 06 Section blocks | one group per block (`Block_1_<caption>`): plate, frame, tab, scrolls |
| 07 Rules | subtotal rules with beads |
| 08 Plates | rectangular fields and list wells |
| 09 Run boxes | the box round a run named once |
| 10 Pair ties | hairline tying a trimpot to its jack |
| 11 Wells | the dark seat behind every control |
| 12 Detents | stepped-knob arcs |
| 13 Primary rings | the double ring on the one primary control |
| 14 Traces | engraved wires and rosettes |

Edit, redraw or replace anything on these layers. Keep a stage's shapes on its
layer: that is the structure an importer will read back. Empty layers are fine.

**`GUIDES_*`** are what goes on top of the art when Rack draws the module. They are
not the panel and nothing on them is ever exported.

| layer | what it is |
|---|---|
| Alignment_lines | hairlines through every control centre, the screws, the bands and blocks. Select the layer's contents and press **Cmd-5** to make real guides |
| Limits | band, block and read-out extents with their mm ranges; the full-width floor |
| Screws | the four corner screws and a keep-clear ring |
| Seats | the well behind each control, and the primary ring |
| Controls | every knob/switch (red), jack (green) and light (magenta) at true size, named as in the C++ |
| Words_on_DARK_ground | each label drawn in pale ink, in its real colour on the dark ground it needs |
| Words_on_PALE_ground | each label drawn in dark ink, in its real colour on the paper |

## What Rack keeps (so what to draw)

Rack renders panels through nanosvg and discards a lot. Draw with:

* **filled and stroked paths, rectangles, circles, polygons** - with plain solid fills;
* opacity on any of them (that is how the guilloche is made);
* a gradient only if it has **two stops**; everything in between is lost.

Do **not** use, or expand/outline before saving: type (`<text>` is dropped - every word
is drawn at runtime from the spec, which is what the Words layers show), patterns,
clipping or opacity masks, dashed strokes, brushes and symbols (expand them), live
effects and filters, blends, raster images, `<use>`/symbols.

Save as **SVG 1.1, Presentation Attributes** (not *Internal CSS* - Rack drops
`<style>` and `class=`), outline text, no responsive. Keep the artboard at the
panel size and the `viewBox` at 0 0 w h in millimetres.

## Rules that keep the family together

* **Ink follows ground.** The words are fixed: pale ink on bands and displays, dark
  ink on the paper and on section blocks. Whatever you put behind a word has to keep
  that contrast. The two Words layers are your proof.
* **Stay in the palette.** Five anchors, the rest derived (`panelkit/palette.py`).
* **Nothing straddles an edge or a screw**, and nothing may span the full width below
  **123.61 mm** or reach in from either side past **10.16 mm** at that height.
* **Seats mark where controls go.** Don't move or resize one; the solver owns every
  position. If a panel's layout should change, change its spec.

## Hardware

Each `_Hardware_*.svg` is the stock canvas (screw 15 x 14.9989 px, jack 23.7 px), one
layer per shape bottom to top, with the radii of today's art as rings. **Keep the
canvas size exactly**: Rack sizes the widget from the file. For a jack, the layers that
change between variants are the Throat band (brass in, mint out), the Timing line
(trigger ports only) and the Collar (brass, or gold on a Main port).

## Getting art back into Rack

```bash
python3 tools/art_import.py Toll.svg          # the module is the file name; or: ... Toll.svg Toll
make panel-Toll && make -j8 && make vcv-Toll  # redraw the panel, then see it in Rack
make install                                  # and restart Rack
```

Save from Illustrator with **File > Save As > SVG**, *SVG 1.1*, *Presentation Attributes*,
*Outline text*, and leave the artboard alone. The importer then:

1. **Reads only the `EXPORT_NN_*` layers.** Everything else, GUIDES included, is
   ignored. A stage is a layer: put a stage's shapes on its layer, and rename a layer's
   suffix if you like, but keep `EXPORT_NN`. A layer you leave out stays *generated*, so
   art can arrive a stage at a time. A layer you leave **empty** or **hidden** draws
   nothing for that stage; if Illustrator drops an empty layer on save, say so with
   `--clear 12,13`.
2. **Cleans the art to what Rack draws**, rewriting Illustrator's CSS classes, `style=`
   attributes, `<use>` instances and nested transforms into plain shapes, and rescaling
   the artboard (Illustrator writes points) to millimetres.
3. **Refuses what Rack would silently get wrong**, naming the object, its layer and the
   Illustrator command that fixes it: clipping masks, opacity masks, live effects, dashed
   strokes, blend modes, type, raster images, patterns. Nothing is written until it is
   clean.
4. **Warns** about a gradient with more than two stops (Rack keeps the first and last),
   a colour outside the family palette, an object past the panel edge, and art across the
   full width below the bottom screws.
5. **Checks every word against the ground under it**, on a render of the panel with your
   art in it: pale ink needs a dark ground, dark ink a pale one (3:1). It reports only
   words *your art* broke, never ones that were already marginal.
6. **Stores the result as `art/panels/<Module>.svg`**, which is source and **is
   committed**. `make panel` draws the panel from it, stage by stage, from then on; the
   components layer, the headers and the previews are still generated.

`--dry-run` checks without writing. `--strict` makes warnings errors. Add `--clear 12,13`
to draw those stages empty.

### Staying in step with the spec

An imported panel is drawn against the layout the solver produced *then*. The file records
a signature of it, and `make panel` warns if a spec change has moved a control since. Run

```bash
make art-status            # which panels carry imported art, and which are stale
make art                   # the template now contains your art, on the new layout
```

then adjust in Illustrator and import again: the template is generated from the current
art, so the loop is *template, edit, import, repeat*, not start over.

`python3 tools/art_import.py --reset Toll` deletes the imported art and goes back to the
generated panel.

### The screw and the jacks

The same command takes `_Hardware_ScrewHex.svg` or any `_Hardware_Port*.svg` and stores
it as `art/panels/_Hardware_<Name>.svg`; `make panel` then writes `res/<Name>.svg` from it.
The artboard must be the stock canvas (screw 15 x 14.9989, jack 23.7).

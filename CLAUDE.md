# Moon Technologies

One VCV Rack 2 plugin, slug `MoonTechnologies`, brand **Moon Technologies**,
author **Taxxess**. Twenty-two modules sharing one panel pipeline: the three
originals (`PatchAudit`, `Retroactive`, `UncertaintyPolicy`), seven built to
order (`Dividend`, `TaxBracket`, `Racketeer`, `Gross`, `Amortization`,
`Repossession`, `Collusion`), one expander (`ScheduleA`, which attaches to
`Repossession`) and eleven Modular-in-a-Week banks (`SixFigures`,
`Garnishment`, `Consolidation`, `Installment`, `Volatility`, `Deduction`,
`AuditLogic`, `Kickback`, `PaymentSchedule`, `SignHere`, `Diversified`).

## VCV Rack UI: use the tool, always

**All panel and UI work goes through `panelkit/`. No exceptions, and no
per-module UI code written from scratch in a session.**

Doing UI by hand per module is what produced the drift this rule exists to
prevent: the modules each grew their own palette, screw generator, silkscreen
table, and their own `loadFont` / `nvgFontFaceId` / `nvgFontSize` /
`nvgTextAlign` / `nvgTextLetterSpacing` sequence — and all of them diverged. One
module had a letter-spacing leak documented in a comment rather than fixed,
because the call site owned state it should never have touched.

### The rule

1. A module describes its panel once, in `tools/panels/<Module>.py`. That spec
   is the only file you edit for layout. It names controls, rows and sections —
   not coordinates: the solver works out every x, every y, and how many HP the
   panel is (`hp` defaults to `"auto"`). A millimetre typed into a spec is a
   millimetre that will be wrong on the next change; the only one that belongs
   there is a row the bottom screws pin.
2. `res/*.svg`, `src/PanelTheme.hpp`, `src/<Module>/Panel.hpp` and
   `tools/previews/*` are **generated**. Never hand-edit them. `make panel`
   regenerates; a hand edit is destroyed on the next run, and CI fails the push.
3. **Text is drawn through `panel::` and nothing else.** `PanelTheme.hpp` emits
   the whole vocabulary — `TextStyle`, `text()`, `textWidth()`, `textMirrored()`,
   `FittedText`, `segValue()`. A widget must never call `nvgFontFaceId`,
   `nvgFontSize`, `nvgTextLetterSpacing`, `nvgTextBounds`, `nvgText` or
   `loadFont` directly. `make panel` prints every place that does.
4. **If the kit can't express what a panel needs, improve the kit** —
   `panelkit/emit.py` for anything emitted into the headers,
   `panelkit/render.py` for artwork, `panelkit/layout.py` for the solver,
   `panelkit/palette.py` for colour (five anchors, everything else derived).
   Then regenerate every panel so they all get it. Do not open-code the gap in
   one module.
5. A UI improvement — including a performance one — belongs in the tool by
   default, so every panel gets it at once. Ellipsizing, text measurement
   caching and the letter-spacing discipline all live in the kit for this reason.
6. **Ink follows ground.** A spec names a label's *role* (`PAPER` primary,
   `SAGE` secondary, `LIME`, `MINT`, `CLAY`); the solver tags where it sits and
   the emitter picks the dark or pale variant. Module C++ drawing its own text
   does the same by hand: `panel::PAPER`/`SAGE`/`LIME` on a dark display,
   `panel::INK`/`SAGE_DARK`/`LIME_DARK` on the pale face or a `FELT` plate.

### Two headers, and why

`src/PanelTheme.hpp` is shared and entirely `inline`/template: one definition for
the whole plugin. `src/<Module>/Panel.hpp` is that panel's own numbers, all
`static`: one copy per translation unit, so twenty silkscreen tables cannot
collide. Both are `namespace panel`, so call sites are unchanged.

**A translation unit may include exactly one `Panel.hpp`.** Two is a
duplicate-definition error, which is the loud failure you want — making the
per-panel data `inline` instead would link fine and silently draw one module's
silkscreen on every panel.

### After any UI change

```bash
make panel && make -j8 && make vcv-preview
```

Look at `tools/previews/<Module>.png` before calling a panel done — it goes
through the real widget tree, so it is the only preview that cannot lie. Do this
for **every** module whose header the change touched, not just the one you were
working in; a `panelkit/` change reaches all twenty.

See `panelkit/README.md` for the design language, the spec API and what the
linter checks.

## Always install to Rack 2 Pro after building

A build that only lands in the repo is a build only one of us can see. **After
any change, `make install` it**, so James can open Rack and try the same binary
that was just built:

```bash
make -j8 && make install
```

That copies `dist/MoonTechnologies-<ver>-mac-arm64.vcvplugin` into
`~/Library/Application Support/Rack2/plugins-mac-arm64/`. Rack unpacks it over
the existing plugin directory on its next start and deletes the archive
(`Rack/src/plugin.cpp:225-246`), **so Rack has to be restarted before the change
is live** — say so rather than letting a stale panel get mistaken for a broken
one. A screenshot showing old behaviour after a fix usually means exactly this.

## Slugs are permanent

`MoonTechnologies` and the twenty-two module slugs listed at the top of this file.
Changing any of them orphans every saved patch that used it: Rack's fallback table
(`Rack/src/plugin.cpp:374`) is maintained by VCV, not by plugin authors.

## VCV Rack patch compatibility

Rack 2 opens pre-2.0 patches — `patch::Manager::load` copies a non-zstd `.vcv`
straight to `patch.json` (`Rack/src/patch.cpp:306`) and the loaders below it
carry legacy branches back to v0.3. There is no version gate anywhere in Rack's
load path. Do not add one; the only thing that stops an old patch running is its
plugins never having been ported.

## Adding a module

`src/<New>/<New>.cpp` with a `Model*`, declared in `src/plugin.hpp` and added in
`src/plugin.cpp`; an entry in `plugin.json` with tags from `Rack/src/tag.cpp`; a
spec in `tools/panels/<New>.py`; a manual in `docs/<New>.md`. The Makefile globs
`src/*/*.cpp` and `tools/panels/*.py`, so nothing there needs touching.

## Local checkouts (outside this repo)

`../Rack/` is a full VCV Rack 2.6.6 source tree and `../Rack-SDK/` the plugin
SDK — read them to answer questions about Rack behaviour rather than guessing.
`../Rack/Rack` is a runnable binary; `Rack -h` is headless.

## Library submission

`docs/LIBRARY.md` tracks what is done and what is left. Do not change the plugin
slug, the licence layout or the manifest shape without reading it.

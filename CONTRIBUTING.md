# Contributing

## Before you change anything visual

**All panel work goes through `panelkit/`.** A plugin describes its panel once,
in `tools/panels/<Module>.py`; `res/*.svg`, `src/PanelTheme.hpp`,
`src/<Module>/Panel.hpp` and both previews are generated from it and are
destroyed by the next `make panel`.

Text is drawn through `panel::` and nothing else. A widget must never call
`nvgFontFaceId`, `nvgFontSize`, `nvgTextLetterSpacing`, `nvgTextBounds`,
`nvgText` or `loadFont` directly — `make panel` prints every place that does.

If the kit cannot express what a panel needs, **improve the kit**
(`panelkit/emit.py` for the header, `panelkit/render.py` for artwork,
`panelkit/layout.py` for the solver) and regenerate every panel, so they
all get it. Do not open-code the gap in one module.

After any UI change:

```bash
make panel && make -j8 && make vcv-preview
```

and look at `tools/previews/<Module>.png` — that one is rendered by Rack itself,
through the real widget tree, so it is the only preview that cannot lie.

## Adding a module

1. `src/<NewModule>/<NewModule>.cpp`, declaring `Model* modelNewModule`.
2. Declare it in `src/plugin.hpp` and add it in `src/plugin.cpp`.
3. Add it to `plugin.json` — slug, name, description, tags from
   `Rack/src/tag.cpp`, `manualUrl`.
4. `tools/panels/<NewModule>.py` for the panel; `make panel-<NewModule>`.
5. `docs/<NewModule>.md`.

The Makefile globs `src/*/*.cpp` and `tools/panels/*.py`, so there is nothing to
add there.

**A translation unit may include exactly one `Panel.hpp`.** Each one defines this
panel's `HP`, `LABELS` and widget positions in `namespace panel`, as `static`, so
that twenty panels can be linked into one plugin without colliding. Including two
is a duplicate-definition error at compile time.

## Slugs are permanent

Changing a plugin or module slug orphans every saved patch that used it. Rack's
fallback table (`Rack/src/plugin.cpp:374`) can map an old slug to a new one, but
it is maintained by VCV, not by plugin authors. Pick a slug once.

## Tests

```bash
make test
```

The DSP and the label parser are Rack-free C++11 and are tested against a host
compiler under ASan and UBSan. Anything that can be tested without Rack should
be — the parts that need Rack are verified by `make vcv-preview`.

## Style

Match the surrounding code: tabs, Rack's brace style, and comments that explain
*why* rather than restating the line below. A comment describing a constraint
Rack imposes is worth writing; one describing what `for` does is not.

<!-- GENERATED FILE'S SOURCE. README.md is written by tools/readme.py from this,
     tools/readme/modules.md, plugin.json and the generated panel headers.
     Edit those, then `make readme`. Keep numbers out of the prose: the
     generator supplies the ones that are facts about the repo ({{count}},
     each module's HP), and a figure typed here is one that goes stale. -->
# Moon Technologies

[![Build](https://img.shields.io/github/actions/workflow/status/drphilibuster/MoonTechnologies/build.yml?branch=main&label=build)](https://github.com/drphilibuster/MoonTechnologies/actions/workflows/build.yml)
[![Release](https://img.shields.io/github/v/release/drphilibuster/MoonTechnologies?include_prereleases&label=release)](https://github.com/drphilibuster/MoonTechnologies/releases/latest)
[![Downloads](https://img.shields.io/github/downloads/drphilibuster/MoonTechnologies/total?label=downloads)](https://github.com/drphilibuster/MoonTechnologies/releases)
[![Licence](https://img.shields.io/badge/code-GPL--3.0--or--later-blue)](LICENSE.md)

{{Count}} modules for [VCV Rack 2](https://vcvrack.com), by **Taxxess**.

They share a panel language borrowed from money — a pale engraved note, sage
guilloche round every field, a scroll in every corner, a form number in the
masthead — because all of them are, one way or another, about filing something
and finding out what it cost you.

## Where they come from

**Most are built for this plugin.** Some are original; some are a piece of
hardware or a paper worked out properly — the Olegtron R2R as a real resistor
ladder, Curtis Roads' pulsar synthesis, Partch's tonality diamond. Where one is
modelled on something, it says so and says what. A few go further and run the
original machine's own firmware, on an emulation of its processors; the ROMs
are never in this repository, and each of those modules says what it needs.

**A large group starts from [Modular in a Week](https://www.youtube.com/@modularinaweek)**,
Kristian Blåsol's DIY course. Each takes a day's worth of separate builds and
puts them on one panel with their character selectable and their originals as
the defaults — so Day 2's three VCAs are one dual VCA with a MODE switch rather
than three modules. These are marked below with the day they came from.

**They are starting points, not destinations.** Every one of them will keep
growing, and some already have: [Six Figures](docs/SixFigures.md) is still a
faithful consolidation of its day, while [Kickback](docs/Kickback.md) has a
clock, a Euclidean pattern engine, per-voice clock ratios and a burst mode that
the drum folder it came from has nothing to say about. Each entry says which it
is. The credit belongs to the course either way; the deviations are ours, and
so is anything wrong with them.

**Some grew out of the family** — an expander, a voice that turned out to
deserve its own panel, and a bank doubled into a module of its own.

**The picture has its own modules.** [Transmittal](docs/Transmittal.md)
publishes video from anything in the plugin to a compositor — Syphon on macOS,
read straight into TouchDesigner — so Repossession's footage can go to a
projector while the same patch is making the sound. And
[Projection](docs/Projection.md) makes the video in the first place: a scope, a
spectrum and a warped field, driven by audio and CV.

## The modules

*(Every panel below is rendered by Rack itself, not mocked up.)*

{{gallery}}

## Built for this plugin

{{group:built}}

## Hardware, running its own firmware

{{group:hardware}}

## About the picture

{{group:video}}

## Out of Modular in a Week

Marked with the day each came from, and how far it has moved since.

{{group:miaw}}

## Grown out of the family

{{group:grew}}

---

## Install

Download the `.vcvplugin` for your platform from the
[latest release](https://github.com/drphilibuster/MoonTechnologies/releases/latest),
drop it in your Rack plugins folder, and restart Rack.

**Full step-by-step for Windows, macOS and Linux — including where that folder
is on each — is in [docs/INSTALL.md](docs/INSTALL.md).**

Builds are published for {{platforms}} — every platform VCV ships Rack for.

> Not yet in the VCV Library. See [Library status](#library-status) below.

## Build

```bash
make -j8 && make install     # then restart Rack
```

Needs the [Rack 2 SDK](https://vcvrack.com/downloads) and `jq`. Per-platform
toolchain setup is in [docs/BUILDING.md](docs/BUILDING.md).

## Panels

Every panel is generated from a single spec — the artwork, the runtime
silkscreen, the C++ geometry and both previews all come out of
`tools/panels/<Module>.py`, so the panel art and the code that draws over it
physically cannot drift apart. `res/*.svg`, `src/PanelTheme.hpp` and
`src/<Module>/Panel.hpp` are outputs; never hand-edit them.

```bash
make panel          # regenerate every panel
make vcv-preview    # ... and render each through Rack itself
make readme         # rewrite this file from plugin.json and the panels
```

The design language, the spec API and what the linter enforces are documented in
[`panelkit/README.md`](panelkit/README.md).

This file is generated the same way: the module list, each module's width and
the gallery come from `plugin.json` and the panel headers, so none of them is
typed here. To change the wording, edit `tools/readme/`.

## Library status

Not yet submitted to the VCV Library. The repository is set up for it —
one plugin, one slug, manifest complete, GPL-3.0, reproducible builds through
VCV's own toolchain — and what remains is tracked in
[docs/LIBRARY.md](docs/LIBRARY.md).

Until then, install from [Releases](https://github.com/drphilibuster/MoonTechnologies/releases).

## Licence

Source code is **GPL-3.0-or-later**; the visual design of the panels is
**CC BY-NC-ND 4.0**; the "Moon Technologies" and "Taxxess" names and the maker's
mark are reserved. This is the same split VCV uses for its own Fundamental
plugin — fork the code freely, under your own brand.

See [LICENSE.md](LICENSE.md) for the exact terms and
[LICENSE-GPLv3.txt](LICENSE-GPLv3.txt) for the full GPL text.

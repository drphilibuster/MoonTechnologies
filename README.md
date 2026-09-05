# Moon Technologies

Three modules for [VCV Rack 2](https://vcvrack.com), by **Taxxess**.

They share a panel language borrowed from tax stationery — a green form you fill
in, a lime index tab on every block, a form number in the corner — because all
three are, one way or another, about filing something and finding out what it
cost you.

| | | |
| --- | --- | --- |
| <img src="tools/previews/PatchAudit.png" width="260"> | <img src="tools/previews/Retroactive.png" width="120"> | <img src="tools/previews/UncertaintyPolicy.png" width="160"> |
| **[PatchAudit](docs/PatchAudit.md)** · 26 HP | **[Retroactive](docs/Retroactive.md)** · 12 HP | **[Uncertainty Policy](docs/UncertaintyPolicy.md)** · 16 HP |

*(Panels above are rendered by Rack itself, not mocked up.)*

---

## The modules

### [PatchAudit](docs/PatchAudit.md) — *Utility, Visual*

Browse [Patchstorage](https://patchstorage.com) without leaving Rack. Search the
~9,200 published VCV patches, and before you open one, get an audit of it against
the modules you actually have: what is installed, what is available in the
Library, and what is genuinely gone. Import straight into your rack, or save to
disk.

### [Retroactive](docs/Retroactive.md) — *Effect, Delay, Granular*

A windowed sample-permutation effect. Take a window of consecutive samples and
emit them in a different order, leaving the windows themselves in time order — so
the melody, rhythm and phrasing play forward while every micro-chunk is
rearranged. Short windows are a timbral effect; long ones give the familiar
"reversed but still moving forward" sound. Clock-syncable, with eight permutation
modes and a freeze.

### [Uncertainty Policy](docs/UncertaintyPolicy.md) — *Utility, Random*

A knob and cable randomizer that reads the patch before it rolls. It knows what
each port carries and where the audio goes, auditions every roll, and quietly
withdraws the ones that killed the sound — so you are not spending the session on
roll → silence → undo → roll.

---

## Install

Download the `.vcvplugin` for your platform from the
[latest release](https://github.com/drphilibuster/MoonTechnologies/releases/latest),
drop it in your Rack plugins folder, and restart Rack.

**Full step-by-step for Windows, macOS and Linux — including where that folder
is on each — is in [docs/INSTALL.md](docs/INSTALL.md).**

Builds are published for `win-x64`, `mac-arm64`, `mac-x64` and `lin-x64` — the
four platforms VCV ships Rack for.

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
make panel          # regenerate all three
make vcv-preview    # ... and render each through Rack itself
```

The design language, the spec API and what the linter enforces are documented in
[`panelkit/README.md`](panelkit/README.md).

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
